/*
 * Pico SDK reset interface (picotool reboot / BOOTSEL) for the CDC profile.
 * The SDK only installs its own driver when stdio owns TinyUSB, so the
 * firmware provides it alongside the MCA class driver.
 */
#include "usb_drivers.h"

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdio_usb/reset_interface.h"
#include "tusb.h"

static uint8_t reset_interface_number = 0xff;

static void reset_itf_init(void) {
    reset_interface_number = 0xff;
}

static void reset_itf_reset(uint8_t rhport) {
    (void)rhport;
    reset_itf_init();
}

static uint16_t reset_itf_open(uint8_t rhport, tusb_desc_interface_t const *interface_desc,
                               uint16_t max_length) {
    (void)rhport;
    if (interface_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC ||
        interface_desc->bInterfaceSubClass != RESET_INTERFACE_SUBCLASS ||
        interface_desc->bInterfaceProtocol != RESET_INTERFACE_PROTOCOL ||
        interface_desc->bNumEndpoints != 0 || max_length < interface_desc->bLength)
        return 0;
    reset_interface_number = interface_desc->bInterfaceNumber;
    return interface_desc->bLength;
}

static bool reset_itf_control(uint8_t rhport, uint8_t stage,
                              tusb_control_request_t const *request) {
    (void)rhport;
    if (request->wIndex != reset_interface_number ||
        request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE)
        return false;
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    if (request->bRequest == RESET_REQUEST_BOOTSEL) {
        int gpio = (request->wValue & 0x100) ? request->wValue >> 9u : -1;
        bool active_low = (request->wValue & 0x200) != 0;
        rom_reset_usb_boot_extra(gpio, request->wValue & 0x7f, active_low);
        return true;
    }
    if (request->bRequest == RESET_REQUEST_FLASH) {
        watchdog_reboot(0, 0, 100);
        return true;
    }
    return false;
}

static bool reset_itf_xfer(uint8_t rhport, uint8_t endpoint_address, xfer_result_t result,
                           uint32_t transferred) {
    (void)rhport;
    (void)endpoint_address;
    (void)result;
    (void)transferred;
    return false;
}

static usbd_class_driver_t const reset_itf_driver = {
    .name = "RESET",
    .init = reset_itf_init,
    .reset = reset_itf_reset,
    .open = reset_itf_open,
    .control_xfer_cb = reset_itf_control,
    .xfer_cb = reset_itf_xfer,
    .sof = NULL,
};

usbd_class_driver_t const *usb_reset_itf_driver(void) {
    return &reset_itf_driver;
}
