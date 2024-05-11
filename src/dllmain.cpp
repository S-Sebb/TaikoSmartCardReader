#include "scard.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>

char module[] = "scardreader.dll";

std::thread readerThread;
std::atomic<bool> initialized{false};
std::atomic<bool> stopFlag{false};

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
        uint8_t UID[8] = {0};
        scard_update(UID);

        if (UID[0] > 0) {
            std::stringstream UIDString;
            for (unsigned char c : UID) {
                if (c != 0x00) {
                    UIDString << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
                }
            }

            printWarning("Card UID: %s\n", UIDString.str().c_str());

            // Write to file cards.dat
            std::ofstream fp("cards.dat");
            if (fp.is_open()) {
                fp << UIDString.str();
            } else {
                    printError("%s, %s: Failed to open cards.dat\n", __func__, module);
            }

            // Press F3 key
            pressKey(0x72);

            // Press F3 again
            pressKey(0x72);
        }
    }
}

extern "C" {
    __declspec(dllexport) void Init() {
        if (!initialized) {
            initialized = true;
            printWarning("%s, %s: Initializing SmartCard\n", __func__, module);

            if (!scard_init()) {
                printError("%s, %s: Failed to initialize SmartCard\n", __func__, module);
                return;
            }

            printWarning("%s, %s: SmartCard initialized\n", __func__, module);
            stopFlag = false;
            readerThread = std::thread(readerPollThread);
        }
    }

    __declspec(dllexport) void Exit() {
        if (initialized) {
            scard_exit();
            stopFlag = true;
            if (readerThread.joinable()) {
                readerThread.join();
            }
            initialized = false;
        }
    }
}