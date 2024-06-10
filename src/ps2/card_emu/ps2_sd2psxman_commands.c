#include <stdint.h>
#include <string.h>

#include "ps2_cardman.h"
#include "ps2_mc_internal.h"

#include "ps2_sd2psxman.h"
#include "ps2_sd2psxman_commands.h"

#include "game_names/game_names.h"

#include "debug.h"

#include <sd.h>
#include <sys/_default_fcntl.h>


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

    game_names_extract_title_id(received_id, sanitized_game_id, gameid_len, sizeof(sanitized_game_id));
    if (game_names_sanity_check_title_id(sanitized_game_id)) {
        ps2_sd2psxman_set_gameid(sanitized_game_id);
        sd2psxman_cmd = SD2PSXMAN_SET_GAMEID;
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

#include "file_handling/ps2_file_handling.h"

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_set_path)(void)
{
    uint8_t cmd;
    uint8_t idx = 0;
    ps2_file_handling_operation_t op = {
        .content.buff = { 0 },
        .flag = O_RDONLY,
        .handle = -1,
        .size_remaining = 0,
        .size_used = 0,
        .type = OP_NONE,
        .position = -1
    };

    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    op.type = (cmd == 0U) ? OP_FILE : OP_DIR;
    do {
        mc_respond(0x0); receiveOrNextCmd(&cmd);
        op.content.string[idx++] = cmd;
    } while (cmd != 0x00);
    mc_respond(0x0); receiveOrNextCmd(&cmd); // String finished
    ps2_file_handling_set_operation(&op);
    mc_respond(term);
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_open_file)(void)
{
    uint8_t cmd;
    int handle;
    uint8_t mode = 0U;    
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&mode); //mode 
    handle = ps2_file_handling_open_file(mode);
    mc_respond(handle < 0 ? 0xFF : handle); receiveOrNextCmd(&cmd); // Send file handle
    mc_respond(term);

}


inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_close_file)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //handle
    ps2_file_handling_close_file(cmd);
    mc_respond(term);
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_seek_file)(void)
{
    uint8_t cmd;
    uint8_t handle;
    uint32_t pos = 0;
    uint8_t* pos_u8p = (uint8_t*)&pos;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&handle); //handle
    mc_respond(0x0); receiveOrNextCmd(&pos_u8p[3]); // MSB
    mc_respond(0x0); receiveOrNextCmd(&pos_u8p[2]); // MSB - 1
    mc_respond(0x0); receiveOrNextCmd(&pos_u8p[1]); // MSB - 2
    mc_respond(0x0); receiveOrNextCmd(&pos_u8p[0]); // LSB
    mc_respond(term);
    ps2_file_handling_seek(handle, pos);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_tell_file)(void)
{
    uint8_t cmd;
    uint8_t handle;
    uint32_t pos = 0;
    uint8_t* pos_u8p = (uint8_t*)&pos;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&handle); //handle
    mc_respond(0x0); receiveOrNextCmd(&cmd);
    pos = ps2_file_handling_tell(handle);
    mc_respond(pos_u8p[3]); receiveOrNextCmd(&cmd); 
    mc_respond(pos_u8p[2]); receiveOrNextCmd(&cmd); 
    mc_respond(pos_u8p[1]); receiveOrNextCmd(&cmd); 
    mc_respond(pos_u8p[0]); receiveOrNextCmd(&cmd);
    mc_respond(term);
    ps2_file_handling_seek(handle, pos);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_setup_transaction)(void) {
    uint8_t cmd;
    uint8_t handle;
    uint32_t length = 0U;
    uint8_t mode = 0;
    uint8_t* length_u8p = (uint8_t*)&length;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&handle); //handle
    mc_respond(0x0); receiveOrNextCmd(&length_u8p[3]); // MSB
    mc_respond(0x0); receiveOrNextCmd(&length_u8p[2]); // MSB - 1
    mc_respond(0x0); receiveOrNextCmd(&length_u8p[1]); // MSB - 2
    mc_respond(0x0); receiveOrNextCmd(&length_u8p[0]); // LSB
    mc_respond(0x0); receiveOrNextCmd(&mode); // mode
    mc_respond(term);
    ps2_file_handling_requestTransaction(handle, length, (mode == 0));
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_read_file)(void) {
    uint8_t cmd;
    uint8_t idx = 0;
    uint8_t bytes_read = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte (addr 0x02)
    ps2_file_handling_operation_t *op = ps2_file_handling_get_operation(true);

    bytes_read = (uint8_t)op->size_used;
    
    mc_respond((uint8_t) (bytes_read <= CHUNK_SIZE ?  bytes_read : 0xFF)); //receiveOrNextCmd(&cmd); // bytes read;
    for (idx = 0U; idx < bytes_read; idx++) { // starting at 0x04
        mc_respond(op->content.buff[idx]); //receiveOrNextCmd(&cmd);
    }
    ps2_file_handling_continue_read();
    for (; idx < CHUNK_SIZE; idx++) { // end addr 250 + 4
        mc_respond(0x00); //receiveOrNextCmd(&cmd); // padding;
    }

    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_write_file)(void) {
    uint8_t cmd;
    uint8_t idx;
    ps2_file_handling_operation_t *op = ps2_file_handling_get_operation(false);
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    op->size_used = op->size_remaining < CHUNK_SIZE ? op->size_remaining : CHUNK_SIZE;

    for (idx = 0U; idx < op->size_used; idx++) {
        mc_respond(0x00); receiveOrNextCmd(&op->content.buff[idx]);
    }
    for (; idx < 251U; idx++) {
        mc_respond(0x00); receiveOrNextCmd(&cmd); // padding;
    }
    mc_respond((uint8_t) op->size_used); receiveOrNextCmd(&cmd); // padding;
    ps2_file_handling_flush_buffer();
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_delete_file)(void) {
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    ps2_file_handling_delete();
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_read_stat)(void) {
    uint8_t cmd;
    ps2_file_handling_stat_t stat = { };
    ps2_file_handling_stat(&stat);
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    uint8_t* u8ptr = (uint8_t*)&stat.mode;
    mc_respond(u8ptr[1]);
    mc_respond(u8ptr[0]);
    u8ptr = (uint8_t*)&stat.attr;
    mc_respond(u8ptr[1]);
    mc_respond(u8ptr[0]);
    for (int i = 0; i < 8; i++) {
        mc_respond(stat.ctime[i]);
    }
    for (int i = 0; i < 8; i++) {
        mc_respond(stat.mtime[i]);
    }
    for (int i = 0; i < 8; i++) {
        mc_respond(stat.atime[i]);
    }
    u8ptr = (uint8_t*)&stat.size;
    mc_respond(u8ptr[3]);
    mc_respond(u8ptr[2]);
    mc_respond(u8ptr[1]);
    mc_respond(u8ptr[0]);
    mc_respond(stat.exists);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_sd2psxman_cmds_read_dir_entry)(void) {
    uint8_t cmd;
    ps2_file_handling_dirent_t dirent = {
        .handle = -1,
        .isFile = false,
        .name = {0}
    };
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //index / dir handle

    ps2_file_handling_getDirEnt(&dirent);
    mc_respond(dirent.isFile ? 0 : 1); receiveOrNextCmd(&cmd); //return type

    for (uint8_t idx = 0U; idx < 249; idx++) {
        mc_respond(dirent.name[idx]); receiveOrNextCmd(&cmd); //entry
    };

    mc_respond(term);
}