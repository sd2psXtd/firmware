#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/multicore.h"
#include "pico/critical_section.h"
#include "pico/mutex.h"

#include "sd.h"

//TODO: enumify
#define MMCE_FS_NONE 0x0
#define MMCE_FS_OPEN 0x1
#define MMCE_FS_CLOSE 0x2
#define MMCE_FS_READ 0x3
#define MMCE_FS_READ_AHEAD 0x33
#define MMCE_FS_WRITE 0x4
#define MMCE_FS_LSEEK 0x5
#define MMCE_FS_IOCTL 0x6
#define MMCE_FS_REMOVE 0x7
#define MMCE_FS_MKDIR 0x8
#define MMCE_FS_RMDIR 0x9
#define MMCE_FS_DOPEN 0xA
#define MMCE_FS_DCLOSE 0xB
#define MMCE_FS_DREAD 0xC
#define MMCE_FS_GETSTAT 0xD

#define CHUNK_SIZE 256
#define CHUNK_READ_AHEAD_COUNT 15

#define CHUNK_STATE_NOT_READY 0x0
#define CHUNK_STATE_READY 0x1
#define CHUNK_STATE_INVALID 0x2

typedef struct ps2_mmce_fs_data_t {
    int rv;
    int fd;
    int flags;          //file flags
    int it_fd;          //iterater dir
    int last_read_fd;   //last fd read from

    uint32_t filesize;

    int      offset;
    uint8_t  whence;
    uint32_t position;

    uint32_t length;            //length of transfer, read only
    uint32_t bytes_read;        //stop reading ahead when == length + CHUNK_SIZE OR if length + CHUNK_SIZE > file size
    uint32_t bytes_transferred; //stop sending when == length
    int      bytes_read_ahead;  //how many bytes passed length have been read, used to correct over reading

    uint8_t tail_idx;           //read
    uint8_t head_idx;           //read ahead, head idx
    
    uint8_t buffer[CHUNK_READ_AHEAD_COUNT + 1][CHUNK_SIZE];
    volatile uint8_t chunk_state[CHUNK_READ_AHEAD_COUNT + 1]; //written to by both cores, writes encased in critical section

    ps2_fileio_stat_t fileio_stat;
} ps2_mmce_fs_data_t;

extern critical_section_t mmce_fs_crit; //used to lock writes to chunk_state (sd2psxman_commands <-> ps2_mmceman_fs)

/* Flow (Core 0):
 * enter cmd handler function
 * ps2_mmce_fs_wait_ready();               core0 waits for core1 to finish any ops (shouldn't be any)
 * ps2_mmce_fs_get_data();                 returns ptr to mmce_fs_data_t
 * write necessary data to mmce_fs_data_t
 * ps2_mmce_fs_signal_op(int op);          signal core1 to perform op
 * ps2_mmce_fs_wait_ready();               wait for op to be completed (Mostly unused now in favor of polling)
 *          OR
 * ps2_mmce_fs_is_ready();                 polled by PS2 ready packet
 *          OR
 * poll pull chunk state or other status var (read and dread do this)
 * access data
 * repeat
*/

//Core 1
void ps2_mmce_fs_init(void);
void ps2_mmce_fs_run(void);

//Core 0
void ps2_mmce_fs_wait_ready();
int ps2_mmce_fs_is_ready(void);
void ps2_mmce_fs_signal_operation(int op);

ps2_mmce_fs_data_t *ps2_mmce_fs_get_data(void);
