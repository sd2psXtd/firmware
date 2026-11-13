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

static bool card_ready, discard_reply;
static critical_section_t ready_lock;

static bool is_ready(void) {
    critical_section_enter_blocking(&ready_lock);
    bool ready = card_ready;
    critical_section_exit(&ready_lock);
    return ready;
}
static uint64_t frame_deadline;
#ifdef DEBUG_USB_UART
static bool debug_exit_requested;
static const uint8_t debug_exit_packet[] = {'M', 'C', 'A', '!'};
#endif

static uint8_t card_type(void) {
    /* Stop admitting new operations during a switch to storage; work already
     * owned by the bridge still drains through the normal protocol task. */
    if (settings_get_usb_mode(true) == USB_MODE_MASS || !is_ready() ||
        mca_bridge_cancelled(&mca_bridge))
        return 0;
    if (settings_get_mode(true) != MODE_PS2)
        return 1;
    return ps2_cardman_is_idle() && ps2_cardman_is_accessible() ? 2 : 0;
}

static void out_received(const uint8_t *data, uint16_t length) {
#ifdef DEBUG_USB_UART
    if (mca_device_profile_is_mca() && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE && frame.length == 0 &&
        length == sizeof(debug_exit_packet) &&
        memcmp(data, debug_exit_packet, sizeof(debug_exit_packet)) == 0) {
        debug_exit_requested = true;
        return;
    }
#endif
    if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE && frame.result == FRAME_MORE) {
        frame_feed(data, length);
        if (length)
            frame_deadline = time_us_64() + 500000;
    }
}

static void in_complete(bool success, uint32_t transferred) {
    (void)success;
    (void)transferred;
    mca_bridge_release(&mca_bridge);
}

static void bus_reset(void) {
    /* Accepted operations finish, including a page write's commit. Their old
     * reply is discarded; the worker's buffers remain untouched until then. */
    discard_reply = true;
    mca_bridge_release(&mca_bridge);
    frame_reset();
#ifdef DEBUG_USB_UART
    debug_exit_requested = false;
#endif
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
    card_ready = discard_reply = false;
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
    /* No worker work remains once USB owns the reply. Session teardown drains
     * that transfer separately before stopping USB. */
    return bridge_state == MCA_BRIDGE_IDLE || bridge_state == MCA_BRIDGE_SENDING;
}

void mca_protocol_reset(void) { bus_reset(); }

void mca_protocol_drain_reply(uint32_t milliseconds) {
    uint64_t end = time_us_64() + (uint64_t)milliseconds * 1000;
    while (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING && time_us_64() < end) {
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

static void process_frame(void) {
    uint8_t command = mca_bridge.input[1];
    uint8_t type = card_type();
    discard_reply = false;
    if (frame.result == FRAME_INVALID) {
        mca_bridge_reply(&mca_bridge, 0xaf);
    } else if (command == 0x40) {
        mca_bridge_reply(&mca_bridge, type);
    } else {
        bool valid = command == 0x42 && type;
        if ((command == 0x52 || command == 0x57) && type == 2 &&
            settings_get_ps2_variant() == PS2_VARIANT_RETAIL && ps2_mc_auth_isValid()) {
            uint32_t page = (uint32_t)mca_bridge.input[3] | ((uint32_t)mca_bridge.input[4] << 8) |
                ((uint32_t)mca_bridge.input[5] << 16) | ((uint32_t)mca_bridge.input[6] << 24);
            valid = ((uint64_t)page + 1) * 512 <= ps2_cardman_get_card_size();
        }
        if (!valid || !mca_bridge_submit(&mca_bridge, frame.length))
            mca_bridge_reply(&mca_bridge, 0xaf);
    }
    frame_reset();
}

void mca_protocol_task(void) {
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
    if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE) {
        if (frame.length && frame.result == FRAME_MORE && time_us_64() >= frame_deadline)
            frame.result = FRAME_INVALID;
        if (frame.result != FRAME_MORE)
            process_frame();
        else
            mca_device_receive();
    }
    mca_bridge_poll_timeout(&mca_bridge, time_us_64());
    if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_RESPONSE) {
        if (discard_reply)
            mca_bridge_release(&mca_bridge);
        else if (mca_device_send(mca_bridge.output, mca_bridge.output_length))
            mca_bridge_sending(&mca_bridge);
    }
}

const mca_device_callbacks_t *mca_protocol_callbacks(void) { return &callbacks; }
