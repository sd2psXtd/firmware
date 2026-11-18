#include "protocol_fixture.h"
#include "mca_page.h"

static void finish_raw(uint8_t reply) {
    mca_bridge_respond(&mca_bridge, reply);
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING);
    assert(mca_protocol_idle()); /* Worker drained; USB still owns the reply. */
}
static void complete_reply(void) {
    mca_protocol_callbacks()->in_complete(true, sent_length);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
}

int main(void) {
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    const uint8_t request[] = {0xaa, 0x42, 3, 0, 0x81, 0x11, 0};
    const uint8_t detect[] = {0xaa, 0x40};
    uint8_t byte;
    mca_protocol_init();
    mca_protocol_set_ready(true);
    callbacks->out_received(request, 3);
    callbacks->out_received(request + 3, sizeof(request) - 3);
    mca_protocol_task();
    assert(mca_bridge_begin(&mca_bridge));
    for (unsigned i = 4; i < sizeof(request); ++i)
        assert(mca_bridge_receive(&mca_bridge, &byte) && byte == request[i]);
    mca_bridge_respond(&mca_bridge, 0xff);
    finish_raw(0x2b);
    const uint8_t expected[] = {0x55, 0x5a, 2, 0, 0xff, 0x2b};
    assert(sent_length == sizeof(expected) && memcmp(sent, expected, sizeof(expected)) == 0);
    complete_on_usb_task = true;
    mca_protocol_drain_reply(500);
    assert(!complete_on_usb_task && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    /* Two-byte raw length, fragmented over all USB packets. */
    uint8_t large_request[1028] = {0xaa, 0x42, 0, 4};
    for (unsigned i = 0; i < 1024; ++i) large_request[4 + i] = (uint8_t)(i * 17);
    for (size_t offset = 0; offset < sizeof(large_request);) {
        size_t bytes = sizeof(large_request) - offset;
        if (bytes > 64) bytes = 64;
        callbacks->out_received(large_request + offset, (uint16_t)bytes);
        offset += bytes;
        mca_protocol_task();
    }
    assert(mca_bridge_begin(&mca_bridge));
    for (unsigned i = 0; i < 1024; ++i) {
        assert(mca_bridge_receive(&mca_bridge, &byte) && byte == large_request[4 + i]);
        mca_bridge_respond(&mca_bridge, byte ^ 0x5a);
    }
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    assert(sent_length == 1028 && sent[0] == 0x55 && sent[1] == 0x5a);
    assert(sent[2] == 0 && sent[3] == 4);
    for (unsigned i = 0; i < 1024; ++i)
        assert(sent[4 + i] == (uint8_t)(large_request[4 + i] ^ 0x5a));
    complete_reply();

    /* Readiness loss/storage entry rejects NEW operations, not accepted work. */
    for (unsigned mass = 0; mass < 2; ++mass) {
        callbacks->out_received(request, sizeof(request));
        mca_protocol_task();
        assert(mca_bridge_begin(&mca_bridge));
        if (mass) usb_mode = USB_MODE_MASS;
        else mca_protocol_set_ready(false);
        finish_raw(0xff);
        assert(sent_length == 5 && sent[4] == 0xff);
        complete_reply();
        callbacks->out_received(detect, sizeof(detect));
        mca_protocol_task();
        assert(sent_length == 2 && sent[1] == 0);
        complete_reply();
        callbacks->out_received(request, sizeof(request));
        mca_protocol_task();
        assert(sent_length == 2 && sent[1] == 0xaf);
        complete_reply();
        mca_protocol_set_ready(true);
        usb_mode = USB_MODE_MCA;
    }

    /* Reset preserves ACTIVE ownership but discards the old connection's reply. */
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(mca_bridge_begin(&mca_bridge));
    callbacks->bus_reset();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_ACTIVE);
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    callbacks->out_received(request, 3);
    now += 500000;
    mca_protocol_task();
    assert(sent_length == 2 && sent[1] == 0xaf);
    complete_reply();

    /* An unclaimed request times out directly; ACTIVE work cancels cooperatively. */
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    now += MCA_BRIDGE_TIMEOUT_US + 1;
    mca_protocol_task();
    assert(!mca_bridge_begin(&mca_bridge) && sent_length == 2 && sent[1] == 0xaf);
    complete_reply();

    const uint8_t page_request[] = {0xaa, 0x52, 3, 42, 0, 0, 0, 0x55, 0x2b};
    callbacks->out_received(page_request, sizeof(page_request));
    mca_protocol_task();
    assert(card_present && mca_bridge_begin(&mca_bridge));
    now += MCA_BRIDGE_TIMEOUT_US + 1;
    mca_protocol_task();
    assert(mca_bridge_cancelled(&mca_bridge));
    mca_protocol_task();
    assert(!card_present && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_ACTIVE);
    assert(!mca_bridge_receive(&mca_bridge, &byte));
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    assert(sent_length == 2 && sent[1] == 0xaf);
    complete_reply();
    mca_protocol_task();
    assert(card_present);

    /* A non-polling host must not make session teardown wait indefinitely. */
    callbacks->out_received(request, sizeof(request));
    mca_protocol_task();
    assert(mca_bridge_begin(&mca_bridge));
    finish_raw(0xff);
    uint64_t start = now;
    mca_protocol_drain_reply(500);
    assert(now - start == 500000 && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_SENDING);
    callbacks->bus_reset();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);

    /* High-level page operations need authentication, retail PS2, and bounds. */
    for (unsigned test = 0; test < 5; ++test) {
        auth_valid = test != 0;
        card_accessible = test != 1;
        card_mode = test == 2 ? MODE_PS1 : MODE_PS2;
        card_variant = test == 3 ? PS2_VARIANT_COH : PS2_VARIANT_RETAIL;
        card_capacity = test == 4 ? 42u * 512 : 8u * 1024 * 1024;
        callbacks->out_received(page_request, sizeof(page_request));
        mca_protocol_task();
        assert(sent_length == 2 && sent[1] == 0xaf);
        complete_reply();
    }
    auth_valid = card_accessible = true;
    card_mode = MODE_PS2;
    card_variant = PS2_VARIANT_RETAIL;
    card_capacity = 8u * 1024 * 1024;
    uint8_t page_write[MCA_PAGE_RAW_SIZE + 9] = {0xaa, 0x57, 3, 42};
    page_write[sizeof(page_write) - 2] = 0x55;
    page_write[sizeof(page_write) - 1] = 0x2b;
    auth_valid = false;
    callbacks->out_received(page_write, sizeof(page_write));
    mca_protocol_task();
    assert(sent_length == 2 && sent[1] == 0xaf);
    complete_reply();
    auth_valid = true;

    const int modes[] = {MODE_PS2, MODE_PS1, MODE_PS2};
    const uint8_t types[] = {2, 1, 2};
    for (unsigned i = 0; i < 3; ++i) {
        card_mode = modes[i];
        callbacks->out_received(detect, sizeof(detect));
        mca_protocol_task();
        assert(sent_length == 2 && sent[1] == types[i]);
        complete_reply();
    }

    const uint8_t debug_exit[] = {'M', 'C', 'A', '!'};
#ifdef DEBUG_USB_UART
    const uint8_t raw_header[] = {0xaa, 0x42, 4, 0};
    callbacks->out_received(raw_header, sizeof(raw_header));
    callbacks->out_received(debug_exit, sizeof(debug_exit));
    assert(mca_enabled && mca_setting_updates == 0);
    callbacks->bus_reset();
    callbacks->out_received(debug_exit, sizeof(debug_exit));
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
    assert(mca_enabled && mca_setting_updates == 0 && sent_length == 2 && sent[1] == 0xaf);
    complete_reply();
#endif
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
