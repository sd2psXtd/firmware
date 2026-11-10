#include "../../src/usb/mca_page.h"

#include <assert.h>
#include <string.h>

enum {
    TEST_SIO_IDENTIFIER = 0x81,
    TEST_SIO_SET_WRITE_ADDRESS = 0x22,
    TEST_SIO_SET_READ_ADDRESS = 0x23,
    TEST_SIO_WRITE_DATA = 0x42,
    TEST_SIO_READ_DATA = 0x43,
    TEST_SIO_COMMIT = 0x81,
    TEST_ACK = 0x2b,
    TEST_TERM = 0x7e,
};

static uint8_t test_edc(const uint8_t *bytes, size_t length) {
    uint8_t result = 0;
    for (size_t i = 0; i < length; ++i)
        result ^= bytes[i];
    return result;
}

static size_t consume_request(mca_bridge_t *bridge, uint8_t *request) {
    uint8_t byte;
    size_t length = bridge->input_length;

    assert(mca_bridge_state(bridge) == MCA_BRIDGE_REQUEST);
    assert(bridge->input_offset == 4);
    assert(mca_bridge_core1_begin(bridge));
    for (size_t i = 0; i < length; ++i) {
        assert(mca_bridge_core1_receive(bridge, &byte));
        request[i] = byte;
    }
    assert(!mca_bridge_core1_receive(bridge, &byte));
    return length;
}

static void publish_reply(mca_bridge_t *bridge, const uint8_t *reply, size_t length) {
    for (size_t i = 0; i < length; ++i)
        assert(mca_bridge_core1_respond(bridge, reply[i]));
    assert(mca_bridge_core1_finish(bridge));
}

static void emulate_page(mca_bridge_t *bridge, bool write, const uint8_t *write_data,
                         uint8_t *read_data, uint32_t page_number) {
    uint8_t request[140];
    uint8_t reply[140];
    uint16_t offset = 0;
    uint16_t transactions = 0;
    mca_page_result_t result;

    while (true) {
        size_t request_length = consume_request(bridge, request);
        memset(reply, 0, sizeof(reply));
        assert(request[0] == TEST_SIO_IDENTIFIER);
        if (transactions == 0) {
            assert(request_length == 9);
            assert(request[1] == (write ? TEST_SIO_SET_WRITE_ADDRESS : TEST_SIO_SET_READ_ADDRESS));
            assert(request[2] == (uint8_t)page_number);
            assert(request[3] == (uint8_t)(page_number >> 8));
            assert(request[4] == (uint8_t)(page_number >> 16));
            assert(request[5] == (uint8_t)(page_number >> 24));
            assert(request[6] == test_edc(request + 2, 4));
            reply[0] = reply[1] = 0xff;
            reply[7] = TEST_ACK;
            reply[8] = TEST_TERM;
            publish_reply(bridge, reply, 9);
        } else if (request[1] == (write ? TEST_SIO_WRITE_DATA : TEST_SIO_READ_DATA)) {
            uint16_t chunk = request[2];
            assert(chunk > 0 && chunk <= 128);
            assert(request_length == chunk + 6);
            assert(offset + chunk <= MCA_PAGE_RAW_SIZE);
            reply[0] = reply[1] = 0xff;
            if (write) {
                assert(memcmp(request + 3, write_data + offset, chunk) == 0);
                assert(request[3 + chunk] == test_edc(request + 3, chunk));
                reply[request_length - 2] = TEST_ACK;
            } else {
                reply[3] = TEST_ACK;
                for (uint16_t i = 0; i < chunk; ++i) {
                    read_data[offset + i] = (uint8_t)((offset + i) * 37u + 11u);
                    reply[4 + i] = read_data[offset + i];
                }
                reply[4 + chunk] = test_edc(reply + 4, chunk);
            }
            reply[request_length - 1] = TEST_TERM;
            offset += chunk;
            publish_reply(bridge, reply, request_length);
        } else {
            assert(request[1] == TEST_SIO_COMMIT);
            assert(request_length == 4);
            assert(offset == MCA_PAGE_RAW_SIZE);
            reply[0] = reply[1] = 0xff;
            reply[2] = TEST_ACK;
            reply[3] = TEST_TERM;
            publish_reply(bridge, reply, 4);
        }

        result = mca_page_task(bridge, 100);
        ++transactions;
        if (result == MCA_PAGE_READ_DONE) {
            assert(!write);
            assert(transactions == 7); /* address, five chunks, commit */
            return;
        }
        if (result == MCA_PAGE_WRITE_DONE) {
            assert(write);
            assert(transactions == 7);
            return;
        }
        assert(result == MCA_PAGE_PENDING);
    }
}

static void test_read_page(void) {
    mca_bridge_t bridge;
    uint8_t expected[MCA_PAGE_RAW_SIZE];
    size_t response_length;
    uint8_t *response;

    mca_bridge_init(&bridge);
    assert(mca_page_start(&bridge, false, 0x12345678, NULL, 0));
    emulate_page(&bridge, false, NULL, expected, 0x12345678);

    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_RESPONSE);
    response = mca_bridge_core0_response_buffer(&bridge, &response_length);
    assert(response == bridge.output + 4);
    assert(response_length == MCA_PAGE_RAW_SIZE);
    assert(response_length + 4 == 532);
    assert(response - 4 == bridge.output);
    assert((uintptr_t)(response - 4) % 4 == 0);
    assert(response[-4] == 0x55 && response[-3] == 0x5a);
    assert(response[-2] == 0x10 && response[-1] == 0x02);
    assert(memcmp(response, expected, sizeof(expected)) == 0);
    assert(mca_page_read_data(&bridge) == bridge.input + MCA_PAGE_STAGE_OFFSET);
    assert(memcmp(mca_page_read_data(&bridge), expected, sizeof(expected)) == 0);
    /* The caller keeps the response until USB IN completion. */
    assert(mca_bridge_core0_release_response(&bridge));
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    critical_section_deinit(&bridge.lock);
}

