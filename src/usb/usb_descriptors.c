#include "tusb.h"
#include "mca_usb.h"
#include "pico/stdio_usb/reset_interface.h"
#include "pico/unique_id.h"

#define MCA_VID 0x054c
#define MCA_PID 0x02ea
#define PICO_VID 0x2e8a
#define PICO_CDC_PID 0x000a

#define MCA_EP_IN   0x81
#define MCA_EP_OUT  0x02
#define MCA_EP_INT  0x83

#define CDC_EP_NOTIF 0x81
#define CDC_EP_OUT   0x02
#define CDC_EP_IN    0x82

static tusb_desc_device_t const desc_pico_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = 64,
    .idVendor           = PICO_VID,
    .idProduct          = PICO_CDC_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

static tusb_desc_device_t const desc_mca_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_VENDOR_SPECIFIC,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = 64,
    .idVendor           = MCA_VID,
    .idProduct          = MCA_PID,
    .bcdDevice          = 0x0100,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)(mca_usb_profile_is_mca() ? &desc_mca_device : &desc_pico_device);
}

enum {
    ITF_MCA = 0,
    ITF_CDC = 0,
    ITF_CDC_DATA,
    ITF_RESET,
    ITF_PICO_TOTAL,
};

#define MCA_DESC_LEN (9 + 7 + 7 + 7)
#define MCA_TOTAL_LEN (TUD_CONFIG_DESC_LEN + MCA_DESC_LEN)
#define PICO_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 9)

#define MCA_INTERFACE_DESCRIPTOR \
    9, TUSB_DESC_INTERFACE, ITF_MCA, 0, 3, 0xff, 0x00, 0xff, 0, \
    7, TUSB_DESC_ENDPOINT, MCA_EP_IN,  TUSB_XFER_BULK,      U16_TO_U8S_LE(64), 0, \
    7, TUSB_DESC_ENDPOINT, MCA_EP_OUT, TUSB_XFER_BULK,      U16_TO_U8S_LE(64), 0, \
    7, TUSB_DESC_ENDPOINT, MCA_EP_INT, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(1), 100

static uint8_t const desc_mca[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, MCA_TOTAL_LEN, 0x00, 200),
    MCA_INTERFACE_DESCRIPTOR,
};
TU_VERIFY_STATIC(sizeof(desc_mca) == MCA_TOTAL_LEN, "MCA configuration length mismatch");

static uint8_t const desc_pico[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_PICO_TOTAL, 0, PICO_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, CDC_EP_NOTIF, 8, CDC_EP_OUT, CDC_EP_IN, 64),
    9, TUSB_DESC_INTERFACE, ITF_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
        RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, 5,
};
TU_VERIFY_STATIC(sizeof(desc_pico) == PICO_TOTAL_LEN, "Pico configuration length mismatch");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return mca_usb_profile_is_mca() ? desc_mca : desc_pico;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[20];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    static char const *const strings[] = {
        NULL, "Raspberry Pi", "Pico", serial, "Board CDC", "Reset",
    };

    if (index == 0) {
        desc[0] = (TUSB_DESC_STRING << 8) | 4;
        desc[1] = 0x0409;
        return desc;
    }
    if (mca_usb_profile_is_mca() || index >= sizeof(strings) / sizeof(strings[0]))
        return NULL;
    if (!serial[0])
        pico_get_unique_board_id_string(serial, sizeof(serial));

    char const *str = strings[index];
    uint8_t len = 0;
    while (len < 19 && str[len]) {
        desc[1 + len] = str[len];
        ++len;
    }
    desc[0] = (TUSB_DESC_STRING << 8) | (2 * len + 2);
    return desc;
}
