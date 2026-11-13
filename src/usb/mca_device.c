#include "mca_device.h"

#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "bsp/board.h"
#include "config.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/stdio_usb/reset_interface.h"
#include "pico/time.h"
#include "mca_protocol.h"
#include "settings.h"
#include "tusb.h"

#include <string.h>

#define MCA_EP_IN  0x81
#define MCA_EP_OUT 0x02
#define MCA_EP_INT 0x83
#define MCA_PACKET_SIZE 64
#define MCA_INTERFACE_CLASS 0xff

static bool profile_mca;
static int session_usb_mode;
static mca_device_callbacks_t device_callbacks;
static union { uint32_t alignment; uint8_t bytes[MCA_PACKET_SIZE]; } ep_out_buffer;
static union { uint32_t alignment; uint8_t bytes[1]; } ep_interrupt_buffer;
static bool interface_open;
static bool presence;
static bool presence_reported;
static bool presence_reported_valid;
static bool removal_pending;
static uint8_t reset_interface_number = 0xff;

static void service_presence(uint8_t rhport) {
    bool next = removal_pending ? false : presence;
    if (!interface_open || usbd_edpt_busy(rhport, MCA_EP_INT) ||
        (!removal_pending && presence_reported_valid && presence_reported == next))
        return;

    ep_interrupt_buffer.bytes[0] = next ? 1 : 0;
    (void)usbd_edpt_xfer(rhport, MCA_EP_INT, ep_interrupt_buffer.bytes, sizeof(ep_interrupt_buffer.bytes));
}

void mca_device_init(bool mca_profile, const mca_device_callbacks_t *callbacks) {
    profile_mca = mca_profile;
    if (callbacks)
        device_callbacks = *callbacks;
    else
        memset(&device_callbacks, 0, sizeof(device_callbacks));
    interface_open = false;
    presence = false;
    presence_reported = false;
    presence_reported_valid = false;
    removal_pending = false;
    reset_interface_number = 0xff;
}

bool mca_device_profile_is_mca(void) {
    return profile_mca;
}

void usb_session_init(void) {
    mca_protocol_init();
    board_init();
}

void usb_session_prepare(void) {
    session_usb_mode = settings_get_usb_mode(true);
    mca_device_init(session_usb_mode == USB_MODE_MCA, mca_protocol_callbacks());
}

int usb_session_get_mode(void) {
    return session_usb_mode;
}

void usb_session_start(void) {
    tusb_init();
    if (!profile_mca) {
        stdio_usb_init();
#if DEBUG_USB_UART
        static bool startup_waited;
        if (!startup_waited) {
            uint64_t end = time_us_64() + (uint64_t)DEBUG_STARTUP_DELAY_MS * 1000;
            while (!stdio_usb_connected() && time_us_64() < end) {
                tud_task_ext(0, false);
                sleep_ms(1);
            }
            if (DEBUG_STARTUP_DELAY_MS > 0 && stdio_usb_connected())
                sleep_ms(PICO_STDIO_USB_POST_CONNECT_WAIT_DELAY_MS);
            startup_waited = true;
        }
#endif
    }
}

bool usb_session_continue(bool mode_running) {
    return mode_running || !mca_protocol_idle();
}

void usb_session_poll(void) {
    tud_task_ext(0, false);
}

void usb_session_drain(void) {
    if (profile_mca)
        mca_protocol_drain_reply(500);
}

void usb_session_stop(void) {
    if (!profile_mca) {
        stdio_set_driver_enabled(&stdio_usb, false);
        sleep_ms(PICO_STDIO_USB_DEINIT_DELAY_MS);
    }
    tud_disconnect();
    tud_deinit(0);
    mca_protocol_reset();
}

bool mca_device_send(const uint8_t *data, size_t length) {
    if (!profile_mca || !interface_open || usbd_edpt_busy(0, MCA_EP_IN) || !data ||
        length == 0 || length > MCA_DEVICE_MAX_TRANSFER || ((uintptr_t)data & 3u) != 0)
        return false;
    return usbd_edpt_xfer(0, MCA_EP_IN, (uint8_t *)(uintptr_t)data, (uint32_t)length);
}

void mca_device_set_presence(bool present) {
    if (presence && !present)
        removal_pending = true;
    presence = present;
}

void mca_device_receive(void) {
    if (interface_open && !usbd_edpt_busy(0, MCA_EP_OUT))
        (void)usbd_edpt_xfer(0, MCA_EP_OUT, ep_out_buffer.bytes, sizeof(ep_out_buffer.bytes));
}

void mca_device_task(void) {
    service_presence(0);
}

static void mca_driver_init(void) {
    interface_open = false;
    presence_reported_valid = false;
    removal_pending = false;
    reset_interface_number = 0xff;
}

