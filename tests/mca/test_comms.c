#include "mca_comms.h"
#include <assert.h>
#include <string.h>

int main(void) {
    uint8_t buffer[MCA_FRAME_MAX], input[MCA_FRAME_MAX];
    mca_frame_t frame;
    mca_frame_init(&frame, buffer, sizeof(buffer));

    const uint8_t detect[] = {0xaa, 0x40, 0xaa, 0x40};
    assert(mca_frame_feed(&frame, detect, sizeof(detect)) == 2);
    assert(frame.result == MCA_FRAME_READY && frame.length == 2);
    mca_frame_reset(&frame);

    memset(input, 0, sizeof(input));
    input[0] = 0xaa; input[1] = 0x42;
    input[2] = MCA_RAW_MAX & 0xff; input[3] = MCA_RAW_MAX >> 8;
    for (size_t i = 0; i < sizeof(input); ++i) {
        assert(mca_frame_feed(&frame, input + i, 1) == 1);
        assert(frame.result == (i + 1 == sizeof(input) ? MCA_FRAME_READY : MCA_FRAME_MORE));
    }
    mca_frame_reset(&frame);
    input[2]++; /* exceeds the supported maximum */
    assert(mca_frame_feed(&frame, input, 4) == 4);
    assert(frame.result == MCA_FRAME_INVALID);
    mca_frame_reset(&frame);

    const uint8_t page[] = {0xaa, 0x52, 0x03, 1, 2, 3, 4, 0x55, 0x2b};
    assert(mca_frame_feed(&frame, page, sizeof(page) - 1) == sizeof(page) - 1);
    assert(frame.result == MCA_FRAME_MORE);
    assert(mca_frame_feed(&frame, page + sizeof(page) - 1, 1) == 1);
    assert(frame.result == MCA_FRAME_READY);
    mca_frame_reset(&frame);
    memcpy(input, page, sizeof(page)); input[8] = 0;
    assert(mca_frame_feed(&frame, input, sizeof(page)) == sizeof(page));
    assert(frame.result == MCA_FRAME_INVALID);
    mca_frame_reset(&frame);
    mca_frame_init(&frame, buffer, 8);
    assert(mca_frame_feed(&frame, page, sizeof(page)) == 2);
    assert(frame.result == MCA_FRAME_INVALID);

    mca_frame_init(&frame, buffer, sizeof(buffer));
    memset(input, 0, sizeof(input));
    input[0] = 0xaa; input[1] = 0x57; input[2] = 3;
    input[MCA_PS2_PAGE_RAW + 7] = 0x55;
    input[MCA_PS2_PAGE_RAW + 8] = 0x2b;
    for (size_t i = 0; i < MCA_PS2_PAGE_RAW + 9;) {
        size_t n = MCA_PS2_PAGE_RAW + 9 - i;
        if (n > 64) n = 64;
        assert(mca_frame_feed(&frame, input + i, n) == n);
        i += n;
        assert(frame.result == (i == MCA_PS2_PAGE_RAW + 9 ? MCA_FRAME_READY : MCA_FRAME_MORE));
    }
    mca_frame_reset(&frame);
    const uint8_t bad[] = {0xaa, 0x42, 0, 0};
    assert(mca_frame_feed(&frame, bad, sizeof(bad)) == sizeof(bad));
    assert(frame.result == MCA_FRAME_INVALID);
    return 0;
}
