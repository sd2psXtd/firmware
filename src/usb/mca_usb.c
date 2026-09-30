#include "mca_usb.h"
#include "mca_transport.h"
#include "mca_page_transfer.h"

#include "bsp/board.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/stdio_usb/reset_interface.h"
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
#define MCA_EP_INT  0x83
#define MCA_PACKET 64

/* The USB identity must stay fixed until the host sees a disconnect. Loading
 * settings.ini can change the saved mode before we switch this profile. */
static bool g_usb_mca_profile;
static bool g_card_switching;

static uint8_t ep_out_buf[MCA_PACKET] CFG_TUSB_MEM_ALIGN;
static uint8_t ep_in_buf[MCA_TRANSACTION_MAX + 4] CFG_TUSB_MEM_ALIGN;
static uint8_t ep_int_buf[1] CFG_TUSB_MEM_ALIGN;
static bool int_open, int_busy, card_present, removal_pending;
static uint8_t reported_presence;

static uint8_t cmd_buf[MCA_TRANSACTION_MAX + 4];
static uint16_t cmd_len;
static uint8_t response_buf[MCA_TRANSACTION_MAX + 4];
static uint16_t response_len;
static bool in_busy;
static uint8_t reset_itf_num;

/* One accepted card request owns the transport. A bus reset drops only its
 * USB reply; card work still runs to completion (or cooperative recovery). */
enum { REQUEST_IDLE, REQUEST_RAW, REQUEST_PAGE, REQUEST_RECOVER };
static uint8_t request_state;
static bool reply_pending;
static uint64_t request_deadline;

/* Optional read-only EP0 diagnostics. Keep the failure sample before recovery
 * clears transport/cursors; normal Sony bulk commands remain unchanged. */
static uint8_t last_stall[24], diagnostic_reply[24];

static void sample_progress(uint8_t sample[24], uint8_t reason) {
    bool ps2_running = ps2_memory_card_running();
    uint32_t cursor[] = { read_sector, readptr, (uint32_t)time_us_64() };

    sample[0] = 1; /* Format version. */
    sample[1] = reason;
    sample[2] = request_state;
    sample[3] = (ps2_mc_data_interface_get_sdmode() ? 1u : 0u) |
                (ps2_mc_data_interface_delay_required() ? 2u : 0u) |
                (card_active ? 4u : 0u) |
                (ps2_running && ps2_mc_data_interface_data_available() ? 8u : 0u) |
                (ps2_running ? 16u : 0u) | (in_busy ? 32u : 0u) |
                (response_len ? 64u : 0u) | (ps2_cardman_is_idle() ? 128u : 0u);
    mca_transport_get_progress(sample + 4);
    memcpy(sample + 12, cursor, sizeof(cursor)); /* RP2040 is little endian. */
}

static void clear_request(void) {
    mca_page_transfer_reset();
    request_state = REQUEST_IDLE;
    reply_pending = false;
}

static void accept_request(uint8_t state) {
    request_state = state;
    reply_pending = true;
    request_deadline = time_us_64() + MCA_CARD_REPLY_TIMEOUT_US;
}

static void connect_usb(void) {
    tusb_init();
    /* With tinyusb_device linked, stdio_usb_init() only registers the CDC
     * driver; the application owns TinyUSB and runs tud_task_ext(). */
    if (!g_usb_mca_profile)
        stdio_usb_init();
}

static void disconnect_usb(void) {
    if (!g_usb_mca_profile)
        stdio_usb_deinit();
    tud_disconnect();
    sleep_ms(150);
    tud_deinit(0);
    int_open = false;
}

void mca_usb_init(void) {
    mca_transport_init();
    clear_request();
    memset(last_stall, 0, sizeof(last_stall));
    g_card_switching = false;
    g_usb_mca_profile = settings_get_mca_enabled();
    mca_transport_set_active(g_usb_mca_profile);
    board_init();

    /* Pico CDC/picotool starts immediately; the first MCA connection waits
     * until the card is ready and main() calls mca_usb_start(). */
    if (!g_usb_mca_profile)
        connect_usb();
}

