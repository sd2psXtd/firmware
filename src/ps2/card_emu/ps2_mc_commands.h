#pragma once

#define PS2_SIO2_CMD_IDENTIFIER         0x81
#define PS1_SIO2_CMD_IDENTIFIER         0x01

#define PS2_SIO2_CMD_0x11               0x11
#define PS2_SIO2_CMD_0x12               0x12
#define PS2_SIO2_CMD_SET_ERASE_ADDRESS  0x21
#define PS2_SIO2_CMD_SET_WRITE_ADDRESS  0x22
#define PS2_SIO2_CMD_SET_READ_ADDRESS   0x23
#define PS2_SIO2_CMD_GET_SPECS          0x26
#define PS2_SIO2_CMD_SET_TERMINATOR     0x27
#define PS2_SIO2_CMD_GET_TERMINATOR     0x28
#define PS2_SIO2_CMD_WRITE_DATA         0x42
#define PS2_SIO2_CMD_READ_DATA          0x43
#define PS2_SIO2_CMD_COMMIT_DATA        0x81
#define PS2_SIO2_CMD_ERASE              0x82
#define PS2_SIO2_CMD_BF                 0xBF
#define PS2_SIO2_CMD_F3                 0xF3
#define PS2_SIO2_CMD_KEY_SELECT         0xF7
#define PS2_SIO2_CMD_AUTH               0xF0
#define PS2_SIO2_CMD_SESSION_KEY_0      0xF1
#define PS2_SIO2_CMD_SESSION_KEY_1      0xF2


extern void ps2_mc_cmd_0x11(void);
extern void ps2_mc_cmd_0x12(void);
extern void ps2_mc_cmd_setEraseAddress(void);
extern void ps2_mc_cmd_setWriteAddress(void);
extern void ps2_mc_cmd_setReadAddress(void);
extern void ps2_mc_cmd_getSpecs(void);
extern void ps2_mc_cmd_setTerminator(void);
extern void ps2_mc_cmd_getTerminator(void);
extern void ps2_mc_cmd_writeData(void);
extern void ps2_mc_cmd_readData(void);
extern void ps2_mc_cmd_commitData(void);
extern void ps2_mc_cmd_erase(void);
extern void ps2_mc_cmd_0xBF(void);
extern void ps2_mc_cmd_0xF3(void);

