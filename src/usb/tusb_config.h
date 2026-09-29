#pragma once

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 64
#define CFG_TUD_CDC_TX_BUFSIZE 64
#define CFG_TUD_CDC_EP_BUFSIZE 64

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif
