#include <stdint.h>
#include "SdCard/SdSpiCard.h"
#include "stdbool.h"
#include "config.h"
#include "debug.h"

extern "C" {
    #include "sd.h"
}

static SharedSpiCard sd;

extern "C" bool sd_block_init() {
    printf("Initializing SD card\n");

    SD_PERIPH.setRX(SD_MISO);
    SD_PERIPH.setTX(SD_MOSI);
    SD_PERIPH.setSCK(SD_SCK);
    SD_PERIPH.setCS(SD_CS);
    bool ret = sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_BAUD, &SD_PERIPH));
    printf("SD card initialized: ret=%i\n", ret);
    if (!ret) {
        if (sd.errorCode()) {
            printf("failed to mount the card\nSdError: 0x%02X,0x%02X\ncheck the card", sd.errorCode(), sd.errorData());
        } else {
            printf("failed to mount the card\nUNKNOWN");
        }
    }
    uint32_t blockCount;
    uint16_t blockSize;
    sd_block_getSize(&blockCount, &blockSize);
    printf("SD card size: %u blocks, %u bytes per block\n", (unsigned int)blockCount, (unsigned int)blockSize);
    return true;
}

extern "C" int sd_block_getStatus(void) {
    return sd.errorCode();

}
extern "C" int sd_block_getSize(uint32_t* blockCount, uint16_t* blockSize) {
    *blockCount = sd.sectorCount();
    *blockSize = 512;
    return 0;

}

extern "C" int sd_block_readBlock(uint32_t block, uint8_t* dst) {
    return sd.readSector(block, dst) ? 0 : 1;

}

extern "C" int sd_block_writeBlock(uint32_t block, const uint8_t* src) {
    return sd.writeSector(block, src) ? 0 : 1;
}

