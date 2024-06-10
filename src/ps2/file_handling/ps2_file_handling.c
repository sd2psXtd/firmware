#include "ps2_file_handling.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "sd.h"
#include <stdbool.h>
#include "debug.h"
#include <stddef.h>
#include <string.h>
#include <sys/_default_fcntl.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#define WAIT_FOR(X, Y) ({do\
                         sleep_us(1);\
                        while(X != Y);})

#define BUFF_LENGTH 256

#define FILE_MODE_EXECUTE 0x0001
#define FILE_MODE_WRITE   0x0002
#define FILE_MODE_READ    0x0004
#define FILE_MODE_DIR     0x1000
#define FILE_MODE_FILE    0x2000


enum {
    FILE_HANDLING_OPEN,
    FILE_HANDLING_CLOSE,
    FILE_HANDLING_READ,
    FILE_HANDLING_DATA_READY,
    FILE_HANDLING_WRITE,
    FILE_HANDLING_WRITE_DONE,
    FILE_HANDLING_SEEK,
    FILE_HANDLING_TELL,
    FILE_HANDLING_DELETE,
    FILE_HANDLING_GET_DIRENT,
    FILE_HANDLING_STAT,
    FILE_HANDLING_AVAILABLE
} fh_state;

static ps2_file_handling_stat_t* curr_stat_p;

static ps2_file_handling_operation_t curr_operation;

static ps2_file_handling_dirent_t* curr_dirent;

static void mapTime(const uint16_t date, const uint16_t time, uint8_t* const out_time) {
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

void ps2_file_handling_init(void) {
    fh_state = FILE_HANDLING_AVAILABLE;
    curr_dirent = NULL;
    
    memset(curr_operation.content.buff, 0x00, sizeof(curr_operation.content.buff));
    curr_operation.flag = O_RDONLY;
    curr_operation.handle = -1;
    curr_operation.size_remaining = 0;
    curr_operation.size_used = 0;
    curr_operation.type = OP_NONE;
    curr_operation.position = -1;
}

void ps2_file_handling_run(void) {
    switch(fh_state) {
        case FILE_HANDLING_OPEN:
            curr_operation.handle = sd_open(curr_operation.content.string, curr_operation.flag);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_CLOSE:
            sd_close(curr_operation.handle);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_SEEK:
            sd_seek(curr_operation.handle, curr_operation.position);
            curr_operation.position = sd_tell(curr_operation.handle);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_TELL:
            curr_operation.position = sd_tell(curr_operation.handle);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_READ:
            {
                //printf("Start read...");
                size_t chunk = curr_operation.size_remaining > CHUNK_SIZE ? CHUNK_SIZE : curr_operation.size_remaining;
                int rv = sd_read(curr_operation.handle, curr_operation.content.buff, chunk);
                //if (rv != (int32_t)chunk)
                //    printf("ERROR, read %i\n", rv);
                curr_operation.size_used = rv;
                curr_operation.size_remaining -= curr_operation.size_used;
                //printf("done! ");
                fh_state = FILE_HANDLING_DATA_READY;
                break;
            }
        case FILE_HANDLING_WRITE:
            sd_write(curr_operation.handle, curr_operation.content.buff, curr_operation.size_used);
            curr_operation.size_used = 0;
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_DELETE:
            sd_delete(curr_operation.content.string);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_GET_DIRENT:
            if (curr_operation.handle < 0)
                sd_open(curr_operation.content.string, O_RDONLY);
            curr_operation.position = sd_iterate_dir(curr_operation.handle, curr_operation.position);
            sd_get_name(curr_operation.position, curr_dirent->name, 255U);
            curr_dirent->isFile = !sd_is_dir(curr_operation.position);
            fh_state = FILE_HANDLING_AVAILABLE;
            break;
        case FILE_HANDLING_STAT:
            if (curr_stat_p != NULL) {
                sd_file_stat_t sdStat;
                if (sd_exists(curr_operation.content.string)) {
                    int fd = sd_open(curr_operation.content.string, O_RDONLY);
                    sd_getStat(fd, &sdStat);
                    mapTime(sdStat.adate, sdStat.atime, curr_stat_p->atime);
                    mapTime(sdStat.mdate, sdStat.mtime, curr_stat_p->mtime);
                    mapTime(sdStat.cdate, sdStat.ctime, curr_stat_p->ctime);
                    curr_stat_p->attr = 0777;
                    curr_stat_p->mode = FILE_MODE_EXECUTE | FILE_MODE_READ;
                    if (sd_is_dir(fd))
                        curr_stat_p->mode |= FILE_MODE_DIR;
                    else
                        curr_stat_p->mode |= FILE_MODE_FILE;
                    curr_stat_p->mode |= sdStat.writable ? FILE_MODE_WRITE : 0;
                    curr_stat_p->exists = true;
                } else {
                    curr_stat_p->exists = false;
                }
            }
            fh_state = FILE_HANDLING_AVAILABLE;
            default:
        break;
    }
}


bool ps2_file_handling_available(void) {
    return FILE_HANDLING_AVAILABLE == fh_state;
}

ps2_file_handling_operation_t* ps2_file_handling_get_operation(bool isRead) {
    
    WAIT_FOR(fh_state, (isRead ? FILE_HANDLING_DATA_READY : FILE_HANDLING_AVAILABLE));
    
//    printf("Reading %u/%u from handle %u...", curr_operation.size_used, curr_operation.size_remaining, curr_operation.handle);

    return &curr_operation;
}

void ps2_file_handling_set_operation(const ps2_file_handling_operation_t* const operation) {
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    memcpy(&curr_operation, operation, sizeof(*operation));

}

int ps2_file_handling_open_file(uint8_t mode) {
    switch (mode) {
        case SD2PSX_FILE_MODE_WRITE:
            curr_operation.flag = O_WRONLY;
            break;
        case SD2PSX_FILE_MODE_RW:
            curr_operation.flag = O_RDWR;
            break;
        case SD2PSX_FILE_MODE_READ:
        default:
            curr_operation.flag = O_RDONLY;
            break;
    }

    fh_state = FILE_HANDLING_OPEN;
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);

    return curr_operation.handle;
}

