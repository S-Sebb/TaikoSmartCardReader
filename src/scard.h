#pragma once
#include <cstdint>
#include <winscard.h>
#include <helpers.h>
#include <string>

class SmartCard {
public:
    cardInfoType cardInfo{};            // Information about the card.

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
    bool connected = false;         // Whether the card is connected.
    std::string serverUrl = ""; // URL of the server.

    void handleCardStatusChange();                 // Handle changes in card status.
    bool isCardPresent();                  // Check if a card is present in the reader.
    bool setupReader();                                           // Set up the card reader.
    bool sendPiccOperatingParams();                               // Send PICC operating parameters to the card.
    void poll(); // Poll the smart card reader for changes.
	bool checkMifareAccessCode (const std::string &accessCode);
	bool checkAICAccessCode (const std::string &accessCode);
	bool readATR();                                               // Read the ATR of the card.
    bool connect(); // Connect to a specific reader.
    void disconnect();             // Disconnect from the smart card reader.
    long connectReader(DWORD shareMode, DWORD preferredProtocols); // Connect to a specific reader.
    long transmit(LPCSCARD_IO_REQUEST pci, const BYTE* cmd, size_t cmdLen, BYTE* recv, DWORD* recvLen); // Transmit data to the card.
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp); // Helper function to handle the response data.
    static std::string hexToString(const BYTE* hex, size_t len); // Convert a hex string to a string.
	static std::string hexToString (const std::vector<uint8_t>& hex);
};
