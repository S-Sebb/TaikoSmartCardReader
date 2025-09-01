#include "scard.h"
#include "helpers.h"
#include <windows.h>
#include <fstream>
#include <thread>
#include <sstream>
#include <chrono>
#include <atomic>

char module[] = "scardreader";

u8 cardData[168]= { 0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2E, 0x58, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x5C, 0x97, 0x44, 0xF0, 0x88, 0x04, 0x00, 0x43, 0x26, 0x2C, 0x33, 0x00, 0x04, 0x06, 0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x43, 0x36, 0x00, 0x00, 0xFA, 0xE9, 0x69, 0x00, 0xF6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

std::thread readerThread;
bool initialized = false;
std::atomic stopFlag(false);
SmartCard sCard;
bool waitingForTouch = false;
typedef i32 (*callbackTouch)(i32, i32, u8[168], u64);
callbackTouch touchCallback;
u64 touchData;

void pressKey(const WORD key) {
    INPUT ip = {};
    ip.type = INPUT_KEYBOARD;
    ip.ki.wVk = key;
    SendInput(1, &ip, sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ip.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ip, sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void readerPollThread() {
    while (!stopFlag.load()) {
        if (stopFlag.load()) {
            break;
        }

        sCard.update();

        if (sCard.cardInfo.cardType == "empty") {
            continue;
        }

        if (sCard.cardInfo.cardType == "unknown") {
            printWarning("Unknown card type\n");
            printInfo("Card UID: %s\n", sCard.cardInfo.uid.c_str());
            continue;
        }

        if (sCard.cardInfo.cardType == "error") {
            printError("Error during lookUpCard request\n");
            printInfo("Card UID: %s\n", sCard.cardInfo.uid.c_str());
            continue;
        }

        printInfo("Card Type: %s\n", sCard.cardInfo.cardType.c_str());
        printInfo("Card UID: %s\n", sCard.cardInfo.uid.c_str());
        printInfo("Access Code: %s\n", sCard.cardInfo.accessCode.c_str());

    	if (waitingForTouch) {
    		waitingForTouch = false;
    		// Generating a ChipID Derived from the AccessID
    		char ChipID[33] = "000000000000";
    		strcat(ChipID, sCard.cardInfo.accessCode.c_str());

    		memcpy(cardData + 0x2C, ChipID, 33);
    		memcpy(cardData + 0x50, sCard.cardInfo.accessCode.c_str(), 21);
    		touchCallback(0, 0, cardData, touchData);
    	}
    }
}

extern "C" {
__declspec(dllexport) void Init() {
    if (!initialized) {
        sCard = SmartCard();

        initialized = true;

        if (!sCard.initialize()) {
            printWarning("%s, %s: SmartCardReader not initialized\n", __func__, module);
            return;
        }

        printInfo("%s, %s: SmartCardReader initialized\n", __func__, module);
    }

    stopFlag.store(false);
    readerThread = std::thread(readerPollThread);
}

__declspec(dllexport) void WaitTouch(i32 (*callback)(i32, i32, u8[168], u64), u64 data) // This is called everytime the game expects a card to be read
{
	printInfo("%s (%s): Waiting for touch\n", __func__, module);
	waitingForTouch = true;
	touchCallback = callback;
	touchData = data;
}


__declspec(dllexport) void Exit() {
    printInfo("%s, %s: Exiting SmartCardReader\n", __func__, module);
    stopFlag.store(true);
    if (readerThread.joinable()) {
        readerThread.join();
    }

    if (initialized) {
        sCard.~SmartCard();
        initialized = false;
    }
}
}