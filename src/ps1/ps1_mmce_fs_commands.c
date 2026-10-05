#include "ps1_mmce_fs_commands.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico.h"
#include "pico/critical_section.h"
#include "ps1_memory_card.h"
#include "ps1_mmce_fs.h"

/* These transport macros intentionally leave the immediate caller when a
   transfer is interrupted. Helpers expose completion when callers continue. */
#define receiveOrNextCmd(cmd)                                                 \
    do {                                                                      \
        if (recv_mc(cmd) == RECEIVE_RESET || !ps1_memory_card_sio_active()) { \
            return;                                                           \
        }                                                                     \
    } while (0)

#define respondOrNextCmd(response)          \
    do {                                    \
        if (ps1_memory_card_sio_active()) { \
            ps1_mc_respond(response);       \
        } else {                            \
            return;                         \
        }                                   \
    } while (0)

#define MMCE_FS_STREAM_BLOCK_READY 0x01U
#define MMCE_FS_STREAM_COMPLETE    0x02U
#define MMCE_FS_STATUS_BUSY        0xF0U
#define MMCE_FS_READ_LOOKAHEAD_US   1000U
#define MMCE_FS_CAPABILITIES_SIZE   16U
#define MMCE_FS_CAPABILITY_SENTINEL 0xFFU

typedef struct {
    char magic[4];
    uint16_t protocol_version;
    uint8_t read_buffer_count;
    uint8_t read_block_shift;
    uint16_t max_read_blocks;
    uint16_t max_write_bytes;
    uint16_t max_path_bytes;
    uint8_t max_handles;
    uint8_t supported_open_flags;
} fs_capabilities_t;

static const fs_capabilities_t fs_capabilities = {
    .magic = {'M', 'F', 'S', '1'},
    .protocol_version = PS1_MMCE_FS_PROTOCOL_VERSION,
    .read_buffer_count = PS1_MMCE_FS_BUFFER_COUNT,
    .read_block_shift = 8U,
    .max_read_blocks = PS1_MMCE_FS_MAX_READ_BLOCKS,
    .max_write_bytes = PS1_MMCE_FS_MAX_TRANSFER,
    .max_path_bytes = PS1_MMCE_FS_MAX_PATH,
    .max_handles = PS1_MMCE_FS_MAX_HANDLES,
    .supported_open_flags = 0x3FU,
};

typedef enum {
    FS_PHASE_PARAMETERS,
    FS_PHASE_POLL,
} fs_command_phase_t;

/* Core 1 returns BUSY while Core 0 executes storage operations. Reads may
   wait up to READ_LOOKAHEAD_US between blocks before ending the transfer. */
typedef enum {
    FS_STAGE_NONE = 0,
    FS_STAGE_RESULT,
    FS_STAGE_READ_BLOCK,
    FS_STAGE_READ_FOOTER,
    FS_STAGE_WRITE_BLOCK,
} fs_session_stage_t;

typedef struct {
    fs_session_stage_t stage;
    ps1_mmce_fs_command_t command;
    uint32_t bytes_transferred;
    uint32_t requested_bytes;
    uint16_t block_sequence;
    uint16_t pending_write_bytes;
    ps1_mmce_fs_result_t read_result;
} fs_session_t;

static fs_session_t fs_session;
static bool fs_abort_issued;

static void fs_session_clear(void) {
    memset(&fs_session, 0, sizeof(fs_session));
}

void ps1_mmce_fs_commands_abort(void) {
    ps1_mmce_fs_abort();
    fs_session_clear();
    fs_abort_issued = true;
}

static void __time_critical_func(fs_respond_busy)(void) {
    uint8_t _;
    respondOrNextCmd(MMCE_FS_STATUS_BUSY);
    receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);
}

