#pragma once

#include <stdbool.h>
#include <stdint.h>

void rom_reset_usb_boot_extra(int gpio, uint32_t disable_interface_mask, bool activity_led_active_low);