void mca_usb_start(void) {
    if (!tud_inited())
        connect_usb();
}

void mca_usb_prepare(void) {
    bool enabled = settings_get_mca_enabled();

    /* Called by the outer mode loop after deinit/reset, never from a GUI or
     * USB callback. The new emulator and USB profile use the same setting. */
    if (g_usb_mca_profile != enabled) {
        if (tud_inited())
            disconnect_usb();
        /* A transport change starts a new session; timeout recovery within
         * the same transport preserves the host-selected terminator/auth. */
        term = 0xff;
        ps2_mc_auth_invalidate();
    }
    g_usb_mca_profile = enabled;
    mca_transport_set_active(enabled);
    clear_request();
    g_card_switching = false;
    /* CDC/picotool can connect immediately. MCA waits for card readiness. */
    if (!enabled && !tud_inited())
        connect_usb();
}

bool mca_usb_profile_is_mca(void) { return g_usb_mca_profile; }
bool mca_usb_is_started(void) { return tud_inited(); }

uint8_t mca_usb_receive_first(uint8_t *byte, volatile int *exit_request) {
    uint8_t result = mca_transport_receive_first(byte, exit_request);

    /* MCA has no /CS edge. Receiving the first byte selects the card. */
    if (result == MCA_RECEIVE_OK)
        card_active = true;
    return result;
}

void mca_usb_card_switch_begin(void) {
    g_card_switching = g_usb_mca_profile && mca_transport_is_active();
    if (g_card_switching) {
        mca_transport_set_active(false);
        clear_request();
    }
}

void mca_usb_card_switch_delay(void) {
    uint64_t resume_time;

    if (!g_card_switching || !tud_inited()) {
        sleep_ms(500);
    } else {
        /* Core 0 owns TinyUSB; keep servicing the card-absent interval. */
        resume_time = time_us_64() + 500000;
        while (time_us_64() < resume_time) {
            mca_usb_task();
            sleep_ms(1);
        }
    }
}

void mca_usb_card_switch_end(void) {
    if (g_card_switching) {
        /* The new card must not inherit the previous MagicGate session. */
        ps2_mc_auth_invalidate();
        mca_transport_set_active(true);
        g_card_switching = false;
    }
}

static void prime_out(uint8_t rhport) {
    usbd_edpt_xfer(rhport, MCA_EP_OUT, ep_out_buf, sizeof(ep_out_buf));
}

static void queue_in(uint8_t rhport) {
    if (!in_busy && response_len) {
        /* One transfer: the final short packet completes single host reads. */
        memcpy(ep_in_buf, response_buf, response_len);
        if (usbd_edpt_xfer(rhport, MCA_EP_IN, ep_in_buf, response_len)) {
            response_len = 0;
            in_busy = true;
        }
    }
}

static void set_short_response(uint8_t value) {
    response_buf[0] = 0x55;
    response_buf[1] = value;
    response_len = 2;
}

void mca_usb_card_exit(void) {
    bool send_reply = reply_pending;

    /* Core 0 calls this only after the emulator has stopped. USB owns abort,
     * sequencer cleanup and the reply; the physical transport needs no work. */
    if (g_usb_mca_profile) {
        mca_transport_abort();
        clear_request();
        if (send_reply && tud_inited()) {
            set_short_response(0xaf);
            queue_in(0);
        }
    }
}

/* Payload has already been written to response_buf + 4. */
static void set_long_success(uint16_t len) {
    response_buf[0] = 0x55;
    response_buf[1] = 0x5a;
    response_buf[2] = (uint8_t)len;
    response_buf[3] = (uint8_t)(len >> 8);
    response_len = len + 4;
}

