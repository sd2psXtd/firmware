#include "mca_usb.h"
#include "mca_transport.h"

#include "bsp/board.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/stdio_usb/reset_interface.h"
#include "ps1/ps1_memory_card.h"
#include "ps2/card_emu/ps2_mc_data_interface.h"
#include "ps2/card_emu/ps2_mc_auth.h"
#include "ps2/card_emu/ps2_mc_internal.h"
#include "ps2/card_emu/ps2_memory_card.h"
#include "ps2/mmceman/ps2_mmceman.h"
#include "ps2/ps2_cardman.h"
#include "settings.h"
#include "tusb.h"
#include <string.h>

#define MCA_EP_IN   0x81
#define MCA_EP_OUT  0x02
#define MCA_PACKET 64

#define MCA_PS2_PAGE_DATA 512u
#define MCA_PS2_PAGE_ECC   16u
#define MCA_PS2_PAGE_RAW  (MCA_PS2_PAGE_DATA + MCA_PS2_PAGE_ECC)

static bool g_enabled;
static bool g_usb_started;
static bool g_card_switching;

static uint8_t ep_out_buf[MCA_PACKET] CFG_TUSB_MEM_ALIGN;
static uint8_t ep_in_buf[MCA_TRANSACTION_MAX + 4] CFG_TUSB_MEM_ALIGN;

static uint8_t cmd_buf[MCA_TRANSACTION_MAX + 4];
static uint16_t cmd_len;
static uint16_t expected_len;
static uint8_t response_buf[MCA_TRANSACTION_MAX + 4];
static uint16_t response_len;
static bool in_busy;
static uint8_t reset_itf_num;

static void select_card_transport(bool enabled) {
    mca_transport_set_active(enabled);
    ps1_memory_card_set_mca_transport(enabled);
    ps2_memory_card_set_mca_transport(enabled);
}

static void connect_usb(void) {
    tusb_init();
    /* With tinyusb_device linked, stdio_usb_init() only registers the CDC
     * driver; the application owns TinyUSB and runs tud_task_ext(). */
    if (!g_enabled)
        stdio_usb_init();
    g_usb_started = true;
}

static void disconnect_usb(void) {
    if (!g_enabled)
        stdio_usb_deinit();
    tud_disconnect();
    sleep_ms(150);
    tud_deinit(0);
    g_usb_started = false;
}

void mca_usb_init(void) {
    mca_transport_init();
    g_card_switching = false;
    g_enabled = settings_get_mca_enabled();
    select_card_transport(g_enabled);
    board_init();

    /* Pico CDC/picotool starts immediately; the first MCA connection waits
     * until the card is ready and main() calls mca_usb_start(). */
    if (!g_enabled)
        connect_usb();
}

void mca_usb_start(void) {
    if (!g_usb_started)
        connect_usb();
}

void mca_usb_set_enabled(bool enabled) {
    if (g_enabled == enabled)
        return;

    /* Exit the live emulator before replacing its receive/send handlers.
     * GUI and serial commands run on core 0; exit waits for core 1 to stop. */
    bool ps2_running = ps2_memory_card_running();
    uint8_t retry_counter = mmceman_mcman_retry_counter;
    if (ps2_running)
        ps2_memory_card_exit();
    else
        ps1_memory_card_exit();

    select_card_transport(enabled);

    if (ps2_running) {
        /* No card changed: preserve any pending cache retry, rather than
         * starting five new refusals as a normal card-switch exit would. */
        mmceman_mcman_retry_counter = retry_counter;
        ps2_memory_card_enter();
    } else {
        ps1_memory_card_enter();
    }

    /* Before the first connection (e.g. an SD settings override), select the
     * new identity without disconnecting or starting USB prematurely. */
    bool reconnect = g_usb_started;
    if (reconnect)
        disconnect_usb();
    g_enabled = enabled;
    if (reconnect)
        connect_usb();
    settings_set_mca_enabled(enabled);
}

