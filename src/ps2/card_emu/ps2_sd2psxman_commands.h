#pragma once

#define PS2_SD2PSXMAN_CMD_IDENTIFIER    0x8B

#define SD2PSXMAN_PING 0x1
#define SD2PSXMAN_GET_STATUS 0x2
#define SD2PSXMAN_GET_CARD 0x3
#define SD2PSXMAN_SET_CARD 0x4
#define SD2PSXMAN_GET_CHANNEL 0x5
#define SD2PSXMAN_SET_CHANNEL 0x6
#define SD2PSXMAN_GET_GAMEID 0x7
#define SD2PSXMAN_SET_GAMEID 0x8

#define SD2PSXMAN_UNMOUNT_BOOTCARD 0x30

//Implemented:
#define MMCEMAN_CMD_FS_OPEN 0x40
#define MMCEMAN_CMD_FS_CLOSE 0x41
#define MMCEMAN_CMD_FS_READ 0x42
#define MMCEMAN_CMD_FS_WRITE 0x43
#define MMCEMAN_CMD_FS_LSEEK 0x44
#define MMCEMAN_CMD_FS_REMOVE 0x46
#define MMCEMAN_CMD_FS_MKDIR 0x47
#define MMCEMAN_CMD_FS_RMDIR 0x48
#define MMCEMAN_CMD_FS_DOPEN 0x49
#define MMCEMAN_CMD_FS_DCLOSE 0x4a
#define MMCEMAN_CMD_FS_DREAD 0x4b
#define MMCEMAN_CMD_FS_GETSTAT 0x4c

#define SD2PSXMAN_MODE_NUM 0x0
#define SD2PSXMAN_MODE_NEXT 0x1
#define SD2PSXMAN_MODE_PREV 0x2

extern void ps2_sd2psxman_cmds_ping(void);
extern void ps2_sd2psxman_cmds_get_status(void);
extern void ps2_sd2psxman_cmds_get_card(void);
extern void ps2_sd2psxman_cmds_set_card(void);
extern void ps2_sd2psxman_cmds_get_channel(void);
extern void ps2_sd2psxman_cmds_set_channel(void);
extern void ps2_sd2psxman_cmds_get_gameid(void);
extern void ps2_sd2psxman_cmds_set_gameid(void);
extern void ps2_sd2psxman_cmds_unmount_bootcard(void);
extern void ps2_sd2psxman_cmds_set_path(void);

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