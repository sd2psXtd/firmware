#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mca_bridge.h"
#include "mca_device.h"

/* Raw PS2 page (512 data + 16 ECC bytes) carried by AA52/AA57. */
#define MCA_PAGE_RAW_SIZE 528u

/* The card the attached memory-card worker emulates. Called on core 0. */
typedef struct {
    /* AA40 card type: 0 = no card, 1 = PS1, 2 = PS2. */
    uint8_t (*type)(void);
    /* Whether an AA52/AA57 page operation may run on `page`. NULL when the
     * card has no page commands. */
    bool (*page_allowed)(uint32_t page);
} mca_card_t;

/* The single request/reply mailbox between USB (core 0) and the card worker
 * (core 1). */
extern mca_bridge_t mca_bridge;

void mca_protocol_init(void);
/* Card worker start/stop (core 1). NULL detaches: the card reads as absent
 * and no new operations are admitted. */
void mca_protocol_attach_card(const mca_card_t *card);
/* Core 0: service USB for the active backend while it runs or still owns work.
 * Returns `running`. */
bool mca_protocol_service(bool running);
void mca_protocol_task(void);
bool mca_protocol_idle(void);
void mca_protocol_reset(void);
void mca_protocol_drain_reply(uint32_t milliseconds);
void mca_protocol_switch_delay(uint32_t milliseconds);
const mca_device_callbacks_t *mca_protocol_callbacks(void);
