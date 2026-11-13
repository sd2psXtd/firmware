#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pico/critical_section.h"

#define MCA_BRIDGE_BUFFER_MAX 1152u
#define MCA_BRIDGE_RX_CAPACITY (MCA_BRIDGE_BUFFER_MAX + 4u)
#define MCA_BRIDGE_TX_CAPACITY (MCA_BRIDGE_BUFFER_MAX + 4u)
#define MCA_BRIDGE_TIMEOUT_US 1000000u

typedef enum {
    MCA_BRIDGE_IDLE = 0,
    MCA_BRIDGE_REQUEST,
    MCA_BRIDGE_ACTIVE,
    MCA_BRIDGE_RESPONSE,
    MCA_BRIDGE_SENDING,
} mca_bridge_state_t;

/* One complete MCA operation at a time. Core 0 owns IDLE (including frame
 * assembly), RESPONSE and SENDING; core 1 owns ACTIVE. Neither buffer may be
 * reused until the reply completes or is discarded. Initialize only once. */
typedef struct {
    critical_section_t lock;
    mca_bridge_state_t state;
    bool cancel_requested;
    uint64_t deadline;
    uint16_t input_length;
    uint16_t input_position;
    uint16_t output_length;
    union { uint32_t input_alignment; uint8_t input[MCA_BRIDGE_RX_CAPACITY]; };
    union { uint32_t output_alignment; uint8_t output[MCA_BRIDGE_TX_CAPACITY]; };
} mca_bridge_t;

void mca_bridge_init(mca_bridge_t *bridge);
mca_bridge_state_t mca_bridge_state(mca_bridge_t *bridge);

/* Core 0: publish the complete frame, or prepare a two-byte local reply. */
bool mca_bridge_submit(mca_bridge_t *bridge, size_t length);
void mca_bridge_reply(mca_bridge_t *bridge, uint8_t value);
/* Timeouts request cooperative cancellation; ACTIVE buffers remain owned by
 * core 1 until finish. A request not yet claimed can be failed immediately. */
void mca_bridge_poll_timeout(mca_bridge_t *bridge, uint64_t now_us);
/* Core 0: mark a queued USB transfer, then release on completion/reset. */
void mca_bridge_sending(mca_bridge_t *bridge);
void mca_bridge_release(mca_bridge_t *bridge);

/* Core 1: begin returns false while core 0/USB owns the previous reply. The
 * backend waits at this boundary, checking its exit request between attempts. */
bool mca_bridge_begin(mca_bridge_t *bridge);
bool mca_bridge_receive(mca_bridge_t *bridge, uint8_t *byte);
void mca_bridge_respond(mca_bridge_t *bridge, uint8_t byte);
bool mca_bridge_cancelled(mca_bridge_t *bridge);
void mca_bridge_restart_timeout(mca_bridge_t *bridge);
/* Raw replies are framed here. Page execution prepares its final reply itself.
 * Overflow, cancellation or unsuccessful execution produces 55 AF. */
void mca_bridge_finish(mca_bridge_t *bridge, bool success);
