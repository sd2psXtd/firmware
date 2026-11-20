#include "mca_device.h"

#include "device/dcd.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "usb_drivers.h"

#include <string.h>

#ifndef MCA_DEVICE_SETUP_PACKET
#include "hardware/structs/usb.h"
/* The SETUP packet the controller just received (RP2040 DPRAM). */
#define MCA_DEVICE_SETUP_PACKET (usb_dpram->setup_packet)
#endif

#define MCA_PACKET_SIZE 64
#define MCA_INTERFACE_CLASS 0xff

/* Endpoint bits for pipe resets requested by the host. */
#define PIPE_IN  0x1u
#define PIPE_OUT 0x2u
#define PIPE_INT 0x4u
#define PIPE_ALL (PIPE_IN | PIPE_OUT | PIPE_INT)

static const uint8_t pipe_endpoints[] = { MCA_EP_IN, MCA_EP_OUT, MCA_EP_INT };

static bool profile_mca;
static mca_device_callbacks_t device_callbacks;
static union { uint32_t alignment; uint8_t bytes[MCA_PACKET_SIZE]; } ep_out_buffer;
static union { uint32_t alignment; uint8_t bytes[1]; } ep_interrupt_buffer;

/* Shared with the USB interrupt (tud_event_hook_cb). */
static volatile bool interface_open;
static volatile uint8_t interface_number;
static volatile uint8_t pipes_reset_pending;
/* Pending resets from CLEAR_FEATURE(ENDPOINT_HALT) that TinyUSB has not
 * processed yet. TinyUSB marks the endpoint idle when it does, so finishing
 * (and re-arming) before that would arm the endpoint twice and hit TinyUSB's
 * "ep was already available" panic. Released by mca_driver_control(). */
static volatile uint8_t pipes_reset_awaiting_stack;

static bool presence;
static bool presence_reported;
static bool presence_reported_valid;
static bool removal_pending;

/*
 * Which of our pipes a standard request resets. USB 2.0 requires both sides to
 * return the affected endpoints to DATA0 for SET_CONFIGURATION (even with the
 * current value), SET_INTERFACE and CLEAR_FEATURE(ENDPOINT_HALT). TinyUSB 0.18
 * ignores a repeated SET_CONFIGURATION, only acknowledges SET_INTERFACE, and
 * clears a halt without dropping a buffer that is still armed.
 */
static uint8_t pipe_of_endpoint(uint8_t endpoint) {
    for (size_t i = 0; i < sizeof(pipe_endpoints); ++i)
        if (endpoint == pipe_endpoints[i])
            return (uint8_t)(1u << i);
    return 0;
}

static bool is_clear_halt(const tusb_control_request_t *request) {
    return request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD &&
        request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_ENDPOINT &&
        request->bRequest == TUSB_REQ_CLEAR_FEATURE &&
        request->wValue == TUSB_REQ_FEATURE_EDPT_HALT;
}

static uint8_t pipes_reset_by(const tusb_control_request_t *request) {
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_STANDARD)
        return 0;
    switch (request->bmRequestType_bit.recipient) {
        case TUSB_REQ_RCPT_DEVICE:
            return request->bRequest == TUSB_REQ_SET_CONFIGURATION ? PIPE_ALL : 0;
        case TUSB_REQ_RCPT_INTERFACE:
            return request->bRequest == TUSB_REQ_SET_INTERFACE &&
                (request->wIndex & 0xff) == interface_number ? PIPE_ALL : 0;
        case TUSB_REQ_RCPT_ENDPOINT:
            return is_clear_halt(request) ? pipe_of_endpoint(request->wIndex & 0xff) : 0;
        default:
            return 0;
    }
}

/*
 * USB interrupt, at SETUP time. Buffer completions of the same interrupt were
 * already handled, and the host waits for this request's status stage before
 * using the pipes again, so dropping the armed buffers and resetting the data
 * toggles here cannot race with the host. Stalling overwrites the buffer
 * control (dropping AVAIL); clearing the stall resets the toggle to DATA0.
 */
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
    tusb_control_request_t request;

    if (eventid != DCD_EVENT_SETUP_RECEIVED || !in_isr || !profile_mca || !interface_open)
        return;
    memcpy(&request, (const void *)MCA_DEVICE_SETUP_PACKET, sizeof(request));
    uint8_t pipes = pipes_reset_by(&request);
    for (size_t i = 0; i < sizeof(pipe_endpoints); ++i) {
        if (pipes & (1u << i)) {
            dcd_edpt_stall(rhport, pipe_endpoints[i]);
            dcd_edpt_clear_stall(rhport, pipe_endpoints[i]);
        }
    }
    pipes_reset_pending |= pipes;
    if (is_clear_halt(&request))
        pipes_reset_awaiting_stack |= pipes;
}

/* Task side of a pipe reset: TinyUSB still considers the dropped transfers
 * busy, and the protocol must forget the requests and replies they carried. */