static bool __time_critical_func(fs_utf8_valid)(const uint8_t* text, size_t length) {
    size_t index = 0U;
    bool valid = true;
    while (index < length && valid) {
        uint8_t first = text[index++];
        uint32_t codepoint = 0U;
        uint8_t continuation_count = 0U;
        if (first < 0x80U) {
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            codepoint = first & 0x1FU;
            continuation_count = 1U;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            codepoint = first & 0x0FU;
            continuation_count = 2U;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            codepoint = first & 0x07U;
            continuation_count = 3U;
        } else {
            valid = false;
        }
        valid = valid && index + continuation_count <= length;
        for (uint8_t i = 0U; i < continuation_count && valid; ++i) {
            uint8_t continuation = text[index++];
            valid = (continuation & 0xC0U) == 0x80U;
            codepoint = (codepoint << 6U) | (continuation & 0x3FU);
        }
        if ((continuation_count == 1U && codepoint < 0x80U) || (continuation_count == 2U && codepoint < 0x800U) ||
            (continuation_count == 3U && codepoint < 0x10000U) || (codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU) {
            valid = false;
        }
    }
    return valid;
}

static bool __time_critical_func(fs_component_is_dot)(const char* component, size_t length) {
    return (length == 1U && component[0] == '.') || (length == 2U && component[0] == '.' && component[1] == '.');
}

static ps1_mmce_fs_result_t __time_critical_func(fs_validate_path_components)(const char* path, size_t text_length) {
    size_t component_start = 0U;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    for (size_t index = 0U; index < text_length && result == PS1_MMCE_FS_RESULT_OK; ++index) {
        uint8_t byte = (uint8_t)path[index];
        if (byte == '\\') {
            result = PS1_MMCE_FS_RESULT_ACCESS;
        } else if ((byte >= 0x01U && byte <= 0x1FU) || byte == 0x7FU) {
            result = PS1_MMCE_FS_RESULT_INVALID;
        } else if (byte == '/') {
            size_t component_length = index - component_start;
            if (component_length == 0U || fs_component_is_dot(&path[component_start], component_length)) {
                result = PS1_MMCE_FS_RESULT_ACCESS;
            }
            component_start = index + 1U;
        }
    }
    if (result == PS1_MMCE_FS_RESULT_OK && fs_component_is_dot(&path[component_start], text_length - component_start)) {
        result = PS1_MMCE_FS_RESULT_ACCESS;
    }
    return result;
}

static ps1_mmce_fs_result_t __time_critical_func(fs_validate_path)(const char* path, uint16_t length, bool allow_root) {
    size_t text_length = length - 1U;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
    if (path[length - 1U] != '\0' || memchr(path, '\0', text_length) != NULL) {
        result = PS1_MMCE_FS_RESULT_INVALID;
    } else if (text_length == 0U) {
        result = allow_root ? PS1_MMCE_FS_RESULT_OK : PS1_MMCE_FS_RESULT_ACCESS;
    } else if (path[0] == '/' || path[text_length - 1U] == '/') {
        result = PS1_MMCE_FS_RESULT_ACCESS;
    } else {
        result = fs_validate_path_components(path, text_length);
        if (result == PS1_MMCE_FS_RESULT_OK && !fs_utf8_valid((const uint8_t*)path, text_length)) {
            result = PS1_MMCE_FS_RESULT_INVALID;
        }
    }
    return result;
}

/* An invalid length omits the payload, including the second RENAME path.
   Completion distinguishes macro exits from a fully received invalid path. */
static void __time_critical_func(fs_receive_path)(char* path, bool allow_root, bool* complete, ps1_mmce_fs_result_t* result, bool* payload_omitted) {
    uint8_t high = 0U;
    uint8_t low = 0U;
    *complete = false;
    *result = PS1_MMCE_FS_RESULT_OK;
    *payload_omitted = false;
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&high);
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&low);
    uint16_t length = ((uint16_t)high << 8U) | low;
    if (length == 0U) {
        if (allow_root) {
            path[0] = '\0';
        } else {
            *result = PS1_MMCE_FS_RESULT_INVALID;
            *payload_omitted = true;
        }
    } else if (length > PS1_MMCE_FS_MAX_PATH) {
        *result = PS1_MMCE_FS_RESULT_NAME_TOO_LONG;
        *payload_omitted = true;
    } else {
        for (uint16_t index = 0U; index < length; ++index) {
            uint8_t byte = 0U;
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&byte);
            path[index] = (char)byte;
        }
        *result = fs_validate_path(path, length, allow_root);
    }
    *complete = true;
}

static bool __time_critical_func(fs_receive_u32)(uint32_t* value) {
    uint32_t received = 0U;
    bool complete = ps1_memory_card_sio_active();
    for (unsigned index = 0U; index < 4U && complete; ++index) {
        uint8_t byte = 0U;
        ps1_mc_respond(0x00);
        complete = recv_mc(&byte) == RECEIVE_OK && ps1_memory_card_sio_active();
        received = (received << 8U) | byte;
    }
    if (complete) {
        *value = received;
    }
    return complete;
}

static bool __time_critical_func(fs_receive_u64)(uint64_t* value) {
    uint32_t high = 0U;
    uint32_t low = 0U;
    bool complete = fs_receive_u32(&high) && fs_receive_u32(&low);
    if (complete) {
        *value = ((uint64_t)high << 32U) | low;
    }
    return complete;
}

