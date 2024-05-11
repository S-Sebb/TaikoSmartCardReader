#include "helpers.h"
#include <windows.h>

void *consoleHandle = 0;

void
printColour (int colour, const char *format, ...) {
	va_list args;
	va_start (args, format);

	if (consoleHandle == 0) consoleHandle = GetStdHandle (STD_OUTPUT_HANDLE);

	SetConsoleTextAttribute (consoleHandle, colour);
	vprintf (format, args);
	SetConsoleTextAttribute (consoleHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);

	va_end (args);
}
