#include "ps2/card_emu/ps2_sd2psxman.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "card_emu/ps2_mc_internal.h"
#include "debug.h"
#include "hardware/timer.h"
#include "history_tracker/ps2_history_tracker.h"
#include "mmce_fs/ps2_mmce_fs.h"
#include "pico/time.h"
#if WITH_GUI
#include "gui.h"
#endif
#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2/card_emu/ps2_sd2psxman_commands.h"
#include "ps2/ps2_cardman.h"

#include "game_db/game_db.h"
#include "input.h"
#include "settings.h"

#include "pico/platform.h"


#if LOG_LEVEL_PS2_S2M == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_S2M, level, fmt, ##x)
#endif


volatile uint8_t sd2psxman_cmd;
volatile uint8_t sd2psxman_mode;
volatile uint16_t sd2psxman_cnum;
char sd2psxman_gameid[251] = {0x00};
static uint64_t sd2psxman_switching_timeout = 0;

void ps2_sd2psxman_task(void) {
    if ((sd2psxman_cmd != 0) && (!ps2_mc_data_interface_write_occured())) {

        switch (sd2psxman_cmd) {
            case SD2PSXMAN_SET_CARD:
                if (sd2psxman_mode == SD2PSXMAN_MODE_NUM) {
                    ps2_cardman_set_idx(sd2psxman_cnum);
                    log(LOG_INFO, "set num idx\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_NEXT) {
                    ps2_cardman_next_idx();
                    log(LOG_INFO, "set next idx\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_PREV) {
                    ps2_cardman_prev_idx();
                    log(LOG_INFO, "set prev idx\n");
                }
                break;

            case SD2PSXMAN_SET_CHANNEL:
                if (sd2psxman_mode == SD2PSXMAN_MODE_NUM) {
                    ps2_cardman_set_channel(sd2psxman_cnum);
                    log(LOG_INFO, "set num channel\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_NEXT) {
                    ps2_cardman_next_channel();
                    log(LOG_INFO, "set next channel\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_PREV) {
                    ps2_cardman_prev_channel();
                    log(LOG_INFO, "set prev channel\n");
                }
                break;

            case SD2PSXMAN_SET_GAMEID: 
            {
                if (MODE_PS1 == game_db_update_game(sd2psxman_gameid))
                    settings_set_mode(MODE_TEMP_PS1);
                else
                    ps2_cardman_set_gameid(sd2psxman_gameid); 
                    log(LOG_INFO, "%s: set game id\n", __func__);
                break;
            }
            case SD2PSXMAN_UNMOUNT_BOOTCARD:
                if (ps2_cardman_get_idx() == 0) {
                    ps2_cardman_next_idx();
                }
                break;

            default: break;
        }

        sd2psxman_cmd = 0;
    }

    if (ps2_cardman_needs_update() 
        && (sd2psxman_switching_timeout < time_us_64())
        && !input_is_any_down()
        && !op_in_progress) {
        log(LOG_INFO, "%s Switching card now\n", __func__);

        ps2_history_tracker_init();
        // close old card
        ps2_memory_card_exit();
        log(LOG_TRACE, "%s After Exit\n", __func__);
        ps2_cardman_close();
        log(LOG_TRACE, "%s After Close\n", __func__);
        ps2_mmce_fs_init();

        sleep_ms(500);
#if WITH_GUI
        gui_do_ps2_card_switch();
        log(LOG_TRACE, "%s After GUI\n", __func__);
        gui_request_refresh();
        log(LOG_TRACE, "%s After Refresh\n", __func__);
#endif
        // open new card
        ps2_cardman_open();
        ps2_memory_card_enter();
    }
}

void __time_critical_func(ps2_sd2psxman_set_gameid)(const char* const game_id) {
    if (strcmp(game_id, sd2psxman_gameid)) {
        snprintf(sd2psxman_gameid, sizeof(sd2psxman_gameid), "%s", game_id);
        sd2psxman_switching_timeout = 0U;
        sd2psxman_cmd = SD2PSXMAN_SET_GAMEID;
    }
}

const char* ps2_sd2psxman_get_gameid(void) {
    return sd2psxman_gameid;
}

void ps2_sd2psxman_next_ch(bool delay) {
    sd2psxman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    sd2psxman_mode = SD2PSXMAN_MODE_NEXT;
    sd2psxman_cmd = SD2PSXMAN_SET_CHANNEL;
}

void ps2_sd2psxman_prev_ch(bool delay) {
    sd2psxman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    sd2psxman_mode = SD2PSXMAN_MODE_PREV;
    sd2psxman_cmd = SD2PSXMAN_SET_CHANNEL;
}

void ps2_sd2psxman_next_idx(bool delay) {
    sd2psxman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    sd2psxman_mode = SD2PSXMAN_MODE_NEXT;
    sd2psxman_cmd = SD2PSXMAN_SET_CARD;
}

void ps2_sd2psxman_prev_idx(bool delay) {
    sd2psxman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    sd2psxman_mode = SD2PSXMAN_MODE_PREV;
    sd2psxman_cmd = SD2PSXMAN_SET_CARD;
}
