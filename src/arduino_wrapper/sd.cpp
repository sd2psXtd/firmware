#include "config.h"

#include "SdFat.h"
#include "SPI.h"

#include "hardware/gpio.h"

extern "C" {
#include "debug.h"
#include "sd.h"
}

#include <stdio.h>

#define NUM_FILES 8

static SdFat sd;
static File files[NUM_FILES];

extern "C" void sd_init() {
    SPI1.setRX(SD_MISO);
    SPI1.setTX(SD_MOSI);
    SPI1.setSCK(SD_SCK);
    SPI1.setCS(SD_CS);

    int ret = sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_BAUD, &SPI1));
    if (ret != 1) {
        if (sd.sdErrorCode()) {
            fatal("failed to mount the card\nSdError: 0x%02X,0x%02X\ncheck the card", sd.sdErrorCode(), sd.sdErrorData());
        } else if (!sd.fatType()) {
            fatal("failed to mount the card\ncheck the card is formatted correctly");
        } else {
            fatal("failed to mount the card\nUNKNOWN");
        }
    }
}

void sdCsInit(SdCsPin_t pin) {
    gpio_init(pin);
    gpio_set_dir(pin, 1);
}

void sdCsWrite(SdCsPin_t pin, bool level) {
    gpio_put(pin, level);
}

extern "C" int sd_open(const char *path, int oflag) {
    size_t fd;

    for (fd = 0; fd < NUM_FILES; ++fd)
        if (!files[fd].isOpen())
            break;

    /* no fd available */
    if (fd >= NUM_FILES)
        return -1;

    files[fd].open(path, oflag);

    /* error during opening file */
    if (!files[fd].isOpen())
        return -1;

    return fd;
}

#define CHECK_FD(fd) if (fd >= NUM_FILES || !files[fd].isOpen()) return -1;
#define CHECK_FD_VOID(fd) if (fd >= NUM_FILES || !files[fd].isOpen()) return;

extern "C" void sd_close(int fd) {
    CHECK_FD_VOID(fd);

    files[fd].close();
}

extern "C" void sd_flush(int fd) {
    CHECK_FD_VOID(fd);

    files[fd].flush();
}

extern "C" int sd_read(int fd, void *buf, size_t count) {
    CHECK_FD(fd);

    return files[fd].read(buf, count);
}

extern "C" int sd_write(int fd, void *buf, size_t count) {
    CHECK_FD(fd);

    return files[fd].write(buf, count);
}

extern "C" int sd_seek(int fd, uint64_t pos) {
    CHECK_FD(fd);

    /* return 1 on error */
    return files[fd].seekSet(pos) != true;
}

extern "C" size_t sd_tell(int fd) {
    CHECK_FD(fd);

    return files[fd].curPosition();
}

extern "C" void sd_delete(const char* path) {
    sd.remove(path);
}

extern "C" int sd_mkdir(const char *path) {
    /* return 1 on error */
    return sd.mkdir(path) != true;
}

extern "C" int sd_exists(const char *path) {
    return sd.exists(path);
}

extern "C" int sd_filesize(int fd) {
    return files[fd].fileSize();;
}

extern "C" int sd_iterate_dir(int dir, int it) {
    if (it == -1) {
        for (it = 0; it < NUM_FILES; ++it)
            if (!files[it].isOpen())
                break;
    }
    if (!files[it].openNext(&files[dir], O_RDONLY)) {
        it = -1;
    }
    return it;
}

extern "C" size_t sd_get_name(int fd, char* name, size_t size) {
    return files[fd].getName(name, size);
}

extern "C" bool sd_is_dir(int fd) {
    return files[fd].isDirectory();
}

extern "C" int sd_getStat(int fd, sd_file_stat_t* const sd_stat) {
    files[fd].getAccessDateTime(&sd_stat->adate, &sd_stat->atime);
    files[fd].getCreateDateTime(&sd_stat->cdate, &sd_stat->ctime);
    files[fd].getModifyDateTime(&sd_stat->mdate, &sd_stat->mtime);
    sd_stat->writable = files[fd].isWritable();
    sd_stat->size = files[fd].fileSize();

    return -1;
}
