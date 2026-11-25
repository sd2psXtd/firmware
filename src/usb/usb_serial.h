#pragma once

#include <stdbool.h>

/*
 * stdio over the CDC interface, used by the Serial and mass storage profiles.
 * Call after tusb_init() and stop before tud_disconnect().
 *
 * With background servicing, TinyUSB is run from a low-priority IRQ instead of
 * the main loop, like pico-sdk's stdio_usb does when it owns TinyUSB.
 */
void usb_serial_start(bool background);
void usb_serial_stop(void);
/* True while TinyUSB is serviced in the background (main loop must not poll). */
bool usb_serial_in_background(void);
