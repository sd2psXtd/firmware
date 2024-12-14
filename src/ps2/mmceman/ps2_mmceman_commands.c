#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/_default_fcntl.h>

#include "sd.h"

#include "ps2_cardman.h"
#include "card_emu/ps2_memory_card.h"
#include "card_emu/ps2_mc_internal.h"

#include "ps2_mmceman.h"
#include "ps2_mmceman_commands.h"
#include "ps2_mmceman_debug.h"
#include "ps2_mmceman_fs.h"

#include "game_db/game_db.h"

#include "debug.h"

#if LOG_LEVEL_MMCEMAN == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_MMCEMAN, level, fmt, ##x)
#endif

//TODO: temp global values, find them a home
volatile ps2_mmceman_fs_op_data_t *op_data = NULL;

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_ping)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x1); receiveOrNextCmd(&cmd); //protocol version
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product ID
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product revision number
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_PING\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_status)(void)
{
    uint8_t cmd;
    uint8_t status = 0;
    uint8_t errno = 0;

    /* TODO: Finish fleshing out spec and implementation details.
     * Currently it sets bit 0 of the first byte if the sd2psx
     * is busy switching memcards */
    mc_respond(0x0);    receiveOrNextCmd(&cmd); //reserved byte

    if (!ps2_cardman_is_idle()) {
        status |= 1;
    }

    mc_respond(errno);    receiveOrNextCmd(&cmd);
    mc_respond(status);   receiveOrNextCmd(&cmd);
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_GET_STATUS\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_card)(void)
{
    uint8_t cmd;
    int card = ps2_cardman_get_idx();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(card >> 8);   receiveOrNextCmd(&cmd); //card upper 8 bits
    mc_respond(card & 0xff); receiveOrNextCmd(&cmd); //card lower 8 bits
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_GET_CARD\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_set_card)(void)
{
    uint8_t cmd;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //type (unused?)
    mc_respond(0x0); receiveOrNextCmd(&cmd); //mode
    mmceman_mode = cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //card upper 8 bits
    mmceman_cnum = cmd << 8;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //card lower 8 bits
    mmceman_cnum |= cmd;
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_SET_CARD mode: %i, num: %i\n", mmceman_mode, mmceman_cnum);

    mmceman_cmd = MMCEMAN_SET_CARD;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_channel)(void)
{
    uint8_t cmd;
    int chan = ps2_cardman_get_channel();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(chan >> 8);   receiveOrNextCmd(&cmd); //channel upper 8 bits
    mc_respond(chan & 0xff); receiveOrNextCmd(&cmd); //channel lower 8 bits
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_GET_CHANNEL\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_set_channel)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //mode
    mmceman_mode = cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //channel upper 8 bits
    mmceman_cnum = cmd << 8;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //channel lower 8 bits
    mmceman_cnum |= cmd;
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_SET_CHANNEL mode: %i, num: %i\n", mmceman_mode, mmceman_cnum);

    mmceman_cmd = MMCEMAN_SET_CHANNEL;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_gameid)(void)
{
    uint8_t cmd;
    const char* gameId = ps2_mmceman_get_gameid();
    if (gameId == NULL) {
        return;
    } else {
        uint8_t gameid_len = strlen(gameId) + 1; //+1 null terminator
        mc_respond(0x0);        receiveOrNextCmd(&cmd);    //reserved byte
        mc_respond(gameid_len); receiveOrNextCmd(&cmd);    //gameid length

        for (int i = 0; i < gameid_len; i++) {
            mc_respond(gameId[i]); receiveOrNextCmd(&cmd); //gameid
        }

        for (int i = 0; i < (250 - gameid_len); i++) {
            mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
        }

        mc_respond(term);
    }

    log(LOG_INFO, "received MMCEMAN_GET_GAMEID\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_set_gameid)(void)
{
    uint8_t cmd;
    uint8_t gameid_len;
    uint8_t received_id[252] = { 0 };
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //gameid length
    gameid_len = cmd;

    for (int i = 0; i < gameid_len; i++) {
        mc_respond(0x0);    receiveOrNextCmd(&cmd); //gameid
        received_id[i] = cmd;
    }

    mc_respond(term);

    ps2_mmceman_set_gameid(received_id);

    //log(LOG_INFO, "received MMCEMAN_SET_GAMEID len %i, id: %s\n", gameid_len, sanitized_game_id);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_unmount_bootcard)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_UNMOUNT_BOOTCARD\n");

    mmceman_cmd = MMCEMAN_UNMOUNT_BOOTCARD;
}

/* NOTE: Only used for MMCEMAN_FS at the moment, in future it could be used to
 * reset other states as well */
inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_reset)(void)
{
    uint8_t cmd;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x0); receiveOrNextCmd(&cmd); //padding

#ifdef FEAT_PS2_MMCE
    ps2_mmceman_fs_wait_ready();

    //close all open files, reset states
    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_RESET);
