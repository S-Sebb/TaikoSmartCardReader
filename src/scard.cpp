#include "scard.h"
#include "constants.h"
#include <winscard.h>

extern char module[];

int readCooldown = 500;
SCARDCONTEXT hContext = 0;
SCARD_READERSTATE reader_states[1];
LPTSTR reader = nullptr;
int reader_count = 0;

void scard_disconnect(SCARDHANDLE hCard)
{
    long lRet;
    lRet = SCardDisconnect(hCard, SCARD_LEAVE_CARD);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s (%s): Failed SCardDisconnect: 0x%08X\n", __func__, module, lRet);
    }
    else{
        printInfo("%s (%s): Disconnected from reader\n", __func__, module);
    }
}

bool scard_connect(SCARDCONTEXT _hContext, LPCTSTR _readerName, SCARDHANDLE *hCard, DWORD *dwActiveProtocol)
{
    long lRet;
    for (int retry = 0; retry < 500; retry++) {
        lRet = SCardConnect(_hContext, _readerName, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, hCard, dwActiveProtocol);
        if (lRet == SCARD_S_SUCCESS) {
            break;
        }
        Sleep(10);
    }

    if (lRet != SCARD_S_SUCCESS) {
        printError("%s (%s): Error connecting to the card: 0x%08X\n", __func__, module, lRet);
        return false;
    }
    return true;
}

bool scard_init()
{
    long lRet;
    SCARDHANDLE hCard;
    DWORD dwActiveProtocol;

    lRet = SCardEstablishContext(SCARD_SCOPE_USER, nullptr, nullptr, &hContext);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s (%s): Failed SCardEstablishContext: 0x%08X\n", __func__, module, lRet);
        return false;
    }

    LPTSTR reader_list = nullptr;
    auto pcchReaders = SCARD_AUTOALLOCATE;
    lRet = SCardListReaders(hContext, nullptr, (LPTSTR)&reader_list, &pcchReaders);

    DWORD cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];
    switch (lRet) {
        case SCARD_E_NO_READERS_AVAILABLE:
            printError("%s (%s): No readers available\n", __func__, module);
            if (reader_list)
            {
                SCardFreeMemory(hContext, reader_list);
            }
            return false;

        case SCARD_S_SUCCESS:
            reader = reader_list;
            reader_count = 1;
            printInfo("%s (%s): Connected to reader: %s\n", __func__, module, reader);

            // Connect to reader and send PICC operating params command
            lRet = SCardConnect(hContext, reader, SCARD_SHARE_DIRECT, 0, &hCard, &dwActiveProtocol);
            if (lRet != SCARD_S_SUCCESS) {
                printError("%s (%s): Error connecting to the reader: 0x%08X\n", __func__, module, lRet);
            }

            lRet = SCardControl(hCard, SCARD_CTL_CODE(3500), piccOperatingParamCmd, sizeof(piccOperatingParamCmd), pbRecv, cbRecv, &cbRecv);
            if (lRet != SCARD_S_SUCCESS) {
                printError("%s (%s): Error setting PICC params: 0x%08X\n", __func__, module, lRet);
                return FALSE;
            }
            if (cbRecv > 2 && pbRecv[0] != piccSuccess && pbRecv[1] != piccOperatingParams) {
                printError("%s (%s): PICC params not valid 0x%02X != 0x%02X\n", __func__, module, pbRecv[1], piccOperatingParams);
                return FALSE;
            }

            // Disconnect from reader
            scard_disconnect(hCard);

            memset(&reader_states[0], 0, sizeof(SCARD_READERSTATE));
            reader_states[0].szReader = reader;

            return true;

        default:
            printWarning("%s (%s): Failed SCardListReaders: 0x%08X\n", __func__, module, lRet);
            if (reader_list)
            {
                SCardFreeMemory(hContext, reader_list);
            }
            return false;
    }
}

void scard_update(uint8_t *buf)
{
    long lRet;
    if (reader_count < 1) {
        return;
    }

    lRet = SCardGetStatusChange(hContext, INFINITE, reader_states, 1);
    if (lRet == SCARD_E_TIMEOUT) {
        return;
    } else if (lRet != SCARD_S_SUCCESS) {
        printError("%s (%s): Failed SCardGetStatusChange: 0x%08X\n", __func__, module, lRet);
        return;
    }

    if (!(reader_states[0].dwEventState & SCARD_STATE_CHANGED))
    {
        return;
    }

    DWORD newState = reader_states[0].dwEventState ^ SCARD_STATE_CHANGED;
    bool wasCardPresent = (reader_states[0].dwCurrentState & SCARD_STATE_PRESENT) > 0;
    if (newState & SCARD_STATE_UNAVAILABLE)
    {
        printWarning("%s (%s): New card state: unavailable\n", __func__, module);
        Sleep(readCooldown);
    }
    else if (newState & SCARD_STATE_EMPTY)
    {
        printWarning("%s (%s): New card state: empty\n", __func__, module);
    }
    else if (newState & SCARD_STATE_PRESENT && !wasCardPresent)
    {
        printInfo("%s (%s): New card state: present\n", __func__, module);
        scard_poll(buf, hContext, reader_states[0].szReader, 0);
    }

    reader_states[0].dwCurrentState = reader_states[0].dwEventState;
}

