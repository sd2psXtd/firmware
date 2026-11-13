#include "settings.h"
#include "mca_bridge.h"
#include "mca_comms.h"
#include "mca_protocol.h"
#include "ps2/ps2_card_backend_mca.h"
#include "ps2/card_emu/ps2_mc_commands.h"
#include "ps2/card_emu/ps2_mc_data_interface.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

extern mca_bridge_t mca_bridge;
ps2_memory_card_backend_t *ps2_card_backend = &ps2_card_backend_mca;
volatile bool ps2_card_active;
volatile int ps2_mc_exit_request;
volatile bool mmceman_tx_queued;
volatile uint8_t mmceman_tx_byte, mmceman_mcman_retry_counter;
void (*mmceman_callback)(void);
uint8_t term = 0xff;
/* The read fixture uses only 0xff card bytes and its first 0x77 ECC byte. */
const uint8_t EccTable[256] = {[0x77] = 0x44};

static uint8_t usb_reply[MCA_FRAME_MAX];
static size_t usb_reply_length;
static uint8_t card_page[PS2_PAGE_SIZE];
static volatile ps2_mcdi_page_t page = {.data = card_page};
static unsigned commits;

uint64_t time_us_64(void) { return 0; }
void sleep_us(uint64_t us) { (void)us; }
void sleep_ms(uint32_t ms) { (void)ms; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) { (void)timeout_ms; (void)in_isr; }
int settings_get_mode(bool temporary) { (void)temporary; return 1; }
int settings_get_usb_mode(bool current) { (void)current; return USB_MODE_MCA; }
int settings_get_ps2_variant(void) { return 0; }
bool ps2_cardman_is_accessible(void) { return true; }
bool ps2_cardman_is_idle(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return 8u * 1024 * 1024; }
bool ps2_mc_auth_isValid(void) { return true; }
bool ps2_mc_data_interface_delay_required(void) { return false; }
bool ps2_mc_data_interface_data_available(void) { return true; }
void ps2_mc_data_interface_setup_read_page(uint32_t p, bool ahead, bool wait) {
    (void)p; (void)ahead; (void)wait;
}
volatile ps2_mcdi_page_t *ps2_mc_data_interface_get_page(uint32_t p) {
    (void)p;
    return &page;
}
void ps2_mc_data_interface_wait_for_byte(uint32_t offset) { (void)offset; }
void ps2_mc_data_interface_write_mc(uint32_t p, void *data) {
    assert(p == 7);
    memcpy(card_page, data, sizeof(card_page));
    ++commits;
}
void mca_device_set_presence(bool present) { (void)present; }
void mca_device_task(void) {}
bool mca_device_send(const uint8_t *data, size_t length) {
    assert(length <= sizeof(usb_reply));
    memcpy(usb_reply, data, length);
    usb_reply_length = length;
    return true;
}

static void execute_card_command(void) {
    uint8_t byte;
    assert(ps2_card_backend->transactionAborted());
    ps2_card_backend->signalTransactionReady();
    assert(ps2_card_backend->receiveFirst(&byte) == PS2_RECEIVE_OK && byte == 0x81);
    ps2_card_backend->respond(0xff);
    assert(ps2_card_backend->receive(&byte) == PS2_RECEIVE_OK);
    switch (byte) {
        case 0x11: ps2_mc_cmd_0x11(); break;
        case 0x26: ps2_mc_cmd_getSpecs(); break;
        case 0x27: ps2_mc_cmd_setTerminator(); break;
        case 0x28: ps2_mc_cmd_getTerminator(); break;
        case 0x22: ps2_mc_cmd_setWriteAddress(); break;
        case 0x23: ps2_mc_cmd_setReadAddress(); break;
        case 0x42: ps2_mc_cmd_writeData(); break;
        case 0x43: ps2_mc_cmd_readData(); break;
        case 0x81: ps2_mc_cmd_commitData(); break;
        default: assert(false);
    }
    ps2_card_backend->finish();
}

