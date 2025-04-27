
#include <input.h>
#include "pico/time.h"
#include "ps1_mc_data_interface.h"
#include "ps1_memory_card.h"
#include "ps1_cardman.h"
#include "ps1_mmce.h"
#include "debug.h"
#include "game_db/game_db.h"

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
static uint64_t mmce_switching_timeout = 0;

static char received_game_id[MAX_GAME_ID_LENGTH];


void ps1_mmce_task(void) {
    if (mmce_command != 0U) {

        switch (mmce_command) {
            case MCP_GAME_ID: {
                DPRINTF("Received Game ID: %s\n", received_game_id);
                game_db_update_game(received_game_id);
                ps1_cardman_set_game_id(received_game_id);
                break;
            }
            case MCP_NXT_CARD:
                DPRINTF("Received next card.\n");
                ps1_cardman_next_idx();
                break;
            case MCP_PRV_CARD:
                DPRINTF("Received prev card.\n");
                ps1_cardman_prev_idx();
                break;
            case MCP_NXT_CH:
                DPRINTF("Received next chan.\n");
                ps1_cardman_next_channel();
                break;
            case MCP_PRV_CH:
                DPRINTF("Received prev chan.\n");
                ps1_cardman_prev_channel();
                break;
            case MCP_SWITCH_BOOTCARD:
                DPRINTF("Received switch boot card.\n");
                ps1_cardman_switch_bootcard();
                break;
            default:
                DPRINTF("Invalid ODE Command received.");
                break;
        }

        mmce_command = 0;
    }

    if ((mmce_switching_timeout < time_us_64())
        && !input_is_any_down()
        && (ps1_cardman_needs_update())) {

        ps1_memory_card_exit();
        ps1_mc_data_interface_flush();
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
        mmce_command = MCP_GAME_ID;
        ret = true;
    }
    return ret;
}

const char* ps1_mmce_get_gameid(void) {
    return received_game_id;
}

void ps1_mmce_next_ch(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MCP_NXT_CH;
}

void ps1_mmce_prev_ch(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MCP_PRV_CH;
}

void ps1_mmce_next_idx(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MCP_NXT_CARD;
}

void ps1_mmce_prev_idx(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MCP_PRV_CARD;
}

void ps1_mmce_switch_bootcard(bool delay) {
    mmce_switching_timeout = time_us_64() + (delay ? 1500 * 1000 : 0);
    mmce_command = MCP_SWITCH_BOOTCARD;
}
