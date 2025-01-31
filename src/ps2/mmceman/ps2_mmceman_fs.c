#include "ps2_mmceman_fs.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/_default_fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "pico/multicore.h"
#include "pico/time.h"
#include "sd.h"

#include "debug.h"
#include "ps2_cardman.h"
#include "ps2_mmceman.h"
#include "ps2_mmceman_debug.h"
#include "card_emu/ps2_mc_internal.h"

#if LOG_LEVEL_MMCEMAN_FS == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_MMCEMAN_FS, level, fmt, ##x)
#endif

//Global data struct
static volatile ps2_mmceman_fs_op_data_t op_data;
static volatile uint32_t mmceman_fs_operation;
critical_section_t mmceman_fs_crit;

void ps2_mmceman_fs_init(void)
{
    op_data.rv = 0;
    op_data.fd = 0;
    //op_data.it_fd = 0;

    memset(op_data.it_fd, -1, 16);
    op_data.flags = 0;

    op_data.filesize = 0;

    op_data.offset = 0;
    op_data.whence = 0;
    op_data.position = 0;

    op_data.length = 0;
    op_data.bytes_read = 0;
    op_data.bytes_transferred = 0;

    op_data.head_idx = 0;
    op_data.tail_idx = 0;

    op_data.read_ahead.fd = -1;
    op_data.read_ahead.valid = 0;

    op_data.transfer_failed = 0;

    memset((void*)op_data.chunk_state, 0, sizeof(op_data.chunk_state));

    if (!mmceman_fs_crit.spin_lock)
        critical_section_init(&mmceman_fs_crit);

    mmceman_fs_operation = MMCEMAN_FS_NONE;
}

bool ps2_mmceman_fs_idle(void)
{
    return (mmceman_fs_operation == MMCEMAN_FS_NONE);
}

