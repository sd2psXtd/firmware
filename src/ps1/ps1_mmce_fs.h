#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pico/critical_section.h"

#define PS1_MMCE_FS_BLOCK_SIZE        256U
#define PS1_MMCE_FS_PROTOCOL_VERSION  0x0100U
#define PS1_MMCE_FS_BUFFER_COUNT      16U
#define PS1_MMCE_FS_MAX_READ_BLOCKS   4096U
#define PS1_MMCE_FS_MAX_READ_TRANSFER (PS1_MMCE_FS_BLOCK_SIZE * PS1_MMCE_FS_MAX_READ_BLOCKS)
#define PS1_MMCE_FS_MAX_TRANSFER      4096U /* WRITE_FILE maximum */
#define PS1_MMCE_FS_MAX_PATH          240U
#define PS1_MMCE_FS_MAX_HANDLES       4U
/* Full FAT/exFAT UTF-8 name scratch space, before enforcing MAX_PATH. */
#define PS1_MMCE_FS_NATIVE_NAME_MAX 768U

#define PS1_MMCE_FS_BUFFER_NOT_READY 0x0U
#define PS1_MMCE_FS_BUFFER_READY     0x1U
#define PS1_MMCE_FS_BUFFER_INVALID   0x2U

typedef enum {
    PS1_MMCE_FS_RESULT_OK = 0x00,
    PS1_MMCE_FS_RESULT_EOF = 0x01,
    PS1_MMCE_FS_RESULT_UNSUPPORTED = 0x02,
    PS1_MMCE_FS_RESULT_INVALID = 0x03,
    PS1_MMCE_FS_RESULT_BAD_HANDLE = 0x04,
    PS1_MMCE_FS_RESULT_ACCESS = 0x05,
    PS1_MMCE_FS_RESULT_NOT_FOUND = 0x06,
    PS1_MMCE_FS_RESULT_EXISTS = 0x07,
    PS1_MMCE_FS_RESULT_NO_SPACE = 0x08,
    PS1_MMCE_FS_RESULT_IO_ERROR = 0x09,
    PS1_MMCE_FS_RESULT_NAME_TOO_LONG = 0x0A,
    PS1_MMCE_FS_RESULT_BAD_CRC = 0x0B,
    PS1_MMCE_FS_RESULT_NO_HANDLES = 0x0C,
    PS1_MMCE_FS_RESULT_NOT_EMPTY = 0x0D,
    PS1_MMCE_FS_RESULT_RANGE = 0x0E,
    PS1_MMCE_FS_RESULT_INTERNAL = 0xFF,
} ps1_mmce_fs_result_t;

typedef enum {
    PS1_MMCE_FS_OP_NONE = 0x0,
    PS1_MMCE_FS_OP_OPEN_FILE = 0x1,
    PS1_MMCE_FS_OP_CLOSE_FILE = 0x2,
    PS1_MMCE_FS_OP_READ = 0x3,
    PS1_MMCE_FS_OP_WRITE = 0x4,
    PS1_MMCE_FS_OP_SEEK_FILE = 0x5,
    PS1_MMCE_FS_OP_REMOVE_FILE = 0x7,
    PS1_MMCE_FS_OP_CREATE_DIR = 0x8,
    PS1_MMCE_FS_OP_REMOVE_DIR = 0x9,
    PS1_MMCE_FS_OP_RENAME = 0xA,
    PS1_MMCE_FS_OP_OPEN_DIR = 0xB,
    PS1_MMCE_FS_OP_READ_DIR = 0xC,
    PS1_MMCE_FS_OP_CLOSE_DIR = 0xD,
} ps1_mmce_fs_operation_t;

/* Shared parameter/result/data block. Core 1 writes the request fields while
   Core 0 is idle, then signals the operation; Core 0 executes the blocking SD
   I/O and stores results back. Buffer states are touched by both cores under
   ps1_mmce_fs_crit. */
typedef struct ps1_mmce_fs_op_data_t {
    int result; /* ps1_mmce_fs_result_t */

    uint8_t handle; /* in: public handle; out: published handle for opens */
    uint8_t flags;
    uint8_t whence;
    int64_t offset;
    uint32_t length; /* requested transfer length */

    uint32_t bytes_read;    /* Core 0 read progress */
    uint32_t bytes_written; /* Core 0 write progress */

    uint64_t position;

    uint8_t entry_type;
    uint64_t entry_size;
    uint16_t name_length;
    char name[PS1_MMCE_FS_NATIVE_NAME_MAX];

    char path[2][PS1_MMCE_FS_MAX_PATH];

    /* Read ring with PS1_MMCE_FS_BUFFER_COUNT entries. Core 0 fills buffers at head_idx, Core 1
       drains them at tail_idx; buffer_length[i] is the number of valid bytes
       in buffer[i] (may be short on EOF). */
    uint8_t buffer[PS1_MMCE_FS_BUFFER_COUNT][PS1_MMCE_FS_BLOCK_SIZE];
    uint16_t buffer_length[PS1_MMCE_FS_BUFFER_COUNT];
    volatile uint8_t buffer_state[PS1_MMCE_FS_BUFFER_COUNT]; /* critical section */
    uint8_t head_idx;
    uint8_t tail_idx;
} ps1_mmce_fs_op_data_t;

extern critical_section_t ps1_mmce_fs_crit;

/* Flow (Core 1):
 * enter cmd handler function
 * ps1_mmce_fs_is_ready();                    core 1 must not signal while an
 *                                            operation is still running
 * write necessary data to ps1_mmce_fs_op_data_t
 * ps1_mmce_fs_signal_operation(op);          signal Core 0 to perform the op
 * respond BUSY and end the SIO transfer      Storage runs on Core 0; Core 1
 *                                            only performs bounded read lookahead
 * client re-issues 0x28 + FS_POLL until the op is ready or buffers are
 * available, then the result/data is streamed from RAM
 */

// Core 0
bool ps1_mmce_fs_idle(void);
void ps1_mmce_fs_init(void);
void ps1_mmce_fs_run(void);
void ps1_mmce_fs_reset(void);

// Core 1
bool ps1_mmce_fs_is_ready(void);
void ps1_mmce_fs_signal_operation(int op);
void ps1_mmce_fs_abort(void);
/* Complete delivery of a read before later aborts can rewind its origin. */
void ps1_mmce_fs_finish_read(void);
void ps1_mmce_fs_mark_handle_delivered(uint8_t handle);
ps1_mmce_fs_op_data_t* ps1_mmce_fs_get_op_data(void);
