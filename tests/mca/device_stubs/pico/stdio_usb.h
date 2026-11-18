#pragma once
#include "pico/stdio.h"
extern stdio_driver_t stdio_usb;
bool stdio_usb_init(void);
bool stdio_usb_connected(void);
#define PICO_STDIO_USB_DEINIT_DELAY_MS 0
#define PICO_STDIO_USB_POST_CONNECT_WAIT_DELAY_MS 0
