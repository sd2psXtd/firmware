#pragma once

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/** Symbolic link */
#define FIO_S_IFLNK 0x4000
/** Regular file */
#define FIO_S_IFREG 0x2000
/** Directory */
#define FIO_S_IFDIR 0x1000
/** Others read permission */
#define FIO_S_IROTH 0x0004
/** Others write permission */
#define FIO_S_IWOTH 0x0002

typedef struct ps2_fileio_stat_t
{
    unsigned int mode;
    unsigned int attr;
    unsigned int size;
    unsigned char ctime[8];
    unsigned char atime[8];
    unsigned char mtime[8];
    unsigned int hisize;
} ps2_fileio_stat_t;

typedef struct sd_file_stat_t {
    size_t size;
    uint16_t adate;
    uint16_t atime;
    uint16_t cdate;
    uint16_t ctime;
    uint16_t mdate;
    uint16_t mtime;
    bool writable;
} sd_file_stat_t;

typedef struct sd_cid_t {
    /** Product Serial Number */
    uint32_t psn;
    /** Manufacturer ID */
    uint8_t mid;
    /** OEM/Application ID */
    uint8_t oid[2];
    /** Product Name */
    uint8_t pnm[5];
    /** Product Revision */
    uint8_t prv;
    /** Manufacturing Date Month */
    uint8_t mdt_month : 4;
    /** Manufacturing Date Year High Bits */
    uint8_t mdt_year_high : 4;
    /** Manufacturing Date Year Low Bits */
    uint8_t mdt_year_low : 8;
    /** not used always 1 */
    uint8_t always1 : 1;
    /** checksum */
} sd_cid_t;

void sd_init(void);
int sd_open(const char *path, int oflag);
int sd_close(int fd);
void sd_flush(int fd);
int sd_read(int fd, void *buf, size_t count);
int sd_write(int fd, void *buf, size_t count);
/* Checked MMCE storage path: one write, accepted prefix from position progress.
 * Nonzero return denotes failure; accepted bytes can still be nonzero. */
int sd_write_once(int fd, const void *buf, uint32_t count, uint32_t *written);
int sd_seek(int fd, int32_t offset, int whence);
uint32_t sd_tell(int fd);
int sd_getStat(int fd, sd_file_stat_t* const sd_stat);

int sd_filesize(int fd);
int sd_mkdir(const char *path);
int sd_exists(const char *path);

int sd_remove(const char* path);
int sd_rmdir(const char* path);
int sd_rename(const char* old_path, const char* new_path);
int sd_get_stat(int fd, ps2_fileio_stat_t* const ps2_fileio_stat);

int sd_iterate_dir(int dir, int it);
/* 1 = opened entry, 0 = EOF, -1 = I/O/invalid descriptor, -2 = no free descriptor. */
int sd_iterate_dir_checked(int dir, int *entry);
size_t sd_get_name(int fd, char* name, size_t size);
bool sd_is_dir(int fd);
int sd_fd_is_open(int fd);

uint64_t sd_filesize64(int fd);
int sd_seek64(int fd, int64_t offset, int whence);
uint64_t sd_tell64(int fd);

sd_cid_t sd_get_CID(void);