static bool ps2_page_available(uint32_t *page) {
    bool available = false;

    if (settings_get_mode(true) == MODE_PS2 &&
        settings_get_ps2_variant() == PS2_VARIANT_RETAIL &&
        ps2_cardman_is_accessible() && ps2_mc_auth_isValid()) {
        *page = (uint32_t)cmd_buf[3] |
                ((uint32_t)cmd_buf[4] << 8) |
                ((uint32_t)cmd_buf[5] << 16) |
                ((uint32_t)cmd_buf[6] << 24);
        available = ((uint64_t)*page + 1u) * MCA_PS2_PAGE_DATA <= ps2_cardman_get_card_size();
    }
    return available;
}

static bool process_ps2_page(bool write) {
    uint16_t expected = write ? MCA_PS2_PAGE_RAW + 9 : 9;
    uint32_t page;
    bool accepted = false;

    /* AA 52/57 03 <page LE32> [528 write bytes] 55 2B */
    if (cmd_len == expected && cmd_buf[2] == 0x03 &&
        cmd_buf[expected - 2] == 0x55 && cmd_buf[expected - 1] == 0x2b &&
        ps2_page_available(&page) &&
        mca_page_transfer_start(page, write ? cmd_buf + 7 : NULL, time_us_64())) {
        accept_request(REQUEST_PAGE);
        accepted = true;
    }
    return accepted;
}

static uint8_t card_type(void) {
    uint8_t type = 0;

    if (mca_transport_is_active()) {
        if (settings_get_mode(true) != MODE_PS2)
            type = 1;
        /* open() starts asynchronous creation/PSRAM loading. Report removal
         * until the new card can answer without waiting for that load. */
        else if (ps2_memory_card_running() && ps2_cardman_is_idle() &&
                 ps2_cardman_is_accessible())
            type = 2;
    }
    return type;
}

static void service_card_presence(uint8_t rhport) {
    bool present;
    uint8_t status;

    /* libmcadpt's interrupt callback tests bit 0: 1 -> card inserted (event 3),
     * 0 -> card removed (event 4). Preserve removal if the previous interrupt
     * is still awaiting a host poll when the replacement card becomes ready. */
    if (int_open) {
        present = card_type() != 0;
        if (card_present && !present)
            removal_pending = true;
        card_present = present;
        status = removal_pending ? 0 : present;
        if (!int_busy && (removal_pending || reported_presence != status)) {
            ep_int_buf[0] = status;
            if (usbd_edpt_xfer(rhport, MCA_EP_INT, ep_int_buf, sizeof(ep_int_buf))) {
                int_busy = true;
                reported_presence = status;
                if (!status)
                    removal_pending = false;
            }
        }
    }
}

static void process_command(void) {
    uint8_t type;
    uint16_t payload;
    bool accepted = false;

    if (cmd_len >= 2 && cmd_buf[0] == 0xaa) {
        /* AA40 and card commands use the same switch/loading readiness gate. */
        type = card_type();
        if (g_usb_mca_profile &&
            (cmd_buf[1] == 0x40 || (type && request_state == REQUEST_IDLE))) {
            switch (cmd_buf[1]) {
                case 0x40:
                    if (cmd_len == 2) {
                        set_short_response(type);
                        accepted = true;
                    }
                    break;
                case 0x42:
                    if (cmd_len >= 4) {
                        payload = (uint16_t)cmd_buf[2] | ((uint16_t)cmd_buf[3] << 8);
                        if (payload + 4 == cmd_len && mca_transport_submit(cmd_buf + 4, payload)) {
                            accept_request(REQUEST_RAW);
                            accepted = true; /* Core 1 provides the reply. */
                        }
                    }
                    break;
                case 0x52:
                    accepted = process_ps2_page(false);
                    break;
                case 0x57:
                    accepted = process_ps2_page(true);
                    break;
                default:
                    break;
            }
        }
        if (!accepted)
            set_short_response(0xaf);
    }
}

static uint32_t command_length(void) {
    uint32_t length = 2;

    switch (cmd_buf[1]) {
        case 0x42:
            length = cmd_len >= 4 ?
                4u + ((uint16_t)cmd_buf[2] | ((uint16_t)cmd_buf[3] << 8)) : 0;
            break;
        case 0x52:
            length = 9;
            break;
        case 0x57:
            length = MCA_PS2_PAGE_RAW + 9;
            break;
        default:
            break;
    }
    return length;
}

