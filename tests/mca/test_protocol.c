#include "mca_bridge.h"
#include "mca_protocol.h"
#include "mca_page.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

extern mca_bridge_t mca_bridge;
static uint64_t now;
static const uint8_t *sent;
static size_t sent_length;
static bool complete_on_usb_task, card_present;
static bool mca_enabled = true;
static unsigned mca_setting_updates;
static bool auth_valid = true, card_accessible = true;
static int card_mode = 1, card_variant;
static uint32_t card_capacity = 8u * 1024 * 1024;
uint64_t time_us_64(void) { return now; }
void sleep_ms(uint32_t milliseconds) { now += milliseconds * 1000; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) {
    (void)timeout_ms; (void)in_isr;
    if (complete_on_usb_task) {
        complete_on_usb_task = false;
        mca_protocol_callbacks()->in_complete(true, sent_length);
    }
}
int settings_get_mode(bool temporary) { (void)temporary; return card_mode; }
bool settings_get_mca_enabled(void) { return mca_enabled; }
void settings_set_mca_enabled(bool enabled) {
    mca_setting_updates++;
    mca_enabled = enabled;
}
int settings_get_ps2_variant(void) { return card_variant; }
bool ps2_cardman_is_accessible(void) { return card_accessible; }
bool ps2_cardman_is_idle(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return card_capacity; }
bool ps2_mc_auth_isValid(void) { return auth_valid; }
void mca_device_set_presence(bool present) { card_present = present; }
void mca_device_task(void) {}
bool mca_device_profile_is_mca(void) { return true; }
bool mca_device_send(const uint8_t *data, size_t length) {
    assert(((uintptr_t)data & 3) == 0);
    sent = data;
    sent_length = length;
    return true;
}

