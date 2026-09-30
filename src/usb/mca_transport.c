#include "mca_transport.h"

#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include <string.h>

typedef enum {
    MCA_STATE_IDLE = 0,
    MCA_STATE_REQUEST_READY,
    MCA_STATE_PROCESSING,
    MCA_STATE_RESPONSE_READY,
} mca_state_t;

typedef struct {
    volatile bool active;
    volatile mca_state_t state;
    volatile uint16_t rx_len;
    volatile uint16_t rx_pos;
    volatile uint16_t tx_len;
    uint8_t rx[MCA_TRANSACTION_MAX];
    uint8_t tx[MCA_TRANSACTION_MAX];
} mca_shared_t;

static mca_shared_t g_mca;

void mca_transport_init(void) {
    memset(&g_mca, 0, sizeof(g_mca));
}

void mca_transport_set_active(bool active) {
    uint32_t irq = save_and_disable_interrupts();
    g_mca.active = active;
    g_mca.state = MCA_STATE_IDLE;
    g_mca.rx_len = g_mca.rx_pos = g_mca.tx_len = 0;
    restore_interrupts(irq);
}

bool mca_transport_is_active(void) {
    return g_mca.active;
}

bool mca_transport_submit(const uint8_t *data, uint16_t len) {
    bool submitted = false;
    uint32_t irq;

    if (g_mca.active && data && len && len <= MCA_TRANSACTION_MAX) {
        irq = save_and_disable_interrupts();
        if (g_mca.state == MCA_STATE_IDLE) {
            memcpy(g_mca.rx, data, len);
            g_mca.rx_len = len;
            g_mca.rx_pos = 0;
            g_mca.tx_len = 0;
            __dmb();
            g_mca.state = MCA_STATE_REQUEST_READY;
            submitted = true;
        }
        restore_interrupts(irq);
    }
    return submitted;
}

uint8_t mca_transport_receive_first(uint8_t *byte, volatile int *exit_request) {
    uint8_t result = MCA_RECEIVE_RESET;

    /* Poll only between transactions. A bounded busy wait needs neither a
     * cross-core event nor an SDK alarm/interrupt to observe requests/exits. */
    while (g_mca.active && !*exit_request && g_mca.state != MCA_STATE_REQUEST_READY)
        busy_wait_us_32(10);

    if (*exit_request)
        result = MCA_RECEIVE_EXIT;
    else if (g_mca.active)
        result = mca_transport_receive(byte);
    return result;
}

uint8_t mca_transport_receive(uint8_t *byte) {
    uint8_t result = MCA_RECEIVE_RESET;

    if (g_mca.active) {
        if (g_mca.state == MCA_STATE_REQUEST_READY) {
            __dmb(); /* Acquire the published request before reading its bytes. */
            g_mca.state = MCA_STATE_PROCESSING;
            g_mca.rx_pos = 0;
            /* PS1 and PS2 use the same full-duplex framing: DAT is idle high
             * while the host clocks the first command byte. */
            g_mca.tx[0] = 0xFF;
            g_mca.tx_len = 1;
        }
        if (g_mca.state == MCA_STATE_PROCESSING && g_mca.rx_pos < g_mca.rx_len) {
            *byte = g_mca.rx[g_mca.rx_pos++];
            result = MCA_RECEIVE_OK;
        }
    }
    return result;
}

void mca_transport_respond(uint8_t byte) {
    if (g_mca.active && g_mca.state == MCA_STATE_PROCESSING &&
        g_mca.tx_len < MCA_TRANSACTION_MAX)
        g_mca.tx[g_mca.tx_len++] = byte;
}

void mca_transport_complete(void) {
    if (g_mca.active && g_mca.state == MCA_STATE_PROCESSING) {
        __dmb();
        g_mca.state = MCA_STATE_RESPONSE_READY;
    }
}

bool mca_transport_abort(void) {
    uint32_t irq = save_and_disable_interrupts();
    bool pending = g_mca.state != MCA_STATE_IDLE;
    g_mca.rx_len = g_mca.rx_pos = g_mca.tx_len = 0;
    g_mca.state = MCA_STATE_IDLE;
    restore_interrupts(irq);
    return pending;
}

bool mca_transport_response_ready(void) {
    return g_mca.state == MCA_STATE_RESPONSE_READY;
}

void mca_transport_get_progress(uint8_t progress[8]) {
    uint16_t counters[] = { g_mca.rx_len, g_mca.rx_pos, g_mca.tx_len };
    unsigned i;

    progress[0] = (uint8_t)g_mca.state;
    progress[1] = g_mca.active;
    for (i = 0; i < 3; ++i) {
        progress[2 + i * 2] = (uint8_t)counters[i];
        progress[3 + i * 2] = (uint8_t)(counters[i] >> 8);
    }
}

uint16_t mca_transport_take_response(uint8_t *dst, uint16_t max_len) {
    uint16_t len = 0;
    uint32_t irq;

    if (dst && g_mca.state == MCA_STATE_RESPONSE_READY) {
        __dmb(); /* Acquire the response published by core 1. */
        len = g_mca.tx_len;
        if (len > max_len)
            len = max_len;
        memcpy(dst, g_mca.tx, len);
        irq = save_and_disable_interrupts();
        g_mca.rx_len = g_mca.rx_pos = g_mca.tx_len = 0;
        g_mca.state = MCA_STATE_IDLE;
        restore_interrupts(irq);
    }
    return len;
}