static void parse_out(uint8_t const *data, uint16_t len) {
    uint32_t expected_len;

    if (len && cmd_len + len > sizeof(cmd_buf)) {
        cmd_len = 0;
    } else if (len) {
        memcpy(cmd_buf + cmd_len, data, len);
        cmd_len += len;
    }
    if (len && cmd_len >= 2) {
        if (cmd_buf[0] != 0xaa) {
            cmd_len = 0;
        } else {
            expected_len = command_length();
            if (expected_len && cmd_len >= expected_len) {
                process_command();
                cmd_len = 0;
            }
        }
    }
}

static void mca_driver_init(void) {
    /* A bus reset can occur during an internal card transaction. Let it
     * finish (including a write commit), but never send a stale USB reply. */
    reply_pending = false;
    cmd_len = response_len = 0;
    in_busy = false;
    int_open = int_busy = card_present = removal_pending = false;
    reported_presence = 0xff; /* Send the initial state after enumeration. */
    reset_itf_num = 0xff;
}

static void mca_driver_reset(uint8_t rhport) {
    (void)rhport;
    mca_driver_init();
}

static uint16_t mca_driver_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len) {
    uint8_t const *p;
    tusb_desc_endpoint_t const *ep;
    uint16_t consumed = 0;
    bool valid = true;
    int i;

    if (itf->bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC &&
        itf->bInterfaceSubClass == RESET_INTERFACE_SUBCLASS &&
        itf->bInterfaceProtocol == RESET_INTERFACE_PROTOCOL &&
        itf->bNumEndpoints == 0 && max_len >= itf->bLength) {
        reset_itf_num = itf->bInterfaceNumber;
        consumed = itf->bLength;
    } else if (itf->bInterfaceClass == 0xff && itf->bInterfaceSubClass == 0x00 &&
               itf->bInterfaceProtocol == 0xff && max_len >= 9 + 3 * 7) {
        p = (uint8_t const *)itf + itf->bLength;
        consumed = itf->bLength;
        for (i = 0; i < 3 && valid; ++i) {
            ep = (tusb_desc_endpoint_t const *)p;
            valid = ep->bDescriptorType == TUSB_DESC_ENDPOINT && usbd_edpt_open(rhport, ep);
            if (valid) {
                p += ep->bLength;
                consumed += ep->bLength;
            }
        }
        if (valid) {
            prime_out(rhport);
            int_open = true;
            service_card_presence(rhport);
        } else {
            consumed = 0;
        }
    }
    return consumed;
}

static bool mca_driver_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    bool handled = false;
    int gpio = -1;
    bool active_low;

    /* C1/5F, interface 0: wValue 0 = live, 1 = last failure. This is a debug
     * extension, not a genuine-adapter command, and never queues card work. */
    if (g_usb_mca_profile && request->bmRequestType == 0xc1 &&
         request->bRequest == 0x5f && request->wIndex == 0 && request->wValue <= 1) {
        handled = true;
        if (stage == CONTROL_STAGE_SETUP) {
            if (request->wValue)
                memcpy(diagnostic_reply, last_stall, sizeof(diagnostic_reply));
            else
                sample_progress(diagnostic_reply, 0);
            handled = tud_control_xfer(rhport, request, diagnostic_reply, sizeof(diagnostic_reply));
        }
    } else if (request->wIndex == reset_itf_num &&
               request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE) {
        if (stage != CONTROL_STAGE_SETUP) {
            handled = true;
        } else if (request->bRequest == RESET_REQUEST_BOOTSEL) {
            if (request->wValue & 0x100)
                gpio = request->wValue >> 9u;
            active_low = (request->wValue & 0x200) != 0;
            rom_reset_usb_boot_extra(gpio, request->wValue & 0x7f, active_low);
        } else if (request->bRequest == RESET_REQUEST_FLASH) {
            watchdog_reboot(0, 0, 100);
            handled = true;
        }
    }
    return handled;
}