static void transact(const uint8_t *request, size_t length) {
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();

    usb_reply_length = 0;
    callbacks->out_received(request, length);
    mca_protocol_task();
    execute_card_command();
    mca_protocol_task();
    assert(usb_reply_length >= 4 && usb_reply[0] == 0x55 && usb_reply[1] == 0x5a);
    assert(usb_reply[2] == length - 4 && usb_reply[3] == 0);
    assert(usb_reply_length == length);
    callbacks->in_complete(true, usb_reply_length);
}

static void page_transfer(const uint8_t *request, size_t length) {
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    usb_reply_length = 0;
    for (size_t pos = 0; pos < length;) {
        size_t bytes = length - pos;
        if (bytes > 64) bytes = 64;
        callbacks->out_received(request + pos, bytes);
        pos += bytes;
        mca_protocol_task();
    }
    for (unsigned i = 0; i < 7; ++i) {
        assert(!usb_reply_length);
        execute_card_command();
        mca_protocol_task();
    }
    assert(usb_reply_length && usb_reply[0] == 0x55 && usb_reply[1] == 0x5a);
    callbacks->in_complete(true, usb_reply_length);
}

int main(void) {
    /* Request lengths and offsets follow the local ps3mca-tool mcio.c. */
    const uint8_t changed[] = {0xaa, 0x42, 4, 0, 0x81, 0x11, 0, 0};
    const uint8_t set_term[] = {0xaa, 0x42, 5, 0, 0x81, 0x27, 0x5a, 0, 0};
    const uint8_t get_term[] = {0xaa, 0x42, 5, 0, 0x81, 0x28, 0, 0, 0};
    const uint8_t specs[] = {0xaa, 0x42, 13, 0, 0x81, 0x26, 0x5a, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    mca_protocol_init();
    mca_protocol_set_ready(true);

    transact(changed, sizeof(changed));
    assert(usb_reply[7] != 0x66);
    transact(set_term, sizeof(set_term));
    assert(usb_reply[8] == 0x5a);
    transact(get_term, sizeof(get_term));
    assert(usb_reply[8] == 0x5a);
    transact(specs, sizeof(specs));
    assert(usb_reply[16] == 0x5a);
    assert(usb_reply[7] == 0 && usb_reply[8] == 2); /* 512-byte pages */
    assert(usb_reply[9] == 16 && usb_reply[10] == 0); /* pages per erase block */
    assert(usb_reply[11] == 0 && usb_reply[12] == 0x40 &&
           usb_reply[13] == 0 && usb_reply[14] == 0); /* 16384 pages */
    uint8_t checksum = 0;
    for (unsigned i = 7; i < 15; ++i) checksum ^= usb_reply[i];
    assert(checksum == usb_reply[15]);

    /* Read a complete erased page with the same 128-byte chunks and spare
     * read that ps3mca-tool uses. The first twelve spare bytes are ECC. */
    uint8_t request[4 + 134] = {0xaa, 0x42, 9, 0, 0x81, 0x23, 7, 0, 0, 0, 7, 0, 0};
    memset(card_page, 0xff, sizeof(card_page));
    transact(request, 13);
    assert(usb_reply[12] == 0x5a);
    for (unsigned chunk = 0; chunk < 5; ++chunk) {
        unsigned bytes = chunk == 4 ? 16 : 128;
        size_t payload = bytes + 6;
        memset(request + 4, 0, payload);
        request[2] = (uint8_t)payload;
        request[4] = 0x81; request[5] = 0x43; request[6] = bytes;
        transact(request, payload + 4);
        assert(usb_reply[payload + 3] == 0x5a);
        uint8_t edc = 0;
        for (unsigned i = 0; i < bytes; ++i) {
            uint8_t actual = usb_reply[8 + i];
            edc ^= actual;
            if (chunk < 4) assert(actual == 0xff);
            else if (i < 12) assert(actual == (i % 3 ? 0x7f : 0x77));
        }
        assert(usb_reply[8 + bytes] == edc);
    }
    const uint8_t commit[] = {0xaa, 0x42, 4, 0, 0x81, 0x81, 0, 0};
    transact(commit, sizeof(commit));
    assert(usb_reply[7] == 0x5a && commits == 0);

    /* Write through the same raw commands and confirm the existing command
     * handler does not commit the page before the final SIO commit. */
    request[2] = 9; request[4] = 0x81; request[5] = 0x22;
    request[6] = 7; request[7] = request[8] = request[9] = 0;
    request[10] = 7; request[11] = request[12] = 0;
    transact(request, 13);
    for (unsigned chunk = 0; chunk < 4; ++chunk) {
        memset(request + 4, 0, 134);
        request[2] = 134; request[4] = 0x81; request[5] = 0x42; request[6] = 128;
        memset(request + 7, 0xa5, 128); /* XOR of 128 equal bytes is zero. */
        transact(request, sizeof(request));
        assert(usb_reply[137] == 0x5a && commits == 0);
    }
    transact(commit, sizeof(commit));
    assert(usb_reply[7] == 0x5a && commits == 1);
    for (unsigned i = 0; i < sizeof(card_page); ++i) assert(card_page[i] == 0xa5);

    /* Sony's high-level page commands must traverse the same real handlers. */
    const uint8_t page_read[] = {0xaa, 0x52, 3, 7, 0, 0, 0, 0x55, 0x2b};
    memset(card_page, 0xff, sizeof(card_page));
    page_transfer(page_read, sizeof(page_read));
    assert(usb_reply_length == 532 && usb_reply[2] == 0x10 && usb_reply[3] == 2);
    for (unsigned i = 0; i < sizeof(card_page); ++i) assert(usb_reply[4 + i] == 0xff);
    for (unsigned i = 0; i < 12; ++i) assert(usb_reply[516 + i] == (i % 3 ? 0x7f : 0x77));

    uint8_t page_write[537] = {0xaa, 0x57, 3, 7, 0, 0, 0};
    memset(page_write + 7, 0xa5, 528);
    page_write[535] = 0x55; page_write[536] = 0x2b;
    page_transfer(page_write, sizeof(page_write));
    assert(usb_reply_length == 2 && commits == 2);
    for (unsigned i = 0; i < sizeof(card_page); ++i) assert(card_page[i] == 0xa5);

    /* A USB reset after the page data was accepted must not discard the
     * pending commit or send the old reply on the new USB connection. */
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    memset(page_write + 7, 0x3c, 528);
    usb_reply_length = 0;
    for (size_t pos = 0; pos < sizeof(page_write);) {
        size_t bytes = sizeof(page_write) - pos;
        if (bytes > 64) bytes = 64;
        callbacks->out_received(page_write + pos, bytes);
        pos += bytes;
        mca_protocol_task();
    }
    for (unsigned i = 0; i < 6; ++i) {
        execute_card_command();
        mca_protocol_task();
    }
    assert(commits == 2 && mca_bridge_state(&mca_bridge) == MCA_BRIDGE_REQUEST);
    callbacks->bus_reset();
    execute_card_command();
    mca_protocol_task();
    assert(commits == 3 && usb_reply_length == 0);
    assert(mca_bridge_state(&mca_bridge) == MCA_BRIDGE_IDLE);
    for (unsigned i = 0; i < sizeof(card_page); ++i) assert(card_page[i] == 0x3c);

    const uint8_t detect[] = {0xaa, 0x40};
    callbacks->out_received(detect, sizeof(detect));
    mca_protocol_task();
    assert(usb_reply_length == 2 && usb_reply[0] == 0x55 && usb_reply[1] == 2);
    callbacks->in_complete(true, usb_reply_length);
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
