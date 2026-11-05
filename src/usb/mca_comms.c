#include "mca_comms.h"

void mca_frame_reset(mca_frame_t *frame) {
    frame->length = 0;
    frame->expected = 0;
    frame->result = MCA_FRAME_MORE;
}

void mca_frame_init(mca_frame_t *frame, uint8_t *buffer, size_t capacity) {
    frame->data = buffer;
    frame->capacity = capacity;
    mca_frame_reset(frame);
}

size_t mca_frame_feed(mca_frame_t *frame, const uint8_t *data, size_t length) {
    size_t consumed = 0;

    while (consumed < length && frame->result == MCA_FRAME_MORE) {
        if (frame->length == frame->capacity) {
            frame->result = MCA_FRAME_INVALID;
            break;
        }
        frame->data[frame->length++] = data[consumed++];
        if (frame->length == 1 && frame->data[0] != 0xaa) {
            frame->result = MCA_FRAME_INVALID;
        } else if (frame->length == 2) {
            switch (frame->data[1]) {
                case 0x40: frame->expected = 2; break;
                case 0x42: break;
                case 0x52: frame->expected = 9; break;
                case 0x57: frame->expected = MCA_PS2_PAGE_RAW + 9; break;
                default: frame->result = MCA_FRAME_INVALID; break;
            }
        } else if (frame->length == 4 && frame->data[1] == 0x42) {
            size_t payload = frame->data[2] | ((size_t)frame->data[3] << 8);
            if (!payload || payload > MCA_RAW_MAX)
                frame->result = MCA_FRAME_INVALID;
            else
                frame->expected = payload + 4;
        }

        if (frame->expected > frame->capacity)
            frame->result = MCA_FRAME_INVALID;
        else if (frame->expected && frame->length == frame->expected) {
            if ((frame->data[1] == 0x52 || frame->data[1] == 0x57) &&
                (frame->data[2] != 0x03 || frame->data[frame->length - 2] != 0x55 ||
                 frame->data[frame->length - 1] != 0x2b))
                frame->result = MCA_FRAME_INVALID;
            else
                frame->result = MCA_FRAME_READY;
        }
    }
    return consumed;
}
