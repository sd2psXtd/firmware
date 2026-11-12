#pragma once
#include <stdbool.h>

void usb_storage_init(void);
bool usb_storage_task(void);
void usb_storage_deinit(void);
