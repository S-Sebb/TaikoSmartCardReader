#include "scard.h"
#include "constants.h"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <toml++/toml.h>

extern char module[];

SmartCard::SmartCard() : hContext(0), hCard(0), readerState{{nullptr}}, readerName(nullptr) {
    memset(&readerState, 0, sizeof(SCARD_READERSTATE));
}

SmartCard::~SmartCard() {
    if (hCard && connected) {
        disconnect();
    }
    if (readerName) {
        SCardFreeMemory(hContext, readerName);
    }
    SCardReleaseContext(hContext);
}

bool SmartCard::initialize() {
    if (const long lRet = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &hContext); lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to establish context: 0x%08X\n", __func__, module, lRet);
        return false;
    }
    // Read serverUrl from config.toml
    try {
        toml::table config = toml::parse_file("config.toml");
        serverUrl = config["amauth"]["server"].value_or("");
    } catch (const std::exception& e) {
        printError("%s, %s: Failed to read config.toml: %s\n", __func__, module, e.what());
        return false;
    }
    return setupReader();
}

bool SmartCard::connect() {
    int retryCount = 0;
    constexpr int maxRetries = 100;
    long lRet = 0;

    if (connected) {
        disconnect();
    }
    
    while (retryCount < maxRetries) {
        constexpr int retryDelay = 10;
        lRet = connectReader(SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1);
        if (lRet == SCARD_S_SUCCESS) {
            connected = true;
            return true;
        }
        if (lRet == SCARD_W_REMOVED_CARD) {
            if (!isCardPresent()) {
                printWarning("%s, %s: Card was removed!\n", __func__, module);
                return false;
            }
        }
        retryCount++;
        Sleep(retryDelay);
    }
    printError("%s, %s: Failed to connect to reader: 0x%08X\n", __func__, module, lRet);
    return false;
}

void SmartCard::disconnect() {
    if (hCard) {
        SCardDisconnect(hCard, SCARD_RESET_CARD);
        hCard = 0;
    }
    connected = false;
}

bool SmartCard::isCardPresent() {
    int retryCount = 0;

    readerState[0].dwCurrentState = SCARD_STATE_EMPTY;
    long lRet = SCardGetStatusChange(hContext, 0, readerState, 1);
    while (lRet == SCARD_E_SERVICE_STOPPED || lRet == SCARD_E_NO_SERVICE || lRet == SCARD_E_NO_READERS_AVAILABLE) {
        constexpr int retryDelay = 10;
        printWarning("%s, %s: Service stopped, no service or no readers available, attempting to reestablish context\n", __func__, module);
        if (!initialize()) {
            printError("%s, %s: Failed to reestablish context: 0x%08X\n", __func__, module, lRet);
            return false;
        }

        lRet = SCardGetStatusChange(hContext, 0, readerState, 1);
        retryCount++;
        if (constexpr int maxRetries = 100; retryCount >= maxRetries) {
            printError("%s, %s: Failed to get status change: 0x%08X\n", __func__, module, lRet);
            return false;
        }
        Sleep(retryDelay);
    }

    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to get status change: 0x%08X\n", __func__, module, lRet);
        return false;
    }

    return (readerState[0].dwEventState & SCARD_STATE_PRESENT) != 0;
}

void SmartCard::update() {
    int retryCount = 0;

    // Reset card info
    cardInfo.uid = "";
    cardInfo.accessCode = "";
    cardInfo.cardType = "empty";

    long lRet = SCardGetStatusChange(hContext, readCooldown, readerState, 1);
    if (lRet == SCARD_E_TIMEOUT) return;
    while (lRet == SCARD_E_SERVICE_STOPPED || lRet == SCARD_E_NO_SERVICE || lRet == SCARD_E_NO_READERS_AVAILABLE) {
        constexpr int retryDelay = 10;
        printWarning("%s, %s: Service stopped, no service or no readers available, attempting to reestablish context\n", __func__, module);
        if (!initialize()) {
            printError("%s, %s: Failed to reestablish context: 0x%08X\n", __func__, module, lRet);
            return;
        }

        lRet = SCardGetStatusChange(hContext, readCooldown, readerState, 1);
        retryCount++;
        if (constexpr int maxRetries = 100; retryCount >= maxRetries) {
            printError("%s, %s: Failed to get status change: 0x%08X\n", __func__, module, lRet);
            return;
        }
        Sleep(retryDelay);
    }

    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to get status change: 0x%08X\n", __func__, module, lRet);
        return;
    }

    handleCardStatusChange();
}

