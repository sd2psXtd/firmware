#include "settings.h"
#include "ps2_card_backend_mca.h"
#include "usb/mca_bridge.h"
#include "usb/mca_page.h"
#include "usb/mca_protocol.h"
#include "mmceman/ps2_mmceman.h"

extern mca_bridge_t mca_bridge;

static void no_op(void) {}

static void startup(void) { mca_protocol_set_ready(true); }
static void shutdown(void) { mca_protocol_set_ready(false); }

static void finish(void) {
    mca_bridge_finish(&mca_bridge, true);
    ps2_card_active = false;
}

/* Wait at the operation boundary while core 0/USB owns the buffers. Page
 * operations execute entirely here on core 1 through the normal handlers. */
static bool begin(void) {
    while (!ps2_mc_exit_request) {
        if (!mca_bridge_begin(&mca_bridge))
            continue;
        ps2_card_active = true;
        if (mca_bridge.input[1] == 0x42)
            return true;
        bool success = mca_page_execute(&mca_bridge);
        mca_bridge_finish(&mca_bridge, success);
        ps2_card_active = false;
    }
    return false;
}

/* Some command paths return without finish(). Complete them before claiming
 * another operation, including MMCEMAN continuation packets. */
static bool transaction_aborted(void) {
    finish();
    if (ps2_mc_exit_request)
        return false;
    if (mmceman_callback) {
        if (!begin())
            return false;
        mca_bridge_respond(&mca_bridge, mmceman_tx_queued ? mmceman_tx_byte : 0xff);
        mmceman_tx_queued = false;
        mmceman_tx_byte = 0;
    }
    return true;
}

static void transaction_ready(void) {
    /* MCA has explicit card-presence notifications instead of PIO retries. */
    mmceman_mcman_retry_counter = 0;
}

static uint8_t receive(uint8_t *byte) {
    if (ps2_mc_exit_request)
        return PS2_RECEIVE_EXIT;
    bool received = mca_page_active() ? mca_page_receive(byte) : mca_bridge_receive(&mca_bridge, byte);
    return received ? PS2_RECEIVE_OK : PS2_RECEIVE_RESET;
}

static uint8_t receive_first(uint8_t *byte) {
    if (!begin())
        return PS2_RECEIVE_EXIT;
    mca_bridge_respond(&mca_bridge, 0xff);
    return receive(byte);
}

static void respond(uint8_t byte) {
    if (mca_page_active())
        mca_page_respond(byte);
    else
        mca_bridge_respond(&mca_bridge, byte);
}

static bool probe(void) { return false; }

static bool idle(void) {
    return settings_get_mode(true) == MODE_PS2 &&
        settings_get_mca_enabled() && mca_protocol_idle();
}

ps2_memory_card_backend_t ps2_card_backend_mca = {
    .idle = idle,
    .switch_delay = mca_protocol_switch_delay,
    .init = no_op,
    .deinit = no_op,
    .reset = no_op,
    .startup = startup,
    .shutdown = shutdown,
    .receive = receive,
    .receiveFirst = receive_first,
    .respond = respond,
    .transactionAborted = transaction_aborted,
    .signalTransactionReady = transaction_ready,
    .abort = finish,
    .finish = finish,
    .probe = probe,
};
