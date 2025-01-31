#pragma once

#define PS2_MMCEMAN_CMD_IDENTIFIER    0x8B

#define MMCEMAN_PING 0x1
#define MMCEMAN_GET_STATUS 0x2
#define MMCEMAN_GET_CARD 0x3
#define MMCEMAN_SET_CARD 0x4
#define MMCEMAN_GET_CHANNEL 0x5
#define MMCEMAN_SET_CHANNEL 0x6
#define MMCEMAN_GET_GAMEID 0x7
#define MMCEMAN_SET_GAMEID 0x8
#define MMCEMAN_RESET 0x9

//TEMP
#define MMCEMAN_SWITCH_BOOTCARD 0x20
#define MMCEMAN_UNMOUNT_BOOTCARD 0x30

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

#define MMCEMAN_CMD_FS_LSEEK64 0x53

#define MMCEMAN_CMD_FS_READ_SECTOR 0x58

#define MMCEMAN_MODE_NUM 0x0
#define MMCEMAN_MODE_NEXT 0x1
#define MMCEMAN_MODE_PREV 0x2

extern void ps2_mmceman_cmd_ping(void);
extern void ps2_mmceman_cmd_get_status(void);
extern void ps2_mmceman_cmd_get_card(void);
extern void ps2_mmceman_cmd_set_card(void);
extern void ps2_mmceman_cmd_get_channel(void);
extern void ps2_mmceman_cmd_set_channel(void);
extern void ps2_mmceman_cmd_get_gameid(void);
extern void ps2_mmceman_cmd_set_gameid(void);
extern void ps2_mmceman_cmd_unmount_bootcard(void);
extern void ps2_mmceman_cmd_reset(void);

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