void SmartCard::poll() {
    if (!readerName) {
        return;
    }
    if (!connect()) {
        return;
    }

    if (!readATR()) {
        disconnect();
        return;
    }

    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO15693_PART3) {
        printInfo("%s (%s): Card protocol: ISO15693_PART3\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        printInfo("%s (%s): Card protocol: ISO14443_PART3\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K) {
        printInfo("%s (%s): Card protocol: FELICA_212K\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
        printInfo("%s (%s): Card protocol: FELICA_424K\n", __func__, module);
    }
    else{
        printError("%s (%s): Unknown NFC Protocol: 0x%02X\n", __func__, module, cardProtocol);
        disconnect();
        return;
    }

    const LPCSCARD_IO_REQUEST pci = activeProtocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    DWORD cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];

    // Send UID command
    long lRet = transmit(pci, uidCmd, sizeof(uidCmd), pbRecv, &cbRecv);
    if (lRet != SCARD_S_SUCCESS) {
        disconnect();
        return;
    }
    int card_uid_len = static_cast<int>(cbRecv) - 2;

    if (card_uid_len > 8) {
        printWarning("%s (%s): Taking first 8 bytes of UID\n", __func__, module);
        card_uid_len = 8;
    }

    // Convert pbRecv 0-8 to string
    cardInfo.uid = hexToString(pbRecv, card_uid_len);

    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        // Send Load Key command
        cbRecv = maxApduSize;
        lRet = transmit(pci, loadKeyCmd, sizeof(loadKeyCmd), pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        cbRecv = maxApduSize;
        // Send Auth Block 2 command
        lRet = transmit(pci, authBlock2Cmd, sizeof(authBlock2Cmd), pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        cbRecv = maxApduSize;
        // Send Read Block 2 command
        lRet = transmit(pci, readBlock2Cmd, sizeof(readBlock2Cmd), pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        // Convert pbRecv 6-16 to string
        const std::string block2Content = hexToString(pbRecv + 6, 10);
        printInfo("%s (%s): Block 2 content: %s\n", __func__, module, block2Content.c_str());
        lookUpCard(cardInfo.uid, block2Content);
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K || cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
        lookUpCard(cardInfo.uid, "");
    }

    disconnect();
}

bool SmartCard::readATR() {
    byte atr[32];
    DWORD atrLen = sizeof(atr);
    TCHAR szReader[200];
    DWORD cchReader = 200;
    if (const long lRet = SCardStatus(hCard, szReader, &cchReader, nullptr, nullptr, atr, &atrLen); lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to read ATR: 0x%08X\n", __func__, module, lRet);
        return false;
    }
    cardProtocol = atr[12];
    return true;
}

void SmartCard::handleCardStatusChange() {
    const DWORD newState = readerState[0].dwEventState ^ SCARD_STATE_CHANGED;
    const bool wasCardPresent = (readerState[0].dwCurrentState & SCARD_STATE_PRESENT) > 0;
    if (newState & SCARD_STATE_UNAVAILABLE) {
        printError("Card reader unavailable\n");
        Sleep(readCooldown);
    } else if (newState & SCARD_STATE_EMPTY) {
        printWarning("No card in reader\n");
    } else if (newState & SCARD_STATE_PRESENT && !wasCardPresent) {
        printInfo("Card inserted\n");
        poll();  // Assuming `poll` handles detailed card interaction.
    }
    readerState[0].dwCurrentState = readerState[0].dwEventState;

    Sleep(readCooldown);
}

bool SmartCard::setupReader() {
    auto pcchReaders = SCARD_AUTOALLOCATE;
    switch (const long lRet = SCardListReaders(hContext, nullptr, reinterpret_cast<LPTSTR>(&readerName), &pcchReaders)) {
        case SCARD_E_NO_READERS_AVAILABLE:
            printWarning("%s, %s: No readers available\n", __func__, module);
            return false;
        case SCARD_E_NO_MEMORY:
            printError("%s, %s: Out of memory\n", __func__, module);
            return false;
        case SCARD_S_SUCCESS:
            break;
        default:
            printError("%s, %s: Failed to list readers: 0x%08X\n", __func__, module, lRet);
            return false;
    }
    if (pcchReaders > 0) {
        printInfo("%s, %s: Reader found: %s\n", __func__, module, readerName);
    }
    else {
        printError("%s, %s: No readers found\n", __func__, module);
        return false;
    }

    if (!sendPiccOperatingParams()) {
        return false;
    }

    return true;
}

bool SmartCard::sendPiccOperatingParams() {
    long lRet = connectReader(SCARD_SHARE_DIRECT, 0);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to connect to reader: 0x%08X\n", __func__, module, lRet);
        return false;
    }

    DWORD cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];
    lRet = SCardControl(hCard, SCARD_CTL_CODE(3500), piccOperatingParamCmd, sizeof(piccOperatingParamCmd), pbRecv, cbRecv, &cbRecv);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to send PICC operating parameters: 0x%08X\n", __func__, module, lRet);
        disconnect();
        return false;
    }
    printInfo("%s, %s: PICC operating parameters set\n", __func__, module);

    disconnect();
    memset(&readerState[0], 0, sizeof(SCARD_READERSTATE));
    readerState[0].szReader = readerName;

    return true;
}

