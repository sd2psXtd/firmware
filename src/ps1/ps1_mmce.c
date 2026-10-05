
#include <input.h>
#include "pico/time.h"
#include "ps1_mc_data_interface.h"
#include "ps1_memory_card.h"
#include "ps1_cardman.h"
#include "ps1_mmce.h"
#include "ps1_mmce_fs.h"
#include "debug.h"
#include "game_db/game_db.h"
#include "settings.h"

#if WITH_GUI
#include <gui.h>
#endif
#include <string.h>

#define CARD_SWITCH_DELAY_MS    (250)
#define MAX_GAME_ID_LENGTH   (16)

#if LOG_LEVEL_PS1_MMCE == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS1_MMCE, level, fmt, ##x)
#endif
static volatile uint8_t mmce_command;
static volatile uint16_t mmce_cnum;
static volatile uint16_t mmce_chn;
static uint64_t mmce_switching_timeout = 0;

static char received_game_id[MAX_GAME_ID_LENGTH];


void ps1_mmce_task(void) {
    if (mmce_command != 0U) {

        switch (mmce_command) {
            case MMCE_PS1_GAME_ID: {
                DPRINTF("Received Game ID: %s\n", received_game_id);
                game_db_update_game(received_game_id);
                game_db_get_current_parent(received_game_id);
                ps1_cardman_set_game_id(received_game_id);
                break;
            }
            case MMCE_PS1_NXT_CARD:
                DPRINTF("Received next card.\n");
                ps1_cardman_next_idx();
                break;
            case MMCE_PS1_PRV_CARD:
                DPRINTF("Received prev card.\n");
                ps1_cardman_prev_idx();
                break;
            case MMCE_PS1_NXT_CH:
                DPRINTF("Received next chan.\n");
                ps1_cardman_next_channel();
                break;
            case MMCE_PS1_PRV_CH:
                DPRINTF("Received prev chan.\n");
                ps1_cardman_prev_channel();
                break;
            case MMCE_PS1_SWITCH_BOOTCARD:
                DPRINTF("Received switch boot card.\n");
                ps1_cardman_switch_bootcard();
                break;
            case MMCE_PS1_SWITCH_DEFAULT:
                DPRINTF("Received switch default card.\n");
                ps1_cardman_switch_default();
                break;
            case MMCE_PS1_SET_CARD:
                DPRINTF("Received set card index: %d\n", mmce_cnum);
                ps1_cardman_set_idx(mmce_cnum);
                break;
            case MMCE_PS1_SET_CHANNEL:
                DPRINTF("Received set channel index: %d\n", mmce_chn);
                ps1_cardman_set_channel(mmce_chn);
                break;
            case MMCE_PS1_RESET:
                DPRINTF("Received reset command.\n");
                if (settings_get_mode(false) == MODE_PS2) {
                    settings_set_mode(MODE_PS2);
                } else {
                    ps1_cardman_switch_default();
                }
                break;
            default:
                DPRINTF("Invalid ODE Command received.");
                break;
        }

        mmce_command = 0;
    }

    if ((mmce_switching_timeout < time_us_64())
        && !input_is_any_down()
        && ps1_mmce_fs_idle()
        && (ps1_cardman_needs_update())) {

        ps1_memory_card_exit();
        ps1_mc_data_interface_flush();
        ps1_mmce_fs_reset();
        ps1_cardman_close();
#ifdef WITH_GUI
        gui_do_ps1_card_switch();
#endif

        sleep_ms(CARD_SWITCH_DELAY_MS); // This delay is required, so ODE can register the card change

        ps1_cardman_open();
        ps1_memory_card_enter();
#ifdef WITH_GUI
        gui_request_refresh();
#endif
    }
}


bool __time_critical_func(ps1_mmce_set_gameid)(const uint8_t* const game_id) {
    char sanitized_game_id[11] = {0};
    bool ret = false;
    log(LOG_INFO, "Raw Game ID: %s\n", game_id);

    game_db_extract_title_id(game_id, sanitized_game_id, UINT8_MAX, sizeof(sanitized_game_id));
    log(LOG_INFO, "Game ID: %s\n", sanitized_game_id);
    if (game_db_sanity_check_title_id(sanitized_game_id)) {
        snprintf(received_game_id, sizeof(received_game_id), "%s", sanitized_game_id);
        mmce_command = MMCE_PS1_GAME_ID;
        ret = true;
    } else if ((game_id[0] != 0x00) && (ps1_cardman_get_idx() == PS1_CARD_IDX_SPECIAL)) {
        mmce_command = MMCE_PS1_SWITCH_DEFAULT;
    }
    return ret;
}

const char* ps1_mmce_get_gameid(void) {
    return received_game_id;
}

void ps1_mmce_next_ch(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_NXT_CH;
}

void ps1_mmce_prev_ch(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_PRV_CH;
}

void ps1_mmce_next_idx(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_NXT_CARD;
}

void ps1_mmce_prev_idx(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_PRV_CARD;
}

void ps1_mmce_switch_bootcard(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_SWITCH_BOOTCARD;
}

void ps1_mmce_set_card(uint16_t cnum, bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_cnum = cnum;
    mmce_command = MMCE_PS1_SET_CARD;
}

void ps1_mmce_set_channel(uint16_t chn, bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_chn = chn;
    mmce_command = MMCE_PS1_SET_CHANNEL;
}

void ps1_mmce_reset(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MMCE_PS1_RESET;
}
