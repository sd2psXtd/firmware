#pragma once

#include <stdint.h>

/* MMCE memory-card command 0x28 subcommands. Core 1 decodes parameters and resumes the active command on POLL while
   ps1_mmce_fs.c owns the corresponding filesystem operation on Core 0. */
typedef enum {
    PS1_MMCE_FS_CMD_GET_CAPABILITIES = 0x00,
    PS1_MMCE_FS_CMD_OPEN_FILE = 0x01,
    PS1_MMCE_FS_CMD_CLOSE_FILE = 0x02,
    PS1_MMCE_FS_CMD_READ_FILE = 0x03,
    PS1_MMCE_FS_CMD_WRITE_FILE = 0x04,
    PS1_MMCE_FS_CMD_SEEK_FILE = 0x05,
    PS1_MMCE_FS_CMD_REMOVE_FILE = 0x06,
    PS1_MMCE_FS_CMD_CREATE_DIR = 0x07,
    PS1_MMCE_FS_CMD_REMOVE_DIR = 0x08,
    PS1_MMCE_FS_CMD_RENAME = 0x09,
    PS1_MMCE_FS_CMD_OPEN_DIR = 0x0A,
    PS1_MMCE_FS_CMD_READ_DIR = 0x0B,
    PS1_MMCE_FS_CMD_CLOSE_DIR = 0x0C,
    /* Resumes a staged command that answered BUSY in a previous transfer. */
    PS1_MMCE_FS_CMD_POLL = 0x0D,
    PS1_MMCE_FS_CMD_ABORT = 0x0E,
} ps1_mmce_fs_command_t;

/* Core 1 entry point for memory-card command 0x28. */
void ps1_mmce_fs_command(void);

/* Discards the staged session and requests an underlying Core 0 abort. This
   is for actual memory-card core exit/remount boundaries, not ordinary /CS
   rises between params and FS_POLL transfers. */
void ps1_mmce_fs_commands_abort(void);
