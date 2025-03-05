#include "tusb.h"
#include <stdint.h>
#include <string.h>
#include "sd.h"
#include "settings.h"


// Callback: Get the total number of blocks and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
	sd_block_getSize(block_count, block_size);
}

// Callback: Read data from storage
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
	uint32_t blockCount;
	uint16_t blockSize;
    sd_block_getSize(&blockCount, &blockSize);
    if (bufsize % blockSize != 0) return -1;
    if (lba >= blockCount) return -1;
    int32_t readblocks = bufsize / blockSize;
    for (int i = 0; i < readblocks; i++) {
        sd_block_readBlock(lba + i, buffer + (i * blockSize));
    }
    return bufsize;
}

// Callback: Write data to storage
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
	uint32_t blockCount;
	uint16_t blockSize;
	sd_block_getSize(&blockCount, &blockSize);
    if (bufsize % blockSize != 0) return -1;
    if (lba >= blockCount) return -1;
    int32_t writeBlocks = bufsize / blockSize;
    for (int i = 0; i < writeBlocks; i++) {
        sd_block_writeBlock(lba + i, buffer + (i * blockSize));
    }

    return bufsize;
}

// Callback: Handle SCSI INQUIRY command
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    strlcpy((char*)vendor_id, "sd2psXtd", 8);
    strlcpy((char*)product_id, "MSC Device   ", 16);
    strlcpy((char*)product_rev, "1.0 ", 4);
}

// Callback: Test if the device is ready
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    return (settings_get_mode() == MODE_USB);
}

// Callback: Start/Stop unit (e.g., handle ejection)
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    if ((!start) && (load_eject))
        settings_init();

    return true; // Nothing special needed
}

// Callback: Indicate whether write protection is enabled
bool tud_msc_is_writable_cb(uint8_t lun) {
    return true; // Storage is writable
}

// Callback: Handle SCSI mode sense command
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) {
    return -1; // No special handling
}
