#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ps2_cardman.h"
#include "ps2_memory_card.h"
#include "ps2_mc_internal.h"

#include "ps2_sd2psxman.h"
#include "ps2_sd2psxman_commands.h"

#include "game_db/game_db.h"

#include "debug.h"

#include <sd.h>
#include <sys/_default_fcntl.h>

#include "mmce_fs/ps2_mmce_fs.h"

#include "ps2_mmceman_debug.h"

//#define DPRINTF(fmt, x...) printf(fmt, ##x)
#define DPRINTF(x...) 

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
    mc_respond(0x0); receiveOrNextCmd(&cmd); //type (unused?)
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
            MP_CMD_START();
            ps2_mmce_fs_wait_ready();         //Wait for file handling to be ready
            data = ps2_mmce_fs_get_data();    //Get pointer to mmce fs data

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&packed_flags);   //File flags
            
            data->flags  = (packed_flags & 3);          //O_RDONLY, O_WRONLY, O_RDWR
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

            log_info(1, "%s: name: %s flags: 0x%x\n", __func__, (const char*)data->buffer, data->flags);

            MP_SIGNAL_OP();
            //Signal op in core1 (ps2_mmce_fs_run)
            ps2_mmce_fs_signal_operation(MMCE_FS_OPEN); 
        break;

        //Packet #3: File descriptor and termination byte
        case 2:
            receiveOrNextCmd(&cmd); //Padding
            ps2_mmce_fs_wait_ready(); //Wait ready up to 1s

            mc_respond(data->fd);  receiveOrNextCmd(&cmd);

            ps2_memory_card_set_cmd_callback(NULL); //Clear callback
            transfer_stage = 0; //Clear stage
            mc_respond(term);   //End transfer
            MP_CMD_END();
            
        break;
    }    
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_close)(void)
{
    uint8_t cmd;

    MP_CMD_START();
    ps2_mmce_fs_wait_ready();
    data = ps2_mmce_fs_get_data();

    mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
    mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor

    log_info(1, "%s: fd: %i\n", __func__, data->fd);

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_CLOSE);
    ps2_mmce_fs_wait_ready();

    mc_respond(data->rv);   //Return value

    log_info(1, "%s: rv: %i\n", __func__, data->rv);

    mc_respond(term);
    
    MP_CMD_END();
    
}

