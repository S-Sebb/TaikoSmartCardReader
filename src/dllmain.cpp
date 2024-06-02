#include "scard.h"
#include "helpers.h"
#include <windows.h>
#include <fstream>
#include <thread>
#include <sstream>
#include <chrono>
#include <mutex>
#include <atomic>

char module[] = "scardreader";

std::thread readerThread;
bool initialized = false;
std::atomic<bool> stopFlag(false);
SmartCard scard;

void pressKey(WORD key) {
    INPUT ip = {0};
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

        scard.update();

        if (scard.cardInfo.cardType == "empty") {
            continue;
        }

        if (scard.cardInfo.cardType == "unknown") {
            printWarning("Unknown card type\n");
            printInfo("Card UID: %s\n", scard.cardInfo.uid.c_str());
            continue;
        }

        printInfo("Card Type: %s\n", scard.cardInfo.cardType.c_str());
        printInfo("Card UID: %s\n", scard.cardInfo.uid.c_str());
        printInfo("Access Code: %s\n", scard.cardInfo.accessCode.c_str());

        if (!SmartCard::changeAccessCode(scard.cardInfo.uid, scard.cardInfo.accessCode)) {
            printError("%s, %s: Failed to change access code, please check server status\n", __func__, module);
            continue;
        }

        // Write access code to file
        std::ofstream fp("cards.dat");
        if (fp.is_open()) {
            fp << scard.cardInfo.accessCode;
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
        scard = SmartCard();

        initialized = true;

        if (!scard.initialize()) {
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
        scard.~SmartCard();
        initialized = false;
    }

    // Create and signal named event
    HANDLE hEvent = CreateEvent(nullptr, TRUE, FALSE, "PluginExitEvent");
    if (hEvent) {
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }
}
}