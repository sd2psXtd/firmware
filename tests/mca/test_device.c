#include "../../src/usb/mca_device.h"

#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "pico/stdio_usb/reset_interface.h"

#include <assert.h>
#include <string.h>

enum { EP_IN = 0x81, EP_OUT = 0x02, EP_INT = 0x83 };

void tud_cdc_line_coding_cb(uint8_t interface_number, cdc_line_coding_t const *coding);

typedef struct {
    uint8_t endpoint;
    uint8_t *buffer;
    uint32_t length;
    uint8_t int_value;
} xfer_record_t;

static xfer_record_t transfers[128];
static size_t transfer_count;
static bool fail_next_xfer;
static unsigned bus_reset_calls;
static unsigned in_complete_calls;
static bool last_in_success;
static uint32_t last_in_transferred;
static unsigned presence_complete_calls;
static unsigned bootsel_calls;
static unsigned flash_reboot_calls;
static int last_bootsel_gpio;
static uint32_t last_bootsel_mask;
static bool last_bootsel_active_low;

bool usbd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *endpoint) {
    (void)rhport;
    return endpoint && endpoint->bDescriptorType == TUSB_DESC_ENDPOINT;
}

bool usbd_edpt_xfer(uint8_t rhport, uint8_t endpoint_address, uint8_t *buffer,
                    uint32_t total_bytes) {
    (void)rhport;
    assert(transfer_count < sizeof(transfers) / sizeof(transfers[0]));
    xfer_record_t *record = &transfers[transfer_count++];
    record->endpoint = endpoint_address;
    record->buffer = buffer;
    record->length = total_bytes;
    record->int_value = endpoint_address == EP_INT && buffer && total_bytes
        ? buffer[0]
        : 0xff;
    if (fail_next_xfer) {
        fail_next_xfer = false;
        return false;
    }
    return true;
}

void rom_reset_usb_boot_extra(int gpio, uint32_t disable_interface_mask,
                              bool activity_led_active_low) {
    ++bootsel_calls;
    last_bootsel_gpio = gpio;
    last_bootsel_mask = disable_interface_mask;
    last_bootsel_active_low = activity_led_active_low;
}

void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc;
    (void)sp;
    assert(delay_ms == 100);
    ++flash_reboot_calls;
}

static void on_bus_reset(void) { ++bus_reset_calls; }
static void on_in_complete(bool success, uint32_t transferred) {
    ++in_complete_calls;
    last_in_success = success;
    last_in_transferred = transferred;
}
static void on_presence_complete(bool success, uint32_t transferred) {
    (void)success;
    (void)transferred;
    ++presence_complete_calls;
}

static const mca_device_callbacks_t callbacks = {
    .in_complete = on_in_complete,
    .bus_reset = on_bus_reset,
    .presence_complete = on_presence_complete,
};

static usbd_class_driver_t const *driver(void) {
    uint8_t count = 0;
    usbd_class_driver_t const *result = usbd_app_driver_get_cb(&count);
    assert(result && count == 1);
    return result;
}

typedef struct {
    tusb_desc_interface_t interface_desc;
    tusb_desc_endpoint_t endpoints[3];
} mca_interface_descriptor_t;

static uint16_t open_mca_interface(void) {
    mca_interface_descriptor_t descriptor = {
        .interface_desc = {
            .bLength = 9,
            .bDescriptorType = 4,
            .bInterfaceNumber = 0,
            .bNumEndpoints = 3,
            .bInterfaceClass = 0xff,
            .bInterfaceSubClass = 0,
            .bInterfaceProtocol = 0xff,
        },
        .endpoints = {
            {7, TUSB_DESC_ENDPOINT, EP_IN, 2, {64, 0}, 0},
            {7, TUSB_DESC_ENDPOINT, EP_OUT, 2, {64, 0}, 0},
            {7, TUSB_DESC_ENDPOINT, EP_INT, 3, {1, 0}, 100},
        },
    };
    assert(sizeof(descriptor) == 30);
    return driver()->open(0, &descriptor.interface_desc, sizeof(descriptor));
}

static void setup_mca_interface(void) {
    assert(open_mca_interface() == 30);
}

static void complete_in(uint32_t transferred) {
    assert(driver()->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, transferred));
}

static void complete_interrupt(void) {
    assert(driver()->xfer_cb(0, EP_INT, XFER_RESULT_SUCCESS, 1));
}