//TODO: reimplement read ahead for normal reads
inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_read)(void)
{
    uint8_t cmd;
    uint8_t *len8 = NULL;
    uint8_t *bytes8 = NULL;

    uint8_t last_byte;    
    uint8_t next_chunk;
    uint32_t bytes_left_in_packet;

    QPRINTF("%s called in Stage %d \n", __func__, transfer_stage);

    switch(transfer_stage) {
        //Packet #1: File handle, length, and return value
        case 0:
            MP_CMD_START();

            ps2_mmce_fs_wait_ready();           //Wait for file handling to be ready
            data = ps2_mmce_fs_get_data();      //Get pointer to data

            //Clear values used in this transfer
            data->bytes_transferred = 0x0;
            data->bytes_read = 0;
            data->tail_idx = 0;
            data->head_idx = 0;
            
            len8 = (uint8_t*)&data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&data->fd);    //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);   //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);   //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);   //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);   //Len MSB - 3

            log_info(1, "%s: fd: %i, len %u\n", __func__, data->fd, data->length);

            //Check if fd is valid before continuing
            ps2_mmce_fs_signal_operation(MMCE_FS_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (data->rv == -1) {
                log_error(1, "%s: bad fd: %i, abort\n", __func__, data->fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            MP_SIGNAL_OP();
            //Start async continuous read on core 1
            ps2_mmce_fs_signal_operation(MMCE_FS_READ);

            //Wait for first chunk to become available before ending this transfer (~2.5ms until timeout)
            while(data->chunk_state[data->tail_idx] != CHUNK_STATE_READY && data->transfer_failed != 1) {

                log_trace(1, "w: %u s:%u\n", data->tail_idx, data->chunk_state[data->tail_idx]);
                
                //Failed to read data
                if (data->chunk_state[data->tail_idx] == CHUNK_STATE_INVALID) {

                    //Failed to read ANY data
                    if (data->rv == 0) {
                        log_error(1, "Failed to read any data for chunk\n");    
                        data->chunk_state[data->tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        return;             //Abort

                    //Got some data
                    } else {
                        data->transfer_failed = 1; //Mark this transfer as failed to skip chunk waits and proceed
                    }
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
        //TODO: cleanup
        case 1:
            receiveOrNextCmd(&cmd); //Padding

            data->bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = data->length - data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            next_chunk = data->tail_idx + 1;
            if (next_chunk > CHUNK_COUNT)
                next_chunk = 0;

            //If transfer was only 1 byte, skip this
            if (bytes_left_in_packet != 0) {

                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(data->buffer[data->tail_idx][i]);
                }

                last_byte = data->buffer[data->tail_idx][bytes_left_in_packet];

                //Check if there's more packets after this
                if ((bytes_left_in_packet + data->bytes_transferred) < data->length) {

                    /* If reading data from the sdcard fails at any point, the SIO2 is still going to proceed
                     * until it has recieved the number of requested bytes. In this case, skip waiting on the
                     * next chunk(s) to be read and instead send old chunk contents. This should be okay as the 
                     * number of bytes *actually* read is sent in the last packet */

                    //Wait for next chunk to be available before ending this transfer (~2s until timeout)
                    while(data->chunk_state[next_chunk] != CHUNK_STATE_READY && data->transfer_failed != 1) {

                        log_trace(1, "w: %u s:%u\n", next_chunk, data->chunk_state[next_chunk]);

                        if (data->chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                            log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                            data->transfer_failed = 1;
                        }
                        sleep_us(1);
                    }

                    //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                    ps2_queue_tx_byte_on_reset(data->buffer[next_chunk][0]);
                }
            }

            //Update transferred count
            data->bytes_transferred += bytes_left_in_packet;

            //Enter crit and mark chunk as consumed
            critical_section_enter_blocking(&mmce_fs_crit);
            data->chunk_state[data->tail_idx] = CHUNK_STATE_NOT_READY;
            critical_section_exit(&mmce_fs_crit);

            log_trace(1, "%u c, bip: %u\n", data->tail_idx, (bytes_left_in_packet + 1));

            //Update tail idx
            data->tail_idx = next_chunk;

            //If there aren't anymore packet's left after this, move to final transfer stage
            if (data->bytes_transferred == data->length)
                transfer_stage = 2;

            //Send last byte of packet and end current transfer
            if (bytes_left_in_packet != 0)
                mc_respond(last_byte);

            break;

        //Packet #3: Bytes read
        case 2:
            receiveOrNextCmd(&cmd); //Padding

            bytes8 = (uint8_t*)&data->bytes_read;

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd); //Bytes read
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            data->transfer_failed = 0; //clear fail state
            ps2_memory_card_set_cmd_callback(NULL);

            log_info(1, "%s: read: %u\n", __func__, data->bytes_read);
        
            transfer_stage = 0;
            mc_respond(term);
            MP_CMD_END();
            

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
            data->tail_idx = 0;
            data->bytes_read = 0;
            data->bytes_written = 0;

            len8 = (uint8_t*)&data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&data->fd);    //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);   //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);   //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);   //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);   //Len MSB - 3

            log_info(1, "%s: fd: %i, len %u\n", __func__, data->fd, data->length);
            
            //Check if fd is valid before continuing
            ps2_mmce_fs_signal_operation(MMCE_FS_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (data->rv == -1) {
                log_error(1, "%s: bad fd: %i, abort\n", __func__, data->fd);
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

            ps2_mmce_fs_wait_ready();

            if (data->bytes_transferred == data->length) {
                transfer_stage = 3;  //Move to final transfer stage
            } else {
                transfer_stage = 2;  //More data to write
            }

            log_trace(1, "ready\n");
            mc_respond(1);
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
                log_trace(1, "bytes left in packet: %u, bytes transferred: %u\n", bytes_left_in_packet, data->bytes_transferred);

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

                //Reset tail idx
                data->tail_idx = 0;
            
            //More data needed before performing actual write to sdcard
            } else {
                //Update chunk
                next_chunk = data->tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;

                data->tail_idx = next_chunk;
            }

        break;

        //Packet n + 2: Bytes written
        case 3:
            bytes8 = (uint8_t*)&data->bytes_written;

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
    

    MP_CMD_START();
    ps2_mmce_fs_wait_ready();
    data = ps2_mmce_fs_get_data();

    offset8 = (uint8_t*)&data->offset;
    data->offset = 0;
    data->whence = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reserved
    mc_respond(0x0); receiveOrNextCmd(&data->fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);
    mc_respond(0x0); receiveOrNextCmd(&data->whence);

    log_info(1, "%s: fd: %i, offset: %lli, whence: %u\n", __func__, data->fd, (long long int)data->offset, data->whence);

    ps2_mmce_fs_signal_operation(MMCE_FS_VALIDATE_FD);
    ps2_mmce_fs_wait_ready();

    //Invalid fd, send -1
    if (data->rv == -1) {
        log_error(1, "Invalid fd\n");
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(term);
        return;
    }

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_LSEEK);
    ps2_mmce_fs_wait_ready();

    position8 = (uint8_t*)&data->position;
    
    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log_info(1, "%s: position %llu\n", __func__, (long long unsigned int)data->position);

    mc_respond(term);
    MP_CMD_END();
    
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_remove)(void)
{
    uint8_t cmd;
    int idx = 0;
    

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
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

            log_info(1, "%s: name: %s\n", __func__, (const char*)data->buffer);

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_REMOVE);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value

            log_info(1, "%s: rv: %i\n", __func__, data->rv);

            mc_respond(term);
            MP_CMD_END();
            
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_mkdir)(void)
{
    uint8_t cmd;
    int idx = 0;
    

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
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
        
            log_info(1, "%s: name: %s\n", __func__, (const char*)data->buffer);
            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_MKDIR);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value
            
            log_info(1, "%s: rv: %i\n", __func__, data->rv);

            mc_respond(term);
            MP_CMD_END();
            
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_rmdir)(void)
{
    uint8_t cmd;
    int idx = 0;
    

    switch(transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
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
        
            log_info(1, "%s: name: %s\n", __func__, (const char*)data->buffer);
            
            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_RMDIR);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->rv); receiveOrNextCmd(&cmd); //Return value

            log_info(1, "%s: rv: %i\n", __func__, data->rv);

            mc_respond(term);
            MP_CMD_END();
            
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dopen)(void)
{
    uint8_t cmd;

    int idx = 0;
    

    switch(transfer_stage)
    {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
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

            log_info(1, "%s: name: %s\n", __func__, (const char*)data->buffer);

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_DOPEN);
        break;

        //Packet #3: File Descriptor
        case 2:
            receiveOrNextCmd(&cmd); //Padding
            ps2_mmce_fs_wait_ready();
            mc_respond(data->fd);  receiveOrNextCmd(&cmd); //File descriptor
            
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            log_info(1, "%s: rv: %i\n", __func__, data->rv);

            mc_respond(term);

            MP_CMD_END();
            
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dclose)(void)
{
    uint8_t cmd;
    

    MP_CMD_START();
    ps2_mmce_fs_wait_ready();
    data = ps2_mmce_fs_get_data();

    mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
    mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor
    log_info(1, "%s: fd: %i\n", __func__, data->fd);

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_DCLOSE);
    ps2_mmce_fs_wait_ready();

    mc_respond(data->rv);   //Return value

    log_info(1, "%s: rv: %i\n", __func__, data->rv);

    mc_respond(term);       //Term
    MP_CMD_END();
    
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dread)(void)
{
    uint8_t cmd;
    int idx = 0;
    

    switch(transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            MP_CMD_START();
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered
            mc_respond(0x0); receiveOrNextCmd(&data->fd);   //File descriptor

            log_info(1, "%s: fd: %i\n", __func__, data->fd);

            ps2_mmce_fs_signal_operation(MMCE_FS_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (data->rv == -1) {
                log_error(1, "%s: Bad fd: %i, abort\n", __func__, data->fd);
                mc_respond(0x1);
                return;
            }

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_DREAD);            
            ps2_mmce_fs_wait_ready();

            if (data->rv == -1) {
                log_error(1, "%s: Failed to get stat\n", __func__);
                mc_respond(0x1);
                return;
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_dread);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #n + 1: io_stat_t and filename len
        case 1:            
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

            transfer_stage = 2;

            mc_respond(data->length); //Filename length
        break;

        //Packet #n + 2: Filename
        case 2:
            transfer_stage = 3;

            do {
                mc_respond(data->buffer[0][idx++]); receiveOrNextCmd(&cmd);
            } while (data->buffer[0][idx] != 0x0);
            
            mc_respond(0x0); //Null term
        break;

        //Packet #n + 3: Term
        case 3:
            receiveOrNextCmd(&cmd); //Padding
            mc_respond(data->it_fd); //iterator fd
            
            ps2_memory_card_set_cmd_callback(NULL);
            transfer_stage = 0;

            mc_respond(term);       //Term
            MP_CMD_END();
            
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_getstat)(void)
{
    uint8_t cmd;
    int idx = 0;
    

    switch(transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            MP_CMD_START();
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reservered

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_getstat);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2: Name
        case 1:
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            data->flags = 0; //RD_ONLY
            ps2_mmce_fs_signal_operation(MMCE_FS_OPEN);
            transfer_stage = 2;

            log_info(1, "%s: name: %s\n", __func__, (const char*)data->buffer);
        break;

        //Packet #2: io_stat_t, rv, and term
        case 2:
            receiveOrNextCmd(&cmd); //Padding
            ps2_mmce_fs_wait_ready();   //Finish open
            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_GETSTAT);
            ps2_mmce_fs_wait_ready();
            
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

            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            mc_respond(data->rv);

            MP_CMD_END();
            

            if (data->fd > 0) {
                ps2_mmce_fs_signal_operation(MMCE_FS_CLOSE);
                ps2_mmce_fs_wait_ready();
            }

            mc_respond(term); 
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_lseek64)(void)
{
    uint8_t cmd;
    uint8_t *offset8 = NULL;
    uint8_t *position8 = NULL;

    MP_CMD_START();
    ps2_mmce_fs_wait_ready();
    data = ps2_mmce_fs_get_data();

    offset8 = (uint8_t*)&data->offset64;
    position8 = (uint8_t*)&data->position64; //Not sure casting to 64 bit is good here....

    data->offset64 = 0;
    data->whence64 = 0;
    data->position64 = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
    mc_respond(0x0); receiveOrNextCmd(&data->fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x7]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x6]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x5]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x4]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);      
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);

    mc_respond(0x0); receiveOrNextCmd(&data->whence64);

    log_info(1, "%s: fd: %i, whence: %u, offset: %llu\n", __func__, data->fd, data->whence, (long long unsigned int)data->offset);

    ps2_mmce_fs_signal_operation(MMCE_FS_VALIDATE_FD);
    ps2_mmce_fs_wait_ready();

    if (data->rv == -1) {
        log_error(1, "%s: bad fd: %i, abort\n", __func__, data->fd);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(term);
        return;
    }

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_LSEEK64);
    ps2_mmce_fs_wait_ready();

    mc_respond(position8[0x7]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x6]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x5]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x4]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log_info(1, "%s: position: %llu\n", __func__, (long long unsigned int)data->position);

    mc_respond(term);
    MP_CMD_END();
    
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_read_sector)(void)
{
    uint8_t cmd;
    uint32_t sector;
    uint32_t count;
    uint8_t *sector8;
    uint8_t *count8;

    uint8_t last_byte;
    uint8_t next_chunk;
    uint32_t bytes_left_in_packet;
    uint64_t offset;
    uint64_t position;

    switch(transfer_stage) {
        case 0:
            MP_CMD_START();
            ps2_mmce_fs_wait_ready();
            data = ps2_mmce_fs_get_data();
            
            //Clear values used in this transfer
            data->bytes_transferred = 0x0;
            data->bytes_read = 0;
            data->tail_idx = 0;
            data->head_idx = 0;

            sector = 0;
            count  = 0;

            sector8 = (uint8_t*)&sector;
            count8  = (uint8_t*)&count;

            mc_respond(0x0); receiveOrNextCmd(&cmd);         //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&data->fd);    //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x0]);

            offset = ((uint64_t)sector) * 2048;

            //Data read ahead, skip seeking
            if (data->read_ahead.fd == data->fd && data->read_ahead.valid && data->read_ahead.pos == offset) {
                
                log_info(1, "%s: fd: %i, got valid read ahead, skipping seek\n", __func__, data->fd);

                //Mark as consumed
                data->read_ahead.valid = 0;
                data->bytes_read = CHUNK_SIZE;
                data->use_read_ahead = 1;
            } else {
                //TEMP: Retry loop. Heavy fragmentation can result in long seek times and sometimes failed seeks altogether
                //Retry if seek fails up to 3 times and print warning

                log_info(1, "%s: fd: %i, seeking to offset %llu\n", __func__, data->fd, (long long unsigned int)offset);

                for (int i = 0; i < 3; i++) {
                    sd_seek_set_new(data->fd, offset);
                    position = sd_tell_new(data->fd);
                    if (position != offset) {
                        printf("[FATAL] Sector seek failed, possible fragmentation issues, check card! Got: 0x%llu, Exp: 0x%llu\n", position, offset);
                    } else {
                        break;
                    }
                }
            }

            mc_respond(0x0); receiveOrNextCmd(&count8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x0]);

            data->length = count * 2048;

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_READ);

            log_info(1, "%s: sector: %u, count: %u, length: %u\n", __func__, sector, count, data->length);

            if (data->use_read_ahead != 1) {

                while(data->chunk_state[data->tail_idx] != CHUNK_STATE_READY) {

                    log_trace(1, "w: %u s:%u\n", data->tail_idx, data->chunk_state[data->tail_idx]);
                    
                    //Reading ahead failed to get requested data
                    if (data->chunk_state[data->tail_idx] == CHUNK_STATE_INVALID) {
                        log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        data->chunk_state[data->tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        return;             //Abort
                    }

                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(data->buffer[data->tail_idx][0]);
            
            //We already have data ahead, no need to wait
            } else {
                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(data->read_ahead.buffer[0]);
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_read_sector);
            
            transfer_stage = 1;
            
            mc_respond(0x0);
        break;

        case 1:
            receiveOrNextCmd(&cmd); //Padding

            data->bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = data->length - data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            //If we're using the read ahead buffer instead of the ring buffer, avoid incrementing tail idx for now
            if (data->use_read_ahead == 1) {
                next_chunk = data->tail_idx;
            } else {
                next_chunk = data->tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;
            }

            //Using data from read ahead buffer
            if (data->use_read_ahead == 1) {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(data->read_ahead.buffer[i]);
                }

                last_byte = data->read_ahead.buffer[bytes_left_in_packet];

            //Use data from ring buffer
            } else {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(data->buffer[data->tail_idx][i]);
                }

                last_byte = data->buffer[data->tail_idx][bytes_left_in_packet];
            }

            //Check if there's more packets after this
            if ((bytes_left_in_packet + data->bytes_transferred) < data->length) {

                //Wait for next chunk to be available before ending this transfer (~2.5ms until timeout)
                while(data->chunk_state[next_chunk] != CHUNK_STATE_READY && data->transfer_failed != 1) {
                    
                    log_trace(1, "w: %u s:%u\n", next_chunk, data->chunk_state[next_chunk]);

                    //TODO: error handling
                    if (data->chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                        log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        data->transfer_failed = 1;
                    }
                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(data->buffer[next_chunk][0]);
            }

            //Update transferred count
            data->bytes_transferred += bytes_left_in_packet;

            //Using read ahead buffer
            if (data->use_read_ahead) {
                data->use_read_ahead = 0;
                data->read_ahead.valid = 0;

                log_trace(1, "ra c, bip: %u\n", (bytes_left_in_packet + 1));

            //Using ring buffer 
            } else {
                //Enter crit and mark chunk as consumed
                critical_section_enter_blocking(&mmce_fs_crit);
                data->chunk_state[data->tail_idx] = CHUNK_STATE_NOT_READY;
                critical_section_exit(&mmce_fs_crit);

                log_trace(1, "%u c, bip: %u\n", data->tail_idx, (bytes_left_in_packet + 1));
                
                //Update tail idx
                data->tail_idx = next_chunk;
            }

            //If there aren't anymore packet's left after this, move to final transfer stage
            if (data->bytes_transferred == data->length)
                transfer_stage = 2;

            //Send last byte of packet and end current transfer
            mc_respond(last_byte);

        break;

        case 2:
            receiveOrNextCmd(&cmd); //Padding

            //Get sectors read count
            data->bytes_read = data->bytes_read / 2048;
            
            log_info(1, "Sectors read %u of %u\n", data->bytes_read, data->length/2048);

            count8 = (uint8_t*)&data->bytes_read;

            //Sectors read
            mc_respond(count8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x0]); receiveOrNextCmd(&cmd);

            //TODO: Enable read ahead on sector reads and test
            ps2_mmce_fs_signal_operation(MMCE_FS_READ_AHEAD);

            ps2_memory_card_set_cmd_callback(NULL);
        
            transfer_stage = 0;
            mc_respond(term);
            MP_CMD_END();
            
        break;
    }
}