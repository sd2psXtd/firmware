#include "mca_page.h"

#include <string.h>

enum {
    SIO_IDENTIFIER = 0x81,
    SIO_SET_WRITE_ADDRESS = 0x22,
    SIO_SET_READ_ADDRESS = 0x23,
    SIO_WRITE_DATA = 0x42,
    SIO_READ_DATA = 0x43,
    SIO_COMMIT_DATA = 0x81,
    SIO_ACK = 0x2b,
    SIO_TERM_FALLBACK = 0xff,
    PAGE_CHUNK_MAX = 128,
    PAGE_REQUEST_OFFSET = 4,
};

typedef enum {
    STEP_IDLE,
    STEP_ADDRESS,
    STEP_DATA,
    STEP_COMMIT,
    STEP_CANCELLING,
} page_step_t;

static struct {
    mca_bridge_t *bridge;
    page_step_t step;
    bool write;
    uint8_t term;
    uint16_t offset;
    uint16_t chunk;
    uint64_t deadline;
} page;

static uint8_t edc(const uint8_t *bytes, uint16_t length) {
    uint8_t checksum = 0;

    for (uint16_t i = 0; i < length; ++i)
        checksum ^= bytes[i];
    return checksum;
}

static bool submit_request(uint16_t length, uint64_t now_us) {
    uint8_t *input = mca_bridge_core0_request_buffer(page.bridge);

    if (!input || !mca_bridge_core0_publish_request(page.bridge,
                                                     PAGE_REQUEST_OFFSET,
                                                     length))
        return false;
    page.deadline = now_us + MCA_PAGE_REPLY_TIMEOUT_US;
    return true;
}

static bool submit_address(uint32_t page_number, uint64_t now_us) {
    uint8_t *input = mca_bridge_core0_request_buffer(page.bridge);
    uint8_t *request;

    if (!input)
        return false;
    request = input + PAGE_REQUEST_OFFSET;
    memset(request, 0, 9);
    request[0] = SIO_IDENTIFIER;
    request[1] = page.write ? SIO_SET_WRITE_ADDRESS : SIO_SET_READ_ADDRESS;
    for (uint8_t i = 0; i < 4; ++i)
        request[2 + i] = (uint8_t)(page_number >> (8u * i));
    request[6] = edc(request + 2, 4);
    page.step = STEP_ADDRESS;
    return submit_request(9, now_us);
}

static bool submit_data_or_commit(uint64_t now_us) {
    uint8_t *input = mca_bridge_core0_request_buffer(page.bridge);
    uint8_t *request;

    if (!input)
        return false;
    request = input + PAGE_REQUEST_OFFSET;
    memset(request, 0, PAGE_CHUNK_MAX + 6);
    request[0] = SIO_IDENTIFIER;

    if (page.offset == MCA_PAGE_RAW_SIZE) {
        request[1] = SIO_COMMIT_DATA;
        page.step = STEP_COMMIT;
        return submit_request(4, now_us);
    }

    uint16_t remaining = MCA_PAGE_RAW_SIZE - page.offset;
    page.chunk = remaining > PAGE_CHUNK_MAX ? PAGE_CHUNK_MAX : remaining;
    request[1] = page.write ? SIO_WRITE_DATA : SIO_READ_DATA;
    request[2] = (uint8_t)page.chunk;
    if (page.write) {
        const uint8_t *staged = page.bridge->input + MCA_PAGE_STAGE_OFFSET + page.offset;
        memcpy(request + 3, staged, page.chunk);
        request[3 + page.chunk] = edc(request + 3, page.chunk);
    }
    page.step = STEP_DATA;
    return submit_request(page.chunk + 6, now_us);
}

static bool validate_response(const uint8_t *response, size_t length) {
    size_t expected;
    size_t ack_index;

    switch (page.step) {
        case STEP_ADDRESS:
            expected = 9;
            break;
        case STEP_DATA:
            expected = (size_t)page.chunk + 6;
            break;
        case STEP_COMMIT:
            expected = 4;
            break;
        default:
            return false;
    }
    if (!response || length != expected || response[0] != 0xff || response[1] != 0xff)
        return false;

    ack_index = page.step == STEP_DATA && !page.write ? 3 : length - 2;
    if (response[ack_index] != SIO_ACK)
        return false;
    if (page.step == STEP_ADDRESS) {
        page.term = response[length - 1];
    } else if (response[length - 1] != page.term) {
        return false;
    }
    if (page.step == STEP_DATA && !page.write &&
        edc(response + 4, page.chunk) != response[4 + page.chunk])
        return false;
    return true;
}

