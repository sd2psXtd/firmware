#pragma once

#include <fcntl.h>

void sd_init(void);
int sd_open(const char *path, int oflag);
void sd_close(int fd);
void sd_flush(int fd);
int sd_read(int fd, void *buf, size_t count);
int sd_write(int fd, void *buf, size_t count);
int sd_seek(int fd, uint64_t pos);
int sd_filesize(int fd);
int sd_mkdir(const char *path);
int sd_exists(const char *path);

int sd_iterate_dir(int dir, int it);
size_t sd_get_name(int fd, char* name, size_t size);