static void finish_pipes_reset(void) {
    uint32_t interrupts = save_and_disable_interrupts();
    uint8_t pipes = pipes_reset_pending & ~pipes_reset_awaiting_stack;
    pipes_reset_pending &= (uint8_t)~pipes;
    restore_interrupts(interrupts);

    if (!pipes || !interface_open)
        return;
    for (size_t i = 0; i < sizeof(pipe_endpoints); ++i)
        if (pipes & (1u << i))
            usbd_edpt_clear_stall(0, pipe_endpoints[i]);
    if (pipes & PIPE_INT)
        presence_reported_valid = false; /* The host expects the current state again. */
    if ((pipes & (PIPE_IN | PIPE_OUT)) && device_callbacks.pipes_reset)
        device_callbacks.pipes_reset((pipes & PIPE_IN) != 0, (pipes & PIPE_OUT) != 0);
}

/* Arm a transfer unless a host pipe reset is still being finished. Interrupts
 * are masked so a reset cannot slip in between the check and the arm. */
static bool arm(uint8_t endpoint, uint8_t *buffer, uint16_t length) {
    uint32_t interrupts = save_and_disable_interrupts();
    bool armed = interface_open && !pipes_reset_pending && !usbd_edpt_busy(0, endpoint) &&
        usbd_edpt_xfer(0, endpoint, buffer, length);
    restore_interrupts(interrupts);
    return armed;
}

static void service_presence(void) {
    bool next = removal_pending ? false : presence;
    if (!removal_pending && presence_reported_valid && presence_reported == next)
        return;
    /* The buffer of a report still in flight must not change: the
     * completion reads back what the host received from it. */
    if (!interface_open || usbd_edpt_busy(0, MCA_EP_INT))
        return;
    ep_interrupt_buffer.bytes[0] = next ? 1 : 0;
    (void)arm(MCA_EP_INT, ep_interrupt_buffer.bytes, sizeof(ep_interrupt_buffer.bytes));
}

void mca_device_init(bool mca_profile, const mca_device_callbacks_t *callbacks) {
    profile_mca = mca_profile;
    pipes_reset_awaiting_stack = 0;
    if (callbacks)
        device_callbacks = *callbacks;
    else
        memset(&device_callbacks, 0, sizeof(device_callbacks));
    interface_open = false;
    pipes_reset_pending = 0;
    presence = false;
    presence_reported = false;
    presence_reported_valid = false;
    removal_pending = false;
}

bool mca_device_profile_is_mca(void) {
    return profile_mca;
}

bool mca_device_send(const uint8_t *data, size_t length) {
    if (!profile_mca || !data || length == 0 || length > MCA_DEVICE_MAX_TRANSFER ||
        ((uintptr_t)data & 3u) != 0)
        return false;
    return arm(MCA_EP_IN, (uint8_t *)(uintptr_t)data, (uint16_t)length);
}

void mca_device_set_presence(bool present) {
    if (presence && !present)
        removal_pending = true;
    presence = present;
}

void mca_device_receive(void) {
    (void)arm(MCA_EP_OUT, ep_out_buffer.bytes, sizeof(ep_out_buffer.bytes));
}

void mca_device_task(void) {
    finish_pipes_reset();
    service_presence();
}

static void mca_driver_init(void) {
    interface_open = false;
    pipes_reset_pending = 0;
    pipes_reset_awaiting_stack = 0;
    presence_reported_valid = false;
    removal_pending = false;
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
    bool out_open = false, in_open = false, interrupt_open = false;

    if (!profile_mca || interface_desc->bInterfaceClass != MCA_INTERFACE_CLASS ||
        interface_desc->bInterfaceSubClass != 0 || interface_desc->bInterfaceProtocol != 0xff ||
        interface_desc->bNumEndpoints != 3 || max_length < interface_desc->bLength + 21)
        return 0;

    descriptor = (uint8_t const *)interface_desc + interface_desc->bLength;
    consumed = interface_desc->bLength;
    for (uint8_t endpoint_index = 0; endpoint_index < 3; ++endpoint_index) {
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

    interface_number = interface_desc->bInterfaceNumber;
    interface_open = true;
    service_presence();
    return consumed;
}

static bool mca_driver_control(uint8_t rhport, uint8_t stage,
                               tusb_control_request_t const *request) {
    (void)rhport;
    /* TinyUSB forwards CLEAR_FEATURE(ENDPOINT_HALT) here after it has marked
     * the endpoint idle: the pipe reset begun by tud_event_hook_cb may now be
     * finished. Standard requests are otherwise handled by TinyUSB plus the
     * hook; the MCA interface has no class or vendor requests. */
    if (stage == CONTROL_STAGE_SETUP && is_clear_halt(request)) {
        uint32_t interrupts = save_and_disable_interrupts();
        pipes_reset_awaiting_stack &= (uint8_t)~pipe_of_endpoint(request->wIndex & 0xff);
        restore_interrupts(interrupts);
    }
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

usbd_class_driver_t const *mca_device_driver(void) {
    return &mca_driver;
}
