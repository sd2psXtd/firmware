#include "settings.h"
#include "../../src/usb/mca_device.h"
#include "../../src/usb/mca_bridge.h"
#include "../../src/usb/mca_protocol.h"

#include "device/usbd.h"
#include "device/usbd_pvt.h"

#include <assert.h>
#include <string.h>

extern mca_bridge_t mca_bridge;

enum { EP_IN = 0x81, EP_OUT = 0x02, EP_INT = 0x83 };

typedef struct {
    uint8_t endpoint;
    uint8_t *buffer;
    uint32_t length;
} transfer_t;

static transfer_t transfers[16];
static size_t transfer_count;
static uint64_t now;

uint64_t time_us_64(void) { return now; }
void sleep_ms(uint32_t milliseconds) { now += (uint64_t)milliseconds * 1000; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) {
    (void)timeout_ms;
    (void)in_isr;
}
void rom_reset_usb_boot_extra(int gpio, uint32_t disable_interface_mask,
                              bool activity_led_active_low) {
    (void)gpio;
    (void)disable_interface_mask;
    (void)activity_led_active_low;
}
void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    (void)delay_ms;
}
int settings_get_mode(bool temporary) { (void)temporary; return 1; }
int settings_get_usb_mode(bool current) { (void)current; return USB_MODE_MCA; }
int settings_get_ps2_variant(void) { return 1; }
bool ps2_cardman_is_accessible(void) { return true; }
bool ps2_cardman_is_idle(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return 8u * 1024 * 1024; }
bool ps2_mc_auth_isValid(void) { return true; }

bool usbd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *endpoint) {
    (void)rhport;
    return endpoint && endpoint->bDescriptorType == TUSB_DESC_ENDPOINT;
}

bool usbd_edpt_xfer(uint8_t rhport, uint8_t endpoint, uint8_t *buffer,
                    uint32_t length) {
    (void)rhport;
    assert(transfer_count < sizeof(transfers) / sizeof(transfers[0]));
    transfers[transfer_count++] = (transfer_t){endpoint, buffer, length};
    return true;
}

typedef struct {
    tusb_desc_interface_t interface_desc;
    tusb_desc_endpoint_t endpoints[3];
} interface_descriptor_t;

static usbd_class_driver_t const *open_device(void) {
    uint8_t count = 0;
    usbd_class_driver_t const *driver = usbd_app_driver_get_cb(&count);
    assert(driver && count == 1);
    driver->init();
    interface_descriptor_t descriptor = {
        .interface_desc = {9, 4, 0, 0, 3, 0xff, 0, 0xff, 0},
        .endpoints = {
            {7, TUSB_DESC_ENDPOINT, EP_IN, 2, {64, 0}, 0},
            {7, TUSB_DESC_ENDPOINT, EP_OUT, 2, {64, 0}, 0},
            {7, TUSB_DESC_ENDPOINT, EP_INT, 3, {1, 0}, 100},
        },
    };
    assert(driver->open(0, &descriptor.interface_desc, sizeof(descriptor)) ==
           sizeof(descriptor));
    return driver;
}

static void receive_out(usbd_class_driver_t const *driver, const uint8_t *data,
                        uint32_t length) {
    transfer_t out = {0};
    for (size_t i = transfer_count; i > 0; --i) {
        if (transfers[i - 1].endpoint == EP_OUT) {
            out = transfers[i - 1];
            break;
        }
    }
    assert(out.endpoint == EP_OUT);
    assert(length <= out.length);
    memcpy(out.buffer, data, length);
    assert(driver->xfer_cb(0, EP_OUT, XFER_RESULT_SUCCESS, length));
}

int main(void) {
    usbd_class_driver_t const *driver;
    static uint8_t raw_request[64];
    size_t first_in_index;

    mca_protocol_init();
    mca_protocol_set_ready(true);
    mca_device_init(true, mca_protocol_callbacks());
    driver = open_device();

    raw_request[0] = 0xaa;
    raw_request[1] = 0x42;
    raw_request[2] = 60;
    raw_request[3] = 0;
    for (size_t i = 4; i < sizeof(raw_request); ++i)
        raw_request[i] = (uint8_t)(i ^ 0x5a);
    receive_out(driver, raw_request, sizeof(raw_request));
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    for (size_t i = 4; i < sizeof(raw_request); ++i) {
        uint8_t byte;
        assert(mca_bridge_core1_receive(&mca_bridge, &byte));
        assert(byte == raw_request[i]);
        assert(mca_bridge_core1_respond(&mca_bridge, byte));
    }
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();

    first_in_index = transfer_count - 1;
    assert(transfers[first_in_index].endpoint == EP_IN);
    assert(transfers[first_in_index].length == 64);
    assert(transfers[first_in_index].buffer[0] == 0x55);
    assert(transfers[first_in_index].buffer[1] == 0x5a);
    assert(transfers[first_in_index].buffer[2] == 60);
    assert(transfers[first_in_index].buffer[3] == 0);
    for (size_t i = 4; i < 64; ++i)
        assert(transfers[first_in_index].buffer[i] == raw_request[i]);

    assert(driver->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, 64));
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    assert(transfer_count == first_in_index + 1);

    const uint8_t detect_request[] = {0xaa, 0x40};
    size_t before_detect = transfer_count;
    receive_out(driver, detect_request, sizeof(detect_request));
    mca_protocol_task();
    assert(transfer_count == before_detect + 2);
    assert(transfers[transfer_count - 1].endpoint == EP_IN);
    assert(transfers[transfer_count - 1].length == 2);
    assert(transfers[transfer_count - 1].buffer[0] == 0x55);
    assert(transfers[transfer_count - 1].buffer[1] == 2);
    assert(driver->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, 2));
    mca_protocol_task();
    assert(transfer_count == before_detect + 2);
    return 0;
}