static bool __time_critical_func(fs_receive_u16)(uint16_t* value) {
    uint16_t received = 0U;
    bool complete = ps1_memory_card_sio_active();
    for (unsigned index = 0U; index < 2U && complete; ++index) {
        uint8_t byte = 0U;
        ps1_mc_respond(0x00);
        complete = recv_mc(&byte) == RECEIVE_OK && ps1_memory_card_sio_active();
        received = (uint16_t)((received << 8U) | byte);
    }
    if (complete) {
        *value = received;
    }
    return complete;
}

static void __time_critical_func(fs_send_result_frame)(ps1_mmce_fs_result_t result) {
    uint8_t _;
    respondOrNextCmd((uint8_t)result);          receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);
}

static void __time_critical_func(fs_send_open_result_frame)(ps1_mmce_fs_result_t result, uint8_t handle) {
    uint8_t _;
    uint8_t retVal = result == PS1_MMCE_FS_RESULT_OK ? handle : 0xFFU;
    respondOrNextCmd((uint8_t)result);                  receiveOrNextCmd(&_);
    respondOrNextCmd(retVal);                           receiveOrNextCmd(&_);
    if (result == PS1_MMCE_FS_RESULT_OK) {
        ps1_mmce_fs_mark_handle_delivered(handle);
    }
    respondOrNextCmd(0xFF);
}

static void __time_critical_func(fs_send_stream_footer)(ps1_mmce_fs_result_t result, uint32_t count) {
    uint8_t _;
    respondOrNextCmd(MMCE_FS_STREAM_COMPLETE);    receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)result);            receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)(count >> 24U));    receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)(count >> 16U));    receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)(count >> 8U));     receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)count);             receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);
}

static uint8_t* __time_critical_func(fs_serialize_u16_be)(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
    return output + 2U;
}

/* Serialize fields explicitly; native struct padding and byte order are not wire data. */
static void __time_critical_func(fs_serialize_capabilities)(const fs_capabilities_t* capabilities, uint8_t* output) {
    memcpy(output, capabilities->magic, sizeof(capabilities->magic));
    uint8_t* next_byte = output + sizeof(capabilities->magic);
    next_byte = fs_serialize_u16_be(next_byte, capabilities->protocol_version);
    *next_byte++ = capabilities->read_buffer_count;
    *next_byte++ = capabilities->read_block_shift;
    next_byte = fs_serialize_u16_be(next_byte, capabilities->max_read_blocks);
    next_byte = fs_serialize_u16_be(next_byte, capabilities->max_write_bytes);
    next_byte = fs_serialize_u16_be(next_byte, capabilities->max_path_bytes);
    *next_byte++ = capabilities->max_handles;
    *next_byte = capabilities->supported_open_flags;
}

static void __time_critical_func(fs_cmd_get_capabilities)(void) {
    uint8_t discarded_byte = 0U;
    respondOrNextCmd(PS1_MMCE_FS_RESULT_OK);
    receiveOrNextCmd(&discarded_byte);

    uint8_t serialized_capabilities[MMCE_FS_CAPABILITIES_SIZE];
    fs_serialize_capabilities(&fs_capabilities, serialized_capabilities);
    for (size_t index = 0U; index < sizeof(serialized_capabilities); ++index) {
        respondOrNextCmd(serialized_capabilities[index]);
        receiveOrNextCmd(&discarded_byte);
    }

    /* The capability sentinel is acknowledged; the selection terminator is final. */
    respondOrNextCmd(MMCE_FS_CAPABILITY_SENTINEL);
    receiveOrNextCmd(&discarded_byte);
    respondOrNextCmd(0xFF);
}
/* Request preparation is allowed only while both the session and worker are idle. */
static ps1_mmce_fs_op_data_t* __time_critical_func(fs_prepare_operation)(void) {
    ps1_mmce_fs_op_data_t* op_data = NULL;
    if (fs_session.stage == FS_STAGE_NONE && ps1_mmce_fs_is_ready()) {
        op_data = ps1_mmce_fs_get_op_data();
        memset(op_data, 0, sizeof(*op_data));
    }
    return op_data;
}

static void __time_critical_func(fs_start_operation)(ps1_mmce_fs_command_t command, fs_session_stage_t stage, ps1_mmce_fs_operation_t operation) {
    fs_session.command = command;
    fs_session.stage = stage;
    ps1_mmce_fs_signal_operation(operation);
    fs_respond_busy();
}

static void __time_critical_func(fs_poll_open_result)(void) {
    if (!ps1_mmce_fs_is_ready()) {
        fs_respond_busy();
    } else {
        ps1_mmce_fs_op_data_t* op_data = ps1_mmce_fs_get_op_data();
        uint8_t handle = op_data->handle;
        ps1_mmce_fs_result_t result = (ps1_mmce_fs_result_t)op_data->result;
        fs_session_clear();
        fs_send_open_result_frame(result, handle);
    }
}

