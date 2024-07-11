#include <string.h>

#include "ps2_cardman.h"
#include "ps2_mc_internal.h"

#include "ps2_sd2psxman.h"
#include "ps2_sd2psxman_commands.h"

#include "game_db/game_db.h"

#include "debug.h"

//#define DEBUG_SD2PSXMAN_PROTOCOL

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_ping)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x1); receiveOrNextCmd(&cmd); //protocol version
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product ID
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product revision number
    mc_respond(term);
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_PING\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_get_status)(void)
{
    uint8_t cmd;
    //TODO
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_GET_STATUS\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_get_card)(void)
{
    uint8_t cmd;
    int card = ps2_cardman_get_idx();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(card >> 8);   receiveOrNextCmd(&cmd); //card upper 8 bits
    mc_respond(card & 0xff); receiveOrNextCmd(&cmd); //card lower 8 bits
    mc_respond(term);
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_GET_CARD\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_set_card)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //mode
    sd2psxman_mode = cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //card upper 8 bits
    sd2psxman_cnum = cmd << 8;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //card lower 8 bits
    sd2psxman_cnum |= cmd;
    mc_respond(term);
    
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_SET_CARD mode: %i, num: %i\n", sd2psxman_mode, sd2psxman_cnum);
#endif
    
    sd2psxman_cmd = SD2PSXMAN_SET_CARD;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_get_channel)(void)
{
    uint8_t cmd;
    int chan = ps2_cardman_get_channel();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(chan >> 8);   receiveOrNextCmd(&cmd); //channel upper 8 bits
    mc_respond(chan & 0xff); receiveOrNextCmd(&cmd); //channel lower 8 bits
    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_GET_CHANNEL\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_set_channel)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //mode
    sd2psxman_mode = cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //channel upper 8 bits
    sd2psxman_cnum = cmd << 8;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //channel lower 8 bits
    sd2psxman_cnum |= cmd;
    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_SET_CHANNEL mode: %i, num: %i\n", sd2psxman_mode, sd2psxman_cnum);
#endif

    sd2psxman_cmd = SD2PSXMAN_SET_CHANNEL;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_get_gameid)(void)
{
    uint8_t cmd;
    uint8_t gameid_len = strlen(sd2psxman_gameid) + 1; //+1 null terminator
    mc_respond(0x0);        receiveOrNextCmd(&cmd);    //reserved byte
    mc_respond(gameid_len); receiveOrNextCmd(&cmd);    //gameid length

    for (int i = 0; i < gameid_len; i++) {
        mc_respond(sd2psxman_gameid[i]); receiveOrNextCmd(&cmd); //gameid
    }

    for (int i = 0; i < (250 - gameid_len); i++) {
        mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
    }

    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_GET_GAMEID\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_set_gameid)(void)
{
    uint8_t cmd;
    uint8_t gameid_len;
    uint8_t received_id[252] = { 0 };
    char sanitized_game_id[11] = { 0 };
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //gameid length
    gameid_len = cmd;

    for (int i = 0; i < gameid_len; i++) {
        mc_respond(0x0);    receiveOrNextCmd(&cmd); //gameid
        received_id[i] = cmd;
    }

    mc_respond(term);

    game_db_extract_title_id(received_id, sanitized_game_id, gameid_len, sizeof(sanitized_game_id));
    if (game_db_sanity_check_title_id(sanitized_game_id)) {
        ps2_sd2psxman_set_gameid(sanitized_game_id);
    }

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_SET_GAMEID len %i, id: %s\n", gameid_len, sanitized_game_id);
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_unmount_bootcard)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_UNMOUNT_BOOTCARD\n");
#endif

    sd2psxman_cmd = SD2PSXMAN_UNMOUNT_BOOTCARD;
}