#pragma once

#include "pico.h"

extern void ps2_mc_auth(void);
extern void ps2_mc_sessionKeyEncr(void);
extern void ps2_mc_auth_keySelect(void);
extern void ps2_mc_auth_reset(void);

void __time_critical_func(generateIvSeedNonce)(void);


extern bool ps2_mc_auth_keyStoreResetRequired();
extern void ps2_mc_auth_keyStoreResetAck();