static void __time_critical_func(fs_poll_status_result)(void) {
    if (!ps1_mmce_fs_is_ready()) {
        fs_respond_busy();
    } else {
        ps1_mmce_fs_result_t result = (ps1_mmce_fs_result_t)ps1_mmce_fs_get_op_data()->result;
        fs_session_clear();
        fs_send_result_frame(result);
    }
}

static void __time_critical_func(fs_cmd_open)(ps1_mmce_fs_command_t command, fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_open_result_frame(PS1_MMCE_FS_RESULT_INTERNAL, 0xFFU);
        } else {
            bool directory = command == PS1_MMCE_FS_CMD_OPEN_DIR;
            if (!directory) {
                respondOrNextCmd(0x00);
                receiveOrNextCmd(&op_data->flags);
            }
            bool complete = false;
            bool payload_omitted = false;
            ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
            fs_receive_path(op_data->path[0], directory, &complete, &result, &payload_omitted);
            if (complete && ps1_memory_card_sio_active()) {
                if (result != PS1_MMCE_FS_RESULT_OK) {
                    fs_send_open_result_frame(result, 0xFFU);
                } else {
                    fs_start_operation(command, FS_STAGE_RESULT, directory ? PS1_MMCE_FS_OP_OPEN_DIR : PS1_MMCE_FS_OP_OPEN_FILE);
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_open_result();
    }
}

static void __time_critical_func(fs_cmd_close)(ps1_mmce_fs_command_t command, fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->handle);
            if (ps1_memory_card_sio_active()) {
                fs_start_operation(command, FS_STAGE_RESULT, command == PS1_MMCE_FS_CMD_CLOSE_DIR ? PS1_MMCE_FS_OP_CLOSE_DIR : PS1_MMCE_FS_OP_CLOSE_FILE);
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_status_result();
    }
}

static void __time_critical_func(fs_send_read_footer)(void) {
    uint32_t count = fs_session.bytes_transferred;
    ps1_mmce_fs_result_t result = fs_session.read_result;
    fs_session_clear();
    fs_send_stream_footer(result, count);
    if (ps1_memory_card_sio_active()) {
        ps1_mmce_fs_finish_read();
    }
}

static uint8_t __time_critical_func(fs_read_buffer_state)(ps1_mmce_fs_op_data_t* op_data, uint8_t index) {
    critical_section_enter_blocking(&ps1_mmce_fs_crit);
    uint8_t state = op_data->buffer_state[index];
    critical_section_exit(&ps1_mmce_fs_crit);
    return state;
}

/* Streaming reads enqueue bytes without the receive interleave of result frames. */
static void __time_critical_func(fs_send_read_block)(ps1_mmce_fs_op_data_t* op_data, uint8_t tail, uint16_t block_length, bool* complete) {
    *complete = false;
    respondOrNextCmd(MMCE_FS_STREAM_BLOCK_READY);
    respondOrNextCmd((uint8_t)(fs_session.block_sequence >> 8U));
    respondOrNextCmd((uint8_t)fs_session.block_sequence);
    respondOrNextCmd((uint8_t)(block_length >> 8U));
    respondOrNextCmd((uint8_t)block_length);
    for (uint16_t index = 0U; index < block_length; ++index) {
        respondOrNextCmd(op_data->buffer[tail][index]);
    }
    *complete = ps1_memory_card_sio_active();
}

static uint8_t __time_critical_func(fs_next_read_block_available)(ps1_mmce_fs_op_data_t* op_data, uint8_t state, uint8_t next_tail, uint16_t block_length) {
    uint8_t available = 0U;
    if (state == PS1_MMCE_FS_BUFFER_READY && fs_session.bytes_transferred + block_length < fs_session.requested_bytes) {
        uint64_t deadline = time_us_64() + MMCE_FS_READ_LOOKAHEAD_US;
        while (!available && ps1_memory_card_sio_active() && time_us_64() < deadline) {
            uint8_t next_state = fs_read_buffer_state(op_data, next_tail);
            available = next_state == PS1_MMCE_FS_BUFFER_READY;
            if (next_state == PS1_MMCE_FS_BUFFER_INVALID || ps1_mmce_fs_is_ready()) {
                break;
            }
        }
    }
    return available;
}

static void __time_critical_func(fs_release_read_block)(ps1_mmce_fs_op_data_t* op_data, uint8_t tail, uint8_t next_tail, uint16_t block_length) {
    fs_session.bytes_transferred += block_length;
    fs_session.block_sequence++;
    critical_section_enter_blocking(&ps1_mmce_fs_crit);
    op_data->buffer_state[tail] = PS1_MMCE_FS_BUFFER_NOT_READY;
    critical_section_exit(&ps1_mmce_fs_crit);
    op_data->tail_idx = next_tail;
}

