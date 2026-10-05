#include "ps1_mmce_fs.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "pico.h"
#include "pico/critical_section.h"
#include "ps1_mmce_fs_seek.h"
#include "sd.h"

#define PS1_MMCE_FS_PHYSICAL_PATH_MAX 256U
#define PS1_MMCE_FS_SUPPORTED_FLAGS   0x3FU

#define PS1_MMCE_FS_OPEN_ACCESS_MASK 0x03U
#define PS1_MMCE_FS_OPEN_APPEND      0x04U
#define PS1_MMCE_FS_OPEN_CREATE      0x08U
#define PS1_MMCE_FS_OPEN_TRUNCATE    0x10U
#define PS1_MMCE_FS_OPEN_EXCLUSIVE   0x20U

typedef enum {
    FS_HANDLE_NONE,
    FS_HANDLE_FILE,
    FS_HANDLE_DIRECTORY,
} fs_handle_type_t;

typedef struct {
    bool used;
    volatile bool delivered;
    bool readable;
    bool writable;
    bool directory_eof;
    fs_handle_type_t type;
    int fd;
    char path[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
} fs_handle_t;

/* Core 0 owns storage execution; Core 1 prepares requests and consumes results. */
static volatile ps1_mmce_fs_op_data_t fs_op_data;
static volatile uint32_t fs_operation;
static volatile bool fs_abort_request;
static volatile bool fs_abort_in_progress;
/* An aborted staged read must not leave the delivered handle at an
   unpredictable offset after Core 0 has already prefetched SD data. */
static uint64_t fs_read_origin;
static uint8_t fs_read_origin_handle;
static bool fs_read_origin_valid;

critical_section_t ps1_mmce_fs_crit;

static fs_handle_t fs_handles[PS1_MMCE_FS_MAX_HANDLES];
static bool fs_root_ready;
static bool fs_initialized;

static fs_handle_t* fs_find_delivered_handle(uint8_t public_handle, fs_handle_type_t type) {
    fs_handle_t* handle = NULL;
    if (public_handle != 0U && public_handle <= PS1_MMCE_FS_MAX_HANDLES) {
        fs_handle_t* candidate = &fs_handles[public_handle - 1U];
        if (candidate->used && candidate->delivered && candidate->type == type) {
            handle = candidate;
        }
    }
    return handle;
}

static int fs_find_free_handle_slot(void) {
    int slot = -1;
    for (uint8_t i = 0U; i < PS1_MMCE_FS_MAX_HANDLES && slot < 0; ++i) {
        if (!fs_handles[i].used) {
            slot = i;
        }
    }
    return slot;
}

static void fs_invalidate_handle(fs_handle_t* handle) {
    memset(handle, 0, sizeof(*handle));
    handle->fd = -1;
}

static ps1_mmce_fs_result_t fs_close_handle(fs_handle_t* handle) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (handle->used) {
        if (handle->type == FS_HANDLE_FILE) {
            sd_flush(handle->fd);
        }
        int close_error = sd_close(handle->fd);
        /* Close relinquishes the descriptor even when syncing reports an error. */
        fs_invalidate_handle(handle);
        result = close_error ? PS1_MMCE_FS_RESULT_IO_ERROR : PS1_MMCE_FS_RESULT_OK;
    }
    return result;
}

static void fs_close_all_handles(void) {
    for (uint8_t i = 0U; i < PS1_MMCE_FS_MAX_HANDLES; ++i) {
        fs_close_handle(&fs_handles[i]);
    }
}

/* Closes handles that were published but never delivered to the client. They
   can only be orphans from a dead transfer, so they are reclaimed on the next
   open and on abort. */
static void fs_close_undelivered_handles(void) {
    for (uint8_t i = 0U; i < PS1_MMCE_FS_MAX_HANDLES; ++i) {
        fs_handle_t* handle = &fs_handles[i];
        if (handle->used && !handle->delivered) {
            fs_close_handle(handle);
        }
    }
}

