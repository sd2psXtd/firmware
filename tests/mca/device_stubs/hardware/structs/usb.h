#pragma once

#include <stdint.h>

typedef struct {
    volatile uint8_t setup_packet[8];
} usb_device_dpram_t;

/* Provided by the test: where the "controller" stores the SETUP packet. */
extern usb_device_dpram_t *const usb_dpram;
