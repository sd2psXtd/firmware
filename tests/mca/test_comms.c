/* Framing is now private to mca_protocol.c. Test it through OUT callbacks. */
#include "protocol_fixture.h"
#include "ps2_mca_page.h"

static void expect_invalid(const uint8_t *input, size_t length) {
    sent_length = 0;
    mca_protocol_callbacks()->out_received(input, (uint16_t)length);
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 0xaf);
    mca_protocol_callbacks()->in_complete(true, sent_length);
}

int main(void) {
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    mca_protocol_init();
    mca_protocol_set_ready(true);
    const uint8_t detect[] = {0xaa, 0x40, 0xaa, 0x40};
    callbacks->out_received(detect, sizeof(detect));
    mca_protocol_task();
    assert(sent_length == 2 && sent[0] == 0x55 && sent[1] == 2);
    callbacks->in_complete(true, sent_length);

    uint8_t input[MCA_BRIDGE_RX_CAPACITY] = {0xaa, 0x42};
    input[2] = (uint8_t)MCA_BRIDGE_BUFFER_MAX;
    input[3] = (uint8_t)(MCA_BRIDGE_BUFFER_MAX >> 8);
    for (size_t i = 0; i < sizeof(input); ++i) {
        callbacks->out_received(input + i, 1);
        mca_protocol_task();
        assert(mca_bridge_state(&mca_bridge) ==
               (i + 1 == sizeof(input) ? MCA_BRIDGE_REQUEST : MCA_BRIDGE_IDLE));
    }
    assert(mca_bridge_begin(&mca_bridge));
    uint8_t byte;
    for (unsigned i = 0; i < MCA_BRIDGE_BUFFER_MAX; ++i)
        assert(mca_bridge_receive(&mca_bridge, &byte) && byte == 0);
    assert(!mca_bridge_receive(&mca_bridge, &byte));
    mca_bridge_finish(&mca_bridge, true);
    mca_protocol_task();
    callbacks->in_complete(true, sent_length);

    ++input[2]; /* exceeds the supported maximum */
    expect_invalid(input, 4);
    const uint8_t empty[] = {0xaa, 0x42, 0, 0};
    expect_invalid(empty, sizeof(empty));
    const uint8_t bad_header[] = {0xab, 0x40};
    expect_invalid(bad_header, sizeof(bad_header));
    const uint8_t bad_command[] = {0xaa, 0x41};
    expect_invalid(bad_command, sizeof(bad_command));

    uint8_t page[] = {0xaa, 0x52, 3, 1, 0, 0, 0, 0x55, 0x2b};
    callbacks->out_received(page, sizeof(page) - 1);
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    callbacks->out_received(page + sizeof(page) - 1, 1);
    mca_protocol_task();
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_REQUEST);
    assert(mca_bridge_begin(&mca_bridge));
    mca_bridge_finish(&mca_bridge, false);
    mca_protocol_task();
    callbacks->in_complete(true, sent_length);
    page[8] = 0;
    expect_invalid(page, sizeof(page));
    page[8] = 0x2b;
    page[2] = 2;
    expect_invalid(page, sizeof(page));

    uint8_t write[MCA_PAGE_RAW_SIZE + 9] = {0xaa, 0x57, 3};
    write[sizeof(write) - 2] = 0x55;
    write[sizeof(write) - 1] = 0x2b;
    for (size_t i = 0; i < sizeof(write);) {
        size_t n = sizeof(write) - i;
        if (n > 64) n = 64;
        callbacks->out_received(write + i, (uint16_t)n);
        i += n;
        mca_protocol_task();
        assert(mca_bridge_state(&mca_bridge) ==
               (i == sizeof(write) ? MCA_BRIDGE_REQUEST : MCA_BRIDGE_IDLE));
    }
    assert(mca_bridge_begin(&mca_bridge));
    mca_bridge_finish(&mca_bridge, false);
    mca_protocol_task();
    callbacks->in_complete(true, sent_length);
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
