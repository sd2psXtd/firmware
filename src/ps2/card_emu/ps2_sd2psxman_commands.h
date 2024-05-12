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