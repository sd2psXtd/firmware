#include "ps2_card_backend.h"
#include "ps2_card_backend_pio.h"

#include "settings.h"

ps2_memory_card_backend_t* ps2_card_backend_get(void) {
    if ((settings_get_ps2_variant() == PS2_VARIANT_COH)
        || (settings_get_ps2_variant() == PS2_VARIANT_SC2)) {
        return &ps2_card_backend_pio_arcade;
    } else {
        return &ps2_card_backend_pio_console;
    }
}