long SmartCard::connectReader(DWORD shareMode, DWORD preferredProtocols) {
    const long lRet = SCardConnect(hContext, readerName, shareMode, preferredProtocols, &hCard, &activeProtocol);
    return lRet;
}

long SmartCard::transmit(LPCSCARD_IO_REQUEST pci, const BYTE* cmd, size_t cmdLen, BYTE* recv, DWORD* recvLen) {
    constexpr int maxRetries = 3;
    int retryCount = 0;
    long lRet = 0;

    while (retryCount < maxRetries) {
        lRet = SCardTransmit(hCard, pci, cmd, static_cast<DWORD>(cmdLen), nullptr, recv, recvLen);
        if (lRet == SCARD_S_SUCCESS) {
            return lRet;
        }
        if (lRet == SCARD_W_RESET_CARD || lRet == SCARD_W_REMOVED_CARD) {
            printWarning("%s, %s: Card was reset/removed, please leave the card on, retrying... 0x%08X\n", __func__, module, lRet);
            // Reconnect if the card was reset
            if (!connect()) {
                return lRet;
            }
        }

        retryCount++;
        Sleep(readCooldown);
    }

    printError("%s, %s: Failed to transmit: 0x%08X\n", __func__, module, lRet);
    return lRet;
}

// Helper function to handle the response data
size_t SmartCard::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

void SmartCard::lookUpCard(const std::string& uid, const std::string& accessCode) {
    CURLcode res = {};
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (CURL *curl = curl_easy_init()) {
        constexpr long timeoutSeconds = 15;
        constexpr int maxRetries = 3;
        // Add /api/LookUpCard to the server URL
        const std::string url = serverUrl + "/api/Cards/LookUpCard";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // POST a json object {"UId" : "uid", "AccessCode" : "accessCode"}
        nlohmann::json j;
        j["UId"] = uid;
        j["AccessCode"] = accessCode;
        const std::string postData = j.dump();

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // Set Content-Type header to application/json
        curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set up to receive the response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Set timeout
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

        int retryCount = 0;
        bool success = false;
        while (retryCount < maxRetries) {
            res = curl_easy_perform(curl);
            if (res == CURLE_OK) {
                success = true;
                break;
            }
            retryCount++;
        }

        if (!success) {
            printError("%s, %s: Failed to perform request: %s\n", __func__, module, curl_easy_strerror(res));
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            return;
        }

        // Parse the response
        // Example response: {"type":"bng","errors":[],"ids":{"bng":"30760120652285557236"},"info":{"bng":{"rand_num":0,"product":0,"app":0,"namco_id":0,"bcd":0}}}
        // Set cardInfo.cardType to type

        try {
            nlohmann::json response = nlohmann::json::parse(readBuffer);
            printInfo("CardLookUp response: %s\n", response.dump().c_str());
            cardInfo.cardType = response["cardType"].get<std::string>();
            cardInfo.accessCode = response["accessCode"].get<std::string>();
        }
        catch (const std::exception& e) {
            printError("%s, %s: Failed to parse response: %s\n", __func__, module, e.what());
        }

        // Always cleanup
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}

std::string SmartCard::hexToString(const BYTE* hex, const size_t len) {
    std::stringstream ss;
    ss << std::hex << std::uppercase;
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(hex[i]);
    }
    return ss.str();
}