bool mca_usb_is_enabled(void) { return g_enabled; }
bool mca_usb_is_started(void) { return g_usb_started; }

void mca_usb_card_switch_begin(void) {
    g_card_switching = g_enabled && mca_transport_is_active();
    if (g_card_switching)
        mca_transport_set_active(false);
}

void mca_usb_card_switch_delay(void) {
    if (!g_card_switching || !g_usb_started) {
        sleep_ms(500);
        return;
    }

    /* Core 0 owns TinyUSB. Service requests throughout the card-absent
     * interval instead of exceeding the host's bulk-read timeout. */
    uint64_t resume_time = time_us_64() + 500000;
    while (time_us_64() < resume_time) {
        mca_usb_task();
        sleep_ms(1);
    }
}

void mca_usb_card_switch_end(void) {
    if (!g_card_switching)
        return;

    /* The new card must not inherit the previous MagicGate session. */
    ps2_mc_auth_invalidate();
    mca_transport_set_active(true);
    g_card_switching = false;
}

static void prime_out(uint8_t rhport) {
    usbd_edpt_xfer(rhport, MCA_EP_OUT, ep_out_buf, sizeof(ep_out_buf));
}

static void queue_in(uint8_t rhport) {
    if (in_busy || !response_len)
        return;
    /* Keep the response in one transfer so its final short packet completes
     * the native host's single bulk read, including 138-byte data chunks. */
    memcpy(ep_in_buf, response_buf, response_len);
    if (usbd_edpt_xfer(rhport, MCA_EP_IN, ep_in_buf, response_len)) {
        response_len = 0;
        in_busy = true;
    }
}

static void set_short_response(uint8_t value) {
    response_buf[0] = 0x55;
    response_buf[1] = value;
    response_len = 2;
}

void mca_usb_reject_pending_request(void) {
    if (g_enabled && g_usb_started) {
        set_short_response(0xaf);
        queue_in(0);
    }
}

static void set_long_success(const uint8_t *data, uint16_t len) {
    if (len > MCA_TRANSACTION_MAX)
        len = MCA_TRANSACTION_MAX;
    response_buf[0] = 0x55;
    response_buf[1] = 0x5a;
    response_buf[2] = (uint8_t)len;
    response_buf[3] = (uint8_t)(len >> 8);
    if (len)
        memcpy(response_buf + 4, data, len);
    response_len = len + 4;
}

static void calculate_ps2_ecc(uint8_t *ecc, const uint8_t *data) {
    memset(ecc, 0, MCA_PS2_PAGE_ECC);
    for (unsigned chunk = 0; chunk < 4; ++chunk) {
        uint8_t *out = ecc + chunk * 3;
        const uint8_t *in = data + chunk * 128;
        for (unsigned i = 0; i < 128; ++i) {
            uint8_t c = EccTable[in[i]];
            out[0] ^= c;
            if (c & 0x80) {
                out[1] ^= (uint8_t)~i;
                out[2] ^= (uint8_t)i;
            }
        }
        out[0] = (uint8_t)(~out[0] & 0x77);
        out[1] = (uint8_t)(~out[1] & 0x7f);
        out[2] = (uint8_t)(~out[2] & 0x7f);
    }
}

static bool ps2_page_available(uint32_t *page) {
    if (settings_get_mode(true) != MODE_PS2 ||
        settings_get_ps2_variant() != PS2_VARIANT_RETAIL ||
        !ps2_cardman_is_accessible() ||
        !ps2_mc_auth_isValid())
        return false;

    *page = (uint32_t)cmd_buf[3] |
            ((uint32_t)cmd_buf[4] << 8) |
            ((uint32_t)cmd_buf[5] << 16) |
            ((uint32_t)cmd_buf[6] << 24);
    return ((uint64_t)*page + 1u) * MCA_PS2_PAGE_DATA <= ps2_cardman_get_card_size();
}

