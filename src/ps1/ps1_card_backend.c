#include "ps1_card_backend.h"
#include "ps1_card_backend_pio.h"
#include "ps1_card_backend_mca.h"

#include "usb/mca_device.h"
#include "usb/mca_protocol.h"
#include "settings.h"

ps1_memory_card_backend_t* ps1_card_backend_get(void) {
    if (mca_device_profile_is_mca())
        return &ps1_card_backend_mca;
    return &ps1_card_backend_pio;
}

/* Core 0 services the active transport until its worker has stopped. */
bool ps1_card_backend_task(void) {
    bool running = settings_get_mode(true) == MODE_PS1 &&
        settings_get_mca_enabled() == mca_device_profile_is_mca();
    if (mca_device_profile_is_mca() && (running || !mca_protocol_idle()))
        mca_protocol_task();
    return running;
}
