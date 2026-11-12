#include "usb_storage.h"
#include "mca_device.h"
#include "settings.h"
#include "sd.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

bool tud_msc_test_unit_ready_cb(uint8_t);
bool tud_msc_start_stop_cb(uint8_t, uint8_t, bool, bool);
void tud_msc_scsi_complete_cb(uint8_t, const uint8_t[16]);
int32_t tud_msc_scsi_cb(uint8_t, const uint8_t[16], void *, uint16_t);
int32_t tud_msc_read10_cb(uint8_t, uint32_t, uint32_t, void *, uint32_t);
int32_t tud_msc_write10_cb(uint8_t, uint32_t, uint32_t, uint8_t *, uint32_t);

static uint8_t disk[3][512], sense;
static int saved_mode = USB_MODE_UART, requested_mode = USB_MODE_MASS;
static jmp_buf fatal_return;
static bool fatal_expected;
void fatal(int error, const char *format, ...) {
    (void)error; (void)format;
    assert(fatal_expected);
    longjmp(fatal_return, 1);
}
static void expect_failure(void (*operation)(void)) {
    fatal_expected = true;
    if (!setjmp(fatal_return)) {
        operation();
        assert(!"Expected fatal SD failure");
    }
    fatal_expected = false;
}
void sleep_ms(uint32_t milliseconds) { assert(milliseconds == 20); }
int settings_get_usb_mode(bool current) { return current ? requested_mode : saved_mode; }
void settings_set_usb_mode(int mode) { requested_mode = mode; }
static int session_mode = USB_MODE_UART;
int usb_session_get_mode(void) { return session_mode; }
static bool sync_ok = true, mount_ok = true, io_ok = true, unmount_ok = true;
bool sd_unmount(void) { return unmount_ok; }
bool sd_remount(void) { return mount_ok; }
bool sd_sync_device(void) { return sync_ok; }
uint32_t sd_sector_count(void) { return 3; }
bool sd_read_sector(uint32_t n, uint8_t *dst) {
    assert(n < 3);
    if (!io_ok) return false;
    memcpy(dst, disk[n], 512); return true;
}
bool sd_write_sector(uint32_t n, const uint8_t *src) {
    assert(n < 3);
    if (!io_ok) return false;
    memcpy(disk[n], src, 512); return true;
}
void tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq) {
    (void)lun; (void)asc; (void)ascq; sense = key;
}
int main(void) {
    uint8_t buf[600], out[600], eject[16] = {0x1b, 0, 0, 0, 2};
    uint8_t sync[16] = {0x35};
    assert(!tud_msc_test_unit_ready_cb(0));
    unmount_ok = false;
    expect_failure(usb_storage_init);
    assert(!tud_msc_test_unit_ready_cb(0));
    unmount_ok = true;
    usb_storage_init();
    assert(usb_storage_task());
    session_mode = USB_MODE_MASS;
    assert(tud_msc_test_unit_ready_cb(0));
    memset(disk, 0x55, sizeof(disk));
    memset(buf, 0xab, sizeof(buf));
    assert(tud_msc_write10_cb(0, 0, 500, buf, sizeof(buf)) == 600);
    assert(disk[0][499] == 0x55 && disk[0][500] == 0xab);
    assert(disk[2][75] == 0xab && disk[2][76] == 0x55);
    assert(tud_msc_read10_cb(0, 0, 500, out, sizeof(out)) == 600);
    assert(!memcmp(buf, out, sizeof(buf)));
    assert(tud_msc_read10_cb(0, 3, 0, out, 1) == -1 && sense == 5);
    assert(tud_msc_write10_cb(0, UINT32_MAX, UINT32_MAX, buf, 1) == -1);
    assert(tud_msc_read10_cb(0, 2, 512, out, 0) == 0);
    io_ok = false;
    assert(tud_msc_read10_cb(0, 0, 0, out, 512) == -1 && sense == 3);
    io_ok = true;
    assert(tud_msc_start_stop_cb(0, 0, false, false));
    tud_msc_scsi_complete_cb(0, sync);
    assert(usb_storage_task() && tud_msc_test_unit_ready_cb(0));
    sync_ok = false;
    assert(tud_msc_scsi_cb(0, sync, NULL, 0) == -1);
    assert(!tud_msc_start_stop_cb(0, 0, false, true));
    tud_msc_scsi_complete_cb(0, eject);
    assert(usb_storage_task());
    sync_ok = true;
    assert(tud_msc_scsi_cb(0, sync, NULL, 0) == 0);
    assert(tud_msc_start_stop_cb(0, 0, false, true));
    assert(!tud_msc_test_unit_ready_cb(0) && usb_storage_task());
    assert(tud_msc_write10_cb(0, 0, 0, buf, 512) == -1);
    uint8_t stop[16] = {0x1b};
    tud_msc_scsi_complete_cb(0, stop);
    assert(usb_storage_task());
    tud_msc_scsi_complete_cb(0, eject);
    assert(!usb_storage_task());
    mount_ok = false;
    expect_failure(usb_storage_deinit);
    assert(requested_mode == USB_MODE_MASS);
    mount_ok = true;
    usb_storage_deinit();
    assert(requested_mode == saved_mode);
    session_mode = USB_MODE_UART;
    assert(!tud_msc_test_unit_ready_cb(0));
    requested_mode = USB_MODE_MASS;
    usb_storage_init();
    assert(usb_storage_task());
    session_mode = USB_MODE_MASS;
    assert(tud_msc_test_unit_ready_cb(0));
    usb_storage_deinit();
    assert(requested_mode == saved_mode);
    puts("MSC ownership, sector offsets, bounds, errors and eject tests passed");
}
