#pragma once

#include <cstdint>
#include <winscard.h>
#include <helpers.h>

class SmartCard {
public:
    card_info_t cardInfo{};            // Information about the card.

    SmartCard();
    ~SmartCard();

    bool initialize();             // Initialize the smart card reader context.
    void update();    // Update the status of the smart card reader.

private:
    SCARDCONTEXT hContext;          // Handle to the smart card context.
    SCARDHANDLE hCard;              // Handle to the connected card.
    SCARD_READERSTATE readerState[1];  // Current reader state.
    LPTSTR readerName;               // Name of the card reader.
    DWORD activeProtocol{};           // Active protocol used in communication.
    BYTE cardProtocol{};                // Protocol used by the card.
    int readCooldown = 500;               // Cooldown for reading the card.

    void handleCardStatusChange();                 // Handle changes in card status.
    bool setupReader();                                           // Set up the card reader.
    bool sendPiccOperatingParams();                               // Send PICC operating parameters to the card.
    void poll(); // Poll the smart card reader for changes.
    bool readATR();                                               // Read the ATR of the card.
    bool connect(); // Connect to a specific reader.
    void disconnect();             // Disconnect from the smart card reader.
    long connectReader(DWORD shareMode, DWORD preferredProtocols); // Connect to a specific reader.
    long transmit(LPCSCARD_IO_REQUEST pci, const BYTE* cmd, size_t cmdLen, BYTE* recv, DWORD* recvLen) const; // Transmit data to the card.
};
