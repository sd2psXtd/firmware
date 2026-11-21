#include "../../src/usb/mca_device.h"
#include "../../src/usb/usb_drivers.h"
#include "../../src/usb/usb_serial.h"
#include "../../src/usb/usb_session.h"

#include "device/dcd.h"
#include "hardware/structs/usb.h"
#include "pico/stdio_usb/reset_interface.h"
#include "settings.h"
#include "usb/mca_protocol.h"

#include <assert.h>
#include <string.h>

enum { EP_IN = 0x81, EP_OUT = 0x02, EP_INT = 0x83 };

void tud_cdc_line_coding_cb(uint8_t interface_number, cdc_line_coding_t const *coding);
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr);

typedef struct {
    uint8_t endpoint;
    uint8_t *buffer;
    uint32_t length;
    uint8_t int_value;
} xfer_record_t;

static xfer_record_t transfers[128];
static size_t transfer_count;
static bool fail_next_xfer;
static bool endpoint_busy[256];
/* RP2040 buffer control AVAIL: arming an endpoint that is still armed in
 * hardware is TinyUSB's panic("ep %02X was already available"). */
static bool hw_armed[256];
static int usb_mode;
static bool usb_initialized;
static unsigned disconnects, cdc_starts, cdc_stops, reply_drains, main_loop_polls;
static bool serial_background;
int settings_get_usb_mode(bool current) { assert(current); return usb_mode; }
bool tud_inited(void) { return usb_initialized; }
bool tusb_init(void) {
    assert(!usb_initialized);
    usb_initialized = true;
    return true;
}
bool tud_disconnect(void) { assert(usb_initialized); ++disconnects; return true; }
bool tud_deinit(uint8_t rhport) { assert(rhport == 0); usb_initialized = false; return true; }
void usb_serial_start(bool background) {
    assert(usb_initialized);
    serial_background = background;
    ++cdc_starts;
}
void usb_serial_stop(void) {
    assert(usb_initialized);
    serial_background = false;
    ++cdc_stops;
}
bool usb_serial_in_background(void) { return serial_background; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) {
    assert(timeout_ms == 0 && !in_isr && usb_initialized);
    ++main_loop_polls;
}
void sleep_ms(uint32_t ms) { (void)ms; }
void board_init(void) {}
void mca_protocol_init(void) {}
bool mca_protocol_idle(void) { return true; }
void mca_protocol_reset(void) {}
void mca_protocol_drain_reply(uint32_t ms) { (void)ms; ++reply_drains; }

static usb_device_dpram_t dpram;
usb_device_dpram_t *const usb_dpram = &dpram;

/* Hardware-level pipe resets done from the SETUP interrupt, in call order. */
static struct { uint8_t endpoint; bool stall; } dcd_calls[16];
static size_t dcd_call_count;
static unsigned clear_stall_calls[256];
void dcd_edpt_stall(uint8_t rhport, uint8_t endpoint) {
    assert(rhport == 0 && dcd_call_count < sizeof(dcd_calls) / sizeof(dcd_calls[0]));
    hw_armed[endpoint] = false; /* Stalling overwrites the buffer control. */
    dcd_calls[dcd_call_count].endpoint = endpoint;
    dcd_calls[dcd_call_count++].stall = true;
}
void dcd_edpt_clear_stall(uint8_t rhport, uint8_t endpoint) {
    assert(rhport == 0 && dcd_call_count < sizeof(dcd_calls) / sizeof(dcd_calls[0]));
    dcd_calls[dcd_call_count].endpoint = endpoint;
    dcd_calls[dcd_call_count++].stall = false;
}

bool usbd_edpt_busy(uint8_t rhport, uint8_t endpoint) {
    (void)rhport;
    return endpoint_busy[endpoint];
}
void usbd_edpt_clear_stall(uint8_t rhport, uint8_t endpoint) {
    (void)rhport;
    endpoint_busy[endpoint] = false;
    ++clear_stall_calls[endpoint];
}
static unsigned bus_reset_calls;
static unsigned in_complete_calls;
static bool last_in_success;
static uint32_t last_in_transferred;
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
    assert(!endpoint_busy[endpoint_address]);
    assert(!hw_armed[endpoint_address]);
    endpoint_busy[endpoint_address] = true;
    hw_armed[endpoint_address] = true;
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

