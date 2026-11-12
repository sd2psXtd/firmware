#pragma once
#include <stdint.h>
#define SCSI_SENSE_NOT_READY 2
#define SCSI_SENSE_MEDIUM_ERROR 3
#define SCSI_SENSE_ILLEGAL_REQUEST 5
void tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq);
