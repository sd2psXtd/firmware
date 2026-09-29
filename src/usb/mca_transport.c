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
    if (!g_mca.active || !data || !len || len > MCA_TRANSACTION_MAX)
        return false;

    uint32_t irq = save_and_disable_interrupts();
    if (g_mca.state != MCA_STATE_IDLE) {
        restore_interrupts(irq);
        return false;
    }

    memcpy(g_mca.rx, data, len);
    g_mca.rx_len = len;
    g_mca.rx_pos = 0;
    g_mca.tx_len = 0;
    __dmb();
    g_mca.state = MCA_STATE_REQUEST_READY;
    restore_interrupts(irq);
    /* The PS2 emulator may be asleep waiting for the next request. */
    __sev();
    return true;
}

uint8_t mca_transport_receive_first(uint8_t *byte, volatile int *exit_request) {
    while (g_mca.active && !*exit_request && g_mca.state != MCA_STATE_REQUEST_READY)
        __wfe();

    if (*exit_request)
        return MCA_RECEIVE_EXIT;
    if (!g_mca.active)
        return MCA_RECEIVE_RESET;

    g_mca.state = MCA_STATE_PROCESSING;
    g_mca.rx_pos = 0;
    /* Raw memory-card transfers are full duplex. While the host clocks the
     * first command byte the card has not decoded it yet, so DAT is idle high
     * and the adapter captures 0xFF. Keep USB MCA responses byte-aligned with
     * the original transaction. */
    g_mca.tx[0] = 0xFF;
    g_mca.tx_len = 1;
    return mca_transport_receive(byte);
}

uint8_t mca_transport_receive(uint8_t *byte) {
    if (!g_mca.active)
        return MCA_RECEIVE_RESET;
    /* PS1 has no receiveFirst() primitive, so the first ordinary receive starts a queued transaction. */
    if (g_mca.state == MCA_STATE_REQUEST_READY) {
        g_mca.state = MCA_STATE_PROCESSING;
        g_mca.rx_pos = 0;
        /* PS1 reaches the first receive through this path instead of
         * receive_first(); prepend the same idle bus byte. */
        g_mca.tx[0] = 0xFF;
        g_mca.tx_len = 1;
    }
    if (g_mca.state != MCA_STATE_PROCESSING)
        return MCA_RECEIVE_RESET;
    if (g_mca.rx_pos >= g_mca.rx_len)
        return MCA_RECEIVE_RESET;

    *byte = g_mca.rx[g_mca.rx_pos++];
    return MCA_RECEIVE_OK;
}

void mca_transport_respond(uint8_t byte) {
    if (!g_mca.active || g_mca.state != MCA_STATE_PROCESSING)
        return;
    if (g_mca.tx_len < MCA_TRANSACTION_MAX)
        g_mca.tx[g_mca.tx_len++] = byte;
}

void mca_transport_complete(void) {
    if (!g_mca.active || g_mca.state != MCA_STATE_PROCESSING)
        return;
    __dmb();
    g_mca.state = MCA_STATE_RESPONSE_READY;
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

uint16_t mca_transport_take_response(uint8_t *dst, uint16_t max_len) {
    if (!dst || g_mca.state != MCA_STATE_RESPONSE_READY)
        return 0;

    uint16_t len = g_mca.tx_len;
    if (len > max_len)
        len = max_len;
    memcpy(dst, g_mca.tx, len);

    uint32_t irq = save_and_disable_interrupts();
    g_mca.rx_len = g_mca.rx_pos = g_mca.tx_len = 0;
    g_mca.state = MCA_STATE_IDLE;
    restore_interrupts(irq);
    return len;
}