static unsigned pipes_reset_calls;
static bool last_reset_in, last_reset_out;
static void on_bus_reset(void) { ++bus_reset_calls; }
static void on_pipes_reset(bool in, bool out) {
    ++pipes_reset_calls;
    last_reset_in = in;
    last_reset_out = out;
}
static void on_in_complete(bool success, uint32_t transferred) {
    ++in_complete_calls;
    last_in_success = success;
    last_in_transferred = transferred;
}

static const mca_device_callbacks_t callbacks = {
    .in_complete = on_in_complete,
    .bus_reset = on_bus_reset,
    .pipes_reset = on_pipes_reset,
};

const mca_device_callbacks_t *mca_protocol_callbacks(void) { return &callbacks; }

static usbd_class_driver_t const *driver(void) { return mca_device_driver(); }
static usbd_class_driver_t const *reset_driver(void) { return usb_reset_itf_driver(); }

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
    memset(endpoint_busy, 0, sizeof(endpoint_busy));
    memset(hw_armed, 0, sizeof(hw_armed));
    assert(open_mca_interface() == 30);
}

static void complete_in(uint32_t transferred) {
    endpoint_busy[EP_IN] = hw_armed[EP_IN] = false;
    assert(driver()->xfer_cb(0, EP_IN, XFER_RESULT_SUCCESS, transferred));
}

