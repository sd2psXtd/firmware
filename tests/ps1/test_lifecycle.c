/* Exercise the production Core 0/Core 1 handshake with a mock transport. */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include "ps1/ps1_memory_card.c"

static atomic_uint initialized, started, stopped, deinitialized;
static unsigned writes, responses;
static uint8_t receive_status;
static int mode, usb_mode;

uint64_t time_us_64(void) { return 0; }
void multicore_lockout_victim_init(void) {}
int settings_get_mode(bool temporary) { (void)temporary; return mode; }
int settings_get_usb_mode(bool current) { (void)current; return usb_mode; }
bool settings_get_mca_enabled(void) { return usb_mode == USB_MODE_MCA; }
bool settings_get_ps1_controllercombo(void) { return false; }
bool ps1_mmce_set_gameid(const uint8_t* id) { (void)id; return false; }
void ps1_mmce_prev_ch(bool delay) { (void)delay; }
void ps1_mmce_next_ch(bool delay) { (void)delay; }
void ps1_mmce_prev_idx(bool delay) { (void)delay; }
void ps1_mmce_next_idx(bool delay) { (void)delay; }
void ps1_mmce_switch_bootcard(bool delay) { (void)delay; }
void ps1_mmce_reset(bool delay) { (void)delay; }
void ps1_mc_data_interface_setup_read_page(uint32_t page) { (void)page; }
uint8_t* ps1_mc_data_interface_get_page(uint32_t page) {
    static uint8_t data[PS1_PAGE_SIZE];
    (void)page;
    return data;
}
void ps1_mc_data_interface_wait_for_byte(uint32_t offset) { (void)offset; }
void ps1_mc_data_interface_write_byte(uint32_t address, uint8_t byte) {
    (void)address; (void)byte; ++writes;
}
void ps1_mc_data_interface_write_mc(uint32_t page) { (void)page; ++writes; }

static void init(void) { ++initialized; }
static void reset(void) {}
static void startup(void) { ++started; }
static void shutdown(void) { ps1_card_active = false; ++stopped; }
static void deinit(void) { ++deinitialized; }
static uint8_t receive_first(uint8_t* byte) {
    (void)byte;
    while (!ps1_mc_exit_request)
        sched_yield();
    return PS1_RECEIVE_EXIT;
}
static uint8_t receive(uint8_t* byte) { (void)byte; return receive_status; }
static void respond(uint8_t byte) { (void)byte; ++responses; }
static ps1_memory_card_backend_t backend = {
    .init = init, .reset = reset, .startup = startup,
    .shutdown = shutdown, .deinit = deinit,
    .receive_first = receive_first, .recv_mc = receive,
    .recv_cntrl = receive, .respond = respond,
};
ps1_memory_card_backend_t* ps1_card_backend_get(void) { return &backend; }

static void* worker(void* unused) {
    (void)unused;
    ps1_memory_card_main();
    return NULL;
}

static void check_session(int next_mode, int next_usb, bool pause_first) {
    unsigned init_before = initialized, starts_before = started;
    unsigned stops_before = stopped, deinits_before = deinitialized;
    mode = MODE_PS1;
    usb_mode = USB_MODE_UART;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, worker, NULL) == 0);
    ps1_memory_card_enter();
    assert(initialized == init_before + 1 && started == starts_before + 1);

    /* Card/channel switches stop transactions, not the resident worker. */
    ps1_memory_card_exit();
    assert(stopped == stops_before + 1 && deinitialized == deinits_before);
    ps1_memory_card_enter();
    assert(initialized == init_before + 1 && started == starts_before + 2);
    if (pause_first)
        ps1_memory_card_exit();

    mode = next_mode;
    usb_mode = next_usb;
    ps1_memory_card_stop();
    /* stop() must acknowledge only AFTER backend cleanup. */
    assert(deinitialized == deinits_before + 1);
    assert(stopped == stops_before + 2);
    assert(!memcard_running && !mc_stop_request && !mc_stop_response);
    assert(pthread_join(thread, NULL) == 0);
}

int main(void) {
    alarm(15); /* A broken handshake should fail CI rather than spin forever. */
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        check_session(MODE_PS1, USB_MODE_MASS, false);
        check_session(MODE_PS1, USB_MODE_MASS, true);
        check_session(MODE_PS2, USB_MODE_UART, false);
        check_session(MODE_PS1, USB_MODE_MCA, false);
        check_session(MODE_PS1, USB_MODE_UART, false);
    }

    unsigned starts_before = started, deinits_before = deinitialized;
    pthread_t thread;
    assert(pthread_create(&thread, NULL, worker, NULL) == 0);
    ps1_memory_card_stop();
    assert(started == starts_before && deinitialized == deinits_before + 1);
    assert(pthread_join(thread, NULL) == 0);

    /* EXIT must unwind a command without consuming fabricated input or
       committing a write, even while SEL is still active. */
    ps1_card_backend = &backend;
    ps1_card_active = true;
    receive_status = PS1_RECEIVE_EXIT;
    mc_cmd_write();
    assert(writes == 0 && responses == 1);
    alarm(0);
    puts("PS1 worker lifecycle and command exit tests passed");
    return 0;
}
