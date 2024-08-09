#include "ps2/card_emu/ps2_sd2psxman.h"

#include <settings.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "debug.h"
#if WITH_GUI
#include "gui.h"
#endif
#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2/card_emu/ps2_sd2psxman_commands.h"
#include "ps2/ps2_cardman.h"

#include "game_db/game_db.h"

#include "pico/platform.h"

volatile uint8_t sd2psxman_cmd;
volatile uint8_t sd2psxman_mode;
volatile uint16_t sd2psxman_cnum;
char sd2psxman_gameid[251] = {0x00};

void ps2_sd2psxman_task(void) {
    if ((sd2psxman_cmd != 0) && (!ps2_mc_data_interface_write_occured())) {

        switch (sd2psxman_cmd) {
            case SD2PSXMAN_SET_CARD:
                if (sd2psxman_mode == SD2PSXMAN_MODE_NUM) {
                    ps2_cardman_set_idx(sd2psxman_cnum);
                    DPRINTF("set num idx\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_NEXT) {
                    ps2_cardman_next_idx();
                    DPRINTF("set next idx\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_PREV) {
                    ps2_cardman_prev_idx();
                    DPRINTF("set prev idx\n");
                }
                break;

            case SD2PSXMAN_SET_CHANNEL:
                if (sd2psxman_mode == SD2PSXMAN_MODE_NUM) {
                    ps2_cardman_set_channel(sd2psxman_cnum);
                    DPRINTF("set num channel\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_NEXT) {
                    ps2_cardman_next_channel();
                    DPRINTF("set next channel\n");
                } else if (sd2psxman_mode == SD2PSXMAN_MODE_PREV) {
                    ps2_cardman_prev_channel();
                    DPRINTF("set prev channel\n");
                }
                break;

            case SD2PSXMAN_SET_GAMEID: 
            {
                if (MODE_PS1 == game_db_update_game(sd2psxman_gameid))
                    settings_set_mode(MODE_TEMP_PS1);
                else
                    ps2_cardman_set_gameid(sd2psxman_gameid); 
                DPRINTF("%s: set game id\n", __func__);
                break;
            }
            case SD2PSXMAN_UNMOUNT_BOOTCARD:
                if (ps2_cardman_get_idx() == 0) {
                    ps2_cardman_next_idx();
                }
                break;

            default: break;
        }

        if (ps2_cardman_needs_update()) {
            // close old card
            ps2_memory_card_exit();
            ps2_cardman_close();

            // open new card
#if WITH_GUI
            gui_do_ps2_card_switch();
            gui_request_refresh();
#else
            ps2_cardman_open();
            ps2_memory_card_enter();
#endif
        }

        sd2psxman_cmd = 0;
    }
}

void __time_critical_func(ps2_sd2psxman_set_gameid)(const char* const game_id) {
    if (strcmp(game_id, sd2psxman_gameid)) {
        snprintf(sd2psxman_gameid, sizeof(sd2psxman_gameid), "%s", game_id);
        sd2psxman_cmd = SD2PSXMAN_SET_GAMEID;
    }
}

const char* ps2_sd2psxman_get_gameid(void) {
    return sd2psxman_gameid;
}