static void fs_clear_buffer_states(void) {
    memset((void*)fs_op_data.buffer_state, PS1_MMCE_FS_BUFFER_NOT_READY, sizeof(fs_op_data.buffer_state));
}

static bool fs_build_path(const char* relative, char* physical, size_t physical_size) {
    int length = 0;

    if (relative[0] == '\0') {
        length = snprintf(physical, physical_size, "/");
    } else {
        length = snprintf(physical, physical_size, "/%s", relative);
    }

    return length >= 0 && (size_t)length < physical_size;
}

static bool fs_path_is_open(const char* physical) {
    bool is_open = false;
    for (uint8_t i = 0U; i < PS1_MMCE_FS_MAX_HANDLES && !is_open; ++i) {
        is_open = fs_handles[i].used && strcmp(fs_handles[i].path, physical) == 0;
    }
    return is_open;
}

static bool fs_open_flags_valid(uint8_t flags) {
    uint8_t access = flags & PS1_MMCE_FS_OPEN_ACCESS_MASK;
    bool writable = access == 1U || access == 2U;
    bool valid = (flags & ~PS1_MMCE_FS_SUPPORTED_FLAGS) == 0U && access != 3U;
    if ((flags & (PS1_MMCE_FS_OPEN_APPEND | PS1_MMCE_FS_OPEN_CREATE | PS1_MMCE_FS_OPEN_TRUNCATE)) != 0U && !writable) {
        valid = false;
    }
    if ((flags & PS1_MMCE_FS_OPEN_EXCLUSIVE) != 0U && (flags & PS1_MMCE_FS_OPEN_CREATE) == 0U) {
        valid = false;
    }
    return valid;
}

static int fs_native_open_flags(uint8_t flags) {
    int native_flags = O_RDWR;

    uint8_t access = flags & PS1_MMCE_FS_OPEN_ACCESS_MASK;
    if (access == 0U) {
        native_flags = O_RDONLY;
    } else if (access == 1U) {
        native_flags = O_WRONLY;
    }
    if ((flags & PS1_MMCE_FS_OPEN_APPEND) != 0U) {
        native_flags |= O_APPEND;
    }
    if ((flags & PS1_MMCE_FS_OPEN_CREATE) != 0U) {
        native_flags |= O_CREAT;
    }
    if ((flags & PS1_MMCE_FS_OPEN_TRUNCATE) != 0U) {
        native_flags |= O_TRUNC;
    }
    if ((flags & PS1_MMCE_FS_OPEN_EXCLUSIVE) != 0U) {
        native_flags |= O_EXCL;
    }
    return native_flags;
}

static void fs_publish_handle(int slot, int fd, fs_handle_type_t type, const char* path, uint8_t flags) {
    fs_handle_t* handle = &fs_handles[slot];
    uint8_t access = flags & PS1_MMCE_FS_OPEN_ACCESS_MASK;

    memset(handle, 0, sizeof(*handle));
    handle->used = true;
    handle->delivered = false;
    handle->readable = type == FS_HANDLE_DIRECTORY || access == 0U || access == 2U;
    handle->writable = type == FS_HANDLE_FILE && (access == 1U || access == 2U);
    handle->type = type;
    handle->fd = fd;
    snprintf(handle->path, sizeof(handle->path), "%s", path);

    fs_op_data.handle = (uint8_t)(slot + 1U);
    fs_op_data.result = PS1_MMCE_FS_RESULT_OK;
}

static ps1_mmce_fs_result_t fs_validate_open_file_path(char* physical, size_t capacity) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!fs_root_ready) {
        result = PS1_MMCE_FS_RESULT_IO_ERROR;
    } else if (!fs_open_flags_valid(fs_op_data.flags)) {
        result = PS1_MMCE_FS_RESULT_INVALID;
    } else if (!fs_build_path((const char*)fs_op_data.path[0], physical, capacity)) {
        result = PS1_MMCE_FS_RESULT_NAME_TOO_LONG;
    }
    return result;
}

