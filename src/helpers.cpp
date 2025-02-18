#include "helpers.h"
#include "constants.h"

#include <windows.h>


void *consoleHandle = nullptr;
constexpr int nTables = 8;
constexpr int iterAdd = 5;

void
printColour (const int colour, const char *format, ...) {
	va_list args;
	va_start (args, format);

	if (consoleHandle == nullptr) consoleHandle = GetStdHandle (STD_OUTPUT_HANDLE);

	SetConsoleTextAttribute (consoleHandle, colour);
	vprintf (format, args);
	SetConsoleTextAttribute (consoleHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);

	va_end (args);
}


void rotateRight(std::vector<uint8_t>& data, int nBytes, int nBits) {
	uint8_t prior = data[nBytes - 1];
	for (int i = 0; i < nBytes; i++) {
		const uint8_t current = data[i];
		data[i] = (current >> nBits) | ((prior & ((1 << nBits) - 1)) << (8 - nBits));
		prior = current;
	}
}

std::vector<uint8_t> decryptSPAD0(const std::vector<uint8_t>& spad0) {
	std::vector<uint8_t> spad;
	spad.reserve(spad0.size());
	for (const auto b : spad0)
		spad.push_back (sBoxInv[nTables][b]);

	const int count = (spad[15] >> 4) + 7;
	int table = spad[15] + iterAdd * count;

	for (int z = 0; z < count; z++) {
		table -= iterAdd;
		rotateRight (spad, 15, 5);
		for (int i = 0; i < 15; i++)
			spad[i] = sBoxInv[table % nTables][spad[i]];
	}

	auto accessCodeBytes = std::vector(spad.begin() + 6, spad.end());
	return accessCodeBytes;
}