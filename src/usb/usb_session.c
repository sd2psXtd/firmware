#include "usb_session.h"

#include "bsp/board.h"
#include "config.h"
#include "mca_device.h"
#include "mca_protocol.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "settings.h"
#include "tusb.h"
#include "usb_drivers.h"

static int session_usb_mode;

static void usb_session_stop(void) {
    if (!mca_device_profile_is_mca()) {
        stdio_set_driver_enabled(&stdio_usb, false);
        sleep_ms(PICO_STDIO_USB_DEINIT_DELAY_MS);
    }
    tud_disconnect();
    /* Let the host see the disconnect before the next profile connects. */
    sleep_ms(20);
    tud_deinit(0);
    mca_protocol_reset();
}

void usb_session_init(void) {
    mca_protocol_init();
    board_init();
}

void usb_session_prepare(void) {
    int usb_mode = settings_get_usb_mode(true);
    if (tud_inited()) {
        if (session_usb_mode == usb_mode)
            return;
        /* The SD settings reload may have changed the requested profile. */
        usb_session_stop();
    }
    session_usb_mode = usb_mode;
    mca_device_init(session_usb_mode == USB_MODE_MCA, mca_protocol_callbacks());
}

int usb_session_get_mode(void) {
    return session_usb_mode;
}

void usb_session_start(void) {
    if (tud_inited())
        return;
    tusb_init();
    if (!mca_device_profile_is_mca()) {
        stdio_usb_init();
#if DEBUG_USB_UART
        static bool startup_waited;
        if (!startup_waited) {
            uint64_t end = time_us_64() + (uint64_t)DEBUG_STARTUP_DELAY_MS * 1000;
            while (!stdio_usb_connected() && time_us_64() < end) {
                tud_task_ext(0, false);
                sleep_ms(1);
            }
            if (DEBUG_STARTUP_DELAY_MS > 0 && stdio_usb_connected())
                sleep_ms(PICO_STDIO_USB_POST_CONNECT_WAIT_DELAY_MS);
            startup_waited = true;
        }
#endif
    }
}

bool usb_session_continue(bool mode_running) {
    return mode_running || !mca_protocol_idle();
}

void usb_session_poll(void) {
    tud_task_ext(0, false);
}

void usb_session_end_mode(void) {
    if (session_usb_mode == USB_MODE_UART && settings_get_usb_mode(true) == USB_MODE_UART)
        return;

    if (mca_device_profile_is_mca())
        mca_protocol_drain_reply(500);
    usb_session_stop();
}

/* TinyUSB: application class drivers, tried before the built-in ones. */
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    static usbd_class_driver_t drivers[2];
    drivers[0] = *mca_device_driver();
    drivers[1] = *usb_reset_itf_driver();
    *driver_count = 2;
    return drivers;
}

/* Serial profile: a 1200 baud "touch" reboots into BOOTSEL (Arduino style). */
void tud_cdc_line_coding_cb(uint8_t interface_number, cdc_line_coding_t const *coding) {
    (void)interface_number;
    if (coding->bit_rate == 1200)
        rom_reset_usb_boot_extra(-1, 0, false);
}