static void complete_interrupt(void) {
    endpoint_busy[EP_INT] = hw_armed[EP_INT] = false;
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
    assert(!mca_device_send(non_multiple, sizeof(non_multiple_storage.bytes)));
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
    reset_driver()->init();
    assert(driver()->open(0, &reset_interface, sizeof(reset_interface)) == 0);
    assert(reset_driver()->open(0, &reset_interface, sizeof(reset_interface)) == sizeof(reset_interface));

    request.bmRequestType_bit.recipient = TUSB_REQ_RCPT_INTERFACE;
    request.wIndex = 2;
    request.bRequest = RESET_REQUEST_BOOTSEL;
    request.wValue = 0x700 | 0x35;
    assert(!driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(reset_driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(bootsel_calls == 1);
    assert(last_bootsel_gpio == 3);
    assert(last_bootsel_mask == 0x35);
    assert(last_bootsel_active_low);
    assert(reset_driver()->control_xfer_cb(0, CONTROL_STAGE_ACK, &request));
    request.wIndex = 1;
    assert(!reset_driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));

    request.wIndex = 2;
    request.bRequest = RESET_REQUEST_FLASH;
    assert(reset_driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
    assert(flash_reboot_calls == 1);
    tud_cdc_line_coding_cb(0, &line_coding);
    assert(bootsel_calls == 2);

    driver()->reset(0);
    reset_driver()->reset(0);
    assert(bus_reset_calls == 1);
    assert(!reset_driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request));
}

static void test_driver_registration(void) {
    uint8_t count = 0;
    usbd_class_driver_t const *drivers = usbd_app_driver_get_cb(&count);
    assert(count == 2);
    assert(drivers[0].open == driver()->open);
    assert(drivers[1].open == reset_driver()->open);
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
    mca_device_init(true, &callbacks);
    driver()->init();
    mca_device_set_presence(true);
    setup_mca_interface();
    assert(transfer_count == 1);
    assert(transfers[0].endpoint == EP_INT && transfers[0].int_value == 1);

    /* Removal and reinsertion happen while the initial insertion event is
     * still in flight. The host must observe insertion, removal, insertion. */
    mca_device_set_presence(false);
    mca_device_set_presence(true);
    before = transfer_count;
    mca_device_task();
    assert(transfer_count == before); /* Initial event still owns INT. */
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
    before = transfer_count;
    mca_device_task();
    assert(transfer_count == before); /* Already delivered: no duplicate insertion. */
}

/* Boot: the Serial profile starts from the flash settings, before any mode. */
static void test_usb_session_init(void) {
    usb_mode = USB_MODE_MCA;
    usb_session_init();
    assert(!usb_initialized && cdc_starts == 0);

    usb_mode = USB_MODE_UART;
    usb_session_init();
    assert(usb_initialized && cdc_starts == 1 && serial_background);
    /* The mode loop's prepare/start keeps the running session. */
    usb_session_prepare();
    usb_session_start();
    assert(usb_initialized && cdc_starts == 1 && disconnects == 0);

    usb_session_end_mode();
    usb_mode = USB_MODE_MCA;
    usb_session_prepare();
    assert(!usb_initialized && cdc_stops == 1 && disconnects == 1);
    cdc_starts = cdc_stops = disconnects = 0;
}

static void test_usb_session_switch(void) {
    usb_mode = USB_MODE_UART;
    usb_session_prepare();
    usb_session_start();
    usb_session_end_mode();
    usb_session_prepare();
    usb_session_start();
    assert(usb_initialized && cdc_starts == 1 && cdc_stops == 0 && disconnects == 0);
    /* Serial is serviced in the background: the main loop must not poll. */
    assert(serial_background);
    usb_session_poll();
    assert(main_loop_polls == 0);

    /* Settings reloaded between modes can still replace the Serial profile. */
    usb_session_end_mode();
    usb_mode = USB_MODE_MCA;
    usb_session_prepare();
    assert(!usb_initialized && mca_device_profile_is_mca() && disconnects == 1);
    assert(cdc_stops == 1);
    usb_session_start();
    assert(cdc_starts == 1 && !serial_background);
    usb_session_poll();
    assert(main_loop_polls == 1);

    usb_mode = USB_MODE_MASS;
    usb_session_end_mode();
    assert(!usb_initialized && reply_drains == 1 && disconnects == 2);
    usb_session_prepare();
    usb_session_start();
    /* Mass storage has CDC stdio too, but stays main-loop polled. */
    assert(usb_session_get_mode() == USB_MODE_MASS && cdc_starts == 2 && !serial_background);
    usb_session_poll();
    assert(main_loop_polls == 2);

    /* Eject ends USB before storage deinit restores the underlying profile. */
    usb_session_end_mode();
    assert(!usb_initialized && cdc_stops == 2 && disconnects == 3);
}

/* The controller received a SETUP packet: run the interrupt-time hook. */
static void setup_received(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index) {
    tusb_control_request_t setup = {
        .bmRequestType = request_type, .bRequest = request, .wValue = value, .wIndex = index,
    };
    memcpy((void *)dpram.setup_packet, &setup, sizeof(setup));
    tud_event_hook_cb(0, DCD_EVENT_SETUP_RECEIVED, true);
}

/* tud_task processing CLEAR_FEATURE(ENDPOINT_HALT): TinyUSB marks the
 * endpoint idle (without touching an armed buffer), then forwards the request
 * to the endpoint's driver. */
static void stack_clear_halt(uint8_t endpoint) {
    tusb_control_request_t request = {
        .bmRequestType = 0x02, .bRequest = TUSB_REQ_CLEAR_FEATURE,
        .wValue = TUSB_REQ_FEATURE_EDPT_HALT, .wIndex = endpoint,
    };
    usbd_edpt_clear_stall(0, endpoint);
    (void)driver()->control_xfer_cb(0, CONTROL_STAGE_SETUP, &request);
}

static bool dcd_reset(uint8_t endpoint) {
    for (size_t i = 0; i + 1 < dcd_call_count; ++i)
        if (dcd_calls[i].endpoint == endpoint && dcd_calls[i].stall &&
            dcd_calls[i + 1].endpoint == endpoint && !dcd_calls[i + 1].stall)
            return true;
    return false;
}

/* Open the interface with a reply on IN, OUT armed and presence delivered. */
static void open_busy_interface(uint8_t *reply, size_t length) {
    transfer_count = dcd_call_count = pipes_reset_calls = in_complete_calls = 0;
    memset(clear_stall_calls, 0, sizeof(clear_stall_calls));
    mca_device_init(true, &callbacks);
    driver()->init();
    mca_device_set_presence(true);
    setup_mca_interface();
    complete_interrupt();
    mca_device_receive();
    assert(mca_device_send(reply, length));
    assert(endpoint_busy[EP_IN] && endpoint_busy[EP_OUT] && !endpoint_busy[EP_INT]);
}

static void test_reopen_resets_all_pipes(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    /* bmRequestType 0x00/0x01: standard request to the device/interface. */
    const struct { uint8_t type, request; uint16_t value, index; } reopen[] = {
        {0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0}, /* same configuration again */
        {0x01, TUSB_REQ_SET_INTERFACE, 0, 0},
    };

    for (size_t i = 0; i < sizeof(reopen) / sizeof(reopen[0]); ++i) {
        open_busy_interface(reply.bytes, sizeof(reply.bytes));
        setup_received(reopen[i].type, reopen[i].request, reopen[i].value, reopen[i].index);
        /* Interrupt time: armed buffers dropped, toggles back to DATA0. */
        assert(dcd_reset(EP_IN) && dcd_reset(EP_OUT) && dcd_reset(EP_INT));
        assert(pipes_reset_calls == 0);

        /* Nothing can be armed until the task has finished the reset. */
        size_t before = transfer_count;
        assert(!mca_device_send(reply.bytes, sizeof(reply.bytes)));
        mca_device_receive();
        assert(transfer_count == before);

        mca_device_task();
        assert(clear_stall_calls[EP_IN] == 1 && clear_stall_calls[EP_OUT] == 1 &&
               clear_stall_calls[EP_INT] == 1);
        assert(pipes_reset_calls == 1 && last_reset_in && last_reset_out);
        assert(in_complete_calls == 0); /* The dropped reply never completes. */
        /* The reopened interrupt pipe gets the current state again. */
        assert(transfer_count == before + 1);
        assert(transfers[before].endpoint == EP_INT && transfers[before].int_value == 1);

        mca_device_receive();
        assert(mca_device_send(reply.bytes, sizeof(reply.bytes)));
        assert(transfers[transfer_count - 2].endpoint == EP_OUT);
        assert(transfers[transfer_count - 1].endpoint == EP_IN);
        complete_in(sizeof(reply.bytes));
        assert(in_complete_calls == 1);
    }
}

static void test_clear_halt_resets_one_pipe(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    const struct { uint8_t endpoint; bool in, out; } halts[] = {
        {EP_IN, true, false},
        {EP_OUT, false, true},
        {EP_INT, false, false},
    };

    for (size_t i = 0; i < sizeof(halts) / sizeof(halts[0]); ++i) {
        open_busy_interface(reply.bytes, sizeof(reply.bytes));
        if (halts[i].endpoint == EP_INT) {
            mca_device_set_presence(false); /* Arm a report so INT is busy. */
            mca_device_task();
            assert(endpoint_busy[EP_INT]);
        }
        /* 0x02: standard request to an endpoint. */
        setup_received(0x02, TUSB_REQ_CLEAR_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, halts[i].endpoint);
        assert(dcd_call_count == 2 && dcd_reset(halts[i].endpoint));
        /* The main loop may run before tud_task processes the request: the
         * reset must wait for TinyUSB, and nothing may be armed meanwhile. */
        mca_device_task();
        assert(pipes_reset_calls == 0 && clear_stall_calls[halts[i].endpoint] == 0);
        stack_clear_halt(halts[i].endpoint);
        mca_device_task();
        assert(clear_stall_calls[halts[i].endpoint] == 2); /* TinyUSB's and ours */
        for (size_t e = 0; e < sizeof(halts) / sizeof(halts[0]); ++e)
            if (halts[e].endpoint != halts[i].endpoint)
                assert(clear_stall_calls[halts[e].endpoint] == 0);
        if (halts[i].in || halts[i].out)
            assert(pipes_reset_calls == 1 && last_reset_in == halts[i].in &&
                   last_reset_out == halts[i].out);
        else
            assert(pipes_reset_calls == 0);
    }
}

/* Hardware regression (#128 follow-up): CLEAR_FEATURE(OUT) while OUT was armed.
 * When the main loop finished the reset before tud_task had processed the
 * request, it re-armed OUT; TinyUSB then marked OUT idle and the next arm hit
 * "ep 02 was already available", halting the device. */
static void test_clear_halt_out_waits_for_stack(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    open_busy_interface(reply.bytes, sizeof(reply.bytes));
    complete_in(sizeof(reply.bytes));

    setup_received(0x02, TUSB_REQ_CLEAR_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, EP_OUT);
    for (int pass = 0; pass < 3; ++pass) { /* main loop passes before tud_task */
        mca_device_task();
        mca_device_receive();
    }
    assert(!hw_armed[EP_OUT]);
    stack_clear_halt(EP_OUT);
    mca_device_task();
    mca_device_receive(); /* would trip the hw_armed assertion if armed twice */
    assert(hw_armed[EP_OUT] && pipes_reset_calls == 1 && last_reset_out && !last_reset_in);
    mca_device_receive();
    assert(hw_armed[EP_OUT]);
}

static void test_unrelated_requests_ignored(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    open_busy_interface(reply.bytes, sizeof(reply.bytes));

    setup_received(0x01, TUSB_REQ_SET_INTERFACE, 0, 1);              /* other interface */
    setup_received(0x02, TUSB_REQ_CLEAR_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, 0x84);
    setup_received(0x02, TUSB_REQ_SET_FEATURE, TUSB_REQ_FEATURE_EDPT_HALT, EP_IN);
    setup_received(0x02, TUSB_REQ_CLEAR_FEATURE, 1, EP_IN);          /* not ENDPOINT_HALT */
    setup_received(0x21, TUSB_REQ_SET_INTERFACE, 0, 0);              /* class request */
    setup_received(0x40, TUSB_REQ_SET_CONFIGURATION, 1, 0);          /* vendor request */
    tud_event_hook_cb(0, DCD_EVENT_SETUP_RECEIVED, false);           /* not the interrupt */
    tud_event_hook_cb(0, DCD_EVENT_SETUP_RECEIVED + 1, true);        /* other event */
    assert(dcd_call_count == 0);
    mca_device_task();
    assert(pipes_reset_calls == 0 && endpoint_busy[EP_IN] && endpoint_busy[EP_OUT]);

    /* Before the interface is opened, or in the Serial profile, the endpoints
     * do not belong to us. */
    mca_device_init(true, &callbacks);
    driver()->init();
    setup_received(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0);
    mca_device_init(false, &callbacks);
    driver()->init();
    setup_received(0x00, TUSB_REQ_SET_CONFIGURATION, 1, 0);
    assert(dcd_call_count == 0);
}

static void test_bus_reset_discards_pending_pipe_reset(void) {
    static union { uint32_t alignment; uint8_t bytes[4]; } reply;
    open_busy_interface(reply.bytes, sizeof(reply.bytes));
    setup_received(0x00, TUSB_REQ_SET_CONFIGURATION, 0, 0);
    driver()->reset(0); /* TinyUSB deconfigures: the endpoints are closed. */
    mca_device_task();
    assert(pipes_reset_calls == 0 && clear_stall_calls[EP_IN] == 0);
}

int main(void) {
    test_bulk_in_completion();
    test_reset_interface_ownership();
    test_driver_registration();
    test_reopen_resets_all_pipes();
    test_clear_halt_resets_one_pipe();
    test_clear_halt_out_waits_for_stack();
    test_unrelated_requests_ignored();
    test_bus_reset_discards_pending_pipe_reset();
    test_profile_switch();
    test_presence_ordering();
    test_usb_session_init();
    test_usb_session_switch();
    return 0;
}
