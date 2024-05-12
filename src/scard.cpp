#include "scard.h"
#include "constants.h"
#include <iostream>

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
    LONG lRet = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &hContext);
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
    memset(&cardInfo, 0, sizeof(cardInfo));

    LONG lRet = SCardGetStatusChange(hContext, INFINITE, readerState, 1);
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
    cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];

    // Send UID command
    long lRet = transmit(pci, uidCmd, sizeof(uidCmd), pbRecv, cbRecv);
    if (lRet != SCARD_S_SUCCESS) {
        disconnect();
        return;
    }
    int card_uid_len = (int)cbRecv - 2;
    if (card_uid_len > 8) {
        printWarning("%s (%s): Taking first 8 bytes of UID\n", __func__, module);
        card_uid_len = 8;
    }

    for (int i = 0; i < card_uid_len; i++) {
        cardInfo.uid[i] = pbRecv[i];
    }

    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        // Send Load Key command
        lRet = transmit(pci, loadKeyCmd, sizeof(loadKeyCmd), pbRecv, cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        // Send Auth Block 2 command
        lRet = transmit(pci, authBlock2Cmd, sizeof(authBlock2Cmd), pbRecv, cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        // Send Read Block 2 command
        lRet = transmit(pci, readBlock2Cmd, sizeof(readBlock2Cmd), pbRecv, cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            disconnect();
            return;
        }

        memcpy(cardInfo.access_code, pbRecv + 6, 10);
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
    if (lRet != SCARD_S_SUCCESS) {
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

long SmartCard::transmit(LPCSCARD_IO_REQUEST pci, const BYTE* cmd, size_t cmdLen, BYTE* recv, size_t recvLen) {
    DWORD cbRecv = recvLen;
    long lRet = SCardTransmit(hCard, pci, cmd, cmdLen, nullptr, recv, &cbRecv);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s, %s: Failed to transmit: 0x%08X\n", __func__, module, lRet);
    }
    return lRet;
}