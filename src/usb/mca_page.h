#pragma once

#include "mca_bridge.h"

#define MCA_PAGE_RAW_SIZE 528u

/* Core 1 only. Execute a complete AA52/AA57 operation using the existing PS2
 * command handlers. Intermediate SIO transactions never cross to core 0. */
bool mca_page_execute(mca_bridge_t *bridge);
/* The MCA backend routes handler I/O here during page execution. */
bool mca_page_active(void);
bool mca_page_receive(uint8_t *byte);
void mca_page_respond(uint8_t byte);
