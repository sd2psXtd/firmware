#include "mca_page_transfer.h"
#include "mca_transport.h"
#include "ps2/card_emu/ps2_mc_commands.h"

#include <string.h>

#define PAGE_PACKET_SIZE (128 + 7)

enum { PAGE_IDLE, PAGE_ADDRESS, PAGE_DATA, PAGE_COMMIT, PAGE_FAILED };
static struct {
    uint8_t stage;
    bool write;
    uint64_t deadline;
    uint8_t term;
    uint8_t chunk;
    uint16_t offset;
    uint8_t data[MCA_PS2_PAGE_RAW];
} transfer;

static uint8_t edc(const uint8_t *data, uint16_t len) {
    uint8_t checksum = 0;
    uint16_t i;

    for (i = 0; i < len; ++i)
        checksum ^= data[i];
    return checksum;
}

bool mca_page_transfer_busy(void) { return transfer.stage != PAGE_IDLE; }

bool mca_page_transfer_expired(uint64_t now_us) {
    return mca_page_transfer_busy() && now_us >= transfer.deadline &&
           !mca_transport_response_ready();
}

void mca_page_transfer_reset(void) { transfer.stage = PAGE_IDLE; }

bool mca_page_transfer_start(uint32_t page, const uint8_t *write_data, uint64_t now_us) {
    uint8_t request[9] = {PS2_SIO2_CMD_IDENTIFIER,
        write_data ? PS2_SIO2_CMD_SET_WRITE_ADDRESS : PS2_SIO2_CMD_SET_READ_ADDRESS};
    unsigned i;
    bool started = false;

    if (!mca_page_transfer_busy()) {
        for (i = 0; i < 4; ++i)
            request[2 + i] = (uint8_t)(page >> (8 * i));
        request[6] = edc(request + 2, 4);
        if (mca_transport_submit(request, sizeof(request))) {
            transfer.write = write_data != NULL;
            transfer.deadline = now_us + MCA_CARD_REPLY_TIMEOUT_US;
            transfer.offset = 0;
            if (write_data)
                memcpy(transfer.data, write_data, sizeof(transfer.data));
            transfer.stage = PAGE_ADDRESS;
            started = true;
        }
    }
    return started;
}

static mca_page_result_t finish(mca_page_result_t result) {
    transfer.stage = result == MCA_PAGE_FAILED ? PAGE_FAILED : PAGE_IDLE;
    return result;
}

static bool validate_reply(const uint8_t *packet, uint16_t len) {
    uint16_t expected = transfer.stage == PAGE_ADDRESS ? 9 :
                         transfer.stage == PAGE_COMMIT ? 4 : transfer.chunk + 6;
    unsigned ack;
    bool valid = len == expected && packet[0] == 0xff && packet[1] == 0xff;

    if (valid) {
        ack = transfer.stage == PAGE_DATA && !transfer.write ? 3 : len - 2;
        valid = packet[ack] == 0x2b;
        if (transfer.stage == PAGE_ADDRESS)
            transfer.term = packet[len - 1];
        else
            valid = valid && packet[len - 1] == transfer.term;
        if (transfer.stage == PAGE_DATA && !transfer.write)
            valid = valid && edc(packet + 4, transfer.chunk) == packet[4 + transfer.chunk];
    }
    return valid;
}

static bool submit_next_request(uint8_t packet[PAGE_PACKET_SIZE], uint64_t now_us) {
    uint16_t len, remaining;
    bool submitted;

    if (transfer.stage == PAGE_DATA) {
        if (!transfer.write)
            memcpy(transfer.data + transfer.offset, packet + 4, transfer.chunk);
        transfer.offset += transfer.chunk;
    }

    memset(packet, 0, PAGE_PACKET_SIZE);
    packet[0] = PS2_SIO2_CMD_IDENTIFIER;
    if (transfer.offset == MCA_PS2_PAGE_RAW) {
        packet[1] = PS2_SIO2_CMD_COMMIT_DATA;
        len = 4;
        transfer.stage = PAGE_COMMIT;
    } else {
        remaining = MCA_PS2_PAGE_RAW - transfer.offset;
        transfer.chunk = remaining > 128 ? 128 : remaining;
        packet[1] = transfer.write ? PS2_SIO2_CMD_WRITE_DATA : PS2_SIO2_CMD_READ_DATA;
        packet[2] = transfer.chunk;
        len = transfer.chunk + 6;
        if (transfer.write) {
            memcpy(packet + 3, transfer.data + transfer.offset, transfer.chunk);
            packet[3 + transfer.chunk] = edc(packet + 3, transfer.chunk);
        }
        transfer.stage = PAGE_DATA;
    }
    submitted = mca_transport_submit(packet, len);
    if (submitted)
        transfer.deadline = now_us + MCA_CARD_REPLY_TIMEOUT_US;
    return submitted;
}

mca_page_result_t mca_page_transfer_task(uint8_t *read_data, uint64_t now_us) {
    /* One reply per call, never wait for core 1. The extra byte detects an
     * oversized reply. Reuse this buffer for the next request. */
    uint8_t packet[PAGE_PACKET_SIZE];
    uint16_t len;
    mca_page_result_t result = MCA_PAGE_IDLE;

    if (mca_page_transfer_busy()) {
        if (transfer.stage == PAGE_FAILED || !mca_transport_is_active()) {
            result = finish(MCA_PAGE_FAILED);
        } else if (!mca_transport_response_ready()) {
            result = mca_page_transfer_expired(now_us) ? finish(MCA_PAGE_FAILED) : MCA_PAGE_PENDING;
        } else {
            len = mca_transport_take_response(packet, sizeof(packet));
            if (!validate_reply(packet, len)) {
                result = finish(MCA_PAGE_FAILED);
            } else if (transfer.stage == PAGE_COMMIT) {
                if (!transfer.write)
                    memcpy(read_data, transfer.data, sizeof(transfer.data));
                result = finish(transfer.write ? MCA_PAGE_WRITE_DONE : MCA_PAGE_READ_DONE);
            } else {
                result = submit_next_request(packet, now_us) ? MCA_PAGE_PENDING : finish(MCA_PAGE_FAILED);
            }
        }
    }
    return result;
}
