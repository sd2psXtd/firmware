#include "ps2_card_backend_mca.h"
#include "usb/mca_bridge.h"
#include "mmceman/ps2_mmceman.h"

extern mca_bridge_t mca_bridge;

static void no_op(void) {}

static void finish(void) {
    if (mca_bridge_state(&mca_bridge) == MCA_BRIDGE_ACTIVE)
        mca_bridge_core1_finish(&mca_bridge);
    ps2_card_active = false;
}

/* Some command paths return to the main loop without calling finish(). The
 * next transaction boundary is the common completion point for those paths. */
static bool transaction_aborted(void) {
    finish();
    if (ps2_mc_exit_request)
        return false;
    /* MMCEMAN continuation callbacks run before receiveFirst. Claim their
     * next packet here, just as PIO waits for the next selection boundary. */
    if (mmceman_callback) {
        if (!mca_bridge_core1_begin(&mca_bridge))
            return false;
        ps2_card_active = true;
        mca_bridge_core1_respond(&mca_bridge, mmceman_tx_queued ? mmceman_tx_byte : 0xff);
        mmceman_tx_queued = false;
        mmceman_tx_byte = 0;
    }
    return true;
}

static void transaction_ready(void) {
    /* The retry counter simulates a physical card change for mcman. MCA has
     * an explicit card-presence notification instead. */
    mmceman_mcman_retry_counter = 0;
}

static uint8_t receive(uint8_t *byte) {
    return mca_bridge_core1_receive(&mca_bridge, byte) ? PS2_RECEIVE_OK : PS2_RECEIVE_RESET;
}

static uint8_t receive_first(uint8_t *byte) {
    while (!ps2_mc_exit_request) {
        if (mca_bridge_core1_begin(&mca_bridge)) {
            ps2_card_active = true;
            mca_bridge_core1_respond(&mca_bridge, 0xff);
            return receive(byte);
        }
    }
    return PS2_RECEIVE_EXIT;
}

static void respond(uint8_t byte) {
    mca_bridge_core1_respond(&mca_bridge, byte);
}

static bool probe(void) { return false; }

ps2_memory_card_backend_t ps2_card_backend_mca = {
    .init = no_op,
    .deinit = no_op,
    .reset = no_op,
    .startup = no_op,
    .shutdown = no_op,
    .receive = receive,
    .receiveFirst = receive_first,
    .respond = respond,
    .transactionAborted = transaction_aborted,
    .signalTransactionReady = transaction_ready,
    .abort = finish,
    .finish = finish,
    .probe = probe,
};
