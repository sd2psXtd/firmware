#pragma once

#include "tusb.h"

bool usbd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *endpoint);
bool usbd_edpt_xfer(uint8_t rhport, uint8_t endpoint_address, uint8_t *buffer,
                    uint32_t total_bytes);
