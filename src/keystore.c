#include "keystore.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "hardware/regs/addressmap.h"
#include "hardware/flash.h"
#include "pico/multicore.h"

#include "flashmap.h"
#include "sd.h"

uint8_t ps2_civ[8];
int ps2_magicgate;

void keystore_init(void) {
    keystore_read();
}

void keystore_read(void) {
    uint8_t buf[16];
    memcpy(buf, (uint8_t*)XIP_BASE + FLASH_OFF_CIV, sizeof(buf));
    for (int i = 0; i < 8; ++i) {
        uint8_t chk = ~buf[i + 8];
        if (buf[i] != chk) {
            printf("keystore - No CIV key deployed\n");
            return;
        }
    }

    printf("keystore - Found valid CIV : %02X %02X ... - activating magicgate\n", buf[0], buf[1]);
    memcpy(ps2_civ, buf, sizeof(ps2_civ));
    ps2_magicgate = 1;
}

char *keystore_error(int rc) {
    switch (rc) {
    case 0:
        return "No error";
    case KEYSTORE_DEPLOY_NOFILE:
        return "civ.bin not\nfound\n";
    case KEYSTORE_DEPLOY_OPEN:
        return "Cannot open\nciv.bin\n";
    case KEYSTORE_DEPLOY_READ:
        return "Cannot read\nciv.bin\n";
    default:
        return "Unknown error";
    }
}

int keystore_deploy(void) {
    uint8_t civbuf[8] = { 0 };
    uint8_t chkbuf[256] = { 0 };

    if (!sd_exists("civ.bin"))
        return KEYSTORE_DEPLOY_NOFILE;

    int fd = sd_open("civ.bin", O_RDONLY);
    if (fd < 0)
        return KEYSTORE_DEPLOY_OPEN;

    if (sd_read(fd, civbuf, sizeof(civbuf)) != sizeof(civbuf)) {
        sd_close(fd);
        return KEYSTORE_DEPLOY_READ;
    }
    sd_close(fd);

    memcpy(chkbuf, civbuf, sizeof(civbuf));
    for (int i = 0; i < 8; ++i)
        chkbuf[i + 8] = ~chkbuf[i];

    if (memcmp(chkbuf, (uint8_t*)XIP_BASE + FLASH_OFF_CIV, sizeof(chkbuf)) != 0) {
        multicore_lockout_start_blocking();
        flash_range_erase(FLASH_OFF_CIV, 4096);
        flash_range_program(FLASH_OFF_CIV, chkbuf, sizeof(chkbuf));
        multicore_lockout_end_blocking();
    } else {
        printf("keystore - skipping CIV flash because data is unchanged\n");
    }

    keystore_read();

    return 0;
}