int main(void) {
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    const uint8_t request[] = {0xaa, 0x42, 3, 0, 0x81, 0x11, 0};
    uint8_t byte;
    mca_protocol_init();
    mca_protocol_set_ready(true);
    callbacks->out_received(request, 3);
    callbacks->out_received(request + 3, sizeof(request) - 3);
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    for (unsigned i = 4; i < sizeof(request); ++i) {
        assert(mca_bridge_core1_receive(&mca_bridge, &byte));
        assert(byte == request[i]);
    }
    assert(mca_bridge_core1_respond(&mca_bridge, 0xff));
    assert(mca_bridge_core1_respond(&mca_bridge, 0x2b));
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    const uint8_t expected[] = {0x55, 0x5a, 2, 0, 0xff, 0x2b};
    assert(sent_length == sizeof(expected) && memcmp(sent, expected, sizeof(expected)) == 0);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_RESPONSE);
    complete_on_usb_task = true;
    mca_protocol_drain_reply(500);
    assert(!complete_on_usb_task);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    /* Sony's generic raw API permits 1024 bytes. Exercise its two-byte
     * length and all USB packets through the parser and bridge. */
    uint8_t large_request[1028] = {0xaa, 0x42, 0, 4};
    for (unsigned i = 0; i < 1024; ++i)
        large_request[4 + i] = (uint8_t)(i * 17);
    for (size_t offset = 0; offset < sizeof(large_request);) {
        size_t bytes = sizeof(large_request) - offset;
        if (bytes > 64) bytes = 64;
        callbacks->out_received(large_request + offset, bytes);
        offset += bytes;
        mca_protocol_task();
    }
    assert(mca_bridge_core1_begin(&mca_bridge));
    for (unsigned i = 0; i < 1024; ++i) {
        assert(mca_bridge_core1_receive(&mca_bridge, &byte));
        assert(byte == large_request[4 + i]);
        assert(mca_bridge_core1_respond(&mca_bridge, byte ^ 0x5a));
    }
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(sent_length == 1028 && sent[0] == 0x55 && sent[1] == 0x5a);
    assert(sent[2] == 0 && sent[3] == 4);
    for (unsigned i = 0; i < 1024; ++i)
        assert(sent[4 + i] == (uint8_t)(large_request[4 + i] ^ 0x5a));
    callbacks->in_complete(true, sent_length);

    /* A requested profile switch stops admitting new card operations while
     * preserving the reply to the transaction already owned by Core 1. */
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    mca_protocol_set_ready(false);
    assert(mca_bridge_core1_respond(&mca_bridge, 0xff));
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(sent_length == 5 && sent[1] == 0x5a && sent[4] == 0xff);
    callbacks->in_complete(true, sent_length);
    const uint8_t detect[] = {0xaa, 0x40};
    callbacks->out_received(detect, sizeof(detect));
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0);
    callbacks->in_complete(true, sent_length);
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(sent_length == 2 && sent[1] == 0xaf);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    callbacks->in_complete(true, sent_length);
    mca_protocol_set_ready(true);

    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    callbacks->bus_reset();
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    callbacks->out_received(request, 3);
    now += 500000;
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    callbacks->in_complete(true, sent_length);

    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    now += 1000001;
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_REQUEST);
    assert(mca_bridge_core1_begin(&mca_bridge));
    assert(!mca_bridge_core1_receive(&mca_bridge, &byte));
    assert(!mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    callbacks->in_complete(true, sent_length);

    const uint8_t page_request[] = {0xaa, 0x52, 3, 42, 0, 0, 0, 0x55, 0x2b};
    callbacks->out_received(page_request, sizeof(page_request));
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    assert(mca_bridge_core1_receive(&mca_bridge, &byte) && byte == 0x81);
    assert(mca_bridge_core1_receive(&mca_bridge, &byte) && byte == 0x23);
    assert(mca_bridge_core1_receive(&mca_bridge, &byte) && byte == 42);
    /* A malformed address reply must not become a successful USB reply. */
    for (unsigned i = 0; i < 9; ++i)
        assert(mca_bridge_core1_respond(&mca_bridge, 0));
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    callbacks->in_complete(true, sent_length);

    /* Mode/profile teardown must not wait forever for a host that stopped
     * polling, and must retain the reply buffer until USB is reset. */
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(mca_bridge_core1_begin(&mca_bridge));
    assert(mca_bridge_core1_respond(&mca_bridge, 0xff));
    assert(mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    uint64_t start = now;
    mca_protocol_drain_reply(500);
    assert(now - start == 500000);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_RESPONSE);
    callbacks->bus_reset();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    /* A page request must never reach the SIO backend without authorization,
     * a present retail PS2 card, and an address within the image. */
    for (unsigned test = 0; test < 5; ++test) {
        auth_valid = test != 0;
        card_accessible = test != 1;
        card_mode = test == 2 ? 0 : 1;
        card_variant = test == 3 ? 1 : 0;
        card_capacity = test == 4 ? 42u * 512 : 8u * 1024 * 1024;
        callbacks->out_received(page_request, sizeof(page_request));
        mca_protocol_task();
        assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
        assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
        callbacks->in_complete(true, sent_length);
    }
    auth_valid = card_accessible = true;
    card_mode = 1;
    card_variant = 0;
    card_capacity = 8u * 1024 * 1024;
    uint8_t page_write[537] = {0xaa, 0x57, 3, 42, 0, 0, 0};
    page_write[535] = 0x55;
    page_write[536] = 0x2b;
    auth_valid = false;
    for (size_t offset = 0; offset < sizeof(page_write);) {
        size_t length = sizeof(page_write) - offset;
        if (length > 64) length = 64;
        callbacks->out_received(page_write + offset, length);
        offset += length;
    }
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    callbacks->in_complete(true, sent_length);

    /* A stalled page operation keeps its buffers, reports removal while
     * cancellation is pending, and reports insertion after recovery. */
    auth_valid = true;
    callbacks->out_received(page_request, sizeof(page_request));
    mca_protocol_task();
    assert(card_present && mca_bridge_core1_begin(&mca_bridge));
    now += MCA_PAGE_REPLY_TIMEOUT_US + 1;
    mca_protocol_task();
    assert(mca_page_is_cancelling());
    mca_protocol_task();
    assert(!card_present && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_ACTIVE);
    assert(!mca_bridge_core1_receive(&mca_bridge, &byte));
    assert(!mca_bridge_core1_finish(&mca_bridge));
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    callbacks->in_complete(true, sent_length);
    mca_protocol_task();
    assert(card_present && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    /* The next idle detection reflects a PS2 -> PS1 -> PS2 mode change. */
    const int modes[] = {1, 0, 1};
    const uint8_t types[] = {2, 1, 2};
    for (unsigned i = 0; i < 3; ++i) {
        card_mode = modes[i];
        callbacks->out_received(detect, sizeof(detect));
        mca_protocol_task();
        assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == types[i]);
        callbacks->in_complete(true, sent_length);
    }

    const uint8_t debug_exit[] = {'M', 'C', 'A', '!'};
#ifdef DEBUG_USB_UART
    const uint8_t raw_header[] = {0xaa, 0x42, 4, 0};
    callbacks->out_received(raw_header, sizeof(raw_header));
    callbacks->out_received(debug_exit, sizeof(debug_exit));
    assert(mca_enabled && mca_setting_updates == 0);
    callbacks->bus_reset();
    callbacks->out_received(debug_exit, sizeof(debug_exit));
    assert(mca_enabled && mca_setting_updates == 0);
    callbacks->bus_reset();
    mca_protocol_task();
    assert(mca_enabled && mca_setting_updates == 0);
    callbacks->out_received(debug_exit, sizeof(debug_exit));
    assert(mca_enabled && mca_setting_updates == 0);
    mca_protocol_task();
    assert(!mca_enabled && mca_setting_updates == 1);
#else
    callbacks->out_received(debug_exit, sizeof(debug_exit));
    mca_protocol_task();
    assert(mca_enabled && mca_setting_updates == 0);
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    callbacks->in_complete(true, sent_length);
#endif
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
