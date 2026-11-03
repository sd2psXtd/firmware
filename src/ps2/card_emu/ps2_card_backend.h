#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {

    /**
     * @brief Initialize the memory card backend and allocate resources
     */
    void (*init)(void);

    /**
     * @brief Deinitialize the memory card backend and free resources
     */
    void (*deinit)(void);

    /**
     * @brief End current transfer and reset the backend state
     */
    void (*reset)(void);

    void (*shutdown)(void);

    uint8_t (*receive)(uint8_t *cmd);
    uint8_t (*receiveFirst)(uint8_t *cmd);
    void    (*respond)(uint8_t ch);

    void (*startup)(void);

    bool (*probe)(void);
} ps2_memory_card_backend_t;


enum { PS2_RECEIVE_RESET, PS2_RECEIVE_EXIT, PS2_RECEIVE_OK };


extern volatile bool ps2_card_active;
extern volatile int ps2_reset;
extern volatile int ps2_mc_exit_request;

#define receiveOrNextCmd(cmd)          \
    if (ps2_card_backend->receive(cmd) == PS2_RECEIVE_RESET) {\
    DPRINTF("Reset at %s:%u", __func__, __LINE__); \
    return;}


extern ps2_memory_card_backend_t* ps2_card_backend_get(void);
