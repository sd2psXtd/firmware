#include "config.h"

#include "SdFat.h"
#include "SPI.h"

#include "hardware/gpio.h"

extern "C" {
#include "debug.h"
#include "sd.h"
}

#include <stdio.h>

#ifndef NUM_FILES
    #define NUM_FILES 16
#endif

static SdFat sd;
static File files[NUM_FILES + 1];
static bool initialized = false;

extern "C" void sd_init() {
    if (!initialized) {
        SD_PERIPH.setRX(SD_MISO);
        SD_PERIPH.setTX(SD_MOSI);
        SD_PERIPH.setSCK(SD_SCK);
        SD_PERIPH.setCS(SD_CS);

        int ret = sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_BAUD, &SD_PERIPH));
        if (ret != 1) {
            if (sd.sdErrorCode()) {
                fatal("failed to mount the card\nSdError: 0x%02X,0x%02X\ncheck the card", sd.sdErrorCode(), sd.sdErrorData());
            } else if (!sd.fatType()) {
                fatal("failed to mount the card\ncheck the card is formatted correctly");
            } else {
                fatal("failed to mount the card\nUNKNOWN");
            }
        }
        initialized = true;
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

extern "C" int sd_close(int fd) {
    CHECK_FD(fd);

    return files[fd].close() != true;
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

extern "C" int sd_seek(int fd, int32_t offset, int whence) {
    CHECK_FD(fd);

    if (whence == 0) {
        return files[fd].seekSet(offset) != true;
    } else if (whence == 1) {
        return files[fd].seekCur(offset) != true;
    } else if (whence == 2) {
        return files[fd].seekEnd(offset) != true;
    }

    return 1;
}

extern "C" uint32_t sd_tell(int fd) {
    CHECK_FD(fd);

    return (uint32_t)files[fd].curPosition();
}

extern "C" int sd_mkdir(const char *path) {
    /* return 1 on error */
    return sd.mkdir(path) != true;
}

extern "C" int sd_exists(const char *path) {
    return sd.exists(path);
}

extern "C" int sd_filesize(int fd) {
    CHECK_FD(fd);
    return files[fd].fileSize();
}

extern "C" int sd_rmdir(const char* path) {
    /* return 1 on error */
    return sd.rmdir(path) != true;
}

extern "C" int sd_remove(const char* path) {
    /* return 1 on error */
    return sd.remove(path) != true;
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

extern "C" void mapTime(const uint16_t date, const uint16_t time, uint8_t* const out_time) {
    uint16_t year;
    out_time[0] = 0; // Padding

    out_time[4] = (date & 31); // Day
    out_time[5] = (date >> 5) & 15; // Month

    year = (date >> 9) + 1980;
    out_time[6] = year & 0xff; // Year (low bits)
    out_time[7] = (year >> 8) & 0xff; // Year (high bits)

    out_time[3] = (time >> 11); // Hours
    out_time[2] = (time >> 5) & 63; // Minutes
    out_time[1] = (time << 1) & 31; // Seconds (multiplied by 2)
}

//Get stat and convert to format fileio expects
extern "C" int sd_get_stat(int fd, ps2_fileio_stat_t* const ps2_fileio_stat) {
    CHECK_FD(fd);

    uint16_t date, time;

    //FIO_S_IFREG
    if (files[fd].isFile())
        ps2_fileio_stat->mode = FIO_S_IFREG;
    //FIO_S_IFDIR
    else if (files[fd].isDir())
        ps2_fileio_stat->mode = FIO_S_IFDIR;

    //FIO_S_IROTH
    if (files[fd].isReadable())
        ps2_fileio_stat->mode |= FIO_S_IROTH;

    //FIO_S_IWOTH
    if (files[fd].isWritable())
        ps2_fileio_stat->mode |= FIO_S_IWOTH;

    //FIO_S_IXOTH - TODO

    ps2_fileio_stat->attr = 0x0; //TODO
    ps2_fileio_stat->size = (uint32_t)files[fd].fileSize();

    files[fd].getCreateDateTime(&date, &time);
    mapTime(date, time, ps2_fileio_stat->ctime);
    files[fd].getAccessDateTime(&date, &time);
    mapTime(date, time, ps2_fileio_stat->atime);
    files[fd].getModifyDateTime(&date, &time);
    mapTime(date, time, ps2_fileio_stat->mtime);

    ps2_fileio_stat->hisize = (files[fd].fileSize() >> 32);

    return 0;
}

extern "C" int sd_fd_is_open(int fd) {
    CHECK_FD(fd);
    return 0;
}

extern "C" uint64_t sd_filesize64(int fd) {
    CHECK_FD(fd);
    return files[fd].fileSize();
}

//curPosition returns a uint64_t when using exFAT
extern "C" uint64_t sd_tell64(int fd) {
    CHECK_FD(fd);

    return (uint64_t)files[fd].curPosition();
}

extern "C" int sd_seek64(int fd, int64_t offset, int whence) {
    if (whence == 0) {
        return files[fd].seekSet((uint64_t)offset) != true;
    } else if (whence == 1) {
        return files[fd].seekCur(offset) != true;
    } else if (whence == 2) {
        return files[fd].seekEnd(offset) != true;
    }
    return 1;
}