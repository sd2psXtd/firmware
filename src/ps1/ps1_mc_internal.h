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

    uint8_t (*recv_cntrl)(uint8_t *cmd);
    uint8_t (*recv_mc)(uint8_t *cmd);
    void    (*respond)(uint8_t ch);

    void (*startup)(void);
} ps1_memory_card_backend_t;

enum { PS1_RECEIVE_RESET, PS1_RECEIVE_EXIT, PS1_RECEIVE_OK };

extern volatile int ps1_mc_reset, ps1_mc_exit_request;
extern volatile bool ps1_card_active;
