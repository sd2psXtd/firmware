#include "ps2_mmce_fs.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "pico/multicore.h"
#include "pico/time.h"
#include "sd.h"

#include "../card_emu/ps2_mmceman_debug.h"

//Global data struct
volatile ps2_mmce_fs_data_t fs_op_data;
static volatile uint32_t mmce_fs_op_dataeration;
critical_section_t mmce_fs_crit;

void ps2_mmce_fs_init(void)
{
    fs_op_data.rv = 0;
    fs_op_data.fd = 0;
    fs_op_data.it_fd = 0;
    fs_op_data.flags = 0;
    
    fs_op_data.filesize = 0;

    fs_op_data.offset = 0;
    fs_op_data.whence = 0;
    fs_op_data.position = 0;

    fs_op_data.length = 0;
    fs_op_data.bytes_read = 0;
    fs_op_data.bytes_written = 0;
    fs_op_data.bytes_transferred = 0;

    fs_op_data.head_idx = 0;
    fs_op_data.tail_idx = 0;

    fs_op_data.read_ahead.fd = -1;
    fs_op_data.read_ahead.valid = 0;

    fs_op_data.transfer_failed = 0;

    memset((void*)fs_op_data.chunk_state, 0, sizeof(fs_op_data.chunk_state));

    critical_section_init(&mmce_fs_crit);
    mmce_fs_op_dataeration = MMCE_FS_OP_NONE;
}

