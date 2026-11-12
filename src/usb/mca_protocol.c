#include "mca_protocol.h"
#include "mca_bridge.h"
#include "mca_page.h"
#include "pico/time.h"
#include "tusb.h"
#include "ps2/ps2_cardman.h"
#include "ps2/card_emu/ps2_mc_auth.h"
#include "settings.h"

#include <string.h>

mca_bridge_t mca_bridge;

typedef enum {
    FRAME_MORE,
    FRAME_READY,
    FRAME_INVALID,
} frame_result_t;

typedef struct {
    size_t length;
    size_t expected;
    frame_result_t result;
} frame_t;

static frame_t frame;

static void frame_reset(void) {
    frame.length = 0;
    frame.expected = 0;
    frame.result = FRAME_MORE;
}

/* A completed frame stays owned by the protocol until reset. */
static void frame_feed(const uint8_t *data, size_t length) {
    size_t consumed = 0;

    while (consumed < length && frame.result == FRAME_MORE) {
        if (frame.length == sizeof(mca_bridge.input)) {
            frame.result = FRAME_INVALID;
            break;
        }
        mca_bridge.input[frame.length++] = data[consumed++];
        if (frame.length == 1 && mca_bridge.input[0] != 0xaa) {
            frame.result = FRAME_INVALID;
        } else if (frame.length == 2) {
            switch (mca_bridge.input[1]) {
                case 0x40: frame.expected = 2; break;
                case 0x42: break;
                case 0x52: frame.expected = 9; break;
                case 0x57: frame.expected = MCA_PAGE_RAW_SIZE + 9; break;
                default: frame.result = FRAME_INVALID; break;
            }
        } else if (frame.length == 4 && mca_bridge.input[1] == 0x42) {
            size_t payload = mca_bridge.input[2] | ((size_t)mca_bridge.input[3] << 8);
            if (!payload || payload > MCA_BRIDGE_BUFFER_MAX)
                frame.result = FRAME_INVALID;
            else
                frame.expected = payload + 4;
        }

        if (frame.expected > sizeof(mca_bridge.input))
            frame.result = FRAME_INVALID;
        else if (frame.expected && frame.length == frame.expected) {
            if ((mca_bridge.input[1] == 0x52 || mca_bridge.input[1] == 0x57) &&
                (mca_bridge.input[2] != 0x03 || mca_bridge.input[frame.length - 2] != 0x55 ||
                 mca_bridge.input[frame.length - 1] != 0x2b))
                frame.result = FRAME_INVALID;
            else
                frame.result = FRAME_READY;
        }
    }
}

enum { IDLE, RAW, PAGE, PAGE_REPLY, RECOVER };
static uint8_t state;
static bool card_ready, reply_pending, in_flight, bridge_reply;
static critical_section_t ready_lock;

static bool is_ready(void) {
    critical_section_enter_blocking(&ready_lock);
    bool ready = card_ready;
    critical_section_exit(&ready_lock);
    return ready;
}
static bool page_read;
static uint64_t deadline;
static uint64_t frame_deadline;
#ifdef DEBUG_USB_UART
static bool debug_exit_requested;
static const uint8_t debug_exit_packet[] = {'M', 'C', 'A', '!'};
#endif
static union { uint32_t alignment; uint8_t bytes[4]; } short_reply;

static uint8_t card_type(void) {
    /* Stop admitting new operations during a switch to storage; work already
     * owned by the bridge still drains through the normal protocol task. */
    if (settings_get_usb_mode(true) == USB_MODE_MASS || !is_ready() || state == RECOVER ||
        (state == PAGE && mca_page_is_cancelling()))
        return 0;
    if (settings_get_mode(true) != MODE_PS2)
        return 1;
    return ps2_cardman_is_idle() && ps2_cardman_is_accessible() ? 2 : 0;
}

static void out_received(const uint8_t *data, uint16_t length) {
#ifdef DEBUG_USB_UART
    if (mca_device_profile_is_mca() && state == IDLE && frame.length == 0 && !in_flight &&
        length == sizeof(debug_exit_packet) &&
        memcmp(data, debug_exit_packet, sizeof(debug_exit_packet)) == 0) {
        debug_exit_requested = true;
        return;
    }
#endif
    if (state == IDLE && !in_flight && frame.result == FRAME_MORE) {
        frame_feed(data, length);
        if (length)
            frame_deadline = time_us_64() + 500000;
    }
}

static void in_complete(bool success, uint32_t transferred) {
    (void)success;
    (void)transferred;
    in_flight = false;
    if (bridge_reply) {
        mca_bridge_core0_release_response(&mca_bridge);
        bridge_reply = false;
    }
}

static void bus_reset(void) {
    reply_pending = false;
    frame_reset();
#ifdef DEBUG_USB_UART
    debug_exit_requested = false;
#endif
    if (in_flight && bridge_reply)
        mca_bridge_core0_release_response(&mca_bridge);
    in_flight = bridge_reply = false;
}

static const mca_device_callbacks_t callbacks = {
    .out_received = out_received,
    .in_complete = in_complete,
    .bus_reset = bus_reset,
};

void mca_protocol_init(void) {
    critical_section_init(&ready_lock);
    mca_bridge_init(&mca_bridge);
    frame_reset();
    state = IDLE;
    card_ready = reply_pending = in_flight = bridge_reply = false;
#ifdef DEBUG_USB_UART
    debug_exit_requested = false;
#endif
}

