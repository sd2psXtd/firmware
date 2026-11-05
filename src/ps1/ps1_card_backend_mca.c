#include "ps1_card_backend_mca.h"
#include "usb/mca_bridge.h"

extern mca_bridge_t mca_bridge;

static void no_op(void) {}

static void finish(void) {
    if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_ACTIVE)
        mca_bridge_core1_finish(&mca_bridge);
    ps1_card_active = false;
}

static uint8_t receive_first(uint8_t *byte) {
    finish();
    while (!ps1_mc_exit_request) {
        if (mca_bridge_core1_begin(&mca_bridge)) {
            ps1_card_active = true;
            mca_bridge_core1_respond(&mca_bridge, 0xff);
            ps1_mc_reset = 0;
            return mca_bridge_core1_receive(&mca_bridge, byte) ? PS1_RECEIVE_OK : PS1_RECEIVE_RESET;
        }
    }
    return PS1_RECEIVE_EXIT;
}

static uint8_t receive(uint8_t *byte) {
    return mca_bridge_core1_receive(&mca_bridge, byte) ? PS1_RECEIVE_OK : PS1_RECEIVE_RESET;
}

static void respond(uint8_t byte) {
    mca_bridge_core1_respond(&mca_bridge, byte);
}

ps1_memory_card_backend_t ps1_card_backend_mca = {
    .init = no_op,
    .deinit = no_op,
    .reset = no_op,
    .startup = no_op,
    .shutdown = no_op,
    .recv_cntrl = receive,
    .recv_mc = receive,
    .receive_first = receive_first,
    .respond = respond,
    .finish = finish,
};
