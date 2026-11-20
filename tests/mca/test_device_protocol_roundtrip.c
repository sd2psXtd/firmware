#include "settings.h"
#include "../../src/usb/mca_device.h"
#include "../../src/usb/mca_bridge.h"
#include "../../src/usb/mca_protocol.h"
#include "../../src/usb/usb_drivers.h"

#include "device/dcd.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "hardware/structs/usb.h"

#include <assert.h>
#include <string.h>

extern mca_bridge_t mca_bridge;

enum { EP_IN = 0x81, EP_OUT = 0x02, EP_INT = 0x83 };

typedef struct {
    uint8_t endpoint;
    uint8_t *buffer;
    uint32_t length;
} transfer_t;

static transfer_t transfers[64];
static size_t transfer_count;
static uint64_t now;
static bool endpoint_busy[256];
bool usbd_edpt_busy(uint8_t rhport, uint8_t endpoint) {
    (void)rhport;
    return endpoint_busy[endpoint];
}
void usbd_edpt_clear_stall(uint8_t rhport, uint8_t endpoint) {
    (void)rhport;
    endpoint_busy[endpoint] = false;
}
/* Stalling drops whatever the controller had armed on the endpoint. */
void dcd_edpt_stall(uint8_t rhport, uint8_t endpoint) { (void)rhport; (void)endpoint; }
void dcd_edpt_clear_stall(uint8_t rhport, uint8_t endpoint) { (void)rhport; (void)endpoint; }
static usb_device_dpram_t dpram;
usb_device_dpram_t *const usb_dpram = &dpram;
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr);

static uint8_t card_type(void) { return 2; }
static const mca_card_t card = {card_type, NULL};

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
    assert(!endpoint_busy[endpoint]);
    endpoint_busy[endpoint] = true;
    transfers[transfer_count++] = (transfer_t){endpoint, buffer, length};
    return true;
}

typedef struct {
    tusb_desc_interface_t interface_desc;
    tusb_desc_endpoint_t endpoints[3];
} interface_descriptor_t;

static usbd_class_driver_t const *open_device(void) {
    usbd_class_driver_t const *driver = mca_device_driver();
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
    endpoint_busy[EP_OUT] = false;
    assert(driver->xfer_cb(0, EP_OUT, XFER_RESULT_SUCCESS, length));
}

int main(void) {
    usbd_class_driver_t const *driver;
    static uint8_t raw_request[64];
    size_t first_in_index;

    mca_protocol_init();
    mca_protocol_attach_card(&card);
    mca_device_init(true, mca_protocol_callbacks());
    driver = open_device();
    mca_protocol_task(); /* The protocol explicitly arms OUT while idle. */

    raw_request[0] = 0xaa;
    raw_request[1] = 0x42;
    raw_request[2] = 60;
    raw_request[3] = 0;
    for (size_t i = 4; i < sizeof(raw_request); ++i)
        raw_request[i] = (uint8_t)(i ^ 0x5a);
    receive_out(driver, raw_request, sizeof(raw_request));
    mca_protocol_task();
    assert(mca_bridge_begin(&mca_bridge));
    for (size_t i = 4; i < sizeof(raw_request); ++i) {
        uint8_t byte;
        assert(mca_bridge_receive(&mca_bridge, &byte));
        assert(byte == raw_request[i]);
        mca_bridge_respond(&mca_bridge, byte);
    }
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();

    first_in_index = transfer_count - 1;
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING);
    assert(!mca_bridge_begin(&mca_bridge));
    assert(transfers[first_in_index].endpoint == EP_IN);
    assert(transfers[first_in_index].length == 64);
    assert(transfers[first_in_index].buffer[0] == 0x55);
    assert(transfers[first_in_index].buffer[1] == 0x5a);
    assert(transfers[first_in_index].buffer[2] == 60);
    assert(transfers[first_in_index].buffer[3] == 0);
    for (size_t i = 4; i < 64; ++i)
        assert(transfers[first_in_index].buffer[i] == raw_request[i]);

    endpoint_busy[EP_IN] = false;
    assert(driver->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, 64));
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    assert(transfer_count == first_in_index + 2); /* Re-arm OUT after release. */

    const uint8_t detect_request[] = {0xaa, 0x40};
    size_t before_detect = transfer_count;
    receive_out(driver, detect_request, sizeof(detect_request));
    mca_protocol_task();
    assert(transfer_count == before_detect + 1);
    assert(transfers[transfer_count - 1].endpoint == EP_IN);
    assert(transfers[transfer_count - 1].length == 2);
    assert(transfers[transfer_count - 1].buffer[0] == 0x55);
    assert(transfers[transfer_count - 1].buffer[1] == 2);
    endpoint_busy[EP_IN] = false;
    assert(driver->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, 2));
    mca_protocol_task();
    assert(transfer_count == before_detect + 2);

    /* The host abandons a reply (e.g. the XMB leaves the memory card screen
     * mid-request) and clears the IN halt before talking to us again. The old
     * reply must not be read as the answer to the next command. */
    receive_out(driver, detect_request, sizeof(detect_request));
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING);
    const tusb_control_request_t clear_halt = {
        .bmRequestType = 0x02, .bRequest = TUSB_REQ_CLEAR_FEATURE,
        .wValue = TUSB_REQ_FEATURE_EDPT_HALT, .wIndex = EP_IN,
    };
    memcpy((void *)dpram.setup_packet, &clear_halt, sizeof(clear_halt));
    tud_event_hook_cb(0, DCD_EVENT_SETUP_RECEIVED, true);
    mca_protocol_task(); /* before tud_task: the reset waits for TinyUSB */
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING);
    usbd_edpt_clear_stall(0, EP_IN); /* tud_task: TinyUSB's own handling */
    (void)driver->control_xfer_cb(0, 0 /* SETUP */, &clear_halt);
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    assert(!endpoint_busy[EP_IN]);

    const uint8_t raw_short[] = {0xaa, 0x42, 1, 0, 0x81};
    size_t before_raw = transfer_count;
    receive_out(driver, raw_short, sizeof(raw_short));
    mca_protocol_task();
    assert(mca_bridge_begin(&mca_bridge));
    uint8_t byte;
    assert(mca_bridge_receive(&mca_bridge, &byte) && byte == 0x81);
    mca_bridge_respond(&mca_bridge, 0x2b);
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    assert(transfer_count > before_raw);
    transfer_t reply = transfers[transfer_count - 1];
    assert(reply.endpoint == EP_IN && reply.length == 5);
    assert(reply.buffer[0] == 0x55 && reply.buffer[1] == 0x5a && reply.buffer[4] == 0x2b);
    return 0;
}
