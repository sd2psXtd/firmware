#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/multicore.h"
#include "pico/critical_section.h"
#include "pico/mutex.h"

#include "sd.h"

//TODO: enumify
#define MMCEMAN_FS_NONE 0x0
#define MMCEMAN_FS_OPEN 0x1
#define MMCEMAN_FS_CLOSE 0x2
#define MMCEMAN_FS_READ 0x3
#define MMCEMAN_FS_WRITE 0x4
#define MMCEMAN_FS_LSEEK 0x5
#define MMCEMAN_FS_IOCTL 0x6
#define MMCEMAN_FS_REMOVE 0x7
#define MMCEMAN_FS_MKDIR 0x8
#define MMCEMAN_FS_RMDIR 0x9
#define MMCEMAN_FS_DOPEN 0xA
#define MMCEMAN_FS_DCLOSE 0xB
#define MMCEMAN_FS_DREAD 0xC
#define MMCEMAN_FS_GETSTAT 0xD
#define MMCEMAN_FS_VALIDATE_FD 0xE
#define MMCEMAN_FS_READ_AHEAD 0xF
#define MMCEMAN_FS_LSEEK64 0x10

#define MMCEMAN_FS_RESET 0x11

#define CHUNK_SIZE 256
#define CHUNK_COUNT 15

#define CHUNK_STATE_NOT_READY 0x0
#define CHUNK_STATE_READY 0x1
#define CHUNK_STATE_INVALID 0x2

//Single chunk read ahead on open, lseek, and after read
typedef struct ps2_mmceman_fs_read_ahead_t {
    int fd;
    int valid;
    uint64_t pos;
    uint8_t buffer[CHUNK_SIZE];
} ps2_mmceman_fs_read_ahead_t;

typedef struct ps2_mmceman_fs_op_data_t {
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
    ps2_mmceman_fs_read_ahead_t read_ahead;

    ps2_fileio_stat_t fileio_stat;
} ps2_mmceman_fs_op_data_t;

extern critical_section_t mmceman_fs_crit; //used to lock writes to chunk_state (mmceman_commands <-> ps2_mmceman_fs)

/* Flow (Core 1):
 * enter cmd handler function
 * ps2_mmceman_fs_wait_ready();               core1 waits for core0 to finish any ops (shouldn't be any)
 * write necessary data to mmce_fs_data_t
 * ps2_mmceman_fs_signal_op(int op);          signal core0 to perform op
 * ps2_mmceman_fs_wait_ready();               wait for op to be completed
 *          OR
 * ps2_mmceman_fs_is_ready();                 polled by PS2 ready packet (used only with writes)
 *          OR
 * poll chunk state or other status var (read and dread do this)
 * access data
 * repeat
*/

//Core 0
bool ps2_mmceman_fs_idle(void);
void ps2_mmceman_fs_init(void);
void ps2_mmceman_fs_run(void);

//Core 1
void ps2_mmceman_fs_wait_ready();
void ps2_mmceman_fs_signal_operation(int op);
int ps2_mmceman_fs_get_operation();


ps2_mmceman_fs_op_data_t *ps2_mmceman_fs_get_op_data(void);
