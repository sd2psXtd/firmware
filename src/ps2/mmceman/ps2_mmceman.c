#include "ps2_mmceman.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "card_emu/ps2_mc_internal.h"
#include "debug.h"
#include "hardware/timer.h"
#include "history_tracker/ps2_history_tracker.h"
#include "pico/time.h"

#if WITH_GUI
#include "gui.h"
#endif

#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2_mmceman_commands.h"
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

void (*mmceman_callback)(void);
int mmceman_transfer_stage = 0;

volatile bool mmceman_tx_queued;
volatile uint8_t mmceman_tx_byte;

volatile uint8_t mmceman_mcman_retry_counter;
volatile bool mmceman_op_in_progress = false;
volatile bool mmceman_timeout_detected = false;
volatile bool mmceman_fs_abort_read = false;

volatile uint8_t mmceman_cmd;
volatile uint8_t mmceman_mode;
volatile uint16_t mmceman_cnum;

char mmceman_gameid[251] = {0x00};
static uint64_t mmceman_switching_timeout = 0;

void ps2_mmceman_task(void) {
    if ((mmceman_cmd != 0) && (!ps2_mc_data_interface_write_occured())) {

        switch (mmceman_cmd) {
            case MMCEMAN_SET_CARD:
                if (mmceman_mode == MMCEMAN_MODE_NUM) {
                    ps2_cardman_set_idx(mmceman_cnum);
                    log(LOG_INFO, "set num idx\n");
                } else if (mmceman_mode == MMCEMAN_MODE_NEXT) {
                    ps2_cardman_next_idx();
                    log(LOG_INFO, "set next idx\n");
                } else if (mmceman_mode == MMCEMAN_MODE_PREV) {
                    ps2_cardman_prev_idx();
                    log(LOG_INFO, "set prev idx\n");
                }
                break;

            case MMCEMAN_SET_CHANNEL:
                if (mmceman_mode == MMCEMAN_MODE_NUM) {
                    ps2_cardman_set_channel(mmceman_cnum);
                    log(LOG_INFO, "set num channel\n");
                } else if (mmceman_mode == MMCEMAN_MODE_NEXT) {
                    ps2_cardman_next_channel();
                    log(LOG_INFO, "set next channel\n");
                } else if (mmceman_mode == MMCEMAN_MODE_PREV) {
                    ps2_cardman_prev_channel();
                    log(LOG_INFO, "set prev channel\n");
                }
                break;

            case MMCEMAN_SET_GAMEID:
            {
                if (MODE_PS1 == game_db_update_game(mmceman_gameid))
                    settings_set_mode(MODE_TEMP_PS1);
                else
                    ps2_cardman_set_gameid(mmceman_gameid);
                    log(LOG_INFO, "%s: set game id\n", __func__);
                break;
            }

            //TEMP:
            case MMCEMAN_SWITCH_BOOTCARD:
                ps2_cardman_switch_bootcard();
            break;

            case MMCEMAN_UNMOUNT_BOOTCARD:
                if (ps2_cardman_get_idx() == 0) {
                    ps2_cardman_next_idx();
                }
                break;

            default: break;
        }

        mmceman_cmd = 0;
    }

    if (ps2_cardman_needs_update()
        && (mmceman_switching_timeout < time_us_64())
        && !input_is_any_down()
        && !mmceman_op_in_progress) {

        log(LOG_INFO, "%s Switching card now\n", __func__);
        uint32_t switching_time = time_us_32();

        ps2_history_tracker_init();

        // close old card
        ps2_memory_card_exit();
        log(LOG_TRACE, "%s After Exit\n", __func__);
        ps2_mc_data_interface_flush();
        ps2_cardman_close();
        log(LOG_TRACE, "%s After Close\n", __func__);

        sleep_ms(500);
#if WITH_GUI
        gui_do_ps2_card_switch();
        log(LOG_TRACE, "%s After GUI\n", __func__);
        gui_request_refresh();
        log(LOG_TRACE, "%s After Refresh\n", __func__);
#endif
        /* Set retry counter to stop the sd2psx from
         * responding to the next 5 requests from mcman.
         * This causes mmcman to clear it's cache and invalidate
         * handles, preventing a cache flush bug from causing corruption */
        mmceman_mcman_retry_counter = 5;

        // open new card
        ps2_cardman_open();
        ps2_memory_card_enter();

        log(LOG_INFO, "%s Card switch took %u ms\n", __func__, (time_us_32() - switching_time)/1000U);
    }
}

void ps2_mmceman_set_cb(void (*cb)(void))
{
    mmceman_callback = cb;
}

void __time_critical_func(ps2_mmceman_queue_tx)(uint8_t byte)
{
    mmceman_tx_queued = true;
    mmceman_tx_byte = byte;
}

bool __time_critical_func(ps2_mmceman_set_gameid)(const uint8_t* const game_id) {
    char sanitized_game_id[11] = {0};
    bool ret = false;
    game_db_extract_title_id(game_id, sanitized_game_id, 16, sizeof(sanitized_game_id));
    log(LOG_INFO, "Game ID: %s\n", sanitized_game_id);
    if (game_db_sanity_check_title_id(sanitized_game_id)) {
        snprintf(mmceman_gameid, sizeof(mmceman_gameid), "%s", sanitized_game_id);
        mmceman_switching_timeout = 0U;
        mmceman_cmd = MMCEMAN_SET_GAMEID;
        ret = true;
    }
    return ret;
}

const char* ps2_mmceman_get_gameid(void) {
    if (ps2_cardman_is_accessible())
        return mmceman_gameid;
    else
        return NULL;
}

void ps2_mmceman_next_ch(bool delay) {
    mmceman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmceman_mode = MMCEMAN_MODE_NEXT;
    mmceman_cmd = MMCEMAN_SET_CHANNEL;
}

void ps2_mmceman_prev_ch(bool delay) {
    mmceman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmceman_mode = MMCEMAN_MODE_PREV;
    mmceman_cmd = MMCEMAN_SET_CHANNEL;
}

void ps2_mmceman_next_idx(bool delay) {
    mmceman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmceman_mode = MMCEMAN_MODE_NEXT;
    mmceman_cmd = MMCEMAN_SET_CARD;
}

void ps2_mmceman_prev_idx(bool delay) {
    mmceman_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmceman_mode = MMCEMAN_MODE_PREV;
    mmceman_cmd = MMCEMAN_SET_CARD;
}