void mca_protocol_set_ready(bool ready) {
    critical_section_enter_blocking(&ready_lock);
    card_ready = ready;
    critical_section_exit(&ready_lock);
}

bool mca_protocol_idle(void) {
    mca_bridge_state_t bridge_state = mca_bridge_state(&mca_bridge);
    return state == IDLE && (bridge_state == MCA_BRIDGE_IDLE ||
                             (in_flight && bridge_reply && bridge_state == MCA_BRIDGE_RESPONSE));
}

void mca_protocol_reset(void) { bus_reset(); }

void mca_protocol_drain_reply(uint32_t milliseconds) {
    uint64_t end = time_us_64() + (uint64_t)milliseconds * 1000;
    while (in_flight && time_us_64() < end) {
        tud_task_ext(0, false);
        sleep_ms(1);
    }
}

void mca_protocol_switch_delay(uint32_t milliseconds) {
    uint64_t end = time_us_64() + (uint64_t)milliseconds * 1000;
    while (time_us_64() < end) {
        tud_task_ext(0, false);
        mca_protocol_task();
        sleep_ms(1);
    }
}

static void short_response(uint8_t value) {
    short_reply.bytes[0] = 0x55;
    short_reply.bytes[1] = value;
    if (mca_device_send(short_reply.bytes, 2))
        in_flight = true;
}

static void process_frame(void) {
    uint32_t page;
    if (frame.result == FRAME_INVALID) {
        short_response(0xaf);
    } else if (mca_bridge.input[1] == 0x40) {
        short_response(card_type());
    } else if (mca_bridge.input[1] == 0x42 && card_type() &&
               mca_bridge_core0_publish_request(&mca_bridge, 4, frame.length - 4)) {
        state = RAW;
        reply_pending = true;
        deadline = time_us_64() + 1000000;
    } else if ((mca_bridge.input[1] == 0x52 || mca_bridge.input[1] == 0x57) &&
               card_type() == 2 && settings_get_ps2_variant() == PS2_VARIANT_RETAIL &&
               ps2_mc_auth_isValid()) {
        page = (uint32_t)mca_bridge.input[3] | ((uint32_t)mca_bridge.input[4] << 8) |
               ((uint32_t)mca_bridge.input[5] << 16) | ((uint32_t)mca_bridge.input[6] << 24);
        page_read = mca_bridge.input[1] == 0x52;
        if (((uint64_t)page + 1) * 512 <= ps2_cardman_get_card_size() &&
            mca_page_start(&mca_bridge, !page_read, page,
                           page_read ? NULL : mca_bridge.input + 7, time_us_64())) {
            state = PAGE;
            reply_pending = true;
        } else {
            short_response(0xaf);
        }
    } else {
        short_response(0xaf);
    }
    frame_reset();
}

void mca_protocol_task(void) {
    size_t length;
    uint8_t *response;

#ifdef DEBUG_USB_UART
    if (debug_exit_requested) {
        debug_exit_requested = false;
        mca_protocol_set_ready(false);
        frame_reset();
        settings_set_mca_enabled(false);
    }
#endif

    mca_device_set_presence(card_type() != 0);
    mca_device_task();
    if (state == IDLE && frame.length && frame.result == FRAME_MORE &&
        time_us_64() >= frame_deadline)
        frame.result = FRAME_INVALID;
    if (state == IDLE && !in_flight && frame.result != FRAME_MORE)
        process_frame();

    if (state == RAW) {
        response = mca_bridge_core0_response_buffer(&mca_bridge, &length);
        if (response) {
            if (!reply_pending) {
                mca_bridge_core0_release_response(&mca_bridge);
                state = IDLE;
            } else if (!in_flight) {
                response -= 4;
                response[0] = 0x55;
                response[1] = 0x5a;
                response[2] = (uint8_t)length;
                response[3] = (uint8_t)(length >> 8);
                if (mca_device_send(response, length + 4)) {
                    bridge_reply = in_flight = true;
                    state = IDLE;
                }
            }
        } else if (time_us_64() >= deadline) {
            mca_bridge_cancel(&mca_bridge);
            state = RECOVER;
        }
    } else if (state == PAGE) {
        mca_page_result_t result = mca_page_task(&mca_bridge, time_us_64());
        if (result == MCA_PAGE_READ_DONE || result == MCA_PAGE_WRITE_DONE) {
            state = PAGE_REPLY;
        } else if (result == MCA_PAGE_FAILED) {
            state = IDLE;
            if (reply_pending)
                short_response(0xaf);
        }
    } else if (state == RECOVER) {
        response = mca_bridge_core0_response_buffer(&mca_bridge, &length);
        if (response)
            mca_bridge_core0_release_response(&mca_bridge);
        if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE) {
            state = IDLE;
            if (reply_pending)
                short_response(0xaf);
        }
    }
    if (state == PAGE_REPLY) {
        response = mca_bridge_core0_response_buffer(&mca_bridge, &length);
        if (!reply_pending) {
            mca_bridge_core0_release_response(&mca_bridge);
            state = IDLE;
        } else if (response && !in_flight &&
                   mca_device_send(page_read ? response - 4 : response,
                                   page_read ? length + 4 : length)) {
            bridge_reply = in_flight = true;
            state = IDLE;
        }
    }
}

const mca_device_callbacks_t *mca_protocol_callbacks(void) { return &callbacks; }
