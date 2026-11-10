#pragma once

#include "tusb.h"

typedef struct {
    char const *name;
    void (*init)(void);
    bool (*deinit)(void);
    void (*reset)(uint8_t rhport);
    uint16_t (*open)(uint8_t rhport, tusb_desc_interface_t const *interface_desc,
                     uint16_t max_length);
    bool (*control_xfer_cb)(uint8_t rhport, uint8_t stage,
                            tusb_control_request_t const *request);
    bool (*xfer_cb)(uint8_t rhport, uint8_t endpoint_address,
                    xfer_result_t result, uint32_t transferred);
    void (*sof)(uint8_t rhport, uint32_t frame_count);
} usbd_class_driver_t;

enum { CONTROL_STAGE_SETUP, CONTROL_STAGE_DATA, CONTROL_STAGE_ACK };

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count);
