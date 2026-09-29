#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MCA_TRANSACTION_MAX 1152

enum {
    MCA_RECEIVE_RESET = 0,
    MCA_RECEIVE_EXIT  = 1,
    MCA_RECEIVE_OK    = 2,
};

void mca_transport_init(void);
/* Change modes only while the card emulation loop is stopped (or before launch). */
void mca_transport_set_active(bool active);
bool mca_transport_is_active(void);

bool mca_transport_submit(const uint8_t *data, uint16_t len);
bool mca_transport_response_ready(void);
uint16_t mca_transport_take_response(uint8_t *dst, uint16_t max_len);

uint8_t mca_transport_receive_first(uint8_t *byte, volatile int *exit_request);
uint8_t mca_transport_receive(uint8_t *byte);
void mca_transport_respond(uint8_t byte);
void mca_transport_complete(void);
bool mca_transport_abort(void); /* True if a pending response was discarded. */
