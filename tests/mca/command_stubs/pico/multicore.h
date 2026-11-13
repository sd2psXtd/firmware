#pragma once
void multicore_lockout_victim_init(void);
int multicore_lockout_victim_is_initialized(unsigned int core);
void multicore_lockout_start_blocking(void);
void multicore_lockout_end_blocking(void);
