#pragma once
#include <stdbool.h>
typedef struct { int unused; } stdio_driver_t;
void stdio_set_driver_enabled(stdio_driver_t *driver, bool enabled);