static void mca_driver_reset(uint8_t rhport) {
    (void)rhport;
    mca_driver_init();
    if (device_callbacks.bus_reset)
        device_callbacks.bus_reset();
}

static uint16_t mca_driver_open(uint8_t rhport,
                                tusb_desc_interface_t const *interface_desc,
                                uint16_t max_length) {
    uint8_t const *descriptor;
    uint16_t consumed;
    uint8_t endpoint_index;
    bool out_open = false, in_open = false, interrupt_open = false;

    if (interface_desc->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC &&
        interface_desc->bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
        interface_desc->bInterfaceProtocol == RESET_INTERFACE_PROTOCOL &&
        interface_desc->bNumEndpoints == 0) {
        if (max_length < interface_desc->bLength)
            return 0;
        reset_interface_number = interface_desc->bInterfaceNumber;
        return interface_desc->bLength;
    }

    if (!profile_mca || interface_desc->bInterfaceClass != MCA_INTERFACE_CLASS ||
        interface_desc->bInterfaceSubClass != 0 || interface_desc->bInterfaceProtocol != 0xff ||
        interface_desc->bNumEndpoints != 3 || max_length < interface_desc->bLength + 21)
        return 0;

    descriptor = (uint8_t const *)interface_desc + interface_desc->bLength;
    consumed = interface_desc->bLength;
    for (endpoint_index = 0; endpoint_index < 3; ++endpoint_index) {
        if (consumed > max_length || (size_t)(max_length - consumed) < sizeof(tusb_desc_endpoint_t))
            return 0;
        tusb_desc_endpoint_t const *endpoint = (tusb_desc_endpoint_t const *)descriptor;
        if (endpoint->bDescriptorType != TUSB_DESC_ENDPOINT ||
            endpoint->bLength < sizeof(tusb_desc_endpoint_t) ||
            endpoint->bLength > max_length - consumed ||
            !usbd_edpt_open(rhport, endpoint))
            return 0;
        if (endpoint->bEndpointAddress == MCA_EP_OUT)
            out_open = true;
        else if (endpoint->bEndpointAddress == MCA_EP_IN)
            in_open = true;
        else if (endpoint->bEndpointAddress == MCA_EP_INT)
            interrupt_open = true;
        descriptor += endpoint->bLength;
        consumed += endpoint->bLength;
    }
    if (!out_open || !in_open || !interrupt_open)
        return 0;

    interface_open = true;
    service_presence(rhport);
    return consumed;
}

static bool mca_driver_control(uint8_t rhport, uint8_t stage,
                               tusb_control_request_t const *request) {
    int gpio;
    bool active_low;

    if (request->wIndex != reset_interface_number ||
        request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE)
        return false;
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    if (request->bRequest == RESET_REQUEST_BOOTSEL) {
        gpio = (request->wValue & 0x100) ? request->wValue >> 9u : -1;
        active_low = (request->wValue & 0x200) != 0;
        rom_reset_usb_boot_extra(gpio, request->wValue & 0x7f, active_low);
        return true;
    }
    if (request->bRequest == RESET_REQUEST_FLASH) {
        watchdog_reboot(0, 0, 100);
        return true;
    }
    (void)rhport;
    return false;
}

static bool mca_driver_xfer(uint8_t rhport, uint8_t endpoint_address,
                            xfer_result_t result, uint32_t transferred) {
    bool success = result == XFER_RESULT_SUCCESS;
    (void)rhport;

    if (endpoint_address == MCA_EP_OUT) {
        if (success && device_callbacks.out_received)
            device_callbacks.out_received(ep_out_buffer.bytes, (uint16_t)transferred);
        return true;
    }
    if (endpoint_address == MCA_EP_IN) {
        if (device_callbacks.in_complete)
            device_callbacks.in_complete(success, transferred);
        return true;
    }
    if (endpoint_address == MCA_EP_INT) {
        if (success && transferred == sizeof(ep_interrupt_buffer.bytes)) {
            presence_reported = ep_interrupt_buffer.bytes[0] != 0;
            presence_reported_valid = true;
            if (!presence_reported)
                removal_pending = false;
        } else {
            presence_reported_valid = false;
        }
        return true;
    }
    return false;
}

static usbd_class_driver_t const mca_driver = {
    .name = "MCA",
    .init = mca_driver_init,
    .reset = mca_driver_reset,
    .open = mca_driver_open,
    .control_xfer_cb = mca_driver_control,
    .xfer_cb = mca_driver_xfer,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &mca_driver;
}

void tud_cdc_line_coding_cb(uint8_t interface_number, cdc_line_coding_t const *coding) {
    (void)interface_number;
    if (coding->bit_rate == 1200)
        rom_reset_usb_boot_extra(-1, 0, false);
}
