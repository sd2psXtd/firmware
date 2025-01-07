#pragma once

#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>

#define LOG_LEVEL_MC_DATA    2
#define LOG_LEVEL_MMCEMAN    3
#define LOG_LEVEL_MMCEMAN_FS 3
#define LOG_LEVEL_MC_AUTH    2
#define LOG_LEVEL_PS2_CM     2
#define LOG_LEVEL_PS2_MAIN   2
#define LOG_LEVEL_PS2_MC     2
#define LOG_LEVEL_PS2_HT     2
#define LOG_LEVEL_PS2_S2M    3
#define LOG_LEVEL_GUI        2

#define LOG_ERROR 1
#define LOG_WARN 2
#define LOG_INFO 3
#define LOG_TRACE 4

extern const char *log_level_str[];

#ifdef DEBUG_USB_UART
#define LOG_PRINT(file_level, level, fmt, x...) \
    do { \
        if (level <= file_level) { \
            printf("%s C%i: "fmt, log_level_str[level], get_core_num(), ##x); \
        } \
    } while (0);
#else
#define LOG_PRINT(file_level, level, fmt, x...)

#endif

#define TIME_PROFILE(f) do {\
    uint64_t time = time_us_64();\
    f();\
    printf("%s took %u us\n", #f, (uint32_t)(time_us_64() - time));\
} while (0);

#ifdef DEBUG_USB_UART
    #define DPRINTF(fmt, x...) buffered_printf(fmt, ##x)
    #define QPRINTF(fmt, x...) printf(fmt, ##x)
#else
    #define DPRINTF(fmt, x...)
    #define QPRINTF(fmt, x...)
#endif

#define DPRINTFFLT() QPRINTF("%s:%u - %lu\n", __func__, __LINE__, (uint32_t)time_us_64()/1000U)


void debug_put(char c);
char debug_get(void);
void buffered_printf(const char *format, ...);
void fatal(const char *format, ...);
void hexdump(const uint8_t *buf, size_t sz);
