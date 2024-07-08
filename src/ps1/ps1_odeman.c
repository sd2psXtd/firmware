
#include "pico/time.h"
#include "ps1_memory_card.h"
#include "ps1_cardman.h"
#include "ps1_odeman.h"
#include "debug.h"

#include <game_names/game_names.h>
#include <gui.h>
#include <string.h>

#define CARD_SWITCH_DELAY_MS    (250)
#define MAX_GAME_ID_LENGTH   (16)

void ps1_odeman_task(void) {
    uint8_t ode_command = ps1_memory_card_get_ode_command();

    if (ode_command != 0U) {
        ps1_memory_card_reset_ode_command();
        ps1_memory_card_exit();
        ps1_cardman_close();

        switch (ode_command) {
            case MCP_GAME_ID: {
                const char *game_id;
                game_id = ps1_memory_card_get_game_id();
                debug_printf("Received Game ID: %s\n", game_id);
                ps1_cardman_set_ode_idx();
                break;
            }
            case MCP_NXT_CARD:
                debug_printf("Received next card.\n");
                ps1_cardman_next_idx();
                break;
            case MCP_PRV_CARD:
                debug_printf("Received prev card.\n");
                ps1_cardman_prev_idx();
                break;
            case MCP_NXT_CH:
                debug_printf("Received next chan.\n");
                ps1_cardman_next_channel();
                break;
            case MCP_PRV_CH:
                debug_printf("Received prev chan.\n");
                ps1_cardman_prev_channel();
                break;
            default:
                debug_printf("Invalid ODE Command received.");
                break;
        }

        sleep_ms(CARD_SWITCH_DELAY_MS); // This delay is required, so ODE can register the card change

        ps1_cardman_open();
        ps1_memory_card_enter();
        gui_request_refresh();
    }
}