static ps1_mmce_fs_result_t fs_validate_open_file_existence(const char* physical) {
    bool exists = sd_exists(physical) != 0;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!exists && (fs_op_data.flags & PS1_MMCE_FS_OPEN_CREATE) == 0U) {
        result = PS1_MMCE_FS_RESULT_NOT_FOUND;
    } else if (exists && (fs_op_data.flags & PS1_MMCE_FS_OPEN_EXCLUSIVE) != 0U) {
        result = PS1_MMCE_FS_RESULT_EXISTS;
    }
    return result;
}

/* The caller owns descriptor cleanup, including a successful open of the wrong type. */
static ps1_mmce_fs_result_t fs_open_descriptor(const char* physical, fs_handle_type_t type, uint8_t flags, int* descriptor) {
    int native_flags = type == FS_HANDLE_FILE ? fs_native_open_flags(flags) : O_RDONLY;
    *descriptor = sd_open(physical, native_flags);
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_IO_ERROR;
    if (*descriptor >= 0) {
        bool is_directory = sd_is_dir(*descriptor);
        result = is_directory == (type == FS_HANDLE_DIRECTORY) ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_INVALID;
    }
    return result;
}

static ps1_mmce_fs_result_t fs_cancel_undelivered_open(int slot) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (__atomic_load_n(&fs_abort_request, __ATOMIC_ACQUIRE)) {
        fs_close_handle(&fs_handles[slot]);
        fs_op_data.handle = 0xFFU;
        result = PS1_MMCE_FS_RESULT_INTERNAL;
    }
    return result;
}

/* A temporary descriptor is closed here unless publication transfers ownership. */
static ps1_mmce_fs_result_t fs_open_handle(const char* physical, fs_handle_type_t type, uint8_t flags) {
    int slot = fs_find_free_handle_slot();
    int descriptor = -1;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_NO_HANDLES;
    if (slot >= 0) {
        result = fs_open_descriptor(physical, type, flags, &descriptor);
    }
    if (result == PS1_MMCE_FS_RESULT_OK) {
        fs_publish_handle(slot, descriptor, type, physical, flags);
        descriptor = -1;
        result = fs_cancel_undelivered_open(slot);
    }
    if (descriptor >= 0) {
        sd_close(descriptor);
    }
    return result;
}

static void fs_open_file(void) {
    char physical[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    fs_op_data.handle = 0xFFU;
    ps1_mmce_fs_result_t result = fs_validate_open_file_path(physical, sizeof(physical));
    if (result == PS1_MMCE_FS_RESULT_OK) {
        fs_close_undelivered_handles();
        result = fs_validate_open_file_existence(physical);
    }
    if (result == PS1_MMCE_FS_RESULT_OK) {
        result = fs_open_handle(physical, FS_HANDLE_FILE, fs_op_data.flags);
    }
    fs_op_data.result = result;
}

static void fs_close_delivered_handle(fs_handle_type_t type) {
    fs_handle_t* handle = fs_find_delivered_handle(fs_op_data.handle, type);
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_BAD_HANDLE;
    if (handle != NULL) {
        result = fs_close_handle(handle);
    }
    fs_op_data.result = result;
}

static void fs_close_file(void) {
    fs_close_delivered_handle(FS_HANDLE_FILE);
}

static ps1_mmce_fs_result_t fs_validate_file_transfer(fs_handle_t* handle, bool writing, uint32_t maximum) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (handle == NULL) {
        result = PS1_MMCE_FS_RESULT_BAD_HANDLE;
    } else if (writing ? !handle->writable : !handle->readable) {
        result = PS1_MMCE_FS_RESULT_ACCESS;
    } else if (fs_op_data.length > maximum) {
        result = PS1_MMCE_FS_RESULT_RANGE;
    }
    return result;
}

static void fs_record_read_origin(fs_handle_t* handle) {
    if (!__atomic_load_n(&fs_read_origin_valid, __ATOMIC_ACQUIRE)) {
        fs_read_origin = sd_tell64(handle->fd);
        fs_read_origin_handle = fs_op_data.handle;
        __atomic_store_n(&fs_read_origin_valid, true, __ATOMIC_RELEASE);
    }
}

