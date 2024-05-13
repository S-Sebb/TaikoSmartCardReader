#include "scard.h"
#include "helpers.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <sstream>
#include <chrono>

char module[] = "scardreader";

std::thread readerThread;
std::atomic<bool> initialized{false};
std::atomic<bool> stopFlag{false};
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
    while (!stopFlag) {
        scard.update();

        if (scard.cardInfo.cardType == "Empty") {
            continue;
        }

        if (scard.cardInfo.cardType == "unknown") {
            printWarning("Unknown card type\n");
            continue;
        }

        printInfo("Card Type: %s\n", scard.cardInfo.cardType.c_str());
        printInfo("Card UID: %s\n", scard.cardInfo.uid.c_str());
        printInfo("Access Code: %s\n", scard.cardInfo.accessCode.c_str());

        // Write access code to file
        std::ofstream fp("cards.dat");
        if (fp.is_open()) {
            fp << scard.cardInfo.accessCode;
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
            stopFlag = false;
            readerThread = std::thread(readerPollThread);
        }
    }

    __declspec(dllexport) void Exit() {
        if (initialized) {
            scard.~SmartCard();
            stopFlag = true;
            if (readerThread.joinable()) {
                readerThread.join();
            }
            initialized = false;
        }
    }
}