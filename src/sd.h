#pragma once

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct sd_file_stat_t {
    uint16_t adate;
    uint16_t atime;
    uint16_t cdate;
    uint16_t ctime;
    uint16_t mdate;
    uint16_t mtime;
    bool writable;
} sd_file_stat_t;

void sd_init(void);
int sd_open(const char *path, int oflag);
void sd_close(int fd);
void sd_flush(int fd);
int sd_read(int fd, void *buf, size_t count);
int sd_write(int fd, void *buf, size_t count);
int sd_seek(int fd, uint64_t pos);
size_t sd_tell(int fd);
int sd_getStat(int fd, sd_file_stat_t* const sd_stat);

int sd_filesize(int fd);
int sd_mkdir(const char *path);
int sd_exists(const char *path);
void sd_delete(const char* path);

int sd_iterate_dir(int dir, int it);
size_t sd_get_name(int fd, char* name, size_t size);
bool sd_is_dir(int fd);