static void fs_publish_read_buffer(uint8_t head, uint16_t bytes_read, bool full_block) {
    critical_section_enter_blocking(&ps1_mmce_fs_crit);
    fs_op_data.buffer_length[head] = bytes_read;
    fs_op_data.buffer_state[head] = full_block ? PS1_MMCE_FS_BUFFER_READY : PS1_MMCE_FS_BUFFER_INVALID;
    critical_section_exit(&ps1_mmce_fs_crit);
}

static bool fs_fill_read_buffer(fs_handle_t* handle, uint8_t head) {
    uint32_t requested_bytes = fs_op_data.length - fs_op_data.bytes_read;
    if (requested_bytes > PS1_MMCE_FS_BLOCK_SIZE) {
        requested_bytes = PS1_MMCE_FS_BLOCK_SIZE;
    }
    int read_count = sd_read(handle->fd, (void*)fs_op_data.buffer[head], requested_bytes);
    bool complete = read_count < 0;
    if (read_count < 0) {
        fs_op_data.result = PS1_MMCE_FS_RESULT_IO_ERROR;
        fs_publish_read_buffer(head, 0U, false);
    } else {
        bool full_block = read_count == (int)requested_bytes;
        fs_op_data.bytes_read += (uint32_t)read_count;
        if (!full_block) {
            fs_op_data.result = PS1_MMCE_FS_RESULT_EOF;
        }
        fs_publish_read_buffer(head, (uint16_t)read_count, full_block);
        fs_op_data.head_idx = (uint8_t)((head + 1U) % PS1_MMCE_FS_BUFFER_COUNT);
        complete = !full_block || fs_op_data.bytes_read == fs_op_data.length;
    }
    return complete;
}

/* Produces one block without waiting for Core 1. */
static bool fs_read_next_block(void) {
    fs_handle_t* handle = fs_find_delivered_handle(fs_op_data.handle, FS_HANDLE_FILE);
    ps1_mmce_fs_result_t result = fs_validate_file_transfer(handle, false, PS1_MMCE_FS_MAX_READ_TRANSFER);
    bool complete = result != PS1_MMCE_FS_RESULT_OK;
    if (complete) {
        fs_op_data.result = result;
    } else if (fs_op_data.bytes_read >= fs_op_data.length) {
        fs_op_data.result = PS1_MMCE_FS_RESULT_OK;
        complete = true;
    } else {
        fs_record_read_origin(handle);
        uint8_t head = fs_op_data.head_idx;
        if (__atomic_load_n(&fs_op_data.buffer_state[head], __ATOMIC_ACQUIRE) == PS1_MMCE_FS_BUFFER_NOT_READY) {
            complete = fs_fill_read_buffer(handle, head);
        }
    }
    return complete;
}

/* Write: buffer 0 holds the block from Core 1. A zero block length only
   validates the handle and access rights (first poll of a write stream). */
static void fs_write_file(void) {
    fs_handle_t* handle = fs_find_delivered_handle(fs_op_data.handle, FS_HANDLE_FILE);
    uint16_t block_length = fs_op_data.buffer_length[0];
    fs_op_data.bytes_written = 0U;
    ps1_mmce_fs_result_t result = fs_validate_file_transfer(handle, true, PS1_MMCE_FS_MAX_TRANSFER);
    if (result == PS1_MMCE_FS_RESULT_OK && block_length > PS1_MMCE_FS_BLOCK_SIZE) {
        result = PS1_MMCE_FS_RESULT_RANGE;
    }
    if (result == PS1_MMCE_FS_RESULT_OK && block_length != 0U) {
        uint32_t written_bytes = 0U;
        int write_error = sd_write_once(handle->fd, (const void*)fs_op_data.buffer[0], block_length, &written_bytes);
        fs_op_data.bytes_written = written_bytes;
        result = !write_error && written_bytes == block_length ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_IO_ERROR;
    }
    fs_op_data.result = result;
}