void ps2_mmceman_fs_run(void)
{
    uint32_t bytes_in_chunk = 0;
    uint32_t write_size = 0;

    MP_OP_START();

    switch (mmceman_fs_operation) {
        case MMCEMAN_FS_OPEN:
            op_data.fd = sd_open((const char*)op_data.buffer[0], op_data.flags);

            if (op_data.fd < 0) {
                log(LOG_ERROR, "Open failed, fd: %i\n", op_data.fd);
            }

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_CLOSE:
            op_data.rv = sd_close(op_data.fd);

            //Discard data read ahead from file
            if (op_data.fd == op_data.read_ahead.fd) {
                op_data.read_ahead.fd = -1;
                op_data.read_ahead.valid = 0;
            }

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        //Read async continuous until bytes_read == length
        case MMCEMAN_FS_READ:
            log(LOG_INFO, "Entering read loop, bytes read: %u len: %u\n", op_data.bytes_read, op_data.length);
            mmceman_fs_abort_read = false;

            //Read requested length
            while (op_data.bytes_read < op_data.length)
            {
                if (mmceman_fs_abort_read) {
                    log(LOG_WARN, "Caught MMCEMAN FS abort read!\n");
                    //Clear chunk states
                    memset((void*)op_data.chunk_state, 0, sizeof(op_data.chunk_state));
                    break;
                }

                //Wait for chunk at head to be consumed
                if (op_data.chunk_state[op_data.head_idx] != CHUNK_STATE_READY) {

                    //Get number of bytes to try reading
                    bytes_in_chunk = (op_data.length - op_data.bytes_read);

                    //Cap at CHUNK_SIZE
                    if (bytes_in_chunk > CHUNK_SIZE)
                        bytes_in_chunk = CHUNK_SIZE;

                    //Read
                    op_data.rv = sd_read(op_data.fd, (void*)op_data.buffer[op_data.head_idx], bytes_in_chunk);

                    //Failed to get requested amount
                    if (op_data.rv != (int)bytes_in_chunk) {
                        op_data.bytes_read += op_data.rv;
                        log(LOG_ERROR, "Failed to read %u bytes, got %i bytes\n", bytes_in_chunk, op_data.rv);
                        critical_section_enter_blocking(&mmceman_fs_crit);
                        op_data.chunk_state[op_data.head_idx] = CHUNK_STATE_INVALID; //Notify core0
                        critical_section_exit(&mmceman_fs_crit);
                        break;
                    }

                    //Update read count
                    op_data.bytes_read += op_data.rv;

                    //Enter crit and update chunk state
                    critical_section_enter_blocking(&mmceman_fs_crit);
                    op_data.chunk_state[op_data.head_idx] = CHUNK_STATE_READY;
                    critical_section_exit(&mmceman_fs_crit);

                    log(LOG_TRACE, "%u r, bic %u\n", op_data.head_idx, bytes_in_chunk);

                    //Increment head pointer
                    op_data.head_idx++;

                    //Loop around
                    if (op_data.head_idx > CHUNK_COUNT)
                        op_data.head_idx = 0;

                    sleep_us(1);
                }
            }

            log(LOG_INFO, "Exit read loop\n");
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        /* Try to read a single chunk ahead into a separate buffer */
        case MMCEMAN_FS_READ_AHEAD:
            op_data.filesize = sd_filesize64(op_data.fd);

            log(LOG_INFO, "Entering read ahead\n");

            //Check if reading beyond file size
            if (sd_tell64(op_data.fd) + CHUNK_SIZE <= op_data.filesize) {
                op_data.read_ahead.pos = sd_tell64(op_data.fd);
                op_data.rv = sd_read(op_data.fd, (void*)op_data.read_ahead.buffer, CHUNK_SIZE);

                if (op_data.rv == CHUNK_SIZE) {
                    log(LOG_INFO, "Read ahead: %i\n", op_data.rv);
                    op_data.read_ahead.fd = op_data.fd;
                    op_data.read_ahead.valid = 1;
                } else {
                    log(LOG_ERROR, "Failed to read ahead %i bytes, got %i\n", CHUNK_SIZE, op_data.rv);
                }
            } else {
                log(LOG_WARN, "Skipping request to read ahead beyond file length\n");
            }

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_WRITE:
            write_size = op_data.bytes_transferred % 4096;
            if (write_size == 0)
                write_size = 4096;

            log(LOG_INFO, "Writing: %u to sd\n", write_size);
            op_data.rv = sd_write(op_data.fd, (void*)op_data.buffer[0], write_size);
            sd_flush(op_data.fd); //flush data

            op_data.bytes_written += op_data.rv;
            log(LOG_INFO, "Wrote: %i, progress: %u of %u\n", op_data.rv, op_data.bytes_written, op_data.length);

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_LSEEK:
            //If we're seeking on a file that has data read ahead
            if ((op_data.fd == op_data.read_ahead.fd) && (op_data.read_ahead.valid == 1)) {

                //SEEK_CUR - adjust offset
                if (op_data.whence == 1) {
                    DPRINTF("C1: Correcting SEEK_CUR offset: %i\n", op_data.offset);
                    op_data.offset -= CHUNK_SIZE;
                    DPRINTF("C1: New offset: %i\n", op_data.offset);
                }

                //Invalidate data read ahead
                op_data.read_ahead.valid = 0;
            }

            sd_seek(op_data.fd, op_data.offset, op_data.whence);
            op_data.position = sd_tell(op_data.fd);

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_LSEEK64:
            //If we're seeking on a file that has data read ahead
            if ((op_data.fd == op_data.read_ahead.fd) && (op_data.read_ahead.valid == 1)) {

                //SEEK_CUR - adjust offset
                if (op_data.whence64 == 1) {
                    DPRINTF("C1: Correcting SEEK_CUR offset: %lli\n", op_data.offset64);
                    op_data.offset64 -= CHUNK_SIZE;
                    DPRINTF("C1: New offset: %lli\n", op_data.offset64);
                }

                //Invalidate data read ahead
                op_data.read_ahead.valid = 0;
            }

            sd_seek64(op_data.fd, op_data.offset64, op_data.whence64);
            op_data.position64 = sd_tell64(op_data.fd);

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_REMOVE:
            op_data.rv = sd_remove((const char*)op_data.buffer[0]);
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_MKDIR:
            op_data.rv = sd_mkdir((const char*)op_data.buffer[0]);
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_RMDIR:
            op_data.rv = sd_rmdir((const char*)op_data.buffer[0]);
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_DOPEN:
            op_data.fd = sd_open((const char*)op_data.buffer[0], 0x0);
            op_data.it_fd[op_data.fd] = -1; //clear itr stat
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_DCLOSE:
            if (op_data.it_fd[op_data.fd] > 0) {
                sd_close(op_data.it_fd[op_data.fd]); //if iterated on
                op_data.it_fd[op_data.fd] = -1;
            }

            if (op_data.fd > 0) {
                op_data.rv = sd_close(op_data.fd);
                op_data.fd = -1;
            }
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_DREAD:
            //TODO: rework dir iteration
            op_data.it_fd[op_data.fd] = sd_iterate_dir(op_data.fd, op_data.it_fd[op_data.fd]);

            if (op_data.it_fd[op_data.fd] != -1) {
                sd_get_stat(op_data.it_fd[op_data.fd], (ps2_fileio_stat_t*)&op_data.fileio_stat);
                op_data.length = sd_get_name(op_data.it_fd[op_data.fd], (char*)&op_data.buffer[0], 128);

                op_data.length++;
                op_data.buffer[0][op_data.length] = '\0'; //add null term

            } else {
                op_data.rv = -1;
            }

            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_GETSTAT:
            if (op_data.fd > 0) {
                sd_get_stat(op_data.fd, (ps2_fileio_stat_t*)&op_data.fileio_stat);
                op_data.rv = 0;
            } else {
                op_data.rv = 1;
            }
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_VALIDATE_FD:
            op_data.rv = sd_fd_is_open(op_data.fd);
            mmceman_fs_operation = MMCEMAN_FS_NONE;
        break;

        case MMCEMAN_FS_RESET:
            //SD max files
            for (int i = 0; i < 16; i++) {
                if (i != cardman_fd) {
                    sd_close(i);
                }
            }

            memset(op_data.it_fd, -1, 16);

            //Reset mmceman_fs
            ps2_mmceman_fs_init();

            log(LOG_INFO, "MMCEMAN FS Reset\n");
        break;

        default:
        break;
    }

    MP_OP_END();
}

//Core 1
void ps2_mmceman_fs_wait_ready(void)
{
    while ((mmceman_fs_operation != MMCEMAN_FS_NONE))
    {
        sleep_us(1);
    }
}

int ps2_mmceman_fs_get_operation(void)
{
    return mmceman_fs_operation;
}

void ps2_mmceman_fs_signal_operation(int op)
{
    mmceman_fs_operation = op;
}

ps2_mmceman_fs_op_data_t *ps2_mmceman_fs_get_op_data(void)
{
    return (ps2_mmceman_fs_op_data_t*)&op_data;
}