static bool process_ps2_page_read(void) {
    /* AA 52 03 <page LE32> 55 2B */
    if (cmd_len != 9 || cmd_buf[2] != 0x03 || cmd_buf[7] != 0x55 || cmd_buf[8] != 0x2b)
        return false;
    uint32_t page;
    if (!ps2_page_available(&page))
        return false;

    uint8_t page_data[MCA_PS2_PAGE_RAW];
    ps2_mc_data_interface_setup_read_page(page, false, true);
    volatile ps2_mcdi_page_t *page_state = ps2_mc_data_interface_get_page(page);
    if (!page_state || !page_state->data)
        return false;
    ps2_mc_data_interface_wait_for_byte(MCA_PS2_PAGE_DATA - 1);
    memcpy(page_data, (const void *)page_state->data, MCA_PS2_PAGE_DATA);
    calculate_ps2_ecc(page_data + MCA_PS2_PAGE_DATA, page_data);
    set_long_success(page_data, sizeof(page_data));
    return true;
}

static bool process_ps2_page_write(void) {
    /* AA 57 03 <page LE32> <528 raw bytes> 55 2B */
    if (cmd_len != 537 || cmd_buf[2] != 0x03 || cmd_buf[535] != 0x55 || cmd_buf[536] != 0x2b)
        return false;
    uint32_t page;
    if (!ps2_page_available(&page))
        return false;

    /* sd2psXtd stores the 512 data bytes and regenerates ECC when read. */
    ps2_mc_data_interface_write_mc(page, cmd_buf + 7);
    set_short_response(0x5a);
    return true;
}

static void process_command(void) {
    if (cmd_len < 2 || cmd_buf[0] != 0xaa)
        return;

    /* The adapter stays present while the card is exchanged, but requests
     * for the stopped emulator must not be queued to core 1. */
    if (!g_enabled || (!mca_transport_is_active() && cmd_buf[1] != 0x40)) {
        set_short_response(0xaf);
        return;
    }

    switch (cmd_buf[1]) {
        case 0x40:
            if (cmd_len == 2) {
                set_short_response(settings_get_mode(true) == MODE_PS2 ? 2 : 1);
                return;
            }
            break;
        case 0x42:
            if (cmd_len >= 4) {
                uint16_t payload = (uint16_t)cmd_buf[2] | ((uint16_t)cmd_buf[3] << 8);
                if (payload + 4 == cmd_len && mca_transport_submit(cmd_buf + 4, payload))
                    return; /* Core 1 will provide the response. */
            }
            break;
        case 0x52:
            if (process_ps2_page_read())
                return;
            break;
        case 0x57:
            if (process_ps2_page_write())
                return;
            break;
    }
    set_short_response(0xaf);
}

static void parse_out(uint8_t const *data, uint16_t len) {
    if (!len)
        return;

    if (cmd_len + len > sizeof(cmd_buf)) {
        cmd_len = expected_len = 0;
        return;
    }
    memcpy(cmd_buf + cmd_len, data, len);
    cmd_len += len;

    if (cmd_len >= 2 && cmd_buf[0] != 0xaa) {
        cmd_len = expected_len = 0;
        return;
    }
    if (cmd_len >= 2 && !expected_len) {
        switch (cmd_buf[1]) {
            case 0x40:
                expected_len = 2;
                break;
            case 0x42:
                if (cmd_len >= 4)
                    expected_len = 4 + ((uint16_t)cmd_buf[2] | ((uint16_t)cmd_buf[3] << 8));
                break;
            case 0x52:
                expected_len = 9;
                break;
            case 0x57:
                expected_len = 537;
                break;
            default:
                expected_len = 2;
                break;
        }
    }

    if (expected_len && cmd_len >= expected_len) {
        process_command();
        cmd_len = expected_len = 0;
    }
}