static void __time_critical_func(fs_poll_read_blocks)(void) {
    ps1_mmce_fs_op_data_t* op_data = ps1_mmce_fs_get_op_data();
    bool continue_stream = true;
    while (continue_stream) {
        uint8_t tail = op_data->tail_idx;
        uint8_t state = fs_read_buffer_state(op_data, tail);
        if (state == PS1_MMCE_FS_BUFFER_NOT_READY) {
            if (!ps1_mmce_fs_is_ready()) {
                fs_respond_busy();
            } else {
                fs_session.read_result = (ps1_mmce_fs_result_t)op_data->result;
                fs_session.stage = FS_STAGE_READ_FOOTER;
                fs_send_read_footer();
            }
            break;
        }
        uint16_t block_length = op_data->buffer_length[tail];
        if (state == PS1_MMCE_FS_BUFFER_INVALID && block_length == 0U) {
            fs_session.read_result = (ps1_mmce_fs_result_t)op_data->result;
            fs_session.stage = FS_STAGE_READ_FOOTER;
            fs_send_read_footer();
            break;
        }
        bool complete = false;
        fs_send_read_block(op_data, tail, block_length, &complete);
        if (!complete) {
            break;
        }
        uint8_t next_tail = (uint8_t)((tail + 1U) % PS1_MMCE_FS_BUFFER_COUNT);
        uint8_t available = fs_next_read_block_available(op_data, state, next_tail, block_length);
        /* Preserve the commit boundary before the availability byte. */
        fs_release_read_block(op_data, tail, next_tail, block_length);
        respondOrNextCmd(available);
        if (!ps1_memory_card_sio_active()) {
            break;
        }
        if (state == PS1_MMCE_FS_BUFFER_INVALID) {
            fs_session.read_result = (ps1_mmce_fs_result_t)op_data->result;
            fs_session.stage = FS_STAGE_READ_FOOTER;
            respondOrNextCmd(0xFF);
            continue_stream = false;
        } else if (fs_session.bytes_transferred == fs_session.requested_bytes) {
            fs_session_clear();
            respondOrNextCmd(0xFF);
            if (ps1_memory_card_sio_active()) {
                ps1_mmce_fs_finish_read();
            }
            continue_stream = false;
        } else if (!available) {
            respondOrNextCmd(0xFF);
            continue_stream = false;
        }
    }
}

static bool __time_critical_func(fs_read_length_valid)(uint16_t block_count, uint16_t final_length) {
    bool valid = block_count <= PS1_MMCE_FS_MAX_READ_BLOCKS;
    valid = valid && (block_count == 0U ? final_length == 0U : final_length != 0U && final_length <= PS1_MMCE_FS_BLOCK_SIZE);
    return valid;
}

static void __time_critical_func(fs_cmd_read)(fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_stream_footer(PS1_MMCE_FS_RESULT_INTERNAL, 0U);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->handle);
            uint16_t block_count = 0U;
            uint16_t final_length = 0U;
            bool complete = fs_receive_u16(&block_count) && fs_receive_u16(&final_length);
            if (complete && ps1_memory_card_sio_active()) {
                if (!fs_read_length_valid(block_count, final_length)) {
                    fs_send_stream_footer(PS1_MMCE_FS_RESULT_RANGE, 0U);
                } else {
                    op_data->length = block_count == 0U ? 0U : (uint32_t)(block_count - 1U) * PS1_MMCE_FS_BLOCK_SIZE + final_length;
                    fs_session.requested_bytes = op_data->length;
                    fs_start_operation(PS1_MMCE_FS_CMD_READ_FILE, FS_STAGE_READ_BLOCK, PS1_MMCE_FS_OP_READ);
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        if (fs_session.stage == FS_STAGE_READ_FOOTER) {
            fs_send_read_footer();
        } else {
            fs_poll_read_blocks();
        }
    }
}

/* Consume completion once before any next-header macro can interrupt the poll. */
static ps1_mmce_fs_result_t __time_critical_func(fs_consume_write_completion)(ps1_mmce_fs_op_data_t* op_data) {
    uint32_t written_bytes = op_data->bytes_written;
    uint16_t pending_bytes = fs_session.pending_write_bytes;
    op_data->bytes_written = 0U;
    fs_session.pending_write_bytes = 0U;
    ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_INTERNAL;
    if (written_bytes <= pending_bytes) {
        if (pending_bytes != 0U) {
            fs_session.bytes_transferred += written_bytes;
        }
        /* The worker reports every short write as IO_ERROR. */
        result = (ps1_mmce_fs_result_t)op_data->result;
    }
    return result;
}

