#include "mca_page.h"
#include "ps2/card_emu/ps2_mc_commands.h"

#include <string.h>

#define PAGE_CHUNK_MAX 128u

/* Only core 1 uses this scratch space, while it owns the complete operation. */
static struct {
    mca_bridge_t *bridge;
    uint8_t request[PAGE_CHUNK_MAX + 6];
    uint8_t response[PAGE_CHUNK_MAX + 6];
    size_t length, received, sent;
} transaction;

bool mca_page_active(void) { return transaction.bridge != NULL; }

bool mca_page_receive(uint8_t *byte) {
    if (mca_bridge_cancelled(transaction.bridge) || transaction.received == transaction.length)
        return false;
    *byte = transaction.request[transaction.received++];
    return true;
}

void mca_page_respond(uint8_t byte) {
    if (transaction.sent < sizeof(transaction.response))
        transaction.response[transaction.sent] = byte;
    ++transaction.sent;
}

static uint8_t edc(const uint8_t *bytes, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; ++i)
        checksum ^= bytes[i];
    return checksum;
}

static bool exchange(void (*handler)(void), size_t length, size_t ack, uint8_t term) {
    transaction.length = length;
    /* The caller dispatches the handler directly, replacing the main loop's
     * identifier/subcommand exchange and its first two FF response bytes. */
    transaction.received = transaction.sent = 2;
    transaction.response[0] = transaction.response[1] = 0xff;
    if (mca_bridge_cancelled(transaction.bridge))
        return false;
    mca_bridge_restart_timeout(transaction.bridge);
    handler();
    return !mca_bridge_cancelled(transaction.bridge) && transaction.sent == length &&
        transaction.response[ack] == 0x2b && transaction.response[length - 1] == term;
}

bool mca_page_execute(mca_bridge_t *bridge) {
    bool write = bridge->input[1] == 0x57;
    uint8_t *request = transaction.request;
    uint8_t *response = transaction.response;
    bool success = false;
    transaction.bridge = bridge;

    /* The address handler returns the current terminator. Subsequent exchanges
     * must preserve it, just as the raw SIO path does. */
    memset(request, 0, sizeof(transaction.request));
    memcpy(request + 2, bridge->input + 3, 4);
    request[6] = edc(request + 2, 4);
    transaction.length = 9;
    transaction.received = transaction.sent = 2;
    response[0] = response[1] = 0xff;
    mca_bridge_restart_timeout(bridge);
    if (write)
        ps2_mc_cmd_setWriteAddress();
    else
        ps2_mc_cmd_setReadAddress();
    if (mca_bridge_cancelled(bridge) || transaction.sent != 9 || response[7] != 0x2b)
        goto done;
    uint8_t term = response[8];

    for (size_t offset = 0; offset < MCA_PAGE_RAW_SIZE;) {
        size_t chunk = MCA_PAGE_RAW_SIZE - offset;
        if (chunk > PAGE_CHUNK_MAX)
            chunk = PAGE_CHUNK_MAX;
        memset(request, 0, sizeof(transaction.request));
        request[2] = (uint8_t)chunk;
        if (write) {
            memcpy(request + 3, bridge->input + 7 + offset, chunk);
            request[3 + chunk] = edc(request + 3, chunk);
        }
        if (!exchange(write ? ps2_mc_cmd_writeData : ps2_mc_cmd_readData,
                      chunk + 6, write ? chunk + 4 : 3, term))
            goto done;
        if (!write) {
            if (edc(response + 4, chunk) != response[4 + chunk])
                goto done;
            memcpy(bridge->output + 4 + offset, response + 4, chunk);
        }
        offset += chunk;
    }

    memset(request, 0, sizeof(transaction.request));
    if (!exchange(ps2_mc_cmd_commitData, 4, 2, term))
        goto done;
    bridge->output[0] = 0x55;
    bridge->output[1] = 0x5a;
    if (write) {
        bridge->output_length = 2;
    } else {
        bridge->output[2] = (uint8_t)MCA_PAGE_RAW_SIZE;
        bridge->output[3] = (uint8_t)(MCA_PAGE_RAW_SIZE >> 8);
        bridge->output_length = MCA_PAGE_RAW_SIZE + 4;
    }
    success = true;
done:
    transaction.bridge = NULL;
    return success;
}
