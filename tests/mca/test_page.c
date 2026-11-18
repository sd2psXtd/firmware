#include "mca_page.h"
#include <assert.h>
#include <string.h>

enum { TEST_TERM = 0x7e, BAD_ACK = 1, BAD_TERM, BAD_EDC, SHORT_REPLY, CANCEL };
static mca_bridge_t bridge;
static uint64_t now;
static unsigned transactions, offset, fault, fault_at;
static bool writing;
static uint8_t data[MCA_PAGE_RAW_SIZE];
uint64_t time_us_64(void) { return now; }

static uint8_t edc(const uint8_t *bytes, size_t length) {
    uint8_t result = 0;
    for (size_t i = 0; i < length; ++i) result ^= bytes[i];
    return result;
}
static size_t request(uint8_t *bytes) {
    size_t length = 2;
    assert(mca_page_active());
    while (length < 134 && mca_page_receive(bytes + length)) ++length;
    ++transactions;
    return length;
}
static void reply(uint8_t *bytes, size_t length, size_t ack) {
    if (transactions == fault_at) {
        if (fault == BAD_ACK) bytes[ack] = 0;
        if (fault == BAD_TERM) bytes[length - 1] ^= 1;
        if (fault == BAD_EDC) bytes[length - 2] ^= 1;
        if (fault == SHORT_REPLY) --length;
        if (fault == CANCEL) mca_bridge_poll_timeout(&bridge, bridge.deadline);
    }
    for (size_t i = 2; i < length; ++i) mca_page_respond(bytes[i]);
    now += 100;
}
static void address(bool write) {
    uint8_t input[134] = {0}, output[9] = {0};
    assert(write == writing && request(input) == 9 && transactions == 1);
    assert(input[2] == 0x78 && input[3] == 0x56 && input[4] == 0x34 && input[5] == 0x12);
    assert(input[6] == edc(input + 2, 4));
    output[7] = 0x2b;
    output[8] = TEST_TERM;
    reply(output, sizeof(output), 7);
}
void ps2_mc_cmd_setReadAddress(void) { address(false); }
void ps2_mc_cmd_setWriteAddress(void) { address(true); }
void ps2_mc_cmd_readData(void) {
    uint8_t input[134] = {0}, output[134] = {0};
    size_t length = request(input), chunk = input[2];
    assert(!writing && chunk && chunk <= 128 && length == chunk + 6);
    assert(offset + chunk <= sizeof(data));
    output[3] = 0x2b;
    memcpy(output + 4, data + offset, chunk);
    output[chunk + 4] = edc(output + 4, chunk);
    output[chunk + 5] = TEST_TERM;
    offset += chunk;
    reply(output, length, 3);
}
void ps2_mc_cmd_writeData(void) {
    uint8_t input[134] = {0}, output[134] = {0};
    size_t length = request(input), chunk = input[2];
    assert(writing && chunk && chunk <= 128 && length == chunk + 6);
    assert(offset + chunk <= sizeof(data));
    assert(input[chunk + 3] == edc(input + 3, chunk));
    memcpy(data + offset, input + 3, chunk);
    offset += chunk;
    output[chunk + 4] = 0x2b;
    output[chunk + 5] = TEST_TERM;
    reply(output, length, chunk + 4);
}
void ps2_mc_cmd_commitData(void) {
    uint8_t input[134] = {0}, output[] = {0xff, 0xff, 0x2b, TEST_TERM};
    assert(request(input) == 4 && transactions == 7 && offset == sizeof(data));
    reply(output, sizeof(output), 2);
}
static void check_page(bool write, unsigned failure, unsigned phase) {
    uint8_t input[MCA_PAGE_RAW_SIZE + 9] = {0xaa, write ? 0x57 : 0x52, 3, 0x78, 0x56, 0x34, 0x12};
    uint8_t expected[MCA_PAGE_RAW_SIZE];
    for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)(i * 37 + 11);
    writing = write;
    transactions = offset = 0;
    fault = failure;
    fault_at = phase;
    size_t length = write ? sizeof(input) : 9;
    input[length - 2] = 0x55;
    input[length - 1] = 0x2b;
    if (write) {
        memcpy(input + 7, expected, sizeof(expected));
        memset(data, 0, sizeof(data));
    } else memcpy(data, expected, sizeof(data));
    memcpy(bridge.input, input, length);
    assert(mca_bridge_submit(&bridge, length) && mca_bridge_begin(&bridge));
    bool success = mca_page_execute(&bridge);
    assert(!mca_page_active() && success == (failure == 0));
    mca_bridge_finish(&bridge, success);
    assert(mca_bridge_state(&bridge) == MCA_BRIDGE_RESPONSE);
    if (failure) {
        assert(transactions == phase);
        assert(bridge.output_length == 2 && bridge.output[1] == 0xaf);
    } else {
        assert(transactions == 7 && bridge.output[0] == 0x55 && bridge.output[1] == 0x5a);
        if (write) {
            assert(bridge.output_length == 2 && memcmp(data, expected, sizeof(data)) == 0);
        } else {
            assert(bridge.output_length == sizeof(data) + 4);
            assert(bridge.output[2] == 0x10 && bridge.output[3] == 2);
            assert(memcmp(bridge.output + 4, expected, sizeof(data)) == 0);
        }
    }
    mca_bridge_release(&bridge);
}
int main(void) {
    mca_bridge_init(&bridge);
    check_page(false, 0, 0);
    check_page(true, 0, 0);
    check_page(false, BAD_ACK, 1);
    check_page(false, BAD_ACK, 2);
    check_page(false, BAD_TERM, 2);
    check_page(false, BAD_EDC, 2);
    check_page(true, SHORT_REPLY, 2);
    check_page(false, CANCEL, 2);
    check_page(false, BAD_ACK, 7);
    check_page(true, 0, 0); /* Recovery after each failed operation. */
    critical_section_deinit(&bridge.lock);
    return 0;
}