static void __time_critical_func(fs_finish_write)(ps1_mmce_fs_result_t result) {
    uint32_t count = fs_session.bytes_transferred;
    fs_session_clear();
    fs_send_stream_footer(result, count);
}

static void __time_critical_func(fs_receive_write_block)(ps1_mmce_fs_op_data_t* op_data, uint16_t expected_bytes, bool* complete,
                                                         ps1_mmce_fs_result_t* result) {
    uint8_t _;
    uint8_t sequence_high = 0U, sequence_low = 0U;
    uint8_t length_high = 0U, length_low = 0U;
    *complete = false;
    *result = PS1_MMCE_FS_RESULT_OK;
    /* Drain the client's status slot before receiving the block header. */
    respondOrNextCmd(MMCE_FS_STREAM_BLOCK_READY);
    receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&sequence_high);
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&sequence_low);
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&length_high);
    respondOrNextCmd(0x00);
    receiveOrNextCmd(&length_low);
    uint16_t sequence = ((uint16_t)sequence_high << 8U) | sequence_low;
    uint16_t block_length = ((uint16_t)length_high << 8U) | length_low;
    if (sequence != fs_session.block_sequence || block_length != expected_bytes) {
        *result = PS1_MMCE_FS_RESULT_INVALID;
    } else {
        for (uint16_t index = 0U; index < block_length; ++index) {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->buffer[0][index]);
        }
    }
    *complete = true;
}

static void __time_critical_func(fs_poll_write_block)(void) {
    if (!ps1_mmce_fs_is_ready()) {
        fs_respond_busy();
    } else {
        ps1_mmce_fs_op_data_t* op_data = ps1_mmce_fs_get_op_data();
        ps1_mmce_fs_result_t result = fs_consume_write_completion(op_data);
        uint32_t remaining_bytes = fs_session.requested_bytes - fs_session.bytes_transferred;
        if (result != PS1_MMCE_FS_RESULT_OK || remaining_bytes == 0U) {
            fs_finish_write(result);
        } else {
            uint16_t block_length = remaining_bytes > PS1_MMCE_FS_BLOCK_SIZE ? PS1_MMCE_FS_BLOCK_SIZE : (uint16_t)remaining_bytes;
            bool complete = false;
            fs_receive_write_block(op_data, block_length, &complete, &result);
            if (complete && result != PS1_MMCE_FS_RESULT_OK) {
                fs_finish_write(result);
            } else if (complete && ps1_memory_card_sio_active()) {
                fs_session.pending_write_bytes = block_length;
                op_data->buffer_length[0] = block_length;
                ps1_mmce_fs_signal_operation(PS1_MMCE_FS_OP_WRITE);
                fs_session.block_sequence++;
                fs_respond_busy();
            }
        }
    }
}

static void __time_critical_func(fs_cmd_write)(fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_stream_footer(PS1_MMCE_FS_RESULT_INTERNAL, 0U);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->handle);
            uint32_t requested_bytes = 0U;
            bool complete = fs_receive_u32(&requested_bytes);
            if (complete && ps1_memory_card_sio_active()) {
                if (requested_bytes > PS1_MMCE_FS_MAX_TRANSFER) {
                    fs_send_stream_footer(PS1_MMCE_FS_RESULT_RANGE, 0U);
                } else {
                    op_data->length = requested_bytes;
                    fs_session.requested_bytes = requested_bytes;
                    /* Buffer 0 is empty: the first worker run only validates access. */
                    fs_start_operation(PS1_MMCE_FS_CMD_WRITE_FILE, FS_STAGE_WRITE_BLOCK, PS1_MMCE_FS_OP_WRITE);
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_write_block();
    }
}

static void __time_critical_func(fs_poll_seek)(void) {
    uint8_t _;
    if (!ps1_mmce_fs_is_ready()) {
        fs_respond_busy();
    } else {
        ps1_mmce_fs_op_data_t* op_data = ps1_mmce_fs_get_op_data();
        ps1_mmce_fs_result_t result = (ps1_mmce_fs_result_t)op_data->result;
        uint64_t position = result == PS1_MMCE_FS_RESULT_OK ? op_data->position : 0U;
        fs_session_clear();
        respondOrNextCmd((uint8_t)result);
        receiveOrNextCmd(&_);
        for (unsigned index = 0U; index < 8U; ++index) {
            respondOrNextCmd((uint8_t)(position >> (56U - 8U * index)));
            receiveOrNextCmd(&_);
        }
        respondOrNextCmd(0xFF);
    }
}

