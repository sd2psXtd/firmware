#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico/critical_section.h"

#define MCA_BRIDGE_BUFFER_MAX 1152u
#define MCA_BRIDGE_RX_CAPACITY (MCA_BRIDGE_BUFFER_MAX + 4u)
#define MCA_BRIDGE_TX_CAPACITY (MCA_BRIDGE_BUFFER_MAX + 4u)

typedef enum {
    MCA_BRIDGE_IDLE = 0,
    MCA_BRIDGE_REQUEST,
    MCA_BRIDGE_ACTIVE,
    MCA_BRIDGE_RESPONSE,
} mca_bridge_state_t;

/* One bridge is shared between the two cores. Core 0 owns submission and
 * response release; Core 1 owns active transactions. Initialize once before
 * starting Core 1 and do not reinitialize it while either core uses it. */
typedef struct {
    critical_section_t lock;
    uint32_t state;
    bool cancel_requested;
    uint16_t input_length;
    uint16_t input_offset;
    uint16_t input_position;
    uint16_t output_length;
    uint16_t output_position;
    union { uint32_t input_alignment; uint8_t input[MCA_BRIDGE_RX_CAPACITY]; };
    union { uint32_t output_alignment; uint8_t output[MCA_BRIDGE_TX_CAPACITY]; };
} mca_bridge_t;

void mca_bridge_init(mca_bridge_t *bridge);
/* Core 0. Copies the full request; false means busy or invalid length. */
bool mca_bridge_submit(mca_bridge_t *bridge, const uint8_t *input, size_t length);
/* Zero-copy core0 API. The request buffer is writable only while IDLE. Publish
 * a subrange so a scheduler can retain other RX bytes across transactions. */
uint8_t *mca_bridge_core0_request_buffer(mca_bridge_t *bridge);
bool mca_bridge_core0_publish_request(mca_bridge_t *bridge, size_t offset,
                                      size_t length);
/* Core 0. Requests cancellation without waiting. Storage remains unavailable
 * until core 1 claims and acknowledges the request via core1_finish(). */
bool mca_bridge_cancel(mca_bridge_t *bridge);
/* Core 0. Copies a completed response then releases the transaction to IDLE. */
bool mca_bridge_take_response(mca_bridge_t *bridge, uint8_t *output,
                              size_t capacity, size_t *length);
/* Zero-copy core0 response API. Returns the payload pointer; exactly four
 * writable prefix bytes precede it in the same TX array. Pointer and prefix
 * remain valid until release. */
uint8_t *mca_bridge_core0_response_buffer(mca_bridge_t *bridge,
                                          size_t *length);
bool mca_bridge_core0_release_response(mca_bridge_t *bridge);
/* Core 1. Claim a pending request. */
bool mca_bridge_core1_begin(mca_bridge_t *bridge);
/* Core 1 byte-at-a-time request receive. False means cancelled or exhausted. */
bool mca_bridge_core1_receive(mca_bridge_t *bridge, uint8_t *byte);
/* Core 1 byte-at-a-time response append. False means cancelled or full. */
bool mca_bridge_core1_respond(mca_bridge_t *bridge, uint8_t byte);
/* Core 1. Publishes response, or releases ownership after cancellation. */
bool mca_bridge_core1_finish(mca_bridge_t *bridge);
bool mca_bridge_core1_cancelled(mca_bridge_t *bridge);
mca_bridge_state_t mca_bridge_state(mca_bridge_t *bridge);