static void prepare_final_response(void) {
    uint8_t *output = page.bridge->output;

    if (page.write) {
        output[4] = 0x55;
        output[5] = 0x5a;
        page.bridge->output_length = 2;
    } else {
        memcpy(output + 4, page.bridge->input + MCA_PAGE_STAGE_OFFSET,
               MCA_PAGE_RAW_SIZE);
        output[0] = 0x55;
        output[1] = 0x5a;
        output[2] = (uint8_t)MCA_PAGE_RAW_SIZE;
        output[3] = (uint8_t)(MCA_PAGE_RAW_SIZE >> 8);
        page.bridge->output_length = MCA_PAGE_RAW_SIZE;
    }
}

static mca_page_result_t finish(mca_page_result_t result) {
    page.step = STEP_IDLE;
    return result;
}

bool mca_page_start(mca_bridge_t *bridge, bool write, uint32_t page_number,
                    const uint8_t *write_data, uint64_t now_us) {
    if (!bridge || page.step != STEP_IDLE || mca_bridge_state(bridge) != MCA_BRIDGE_IDLE ||
        (write && !write_data) || (!write && write_data))
        return false;

    page.bridge = bridge;
    page.write = write;
    page.offset = 0;
    page.chunk = 0;
    page.term = SIO_TERM_FALLBACK;
    if (write) {
        uint8_t *input = mca_bridge_core0_request_buffer(bridge);
        if (!input)
            return false;
        memmove(input + MCA_PAGE_STAGE_OFFSET, write_data, MCA_PAGE_RAW_SIZE);
    }
    if (!submit_address(page_number, now_us)) {
        page.step = STEP_IDLE;
        page.bridge = NULL;
        return false;
    }
    return true;
}

static mca_page_result_t poll_cancellation(void) {
    mca_bridge_state_t bridge_state = mca_bridge_state(page.bridge);

    if (bridge_state == MCA_BRIDGE_RESPONSE)
        (void)mca_bridge_core0_release_response(page.bridge);
    else if (bridge_state != MCA_BRIDGE_IDLE)
        return MCA_PAGE_PENDING;

    return finish(MCA_PAGE_FAILED);
}

mca_page_result_t mca_page_task(mca_bridge_t *bridge, uint64_t now_us) {
    mca_bridge_state_t bridge_state;
    uint8_t *response;
    size_t response_length = 0;

    if (page.step == STEP_IDLE)
        return MCA_PAGE_IDLE;
    if (bridge != page.bridge)
        return MCA_PAGE_FAILED;
    if (page.step == STEP_CANCELLING)
        return poll_cancellation();

    bridge_state = mca_bridge_state(bridge);
    if (bridge_state == MCA_BRIDGE_RESPONSE) {
        response = mca_bridge_core0_response_buffer(bridge, &response_length);
        if (!validate_response(response, response_length)) {
            (void)mca_bridge_core0_release_response(bridge);
            return finish(MCA_PAGE_FAILED);
        }

        if (page.step == STEP_COMMIT) {
            prepare_final_response();
            return finish(page.write ? MCA_PAGE_WRITE_DONE : MCA_PAGE_READ_DONE);
        }

        if (page.step == STEP_DATA) {
            if (!page.write)
                memcpy(bridge->input + MCA_PAGE_STAGE_OFFSET + page.offset,
                       response + 4, page.chunk);
            page.offset += page.chunk;
        }
        if (!mca_bridge_core0_release_response(bridge))
            return finish(MCA_PAGE_FAILED);

        bool submitted = submit_data_or_commit(now_us);
        return submitted ? MCA_PAGE_PENDING : finish(MCA_PAGE_FAILED);
    }

    if (bridge_state == MCA_BRIDGE_IDLE)
        return finish(MCA_PAGE_FAILED);
    if (now_us >= page.deadline) {
        page.step = STEP_CANCELLING;
        (void)mca_bridge_cancel(bridge);
        return poll_cancellation();
    }
    return MCA_PAGE_PENDING;
}

bool mca_page_is_cancelling(void) {
    return page.step == STEP_CANCELLING;
}
