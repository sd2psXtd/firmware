#include "mca_bridge.h"
#include "mca_protocol.h"
#include "ps1_card_backend_mca.h"
#include "ps2_card_backend_mca.h"
#include "settings.h"
#include <assert.h>
#include <string.h>

mca_bridge_t mca_bridge;
volatile bool ps1_card_active, ps2_card_active;
volatile int ps1_mc_reset, ps1_mc_exit_request, ps2_mc_exit_request;
volatile uint8_t mmceman_mcman_retry_counter;
volatile bool mmceman_tx_queued;
volatile uint8_t mmceman_tx_byte;
void (*mmceman_callback)(void);
uint64_t time_us_64(void) { return 0; }
int settings_get_mode(bool temporary) { (void)temporary; return MODE_PS2; }
bool settings_get_mca_enabled(void) { return true; }
int settings_get_ps2_variant(void) { return PS2_VARIANT_RETAIL; }
bool ps2_cardman_is_accessible(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return 8u * 1024 * 1024; }
bool ps2_mc_auth_isValid(void) { return true; }
bool mca_protocol_idle(void) { return true; }
void mca_protocol_attach_card(const mca_card_t *card) { (void)card; }
void mca_protocol_switch_delay(uint32_t ms) { (void)ms; }
bool mca_page_active(void) { return false; }
bool mca_page_receive(uint8_t *byte) { (void)byte; assert(false); return false; }
void mca_page_respond(uint8_t byte) { (void)byte; assert(false); }
bool mca_page_execute(mca_bridge_t *bridge) { (void)bridge; assert(false); return false; }
static void continuation(void) {}
static void submit(void) {
    const uint8_t request[] = {0xaa, 0x42, 4, 0, 0x81, 0x11, 0, 0};
    memcpy(mca_bridge.input, request, sizeof(request));
    assert(mca_bridge_submit(&mca_bridge, sizeof(request)));
}
static void expect_reply(const uint8_t *bytes, size_t length) {
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_RESPONSE);
    assert(mca_bridge.output_length == length + 4);
    assert(mca_bridge.output[0] == 0x55 && mca_bridge.output[1] == 0x5a);
    assert(mca_bridge.output[2] == length && mca_bridge.output[3] == 0);
    assert(memcmp(mca_bridge.output + 4, bytes, length) == 0);
    mca_bridge_release(&mca_bridge);
}
int main(void) {
    uint8_t byte;
    mca_bridge_init(&mca_bridge);
    submit();
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
    const uint8_t expected[] = {0xff, 0xff, 0x2b, 0x66};
    expect_reply(expected, sizeof(expected));

    /* Early handler returns finish at the next operation boundary. */
    submit();
    assert(ps2_card_backend_mca.receiveFirst(&byte) == PS2_RECEIVE_OK);
    assert(ps2_card_backend_mca.transactionAborted());
    expect_reply(expected, 1);

    mmceman_callback = continuation;
    mmceman_tx_queued = true;
    mmceman_tx_byte = 0x42;
    submit();
    assert(ps2_card_backend_mca.transactionAborted());
    assert(!mmceman_tx_queued);
    assert(ps2_card_backend_mca.receive(&byte) == PS2_RECEIVE_OK && byte == 0x81);
    ps2_card_backend_mca.finish();
    const uint8_t queued = 0x42;
    expect_reply(&queued, 1);
    mmceman_callback = NULL;

    submit();
    assert(ps1_card_backend_mca.receive_first(&byte) == PS1_RECEIVE_OK && byte == 0x81);
    ps1_card_backend_mca.respond(0x08);
    ps1_card_backend_mca.finish();
    const uint8_t ps1_reply[] = {0xff, 0x08};
    expect_reply(ps1_reply, sizeof(ps1_reply));
    ps1_mc_exit_request = 1;
    assert(ps1_card_backend_mca.receive_first(&byte) == PS1_RECEIVE_EXIT);
    ps2_mc_exit_request = 1;
    assert(ps2_card_backend_mca.receiveFirst(&byte) == PS2_RECEIVE_EXIT);
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