static void __time_critical_func(fs_cmd_seek)(fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->handle);
            uint64_t offset = 0U;
            if (fs_receive_u64(&offset)) {
                respondOrNextCmd(0x00);
                receiveOrNextCmd(&op_data->whence);
                if (ps1_memory_card_sio_active()) {
                    /* Decode two's complement without an out-of-range unsigned cast. */
                    op_data->offset = offset <= INT64_MAX ? (int64_t)offset : -(int64_t)(~offset) - 1;
                    fs_start_operation(PS1_MMCE_FS_CMD_SEEK_FILE, FS_STAGE_RESULT, PS1_MMCE_FS_OP_SEEK_FILE);
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_seek();
    }
}

static ps1_mmce_fs_operation_t __time_critical_func(fs_path_operation)(ps1_mmce_fs_command_t command) {
    ps1_mmce_fs_operation_t operation = PS1_MMCE_FS_OP_NONE;
    switch (command) {
        case PS1_MMCE_FS_CMD_REMOVE_FILE: operation = PS1_MMCE_FS_OP_REMOVE_FILE; break;
        case PS1_MMCE_FS_CMD_CREATE_DIR: operation = PS1_MMCE_FS_OP_CREATE_DIR; break;
        case PS1_MMCE_FS_CMD_REMOVE_DIR: operation = PS1_MMCE_FS_OP_REMOVE_DIR; break;
        default: break;
    }
    return operation;
}

static void __time_critical_func(fs_cmd_path)(ps1_mmce_fs_command_t command, fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
        } else {
            bool complete = false;
            bool payload_omitted = false;
            ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
            fs_receive_path(op_data->path[0], false, &complete, &result, &payload_omitted);
            if (complete && ps1_memory_card_sio_active()) {
                if (result != PS1_MMCE_FS_RESULT_OK) {
                    fs_send_result_frame(result);
                } else {
                    fs_start_operation(command, FS_STAGE_RESULT, fs_path_operation(command));
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_status_result();
    }
}

/* Receive both paths before reporting validation errors, except when an
   invalid source length tells the client to omit the remaining payload. */
static void __time_critical_func(fs_receive_rename_paths)(ps1_mmce_fs_op_data_t* op_data, bool* complete, ps1_mmce_fs_result_t* result) {
    bool payload_omitted = false;
    ps1_mmce_fs_result_t source_result = PS1_MMCE_FS_RESULT_OK;
    ps1_mmce_fs_result_t destination_result = PS1_MMCE_FS_RESULT_OK;
    fs_receive_path(op_data->path[0], false, complete, &source_result, &payload_omitted);
    if (*complete && ps1_memory_card_sio_active() && !payload_omitted) {
        fs_receive_path(op_data->path[1], false, complete, &destination_result, &payload_omitted);
    }
    *result = source_result != PS1_MMCE_FS_RESULT_OK ? source_result : destination_result;
}

static void __time_critical_func(fs_cmd_rename)(fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
        } else {
            bool complete = false;
            ps1_mmce_fs_result_t result = PS1_MMCE_FS_RESULT_OK;
            fs_receive_rename_paths(op_data, &complete, &result);
            if (complete && ps1_memory_card_sio_active()) {
                if (result != PS1_MMCE_FS_RESULT_OK) {
                    fs_send_result_frame(result);
                } else {
                    fs_start_operation(PS1_MMCE_FS_CMD_RENAME, FS_STAGE_RESULT, PS1_MMCE_FS_OP_RENAME);
                }
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_status_result();
    }
}

static void __time_critical_func(fs_send_directory_entry)(ps1_mmce_fs_op_data_t* op_data, bool* complete) {
    uint8_t _;
    *complete = false;
    respondOrNextCmd(op_data->entry_type);    receiveOrNextCmd(&_);
    for (unsigned index = 0U; index < 8U; ++index) {
        respondOrNextCmd((uint8_t)(op_data->entry_size >> (56U - 8U * index)));
        receiveOrNextCmd(&_);
    }
    respondOrNextCmd((uint8_t)(op_data->name_length >> 8U));
    receiveOrNextCmd(&_);
    respondOrNextCmd((uint8_t)op_data->name_length);
    receiveOrNextCmd(&_);
    for (uint16_t index = 0U; index < op_data->name_length; ++index) {
        respondOrNextCmd((uint8_t)op_data->name[index]);
        receiveOrNextCmd(&_);
    }
    *complete = true;
}

static void __time_critical_func(fs_poll_read_dir)(void) {
    uint8_t _;
    if (!ps1_mmce_fs_is_ready()) {
        fs_respond_busy();
    } else {
        ps1_mmce_fs_op_data_t* op_data = ps1_mmce_fs_get_op_data();
        ps1_mmce_fs_result_t result = (ps1_mmce_fs_result_t)op_data->result;
        respondOrNextCmd((uint8_t)result);
        receiveOrNextCmd(&_);
        if (result == PS1_MMCE_FS_RESULT_OK) {
            bool complete = false;
            fs_send_directory_entry(op_data, &complete);
            if (complete) {
                fs_session_clear();
                if (ps1_memory_card_sio_active()) {
                    /* Next entry requires fresh READ_DIR params, never prefetch here. */
                    fs_respond_busy();
                }
            }
        } else {
            fs_session_clear();
            respondOrNextCmd(0xFF);
        }
    }
}

static void __time_critical_func(fs_cmd_read_dir)(fs_command_phase_t phase) {
    if (phase == FS_PHASE_PARAMETERS) {
        ps1_mmce_fs_op_data_t* op_data = fs_prepare_operation();
        if (op_data == NULL) {
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&op_data->handle);
            if (ps1_memory_card_sio_active()) {
                fs_start_operation(PS1_MMCE_FS_CMD_READ_DIR, FS_STAGE_RESULT, PS1_MMCE_FS_OP_READ_DIR);
            }
        }
    } else if (phase == FS_PHASE_POLL) {
        fs_poll_read_dir();
    }
}

