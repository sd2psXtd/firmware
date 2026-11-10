#include "ps2_card_backend.h"
#include "ps2_card_backend_pio.h"
#include "ps2/ps2_card_backend_mca.h"

#include "usb/mca_device.h"
#include "usb/mca_protocol.h"
#include "settings.h"

ps2_memory_card_backend_t* ps2_card_backend_get(void) {
    if (mca_device_profile_is_mca())
        return &ps2_card_backend_mca;
    if ((settings_get_ps2_variant() == PS2_VARIANT_COH)
        || (settings_get_ps2_variant() == PS2_VARIANT_SC2)) {
        return &ps2_card_backend_pio_arcade;
    } else {
        return &ps2_card_backend_pio_console;
    }
}

/* Core 0 services the active transport until its worker has stopped. */
bool ps2_card_backend_task(void) {
    bool running = settings_get_mode(true) == MODE_PS2 &&
        settings_get_mca_enabled() == mca_device_profile_is_mca();
    if (mca_device_profile_is_mca() && (running || !mca_protocol_idle()))
        mca_protocol_task();
    return running;
}
