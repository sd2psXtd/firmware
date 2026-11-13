#include "usb_storage.h"
#include "mca_device.h"
#include "settings.h"
#include "sd.h"
#include "debug.h"
#include "pico/time.h"
#if WITH_GUI
#include "gui.h"
#include "input.h"
#include "oled.h"
#endif
#include "tusb.h"
#include <string.h>

static bool eject_pending, finished;
static uint32_t sectors;

void usb_storage_init(void) {
    /* The previous USB session has stopped; allow the host to see disconnect. */
    sleep_ms(20);
    if (!sd_unmount())
        fatal(ERR_SDCARD, "USB storage: SD handover failed");
    sectors = sd_sector_count();
    if (!sectors)
        fatal(ERR_SDCARD, "USB storage: invalid SD capacity");
    eject_pending = finished = false;
#if WITH_GUI
    gui_usb_storage(true);
#endif
}

bool usb_storage_task(void) {
#if WITH_GUI
    input_task();
    gui_task();
    oled_task();
#endif
    return !finished;
}

void usb_storage_deinit(void) {
    /* USB is stopped by the common session loop before remounting. */
    sleep_ms(20);
    if (!sd_sync_device() || !sd_remount())
        fatal(ERR_SDCARD, "USB storage: SD remount failed");
    settings_set_usb_mode(settings_get_usb_mode(false));
#if WITH_GUI
    gui_usb_storage(false);
#endif
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t rev[4]) {
    (void)lun;
    memcpy(vendor, "sd2psXtd", 8);
    memcpy(product, "SD card         ", 16);
    memcpy(rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (usb_session_get_mode() == USB_MODE_MASS && !eject_pending)
        return true;
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size) {
    (void)lun;
    *count = sectors;
    *size = 512;
}

/* Honour TinyUSB byte offsets, including partial-sector writes. */
static int32_t transfer(uint8_t lun, uint32_t lba, uint32_t offset,
                        void *buffer, uint32_t size, bool write) {
    uint64_t pos = (uint64_t)lba * 512 + offset;
    if (!tud_msc_test_unit_ready_cb(lun))
        return -1;
    if (pos > (uint64_t)sectors * 512 || size > (uint64_t)sectors * 512 - pos) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0);
        return -1;
    }
    uint8_t sector[512];
    uint8_t *bytes = buffer;
    uint32_t remaining = size;
    while (remaining) {
        uint32_t block = pos / 512, off = pos % 512;
        uint32_t n = 512 - off;
        if (n > remaining) n = remaining;
        if (!write || off || n != 512) {
            if (!sd_read_sector(block, sector)) {
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0c : 0x11, 0);
                return -1;
            }
        }
        if (write) {
            memcpy(sector + off, bytes, n);
            if (!sd_write_sector(block, sector)) {
                tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
                return -1;
            }
        } else {
            memcpy(bytes, sector + off, n);
        }
        remaining -= n;
        bytes += n;
        pos += n;
    }
    return size;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
    return transfer(lun, lba, offset, buf, size, false);
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
    return transfer(lun, lba, offset, buf, size, true);
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power, bool start, bool load_eject) {
    if (power || start || !load_eject)
        return true;
    if (!sd_sync_device()) {
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
        return false;
    }
    eject_pending = true;
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t cmd[16], void *buf, uint16_t size) {
    (void)buf; (void)size;
    if (cmd[0] == 0x35) { /* SYNCHRONIZE CACHE (10) */
        if (sd_sync_device()) return 0;
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0);
    } else {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    }
    return -1;
}

/* Called by TinyUSB after the command status has actually reached the host. */
void tud_msc_scsi_complete_cb(uint8_t lun, const uint8_t cmd[16]) {
    (void)lun;
    if (cmd[0] == 0x1b && (cmd[4] & 0xf3) == 2 && eject_pending)
        finished = true;
}