static ps1_mmce_fs_result_t fs_get_seek_base(fs_handle_t* handle, uint64_t* base) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    switch (fs_op_data.whence) {
        case SEEK_SET: *base = 0U; break;
        case SEEK_CUR: *base = sd_tell64(handle->fd); break;
        case SEEK_END: *base = sd_filesize64(handle->fd); break;
        default: result = PS1_MMCE_FS_RESULT_INVALID; break;
    }
    return result;
}

static ps1_mmce_fs_result_t fs_resolve_seek_target(fs_handle_t* handle, uint64_t* target) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_BAD_HANDLE;
    if (handle != NULL) {
        uint64_t base = 0U;
        result = fs_get_seek_base(handle, &base);
        if (result == PS1_MMCE_FS_RESULT_OK
            && !ps1_mmce_fs_seek_target(base, fs_op_data.offset, target)) {
            result = PS1_MMCE_FS_RESULT_RANGE;
        }
    }
    return result;
}

/* Publish the position only after storage confirms the seek. */
static ps1_mmce_fs_result_t fs_seek_to_target(fs_handle_t* handle, uint64_t target) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_IO_ERROR;
    if (sd_seek64(handle->fd, (int64_t)target, SEEK_SET) == 0) {
        fs_op_data.position = target;
        result = PS1_MMCE_FS_RESULT_OK;
    }
    return result;
}

static void fs_seek_file(void) {
    fs_handle_t* handle = fs_find_delivered_handle(fs_op_data.handle, FS_HANDLE_FILE);
    uint64_t target = 0U;
    fs_op_data.position = 0U;

    ps1_mmce_fs_result_t result = fs_resolve_seek_target(handle, &target);
    if (result == PS1_MMCE_FS_RESULT_OK) {
        result = fs_seek_to_target(handle, target);
    }
    fs_op_data.result = result;
}

static ps1_mmce_fs_result_t fs_validate_existing_path(char* physical, size_t capacity, bool reject_open) {
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!fs_build_path((const char*)fs_op_data.path[0], physical, capacity)) {
        result = PS1_MMCE_FS_RESULT_NAME_TOO_LONG;
    } else if (!sd_exists(physical)) {
        result = PS1_MMCE_FS_RESULT_NOT_FOUND;
    } else if (reject_open && fs_path_is_open(physical)) {
        result = PS1_MMCE_FS_RESULT_ACCESS;
    }
    return result;
}

/* Releases the temporary descriptor before the caller mutates the path. */
static ps1_mmce_fs_result_t fs_check_path_type(const char* physical, bool expect_directory) {
    int descriptor = sd_open(physical, O_RDONLY);
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_IO_ERROR;
    if (descriptor >= 0) {
        result = sd_is_dir(descriptor) == expect_directory ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_INVALID;
        sd_close(descriptor);
    }
    return result;
}

