#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void tud_task_ext(uint32_t timeout_ms, bool in_isr);

#define TUSB_CLASS_VENDOR_SPECIFIC 0xff
#define TUSB_DESC_ENDPOINT 5
#define TUSB_REQ_RCPT_INTERFACE 1

typedef enum {
    XFER_RESULT_SUCCESS,
    XFER_RESULT_FAILED,
} xfer_result_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} tusb_desc_interface_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint8_t wMaxPacketSize[2];
    uint8_t bInterval;
} tusb_desc_endpoint_t;

typedef struct {
    uint8_t recipient : 5;
    uint8_t type : 2;
    uint8_t direction : 1;
} tusb_request_type_bits_t;

typedef struct {
    tusb_request_type_bits_t bmRequestType_bit;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
} tusb_control_request_t;

typedef struct {
    uint32_t bit_rate;
} cdc_line_coding_t;
