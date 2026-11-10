#pragma once
#include <stdbool.h>
#include <stdint.h>
bool ps2_cardman_is_accessible(void);
bool ps2_cardman_is_idle(void);
uint32_t ps2_cardman_get_card_size(void);