static void fs_remove_file(void) {
    char physical[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    ps1_mmce_fs_result_t result = fs_validate_existing_path(physical, sizeof(physical), true);
    if (result == PS1_MMCE_FS_RESULT_OK) {
        result = fs_check_path_type(physical, false);
    }
    if (result == PS1_MMCE_FS_RESULT_OK) {
        result = sd_remove(physical) == 0 ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_IO_ERROR;
    }
    fs_op_data.result = result;
}

static void fs_create_directory(void) {
    char physical[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!fs_build_path((const char*)fs_op_data.path[0], physical, sizeof(physical))) {
        result = PS1_MMCE_FS_RESULT_NAME_TOO_LONG;
    } else if (sd_exists(physical)) {
        result = PS1_MMCE_FS_RESULT_EXISTS;
    } else {
        result = sd_mkdir(physical) == 0 ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_IO_ERROR;
    }
    fs_op_data.result = result;
}

static ps1_mmce_fs_result_t fs_directory_iteration_error(int iterate_result) {
    ps1_mmce_fs_result_t result = iterate_result == -2 ? PS1_MMCE_FS_RESULT_NO_HANDLES : PS1_MMCE_FS_RESULT_IO_ERROR;
    return result;
}

static bool fs_is_dot_entry(const char* name) {
    bool is_dot = strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
    return is_dot;
}

static ps1_mmce_fs_result_t fs_check_directory_empty(int directory_fd) {
    bool complete = false;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    while (!complete) {
        int entry_fd = -1;
        int iterate_result = sd_iterate_dir_checked(directory_fd, &entry_fd);
        complete = iterate_result != 1;
        if (iterate_result == 1) {
            char entry_name[4] = {0};
            sd_get_name(entry_fd, entry_name, sizeof(entry_name));
            bool is_dot = fs_is_dot_entry(entry_name);
            sd_close(entry_fd);
            if (!is_dot) {
                result = PS1_MMCE_FS_RESULT_NOT_EMPTY;
                complete = true;
            }
        } else if (iterate_result < 0) {
            result = fs_directory_iteration_error(iterate_result);
        }
    }
    return result;
}

static void fs_remove_directory(void) {
    char physical[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    int directory_fd = -1;
    ps1_mmce_fs_result_t result = fs_validate_existing_path(physical, sizeof(physical), true);
    if (result == PS1_MMCE_FS_RESULT_OK) {
        directory_fd = sd_open(physical, O_RDONLY);
        if (directory_fd < 0) {
            result = PS1_MMCE_FS_RESULT_IO_ERROR;
        } else if (!sd_is_dir(directory_fd)) {
            result = PS1_MMCE_FS_RESULT_INVALID;
        } else {
            result = fs_check_directory_empty(directory_fd);
        }
    }
    if (directory_fd >= 0) {
        sd_close(directory_fd);
    }
    if (result == PS1_MMCE_FS_RESULT_OK) {
        result = sd_rmdir(physical) == 0 ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_IO_ERROR;
    }
    fs_op_data.result = result;
}

static void fs_rename(void) {
    char source[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    char destination[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!fs_build_path((const char*)fs_op_data.path[0], source, sizeof(source)) ||
        !fs_build_path((const char*)fs_op_data.path[1], destination, sizeof(destination))) {
        result = PS1_MMCE_FS_RESULT_NAME_TOO_LONG;
    } else if (!sd_exists(source)) {
        result = PS1_MMCE_FS_RESULT_NOT_FOUND;
    } else if (sd_exists(destination)) {
        result = PS1_MMCE_FS_RESULT_EXISTS;
    } else if (fs_path_is_open(source) || fs_path_is_open(destination)) {
        result = PS1_MMCE_FS_RESULT_ACCESS;
    } else {
        result = sd_rename(source, destination) == 0 ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_IO_ERROR;
    }
    fs_op_data.result = result;
}

static void fs_open_directory(void) {
    char physical[PS1_MMCE_FS_PHYSICAL_PATH_MAX];
    fs_op_data.handle = 0xFFU;
    ps1_mmce_fs_result_t result = fs_validate_existing_path(physical, sizeof(physical), false);
    if (result == PS1_MMCE_FS_RESULT_OK) {
        fs_close_undelivered_handles();
        result = fs_open_handle(physical, FS_HANDLE_DIRECTORY, 0U);
    }
    fs_op_data.result = result;
}

static ps1_mmce_fs_result_t fs_read_directory_entry(int entry_fd, bool* is_dot) {
    memset((void*)fs_op_data.name, 0, sizeof(fs_op_data.name));
    size_t name_length = sd_get_name(entry_fd, (void*)fs_op_data.name, sizeof(fs_op_data.name));
    *is_dot = fs_is_dot_entry((const char*)fs_op_data.name);
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (!*is_dot) {
        if (name_length == 0U || name_length + 1U > PS1_MMCE_FS_MAX_PATH) {
            result = name_length ? PS1_MMCE_FS_RESULT_NAME_TOO_LONG : PS1_MMCE_FS_RESULT_IO_ERROR;
        } else {
            fs_op_data.entry_type = sd_is_dir(entry_fd) ? 0x01U : 0x00U;
            fs_op_data.entry_size = fs_op_data.entry_type == 0x01U ? 0U : sd_filesize64(entry_fd);
            fs_op_data.name_length = (uint16_t)(name_length + 1U);
        }
    }
    return result;
}

static ps1_mmce_fs_result_t fs_find_next_directory_entry(fs_handle_t* handle) {
    bool complete = false;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    while (!complete) {
        int entry_fd = -1;
        int iterate_result = sd_iterate_dir_checked(handle->fd, &entry_fd);
        if (iterate_result < 0) {
            result = fs_directory_iteration_error(iterate_result);
            complete = true;
        } else if (iterate_result == 0) {
            handle->directory_eof = true;
            result = PS1_MMCE_FS_RESULT_EOF;
            complete = true;
        } else {
            bool is_dot = false;
            result = fs_read_directory_entry(entry_fd, &is_dot);
            sd_close(entry_fd);
            complete = !is_dot;
        }
    }
    return result;
}

static void fs_read_directory(void) {
    fs_handle_t* handle = fs_find_delivered_handle(fs_op_data.handle, FS_HANDLE_DIRECTORY);
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_BAD_HANDLE;
    fs_op_data.name_length = 0U;
    if (handle != NULL) {
        result = handle->directory_eof ? PS1_MMCE_FS_RESULT_EOF : fs_find_next_directory_entry(handle);
    }
    fs_op_data.result = result;
}

static void fs_close_directory(void) {
    fs_close_delivered_handle(FS_HANDLE_DIRECTORY);
}

/* Explicit abort/lifecycle boundaries reclaim orphan handles and rewind reads.
   Ordinary /CS rises between staged transfers do not request cancellation. */
static void fs_process_abort(void) {
    if (__atomic_load_n(&fs_read_origin_valid, __ATOMIC_ACQUIRE)) {
        fs_handle_t* handle = fs_find_delivered_handle(fs_read_origin_handle, FS_HANDLE_FILE);
        if (handle != NULL) {
            sd_seek64(handle->fd, (int64_t)fs_read_origin, SEEK_SET);
        }
        __atomic_store_n(&fs_read_origin_valid, false, __ATOMIC_RELEASE);
    }
    fs_close_undelivered_handles();

    if (fs_operation != PS1_MMCE_FS_OP_NONE) {
        fs_clear_buffer_states();
        fs_op_data.result = PS1_MMCE_FS_RESULT_INTERNAL;
        fs_operation = PS1_MMCE_FS_OP_NONE;
    }
    __atomic_store_n(&fs_abort_in_progress, false, __ATOMIC_RELEASE);
}

/* Fill free ring entries during this task slice without waiting for Core 1. */
static bool fs_fill_read_ring(void) {
    bool complete = false;
    for (unsigned blocks_filled = 0U; blocks_filled < PS1_MMCE_FS_BUFFER_COUNT; ++blocks_filled) {
        if (__atomic_load_n(&fs_abort_request, __ATOMIC_ACQUIRE)) {
            break;
        }
        complete = fs_read_next_block();
        if (complete) {
            break;
        }
        if (__atomic_load_n(&fs_op_data.buffer_state[fs_op_data.head_idx], __ATOMIC_ACQUIRE) != PS1_MMCE_FS_BUFFER_NOT_READY) {
            break;
        }
    }
    return complete;
}

static bool fs_execute_operation(uint32_t operation) {
    bool complete = true;
    switch (operation) {
        case PS1_MMCE_FS_OP_OPEN_FILE: fs_open_file(); break;
        case PS1_MMCE_FS_OP_CLOSE_FILE: fs_close_file(); break;
        case PS1_MMCE_FS_OP_READ: complete = fs_fill_read_ring(); break;
        case PS1_MMCE_FS_OP_WRITE: fs_write_file(); break;
        case PS1_MMCE_FS_OP_SEEK_FILE: fs_seek_file(); break;
        case PS1_MMCE_FS_OP_REMOVE_FILE: fs_remove_file(); break;
        case PS1_MMCE_FS_OP_CREATE_DIR: fs_create_directory(); break;
        case PS1_MMCE_FS_OP_REMOVE_DIR: fs_remove_directory(); break;
        case PS1_MMCE_FS_OP_RENAME: fs_rename(); break;
        case PS1_MMCE_FS_OP_OPEN_DIR: fs_open_directory(); break;
        case PS1_MMCE_FS_OP_READ_DIR: fs_read_directory(); break;
        case PS1_MMCE_FS_OP_CLOSE_DIR: fs_close_directory(); break;
        default: complete = false; break;
    }
    return complete;
}

void ps1_mmce_fs_run(void) {
    bool aborted = __atomic_exchange_n(&fs_abort_request, false, __ATOMIC_ACQ_REL);
    if (aborted) {
        fs_process_abort();
    } else if (fs_execute_operation(fs_operation)) {
        fs_operation = PS1_MMCE_FS_OP_NONE;
    }
}

bool ps1_mmce_fs_idle(void) {
    return fs_operation == PS1_MMCE_FS_OP_NONE;
}

static void fs_reset_operation_state(void) {
    __atomic_store_n(&fs_abort_request, false, __ATOMIC_RELEASE);
    __atomic_store_n(&fs_abort_in_progress, false, __ATOMIC_RELEASE);
    __atomic_store_n(&fs_read_origin_valid, false, __ATOMIC_RELEASE);
    fs_operation = PS1_MMCE_FS_OP_NONE;
}

void ps1_mmce_fs_init(void) {
    if (fs_initialized) {
        fs_close_all_handles();
    } else if (!ps1_mmce_fs_crit.spin_lock) {
        critical_section_init(&ps1_mmce_fs_crit);
    }

    memset((void*)&fs_op_data, 0, sizeof(fs_op_data));
    for (uint8_t i = 0U; i < PS1_MMCE_FS_MAX_HANDLES; ++i) {
        fs_invalidate_handle(&fs_handles[i]);
    }

    fs_reset_operation_state();

    fs_root_ready = sd_exists("/") != 0;
    fs_initialized = true;
}

void ps1_mmce_fs_reset(void) {
    fs_close_all_handles();
    memset((void*)&fs_op_data, 0, sizeof(fs_op_data));
    fs_reset_operation_state();
}

// Core 1
bool ps1_mmce_fs_is_ready(void) {
    return !__atomic_load_n(&fs_abort_in_progress, __ATOMIC_ACQUIRE) && fs_operation == PS1_MMCE_FS_OP_NONE;
}

void ps1_mmce_fs_signal_operation(int op) {
    __atomic_store_n(&fs_read_origin_valid, false, __ATOMIC_RELEASE);
    fs_operation = (uint32_t)op;
}

void __time_critical_func(ps1_mmce_fs_abort)(void) {
    __atomic_store_n(&fs_abort_in_progress, true, __ATOMIC_RELEASE);
    __atomic_store_n(&fs_abort_request, true, __ATOMIC_RELEASE);
}

void __time_critical_func(ps1_mmce_fs_finish_read)(void) {
    __atomic_store_n(&fs_read_origin_valid, false, __ATOMIC_RELEASE);
}

void ps1_mmce_fs_mark_handle_delivered(uint8_t public_handle) {
    if (public_handle != 0U && public_handle <= PS1_MMCE_FS_MAX_HANDLES) {
        fs_handle_t* handle = &fs_handles[public_handle - 1U];
        if (handle->used) {
            __atomic_store_n(&handle->delivered, true, __ATOMIC_RELEASE);
        }
    }
}

ps1_mmce_fs_op_data_t* ps1_mmce_fs_get_op_data(void) {
    return (ps1_mmce_fs_op_data_t*)&fs_op_data;
}