void scard_poll(uint8_t *buf, SCARDCONTEXT _hContext, LPCTSTR _readerName, uint8_t unit_no)
{
    SCARDHANDLE hCard;
    DWORD dwActiveProtocol;
    long lRet;

    // Connect to the smart card.
    if (!scard_connect(_hContext, _readerName, &hCard, &dwActiveProtocol)) {
        return;
    }

    // Read ATR to determine card type.
    TCHAR szReader[200];
    DWORD cchReader = 200;
    BYTE atr[32];
    DWORD cByteAtr = 32;
    lRet = SCardStatus(hCard, szReader, &cchReader, nullptr, nullptr, atr, &cByteAtr);
    if (lRet != SCARD_S_SUCCESS) {
        printError("%s (%s): Error getting card status: 0x%08X\n", __func__, module, lRet);
        scard_disconnect(hCard);
        return;
    }

    // Only care about 20-byte ATRs returned by arcade-type smart cards
    if (cByteAtr != 20) {
        printError("%s (%s): Ignoring card with len(%d) = %02X (%08X)\n", __func__, module, cByteAtr, atr, cByteAtr);
        scard_disconnect(hCard);
        return;
    }

    // Figure out if we should reverse the UID returned by the card based on the ATR protocol
    BYTE cardProtocol = atr[12];
    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO15693_PART3) {
        printWarning("%s (%s): Card protocol: ISO15693_PART3\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        printWarning("%s (%s): Card protocol: ISO14443_PART3\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K) {
        printWarning("%s (%s): Card protocol: FELICA_212K\n", __func__, module);
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_424K) {
        printWarning("%s (%s): Card protocol: FELICA_424K\n", __func__, module);
    }
    else{
        printError("%s (%s): Unknown NFC Protocol: 0x%02X\n", __func__, module, cardProtocol);
        scard_disconnect(hCard);
        return;
    }

    // Read UID
    LPCSCARD_IO_REQUEST pci = dwActiveProtocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    DWORD cbRecv = maxApduSize;
    cbRecv = maxApduSize;
    BYTE pbRecv[maxApduSize];

    if ((lRet = SCardTransmit(hCard, pci, uidCmd, sizeof(uidCmd), nullptr, pbRecv, &cbRecv)) != SCARD_S_SUCCESS) {
        printError("%s (%s): Error querying card UID: 0x%08X\n", __func__, module, lRet);
        scard_disconnect(hCard);
        return;
    }

    if (cbRecv > 1 && pbRecv[0] == piccError) {
        printError("%s (%s): UID query failed\n", __func__, module);
        return;
    }

    if (cbRecv > 8) {
        printWarning("%s (%s): taking first 8 bytes of len(uid) = %02X\n", __func__, module, cbRecv);
    }

    int card_uid_len = (int)cbRecv - 2;

    // Cut off any bytes after the first card_uid_len bytes

    printWarning("cbRecv: %02X\n", cbRecv);

    printf("pbRecv: %02X%02X%02X%02X%02X%02X%02X%02X\n", pbRecv[0], pbRecv[1], pbRecv[2], pbRecv[3], pbRecv[4], pbRecv[5], pbRecv[6], pbRecv[7]);

    card_info_t card_info;
    memcpy(card_info.uid, pbRecv, 8);

    for (int i = 0; i < card_uid_len; i++) {
        buf[i] = card_info.uid[i];
    }

    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) {
        // Also read and print block 2 data
        printInfo("%s (%s): Reading block 2\n", __func__, module);

        // Send load key command
        cbRecv = maxApduSize;
        lRet = SCardTransmit(hCard, pci, loadKeyCmd, sizeof(loadKeyCmd), nullptr, pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            printError("%s (%s): Error loading key: 0x%08X\n", __func__, module, lRet);
            return;
        }

        // Authenticate block 2
        cbRecv = maxApduSize;
        lRet = SCardTransmit(hCard, pci, authBlock2Cmd, sizeof(authBlock2Cmd), nullptr, pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            printError("%s (%s): Error authenticating block 2: 0x%08X\n", __func__, module, lRet);
            return;
        }

        // Read block 2
        cbRecv = maxApduSize;
        lRet = SCardTransmit(hCard, pci, readBlock2Cmd, sizeof(readBlock2Cmd), nullptr, pbRecv, &cbRecv);
        if (lRet != SCARD_S_SUCCESS) {
            printError("%s (%s): Error reading block 2: 0x%08X\n", __func__, module, lRet);
            return;
        }

        // Copy 10 bytes of block 2 data
        BYTE block2[10];

        memcpy(block2, pbRecv + 6, 10);

        printf("Block 2: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n", block2[0], block2[1], block2[2], block2[3], block2[4], block2[5], block2[6], block2[7], block2[8], block2[9]);
    }

    scard_disconnect(hCard);

    Sleep(readCooldown);
}

void scard_exit()
{
    if (reader) {
        SCardFreeMemory(hContext, reader);
    }
    SCardReleaseContext(hContext);
}