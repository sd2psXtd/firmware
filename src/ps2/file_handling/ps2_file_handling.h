#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SD2PSX_FILE_MODE_READ        0b001
#define SD2PSX_FILE_MODE_WRITE       0b010
#define SD2PSX_FILE_MODE_READ        0b001
#define SD2PSX_FILE_MODE_RW          0b011
#define SD2PSX_FILE_MODE_CREATE      0b100

#define CHUNK_SIZE 251

typedef struct ps2_file_handling_stat_t {
    uint32_t size;
    uint16_t mode;
    uint16_t attr;
    uint8_t  ctime[8];
    uint8_t  atime[8];
    uint8_t  mtime[8];
    bool    exists;
} ps2_file_handling_stat_t;

typedef struct ps2_file_handling_operation_t {
    size_t size_used;
    size_t size_remaining;
    size_t position;
    int flag;
    int handle;
    enum {
        OP_FILE,
        OP_DIR,
        OP_NONE
    } type;
    union {
        uint8_t buff[250];
        char    string[250];
    } content;
} ps2_file_handling_operation_t;

typedef struct ps2_file_handling_dirent_t {
    int handle;
    char name[253];
    bool isFile;
} ps2_file_handling_dirent_t;

void ps2_file_handling_init(void);
void ps2_file_handling_run(void);

bool ps2_file_handling_available(void);
ps2_file_handling_operation_t* ps2_file_handling_get_operation(bool isRead);
void ps2_file_handling_set_operation(const ps2_file_handling_operation_t* const operation);

int ps2_file_handling_open_file(uint8_t mode);
void ps2_file_handling_close_file(int fh);

void ps2_file_handling_seek(int fh, size_t pos);
size_t ps2_file_handling_tell(int fh);

void ps2_file_handling_requestTransaction(int fh, size_t size, bool isRead);

void ps2_file_handling_flush_buffer(void);
void ps2_file_handling_continue_read(void);
void ps2_file_handling_delete(void);
void ps2_file_handling_getDirEnt(ps2_file_handling_dirent_t* dirent);

void ps2_file_handling_stat(ps2_file_handling_stat_t* const stat);