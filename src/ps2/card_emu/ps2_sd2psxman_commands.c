#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ps2_cardman.h"
#include "ps2_memory_card.h"
#include "ps2_mc_internal.h"

#include "ps2_sd2psxman.h"
#include "ps2_sd2psxman_commands.h"

#include "game_names/game_names.h"

#include "debug.h"

#include <sd.h>
#include <sys/_default_fcntl.h>

#include "mmce_fs/ps2_mmce_fs.h"

#define DPRINTF(fmt, x...) printf(fmt, ##x)
//#define DPRINTF(x...) 

//TODO: temp global values, find them a home
static int transfer_stage = 0;
volatile ps2_mmce_fs_data_t *data = NULL;

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
inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_open)(void)
{
    uint8_t cmd;
    uint8_t packed_flags;

    int idx = 0;
    int ready = 0;

    switch(transfer_stage)
    {
        //Packet #1: Command and flags
        case 0:
            ps2_mmce_fs_wait_ready();         //Wait for file handling to be ready
            data = ps2_mmce_fs_get_data();    //Get pointer to mmce fs data

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&packed_flags);   //File flags
            
            data->flags  = (packed_flags & 2);          //O_RDONLY, O_WRONLY, O_RDWR
            data->flags |= (packed_flags & 8) << 5;     //O_APPEND
            data->flags |= (packed_flags & 0xE0) << 4;  //O_CREATE, O_TRUNC, O_EXCL

            //Jump to this function after /CS triggered reset
            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_open);

            transfer_stage = 1; //Update stage
            mc_respond(0xff);   //End transfer
        break;
        
        //Packet #2: Filename
        case 1:
            transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            DPRINTF("%s: name: %s flags: 0x%x\n", __func__, data->buffer, data->flags);
            
            //Signal op in core1 (ps2_mmce_fs_run)
            ps2_mmce_fs_signal_operation(MMCE_FS_OPEN); 
        break;

        //Packet #3 - n: Poll ready
        case 2:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 3;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;

        //Packet #n + 1: File descriptor and termination byte
        case 3:
            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->fd);  receiveOrNextCmd(&cmd);

            /* Hacky optimization: Start reading ahead when a file is opened
             * The read op will try to read length + CHUNK_SIZE, with a length
             * of zero, CHUNK_SIZE will be read ahead */
            //ps2_mmce_fs_signal_operation(MMCE_FS_READ);

            ps2_memory_card_set_cmd_callback(NULL); //Clear callback
            transfer_stage = 0; //Clear stage

            mc_respond(term);   //End transfer
        break;
    }    
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_close)(void)
{
    uint8_t cmd;
    int ready = 0;

    switch(transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();
            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_close);

            transfer_stage = 1;
            mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
            mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor

            DPRINTF("%s: fd: %i\n", __func__, data->fd);

            ps2_mmce_fs_signal_operation(MMCE_FS_CLOSE);
        break;

        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 2;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;


        //Packet #n + 1: Return value 
        case 2:
            receiveOrNextCmd(&cmd);
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            mc_respond(data->rv);   //Return value
            mc_respond(term);
        break;
    }
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_read)(void)
{
    uint8_t cmd;
    uint8_t *len8 = NULL;
    uint8_t *bytes8 = NULL;

    uint8_t last_byte;    
    uint8_t next_chunk;
    uint32_t bytes_left_in_packet;

    int seek_offset;

    switch(transfer_stage) {
        //Packet #1: File handle, length, and return value
        case 0:
            ps2_mmce_fs_wait_ready();           //Wait for file handling to be ready
            data = ps2_mmce_fs_get_data();      //Get pointer to data
            data->bytes_transferred = 0x0;
            
            len8 = (uint8_t*)&data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&data->fd);    //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);   //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);   //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);   //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);   //Len MSB - 3

            DPRINTF("%s: fd: %i, len %i\n", __func__, data->fd, data->length);

            if (sd_fd_is_open(data->fd) == -1) {
                DPRINTF("%s: bad fd: %i, abort\n", __func__, data->fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            data->bytes_read = 0;

            //If there's data read ahead
            if (data->bytes_read_ahead >= CHUNK_SIZE) {
                //Reading from different file
                if (data->fd != data->last_read_fd) {
                    //Roll back head_idx and invalidate read ahead chunk state
                    if (data->head_idx == 0)
                        data->head_idx = CHUNK_READ_AHEAD_COUNT;
                    else
                        data->head_idx--;

                    data->chunk_state[data->head_idx] = CHUNK_STATE_NOT_READY; 
                //Reading from same file
                } else {
                    //Correct over read, ie if we read 256 bytes but only need 192
                    if (data->length < CHUNK_SIZE) {
                        seek_offset = (data->bytes_read_ahead - data->length) * -1;
                        DPRINTF("Seeking back %i\n", seek_offset);
                        sd_seek_new(data->fd, seek_offset, 1);
                        data->bytes_read = data->length;
                    } else {
                        data->bytes_read = CHUNK_SIZE;
                    }
                }
            }

            //Start async continous read on core 1
            ps2_mmce_fs_signal_operation(MMCE_FS_READ);

            //Wait for first chunk to become available before ending this transfer (~2.5ms until timeout)
            while(data->chunk_state[data->tail_idx] != CHUNK_STATE_READY) {
                
                DPRINTF("C0: Waiting on: %i state:%i\n", data->tail_idx, data->chunk_state[data->tail_idx]);
                
                //Reading ahead failed to get requested data
                if (data->chunk_state[data->tail_idx] == CHUNK_STATE_INVALID) {
                    DPRINTF("Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                    mc_respond(0x1);    //Return 1
                    return;             //Abort
                }

                sleep_us(1);
            }

            //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
            ps2_queue_tx_byte_on_reset(data->buffer[data->tail_idx][0]);

            //Jump to this function after /CS triggered reset
            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_read);

            //Update transfer stage
            transfer_stage = 1;

            //End current transfer
            mc_respond(0x0);

            break;
        
        //Packet #2 - n: Raw data
        case 1:
            receiveOrNextCmd(&cmd); //Padding

            data->bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = data->length - data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            next_chunk = data->tail_idx + 1;
            if (next_chunk > CHUNK_READ_AHEAD_COUNT)
                next_chunk = 0;

            //If transfer was only 1 byte, skip this
            if (bytes_left_in_packet != 0) {

                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(data->buffer[data->tail_idx][i]);
                }

                //Check if there's more packets after this
                if ((bytes_left_in_packet + data->bytes_transferred) < data->length) {

                    //Wait for next chunk to be available before ending this transfer (~2.5ms until timeout)
                    while(data->chunk_state[next_chunk] != CHUNK_STATE_READY) {
                        
                        DPRINTF("C0: Waiting on: %i state:%i\n", next_chunk, data->chunk_state[next_chunk]);

                        //TODO: error handling
                        if (data->chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                            DPRINTF("Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                            transfer_stage = 0;
                            ps2_memory_card_set_cmd_callback(NULL);
                            return;
                        }
                        sleep_us(1);
                    }

                    //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                    ps2_queue_tx_byte_on_reset(data->buffer[next_chunk][0]);
                }

                //Grab this before updating idx and finish off the transfer
                last_byte = data->buffer[data->tail_idx][bytes_left_in_packet];
            }

            //Update transferred count
            data->bytes_transferred += bytes_left_in_packet;

            //Enter crit and mark chunk as consumed
            critical_section_enter_blocking(&mmce_fs_crit);
            data->chunk_state[data->tail_idx] = CHUNK_STATE_NOT_READY;
            critical_section_exit(&mmce_fs_crit);

            DPRINTF("C0: %i consumed, bytes in this packet: %i\n", data->tail_idx, (bytes_left_in_packet + 1));

            //Update tail idx
            data->tail_idx = next_chunk;

            //If there's no more packet's left after this, move to final transfer stage
            if (data->bytes_transferred == data->length)
                transfer_stage = 2;

            //Send last byte of packet and end current transfer
            if (bytes_left_in_packet != 0)
                mc_respond(last_byte);

            break;

        //Packet #3: Bytes read
        case 2:
            receiveOrNextCmd(&cmd); //Padding

            bytes8 = (uint8_t*)&data->bytes_transferred;

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd); //Bytes read
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            ps2_memory_card_set_cmd_callback(NULL);
            
            data->bytes_read = 0;
            data->length = 0;

            transfer_stage = 0;

            mc_respond(term);
            break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_write)(void)
{
    uint8_t cmd;
    uint8_t *len8 = NULL;
    uint8_t *bytes8 = NULL;

    //4096 bytes
    uint32_t bytes_left_in_packet;
    uint32_t next_chunk;

    uint8_t last_byte;
    uint32_t ready = 0;

    /* Currently this is a bit different from read. It waits for a full 4KB buffer to be full (or until len has been read)
    *  before starting the write to the sdcard. Once the write is in progress the PS2 will poll for completeltion or until
    *  a timeout is reached. Once the write is complete, the process will repeat if more data is left or send the final packet
    *  containing the number of bytes written. */
    switch(transfer_stage) {
        //Packet 1: File descriptor, length, and return value
        case 0:
            ps2_mmce_fs_wait_ready();          //Wait for file handling to be ready
            data = ps2_mmce_fs_get_data();     //Get pointer to data
            data->bytes_transferred = 0x0;

            len8 = (uint8_t*)&data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&data->fd);    //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);   //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);   //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);   //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);   //Len MSB - 3

            DPRINTF("%s: fd: %i, len %i\n", __func__, data->fd, data->length);

            if (sd_fd_is_open(data->fd) == -1) {
                DPRINTF("%s: bad fd: %i, abort\n", __func__, data->fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_write);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);

            ready = ps2_mmce_fs_is_ready();

            if (ready == 1) {
                if (data->bytes_transferred == data->length) {
                    transfer_stage = 3;  //Move to final transfer stage
                } else {
                    data->tail_idx = 0;  //Reset head and tail pointers
                    data->head_idx = 0;
                    transfer_stage = 2;  //More data to write
                }
            }
            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;

        //Packet #n + 1: Read bytes
        case 2:
            //Add first byte to buffer
            receiveOrNextCmd(&cmd);
            data->buffer[data->tail_idx][0] = cmd;
            data->bytes_transferred++;

            //Determine bytes left in this packet
            bytes_left_in_packet = data->length - data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1;

            //Avoid trying to read more data if write len == 1
            if (bytes_left_in_packet != 0) {
                DPRINTF("bytes left in packet: %i, bytes transferred: %i\n", bytes_left_in_packet, data->bytes_transferred);

                //Recieve rest of bytes
                for (int i = 1; i <= bytes_left_in_packet; i++) {
                    mc_respond(0x0);
                    receiveOrNextCmd(&data->buffer[data->tail_idx][i]);
                }

                //Update count
                data->bytes_transferred += bytes_left_in_packet;
            }

            //If bytes recieved == 4KB or bytes received == length
            if ((((data->bytes_transferred) % 4096) == 0) || (data->length == data->bytes_transferred)) {

                 //Move back to polling stage
                transfer_stage = 1;

                //Start write to sdcard
                ps2_mmce_fs_signal_operation(MMCE_FS_WRITE);
            }

            //Update chunk
            next_chunk = data->tail_idx + 1;
            if (next_chunk > CHUNK_READ_AHEAD_COUNT)
                next_chunk = 0;

            data->tail_idx = next_chunk;
        break;

        //Packet n + 2: Bytes written
        case 3:
            bytes8 = (uint8_t*)&data->bytes_transferred;

            receiveOrNextCmd(&cmd); //Padding

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            ps2_memory_card_set_cmd_callback(NULL);

            transfer_stage = 0;

            mc_respond(term);

        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_lseek)(void)
{
    uint8_t cmd;
    uint8_t *offset8 = NULL;
    uint8_t *position8 = NULL;
    int ready;

    switch(transfer_stage) {
        //Packet #1: File descriptor, offset, and whence
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();
            offset8 = (uint8_t*)&data->offset;

            mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
            mc_respond(0x0); receiveOrNextCmd(&data->fd);

            mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);
            mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&offset8);
            mc_respond(0x0); receiveOrNextCmd(&data->whence);

            DPRINTF("%s: fd: %i, offset: %i, whence: %i\n", __func__, data->fd, data->offset, data->whence);

            if (sd_fd_is_open(data->fd) != 0) {
                DPRINTF("%s: bad fd: %i, abort\n", __func__, data->fd);
                mc_respond(0x1);
                return; //Abort
            }

            ps2_mmce_fs_signal_operation(MMCE_FS_LSEEK);
            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_lseek);
            transfer_stage = 1;

            mc_respond(0x0);
        break;
        
        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 2;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;
        
        //Packet #n + 1: Position
        case 2:
            receiveOrNextCmd(&cmd);
            
            position8 = (uint8_t*)&data->position;
            
            mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
            mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

            ps2_memory_card_set_cmd_callback(NULL);

            DPRINTF("position: %i\n", data->position);
            transfer_stage = 0;
            mc_respond(term);
        break;
    }
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_remove)(void)
{
    uint8_t cmd;
    int idx = 0;
    int ready;

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_remove);
            transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;
        
        //Packet #2: Filename
        case 1:
            transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            DPRINTF("%s: name: %s\n", __func__, data->buffer);
            ps2_mmce_fs_signal_operation(MMCE_FS_REMOVE);
        break;

        //Packet #3 - n: Poll ready
        case 2:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 3;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;

        //Packet #n + 1: Return value
        case 3:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value
            mc_respond(term);
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_mkdir)(void)
{
    uint8_t cmd;
    int idx = 0;
    int ready;

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_mkdir);
            transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;
        
        //Packet #2: Filename
        case 1:
            transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);
        
            DPRINTF("%s: name: %s\n", __func__, data->buffer);
            ps2_mmce_fs_signal_operation(MMCE_FS_MKDIR);
        break;

        //Packet #3 - n: Poll ready
        case 2:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 3;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;

        //Packet #n + 1: Return value
        case 3:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            receiveOrNextCmd(&cmd); //padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value
            mc_respond(term);
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_rmdir)(void)
{
    uint8_t cmd;
    int idx = 0;
    int ready;

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved
            
            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_rmdir);
            transfer_stage = 1;
            
            mc_respond(0x0); //Padding
        break;
        
        //Packet #2: Filename
        case 1:
            transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);
        
            DPRINTF("%s: name: %s\n", __func__, data->buffer);
            ps2_mmce_fs_signal_operation(MMCE_FS_RMDIR);
        break;

        //Packet #3 - n: Poll ready
        case 2:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 3;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;
        
        //Packet #n + 1: Return value
        case 3:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            
            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value
            mc_respond(term);
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dopen)(void)
{
    uint8_t cmd;

    int idx = 0;
    int ready;

    switch(transfer_stage)
    {
        //Packet #1: Command and padding
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_dopen);
            transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;
        
        //Packet #2: Filename
        case 1:
            transfer_stage = 2;

            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            DPRINTF("%s: name: %s\n", __func__, data->buffer);

            ps2_mmce_fs_signal_operation(MMCE_FS_DOPEN);
        break;

        //Packet #3 - n: Poll ready
        case 2:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 3;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;

        //Packet #n + 1: File Descriptor
        case 3:
            receiveOrNextCmd(&cmd); //Padding

            mc_respond(data->fd);  receiveOrNextCmd(&cmd); //File descriptor
            
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            mc_respond(term);
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dclose)(void)
{
    uint8_t cmd;
    int ready;

    switch (transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_dclose);
            transfer_stage = 1;

            mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
            mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor

            ps2_mmce_fs_signal_operation(MMCE_FS_DCLOSE);

            DPRINTF("%s: fd: %i\n", __func__, data->fd);
        break;

        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 2;

            DPRINTF("ready: %i\n", ready);
            mc_respond(ready);
        break;
        
        //Packet #n + 1: Return value
        case 2:
            receiveOrNextCmd(&cmd);
            
            ps2_memory_card_set_cmd_callback(NULL);
            transfer_stage = 0;

            mc_respond(data->rv);   //Return value
            mc_respond(term);       //Term
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dread)(void)
{
    uint8_t cmd;
    int idx = 0;
    int ready;

    switch(transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();
            
            mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
            mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor
    
            DPRINTF("%s: fd: %i\n", __func__, data->fd);

            if (sd_fd_is_open(data->fd) != 0) {
                mc_respond(term);
                return;
            }

            //Hack: Poll data->rv and have it set to -1 if iterate_dir returns -1
            data->rv = -2;
            ps2_mmce_fs_signal_operation(MMCE_FS_DREAD);            

            while (data->rv == -2) {
                sleep_us(1);
            }

            if (data->rv == -1) {
                mc_respond(term);
                return;
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_dread);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);
            ready = ps2_mmce_fs_is_ready();
            if (ready == 1)
                transfer_stage = 2;

            mc_respond(ready);
        break;

        //Packet #n + 1: io_stat_t and filename len
        case 2:            
            receiveOrNextCmd(&cmd); //Padding
            
            mc_respond(data->fileio_stat.mode >> 24);
            mc_respond(data->fileio_stat.mode >> 16);
            mc_respond(data->fileio_stat.mode >> 8);
            mc_respond(data->fileio_stat.mode);

            mc_respond(data->fileio_stat.attr >> 24);
            mc_respond(data->fileio_stat.attr >> 16);
            mc_respond(data->fileio_stat.attr >> 8);
            mc_respond(data->fileio_stat.attr);

            mc_respond(data->fileio_stat.size >> 24);
            mc_respond(data->fileio_stat.size >> 16);
            mc_respond(data->fileio_stat.size >> 8);
            mc_respond(data->fileio_stat.size);

            for(int i = 0; i < 8; i++) {
                mc_respond(data->fileio_stat.ctime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(data->fileio_stat.atime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(data->fileio_stat.mtime[i]);
            }
            
            mc_respond(data->fileio_stat.hisize >> 24);
            mc_respond(data->fileio_stat.hisize >> 16);
            mc_respond(data->fileio_stat.hisize >> 8);
            mc_respond(data->fileio_stat.hisize);

            transfer_stage = 3;

            mc_respond(data->length); //Filename length
        break;

        //Packet #n + 2: Filename
        case 3:
            transfer_stage = 4;

            do {
                mc_respond(data->buffer[0][idx++]); receiveOrNextCmd(&cmd);
            } while (data->buffer[0][idx] != 0x0);
            
            mc_respond(0x0); //Null term
        break;

        //Packet #n + 3: Term
        case 4:
            receiveOrNextCmd(&cmd); //Padding
            mc_respond(0x0);        //Reserved
            
            ps2_memory_card_set_cmd_callback(NULL);
            transfer_stage = 0;

            mc_respond(0xff);       //Term
        break;
    }
}