static void test_bulk_in_completion(void) {
    static union { uint32_t alignment; uint8_t bytes[64]; } exact;
    static union { uint32_t alignment; uint8_t bytes[532]; } non_multiple_storage;
    uint8_t *exact_packet = exact.bytes;
    uint8_t *non_multiple = non_multiple_storage.bytes;
    size_t first_call;

    transfer_count = 0;
    in_complete_calls = 0;
    mca_device_init(true, &callbacks);
    driver()->init();
    setup_mca_interface();

    first_call = transfer_count;
    assert(mca_device_send(exact_packet, sizeof(exact.bytes)));
    assert(transfers[first_call].endpoint == EP_IN);
    assert(transfers[first_call].buffer == exact_packet);
    assert(transfers[first_call].length == sizeof(exact.bytes));
    complete_in(sizeof(exact.bytes));
    assert(transfer_count == first_call + 1); /* No extra packet after exact-size IN. */
    assert(in_complete_calls == 1 && last_in_success);
    assert(last_in_transferred == sizeof(exact.bytes));

    first_call = transfer_count;
    assert(mca_device_send(non_multiple, sizeof(non_multiple_storage.bytes)));
    complete_in(sizeof(non_multiple_storage.bytes));
    assert(transfer_count == first_call + 1);
    assert(in_complete_calls == 2 && last_in_success);
    assert(last_in_transferred == sizeof(non_multiple_storage.bytes));

    fail_next_xfer = true;
    assert(!mca_device_send(exact_packet, sizeof(exact.bytes)));
    assert(in_complete_calls == 2);
    assert(mca_device_send(non_multiple, sizeof(non_multiple_storage.bytes)));
    complete_in(sizeof(non_multiple_storage.bytes));
    assert(in_complete_calls == 3 && last_in_success);

    bus_reset_calls = 0;
    assert(mca_device_send(exact_packet, sizeof(exact.bytes)));
    driver()->reset(0);
    assert(bus_reset_calls == 1);
    assert(in_complete_calls == 3); /* bus_reset owns abandoning the pending send */
    assert(!mca_device_send(non_multiple, sizeof(non_multiple_storage.bytes)));
}

static void test_reset_interface_ownership(void) {
    tusb_desc_interface_t reset_interface = {
        9, 4, 2, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
        RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, 5,
    };
    tusb_control_request_t request = {0};
    cdc_line_coding_t line_coding = {.bit_rate = 1200};

    bootsel_calls = flash_reboot_calls = bus_reset_calls = 0;
    mca_device_init(false, &callbacks);
    driver()->init();
    assert(driver()->open(0, &reset_interface, sizeof(reset_interface)) == sizeof(reset_interface));

    request.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
    request.wIndex = 2;
    request.bRequest = RESET_REQUEST_BOOTSEL;
    request.wValue = 0x700 | 0x35;
    assert(driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(bootsel_calls == 1);
    assert(last_bootsel_gpio == 3);
    assert(last_bootsel_mask == 0x35);
    assert(last_bootsel_active_low);
    assert(driver()->control_xfer_cb(0, CONTROL_STAGE_ACK, &request));
    request.wIndex = 1;
    assert(!driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));

    request.wIndex = 2;
    request.bRequest = RESET_REQUEST_FLASH;
    assert(driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(flash_reboot_calls == 1);
    tud_cdc_line_coding_cb(0, &line_coding);
    assert(bootsel_calls == 2);

    driver()->reset(0);
    assert(bus_reset_calls == 1);
    assert(!driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
}

static void test_profile_switch(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    transfer_count = bus_reset_calls = in_complete_calls = 0;
    mca_device_init(true, &callbacks);
    driver()->init();
    setup_mca_interface();
    assert(mca_device_send(reply.bytes, sizeof(reply.bytes)));
    driver()->reset(0);
    assert(bus_reset_calls == 1 && in_complete_calls == 0);

    mca_device_init(false, &callbacks);
    driver()->init();
    assert(!mca_device_profile_is_mca());
    assert(open_mca_interface() == 0);
    assert(!mca_device_send(reply.bytes, sizeof(reply.bytes)));

    mca_device_init(true, &callbacks);
    driver()->init();
    assert(mca_device_profile_is_mca());
    setup_mca_interface();
    assert(mca_device_send(reply.bytes, sizeof(reply.bytes)));
    complete_in(sizeof(reply.bytes));
    assert(in_complete_calls == 1 && last_in_success);
}

static void test_presence_ordering(void) {
    size_t before;

    transfer_count = 0;
    presence_complete_calls = 0;
    mca_device_init(true, &callbacks);
    driver()->init();
    mca_device_set_presence(true);
    setup_mca_interface();
    assert(transfer_count >= 2);
    assert(transfers[1].endpoint == EP_INT && transfers[1].int_value == 1);

    /* Removal and reinsertion happen while the initial insertion event is
     * still in flight. The host must observe insertion, removal, insertion. */
    mca_device_set_presence(false);
    mca_device_set_presence(true);
    complete_interrupt();
    mca_device_task();
    before = transfer_count;
    assert(transfers[before - 1].endpoint == EP_INT);
    assert(transfers[before - 1].int_value == 0);
    complete_interrupt();
    mca_device_task();
    assert(transfers[transfer_count - 1].endpoint == EP_INT);
    assert(transfers[transfer_count - 1].int_value == 1);
    complete_interrupt();
    assert(presence_complete_calls == 3);
}

int main(void) {
    test_bulk_in_completion();
    test_reset_interface_ownership();
    test_profile_switch();
    test_presence_ordering();
    return 0;
}