#endif

    mc_respond(term);

    log(LOG_INFO, "received MMCEMAN_RESET\n");
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_open)(void)
{
    uint8_t cmd;
    uint8_t packed_flags;

    int idx = 0;
    int ready = 0;

    switch(mmceman_transfer_stage)
    {
        //Packet #1: Command and flags
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();            //Wait for file handling to be ready
            op_data = ps2_mmceman_fs_get_op_data();    //Get pointer to mmce fs op_data

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&packed_flags);   //File flags

            op_data->flags  = (packed_flags & 3);          //O_RDONLY, O_WRONLY, O_RDWR
            op_data->flags |= (packed_flags & 8) << 5;     //O_APPEND
            op_data->flags |= (packed_flags & 0xE0) << 4;  //O_CREATE, O_TRUNC, O_EXCL

            //Jump to this function after /CS triggered reset
            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_open);

            mmceman_transfer_stage = 1; //Update stage
            mc_respond(0xff);   //End transfer
        break;

        //Packet #2: Filename
        case 1:
            mmceman_transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log(LOG_INFO, "%s: name: %s flags: 0x%x\n", __func__, (const char*)op_data->buffer, op_data->flags);

            MP_SIGNAL_OP();
            //Signal op in core1 (ps2_mmceman_fs_run)
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_OPEN);
        break;

        //Packet #3: File descriptor and termination byte
        case 2:
            ps2_mmceman_set_cb(NULL);   //Clear callback
            mmceman_transfer_stage = 0; //Clear stage

            receiveOrNextCmd(&cmd);     //Padding
            ps2_mmceman_fs_wait_ready();//Wait ready up to 1s

            mc_respond(op_data->fd);  receiveOrNextCmd(&cmd);

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_close)(void)
{
    uint8_t cmd;

    MP_CMD_START();
    mmceman_op_in_progress = true;

    ps2_mmceman_fs_wait_ready();
    op_data = ps2_mmceman_fs_get_op_data();

    mc_respond(0x0); receiveOrNextCmd(&cmd);                  //Reservered
    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd);//File descriptor

    log(LOG_INFO, "%s: fd: %i\n", __func__, op_data->fd);

    MP_SIGNAL_OP();
    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_CLOSE);
    ps2_mmceman_fs_wait_ready();

    mc_respond(op_data->rv);   //Return value

    log(LOG_INFO, "%s: rv: %i\n", __func__, op_data->rv);

    mc_respond(term);

    mmceman_op_in_progress = false;
    MP_CMD_END();

}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_read)(void)
{
    uint8_t cmd;
    uint8_t *len8 = NULL;
    uint8_t *bytes8 = NULL;

    uint8_t last_byte;
    uint8_t next_chunk;
    uint32_t bytes_left_in_packet;

    log(LOG_TRACE, "%s called in Stage %d \n", __func__, mmceman_transfer_stage);

    switch(mmceman_transfer_stage) {
        //Packet #1: File handle, length, and return value
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();        //Wait for file handling to be ready
            op_data = ps2_mmceman_fs_get_op_data();//Get pointer to op_data

            //Clear values used in this transfer
            op_data->bytes_transferred = 0x0;
            op_data->bytes_read = 0;
            op_data->tail_idx = 0;
            op_data->head_idx = 0;

            len8 = (uint8_t*)&op_data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);                   //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);                   //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd); //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);             //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);             //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);             //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);             //Len MSB - 3

            log(LOG_INFO, "%s: fd: %i, len %u\n", __func__, op_data->fd, op_data->length);

            //Check if fd is valid before continuing
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_VALIDATE_FD);
            ps2_mmceman_fs_wait_ready();

            if (op_data->rv == -1) {
                log(LOG_ERROR, "%s: bad fd: %i, abort\n", __func__, op_data->fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            MP_SIGNAL_OP();
            //Start async continuous read on core 1
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_READ);

            //Wait for first chunk to become available before ending this transfer
            while(op_data->chunk_state[op_data->tail_idx] != CHUNK_STATE_READY && op_data->transfer_failed != 1) {

                //Set by /CS high INTR, catch timeout condition
                if (mmceman_timeout_detected)
                    return;

                log(LOG_TRACE, "w: %u s:%u\n", op_data->tail_idx, op_data->chunk_state[op_data->tail_idx]);

                //Failed to read full chunk
                if (op_data->chunk_state[op_data->tail_idx] == CHUNK_STATE_INVALID) {

                    //Failed to read ANY bytes
                    if (op_data->rv == 0) {
                        log(LOG_ERROR, "Failed to read any data for chunk\n");
                        op_data->chunk_state[op_data->tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        return;             //Abort

                    //Got some bytes
                    } else {
                        op_data->transfer_failed = 1; //Mark this transfer as failed to skip chunk waits and proceed
                    }
                }

                sleep_us(1);
            }

            //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
            ps2_mmceman_queue_tx(op_data->buffer[op_data->tail_idx][0]);

            //Jump to this function after /CS triggered reset
            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_read);

            //Update transfer stage
            mmceman_transfer_stage = 1;

            //End current transfer
            mc_respond(0x0);

            break;

        //Packet #2 - n: Raw chunk data
        case 1:
            receiveOrNextCmd(&cmd); //Padding

            op_data->bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = op_data->length - op_data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            next_chunk = op_data->tail_idx + 1;
            if (next_chunk > CHUNK_COUNT)
                next_chunk = 0;

            //If transfer was only 1 byte, skip this
            if (bytes_left_in_packet != 0) {

                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(op_data->buffer[op_data->tail_idx][i]);
                }

                last_byte = op_data->buffer[op_data->tail_idx][bytes_left_in_packet];

                //Check if there's more packets after this
                if ((bytes_left_in_packet + op_data->bytes_transferred) < op_data->length) {

                    /* If reading data from the sdcard fails at any point, the SIO2 is still going to proceed
                     * until it has recieved the number of requested bytes. In this case, skip waiting on the
                     * next chunk(s) to be read and instead send old chunk contents. This should be okay as the
                     * number of bytes *actually* read is sent in the last packet */

                    //Wait for next chunk to be available before ending this transfer
                    while(op_data->chunk_state[next_chunk] != CHUNK_STATE_READY && op_data->transfer_failed != 1) {

                        //Set by /CS high INTR, catch timeout condition
                        if (mmceman_timeout_detected)
                            return;

                        log(LOG_TRACE, "w: %u s:%u\n", next_chunk, op_data->chunk_state[next_chunk]);

                        if (op_data->chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                            log(LOG_ERROR, "Failed to read chunk, got CHUNK_STATE_INVALID, continuing\n");
                            op_data->transfer_failed = 1;
                        }
                        sleep_us(1);
                    }

                    //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                    ps2_mmceman_queue_tx(op_data->buffer[next_chunk][0]);
                }
            }

            //Update transferred count
            op_data->bytes_transferred += bytes_left_in_packet;

            //Enter crit and mark chunk as consumed
            critical_section_enter_blocking(&mmceman_fs_crit);
            op_data->chunk_state[op_data->tail_idx] = CHUNK_STATE_NOT_READY;
            critical_section_exit(&mmceman_fs_crit);

            log(LOG_TRACE, "%u c, bip: %u\n", op_data->tail_idx, (bytes_left_in_packet + 1));

            //Update tail idx
            op_data->tail_idx = next_chunk;

            //If there aren't anymore chunks left after this, move to final transfer stage
            if (op_data->bytes_transferred == op_data->length)
                mmceman_transfer_stage = 2;

            //Send last byte of packet and end current transfer
            if (bytes_left_in_packet != 0)
                mc_respond(last_byte);
        break;

        //Packet #3: Bytes read
        case 2:
            ps2_mmceman_set_cb(NULL);
            mmceman_transfer_stage = 0;

            receiveOrNextCmd(&cmd); //Padding

            bytes8 = (uint8_t*)&op_data->bytes_read;

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd); //Bytes read
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            log(LOG_INFO, "%s: read: %u\n", __func__, op_data->bytes_read);

            op_data->transfer_failed = 0; //clear fail state

            mc_respond(term);

            mmceman_op_in_progress = false;
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

    /* NOTE: Writes behave a bit differently from reads. Writes wait for a 4KB buffer to be filled (or until len has been read)
    *  before writing to the sdcard. While writing to the sdcard the PS2 will wait mid transfer for up to 2 seconds.
    *  Once the write is complete, the process will repeat if there is more data or move onto the final transfer stage */
    switch(mmceman_transfer_stage) {
        //Packet 1: File descriptor, length, and return value
        case 0:
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();            //Wait for file handling to be ready
            op_data = ps2_mmceman_fs_get_op_data();    //Get pointer to op_data

            op_data->bytes_transferred = 0x0;
            op_data->tail_idx = 0;
            op_data->bytes_read = 0;
            op_data->bytes_written = 0;

            len8 = (uint8_t*)&op_data->length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);                   //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);                   //Transfer mode (Unused)
            mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd); //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);             //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);             //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);             //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);             //Len MSB - 3

            log(LOG_INFO, "%s: fd: %i, len %u\n", __func__, op_data->fd, op_data->length);

            //Check if fd is valid before continuing
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_VALIDATE_FD);
            ps2_mmceman_fs_wait_ready();

            if (op_data->rv == -1) {
                log(LOG_ERROR, "%s: bad fd: %i, abort\n", __func__, op_data->fd);
                mc_respond(0x1);    //Return 1
                mmceman_op_in_progress = false;
                return;             //Abort
            }

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_write);
            mmceman_transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2 - n: Poll ready
        case 1:
            receiveOrNextCmd(&cmd);

            ps2_mmceman_fs_wait_ready();

            if (op_data->bytes_transferred == op_data->length) {
                mmceman_transfer_stage = 3;  //Move to final transfer stage
            } else {
                mmceman_transfer_stage = 2;  //More data to write
            }

            log(LOG_TRACE, "ready\n");
            mc_respond(1);
        break;

        //Packet #n + 1: Read bytes
        case 2:
            //Add first byte to buffer
            receiveOrNextCmd(&cmd);
            op_data->buffer[op_data->tail_idx][0] = cmd;
            op_data->bytes_transferred++;

            //Determine bytes left in this packet
            bytes_left_in_packet = op_data->length - op_data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1;

            //Avoid trying to read more data if write len == 1
            if (bytes_left_in_packet != 0) {
                log(LOG_TRACE, "bytes left in packet: %u, bytes transferred: %u\n", bytes_left_in_packet, op_data->bytes_transferred);

                //receive rest of bytes
                for (int i = 1; i <= bytes_left_in_packet; i++) {
                    mc_respond(0x0);
                    receiveOrNextCmd((uint8_t*)&op_data->buffer[op_data->tail_idx][i]);
                }

                //Update count
                op_data->bytes_transferred += bytes_left_in_packet;
            }

            //If bytes received == 4KB or bytes received == length
            if ((((op_data->bytes_transferred) % 4096) == 0) || (op_data->length == op_data->bytes_transferred)) {

                //Move back to polling stage
                mmceman_transfer_stage = 1;

                //Start write to sdcard
                ps2_mmceman_fs_signal_operation(MMCEMAN_FS_WRITE);

                //Reset tail idx
                op_data->tail_idx = 0;

            //More data needed before performing write to sdcard
            } else {
                //Update chunk
                next_chunk = op_data->tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;

                op_data->tail_idx = next_chunk;
            }
        break;

        //Packet n + 2: Bytes written
        case 3:
            ps2_mmceman_set_cb(NULL);
            mmceman_transfer_stage = 0;

            bytes8 = (uint8_t*)&op_data->bytes_written;

            receiveOrNextCmd(&cmd); //Padding

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            mc_respond(term);

            mmceman_op_in_progress = false;
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_lseek)(void)
{
    uint8_t cmd;
    uint8_t *offset8 = NULL;
    uint8_t *position8 = NULL;

    MP_CMD_START();
    mmceman_op_in_progress = true;

    ps2_mmceman_fs_wait_ready();
    op_data = ps2_mmceman_fs_get_op_data();

    offset8 = (uint8_t*)&op_data->offset;
    op_data->offset = 0;
    op_data->whence = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reserved
    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);
    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->whence);

    log(LOG_INFO, "%s: fd: %i, offset: %lli, whence: %u\n", __func__, op_data->fd, (long long int)op_data->offset, op_data->whence);

    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_VALIDATE_FD);
    ps2_mmceman_fs_wait_ready();

    //Invalid fd, send -1
    if (op_data->rv == -1) {
        log(LOG_ERROR, "Invalid fd\n");
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(term);
        mmceman_op_in_progress = false;
        return;
    }

    MP_SIGNAL_OP();
    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_LSEEK);
    ps2_mmceman_fs_wait_ready();

    position8 = (uint8_t*)&op_data->position;

    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log(LOG_INFO, "%s: position %llu\n", __func__, (long long unsigned int)op_data->position);

    mc_respond(term);

    mmceman_op_in_progress = false;
    MP_CMD_END();
}


inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_remove)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_remove);
            mmceman_transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;

        //Packet #2: Filename
        case 1:
            mmceman_transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log(LOG_INFO, "%s: name: %s\n", __func__, (const char*)op_data->buffer);

            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_REMOVE);
        break;

        //Packet #3: Return value
        case 2:
            mmceman_transfer_stage = 0;
            ps2_mmceman_set_cb(NULL);

            ps2_mmceman_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(op_data->rv); receiveOrNextCmd(&cmd); //Return value

            log(LOG_INFO, "%s: rv: %i\n", __func__, op_data->rv);

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_mkdir)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_mkdir);
            mmceman_transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;

        //Packet #2: Filename
        case 1:
            mmceman_transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log(LOG_INFO, "%s: name: %s\n", __func__, (const char*)op_data->buffer);
            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_MKDIR);
        break;

        //Packet #3: Return value
        case 2:
            mmceman_transfer_stage = 0;
            ps2_mmceman_set_cb(NULL);

            ps2_mmceman_fs_wait_ready();

            receiveOrNextCmd(&cmd); //padding
            mc_respond(op_data->rv); receiveOrNextCmd(&cmd); //Return value

            log(LOG_INFO, "%s: rv: %i\n", __func__, op_data->rv);

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_rmdir)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage) {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_rmdir);
            mmceman_transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;

        //Packet #2: Filename
        case 1:
            mmceman_transfer_stage = 2;
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log(LOG_INFO, "%s: name: %s\n", __func__, (const char*)op_data->buffer);

            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_RMDIR);
        break;

        //Packet #3: Return value
        case 2:
            mmceman_transfer_stage = 0;
            ps2_mmceman_set_cb(NULL);
            ps2_mmceman_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(op_data->rv); receiveOrNextCmd(&cmd); //Return value

            log(LOG_INFO, "%s: rv: %i\n", __func__, op_data->rv);

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dopen)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage)
    {
        //Packet #1: Command and padding
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_dopen);
            mmceman_transfer_stage = 1;

            mc_respond(0x0); //Padding
        break;

        //Packet #2: Filename
        case 1:
            mmceman_transfer_stage = 2;

            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log(LOG_INFO, "%s: name: %s\n", __func__, (const char*)op_data->buffer);

            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_DOPEN);
        break;

        //Packet #3: File Descriptor
        case 2:
            mmceman_transfer_stage = 0;
            ps2_mmceman_set_cb(NULL);

            receiveOrNextCmd(&cmd); //Padding

            ps2_mmceman_fs_wait_ready();
            mc_respond(op_data->fd);  receiveOrNextCmd(&cmd); //File descriptor

            log(LOG_INFO, "%s: rv: %i, fd: %i\n", __func__, op_data->rv, op_data->fd);

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dclose)(void)
{
    uint8_t cmd;

    MP_CMD_START();
    mmceman_op_in_progress = true;

    ps2_mmceman_fs_wait_ready();
    op_data = ps2_mmceman_fs_get_op_data();

    mc_respond(0x0); receiveOrNextCmd(&cmd);                    //Reservered
    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd);  //File descriptor
    log(LOG_INFO, "%s: fd: %i\n", __func__, op_data->fd);

    MP_SIGNAL_OP();
    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_DCLOSE);
    ps2_mmceman_fs_wait_ready();

    mc_respond(op_data->rv); //Return value

    log(LOG_INFO, "%s: rv: %i\n", __func__, op_data->rv);

    mc_respond(term);

    mmceman_op_in_progress = false;
    MP_CMD_END();
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_dread)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd);                    //Reservered
            mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd);  //File descriptor

            log(LOG_INFO, "%s: fd: %i\n", __func__, op_data->fd);

            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_VALIDATE_FD);
            ps2_mmceman_fs_wait_ready();

            if (op_data->rv == -1) {
                log(LOG_ERROR, "%s: Bad fd: %i, abort\n", __func__, op_data->fd);
                mc_respond(0x1);
                mmceman_op_in_progress = false;
                return;
            }

            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_DREAD);
            ps2_mmceman_fs_wait_ready();

            if (op_data->rv == -1) {
                log(LOG_ERROR, "%s: Failed to get stat\n", __func__);
                mc_respond(0x1);
                mmceman_op_in_progress = false;
                return;
            }

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_dread);
            mmceman_transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #n + 1: io_stat_t and filename len
        case 1:
            receiveOrNextCmd(&cmd); //Padding

            mc_respond(op_data->fileio_stat.mode >> 24);
            mc_respond(op_data->fileio_stat.mode >> 16);
            mc_respond(op_data->fileio_stat.mode >> 8);
            mc_respond(op_data->fileio_stat.mode);

            mc_respond(op_data->fileio_stat.attr >> 24);
            mc_respond(op_data->fileio_stat.attr >> 16);
            mc_respond(op_data->fileio_stat.attr >> 8);
            mc_respond(op_data->fileio_stat.attr);

            mc_respond(op_data->fileio_stat.size >> 24);
            mc_respond(op_data->fileio_stat.size >> 16);
            mc_respond(op_data->fileio_stat.size >> 8);
            mc_respond(op_data->fileio_stat.size);

            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.ctime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.atime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.mtime[i]);
            }

            mc_respond(op_data->fileio_stat.hisize >> 24);
            mc_respond(op_data->fileio_stat.hisize >> 16);
            mc_respond(op_data->fileio_stat.hisize >> 8);
            mc_respond(op_data->fileio_stat.hisize);

            mmceman_transfer_stage = 2;

            mc_respond(op_data->length); //Filename length
        break;

        //Packet #n + 2: Filename
        case 2:
            mmceman_transfer_stage = 3;

            do {
                mc_respond(op_data->buffer[0][idx++]); receiveOrNextCmd(&cmd);
            } while (op_data->buffer[0][idx] != 0x0);

            mc_respond(0x0); //Null term
        break;

        //Packet #n + 3: Term
        case 3:
            receiveOrNextCmd(&cmd);     //Padding
            mc_respond(op_data->it_fd); //iterator fd

            ps2_mmceman_set_cb(NULL);
            mmceman_transfer_stage = 0;

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_getstat)(void)
{
    uint8_t cmd;
    int idx = 0;

    switch(mmceman_transfer_stage) {
        //Packet #1: File descriptor
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reservered

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_getstat);
            mmceman_transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2: Name
        case 1:
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                op_data->buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            op_data->flags = 0; //RD_ONLY
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_OPEN);
            mmceman_transfer_stage = 2;

            log(LOG_INFO, "%s: name: %s\n", __func__, (const char*)op_data->buffer);
        break;

        //Packet #2: io_stat_t, rv, and term
        case 2:
            receiveOrNextCmd(&cmd);     //Padding
            ps2_mmceman_fs_wait_ready();//Finish open

            MP_SIGNAL_OP();

            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_GETSTAT);
            ps2_mmceman_fs_wait_ready();

            mc_respond(op_data->fileio_stat.mode >> 24);
            mc_respond(op_data->fileio_stat.mode >> 16);
            mc_respond(op_data->fileio_stat.mode >> 8);
            mc_respond(op_data->fileio_stat.mode);

            mc_respond(op_data->fileio_stat.attr >> 24);
            mc_respond(op_data->fileio_stat.attr >> 16);
            mc_respond(op_data->fileio_stat.attr >> 8);
            mc_respond(op_data->fileio_stat.attr);

            mc_respond(op_data->fileio_stat.size >> 24);
            mc_respond(op_data->fileio_stat.size >> 16);
            mc_respond(op_data->fileio_stat.size >> 8);
            mc_respond(op_data->fileio_stat.size);

            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.ctime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.atime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(op_data->fileio_stat.mtime[i]);
            }

            mc_respond(op_data->fileio_stat.hisize >> 24);
            mc_respond(op_data->fileio_stat.hisize >> 16);
            mc_respond(op_data->fileio_stat.hisize >> 8);
            mc_respond(op_data->fileio_stat.hisize);

            mmceman_transfer_stage = 0;
            ps2_mmceman_set_cb(NULL);

            mc_respond(op_data->rv);

            if (op_data->fd > 0) {
                ps2_mmceman_fs_signal_operation(MMCEMAN_FS_CLOSE);
                ps2_mmceman_fs_wait_ready();
            }

            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_lseek64)(void)
{
    uint8_t cmd;
    uint8_t *offset8 = NULL;
    uint8_t *position8 = NULL;

    MP_CMD_START();
    mmceman_op_in_progress = true;

    ps2_mmceman_fs_wait_ready();
    op_data = ps2_mmceman_fs_get_op_data();

    offset8 = (uint8_t*)&op_data->offset64;
    position8 = (uint8_t*)&op_data->position64; //Not sure casting to 64 bit is good here....

    op_data->offset64 = 0;
    op_data->whence64 = 0;
    op_data->position64 = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x7]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x6]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x5]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x4]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);

    mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->whence64);

    log(LOG_INFO, "%s: fd: %i, whence: %u, offset: %llu\n", __func__, op_data->fd, op_data->whence, (long long unsigned int)op_data->offset);

    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_VALIDATE_FD);
    ps2_mmceman_fs_wait_ready();

    if (op_data->rv == -1) {
        log(LOG_ERROR, "%s: bad fd: %i, abort\n", __func__, op_data->fd);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(term);
        mmceman_op_in_progress = false;
        return;
    }

    MP_SIGNAL_OP();
    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_LSEEK64);
    ps2_mmceman_fs_wait_ready();

    mc_respond(position8[0x7]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x6]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x5]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x4]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log(LOG_INFO, "%s: position: %llu\n", __func__, (long long unsigned int)op_data->position);

    mc_respond(term);

    mmceman_op_in_progress = false;
    MP_CMD_END();
}