void ps2_mmce_fs_run(void)
{
    int rv = 0;
    uint32_t bytes_in_chunk = 0;
    uint32_t write_size = 0;

    MP_OP_START();

    switch (mmce_fs_op_dataeration) {
        case MMCE_FS_OP_OPEN:
            fs_op_data.fd = sd_open((const char*)fs_op_data.buffer[0], fs_op_data.flags);
            if (fs_op_data.fd < 0) {
                log_error(0, "Open failed, fd: %i\n", fs_op_data.fd);
            }
        break;

        case MMCE_FS_OP_CLOSE:
            fs_op_data.rv = sd_close(fs_op_data.fd);

            //Discard data read ahead from file
            if (fs_op_data.fd == fs_op_data.read_ahead.fd) {
                fs_op_data.read_ahead.fd = -1;
                fs_op_data.read_ahead.valid = 0;
            }
        break;

        //Read continuously until bytes_read == length or abort
        case MMCE_FS_OP_READ:

            log_info(0, "Entering read loop, bytes read: %u len: %u\n", fs_op_data.bytes_read, fs_op_data.length);

            //Read requested length
            while (fs_op_data.bytes_read < fs_op_data.length)
            {
                if (fs_op_data.abort) {
                    log_error(0, "Got abort flag from C1");
                    fs_op_data.abort = 0;
                    critical_section_enter_blocking(&mmce_fs_crit);
                    memset(fs_op_data.chunk_state, CHUNK_STATE_NOT_READY, CHUNK_COUNT);
                    critical_section_exit(&mmce_fs_crit);
                }

                //Wait for chunk at head to be consumed
                if (fs_op_data.chunk_state[fs_op_data.head_idx] != CHUNK_STATE_READY) {

                    //Get number of bytes to try reading
                    bytes_in_chunk = (fs_op_data.length - fs_op_data.bytes_read);

                    //Cap at CHUNK_SIZE
                    if (bytes_in_chunk > CHUNK_SIZE)
                        bytes_in_chunk = CHUNK_SIZE;

                    //Read
                    fs_op_data.rv = sd_read(fs_op_data.fd, (void*)fs_op_data.buffer[fs_op_data.head_idx], bytes_in_chunk);

                    //Failed to get requested amount
                    if (fs_op_data.rv != bytes_in_chunk) {
                        fs_op_data.bytes_read += fs_op_data.rv;
                        log_error(0, "Failed to read %u bytes, got %i bytes\n", bytes_in_chunk, fs_op_data.rv);
                        critical_section_enter_blocking(&mmce_fs_crit);
                        fs_op_data.chunk_state[fs_op_data.head_idx] = CHUNK_STATE_INVALID; //Notify core1
                        critical_section_exit(&mmce_fs_crit);
                        break;
                    }

                    //Update read count
                    fs_op_data.bytes_read += fs_op_data.rv;

                    //Enter crit and update chunk state
                    critical_section_enter_blocking(&mmce_fs_crit);
                    fs_op_data.chunk_state[fs_op_data.head_idx] = CHUNK_STATE_READY;
                    critical_section_exit(&mmce_fs_crit);

                    log_trace(0, "%u r, bic %u\n", fs_op_data.head_idx, bytes_in_chunk);

                    //Increment head pointer
                    fs_op_data.head_idx++;

                    //Loop around
                    if (fs_op_data.head_idx > CHUNK_COUNT)
                        fs_op_data.head_idx = 0;

                    sleep_us(1);
                }
            }

            log_info(0, "Exit read loop\n");            
        break;
    
        /* Try to read a single chunk ahead into a separate buffer */
        case MMCE_FS_OP_READ_AHEAD:
            fs_op_data.filesize = sd_filesize_new(fs_op_data.fd);

            log_info(0, "Entering read ahead\n");

            //Check if reading beyond file size
            if (sd_tell_new(fs_op_data.fd) + CHUNK_SIZE <= fs_op_data.filesize) {
                fs_op_data.read_ahead.pos = sd_tell_new(fs_op_data.fd);
                fs_op_data.rv = sd_read(fs_op_data.fd, (void*)fs_op_data.read_ahead.buffer, CHUNK_SIZE);

                if (fs_op_data.rv == CHUNK_SIZE) {
                    log_trace(0, "Read ahead: %i\n", fs_op_data.rv);
                    fs_op_data.read_ahead.fd = fs_op_data.fd;
                    fs_op_data.read_ahead.valid = 1;
                } else {
                    log_error(0, "Failed to read ahead %i bytes, got %i\n", CHUNK_SIZE, fs_op_data.rv);
                }
            } else {
                log_warn(0, "Skipping request to read ahead beyond file length\n");
            }
        break;

        case MMCE_FS_OP_WRITE:
            write_size = fs_op_data.bytes_transferred % 4096;
            if (write_size == 0)
                write_size = 4096;

            log_info(0, "Writing: %u to sd\n", write_size);

            fs_op_data.rv = sd_write(fs_op_data.fd, (void*)fs_op_data.buffer[0], write_size);
            sd_flush(fs_op_data.fd); //flush data
            
            fs_op_data.bytes_written += fs_op_data.rv;

            log_info(0, "Wrote: %i, progress: %u of %u\n", fs_op_data.rv, fs_op_data.bytes_written, fs_op_data.length);
        break;

        case MMCE_FS_OP_LSEEK:
            sd_seek_new(fs_op_data.fd, fs_op_data.offset, fs_op_data.whence);
            fs_op_data.position = sd_tell_new(fs_op_data.fd);
        break;

        case MMCE_FS_OP_REMOVE:
            fs_op_data.rv = sd_remove((const char*)fs_op_data.buffer[0]);
        break;

        case MMCE_FS_OP_MKDIR:
            fs_op_data.rv = sd_mkdir((const char*)fs_op_data.buffer[0]);
        break;

        case MMCE_FS_OP_RMDIR:
            fs_op_data.rv = sd_rmdir((const char*)fs_op_data.buffer[0]);
        break;
        
        case MMCE_FS_OP_DOPEN:
            fs_op_data.fd = sd_open((const char*)fs_op_data.buffer[0], 0x0);
            fs_op_data.it_fd = -1; //clear itr stat
        break;

        case MMCE_FS_OP_DCLOSE:
            if (fs_op_data.it_fd > 0) {
                sd_close(fs_op_data.it_fd); //if iterated on
                fs_op_data.it_fd = -1;
            }

            if (fs_op_data.fd > 0) {
                fs_op_data.rv = sd_close(fs_op_data.fd);
                fs_op_data.fd = -1;
            }
        break;

        case MMCE_FS_OP_DREAD:
            fs_op_data.it_fd = sd_iterate_dir(fs_op_data.fd, fs_op_data.it_fd);

            if (fs_op_data.it_fd != -1) {
                sd_get_stat(fs_op_data.it_fd, (ps2_fileio_stat_t*)&fs_op_data.fileio_stat);
                fs_op_data.length = sd_get_name(fs_op_data.it_fd, (char*)&fs_op_data.buffer[0], 256);

                fs_op_data.length++;
                fs_op_data.buffer[0][fs_op_data.length] = '\0'; //add null term
            } else {
                fs_op_data.rv = -1;
            }
        break;

        case MMCE_FS_OP_GETSTAT:
            if (fs_op_data.fd > 0) {
                sd_get_stat(fs_op_data.fd, (ps2_fileio_stat_t*)&fs_op_data.fileio_stat);
                fs_op_data.rv = 0;
            } else {
                fs_op_data.rv = 1;
            }
        break;

        case MMCE_FS_OP_VALIDATE_FD:
            fs_op_data.rv = sd_fd_is_open(fs_op_data.fd);
        break;

        case MMCE_FS_OP_RESET:
            //TODO:
            for (int i = 0; i < NUM_FILES; i++) {
                if (sd_fd_is_open(i))
                    sd_close(i);

                ps2_mmce_fs_init();
                fs_op_data.rv = 0;
            }
        break;

        default:
        break;
    }
    
    MP_OP_END();
    mmce_fs_op_dataeration = MMCE_FS_OP_NONE;
}

//Core 1
void ps2_mmce_fs_wait_ready(void)
{
    while (mmce_fs_op_dataeration != MMCE_FS_OP_NONE)
    {
        sleep_us(1);
    }
}

//Polling approach
int ps2_mmce_fs_is_ready(void)
{
    if (mmce_fs_op_dataeration == MMCE_FS_OP_NONE)
        return 1;
    else
        return 0;
}

void ps2_mmce_fs_signal_operation(int op)
{
    mmce_fs_op_dataeration = op;
}

ps2_mmce_fs_data_t *ps2_mmce_fs_get_data(void)
{
    return (ps2_mmce_fs_data_t*)&fs_op_data;
}