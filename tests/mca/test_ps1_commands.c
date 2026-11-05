#include "mca_bridge.h"
#include "mca_comms.h"
#include "mca_protocol.h"
#include "ps1/ps1_memory_card.c"
#include "ps1/ps1_mc_data_interface.h"
#include "bigmem.h"
#include "ps1/ps1_dirty.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>

extern mca_bridge_t mca_bridge;
ps1_memory_card_backend_t ps1_card_backend_pio;
static uint8_t usb_reply[MCA_FRAME_MAX];
static size_t usb_reply_length;
static const uint8_t *pio_input;
static size_t pio_input_length, pio_input_pos, pio_reply_length;
static uint8_t pio_reply[138];

spin_lock_t *ps1_dirty_spin_lock;
volatile uint32_t ps1_dirty_lockout;
int ps1_dirty_activity;
static unsigned dirty_marks;
void ps1_dirty_mark(uint32_t page) { assert(page < 1024); ++dirty_marks; }

#define card_page (cache + 2 * PS1_PAGE_SIZE)

uint64_t time_us_64(void) { return 0; }
uint64_t RAM_time_us_64(void) { return 0; }
void sleep_ms(uint32_t ms) { (void)ms; }
void tud_task_ext(uint32_t timeout_ms, bool in_isr) { (void)timeout_ms; (void)in_isr; }
int settings_get_mode(bool temporary) { (void)temporary; return MODE_PS1; }
int settings_get_ps2_variant(void) { return 0; }
bool ps2_cardman_is_accessible(void) { return true; }
bool ps2_cardman_is_idle(void) { return true; }
uint32_t ps2_cardman_get_card_size(void) { return 8u * 1024 * 1024; }
bool ps2_mc_auth_isValid(void) { return true; }
void mca_device_set_presence(bool present) { (void)present; }
void mca_device_task(void) {}
bool mca_device_send(const uint8_t *data, size_t length) {
    assert(length <= sizeof(usb_reply));
    memcpy(usb_reply, data, length);
    usb_reply_length = length;
    return true;
}
bool settings_get_ps1_controllercombo(void) { return false; }
bool ps1_mmce_set_gameid(const uint8_t *game_id) { (void)game_id; return false; }
void ps1_mmce_prev_ch(bool delay) { (void)delay; }
void ps1_mmce_next_ch(bool delay) { (void)delay; }
void ps1_mmce_prev_idx(bool delay) { (void)delay; }
void ps1_mmce_next_idx(bool delay) { (void)delay; }
void ps1_mmce_switch_bootcard(bool delay) { (void)delay; }
void ps1_mmce_reset(bool delay) { (void)delay; }
static size_t transact(const uint8_t *sio, size_t length) {
    uint8_t request[MCA_FRAME_MAX] = {0xaa, 0x42};
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    uint8_t byte;
    assert(length <= MCA_RAW_MAX);
    request[2] = (uint8_t)length;
    request[3] = (uint8_t)(length >> 8);
    memcpy(request + 4, sio, length);
    usb_reply_length = 0;
    callbacks->out_received(request, (uint16_t)(length + 4));
    mca_protocol_task();
    assert(ps1_card_backend_mca_wait());
    assert(ps1_card_backend->recv_mc(&byte) == PS1_RECEIVE_OK && byte == 0x81);
    ps1_card_backend->respond(flag);
    assert(ps1_card_backend->recv_mc(&byte) == PS1_RECEIVE_OK);
    switch (byte) {
        case 'S': mc_cmd_get_card_id(); break;
        case 'R': mc_cmd_read(false); break;
        case 'B': mc_cmd_read(true); break;
        case 'W': mc_cmd_write(); break;
        default: assert(false);
    }
    ps1_card_backend_mca_finish();
    mca_protocol_task();
    assert(usb_reply_length >= 4);
    assert(usb_reply[0] == 0x55 && usb_reply[1] == 0x5a);
    assert(usb_reply[2] == ((usb_reply_length - 4) & 255));
    assert(usb_reply[3] == ((usb_reply_length - 4) >> 8));
    callbacks->in_complete(true, usb_reply_length);
    return usb_reply_length;
}

