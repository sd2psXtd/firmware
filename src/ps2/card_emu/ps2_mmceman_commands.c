#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ps2_cardman.h"
#include "ps2_memory_card.h"
#include "ps2_mc_internal.h"

#include "ps2_mmceman.h"
#include "ps2_mmceman_commands.h"

#include "game_names/game_names.h"

#include "debug.h"

#include <sd.h>
#include <sys/_default_fcntl.h>

#include "mmce_fs/ps2_mmce_fs.h"

#include "ps2_mmceman_debug.h"

//TODO: temp global values, find them a home
static int transfer_stage = 0;
volatile ps2_mmce_fs_data_t *data = NULL;

//#define DEBUG_SD2PSXMAN_PROTOCOL

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_ping)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(0x1); receiveOrNextCmd(&cmd); //protocol version
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product ID
    mc_respond(0x1); receiveOrNextCmd(&cmd); //product revision number
    mc_respond(term);
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_PING\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_status)(void)
{
    uint8_t cmd;
    //TODO
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_GET_STATUS\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_card)(void)
{
    uint8_t cmd;
    int card = ps2_cardman_get_idx();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(card >> 8);   receiveOrNextCmd(&cmd); //card upper 8 bits
    mc_respond(card & 0xff); receiveOrNextCmd(&cmd); //card lower 8 bits
    mc_respond(term);
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_GET_CARD\n");
#endif
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
#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_SET_CARD mode: %i, num: %i\n", mmceman_mode, mmceman_cnum);
#endif
    
    mmceman_cmd = MMCEMAN_CMD_SET_CARD;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_channel)(void)
{
    uint8_t cmd;
    int chan = ps2_cardman_get_channel();
    mc_respond(0x0);         receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(chan >> 8);   receiveOrNextCmd(&cmd); //channel upper 8 bits
    mc_respond(chan & 0xff); receiveOrNextCmd(&cmd); //channel lower 8 bits
    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_GET_CHANNEL\n");
#endif
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

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_SET_CHANNEL mode: %i, num: %i\n", mmceman_mode, mmceman_cnum);
#endif

    mmceman_cmd = MMCEMAN_CMD_SET_CHANNEL;  //set after setting mode and cnum
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_get_gameid)(void)
{
    uint8_t cmd;
    uint8_t gameid_len = strlen(mmceman_gameid) + 1; //+1 null terminator
    mc_respond(0x0);        receiveOrNextCmd(&cmd);    //reserved byte
    mc_respond(gameid_len); receiveOrNextCmd(&cmd);    //gameid length

    for (int i = 0; i < gameid_len; i++) {
        mc_respond(mmceman_gameid[i]); receiveOrNextCmd(&cmd); //gameid
    }

    for (int i = 0; i < (250 - gameid_len); i++) {
        mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
    }

    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_GET_GAMEID\n");
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_set_gameid)(void)
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
        ps2_mmceman_set_gameid(sanitized_game_id);
        mmceman_cmd = MMCEMAN_CMD_SET_GAMEID;
    }

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received MMCEMAN_CMD_SET_GAMEID len %i, id: %s\n", gameid_len, sanitized_game_id);
#endif
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_unmount_bootcard)(void)
{
    uint8_t cmd;
    mc_respond(0x0); receiveOrNextCmd(&cmd); //reserved byte
    mc_respond(term);

#ifdef DEBUG_SD2PSXMAN_PROTOCOL
    debug_printf("received SD2PSXMAN_UNMOUNT_BOOTCARD\n");
#endif
    mmceman_cmd = SD2PSXMAN_UNMOUNT_BOOTCARD;
}
inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_open)(void)
{
    uint8_t cmd;
    uint8_t packed_flags;

    int idx = 0;

    switch(transfer_stage)
    {
        //Packet #1: Command and flags
        case 0:
            MP_CMD_START();
            ps2_mmce_fs_wait_ready(); //Wait for file handling to be ready

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&packed_flags);   //File flags
            
            fs_op_data.flags  = (packed_flags & 3);          //O_RDONLY, O_WRONLY, O_RDWR
            fs_op_data.flags |= (packed_flags & 8) << 5;     //O_APPEND
            fs_op_data.flags |= (packed_flags & 0xE0) << 4;  //O_CREATE, O_TRUNC, O_EXCL

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
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log_info(1, "%s: name: %s flags: 0x%x\n", __func__, (const char*)fs_op_data.buffer, fs_op_data.flags);

            MP_SIGNAL_OP();
            //Signal op in core0 (ps2_mmce_fs_run)
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_OPEN); 
        break;

        //Packet #3: File descriptor and termination byte
        case 2:
            receiveOrNextCmd(&cmd);   //Padding
            ps2_mmce_fs_wait_ready(); //Wait ready up to 1s

            mc_respond(fs_op_data.fd);  receiveOrNextCmd(&cmd);

            ps2_memory_card_set_cmd_callback(NULL); //Clear callback
            transfer_stage = 0; //Clear stage
        
            log_info(1, "%s: fd: %i\n", __func__, fs_op_data.fd);

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
    
    mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved
    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);  //File descriptor

    log_info(1, "%s: fd: %i\n", __func__, fs_op_data.fd);

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_OP_CLOSE);
    ps2_mmce_fs_wait_ready();

    mc_respond(fs_op_data.rv);   //Return value

    log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

    mc_respond(term);
    
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

    switch(transfer_stage) {
        //Packet #1: File handle, length, and return value
        case 0:
            MP_CMD_START();
            ps2_mmce_fs_wait_ready(); //Wait for file handling to be ready

            //Clear values used in this transfer
            fs_op_data.bytes_transferred = 0x0;
            fs_op_data.bytes_read = 0;
            fs_op_data.tail_idx = 0;
            fs_op_data.head_idx = 0;
            
            len8 = (uint8_t*)&fs_op_data.length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);          //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);          //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);//File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);    //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);    //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);    //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);    //Len MSB - 3

            log_info(1, "%s: fd: %i, len %u\n", __func__, fs_op_data.fd, fs_op_data.length);

            //Check if fd is valid before continuing
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (fs_op_data.rv == -1) {
                log_error(1, "%s: bad fd: %i, abort\n", __func__, fs_op_data.fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            MP_SIGNAL_OP();
            //Start async continuous read on core 1
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_READ);

            //Wait for first chunk to become available before ending this transfer 
            while(fs_op_data.chunk_state[fs_op_data.tail_idx] != CHUNK_STATE_READY && fs_op_data.transfer_failed != 1) {

                log_trace(1, "w: %u s:%u\n", fs_op_data.tail_idx, fs_op_data.chunk_state[fs_op_data.tail_idx]);

                //Failed to read data
                if (fs_op_data.chunk_state[fs_op_data.tail_idx] == CHUNK_STATE_INVALID) {

                    //Failed to read ANY data
                    if (fs_op_data.rv == 0) {
                        log_error(1, "Failed to read any data for chunk\n");    
                        fs_op_data.chunk_state[fs_op_data.tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        return;             //Abort

                    //Got some data
                    } else {
                        log_warn(1, "Failed to read requested data for chunk, got: %i\n", fs_op_data.rv);
                        fs_op_data.transfer_failed = 1; //Mark this transfer as failed to skip chunk waits and proceed
                    }
                }
                sleep_us(1);
            }

            //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
            ps2_queue_tx_byte_on_reset(fs_op_data.buffer[fs_op_data.tail_idx][0]);

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
            receiveOrNextCmd(&cmd);

            //TEMP: Attempt to detect if a reset occured during a read op
            if (cmd != 0xFF) {
                log_error(1, "Detected reset during read, aborting\n");
                fs_op_data.abort = 1; //signal C0 to exit read loop and clear chunk states
                transfer_stage = 0;
                ps2_memory_card_set_cmd_callback(NULL);
                break;
            }

            fs_op_data.bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = fs_op_data.length - fs_op_data.bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            next_chunk = fs_op_data.tail_idx + 1;
            if (next_chunk > CHUNK_COUNT)
                next_chunk = 0;

            //If transfer was only 1 byte, skip to end
            if (bytes_left_in_packet != 0) {

                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(fs_op_data.buffer[fs_op_data.tail_idx][i]);
                }

                last_byte = fs_op_data.buffer[fs_op_data.tail_idx][bytes_left_in_packet];

                //Check if there's more packets after this
                if ((bytes_left_in_packet + fs_op_data.bytes_transferred) < fs_op_data.length) {

                    /* If reading data from the sdcard fails at any point, the SIO2 is still going to proceed
                     * until it has recieved the number of requested bytes. In this case, skip waiting on the
                     * next chunk(s) to be read and instead send old chunk contents. This should be okay as the 
                     * number of bytes *actually* read is sent in the last packet */

                    //Wait for next chunk to be available before ending this transfer (~2s until timeout)
                    while(fs_op_data.chunk_state[next_chunk] != CHUNK_STATE_READY && fs_op_data.transfer_failed != 1) {
                        
                        log_trace(1, "w: %u s:%u\n", next_chunk, fs_op_data.chunk_state[next_chunk]);

                        if (fs_op_data.chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                            log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                            fs_op_data.transfer_failed = 1;
                        }
                        sleep_us(1);
                    }
                    //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                    ps2_queue_tx_byte_on_reset(fs_op_data.buffer[next_chunk][0]);
                }
            }

            //Update transferred count
            fs_op_data.bytes_transferred += bytes_left_in_packet;

            //Enter crit and mark chunk as consumed
            critical_section_enter_blocking(&mmce_fs_crit);
            fs_op_data.chunk_state[fs_op_data.tail_idx] = CHUNK_STATE_NOT_READY;
            critical_section_exit(&mmce_fs_crit);

            log_trace(1, "%u c, bip: %u\n", fs_op_data.tail_idx, (bytes_left_in_packet + 1));

            //Update tail idx
            fs_op_data.tail_idx = next_chunk;

            //If there aren't anymore packet's left after this, move to final transfer stage
            if (fs_op_data.bytes_transferred == fs_op_data.length)
                transfer_stage = 2;

            //Send last byte of packet and end current transfer
            if (bytes_left_in_packet != 0)
                mc_respond(last_byte);

            break;

        //Packet #3: Bytes read
        case 2:
            receiveOrNextCmd(&cmd); //Padding

            bytes8 = (uint8_t*)&fs_op_data.bytes_read;

            mc_respond(bytes8[0x3]); receiveOrNextCmd(&cmd); //Bytes read
            mc_respond(bytes8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(bytes8[0x0]); receiveOrNextCmd(&cmd);

            fs_op_data.transfer_failed = 0; //clear fail state
            ps2_memory_card_set_cmd_callback(NULL);
        
            log_info(1, "%s: read: %u\n", __func__, fs_op_data.bytes_read);

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
    int ready = 0;

    /* Currently this is a bit different from read. It waits for a 4KB buffer to be full (or until len has been read)
    *  before starting the write to the sdcard. Once the write is in progress the PS2 will poll for completion or until
    *  a timeout is reached. Once the write is complete, the process will repeat if more data is left or send the final packet
    *  containing the number of bytes written. */
    switch(transfer_stage) {
        //Packet 1: File descriptor, length, and return value
        case 0:
            ps2_mmce_fs_wait_ready();          //Wait for file handling to be ready
            
            fs_op_data.bytes_transferred = 0x0;
            fs_op_data.tail_idx = 0;
            fs_op_data.bytes_written = 0;

            len8 = (uint8_t*)&fs_op_data.length;

            mc_respond(0x0); receiveOrNextCmd(&cmd);          //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&cmd);          //Transfer mode (not implemented)
            mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);//File descriptor
            mc_respond(0x0); receiveOrNextCmd(&len8[0x3]);    //Len MSB
            mc_respond(0x0); receiveOrNextCmd(&len8[0x2]);    //Len MSB - 1
            mc_respond(0x0); receiveOrNextCmd(&len8[0x1]);    //Len MSB - 2
            mc_respond(0x0); receiveOrNextCmd(&len8[0x0]);    //Len MSB - 3

            log_info(1, "%s: fd: %i, len %u\n", __func__, fs_op_data.fd, fs_op_data.length);

            //Check if fd is valid before continuing
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (fs_op_data.rv == -1) {
                log_error(1, "%s: bad fd: %i, abort\n", __func__, fs_op_data.fd);
                mc_respond(0x1);    //Return 1
                return;             //Abort
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_write);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2 - n: Wait ready
        case 1:
            receiveOrNextCmd(&cmd);
            ps2_mmce_fs_wait_ready();

            if (fs_op_data.bytes_transferred == fs_op_data.length)
                transfer_stage = 3;  //Move to final transfer stage
            else
                transfer_stage = 2;  //More data to write
            
            log_trace(1, "ready\n");
            mc_respond(0x1);
        break;

        //Packet #n + 1: Read bytes
        case 2:
            //Add first byte to buffer
            receiveOrNextCmd(&cmd);
            fs_op_data.buffer[fs_op_data.tail_idx][0] = cmd;
            fs_op_data.bytes_transferred++;

            //Determine bytes left in this packet
            bytes_left_in_packet = fs_op_data.length - fs_op_data.bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1;

            //Avoid trying to read more data if write len == 1
            if (bytes_left_in_packet != 0) {
                log_trace(1, "bytes left in packet: %u, bytes transferred: %u\n", bytes_left_in_packet, fs_op_data.bytes_transferred);

                //Recieve rest of bytes
                for (int i = 1; i <= bytes_left_in_packet; i++) {
                    mc_respond(0x0);
                    receiveOrNextCmd(&fs_op_data.buffer[fs_op_data.tail_idx][i]);
                }

                //Update count
                fs_op_data.bytes_transferred += bytes_left_in_packet;
            }

            //If bytes recieved == 4KB or bytes received == length
            if ((((fs_op_data.bytes_transferred) % 4096) == 0) || (fs_op_data.length == fs_op_data.bytes_transferred)) {

                 //Move back to polling stage
                transfer_stage = 1;

                //Start write to sdcard
                ps2_mmce_fs_signal_operation(MMCE_FS_OP_WRITE);

                //Reset tail idx
                fs_op_data.tail_idx = 0;
            
            //More data needed before performing actual write to sdcard
            } else {
                //Update chunk
                next_chunk = fs_op_data.tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;

                fs_op_data.tail_idx = next_chunk;
            }
        break;

        //Packet n + 2: Bytes written
        case 3:
            bytes8 = (uint8_t*)&fs_op_data.bytes_written;

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
    int32_t offset32 = 0;
    uint8_t *offset8 = NULL;
    uint8_t *position8 = NULL;

    MP_CMD_START();
    ps2_mmce_fs_wait_ready();
    
    offset8 = (uint8_t*)&offset32;
    fs_op_data.offset = 0;
    fs_op_data.whence = 0;
    fs_op_data.position = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd);        //Reserved
    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);
    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.whence);

    fs_op_data.offset = (int64_t)offset32;

    log_info(1, "%s: fd: %i, offset: %lli, whence: %u\n", __func__, fs_op_data.fd, (long long int)fs_op_data.offset, fs_op_data.whence);

    ps2_mmce_fs_signal_operation(MMCE_FS_OP_VALIDATE_FD);
    ps2_mmce_fs_wait_ready();

    //Invalid fd, send -1
    if (fs_op_data.rv == -1) {
        log_error(1, "Invalid fd\n");
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(0xff);
        mc_respond(term);
        return;
    }

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_OP_LSEEK);
    ps2_mmce_fs_wait_ready();

    position8 = (uint8_t*)&fs_op_data.position;

    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log_info(1, "%s: position %llu\n", __func__, (long long unsigned int)fs_op_data.position);

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
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log_info(1, "%s: name: %s\n", __func__, (const char*)fs_op_data.buffer);

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_REMOVE);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(fs_op_data.rv); receiveOrNextCmd(&cmd); //Return value

            log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

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
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);
        
            log_info(1, "%s: name: %s\n", __func__, (const char*)fs_op_data.buffer);
            
            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_MKDIR);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //padding
            mc_respond(fs_op_data.rv); receiveOrNextCmd(&cmd); //Return value

            log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

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
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);
        
            log_info(1, "%s: name: %s\n", __func__, (const char*)fs_op_data.buffer);

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_RMDIR);
        break;

        //Packet #3: Return value
        case 2:
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            ps2_mmce_fs_wait_ready();

            receiveOrNextCmd(&cmd); //Padding
            mc_respond(fs_op_data.rv); receiveOrNextCmd(&cmd); //Return value

            log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

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
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log_info(1, "%s: name: %s\n", __func__, (const char*)fs_op_data.buffer);

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_DOPEN);
        break;

        //Packet #3: File Descriptor
        case 2:
            receiveOrNextCmd(&cmd); //Padding
            ps2_mmce_fs_wait_ready();
            mc_respond(fs_op_data.fd);  receiveOrNextCmd(&cmd); //File descriptor
            
            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);
            
            log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

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
    
    mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved
    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);  //File descriptor
    
    log_info(1, "%s: fd: %i\n", __func__, fs_op_data.fd);

    MP_SIGNAL_OP();
    ps2_mmce_fs_signal_operation(MMCE_FS_OP_DCLOSE);
    ps2_mmce_fs_wait_ready();

    mc_respond(fs_op_data.rv);   //Return value
    
    log_info(1, "%s: rv: %i\n", __func__, fs_op_data.rv);

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

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved
            mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);  //File descriptor

            log_info(1, "%s: fd: %i\n", __func__, fs_op_data.fd);

            ps2_mmce_fs_signal_operation(MMCE_FS_OP_VALIDATE_FD);
            ps2_mmce_fs_wait_ready();

            if (fs_op_data.rv == -1) {
                log_error(1, "%s: Bad fd: %i, abort\n", __func__, fs_op_data.fd);
                mc_respond(0x1);
                return;
            }

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_DREAD);            
            ps2_mmce_fs_wait_ready();

            if (fs_op_data.rv == -1) {
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

            mc_respond(fs_op_data.fileio_stat.mode >> 24);
            mc_respond(fs_op_data.fileio_stat.mode >> 16);
            mc_respond(fs_op_data.fileio_stat.mode >> 8);
            mc_respond(fs_op_data.fileio_stat.mode);

            mc_respond(fs_op_data.fileio_stat.attr >> 24);
            mc_respond(fs_op_data.fileio_stat.attr >> 16);
            mc_respond(fs_op_data.fileio_stat.attr >> 8);
            mc_respond(fs_op_data.fileio_stat.attr);

            mc_respond(fs_op_data.fileio_stat.size >> 24);
            mc_respond(fs_op_data.fileio_stat.size >> 16);
            mc_respond(fs_op_data.fileio_stat.size >> 8);
            mc_respond(fs_op_data.fileio_stat.size);

            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.ctime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.atime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.mtime[i]);
            }
            
            mc_respond(fs_op_data.fileio_stat.hisize >> 24);
            mc_respond(fs_op_data.fileio_stat.hisize >> 16);
            mc_respond(fs_op_data.fileio_stat.hisize >> 8);
            mc_respond(fs_op_data.fileio_stat.hisize);

            transfer_stage = 2;

            mc_respond(fs_op_data.length); //Filename length
        break;

        //Packet #n + 2: Filename
        case 2:
            transfer_stage = 3;

            do {
                mc_respond(fs_op_data.buffer[0][idx++]); receiveOrNextCmd(&cmd);
            } while (fs_op_data.buffer[0][idx] != 0x0);
            
            mc_respond(0x0); //Null term
        break;

        //Packet #n + 3: Term
        case 3:
            receiveOrNextCmd(&cmd);       //Padding
            mc_respond(fs_op_data.it_fd); //iterator fd
            
            ps2_memory_card_set_cmd_callback(NULL);
            transfer_stage = 0;

            mc_respond(term); //Term
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

            mc_respond(0x0); receiveOrNextCmd(&cmd); //Reserved

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_getstat);
            transfer_stage = 1;

            mc_respond(0x0);
        break;

        //Packet #2: Name
        case 1:
            do {
                mc_respond(0x0); receiveOrNextCmd(&cmd);
                fs_op_data.buffer[0][idx++] = cmd;
            } while (cmd != 0x0);

            log_info(1, "%s: name: %s\n", __func__, (const char*)fs_op_data.buffer);

            fs_op_data.flags = 0; //RD_ONLY
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_OPEN);
            transfer_stage = 2;
        break;

        //Packet #2: io_stat_t, rv, and term
        case 2:
            receiveOrNextCmd(&cmd);     //Padding
            ps2_mmce_fs_wait_ready();   //Finish open
            
            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_GETSTAT);
            ps2_mmce_fs_wait_ready();
            
            mc_respond(fs_op_data.fileio_stat.mode >> 24);
            mc_respond(fs_op_data.fileio_stat.mode >> 16);
            mc_respond(fs_op_data.fileio_stat.mode >> 8);
            mc_respond(fs_op_data.fileio_stat.mode);

            mc_respond(fs_op_data.fileio_stat.attr >> 24);
            mc_respond(fs_op_data.fileio_stat.attr >> 16);
            mc_respond(fs_op_data.fileio_stat.attr >> 8);
            mc_respond(fs_op_data.fileio_stat.attr);

            mc_respond(fs_op_data.fileio_stat.size >> 24);
            mc_respond(fs_op_data.fileio_stat.size >> 16);
            mc_respond(fs_op_data.fileio_stat.size >> 8);
            mc_respond(fs_op_data.fileio_stat.size);

            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.ctime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.atime[i]);
            }
            for(int i = 0; i < 8; i++) {
                mc_respond(fs_op_data.fileio_stat.mtime[i]);
            }
            
            mc_respond(fs_op_data.fileio_stat.hisize >> 24);
            mc_respond(fs_op_data.fileio_stat.hisize >> 16);
            mc_respond(fs_op_data.fileio_stat.hisize >> 8);
            mc_respond(fs_op_data.fileio_stat.hisize);

            transfer_stage = 0;
            ps2_memory_card_set_cmd_callback(NULL);

            mc_respond(fs_op_data.rv);

            MP_CMD_END();

            if (fs_op_data.fd > 0) {
                ps2_mmce_fs_signal_operation(MMCE_FS_OP_CLOSE);
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
    
    offset8 = (uint8_t*)&fs_op_data.offset;
    position8 = (uint8_t*)&fs_op_data.position; //Not sure casting to 64 bit is good here....

    fs_op_data.offset = 0;
    fs_op_data.whence = 0;
    fs_op_data.position = 0;

    mc_respond(0x0); receiveOrNextCmd(&cmd); //padding
    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);

    mc_respond(0x0); receiveOrNextCmd(&offset8[0x7]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x6]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x5]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x4]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x3]);      
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x2]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x1]);
    mc_respond(0x0); receiveOrNextCmd(&offset8[0x0]);

    mc_respond(0x0); receiveOrNextCmd(&fs_op_data.whence);

    log_info(1, "%s: fd: %i, whence: %u, offset: %llu\n", __func__, fs_op_data.fd, fs_op_data.whence, (long long unsigned int)fs_op_data.offset);

    ps2_mmce_fs_signal_operation(MMCE_FS_OP_VALIDATE_FD);
    ps2_mmce_fs_wait_ready();

    if (fs_op_data.rv == -1) {
        log_error(1, "%s: bad fd: %i, abort\n", __func__, fs_op_data.fd);
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
    ps2_mmce_fs_signal_operation(MMCE_FS_OP_LSEEK);
    ps2_mmce_fs_wait_ready();

    mc_respond(position8[0x7]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x6]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x5]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x4]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x3]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x2]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x1]); receiveOrNextCmd(&cmd);
    mc_respond(position8[0x0]); receiveOrNextCmd(&cmd);

    log_info(1, "%s: position: %llu\n", __func__, (long long unsigned int)fs_op_data.position);

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
            
            //Clear values used in this transfer
            fs_op_data.bytes_transferred = 0x0;
            fs_op_data.bytes_read = 0;
            fs_op_data.tail_idx = 0;
            fs_op_data.head_idx = 0;

            sector = 0;
            count  = 0;

            sector8 = (uint8_t*)&sector;
            count8  = (uint8_t*)&count;

            mc_respond(0x0); receiveOrNextCmd(&cmd);            //Reserved byte
            mc_respond(0x0); receiveOrNextCmd(&fs_op_data.fd);  //File descriptor
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&sector8[0x0]);

            offset = ((uint64_t)sector) * 2048;

            //Data read ahead, skip seeking
            if (fs_op_data.read_ahead.fd == fs_op_data.fd && fs_op_data.read_ahead.valid && fs_op_data.read_ahead.pos == offset) {

                log_info(1, "%s: fd: %i, got valid read ahead, skipping seek\n", __func__, fs_op_data.fd);

                //Mark as consumed
                fs_op_data.read_ahead.valid = 0;
                fs_op_data.bytes_read = CHUNK_SIZE;
                fs_op_data.use_read_ahead = 1;
            } else {
                //TEMP: Retry loop. Heavy fragmentation can result in long seek times and sometimes failed seeks altogether
                //Retry if seek fails up to 3 times and print msg
                log_info(1, "%s: fd: %i, seeking to offset %llu\n", __func__, fs_op_data.fd, (long long unsigned int)offset);

                for (int i = 0; i < 3; i++) {
                    sd_seek_set_new(fs_op_data.fd, offset);
                    position = sd_tell_new(fs_op_data.fd);
                    if (position != offset) {
                        printf("[FATAL] Sector seek failed, possible fragmentation issues, check card! Got: 0x%llu, Exp: 0x%llu\n", (long long unsigned int)position, (long long unsigned int)offset);
                    } else {
                        break;
                    }
                }
            }

            mc_respond(0x0); receiveOrNextCmd(&count8[0x2]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x1]);
            mc_respond(0x0); receiveOrNextCmd(&count8[0x0]);

            fs_op_data.length = count * 2048;

            MP_SIGNAL_OP();
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_READ);

            log_info(1, "%s: sector: %u, count: %u, length: %u\n", __func__, sector, count, fs_op_data.length);

            //If there's no data read ahead
            if (fs_op_data.use_read_ahead != 1) {

                while(fs_op_data.chunk_state[fs_op_data.tail_idx] != CHUNK_STATE_READY) {

                    log_trace(1, "w: %u s:%u\n", fs_op_data.tail_idx, fs_op_data.chunk_state[fs_op_data.tail_idx]);

                    //Reading ahead failed to get requested data
                    if (fs_op_data.chunk_state[fs_op_data.tail_idx] == CHUNK_STATE_INVALID) {
                        log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        fs_op_data.chunk_state[fs_op_data.tail_idx] = CHUNK_STATE_NOT_READY;
                        mc_respond(0x1);    //Return 1
                        return;             //Abort
                    }

                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(fs_op_data.buffer[fs_op_data.tail_idx][0]);
            
            //We already have data ahead, no need to wait
            } else {
                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(fs_op_data.read_ahead.buffer[0]);
            }

            ps2_memory_card_set_cmd_callback(&ps2_mmceman_cmd_fs_read_sector);
            
            transfer_stage = 1;
            
            mc_respond(0x0);
        break;

        case 1:
            receiveOrNextCmd(&cmd);

            //TEMP: Attempt to detect if a reset occured during a read op
            if (cmd != 0xFF) {
                log_error(1, "Detected reset during read, aborting\n");
                fs_op_data.abort = 1; //signal C1 to exit read loop and clear chunk states
                transfer_stage = 0;
                ps2_memory_card_set_cmd_callback(NULL);
                break;
            }

            fs_op_data.bytes_transferred += 1; //Byte that went out on the tx fifo at the start

            bytes_left_in_packet = fs_op_data.length - fs_op_data.bytes_transferred;
            if (bytes_left_in_packet >= CHUNK_SIZE)
                bytes_left_in_packet = CHUNK_SIZE - 1; //Since 1 byte was already sent out

            //If we're using the read ahead buffer instead of the ring buffer, avoid incrementing tail idx for now
            if (fs_op_data.use_read_ahead == 1) {
                next_chunk = fs_op_data.tail_idx;
            } else {
                next_chunk = fs_op_data.tail_idx + 1;
                if (next_chunk > CHUNK_COUNT)
                    next_chunk = 0;
            }

            //Using data from read ahead buffer
            if (fs_op_data.use_read_ahead == 1) {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(fs_op_data.read_ahead.buffer[i]);
                }

                last_byte = fs_op_data.read_ahead.buffer[bytes_left_in_packet];

            //Use data from ring buffer
            } else {
                //Send up until the last byte
                for (int i = 1; i < bytes_left_in_packet; i++) {
                    mc_respond(fs_op_data.buffer[fs_op_data.tail_idx][i]);
                }

                last_byte = fs_op_data.buffer[fs_op_data.tail_idx][bytes_left_in_packet];
            }

            //Check if there's more packets after this
            if ((bytes_left_in_packet + fs_op_data.bytes_transferred) < fs_op_data.length) {

                //Wait for next chunk to be available before ending this transfer 
                while(fs_op_data.chunk_state[next_chunk] != CHUNK_STATE_READY && fs_op_data.transfer_failed != 1) {
                    
                    log_trace(1, "w: %u s:%u\n", next_chunk, fs_op_data.chunk_state[next_chunk]);

                    if (fs_op_data.chunk_state[next_chunk] == CHUNK_STATE_INVALID) {
                        log_error(1, "Failed to read chunk, got CHUNK_STATE_INVALID, aborting\n");
                        fs_op_data.transfer_failed = 1;
                    }
                    sleep_us(1);
                }

                //Place the first byte of the chunk in TX FIFO on reset to ensure proper alignment
                ps2_queue_tx_byte_on_reset(fs_op_data.buffer[next_chunk][0]);
            }

            //Update transferred count
            fs_op_data.bytes_transferred += bytes_left_in_packet;

            //Using read ahead buffer
            if (fs_op_data.use_read_ahead) {
                fs_op_data.use_read_ahead = 0;
                fs_op_data.read_ahead.valid = 0;

                log_trace(1, "ra c, bip: %u\n", (bytes_left_in_packet + 1));

            //Using ring buffer 
            } else {
                //Enter crit and mark chunk as consumed
                critical_section_enter_blocking(&mmce_fs_crit);
                fs_op_data.chunk_state[fs_op_data.tail_idx] = CHUNK_STATE_NOT_READY;
                critical_section_exit(&mmce_fs_crit);

                log_trace(1, "%u c, bip: %u\n", fs_op_data.tail_idx, (bytes_left_in_packet + 1));
                
                //Update tail idx
                fs_op_data.tail_idx = next_chunk;
            }

            //If there aren't anymore packet's left after this, move to final transfer stage
            if (fs_op_data.bytes_transferred == fs_op_data.length)
                transfer_stage = 2;

            //Send last byte of packet and end current transfer
            mc_respond(last_byte);
        break;

        case 2:
            receiveOrNextCmd(&cmd); //Padding

            //Get sectors read count
            fs_op_data.bytes_read = fs_op_data.bytes_read / 2048;

            log_info(1, "Sectors read %u of %u\n", fs_op_data.bytes_read, fs_op_data.length/2048);

            count8 = (uint8_t*)&fs_op_data.bytes_read;

            //Sectors read
            mc_respond(count8[0x2]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x1]); receiveOrNextCmd(&cmd);
            mc_respond(count8[0x0]); receiveOrNextCmd(&cmd);

            //Try to read 256 bytes ahead of time
            ps2_mmce_fs_signal_operation(MMCE_FS_OP_READ_AHEAD);

            ps2_memory_card_set_cmd_callback(NULL);
        
            transfer_stage = 0;
            mc_respond(term);
            MP_CMD_END();
        break;
    }
}

//Close all open files
inline __attribute__((always_inline)) void __time_critical_func(ps2_mmceman_cmd_fs_reset)(void)
{
    uint8_t cmd;

    mc_respond(0x0); receiveOrNextCmd(&cmd);    //Reserved
    mc_respond(0x0); receiveOrNextCmd(&cmd);    //Reserved

    ps2_mmce_fs_signal_operation(MMCE_FS_OP_RESET);
    ps2_mmce_fs_wait_ready();

    mc_respond(fs_op_data.rv);
    mc_respond(term);
}