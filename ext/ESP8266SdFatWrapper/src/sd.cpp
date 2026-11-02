#include "config.h"

#include "SdFat.h"
#include "SPI.h"

#include "hardware/gpio.h"

extern "C" {
#include "debug.h"
#include "sd.h"
}

#if LOG_LEVEL_SD == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_SD, level, fmt, ##x)
#endif

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
        gpio_set_drive_strength(SD_SCK, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_drive_strength(SD_MOSI, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_drive_strength(SD_CS, GPIO_DRIVE_STRENGTH_12MA);


        int ret = sd.begin(SdSpiConfig(SD_CS, DEDICATED_SPI, SD_BAUD, &SD_PERIPH));
/*
        cid_t cid;
        if (sd.card()->readCID(&cid)) {
            DPRINTF("SD Card CID:\n");
            DPRINTF(" Manufacturer ID: 0x%02X\n", cid.mid);
            DPRINTF(" OEM ID: %.2s\n", cid.oid);
            DPRINTF(" Product: %.5s\n", cid.pnm);
            DPRINTF(" Revision: %d.%d\n", cid.prv_n, cid.prv_m);
            DPRINTF(" Serial number: 0x%08X\n", cid.psn);
            DPRINTF(" Manufacturing date: %02d/%04d\n",
                cid.mdt_month, 2000 + ((cid.mdt_year_high << 4) | cid.mdt_year_low));
        } else {
            DPRINTF("failed to read CID\n");
        }*/
        if (ret != 1) {
            char text[128];
            cid_t cid;
            memset(&cid, 0, sizeof(cid));
            sd.card()->readCID(&cid);
            snprintf(text, sizeof(text),
                "MID: 0x%02X    OEM-ID: %.2s\nPID: %.5s    R: %d.%d\nSN: 0x%08X  Date: %02d/%04d",
                cid.mid,
                cid.oid,
                cid.pnm,
                cid.prv_n, cid.prv_m,
                cid.psn,
                cid.mdt_month, 2000 + ((cid.mdt_year_high << 4) | cid.mdt_year_low));

            if (sd.sdErrorCode()) {
                fatal(ERR_SDCARD, "failed to mount the card\nSdError: 0x%02X,0x%02X\ncheck the card\n%s", sd.sdErrorCode(), sd.sdErrorData(), text);
            } else if (sd.initErrorCode() == static_cast<uint8_t>(FsVolume::FsInitError::CorruptPartitionTable)) {
                fatal(ERR_SDCARD, "failed to mount the card\nPartition table is corrupt or invalid\n%s", text);
            } else {
                fatal(ERR_SDCARD, "failed to mount the card\nNo supported FAT/exFAT filesystem found\ncheck the card is formatted correctly\n%s", text);
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

    if (!sd_exists(path) && (oflag & O_CREAT) == 0) {
        return -1;
    }

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
    int retry = 5;
    uint64_t pos = files[fd].curPosition();
    size_t ret = files[fd].write(buf, count);
    while (ret != count && retry-- > 0) {
        /* a failed write leaves the cursor advanced by whatever it did manage to write, so rewind
           before trying again */
        if (files[fd].seekSet(pos)) ret = files[fd].write(buf, count);
    }

    return ret;
}

extern "C" int sd_write_isolated(int fd, void *buf) {
    CHECK_FD(fd);
    size_t count = 512;
    int retry = 5;
    sd.card()->setSingleSectorWriteCmd(true);
    size_t ret = files[fd].write(buf, count);
    while (ret != count && retry-- > 0) {
        ret = files[fd].write(buf, count);
    }
    sd.card()->setSingleSectorWriteCmd(false);

    return ret;
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
    if (sd_exists(path)) {
        /* return 0 if the directory already exists */
        return 0;
    } else {
        /* return 1 on error */
        return sd.mkdir(path) != true;
    }
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

extern "C" int sd_rename(const char* old_path, const char* new_path) {
    /* return 1 on error */
    return !sd.rename(old_path, new_path) ? -1 : 0;
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

extern "C" sd_cid_t sd_get_CID(void) {
    cid_t cid;
    sd_cid_t out_cid;

    if (sd.card()->readCID(&cid)) {
        out_cid.psn = cid.psn;
        out_cid.mid = cid.mid;
        memcpy(out_cid.oid, cid.oid, 2);
        memcpy(out_cid.pnm, cid.pnm, 5);
        out_cid.prv = (cid.prv_n << 4) | cid.prv_m;
        out_cid.mdt_month = cid.mdt_month;
        out_cid.mdt_year_high = cid.mdt_year_high;
        out_cid.mdt_year_low = cid.mdt_year_low;
        out_cid.always1 = cid.always1;
    } else {
        memset(&out_cid, 0, sizeof(sd_cid_t));
    }
    return out_cid;
}
