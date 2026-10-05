#pragma once


#include <stdbool.h>
#include <stdint.h>

#define MMCE_PS1_GAME_ID     (1U)
#define MMCE_PS1_NXT_CH      (2U)
#define MMCE_PS1_PRV_CH      (3U)
#define MMCE_PS1_NXT_CARD    (4U)
#define MMCE_PS1_PRV_CARD    (5U)
#define MMCE_PS1_SWITCH_BOOTCARD (6U)
#define MMCE_PS1_SWITCH_DEFAULT  (7U)
#define MMCE_PS1_SET_CARD    (8U)
#define MMCE_PS1_SET_CHANNEL (9U)
#define MMCE_PS1_RESET       (10U)

typedef struct {
    uint32_t offset;
    uint32_t sm;
} pio_t;


void ps1_memory_card_main(void);
void ps1_memory_card_enter(void);
void ps1_memory_card_exit(void);
void ps1_memory_card_unload(void);

uint8_t ps1_memory_card_get_ode_command(void);
void ps1_memory_card_reset_ode_command(void);
const char* ps1_memory_card_get_game_id(void);

/* Shared Core 1 transport for memory-card and controller command handlers. */
enum { RECEIVE_RESET, RECEIVE_EXIT, RECEIVE_OK };

typedef enum {
    PS1_MC_READER,
    PS1_CONTROLLER_READER,
} ps1_mc_receiver_t;

uint8_t recv_cmd(uint8_t* cmd, ps1_mc_receiver_t receiver);
void ps1_mc_respond(uint8_t response);
bool ps1_memory_card_sio_active(void);

#define recv_mc(cmd)    recv_cmd(cmd, PS1_MC_READER)
#define recv_cntrl(cmd) recv_cmd(cmd, PS1_CONTROLLER_READER)
