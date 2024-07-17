#include "ps2_mmce_fs.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "sd.h"
#include <stdbool.h>
#include "debug.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/_default_fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define DPRINTF(fmt, x...) printf(fmt, ##x)
//#define DPRINTF(x...) 

//Global data struct
static volatile ps2_mmce_fs_data_t m_data;
static volatile uint32_t mmce_fs_operation;
critical_section_t mmce_fs_crit;

void ps2_mmce_fs_init(void)
{
    m_data.rv = 0;
    m_data.fd = 0;
    m_data.it_fd = 0;
    m_data.flags = 0;
    
    m_data.filesize = 0;

    m_data.offset = 0;
    m_data.whence = 0;
    m_data.position = 0;

    m_data.length = 0;
    m_data.bytes_read = 0;
    m_data.bytes_transferred = 0;

    m_data.head_idx = 0;
    m_data.tail_idx = 0;

    m_data.read_ahead.fd = -1;
    m_data.read_ahead.valid = 0;

    memset(m_data.chunk_state, 0, sizeof(m_data.chunk_state));

    critical_section_init(&mmce_fs_crit);

    mmce_fs_operation = MMCE_FS_NONE;
}

void ps2_mmce_fs_run(void)
{
    int rv = 0;
    uint16_t bytes_in_chunk = 0;
    uint32_t write_size = 0;

    switch (mmce_fs_operation) {
        case MMCE_FS_OPEN:
            m_data.fd = sd_open(m_data.buffer[0], m_data.flags);

            if (m_data.fd < 0) {
                DPRINTF("Open failed fd: %i\n", m_data.fd);
            }

            mmce_fs_operation = MMCE_FS_NONE;
        break;
        
        case MMCE_FS_CLOSE:
            m_data.rv = sd_close(m_data.fd);

            //Discard data read ahead from file
            if (m_data.fd == m_data.read_ahead.fd) {
                m_data.read_ahead.fd = -1;
                m_data.read_ahead.valid = 0;
            }

            mmce_fs_operation = MMCE_FS_NONE;
        break;

        //Read async continuous until bytes_read == length
        case MMCE_FS_READ:
            DPRINTF("C1: Entering read loop, bytes read: %i len: %i\n", m_data.bytes_read, m_data.length);

            //Get filesize ahead of time
            m_data.filesize = sd_filesize(m_data.fd);

            //Read requested length
            while (m_data.bytes_read < m_data.length)
            {
                //Wait for chunk at head to be consumed
                if (m_data.chunk_state[m_data.head_idx] != CHUNK_STATE_READY) {

                    //Get number of bytes to try reading
                    bytes_in_chunk = (m_data.length - m_data.bytes_read);

                    //Cap at CHUNK_SIZE
                    if (bytes_in_chunk > CHUNK_SIZE)
                        bytes_in_chunk = CHUNK_SIZE;

                    //Check if reading beyond file size
                    if (sd_tell(m_data.fd) + bytes_in_chunk > m_data.filesize) {
                        DPRINTF("C1: Skipping request to read beyond file length\n");                        
                        critical_section_enter_blocking(&mmce_fs_crit);
                        m_data.chunk_state[m_data.head_idx] = CHUNK_STATE_NOT_READY;
                        critical_section_exit(&mmce_fs_crit);
                        break;
                    }

                    //Read
                    rv = sd_read(m_data.fd, m_data.buffer[m_data.head_idx], bytes_in_chunk);

                    //Failed to get requested amount
                    if (rv != bytes_in_chunk) {
                        DPRINTF("C1: Failed to read %i bytes, got %i\n", bytes_in_chunk, rv);
                        critical_section_enter_blocking(&mmce_fs_crit);
                        m_data.chunk_state[m_data.head_idx] = CHUNK_STATE_INVALID; //Notify core0
                        critical_section_exit(&mmce_fs_crit);
                        break;
                    }

                    //Update read count
                    m_data.bytes_read += rv;

                    //Enter crit and update chunk state
                    critical_section_enter_blocking(&mmce_fs_crit);
                    m_data.chunk_state[m_data.head_idx] = CHUNK_STATE_READY;
                    critical_section_exit(&mmce_fs_crit);

                    DPRINTF("C1: %i ready, bytes in this chunk %i\n", m_data.head_idx, bytes_in_chunk);

                    //Increment head pointer
                    m_data.head_idx++;

                    //Loop around
                    if (m_data.head_idx > CHUNK_COUNT)
                        m_data.head_idx = 0;

                    sleep_us(1);
                }
            }

            m_data.filesize = 0;

            mmce_fs_operation = MMCE_FS_NONE;
        break;
    
        /* Try to read a single chunk ahead into a separate buffer
         * Done after open, lseek, and read */
        case MMCE_FS_READ_AHEAD:

            m_data.filesize = sd_filesize(m_data.fd);
            DPRINTF("Entering read ahead\n");
            //Check if reading beyond file size
            if (sd_tell(m_data.fd) + CHUNK_SIZE <= m_data.filesize) {
                rv = sd_read(m_data.fd, m_data.read_ahead.buffer, CHUNK_SIZE);

                if (rv == CHUNK_SIZE) {
                    DPRINTF("C1: Read ahead: %i\n", rv);
                    m_data.read_ahead.fd = m_data.fd;
                    m_data.read_ahead.valid = 1;
                } else {
                    DPRINTF("C1: Failed to read ahead %i bytes, got %i\n", CHUNK_SIZE, rv);
                }
            } else {
                DPRINTF("C1: Skipping request to read ahead beyond file length\n");
            }

            mmce_fs_operation = MMCE_FS_NONE;
        break;

        case MMCE_FS_WRITE:
            write_size = m_data.bytes_transferred % 4096;
            if (write_size == 0)
                write_size = 4096;

            DPRINTF("C1: Writing: %i\n", write_size);
            m_data.rv = sd_write(m_data.fd, m_data.buffer[0], write_size);
            sd_flush(m_data.fd); //flush data
            DPRINTF("C1: Wrote: %i\n", m_data.rv);

            mmce_fs_operation = MMCE_FS_NONE;
        break;
        
        case MMCE_FS_LSEEK:
            //If we're seeking on a file that has data read ahead
            if ((m_data.fd == m_data.read_ahead.fd) && (m_data.read_ahead.valid == 1)) {

                //SEEK_CUR - adjust offset 
                if (m_data.whence == 1) {
                    DPRINTF("C1: Correcting SEEK_CUR offset: %i\n", m_data.offset);
                    m_data.offset -= CHUNK_SIZE;
                    DPRINTF("C1: New offset: %i\n", m_data.offset);
                }

                //Invalidate data read ahead
                m_data.read_ahead.valid = 0;
            }

            sd_seek_new(m_data.fd, m_data.offset, m_data.whence);
            m_data.position = sd_tell(m_data.fd);

            mmce_fs_operation = MMCE_FS_NONE;
        break;
        
        case MMCE_FS_REMOVE:
            m_data.rv = sd_remove(m_data.buffer[0]);
            mmce_fs_operation = MMCE_FS_NONE;
        break;

        case MMCE_FS_MKDIR:
            m_data.rv = sd_mkdir(m_data.buffer[0]);
            mmce_fs_operation = MMCE_FS_NONE;
        break;

        case MMCE_FS_RMDIR:
            m_data.rv = sd_rmdir(m_data.buffer[0]);
            mmce_fs_operation = MMCE_FS_NONE;
        break;
        
        case MMCE_FS_DOPEN:
            m_data.fd = sd_open(m_data.buffer[0], 0x0);
            m_data.it_fd = -1; //clear itr stat
            mmce_fs_operation = MMCE_FS_NONE;
        break;
        
        case MMCE_FS_DCLOSE:
            if (m_data.it_fd > 0) {
                sd_close(m_data.it_fd); //if iterated on
                m_data.it_fd = -1;
            }

            if (m_data.fd > 0) {
                m_data.rv = sd_close(m_data.fd);
                m_data.fd = -1;
            }
            mmce_fs_operation = MMCE_FS_NONE;
        break;

        case MMCE_FS_DREAD:
            if (sd_fd_is_open(m_data.fd) != -1) {
                m_data.it_fd = sd_iterate_dir(m_data.fd, m_data.it_fd);

                if (m_data.it_fd != -1) {
                    m_data.rv = 0; //set return value before performing get_stat

                    sd_get_stat(m_data.it_fd, &m_data.fileio_stat);
                    m_data.length = sd_get_name(m_data.it_fd, &m_data.buffer[0], 128);

                    m_data.length++;
                    m_data.buffer[0][m_data.length] = '\0'; //add null term

                } else {
                    m_data.rv = -1;
                }
            } else {
                m_data.rv = -1;
            }

            mmce_fs_operation = MMCE_FS_NONE;
        break;

        case MMCE_FS_VALIDATE_FD:
            m_data.rv = sd_fd_is_open(m_data.fd);
            mmce_fs_operation = MMCE_FS_NONE;
        break;

        default:
        break;
    }
}

//Core 0
void ps2_mmce_fs_wait_ready(void)
{
    while (mmce_fs_operation != MMCE_FS_NONE)
    {
        sleep_us(1);
    }
}

//Polling approach
int ps2_mmce_fs_is_ready(void)
{
    if (mmce_fs_operation == MMCE_FS_NONE)
        return 1;
    else
        return 0;
}

void ps2_mmce_fs_signal_operation(int op)
{
    mmce_fs_operation = op;
}

ps2_mmce_fs_data_t *ps2_mmce_fs_get_data(void)
{
    return &m_data;
}