static bool mca_driver_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t transferred) {
    bool handled = true;

    if (ep_addr == MCA_EP_INT) {
        int_busy = false;
        if (result != XFER_RESULT_SUCCESS || transferred != sizeof(ep_int_buf)) {
            if (!ep_int_buf[0])
                removal_pending = true;
            reported_presence = 0xff;
        }
        service_card_presence(rhport);
    } else if (result == XFER_RESULT_SUCCESS && ep_addr == MCA_EP_OUT) {
        parse_out(ep_out_buf, (uint16_t)transferred);
        prime_out(rhport);
        queue_in(rhport);
    } else if (result == XFER_RESULT_SUCCESS && ep_addr == MCA_EP_IN) {
        in_busy = false;
        queue_in(rhport);
    } else if (result == XFER_RESULT_SUCCESS) {
        handled = false;
    }
    return handled;
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

/* Card transactions may leave core1 waiting for SD work owned by core0. */
static inline void mca_service_ps2_storage(void) {
    if (mca_transport_is_active() && settings_get_mode(true) == MODE_PS2) {
        if (request_state == REQUEST_RECOVER && !ps2_cardman_is_idle())
            ps2_cardman_task();
        if (ps2_cardman_is_idle() && ps2_mc_data_interface_delay_required())
            ps2_mc_data_interface_task();
    }
}

static bool service_page_request(uint64_t now_us) {
    mca_page_result_t result = mca_page_transfer_task(response_buf + 4, now_us);
    bool complete = false;

    switch (result) {
        case MCA_PAGE_FAILED:
            sample_progress(last_stall, 2);
            request_state = REQUEST_RECOVER;
            break;
        case MCA_PAGE_PENDING:
            break;
        case MCA_PAGE_READ_DONE:
            if (reply_pending)
                set_long_success(MCA_PS2_PAGE_RAW);
            complete = true;
            break;
        case MCA_PAGE_WRITE_DONE:
            if (reply_pending)
                set_short_response(0x5a);
            complete = true;
            break;
        default:
            complete = true;
            break;
    }
    return complete;
}

static void service_card_request(void) {
    uint64_t now_us = time_us_64();
    uint8_t retry_counter;
    uint16_t n;
    bool complete = false;

    /* Check deadlines even when an earlier USB IN is still pending. */
    if ((request_state == REQUEST_PAGE && mca_page_transfer_expired(now_us)) ||
        (request_state == REQUEST_RAW && ps2_memory_card_running() &&
         !mca_transport_response_ready() && now_us >= request_deadline)) {
        sample_progress(last_stall, 1);
        request_state = REQUEST_RECOVER;
    }

    /* Recovery is outside TinyUSB callbacks, and never waits for SD work.
     * New card requests remain rejected until staging and transport are clear. */
    if (request_state == REQUEST_RECOVER) {
        retry_counter = mmceman_mcman_retry_counter;
        if (ps2_memory_card_try_exit()) {
            mmceman_mcman_retry_counter = retry_counter;
            ps2_memory_card_enter();
        }
    } else if (!response_len && !in_busy) {
        switch (request_state) {
            case REQUEST_PAGE:
                complete = service_page_request(now_us);
                break;
            case REQUEST_RAW:
                if (mca_transport_response_ready()) {
                    n = mca_transport_take_response(response_buf + 4, MCA_TRANSACTION_MAX);
                    if (reply_pending)
                        set_long_success(n);
                    complete = true;
                }
                break;
            default:
                break;
        }
        if (complete) {
            clear_request();
            queue_in(0);
        }
    }
}

void mca_usb_task(void) {
    /* Service SD dependencies on either side of USB callbacks. */
    mca_service_ps2_storage();
    tud_task_ext(0, false);
    mca_service_ps2_storage();
    service_card_request();
    service_card_presence(0);
}