/* Keep command routing in the entry point to avoid another SIO stack frame. */
static inline __attribute__((always_inline)) void __time_critical_func(fs_dispatch_command)(ps1_mmce_fs_command_t command, fs_command_phase_t phase) {
    switch (command) {
        case PS1_MMCE_FS_CMD_GET_CAPABILITIES: fs_cmd_get_capabilities(); break;
        case PS1_MMCE_FS_CMD_OPEN_FILE:
        case PS1_MMCE_FS_CMD_OPEN_DIR: fs_cmd_open(command, phase); break;
        case PS1_MMCE_FS_CMD_CLOSE_FILE:
        case PS1_MMCE_FS_CMD_CLOSE_DIR: fs_cmd_close(command, phase); break;
        case PS1_MMCE_FS_CMD_READ_FILE: fs_cmd_read(phase); break;
        case PS1_MMCE_FS_CMD_WRITE_FILE: fs_cmd_write(phase); break;
        case PS1_MMCE_FS_CMD_SEEK_FILE: fs_cmd_seek(phase); break;
        case PS1_MMCE_FS_CMD_REMOVE_FILE:
        case PS1_MMCE_FS_CMD_CREATE_DIR:
        case PS1_MMCE_FS_CMD_REMOVE_DIR: fs_cmd_path(command, phase); break;
        case PS1_MMCE_FS_CMD_RENAME: fs_cmd_rename(phase); break;
        case PS1_MMCE_FS_CMD_READ_DIR: fs_cmd_read_dir(phase); break;
        default:
            fs_session_clear();
            fs_send_result_frame(PS1_MMCE_FS_RESULT_INTERNAL);
            break;
    }
}

static void __time_critical_func(fs_cmd_poll)(void) {
    uint8_t _;
    if (fs_session.stage == FS_STAGE_NONE) {
        fs_send_result_frame(PS1_MMCE_FS_RESULT_INVALID);
    } else {
        respondOrNextCmd(0x00);
        receiveOrNextCmd(&_);
        if (!ps1_memory_card_sio_active()) {
            fs_session_clear();
        } else {
            fs_dispatch_command(fs_session.command, FS_PHASE_POLL);
        }
    }
}

void __time_critical_func(ps1_mmce_fs_command)(void) {
    uint8_t subcommand = 0xFFU;
    uint8_t _;
    respondOrNextCmd(0x5A);
    receiveOrNextCmd(&subcommand);
    if (subcommand == PS1_MMCE_FS_CMD_ABORT) {
        /* Repeated ABORT polls one cancellation; it must not cancel the next op. */
        if (fs_session.stage != FS_STAGE_NONE || !fs_abort_issued) {
            ps1_mmce_fs_commands_abort();
        }
        if (!ps1_mmce_fs_is_ready()) {
            fs_respond_busy();
        } else {
            respondOrNextCmd(0xFF);
        }
    } else {
        fs_abort_issued = false;
        if (subcommand > PS1_MMCE_FS_CMD_POLL) {
            /* Invalid command */
            ps1_mmce_fs_commands_abort();
            respondOrNextCmd(0xFF);
        } else {
            respondOrNextCmd(0x00);
            receiveOrNextCmd(&_);
            if (subcommand == PS1_MMCE_FS_CMD_POLL) {
                /* Polling command from previous call */
                fs_cmd_poll();
            } else {
                fs_dispatch_command((ps1_mmce_fs_command_t)subcommand, FS_PHASE_PARAMETERS);
            }
        }
    }
}
