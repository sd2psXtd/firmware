#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "mca_device.h"

void mca_protocol_init(void);
void mca_protocol_set_ready(bool ready);
void mca_protocol_task(void);
bool mca_protocol_idle(void);
void mca_protocol_reset(void);
void mca_protocol_drain_reply(uint32_t milliseconds);
void mca_protocol_switch_delay(uint32_t milliseconds);
const mca_device_callbacks_t *mca_protocol_callbacks(void);
