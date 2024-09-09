#pragma once

#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>

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
