#pragma once

#include "ps1_mc_internal.h"

extern ps1_memory_card_backend_t ps1_card_backend_mca;
bool ps1_card_backend_mca_wait(void);
void ps1_card_backend_mca_finish(void);
