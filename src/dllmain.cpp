#include "scard.h"
#include "helpers.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <chrono>

char module[] = "scardreader.dll";

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

        uint8_t UID[8] = {0};
        for (int i = 0; i < 8; i++) {
            UID[i] = scard.cardInfo.uid[i];
        }

        if (UID[0] > 0) {
            std::stringstream UIDString;
            for (unsigned char c : UID) {
                if (c != 0x00) {
                    // Convert to hex string uppercase
                    UIDString << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)c;
                }
            }

            printInfo("Card UID: %s\n", UIDString.str().c_str());

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

        uint8_t access_code[10] = {0};
        for (int i = 0; i < 10; i++) {
            access_code[i] = scard.cardInfo.access_code[i];
        }

        if (access_code[0] > 0) {
            printInfo("Access Code: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
                         access_code[0], access_code[1], access_code[2], access_code[3], access_code[4],
                         access_code[5], access_code[6], access_code[7], access_code[8], access_code[9]);
        }
    }
}

extern "C" {
    __declspec(dllexport) void Init() {
        if (!initialized) {
            scard = SmartCard();

            initialized = true;

            if (!scard.initialize()) {
                printError("%s, %s: Failed to initialize SmartCard\n", __func__, module);
                return;
            }

            printInfo("%s, %s: SmartCard initialized\n", __func__, module);
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