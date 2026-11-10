#include "mca_bridge.h"
#include "ps1_card_backend_mca.h"
#include "ps2_card_backend_mca.h"
#include <assert.h>
#include <string.h>

mca_bridge_t mca_bridge;
volatile bool ps1_card_active, ps2_card_active;
volatile int ps1_mc_reset, ps1_mc_exit_request, ps2_mc_exit_request;
volatile uint8_t mmceman_mcman_retry_counter;
volatile bool mmceman_tx_queued;
volatile uint8_t mmceman_tx_byte;
void (*mmceman_callback)(void);

static void continuation(void) {}

int main(void) {
    const uint8_t request[] = {0x81, 0x11, 0, 0};
    const uint8_t expected[] = {0xff, 0xff, 0x2b, 0x66};
    uint8_t byte, result[8];
    size_t length;
    mca_bridge_init(&mca_bridge);
    assert(mca_bridge_submit(&mca_bridge, request, sizeof(request)));
    assert(ps2_card_backend_mca.transactionAborted());
    mmceman_mcman_retry_counter = 5;
    ps2_card_backend_mca.signalTransactionReady();
    assert(mmceman_mcman_retry_counter == 0);
    assert(ps2_card_backend_mca.receiveFirst(&byte) == PS2_RECEIVE_OK && byte == 0x81);
    ps2_card_backend_mca.respond(0xff);
    assert(ps2_card_backend_mca.receive(&byte) == PS2_RECEIVE_OK && byte == 0x11);
    ps2_card_backend_mca.respond(0x2b);
    assert(ps2_card_backend_mca.receive(&byte) == PS2_RECEIVE_OK);
    ps2_card_backend_mca.respond(0x66);
    ps2_card_backend_mca.finish();
    assert(mca_bridge_take_response(&mca_bridge, result, sizeof(result), &length));
    assert(length == sizeof(expected) && memcmp(result, expected, length) == 0);

    /* A handler that returns early is completed at the next loop boundary. */
    assert(mca_bridge_submit(&mca_bridge, request, sizeof(request)));
    assert(ps2_card_backend_mca.receiveFirst(&byte) == PS2_RECEIVE_OK);
    assert(ps2_card_backend_mca.transactionAborted());
    assert(mca_bridge_take_response(&mca_bridge, result, sizeof(result), &length));
    assert(length == 1 && result[0] == 0xff);

    mmceman_callback = continuation;
    assert(!ps2_card_backend_mca.transactionAborted());
    mmceman_tx_queued = true;
    mmceman_tx_byte = 0x42;
    assert(mca_bridge_submit(&mca_bridge, request, sizeof(request)));
    assert(ps2_card_backend_mca.transactionAborted());
    assert(!mmceman_tx_queued);
    assert(ps2_card_backend_mca.receive(&byte) == PS2_RECEIVE_OK && byte == 0x81);
    assert(!ps2_card_backend_mca.transactionAborted());
    assert(mca_bridge_take_response(&mca_bridge, result, sizeof(result), &length));
    assert(length == 1 && result[0] == 0x42);
    mmceman_callback = NULL;

    assert(mca_bridge_submit(&mca_bridge, request, sizeof(request)));
    assert(ps1_card_backend_mca_wait());
    assert(ps1_card_backend_mca.recv_mc(&byte) == PS1_RECEIVE_OK && byte == 0x81);
    ps1_card_backend_mca.respond(0x08);
    ps1_card_backend_mca_finish();
    assert(mca_bridge_take_response(&mca_bridge, result, sizeof(result), &length));
    assert(length == 2 && result[0] == 0xff && result[1] == 0x08);
    ps1_mc_exit_request = 1;
    assert(!ps1_card_backend_mca_wait());
    ps2_mc_exit_request = 1;
    assert(ps2_card_backend_mca.receiveFirst(&byte) == PS2_RECEIVE_EXIT);
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
