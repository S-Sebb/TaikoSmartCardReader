#include "scard.h"
#include "constants.h"
#include <sstream>
#include <iomanip>

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
        const std::string accessCode = hexToString(pbRecv + 6, 10);
        if (!checkMifareAccessCode(accessCode)) {
            disconnect();
            return;
        }
    	cardInfo.accessCode = accessCode;
    } else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K || cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
    	BYTE uid[8];
    	for (int i = 0; i < 8; i++) {
    		uid[i] = static_cast<BYTE>(std::stoi(cardInfo.uid.substr(i * 2, 2), nullptr, 16));
    	}
    	const BYTE felicaReadBlock0Cmd[] = {
    		0xFFu, 0x00u, 0x00u, 0x00u, 0x13u,
			0xD4u, 0x40u, 0x01u,
			0x10u,
			0x06u,
			uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7],
			0x01u,
			0x0Bu, 0x00u,
			0x01u,
			0x80u, 0x00u
		};
    	cbRecv = maxApduSize;

    	lRet = transmit(pci, felicaReadBlock0Cmd, sizeof(felicaReadBlock0Cmd), pbRecv, &cbRecv);
	    if (lRet != SCARD_S_SUCCESS) {
			printError ("%s (%s): Failed to read FeliCa S_PAD 0: 0x%08X\n", __func__, module, lRet);
			disconnect();
			return;
		}
    	// Check status code 0 and status code 1
    	if (pbRecv[cbRecv - 21] != 0x00 || pbRecv[cbRecv - 20] != 0x00) {
    		printError("%s (%s): Failed to read FeliCa S_PAD 0: 0x%02X, 0x%02X\n", __func__, module, pbRecv[cbRecv - 20], pbRecv[cbRecv - 19]);
    		disconnect();
    		return;
    	}

    	// Take S_PAD 0 data
    	std::vector<uint8_t> spad0Content;
    	spad0Content.reserve(16);
    	for (int i = 0; i < 16; i++) {
			spad0Content.push_back (pbRecv[cbRecv - 18 + i]);
		}

		const auto accessCodeBytes = decryptSPAD0 (spad0Content);
		const auto accessCode = hexToString(accessCodeBytes);
    	if (!checkAICAccessCode(accessCode)) {
			disconnect();
			return;
		}
    	cardInfo.accessCode = accessCode;
    }

    disconnect();
}

bool SmartCard::checkMifareAccessCode(const std::string& accessCode) {
    // Check if 20 digits
    if (accessCode.length() != 20) {
        printError("%s (%s): Invalid access code: %s\n", __func__, module, accessCode.c_str());
        return false;
    }

    // Check if all digits are numbers
    for (const auto c : accessCode) {
        if (!std::isdigit(c)) {
            printError("%s (%s): Invalid access code: %s\n", __func__, module, accessCode.c_str());
            return false;
        }
    }

	// Check the string array banapassPrefixes for first three digits matching
	for (const auto& prefix : banapassPrefixes) {
        if (accessCode.substr(0, 3) == prefix) {
            cardInfo.cardType = "Bandai Namco Banapass";
            return true;
        }
    }

	// Check the string array classicalAimePrefixes for first five digits matching
	for (const auto& prefix : classicalAimePrefixes) {
        if (accessCode.substr(0, 5) == prefix) {
            cardInfo.cardType = "Classical AiMe";
            return true;
        }
    }

	printError("%s (%s): Invalid access code: %s\n", __func__, module, accessCode.c_str());
	return false;
}

bool SmartCard::checkAICAccessCode(const std::string& accessCode) {
	// Check if 20 digits
	if (accessCode.length() != 20) {
		printError("%s (%s): Invalid access code: %s\n", __func__, module, accessCode.c_str());
		return false;
	}

	// Check if all digits are numbers
	for (const auto c : accessCode) {
		if (!std::isdigit(c)) {
			printError("%s (%s): Invalid access code: %s\n", __func__, module, accessCode.c_str());
			return false;
		}
	}

	// Check if access code starts with 500 or 501 or 510 or 520 or 530
	switch (std::stoi(accessCode.substr(0, 3))) {
	case 500:
		cardInfo.cardType = "AIC SEGA AiMe limited edition";
		break;
	case 501:
		cardInfo.cardType = "AIC SEGA AiMe";
		break;
	case 510:
		cardInfo.cardType = "AIC Bandai Namco Banapass";
		break;
	case 520:
		cardInfo.cardType = "AIC Konami e-Amusement";
		break;
	case 530:
		cardInfo.cardType = "AIC Taito NESiCA";
		break;
	default:
		cardInfo.cardType = "unknown";
		return false;
	}
	return true;
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

long SmartCard::connectReader (const DWORD shareMode, const DWORD preferredProtocols) {
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

std::string SmartCard::hexToString(const BYTE* hex, const size_t len) {
    std::stringstream ss;
    ss << std::hex << std::uppercase;
    for (size_t i = 0; i < len; i++) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(hex[i]);
    }
    return ss.str();
}

std::string SmartCard::hexToString(const std::vector<uint8_t>& hex) {
    std::stringstream ss;
    ss << std::hex << std::uppercase;
    for (const auto& b : hex) {
        ss << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return ss.str();
}
