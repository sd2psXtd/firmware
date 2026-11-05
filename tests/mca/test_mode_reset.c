#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ps1/ps1_cardman.h"
#include "ps1/ps1_mmce.h"
#include "settings.h"
#include "wear_leveling/wear_leveling.h"
#include "sd.h"

static uint8_t saved_settings[24];
static bool mca_profile;
static unsigned default_card_switches;

void fatal(int error, const char *format, ...) { (void)error; (void)format; }
int multicore_lockout_victim_is_initialized(unsigned int core) { (void)core; return 0; }
void multicore_lockout_start_blocking(void) {}
void multicore_lockout_end_blocking(void) {}
wear_leveling_status_t wear_leveling_write(uint32_t address, const void *value, size_t length) {
    (void)address; (void)value; (void)length; return WEAR_LEVELING_SUCCESS;
}
int sd_exists(const char *path) { (void)path; return 1; }
int sd_mkdir(const char *path) { (void)path; return 0; }
int sd_open(const char *path, int flags) { (void)path; (void)flags; return 1; }
int sd_close(int fd) { (void)fd; return 0; }
int sd_write(int fd, void *value, size_t length) { (void)fd; (void)value; return (int)length; }

/* settings_init reads the same persisted structure used by settings.c. */
wear_leveling_status_t wear_leveling_init(void) { return WEAR_LEVELING_SUCCESS; }
wear_leveling_status_t wear_leveling_read(uint32_t address, void *value, size_t length) {
    assert(address == 0 && length == sizeof(saved_settings));
    memcpy(value, saved_settings, length);
    return WEAR_LEVELING_SUCCESS;
}

bool mca_device_profile_is_mca(void) { return mca_profile; }
bool mca_protocol_idle(void) { return true; }
void mca_protocol_set_ready(bool ready) { (void)ready; }
void mca_protocol_switch_delay(uint32_t milliseconds) { (void)milliseconds; }

uint64_t time_us_64(void) { return 100; }
void sleep_ms(uint32_t milliseconds) { (void)milliseconds; }
int input_is_any_down(void) { return 0; }
bool ps1_cardman_needs_update(void) { return false; }
void ps1_cardman_switch_default(void) { default_card_switches++; }
int game_db_get_current_parent(char *id) { (void)id; return 0; }
int game_db_update_game(const char *id) { (void)id; return 0; }
void ps1_cardman_next_idx(void) {}
void ps1_cardman_prev_idx(void) {}
void ps1_cardman_next_channel(void) {}
void ps1_cardman_prev_channel(void) {}
void ps1_cardman_switch_bootcard(void) {}
void ps1_cardman_set_idx(int index) { (void)index; }
void ps1_cardman_set_channel(int channel) { (void)channel; }
void ps1_cardman_set_game_id(const char *id) { (void)id; }
void ps1_cardman_close(void) {}
void ps1_cardman_open(void) {}
void ps1_memory_card_exit(void) {}
void ps1_memory_card_enter(void) {}
void ps1_mc_data_interface_flush(void) {}

static void load_persistent_mode(int mode) {
    memset(saved_settings, 0, sizeof(saved_settings));
    /* SETTINGS_VERSION_MAGIC in settings.c; sys_flags is byte 14. */
    saved_settings[0] = 0x07;
    saved_settings[1] = 0x00;
    saved_settings[2] = 0xCD;
    saved_settings[3] = 0xAA;
    saved_settings[14] = (uint8_t)mode;
    settings_init();
}

static void reset_with_profile(bool mca) {
    mca_profile = mca;
    ps1_mmce_reset(false);
    ps1_mmce_task();
}

int main(void) {
    load_persistent_mode(MODE_PS2);
    settings_set_mode(MODE_TEMP_PS1);
    assert(settings_get_mode(true) == MODE_PS1);
    reset_with_profile(true);
    assert(settings_get_mode(false) == MODE_PS2);
    assert(settings_get_mode(true) == MODE_PS2);

    load_persistent_mode(MODE_PS2);
    settings_set_mode(MODE_TEMP_PS1);
    reset_with_profile(false);
    assert(settings_get_mode(false) == MODE_PS2);
    assert(settings_get_mode(true) == MODE_PS1);

    load_persistent_mode(MODE_PS1);
    unsigned before = default_card_switches;
    reset_with_profile(true);
    assert(default_card_switches == before + 1);
    assert(settings_get_mode(false) == MODE_PS1);
    assert(settings_get_mode(true) == MODE_PS1);

    puts("mode reset regression passed");
    return 0;
}
