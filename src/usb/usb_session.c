#include "usb_session.h"

#include "bsp/board.h"
#include "mca_device.h"
#include "mca_protocol.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "settings.h"
#include "tusb.h"
#include "usb_drivers.h"
#include "usb_serial.h"

static int session_usb_mode;

static void usb_session_stop(void) {
    if (!mca_device_profile_is_mca())
        usb_serial_stop();
    tud_disconnect();
    /* Let the host see the disconnect before the next profile connects. */
    sleep_ms(20);
    tud_deinit(0);
    mca_protocol_reset();
}

void usb_session_init(void) {
    mca_protocol_init();
    board_init();
    /* Bring up the Serial port right away (serviced in the background) using
     * the flash settings; the SD settings reload restarts it only if the
     * profile changes. MCA and mass storage start right before their mode. */
    usb_session_prepare();
    if (session_usb_mode == USB_MODE_UART)
        usb_session_start();
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
    /* Serial is serviced in the background; mass storage (CDC + MSC) stays
     * main-loop polled because its MSC callbacks access the SD card. */
    if (!mca_device_profile_is_mca())
        usb_serial_start(session_usb_mode == USB_MODE_UART);
}

bool usb_session_continue(bool mode_running) {
    return mode_running || !mca_protocol_idle();
}

void usb_session_poll(void) {
    if (!usb_serial_in_background())
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
