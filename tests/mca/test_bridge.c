#include "../../src/usb/mca_bridge.h"
#include <assert.h>
#include <string.h>

int main(void) {
    mca_bridge_t bridge;
    uint8_t request[] = {0xaa, 0x52, 0x03};
    uint8_t response[8], byte;
    size_t response_length = 0;
    mca_bridge_init(&bridge);
    assert(((uintptr_t)bridge.input & 3u) == 0);
    assert(((uintptr_t)bridge.output & 3u) == 0);

    assert(mca_bridge_submit(&bridge, request, sizeof(request)));
    assert(!mca_bridge_submit(&bridge, request, sizeof(request)));
    assert(mca_bridge_core1_begin(&bridge));
    for (size_t i = 0; i < sizeof(request); ++i) {
        assert(mca_bridge_core1_receive(&bridge, &byte));
        assert(byte == request[i]);
    }
    assert(!mca_bridge_core1_receive(&bridge, &byte));
    assert(mca_bridge_core1_respond(&bridge, 0x5a));
    assert(mca_bridge_core1_respond(&bridge, 0xa5));
    assert(mca_bridge_core1_finish(&bridge));
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_RESPONSE);
    assert(mca_bridge_take_response(&bridge, response, sizeof(response), &response_length));
    assert(response_length == 2 && response[0] == 0x5a && response[1] == 0xa5);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);

    /* Publish a lower-half command without overwriting scheduler data above it. */
    uint8_t *request_area = mca_bridge_core0_request_buffer(&bridge);
    assert(request_area);
    request_area[700] = 0x6c;
    memcpy(request_area + 4, request, sizeof(request));
    assert(mca_bridge_core0_publish_request(&bridge, 4, sizeof(request)));
    assert(mca_bridge_core1_begin(&bridge));
    for (size_t i = 0; i < sizeof(request); ++i) {
        assert(mca_bridge_core1_receive(&bridge, &byte));
        assert(byte == request[i]);
    }
    assert(mca_bridge_core1_respond(&bridge, 0x7e));
    assert(mca_bridge_core1_finish(&bridge));
    uint8_t *tx_area = mca_bridge_core0_response_buffer(&bridge, &response_length);
    assert(tx_area && response_length == 1 && tx_area[0] == 0x7e);
    tx_area[-4] = 0x2b; /* core0 may prepend framing in reserved headroom */
    assert(mca_bridge_core0_release_response(&bridge));
    assert(request_area[700] == 0x6c);

    assert(mca_bridge_submit(&bridge, request, sizeof(request)));
    assert(mca_bridge_cancel(&bridge)); /* core 1 acknowledges pending cancel */
    assert(mca_bridge_core1_begin(&bridge));
    assert(!mca_bridge_core1_finish(&bridge));
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    assert(mca_bridge_submit(&bridge, request, sizeof(request)));
    assert(mca_bridge_core1_begin(&bridge));
    assert(mca_bridge_cancel(&bridge));
    assert(mca_bridge_core1_cancelled(&bridge));
    assert(!mca_bridge_core1_receive(&bridge, &byte));
    assert(!mca_bridge_core1_finish(&bridge));
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);

    uint8_t too_large[MCA_BRIDGE_BUFFER_MAX + 1];
    memset(too_large, 0, sizeof(too_large));
    assert(!mca_bridge_submit(&bridge, too_large, sizeof(too_large)));
    assert(!mca_bridge_submit(&bridge, request, 0));
    critical_section_deinit(&bridge.lock);
    return 0;
}