static void mca_driver_init(void) {
    cmd_len = expected_len = response_len = 0;
    in_busy = false;
    reset_itf_num = 0xff;
}

static void mca_driver_reset(uint8_t rhport) {
    (void)rhport;
    mca_driver_init();
}

static uint16_t mca_driver_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
    if (itf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC &&
        itf->bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
        itf->bInterfaceProtocol == RESET_INTERFACE_PROTOCOL &&
        itf->bNumEndpoints == 0 && max_len >= itf->bLength) {
        reset_itf_num = itf->bInterfaceNumber;
        return itf->bLength;
    }
    if (itf->bInterfaceClass != 0xff || itf->bInterfaceSubClass != 0x00 || itf->bInterfaceProtocol != 0xff)
        return 0;
    if (max_len < 9 + 3 * 7)
        return 0;

    uint8_t const *p = (uint8_t const *)itf + itf->bLength;
    uint16_t consumed = itf->bLength;
    for (int i = 0; i < 3; ++i) {
        tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
        if (ep->bDescriptorType != TUSB_DESC_ENDPOINT || !usbd_edpt_open(rhport, ep))
            return 0;
        p += ep->bLength;
        consumed += ep->bLength;
    }
    prime_out(rhport);
    return consumed;
}

static bool mca_driver_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;
    if (request->wIndex != reset_itf_num ||
        request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE)
        return false;
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    if (request->bRequest == RESET_REQUEST_BOOTSEL) {
        int gpio = -1;
        bool active_low = false;
        if (request->wValue & 0x100)
            gpio = request->wValue >> 9u;
        active_low = (request->wValue & 0x200) != 0;
        rom_reset_usb_boot_extra(gpio, request->wValue & 0x7f, active_low);
    }
    if (request->bRequest == RESET_REQUEST_FLASH) {
        watchdog_reboot(0, 0, 100);
        return true;
    }
    return false;
}

static bool mca_driver_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t transferred) {
    if (result != XFER_RESULT_SUCCESS)
        return true;

    if (ep_addr == MCA_EP_OUT) {
        parse_out(ep_out_buf, (uint16_t)transferred);
        prime_out(rhport);
        queue_in(rhport);
        return true;
    }
    if (ep_addr == MCA_EP_IN) {
        in_busy = false;
        queue_in(rhport);
        return true;
    }
    return false;
}

static usbd_class_driver_t const mca_driver = {
    .name = "MCA",
    .init = mca_driver_init,
    .reset = mca_driver_reset,
    .open = mca_driver_open,
    .control_xfer_cb = mca_driver_control,
    .xfer_cb = mca_driver_xfer,
    .sof = NULL,
};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &mca_driver;
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    (void)itf;
    if (coding->bit_rate == 1200)
        rom_reset_usb_boot_extra(-1, 0, false);
}

/* AA42 may leave core1 waiting for SD work that only core0 can finish. */
static inline void mca_service_ps2_storage(void) {
    if (!g_enabled || !mca_transport_is_active() || settings_get_mode(true) != MODE_PS2)
        return;
    if (!ps2_cardman_is_idle())
        return;

    if (ps2_mc_data_interface_delay_required())
        ps2_mc_data_interface_task();
}

void mca_usb_task(void) {
    /* Drain work from the preceding command before accepting another. */
    mca_service_ps2_storage();

    tud_task_ext(0, false);

    /* This USB command may have queued more work on core1. */
    mca_service_ps2_storage();

    if (g_enabled && mca_transport_response_ready() && !response_len && !in_busy) {
        uint16_t n = mca_transport_take_response(response_buf + 4, MCA_TRANSACTION_MAX);
        response_buf[0] = 0x55;
        response_buf[1] = 0x5a;
        response_buf[2] = (uint8_t)n;
        response_buf[3] = (uint8_t)(n >> 8);
        response_len = n + 4;
        queue_in(0);
    }
}