static uint8_t pio_receive(uint8_t *byte) {
    if (pio_input_pos == pio_input_length) return PS1_RECEIVE_RESET;
    *byte = pio_input[pio_input_pos++];
    return PS1_RECEIVE_OK;
}

static void pio_respond(uint8_t byte) {
    assert(pio_reply_length < sizeof(pio_reply));
    pio_reply[pio_reply_length++] = byte;
}

static uint8_t (*original_mca_receive)(uint8_t *);
static unsigned loop_received;
static uint8_t loop_receive(uint8_t *byte) {
    uint8_t result = original_mca_receive(byte);
    if (result == PS1_RECEIVE_OK && ++loop_received == 141)
        ps1_mc_exit_request = 1;
    return result;
}
static void *run_card_loop(void *unused) {
    (void)unused;
    mc_main_loop();
    return NULL;
}
static void loop_transact(const uint8_t *sio, size_t length) {
    uint8_t request[MCA_FRAME_MAX] = {0xaa, 0x42};
    const mca_device_callbacks_t *callbacks = mca_protocol_callbacks();
    request[2] = (uint8_t)length;
    request[3] = (uint8_t)(length >> 8);
    memcpy(request + 4, sio, length);
    usb_reply_length = 0;
    callbacks->out_received(request, length + 4);
    for (unsigned i = 0; i < 1000000 && !usb_reply_length; ++i) {
        mca_protocol_task();
        sched_yield();
    }
    assert(usb_reply_length != 0);
    callbacks->in_complete(true, usb_reply_length);
}

