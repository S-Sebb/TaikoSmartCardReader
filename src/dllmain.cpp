#include "scard.h"
#include "helpers.h"
#include <windows.h>
#include <fstream>
#include <thread>
#include <sstream>
#include <chrono>
#include <atomic>

char module[] = "scardreader";

std::thread readerThread;
bool initialized = false;
std::atomic stopFlag(false);
SmartCard sCard;

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

        // Write access code to file
        if (std::ofstream fp("cards.dat"); fp.is_open()) {
            fp << sCard.cardInfo.accessCode;
            fp.close();
        } else {
            printError("%s, %s: Failed to open cards.dat\n", __func__, module);
        }
        
        // Press F3 key
        pressKey(0x72);

        // Press F3 again
        pressKey(0x72);
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