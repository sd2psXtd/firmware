#include "keystore.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "hardware/regs/addressmap.h"
#include "hardware/flash.h"
#include "pico/multicore.h"

#include "flashmap.h"
#include "pico/platform.h"
#include "sd.h"
#include "settings.h"

uint8_t ps2_civ[8];
const char civ_path[]           = "civ.bin";
const char civ_path_backup[]    = ".sd2psx/civ.bin";
int ps2_magicgate;


void __not_in_flash_func(keystore_backup)(void) {
    if (!sd_exists(civ_path_backup)) {
        int fd = sd_open(civ_path_backup, O_CREAT | O_WRONLY);
        sd_write(fd, ps2_civ, 8);
        sd_close(fd);
    }
}

void keystore_init(void) {
    keystore_read();
    if (ps2_magicgate == 0) {
        printf("Deploying keys...\n");
        keystore_deploy();
#if WITH_GUI==0
        if (!ps2_magicgate)
            fatal("Cannot find civ!\n");
#endif
    }
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

int __not_in_flash_func(keystore_deploy)(void) {
    uint8_t civbuf[8] = { 0 };
    uint8_t chkbuf[256] = { 0 };
    const char* path;

    sd_init();

    if (sd_exists(civ_path))
        path = civ_path;
    else if (sd_exists(civ_path_backup))
        path = civ_path_backup;
    else
        return KEYSTORE_DEPLOY_NOFILE;

    int fd = sd_open(path, O_RDONLY);
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
        if (multicore_lockout_victim_is_initialized(1))
            multicore_lockout_start_blocking();
        flash_range_erase(FLASH_OFF_CIV, 4096);
        flash_range_program(FLASH_OFF_CIV, chkbuf, sizeof(chkbuf));
        if (multicore_lockout_victim_is_initialized(1))
            multicore_lockout_end_blocking();
    } else {
        printf("keystore - skipping CIV flash because data is unchanged\n");
    }

    sd_remove(path);

    keystore_read();

    keystore_backup();

    return 0;
}

void __not_in_flash_func(keystore_reset)(void) {
    uint8_t chkbuf[256] = { 0 };
    printf("keystore - Resetting CIV\n");
    ps2_magicgate = 0;

    if (multicore_lockout_victim_is_initialized(1))
        multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_OFF_CIV, 4096);
    flash_range_program(FLASH_OFF_CIV, chkbuf, sizeof(chkbuf));
    restore_interrupts (ints);
    if (multicore_lockout_victim_is_initialized(1))
        multicore_lockout_end_blocking();
    if (sd_exists(civ_path_backup))
        sd_remove(civ_path_backup);
    keystore_read();
}