int main(void) {
    uint8_t id[10] = {0x81, 'S'};
    uint8_t read[140] = {0x81, 'R', 0, 0, 0, 2};
    uint8_t write[138] = {0x81, 'W', 0, 0, 0, 2};
    uint8_t checksum = 2;
    mca_protocol_init();
    mca_protocol_set_ready(true);
    ps1_card_backend = &ps1_card_backend_mca;
    flag = 8;

    transact(id, sizeof(id));
    const uint8_t expected_id[] = {0xff, 8, 0x5a, 0x5d, 0x5c, 0x5d, 4, 0, 0, 0x80};
    assert(memcmp(usb_reply + 4, expected_id, sizeof(expected_id)) == 0);

    for (unsigned i = 0; i < 128; ++i) {
        card_page[i] = (uint8_t)(i * 3 + 1);
        checksum ^= card_page[i];
    }
    transact(read, sizeof(read));
    /* The Sony library checks these SIO bytes before accepting a read. */
    assert(usb_reply[6] == 0x5a && usb_reply[7] == 0x5d && usb_reply[8] == 0);
    assert(usb_reply[10] == 0x5c && usb_reply[11] == 0x5d);
    assert(usb_reply[12] == 0 && usb_reply[13] == 2);
    assert(memcmp(usb_reply + 14, card_page, PS1_PAGE_SIZE) == 0);
    assert(usb_reply[142] == checksum && usb_reply[143] == 0x47);

    checksum = 2;
    for (unsigned i = 0; i < 128; ++i) {
        write[6 + i] = (uint8_t)(255 - i);
        checksum ^= write[6 + i];
    }
    write[134] = checksum;
    transact(write, sizeof(write));
    /* The Sony library also validates the write acknowledgements. */
    assert(usb_reply[6] == 0x5a && usb_reply[7] == 0x5d);
    assert(usb_reply[139] == 0x5c && usb_reply[140] == 0x5d);
    assert(usb_reply[141] == 0x47 && dirty_marks == 1);
    assert(memcmp(card_page, write + 6, PS1_PAGE_SIZE) == 0);
    uint8_t saved_page[128];
    memcpy(saved_page, card_page, sizeof(saved_page));
    memset(write + 6, 0x34, 128);
    write[134] = 3; /* The correct checksum for this page is 2. */
    assert(transact(write, sizeof(write)) == sizeof(write) + 4);
    assert(usb_reply[141] == 0x4e && dirty_marks == 1);
    assert(memcmp(card_page, saved_page, sizeof(saved_page)) == 0);

    /* Truncated writes must not mutate the live non-PSRAM card cache. */
    transact(write, 70);
    assert(dirty_marks == 1 && memcmp(card_page, saved_page, sizeof(saved_page)) == 0);

    /* The last valid page must not start a speculative read of page 1024. */
    read[4] = 3; read[5] = 0xff;
    assert(transact(read, sizeof(read)) == sizeof(read) + 4);
    assert(usb_reply[143] == 0x47);
    read[1] = 'B';
    assert(transact(read, sizeof(read)) < sizeof(read) + 4);
    read[1] = 'R';
    read[4] = 4; read[5] = 0;
    assert(transact(read, sizeof(read)) < sizeof(read) + 4);
    read[4] = 0; read[5] = 2;

    /* A valid checksum cannot authorize an out-of-range page write. */
    write[4] = 4; write[5] = 0; write[134] = 4;
    assert(transact(write, sizeof(write)) == sizeof(write) + 4);
    assert(usb_reply[141] == 0xff && dirty_marks == 1);
    assert(memcmp(card_page, saved_page, sizeof(saved_page)) == 0);

    /* A valid write through the existing PIO path retains its SIO bytes and
     * commit behavior. The timing and real PIO hardware need separate tests. */
    uint8_t pio_write[138] = {0x81, 'W', 0, 0, 0, 2};
    memset(pio_write + 6, 0x44, 128);
    pio_write[134] = 2;
    pio_input = pio_write + 2;
    pio_input_length = sizeof(pio_write) - 2;
    pio_input_pos = pio_reply_length = 0;
    ps1_card_backend_pio.recv_mc = pio_receive;
    ps1_card_backend_pio.respond = pio_respond;
    ps1_card_backend = &ps1_card_backend_pio;
    ps1_card_active = true;
    mc_cmd_write();
    assert(pio_input_pos + 1 == pio_input_length && pio_reply_length == 136);
    assert(pio_reply[0] == 0x5a && pio_reply[1] == 0x5d);
    assert(pio_reply[4] == 2 && pio_reply[132] == 0x44);
    assert(pio_reply[133] == 0x5c && pio_reply[134] == 0x5d && pio_reply[135] == 0x47);
    assert(dirty_marks == 2 && memcmp(card_page, pio_write + 6, PS1_PAGE_SIZE) == 0);
    memcpy(card_page, saved_page, PS1_PAGE_SIZE);
    ps1_card_backend = &ps1_card_backend_mca;
    ps1_card_active = false;

    /* Exercise the actual dispatch loop after Sony's optional 81 58 probe. */
    pthread_t core1;
    const uint8_t probe[5] = {0x81, 0x58};
    original_mca_receive = ps1_card_backend_mca.recv_mc;
    ps1_card_backend_mca.recv_mc = loop_receive;
    loop_received = 0;
    ps1_mc_exit_request = 0;
    assert(pthread_create(&core1, NULL, run_card_loop, NULL) == 0);
    loop_transact(probe, sizeof(probe));
    assert(usb_reply_length == 6 && usb_reply[2] == 2 && usb_reply[3] == 0);
    loop_transact(read, sizeof(read));
    assert(usb_reply_length == 144 && usb_reply[6] == 0x5a && usb_reply[7] == 0x5d);
    assert(memcmp(usb_reply + 14, card_page, PS1_PAGE_SIZE) == 0);
    assert(usb_reply[143] == 0x47);
    assert(pthread_join(core1, NULL) == 0);
    ps1_card_backend_mca.recv_mc = original_mca_receive;
    critical_section_deinit(&mca_bridge.lock);
    return 0;
}
