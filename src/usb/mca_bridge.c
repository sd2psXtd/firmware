#include "mca_bridge.h"
#include "pico/time.h"

static void error_response(mca_bridge_t *bridge) {
    bridge->output[0] = 0x55;
    bridge->output[1] = 0xaf;
    bridge->output_length = 2;
    bridge->state = MCA_BRIDGE_RESPONSE;
}

void mca_bridge_init(mca_bridge_t *bridge) {
    critical_section_init(&bridge->lock);
    bridge->cancel_requested = false;
    bridge->deadline = 0;
    bridge->input_length = bridge->input_position = bridge->output_length = 0;
    bridge->state = MCA_BRIDGE_IDLE;
}

mca_bridge_state_t mca_bridge_state(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    mca_bridge_state_t state = bridge->state;
    critical_section_exit(&bridge->lock);
    return state;
}

bool mca_bridge_submit(mca_bridge_t *bridge, size_t length) {
    if (length < 5 || length > sizeof(bridge->input))
        return false;
    critical_section_enter_blocking(&bridge->lock);
    bool submitted = bridge->state == MCA_BRIDGE_IDLE;
    if (submitted) {
        bridge->cancel_requested = false;
        bridge->input_length = (uint16_t)length;
        bridge->deadline = time_us_64() + MCA_BRIDGE_TIMEOUT_US;
        bridge->state = MCA_BRIDGE_REQUEST;
    }
    critical_section_exit(&bridge->lock);
    return submitted;
}

void mca_bridge_reply(mca_bridge_t *bridge, uint8_t value) {
    /* Called by core 0 only while IDLE. */
    bridge->output[0] = 0x55;
    bridge->output[1] = value;
    bridge->output_length = 2;
    critical_section_enter_blocking(&bridge->lock);
    bridge->state = MCA_BRIDGE_RESPONSE;
    critical_section_exit(&bridge->lock);
}

void mca_bridge_poll_timeout(mca_bridge_t *bridge, uint64_t now_us) {
    critical_section_enter_blocking(&bridge->lock);
    if (now_us >= bridge->deadline) {
        if (bridge->state == MCA_BRIDGE_REQUEST)
            error_response(bridge);
        else if (bridge->state == MCA_BRIDGE_ACTIVE)
            bridge->cancel_requested = true;
    }
    critical_section_exit(&bridge->lock);
}

void mca_bridge_sending(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    bridge->state = MCA_BRIDGE_SENDING;
    critical_section_exit(&bridge->lock);
}

void mca_bridge_release(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->state == MCA_BRIDGE_RESPONSE || bridge->state == MCA_BRIDGE_SENDING) {
        bridge->cancel_requested = false;
        bridge->state = MCA_BRIDGE_IDLE;
    }
    critical_section_exit(&bridge->lock);
}

bool mca_bridge_begin(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    bool started = bridge->state == MCA_BRIDGE_REQUEST;
    if (started) {
        bridge->input_position = 4;
        bridge->output_length = 4;
        bridge->state = MCA_BRIDGE_ACTIVE;
    }
    critical_section_exit(&bridge->lock);
    return started;
}

bool mca_bridge_cancelled(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    bool cancelled = bridge->cancel_requested;
    critical_section_exit(&bridge->lock);
    return cancelled;
}

void mca_bridge_restart_timeout(mca_bridge_t *bridge) {
    critical_section_enter_blocking(&bridge->lock);
    bridge->deadline = time_us_64() + MCA_BRIDGE_TIMEOUT_US;
    critical_section_exit(&bridge->lock);
}

bool mca_bridge_receive(mca_bridge_t *bridge, uint8_t *byte) {
    /* Cursors and buffers belong exclusively to core 1 throughout ACTIVE. */
    if (mca_bridge_cancelled(bridge) || bridge->input_position == bridge->input_length)
        return false;
    *byte = bridge->input[bridge->input_position++];
    return true;
}

void mca_bridge_respond(mca_bridge_t *bridge, uint8_t byte) {
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->output_length == sizeof(bridge->output))
        bridge->cancel_requested = true;
    if (!bridge->cancel_requested)
        bridge->output[bridge->output_length++] = byte;
    critical_section_exit(&bridge->lock);
}

void mca_bridge_finish(mca_bridge_t *bridge, bool success) {
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->state == MCA_BRIDGE_ACTIVE) {
        if (!success || bridge->cancel_requested) {
            error_response(bridge);
        } else {
            if (bridge->input[1] == 0x42) {
                size_t length = bridge->output_length - 4;
                bridge->output[0] = 0x55;
                bridge->output[1] = 0x5a;
                bridge->output[2] = (uint8_t)length;
                bridge->output[3] = (uint8_t)(length >> 8);
            }
            bridge->state = MCA_BRIDGE_RESPONSE;
        }
    }
    critical_section_exit(&bridge->lock);
}
