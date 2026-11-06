#include "mca_bridge.h"

void mca_bridge_init(mca_bridge_t *bridge) {
    critical_section_init(&bridge->lock);
    bridge->cancel_requested = false;
    bridge->input_length = bridge->input_offset = bridge->input_position = 0;
    bridge->output_length = bridge->output_position = 0;
    bridge->state = MCA_BRIDGE_IDLE;
}

uint8_t *mca_bridge_core0_request_buffer(mca_bridge_t *bridge) {
    return mca_bridge_state(bridge) == MCA_BRIDGE_IDLE ? bridge->input : NULL;
}

bool mca_bridge_core0_publish_request(mca_bridge_t *bridge, size_t offset,
                                      size_t length) {
    if (!length || length > MCA_BRIDGE_BUFFER_MAX || offset > MCA_BRIDGE_RX_CAPACITY ||
        length > MCA_BRIDGE_RX_CAPACITY - offset)
        return false;
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->state != MCA_BRIDGE_IDLE) {
        critical_section_exit(&bridge->lock);
        return false;
    }
    bridge->cancel_requested = false;
    bridge->input_offset = (uint16_t)offset;
    bridge->input_length = (uint16_t)length;
    bridge->input_position = 0;
    bridge->output_length = bridge->output_position = 0;
    bridge->state = MCA_BRIDGE_REQUEST;
    critical_section_exit(&bridge->lock);
    return true;
}

bool mca_bridge_cancel(mca_bridge_t *bridge) {
    bool pending;
    critical_section_enter_blocking(&bridge->lock);
    pending = bridge->state == MCA_BRIDGE_REQUEST || bridge->state == MCA_BRIDGE_ACTIVE;
    if (pending)
        bridge->cancel_requested = true;
    critical_section_exit(&bridge->lock);
    /* This is a request, not a synchronous cancellation: core 1 owns the
     * request until it observes the flag and finishes. */
    return pending;
}

uint8_t *mca_bridge_core0_response_buffer(mca_bridge_t *bridge,
                                          size_t *length) {
    if (mca_bridge_state(bridge) != MCA_BRIDGE_RESPONSE)
        return NULL;
    if (length)
        *length = bridge->output_length;
    return bridge->output + 4;
}

bool mca_bridge_core0_release_response(mca_bridge_t *bridge) {
    bool released;
    critical_section_enter_blocking(&bridge->lock);
    released = bridge->state == MCA_BRIDGE_RESPONSE;
    if (released)
        bridge->state = MCA_BRIDGE_IDLE;
    critical_section_exit(&bridge->lock);
    return released;
}

bool mca_bridge_core1_begin(mca_bridge_t *bridge) {
    bool started;
    critical_section_enter_blocking(&bridge->lock);
    started = bridge->state == MCA_BRIDGE_REQUEST;
    if (started) {
        bridge->state = MCA_BRIDGE_ACTIVE;
        bridge->input_position = 0;
        bridge->output_position = 4;
    }
    critical_section_exit(&bridge->lock);
    return started;
}

bool mca_bridge_core1_receive(mca_bridge_t *bridge, uint8_t *byte) {
    bool received = false;
    critical_section_enter_blocking(&bridge->lock);
    if (byte && bridge->state == MCA_BRIDGE_ACTIVE && !bridge->cancel_requested &&
        bridge->input_position < bridge->input_length) {
        *byte = bridge->input[bridge->input_offset + bridge->input_position++];
        received = true;
    }
    critical_section_exit(&bridge->lock);
    return received;
}

bool mca_bridge_core1_respond(mca_bridge_t *bridge, uint8_t byte) {
    bool sent = false;
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->state == MCA_BRIDGE_ACTIVE && !bridge->cancel_requested &&
        bridge->output_position < MCA_BRIDGE_TX_CAPACITY) {
        bridge->output[bridge->output_position++] = byte;
        sent = true;
    }
    critical_section_exit(&bridge->lock);
    return sent;
}

bool mca_bridge_core1_finish(mca_bridge_t *bridge) {
    bool completed = false;
    critical_section_enter_blocking(&bridge->lock);
    if (bridge->state == MCA_BRIDGE_ACTIVE) {
        if (bridge->cancel_requested) {
            bridge->input_length = bridge->input_position = 0;
            bridge->output_length = bridge->output_position = 0;
            bridge->cancel_requested = false;
            bridge->state = MCA_BRIDGE_IDLE;
        } else {
            bridge->output_length = bridge->output_position - 4;
            bridge->state = MCA_BRIDGE_RESPONSE;
            completed = true;
        }
    }
    critical_section_exit(&bridge->lock);
    return completed;
}

mca_bridge_state_t mca_bridge_state(mca_bridge_t *bridge) {
    mca_bridge_state_t state;
    critical_section_enter_blocking(&bridge->lock);
    state = bridge->state;
    critical_section_exit(&bridge->lock);
    return state;
}
