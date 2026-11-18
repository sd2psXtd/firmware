#include <stdio.h>
#include "pico.h"
#include "ps1/ps1_card_backend_pio.c"

volatile bool ps1_card_active;
volatile int ps1_mc_reset, ps1_mc_exit_request;
int settings_get_mode(bool temporary) { (void)temporary; return MODE_PS1; }
bool settings_get_mca_enabled(void) { return false; }
void sleep_ms(uint32_t ms) { (void)ms; }

int main(void) {
    for (unsigned session = 0; session < 3; ++session) {
        ps1_card_backend_pio.init();
        assert(claimed == 15 && programs == 4);
        for (unsigned card_switch = 0; card_switch < 3; ++card_switch) {
            ps1_card_backend_pio.reset();
            ps1_card_backend_pio.startup();
            assert(irq_handler && irq_enabled);
            assert(gpio_events == (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL));
            assert(enabled_sms == claimed);
            card_deselected(PIN_PSX_SEL, GPIO_IRQ_EDGE_FALL);
            assert(ps1_card_active);
            card_deselected(PIN_PSX_SEL, GPIO_IRQ_EDGE_RISE);
            assert(!ps1_card_active && ps1_mc_reset);
            released_sms = 0;
            ps1_card_active = true;
            ps1_card_backend_pio.shutdown();
            assert(!irq_handler && !irq_enabled && gpio_events == 0);
            assert(!enabled_sms && !ps1_card_active);
            assert(released_sms & (1u << dat_writer.sm));
            assert(released_sms & (1u << ack_generator.sm));
            assert(claimed == 15 && programs == 4);
        }
        ps1_card_backend_pio.deinit();
        assert(!claimed && !programs);
    }
    puts("PS1 PIO startup, switch, IRQ teardown and resource tests passed");
    return 0;
}
