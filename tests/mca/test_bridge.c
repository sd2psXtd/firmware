#include "mca_bridge.h"
#include <assert.h>
#include <string.h>

uint64_t time_us_64(void) { return 100; }

static void submit(mca_bridge_t *bridge) {
    const uint8_t request[] = {0xaa, 0x42, 2, 0, 0x81, 0x11};
    memcpy(bridge->input, request, sizeof(request));
    assert(mca_bridge_submit(bridge, sizeof(request)));
    assert(!mca_bridge_submit(bridge, sizeof(request)));
}

int main(void) {
    mca_bridge_t bridge;
    uint8_t byte;
    mca_bridge_init(&bridge);
    assert(((uintptr_t)bridge.input & 3u) == 0);
    assert(((uintptr_t)bridge.output & 3u) == 0);
    submit(&bridge);
    assert(mca_bridge_begin(&bridge));
    assert(mca_bridge_receive(&bridge, &byte) && byte == 0x81);
    assert(mca_bridge_receive(&bridge, &byte) && byte == 0x11);
    assert(!mca_bridge_receive(&bridge, &byte));
    mca_bridge_respond(&bridge, 0x5a);
    mca_bridge_respond(&bridge, 0xa5);
    mca_bridge_finish(&bridge, true);
    const uint8_t expected[] = {0x55, 0x5a, 2, 0, 0x5a, 0xa5};
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_RESPONSE);
    assert(bridge.output_length == sizeof(expected));
    assert(memcmp(bridge.output, expected, sizeof(expected)) == 0);
    assert(!mca_bridge_begin(&bridge));
    mca_bridge_sending(&bridge);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_SENDING);
    assert(!mca_bridge_begin(&bridge));
    assert(!mca_bridge_submit(&bridge, bridge.input_length));
    mca_bridge_release(&bridge);

    /* An unclaimed operation fails immediately on timeout. */
    submit(&bridge);
    mca_bridge_poll_timeout(&bridge, bridge.deadline);
    assert(!mca_bridge_begin(&bridge));
    assert(bridge.output_length == 2 && bridge.output[1] == 0xaf);
    mca_bridge_release(&bridge);

    /* ACTIVE buffers cannot be released or reused until Core 1 finishes. */
    submit(&bridge);
    assert(mca_bridge_begin(&bridge));
    mca_bridge_release(&bridge);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_ACTIVE);
    mca_bridge_poll_timeout(&bridge, bridge.deadline);
    assert(mca_bridge_cancelled(&bridge));
    assert(!mca_bridge_receive(&bridge, &byte));
    mca_bridge_finish(&bridge, true);
    assert(bridge.output_length == 2 && bridge.output[1] == 0xaf);
    mca_bridge_release(&bridge);

    /* Overflow cancels cooperatively and produces a bounded error reply. */
    submit(&bridge);
    assert(mca_bridge_begin(&bridge));
    for (unsigned i = 0; i <= MCA_BRIDGE_TX_CAPACITY; ++i)
        mca_bridge_respond(&bridge, 0x7e);
    assert(mca_bridge_cancelled(&bridge));
    mca_bridge_finish(&bridge, true);
    assert(bridge.output_length == 2 && bridge.output[1] == 0xaf);
    mca_bridge_release(&bridge);
    assert(!mca_bridge_submit(&bridge, MCA_BRIDGE_RX_CAPACITY + 1));
    assert(!mca_bridge_submit(&bridge, 0));
    assert(!mca_bridge_submit(&bridge, 4));
    critical_section_deinit(&bridge.lock);
    return 0;
}