//Used only by MMCEDRV atm
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

    switch(mmceman_transfer_stage) {
        case 0:
            MP_CMD_START();
            mmceman_op_in_progress = true;

            ps2_mmceman_fs_wait_ready();
            op_data = ps2_mmceman_fs_get_op_data();

            //Clear values used in this transfer
            op_data->bytes_transferred = 0x0;
            op_data->bytes_read = 0;
            op_data->tail_idx = 0;
            op_data->head_idx = 0;

            sector = 0;
            count  = 0;

            sector8 = (uint8_t*)&sector;
            count8  = (uint8_t*)&count;

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved byte
            mc_respond(0x0); receiveOrNextCmd((uint8_t*)&op_data->fd); //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x0]);

            offset = ((uint64_t)sector) * 2048;

            //chunk read ahead, skip seeking
            if (op_data->read_ahead.fd == op_data->fd && op_data->read_ahead.valid && op_data->read_ahead.pos == offset) {

                log(LOG_INFO, "%s: fd: %i, got valid read ahead, skipping seek\n", __func__, op_data->fd);

                //Mark as consumed
                op_data->read_ahead.valid = 0;
                op_data->bytes_read = CHUNK_SIZE;
                op_data->use_read_ahead = 1;
            } else {
                //NOTE: Heavy fragmentation can result in long seek times and sometimes failed seeks altogether
                log(LOG_INFO, "%s: fd: %i, seeking to offset %llu\n", __func__, op_data->fd, (long long unsigned int)offset);

                for (int i = 0; i < 3; i++) {
                    op_data->offset64 = offset;
                    op_data->whence64 = 0;
                    MP_SIGNAL_OP();
                    ps2_mmceman_fs_signal_operation(MMCEMAN_FS_LSEEK64);
                    ps2_mmceman_fs_wait_ready();
                    if (op_data->position64 != op_data->offset64) {
                        log(LOG_ERROR, "[FATAL] Sector seek failed, possible fragmentation issues, check card! Got: 0x%llu, Exp: 0x%llu\n", op_data->position64, offset);
                    } else {
                        break;
                    }
                }
            }

            mc_respond(0x0); receiveOrNextCmd(&count8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x0]);

            op_data->length = count * 2048;

            MP_SIGNAL_OP();
            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_READ);

            log(LOG_INFO, "%s: sector: %u, count: %u, length: %u\n", __func__, sector, count, op_data->length);

            if (op_data->use_read_ahead != 1) {

                while(op_data->chunk_state[op_data->tail_idx] != CHUNK_STATE_READY) {

                    //Set by /CS high INTR, catch timeout condition
                    if (mmceman_timeout_detected)
                        return;

                    log(LOG_TRACE, "w: %u s:%u\n", op_data->tail_idx, op_data->chunk_state[op_data->tail_idx]);

                    //Reading ahead failed to get requested chunks
                    if (op_data->chunk_state[op_data->tail_idx] == CHUNK_STATE_INVALID) {
                        log(LOG_ERROR, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        op_data->chunk_state[op_data->tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        mmceman_op_in_progress = false;
                        return;             //Abort
                    }

                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_mmceman_queue_tx(op_data->buffer[op_data->tail_idx][0]);

            //We already have a chunk ahead, no need to wait
            } else {
                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_mmceman_queue_tx(op_data->read_ahead.buffer[0]);
            }

            ps2_mmceman_set_cb(&ps2_mmceman_cmd_fs_read_sector);

            mmceman_transfer_stage = 1;

            mc_respond(0x0);
        break;

        case 1:
            receiveOrNextCmd(&cmd); //Padding

            op_data->bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = op_data->length - op_data->bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            //If we're using the read ahead buffer instead of the ring buffer, avoid incrementing tail idx for now
            if (op_data->use_read_ahead == 1) {
                next_chunk = op_data->tail_idx;
            } else {
                next_chunk = op_data->tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;
            }

            //Using chunk from read ahead buffer
            if (op_data->use_read_ahead == 1) {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(op_data->read_ahead.buffer[i]);
                }

                last_byte = op_data->read_ahead.buffer[bytes_left_in_packet];

            //Use chunk from ring buffer
            } else {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(op_data->buffer[op_data->tail_idx][i]);
                }

                last_byte = op_data->buffer[op_data->tail_idx][bytes_left_in_packet];
            }

            //Check if there's more chunks after this
            if ((bytes_left_in_packet + op_data->bytes_transferred) < op_data->length) {

                //Wait for next chunk to be available before ending this transfer
                while(op_data->chunk_state[next_chunk] != CHUNK_STATE_READY && op_data->transfer_failed != 1) {

                    //Set by /CS high INTR, catch timeout condition
                    if (mmceman_timeout_detected)
                        return;

                    log(LOG_TRACE, "w: %u s:%u\n", next_chunk, op_data->chunk_state[next_chunk]);

                    if (op_data->chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                        log(LOG_ERROR, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        op_data->transfer_failed = 1;
                    }
                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_mmceman_queue_tx(op_data->buffer[next_chunk][0]);
            }

            //Update transferred count
            op_data->bytes_transferred += bytes_left_in_packet;

            //Using read ahead buffer
            if (op_data->use_read_ahead) {
                op_data->use_read_ahead = 0;
                op_data->read_ahead.valid = 0;

                log(LOG_TRACE, "ra c, bip: %u\n", (bytes_left_in_packet + 1));

            //Using ring buffer
            } else {
                //Enter crit and mark chunk as consumed
                critical_section_enter_blocking(&mmceman_fs_crit);
                op_data->chunk_state[op_data->tail_idx] = CHUNK_STATE_NOT_READY;
                critical_section_exit(&mmceman_fs_crit);

                log(LOG_TRACE, "%u c, bip: %u\n", op_data->tail_idx, (bytes_left_in_packet + 1));

                //Update tail idx
                op_data->tail_idx = next_chunk;
            }

            //If there aren't anymore chunks left after this, move to final transfer stage
            if (op_data->bytes_transferred == op_data->length)
                mmceman_transfer_stage = 2;

            //Send last byte of packet and end current transfer
            mc_respond(last_byte);
        break;

        case 2:
            receiveOrNextCmd(&cmd); //Padding

            //Get sectors read count
            op_data->bytes_read = op_data->bytes_read / 2048;

            log(LOG_INFO, "Sectors read %u of %u\n", op_data->bytes_read, op_data->length/2048);

            count8 = (uint8_t*)&op_data->bytes_read;

            //Sectors read
            mc_respond(count8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x0]); receiveOrNextCmd(&cmd);

            ps2_mmceman_fs_signal_operation(MMCEMAN_FS_READ_AHEAD);

            ps2_mmceman_set_cb(NULL);

            mmceman_transfer_stage = 0;
            mc_respond(term);

            mmceman_op_in_progress = false;
            MP_CMD_END();
        break;
    }
}