void ps2_file_handling_close_file(int fh) {
    curr_operation.handle = fh;
    fh_state = FILE_HANDLING_CLOSE;
}

void ps2_file_handling_seek(int fh, size_t pos) {
    curr_operation.position = pos;
    curr_operation.handle = fh;
    fh_state = FILE_HANDLING_SEEK;
}

size_t ps2_file_handling_tell(int fh) {
    curr_operation.handle = fh;
    fh_state = FILE_HANDLING_TELL;
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);

    return curr_operation.position;
}

void ps2_file_handling_requestTransaction(int fh, size_t size, bool isRead) {
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    curr_operation.handle = fh;
    curr_operation.size_remaining = size;
//    printf("-----------------------\n");
    fh_state = isRead ? FILE_HANDLING_READ : FILE_HANDLING_WRITE;
}


void ps2_file_handling_continue_read(void) {
//    printf("Completed %u!\n", curr_operation.size_used);
    curr_operation.size_used = 0;
    if (curr_operation.size_remaining > 0)
        fh_state = FILE_HANDLING_READ;
    else
        fh_state = FILE_HANDLING_AVAILABLE;
}

void ps2_file_handling_flush_buffer(void) {
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    fh_state = FILE_HANDLING_WRITE;
}

void ps2_file_handling_delete(void) {
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    fh_state = FILE_HANDLING_DELETE;
}

void ps2_file_handling_stat(ps2_file_handling_stat_t* const stat) {
    curr_stat_p = stat;
    fh_state = FILE_HANDLING_STAT;
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
}

void ps2_file_handling_getDirEnt(ps2_file_handling_dirent_t* dirent) {
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    curr_dirent = dirent;
    fh_state = FILE_HANDLING_GET_DIRENT;
    WAIT_FOR(fh_state, FILE_HANDLING_AVAILABLE);
    curr_dirent = NULL;
}
