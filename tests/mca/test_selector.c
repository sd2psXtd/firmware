#include "ps2/card_emu/ps2_card_backend.h"
#include <assert.h>

ps2_memory_card_backend_t ps2_card_backend_pio_console;
ps2_memory_card_backend_t ps2_card_backend_pio_arcade;
ps2_memory_card_backend_t ps2_card_backend_mca;
static bool mca;
static int variant;
bool settings_get_mca_enabled(void) { return mca; }
int settings_get_ps2_variant(void) { return variant; }

int main(void) {
    variant = 0;
    assert(ps2_card_backend_get() == &ps2_card_backend_pio_console);
    variant = 1;
    assert(ps2_card_backend_get() == &ps2_card_backend_pio_arcade);
    variant = 3;
    assert(ps2_card_backend_get() == &ps2_card_backend_pio_arcade);
    mca = true;
    assert(ps2_card_backend_get() == &ps2_card_backend_mca);
    return 0;
}
