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
#define MMCE_FS_VALIDATE_FD 0xE
#define MMCE_FS_READ_AHEAD 0xF
#define MMCE_FS_BLOCKING 0x10

#define MMCE_FS_LSEEK64 0xee

#define CHUNK_SIZE 256
#define CHUNK_COUNT 15

#define CHUNK_STATE_NOT_READY 0x0
#define CHUNK_STATE_READY 0x1
#define CHUNK_STATE_INVALID 0x2

//Single chunk read ahead on open, lseek, and after read
typedef struct ps2_mmce_fs_read_ahead_t {
    int fd;
    int valid;
    uint64_t pos;
    uint8_t buffer[CHUNK_SIZE];
} ps2_mmce_fs_read_ahead_t;

typedef struct ps2_mmce_fs_data_t {
    int rv;
    int fd;
    int flags;          //file flags
    int it_fd;          //iterator dir

    uint64_t filesize;

    int      offset;
    int      position;
    uint8_t  whence;

    int64_t  offset64;
    int64_t  position64;
    uint8_t  whence64;

    uint32_t length;            //length of transfer, read only
    uint32_t bytes_read;        //stop reading when == length
    uint32_t bytes_written;     //
    uint32_t bytes_transferred; //stop sending when == length

    uint8_t tail_idx;           //read ring tail idx
    uint8_t head_idx;           //read ring head idx

    uint8_t buffer[CHUNK_COUNT + 1][CHUNK_SIZE];
    volatile uint8_t chunk_state[CHUNK_COUNT + 1]; //written to by both cores, writes encased in critical section

    uint8_t transfer_failed;
    int use_read_ahead;
    ps2_mmce_fs_read_ahead_t read_ahead;

    ps2_fileio_stat_t fileio_stat;
} ps2_mmce_fs_data_t;

extern critical_section_t mmce_fs_crit; //used to lock writes to chunk_state (sd2psxman_commands <-> ps2_mmceman_fs)

/* Flow (Core 1):
 * enter cmd handler function
 * ps2_mmce_fs_wait_ready();               core1 waits for core0 to finish any ops (shouldn't be any)
 * write necessary data to mmce_fs_data_t
 * ps2_mmce_fs_signal_op(int op);          signal core0 to perform op
 * ps2_mmce_fs_wait_ready();               wait for op to be completed
 *          OR
 * ps2_mmce_fs_is_ready();                 polled by PS2 ready packet (used only with writes)
 *          OR
 * poll chunk state or other status var (read and dread do this)
 * access data
 * repeat
*/

//Core 0
bool ps2_mmce_fs_idle(void);
void ps2_mmce_fs_init(void);
void ps2_mmce_fs_run(void);

//Core 1
void ps2_mmce_fs_wait_ready();
void ps2_mmce_fs_signal_operation(int op);

ps2_mmce_fs_data_t *ps2_mmce_fs_get_data(void);
