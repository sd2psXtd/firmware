#include "mca_device.h"
#include "usb_session.h"
#include "settings.h"

#include "pico/stdio_usb/reset_interface.h"
#include "pico/unique_id.h"
#include "tusb.h"

#define MCA_VID 0x054c
#define MCA_PID 0x02ea
#define PICO_VID 0x2e8a
#define PICO_CDC_PID 0x000a

#define CDC_EP_NOTIF 0x81
#define CDC_EP_OUT   0x02
#define CDC_EP_IN    0x82

/*
 * The Serial and storage profiles match pico-sdk's stdio_usb descriptors,
 * including bcdUSB 2.1 with the Microsoft OS 2.0 descriptor (see below).
 */
static tusb_desc_device_t const desc_pico_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64,
    .idVendor = PICO_VID,
    .idProduct = PICO_CDC_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

/* Separate PID avoids hosts caching the CDC-only configuration. */
static tusb_desc_device_t const desc_storage_device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = 64, .idVendor = PICO_VID, .idProduct = 0x000b,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2,
    .iSerialNumber = 3, .bNumConfigurations = 1,
};

static tusb_desc_device_t const desc_mca_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0110,
    .bDeviceClass = TUSB_CLASS_VENDOR_SPECIFIC,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0xff,
    .bMaxPacketSize0 = 64,
    .idVendor = MCA_VID,
    .idProduct = MCA_PID,
    .bcdDevice = 0x0100,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    if (usb_session_get_mode() == USB_MODE_MASS) return (uint8_t const *)&desc_storage_device;
    return (uint8_t const *)(mca_device_profile_is_mca() ? &desc_mca_device : &desc_pico_device);
}

enum {
    ITF_CDC = 0,
    ITF_CDC_DATA,
    ITF_RESET,
    ITF_PICO_TOTAL,
    ITF_MCA = 0,
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

static uint8_t const desc_pico[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_PICO_TOTAL, 0, PICO_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, CDC_EP_NOTIF, 8, CDC_EP_OUT, CDC_EP_IN, 64),
    9, TUSB_DESC_INTERFACE, ITF_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
        RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, 5,
};

#define STORAGE_TOTAL_LEN (PICO_TOTAL_LEN + TUD_MSC_DESC_LEN)
static uint8_t const desc_storage[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_PICO_TOTAL + 1, 0, STORAGE_TOTAL_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, CDC_EP_NOTIF, 8, CDC_EP_OUT, CDC_EP_IN, 64),
    9, TUSB_DESC_INTERFACE, ITF_RESET, 0, 0, TUSB_CLASS_VENDOR_SPECIFIC,
        RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, 5,
    TUD_MSC_DESCRIPTOR(ITF_PICO_TOTAL, 0, 0x03, 0x83, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    if (usb_session_get_mode() == USB_MODE_MASS) return desc_storage;
    return mca_device_profile_is_mca() ? desc_mca : desc_pico;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[20];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    static char const *const strings[] = {
        NULL, "Raspberry Pi", "Pico", serial, "Board CDC", "Reset",
    };
    uint8_t len = 0;

    if (index == 0) {
        desc[0] = (TUSB_DESC_STRING << 8) | 4;
        desc[1] = 0x0409;
        return desc;
    }
    if (mca_device_profile_is_mca() || index >= sizeof(strings) / sizeof(strings[0]))
        return NULL;
    if (!serial[0])
        pico_get_unique_board_id_string(serial, sizeof(serial));

    char const *str = strings[index];
    while (len < 19 && str[len]) {
        desc[1 + len] = (uint8_t)str[len];
        ++len;
    }
    desc[0] = (TUSB_DESC_STRING << 8) | (2 * len + 2);
    return desc;
}

/*
 * Microsoft OS 2.0 descriptor, as pico-sdk's stdio_usb sends it when it owns
 * TinyUSB: Windows binds WinUSB to the reset interface (picotool reboot works
 * without a manual driver install). Windows requires it once bcdUSB is 2.1.
 * The reset interface is interface 2 in both the Serial and storage profiles.
 */
#define MS_OS_20_VENDOR_CODE 1
#define MS_OS_20_DESC_LEN    (0x0A + 0x08 + 0x14 + 0x80)
#define BOS_TOTAL_LEN        (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, MS_OS_20_VENDOR_CODE),
};
TU_VERIFY_STATIC(sizeof(desc_bos) == BOS_TOTAL_LEN, "Incorrect BOS size");

static uint8_t const desc_ms_os_20[] = {
    /* Set header: length, type, Windows version (8.1+), total length */
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    /* Function subset header: length, type, first interface, reserved, length */
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_RESET, 0,
    U16_TO_U8S_LE(0x08 + 0x14 + 0x80),
    /* Compatible ID: WINUSB */
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* Registry property: DeviceInterfaceGUID (REG_SZ) */
    U16_TO_U8S_LE(0x0080), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0001), U16_TO_U8S_LE(0x0028),
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
    'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    U16_TO_U8S_LE(0x004E),
    /* {bc7398c1-73cd-4cb7-98b8-913a8fca7bf6}, pico-sdk's reset interface GUID */
    '{', 0, 'b', 0, 'c', 0, '7', 0, '3', 0, '9', 0, '8', 0, 'c', 0, '1', 0, '-', 0,
    '7', 0, '3', 0, 'c', 0, 'd', 0, '-', 0, '4', 0, 'c', 0, 'b', 0, '7', 0, '-', 0,
    '9', 0, '8', 0, 'b', 0, '8', 0, '-', 0, '9', 0, '1', 0, '3', 0, 'a', 0, '8', 0,
    'f', 0, 'c', 0, 'a', 0, '7', 0, 'b', 0, 'f', 0, '6', 0, '}', 0, 0, 0,
};
TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 size");

uint8_t const *tud_descriptor_bos_cb(void) {
    return mca_device_profile_is_mca() ? NULL : desc_bos;
}

/* TinyUSB routes all vendor-type requests here; MCA uses standard requests only. */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP)
        return true;
    if (mca_device_profile_is_mca() ||
        request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_DEVICE ||
        request->bRequest != MS_OS_20_VENDOR_CODE || request->wIndex != 7)
        return false;
    return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, sizeof(desc_ms_os_20));
}
