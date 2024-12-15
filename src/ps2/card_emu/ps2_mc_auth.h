#pragma once

#include "pico/platform.h"

extern void ps2_mc_auth(void);
extern void ps2_mc_sessionKeyEncr(void);
extern void ps2_mc_auth_keySelect(void);

void __time_critical_func(generateIvSeedNonce)(void);
