#include "usb_serial.h"

#include "config.h"
#include "hardware/irq.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "tusb.h"

/*
 * The firmware links tinyusb_device itself (MCA needs its own driver and
 * descriptors), which compiles out pico-sdk's stdio_usb background task. The
 * Serial profile does the same thing here: a 1 ms timer pends a low-priority
 * IRQ that runs tud_task(), so the CDC port is serviced during long main-loop
 * work such as the PSRAM card preload. MCA and mass storage stay polled from
 * the main loop (the MCA protocol and the SD-backed MSC callbacks expect
 * thread context).
 *
 * stdio_usb also calls tud_task() from thread context. stdio goes through the
 * wrapper below, which marks the stack busy; the IRQ skips that tick. The
 * thread cannot interrupt the IRQ on this core, so the flag is sufficient.
 */
#define BACKGROUND_INTERVAL_US 1000

static bool background_active;
static int background_irq = -1;
static repeating_timer_t background_timer;
static volatile bool stdio_busy;

static void guarded_out_chars(const char *buf, int len) {
    stdio_busy = true;
    stdio_usb.out_chars(buf, len);
    stdio_busy = false;
}

static void guarded_out_flush(void) {
    stdio_busy = true;
    stdio_usb.out_flush();
    stdio_busy = false;
}

static int guarded_in_chars(char *buf, int len) {
    stdio_busy = true;
    int ret = stdio_usb.in_chars(buf, len);
    stdio_busy = false;
    return ret;
}

static void guarded_set_chars_available_callback(void (*fn)(void *), void *param) {
    if (stdio_usb.set_chars_available_callback)
        stdio_usb.set_chars_available_callback(fn, param);
}

static stdio_driver_t stdio_usb_guarded = {
    .out_chars = guarded_out_chars,
    .out_flush = guarded_out_flush,
    .in_chars = guarded_in_chars,
    .set_chars_available_callback = guarded_set_chars_available_callback,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_USB_DEFAULT_CRLF,
#endif
};

static void background_worker_irq(void) {
    if (!stdio_busy)
        tud_task_ext(0, false);
}

static bool background_timer_cb(repeating_timer_t *timer) {
    (void)timer;
    irq_set_pending((uint)background_irq);
    return true;
}

static void background_start(void) {
    if (background_irq < 0) {
        background_irq = user_irq_claim_unused(true);
        irq_set_exclusive_handler((uint)background_irq, background_worker_irq);
        irq_set_priority((uint)background_irq, PICO_LOWEST_IRQ_PRIORITY);
    }
    irq_set_enabled((uint)background_irq, true);
    background_active = add_repeating_timer_us(-BACKGROUND_INTERVAL_US, background_timer_cb, NULL,
                                               &background_timer);
}

static void background_stop(void) {
    if (!background_active)
        return;
    cancel_repeating_timer(&background_timer);
    irq_set_enabled((uint)background_irq, false);
    background_active = false;
}

/* DEBUG_STARTUP_DELAY: give a terminal time to attach before the boot logs. */
static void wait_for_terminal(void) {
#if DEBUG_USB_UART
    static bool startup_waited;
    if (startup_waited)
        return;
    startup_waited = true;
    uint64_t end = time_us_64() + (uint64_t)DEBUG_STARTUP_DELAY_MS * 1000;
    while (!stdio_usb_connected() && time_us_64() < end) {
        if (!background_active)
            tud_task_ext(0, false);
        sleep_ms(1);
    }
    if (DEBUG_STARTUP_DELAY_MS > 0 && stdio_usb_connected())
        sleep_ms(PICO_STDIO_USB_POST_CONNECT_WAIT_DELAY_MS);
#endif
}

void usb_serial_start(bool background) {
    if (background)
        background_start();
    /* Sets up stdio_usb's state; output goes through the guarded wrapper. */
    stdio_usb_init();
    stdio_set_driver_enabled(&stdio_usb, false);
    stdio_set_driver_enabled(&stdio_usb_guarded, true);
    wait_for_terminal();
}

void usb_serial_stop(void) {
    stdio_set_driver_enabled(&stdio_usb_guarded, false);
    sleep_ms(PICO_STDIO_USB_DEINIT_DELAY_MS);
    background_stop();
}

bool usb_serial_in_background(void) {
    return background_active;
}
