#include "scard.h"
#include "constants.h"
#include <iostream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>

extern char module[];

SmartCard::SmartCard() : hContext(0), hCard(0), readerState{{nullptr}}, readerName(nullptr) {
    memset(&readerState, 0, sizeof(SCARD_READERSTATE));
}

SmartCard::~SmartCard() {
    disconnect();
    if (readerName) {
        SCardFreeMemory(hContext, readerName);
    }
    SCardReleaseContext(hContext);
}

bool SmartCard::initialize() {
    long lRet = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &hContext);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to establish context: 0x%08X\n", __func__, module, lRet);
        return false;
    }
    return setupReader();
}

bool SmartCard::connect() {
    disconnect();
    long lRet = connectReader(SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to connect to reader %s: 0x%08X\n", __func__, module, readerName, lRet);
        return false;
    }
    return true;
}

void SmartCard::disconnect() {
    if (hCard) {
        SCardDisconnect(hCard, SCARD_LEAVE_CARD);
        hCard = 0;
    }
}

void SmartCard::update() {
    // Reset card info
    cardInfo.uid = "";
    cardInfo.accessCode = "";
    cardInfo.cardType = "Empty";

    long lRet = SCardGetStatusChange(hContext, INFINITE, readerState, 1);
    if (lRet == SCARD_E_TIMEOUT) return;
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
    connect();

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

    LPCSCARD_IO_REQUEST pci = activeProtocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    DWORD cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];

    // Send UID command
    long lRet = transmit(pci, uidCmd, sizeof(uidCmd), pbRecv, &cbRecv);
    if (lRet != SCARD_S_SUCCESS) {
        disconnect();
        return;
    }
    int card_uid_len = (int)cbRecv - 2;

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
        std::string block2Content = hexToString(pbRecv + 6, 10);
        lookUpCard(block2Content);
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K || cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
        lookUpCard(cardInfo.uid);
    }

    Sleep(readCooldown);

    disconnect();
}

bool SmartCard::readATR() {
    byte atr[32];
    DWORD atrLen = sizeof(atr);
    TCHAR szReader[200];
    DWORD cchReader = 200;
    long lRet = SCardStatus(hCard, szReader, &cchReader, nullptr, nullptr, atr, &atrLen);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to read ATR: 0x%08X\n", __func__, module, lRet);
        return false;
    }
    cardProtocol = atr[12];
    return true;
}

void SmartCard::handleCardStatusChange() {
    DWORD newState = readerState[0].dwEventState ^ SCARD_STATE_CHANGED;
    bool wasCardPresent = (readerState[0].dwCurrentState & SCARD_STATE_PRESENT) > 0;
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
}

bool SmartCard::setupReader() {
    auto pcchReaders = SCARD_AUTOALLOCATE;
    long lRet = SCardListReaders(hContext, nullptr, (LPTSTR)&readerName, &pcchReaders);
    switch (lRet) {
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
    } else {
        printInfo("%s, %s: PICC operating parameters set\n", __func__, module);
    }

    disconnect();
    memset(&readerState[0], 0, sizeof(SCARD_READERSTATE));
    readerState[0].szReader = readerName;

    return true;
}

long SmartCard::connectReader(DWORD shareMode, DWORD preferredProtocols) {
    long lRet = SCardConnect(hContext, readerName, shareMode, preferredProtocols, &hCard, &activeProtocol);
    return lRet;
}

long SmartCard::transmit(LPCSCARD_IO_REQUEST pci, const BYTE* cmd, size_t cmdLen, BYTE* recv, DWORD* recvLen) const {
    long lRet = SCardTransmit(hCard, pci, cmd, static_cast<DWORD>(cmdLen), nullptr, recv, recvLen);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to transmit: 0x%08X\n", __func__, module, lRet);
    }
    return lRet;
}

// Helper function to handle the response data
size_t SmartCard::writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void SmartCard::lookUpCard(const std::string& content) {
    CURL* curl;
    CURLcode res;
    std::string readBuffer;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if (curl) {
        // Set the URL for the request
        curl_easy_setopt(curl, CURLOPT_URL, "https://card.bsnk.me/lookup");

        // POST a json object {"card" : "content"}
        nlohmann::json j;
        j["card"] = content;
        std::string postData = j.dump();

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());

        // Set up to receive the response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

        // Perform the request, res will get the return code
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK)
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;

        // Parse the response
        // Example response: {"type":"bng","errors":[],"ids":{"bng":"30760120652285557236"},"info":{"bng":{"rand_num":0,"product":0,"app":0,"namco_id":0,"bcd":0}}}
        // Set cardInfo.cardType to type
        nlohmann::json response = nlohmann::json::parse(readBuffer);
        std::string cardType = response["type"];
        cardInfo.cardType = cardType;

        // Set access code to id
        std::string id = response["ids"][cardType];
        cardInfo.accessCode = id;

        // Always cleanup
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
}

std::string SmartCard::hexToString(BYTE* hex, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::uppercase;
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(hex[i]);
    }
    return ss.str();
}