static void test_write_page(void) {
    mca_bridge_t bridge;
    uint8_t write_data[MCA_PAGE_RAW_SIZE];
    size_t response_length;
    uint8_t *response;

    for (size_t i = 0; i < sizeof(write_data); ++i)
        write_data[i] = (uint8_t)(i ^ (i >> 2));
    mca_bridge_init(&bridge);
    assert(mca_page_start(&bridge, true, 42, write_data, 0));
    assert(memcmp(bridge.input + MCA_PAGE_STAGE_OFFSET, write_data, sizeof(write_data)) == 0);
    emulate_page(&bridge, true, write_data, NULL, 42);

    response = mca_bridge_core0_response_buffer(&bridge, &response_length);
    assert(response && response_length == 2);
    assert(response[0] == 0x55 && response[1] == 0x5a);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_RESPONSE);
    assert(mca_bridge_core0_release_response(&bridge));
    critical_section_deinit(&bridge.lock);
}

static void test_cancellation_and_timeout(void) {
    mca_bridge_t bridge;
    uint8_t byte;

    mca_bridge_init(&bridge);
    assert(mca_page_start(&bridge, false, 1, NULL, 0));
    mca_page_reset(&bridge);
    assert(mca_page_task(&bridge, 1) == MCA_PAGE_PENDING);
    assert(mca_bridge_core1_begin(&bridge));
    assert(!mca_bridge_core1_receive(&bridge, &byte));
    assert(!mca_bridge_core1_finish(&bridge));
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    assert(mca_page_task(&bridge, 2) == MCA_PAGE_CANCELLED);

    assert(mca_page_start(&bridge, false, 2, NULL, 0));
    assert(mca_page_task(&bridge, MCA_PAGE_REPLY_TIMEOUT_US + 1) == MCA_PAGE_PENDING);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_REQUEST);
    assert(!mca_page_start(&bridge, false, 3, NULL, MCA_PAGE_REPLY_TIMEOUT_US + 2));
    assert(mca_bridge_core1_begin(&bridge));
    assert(!mca_bridge_core1_receive(&bridge, &byte));
    assert(!mca_bridge_core1_finish(&bridge));
    assert(mca_page_task(&bridge, MCA_PAGE_REPLY_TIMEOUT_US + 3) == MCA_PAGE_FAILED);
    critical_section_deinit(&bridge.lock);
}

static void test_bad_reply_edc(void) {
    mca_bridge_t bridge;
    uint8_t request[140];
    uint8_t reply[140] = {0};
    size_t length;

    mca_bridge_init(&bridge);
    assert(mca_page_start(&bridge, false, 7, NULL, 0));
    length = consume_request(&bridge, request);
    assert(length == 9);
    reply[0] = reply[1] = 0xff;
    reply[7] = TEST_ACK;
    reply[8] = TEST_TERM;
    publish_reply(&bridge, reply, 9);
    assert(mca_page_task(&bridge, 1) == MCA_PAGE_PENDING);

    length = consume_request(&bridge, request);
    assert(request[1] == TEST_SIO_READ_DATA);
    uint16_t chunk = request[2];
    reply[0] = reply[1] = 0xff;
    reply[3] = TEST_ACK;
    reply[4 + chunk] = 1; /* Deliberately wrong XOR checksum. */
    reply[5 + chunk] = TEST_TERM;
    publish_reply(&bridge, reply, chunk + 6);
    assert(mca_page_task(&bridge, 2) == MCA_PAGE_FAILED);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    assert(mca_page_read_data(&bridge) == NULL);
    critical_section_deinit(&bridge.lock);
}

static void test_bad_ack_and_term(void) {
    mca_bridge_t bridge;
    uint8_t request[140];
    uint8_t reply[140] = {0};
    size_t length;

    mca_bridge_init(&bridge);
    assert(mca_page_start(&bridge, false, 9, NULL, 0));
    length = consume_request(&bridge, request);
    assert(length == 9);
    reply[0] = reply[1] = 0xff;
    reply[7] = 0x00; /* Invalid address ACK. */
    reply[8] = TEST_TERM;
    publish_reply(&bridge, reply, 9);
    assert(mca_page_task(&bridge, 1) == MCA_PAGE_FAILED);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);

    memset(reply, 0, sizeof(reply));
    assert(mca_page_start(&bridge, false, 10, NULL, 2));
    length = consume_request(&bridge, request);
    reply[0] = reply[1] = 0xff;
    reply[7] = TEST_ACK;
    reply[8] = TEST_TERM;
    publish_reply(&bridge, reply, 9);
    assert(mca_page_task(&bridge, 3) == MCA_PAGE_PENDING);

    length = consume_request(&bridge, request);
    uint16_t chunk = request[2];
    reply[0] = reply[1] = 0xff;
    reply[3] = TEST_ACK;
    reply[4 + chunk] = 0;
    reply[5 + chunk] = 0; /* Invalid transaction terminator. */
    publish_reply(&bridge, reply, chunk + 6);
    assert(mca_page_task(&bridge, 4) == MCA_PAGE_FAILED);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_IDLE);
    critical_section_deinit(&bridge.lock);
}

int main(void) {
    test_read_page();
    test_write_page();
    test_cancellation_and_timeout();
    test_bad_reply_edc();
    test_bad_ack_and_term();
    return 0;
}
