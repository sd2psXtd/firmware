#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef unsigned uint;
typedef void (*gpio_irq_callback_t)(uint, uint32_t);
typedef struct { uint32_t ints[4]; } io_bank0_irq_ctrl_hw_t;
static struct {
    uint32_t intr[4];
    io_bank0_irq_ctrl_hw_t proc0_irq_ctrl, proc1_irq_ctrl;
} mock_iobank;
#define iobank0_hw (&mock_iobank)
#define NUM_CORES 2
#define NUM_BANK0_GPIOS 30
#define IO_IRQ_BANK0 0
#define GPIO_IRQ_EDGE_RISE 8u
#define GPIO_IRQ_EDGE_FALL 4u
#define GPIO_IRQ_CALLBACK_ORDER_PRIORITY 0
#define GPIO_SLEW_RATE_SLOW 0
#define GPIO_DRIVE_STRENGTH_4MA 0
#define PIN_PSX_ACK 16
#define PIN_PSX_SEL 17
#define PIN_PSX_CLK 18
#define PIN_PSX_CMD 19
#define PIN_PSX_DAT 20
#define pio0 0
#define pio_pindirs 0

static void (*irq_handler)(void);
static bool irq_enabled;
static uint32_t gpio_events, claimed, enabled_sms, released_sms;
static unsigned programs, next_offset;
static unsigned get_core_num(void) { return 1; }
static void check_gpio_param(uint gpio) { assert(gpio < NUM_BANK0_GPIOS); }
static void gpio_set_dir(uint pin, bool output) { (void)pin; (void)output; }
static void gpio_disable_pulls(uint pin) { (void)pin; }
static void gpio_set_slew_rate(uint pin, int rate) { (void)pin; (void)rate; }
static void gpio_set_drive_strength(uint pin, int strength) { (void)pin; (void)strength; }
static void gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled) {
    assert(gpio == PIN_PSX_SEL);
    assert(!enabled || irq_handler != NULL); /* Pico SDK invariant. */
    if (enabled) gpio_events |= events;
    else gpio_events &= ~events;
}
static void irq_set_enabled(int irq, bool enabled) {
    (void)irq;
    assert(!enabled || irq_handler != NULL);
    irq_enabled = enabled;
}
static void irq_add_shared_handler(int irq, void (*handler)(void), int priority) {
    (void)irq; (void)priority;
    assert(irq_handler == NULL);
    irq_handler = handler;
}
static void irq_remove_handler(int irq, void (*handler)(void)) {
    (void)irq;
    assert(!irq_enabled && gpio_events == 0 && irq_handler == handler);
    irq_handler = NULL;
}
static uint pio_add_program(int pio, const int* program) {
    (void)pio; (void)program; ++programs; return next_offset++;
}
static uint pio_claim_unused_sm(int pio, bool required) {
    (void)pio; (void)required;
    for (uint sm = 0; sm < 4; ++sm) {
        if (!(claimed & (1u << sm))) { claimed |= 1u << sm; return sm; }
    }
    assert(false);
    return 0;
}
static void pio_remove_program(int pio, const int* program, uint offset) {
    (void)pio; (void)program; (void)offset; assert(programs); --programs;
}
static void pio_sm_unclaim(int pio, uint sm) {
    (void)pio; assert(claimed & (1u << sm)); claimed &= ~(1u << sm);
}
static void pio_set_sm_mask_enabled(int pio, uint32_t mask, bool enabled) {
    (void)pio;
    if (enabled) enabled_sms |= mask;
    else enabled_sms &= ~mask;
}
static void pio_restart_sm_mask(int pio, uint32_t mask) { (void)pio; (void)mask; }
static uint pio_encode_jmp(uint offset) { return 0x100u | offset; }
static uint pio_encode_set(int destination, uint value) { (void)destination; return value; }
static void pio_sm_exec(int pio, uint sm, uint instruction) {
    (void)pio;
    if (instruction == 0) released_sms |= 1u << sm;
}
static void pio_sm_clear_fifos(int pio, uint sm) { (void)pio; (void)sm; }
static void pio_sm_drain_tx_fifo(int pio, uint sm) { (void)pio; (void)sm; }
static void pio_interrupt_clear(int pio, uint irq) { (void)pio; (void)irq; }
static void pio_enable_sm_mask_in_sync(int pio, uint32_t mask) {
    pio_set_sm_mask_enabled(pio, mask, true);
}
static bool pio_sm_is_rx_fifo_empty(int pio, uint sm) { (void)pio; (void)sm; return true; }
static uint32_t pio_sm_get(int pio, uint sm) { (void)pio; (void)sm; return 0; }
static void pio_sm_put_blocking(int pio, uint sm, uint32_t word) { (void)pio; (void)sm; (void)word; }

static const int cmd_reader_program, dat_writer_program, ack_generator_program;
static void cmd_reader_program_init(int pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
static void dat_writer_program_init(int pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
static void controller_program_init(int pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
static void ack_generator_program_init(int pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
