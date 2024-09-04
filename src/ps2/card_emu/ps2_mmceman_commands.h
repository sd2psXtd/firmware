#pragma once

#define MMCEMAN_CMD_ID 0x8B
#define SD2PSXMAN_UNMOUNT_BOOTCARD 0x30

//Card commands
enum mmceman_cmds {
    MMCEMAN_CMD_PING = 0x1,
    MMCEMAN_CMD_GET_STATUS,
    MMCEMAN_CMD_GET_CARD,
    MMCEMAN_CMD_SET_CARD,
    MMCEMAN_CMD_GET_CHANNEL,
    MMCEMAN_CMD_SET_CHANNEL,
    MMCEMAN_CMD_GET_GAMEID,
    MMCEMAN_CMD_SET_GAMEID,
};

//FS commands
enum mmceman_cmds_fs {
    MMCEMAN_CMD_FS_OPEN = 0x40,
    MMCEMAN_CMD_FS_CLOSE = 0x41,
    MMCEMAN_CMD_FS_READ = 0x42,
    MMCEMAN_CMD_FS_WRITE = 0x43,
    MMCEMAN_CMD_FS_LSEEK = 0x44,
    MMCEMAN_CMD_FS_IOCTL = 0x45,
    MMCEMAN_CMD_FS_REMOVE = 0x46,
    MMCEMAN_CMD_FS_MKDIR = 0x47,
    MMCEMAN_CMD_FS_RMDIR = 0x48,
    MMCEMAN_CMD_FS_DOPEN = 0x49,
    MMCEMAN_CMD_FS_DCLOSE = 0x4a,
    MMCEMAN_CMD_FS_DREAD = 0x4b,
    MMCEMAN_CMD_FS_GETSTAT = 0x4c,
    MMCEMAN_CMD_FS_CHSTAT = 0x4d,
    MMCEMAN_CMD_FS_LSEEK64 = 0x53,
    MMCEMAN_CMD_FS_READ_SECTOR = 0x58,
};

enum mmceman_modes{
    MMCEMAN_MODE_NUM = 0x0,
    MMCEMAN_MODE_NEXT,
    MMCEMAN_MODE_PREV,
};

extern void ps2_mmceman_cmd_ping(void);
extern void ps2_mmceman_cmd_get_status(void);
extern void ps2_mmceman_cmd_get_card(void);
extern void ps2_mmceman_cmd_set_card(void);
extern void ps2_mmceman_cmd_get_channel(void);
extern void ps2_mmceman_cmd_set_channel(void);
extern void ps2_mmceman_cmd_get_gameid(void);
extern void ps2_mmceman_cmd_set_gameid(void);
extern void ps2_mmceman_cmd_unmount_bootcard(void);

extern void ps2_mmceman_cmd_fs_open(void);
extern void ps2_mmceman_cmd_fs_close(void);
extern void ps2_mmceman_cmd_fs_read(void);
extern void ps2_mmceman_cmd_fs_write(void);
extern void ps2_mmceman_cmd_fs_lseek(void);
extern void ps2_mmceman_cmd_fs_remove(void);
extern void ps2_mmceman_cmd_fs_mkdir(void);
extern void ps2_mmceman_cmd_fs_rmdir(void);
extern void ps2_mmceman_cmd_fs_dclose(void);
extern void ps2_mmceman_cmd_fs_dopen(void);
extern void ps2_mmceman_cmd_fs_dread(void);
extern void ps2_mmceman_cmd_fs_getstat(void);
extern void ps2_mmceman_cmd_fs_lseek64(void);

extern void ps2_mmceman_cmd_fs_read_sector(void);