#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <windows.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef struct card_info
{
    int card_type; // 0: Mifare, 1: Felica
    uint8_t uid[8];
    BYTE access_code[10];
} card_info_t;

#define INFO_COLOUR               FOREGROUND_GREEN
#define WARNING_COLOUR            (FOREGROUND_RED | FOREGROUND_GREEN)
#define ERROR_COLOUR              FOREGROUND_RED
#define printInfo(format, ...)    printColour (INFO_COLOUR, format, ##__VA_ARGS__)
#define printWarning(format, ...) printColour (WARNING_COLOUR, format, ##__VA_ARGS__)
#define printError(format, ...)   printColour (ERROR_COLOUR, format, ##__VA_ARGS__)

void printColour (int colour, const char *format, ...);