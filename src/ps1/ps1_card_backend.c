#include "ps1_card_backend.h"
#include "ps1_card_backend_pio.h"
#include "ps1_card_backend_mca.h"

#include "settings.h"

ps1_memory_card_backend_t* ps1_card_backend_get(void) {
    if (settings_get_mca_enabled())
        return &ps1_card_backend_mca;
    return &ps1_card_backend_pio;
}
