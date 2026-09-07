#include <ctype.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>
#include <pico/stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if WITH_GUI
#include "gui.h"
#endif
#include "ps1/ps1_mmce.h"
#include "ps2/mmceman/ps2_mmceman.h"

#include "debug.h"
#include "serial_input.h"
#include "settings.h"
#include "version.h"

#define SERIAL_INPUT_BUFFER_SIZE 256
#define SERIAL_INPUT_MAX_ARGS   8

#define ASCII_BS  0x08
#define ASCII_DEL 0x7F

typedef enum {
    SERIAL_INPUT_CMD_NONE = 0,
    SERIAL_INPUT_CMD_INVALID,
    SERIAL_INPUT_CMD_RESET,
    SERIAL_INPUT_CMD_RESET_TO_BOOTLOADER,
    SERIAL_INPUT_CMD_PS1_MODE,
    SERIAL_INPUT_CMD_PS2_MODE,
    SERIAL_INPUT_CMD_PS2_VARIANT_RETAIL,
    SERIAL_INPUT_CMD_PS2_VARIANT_PROTO,
    SERIAL_INPUT_CMD_PS2_VARIANT_SC2,
    SERIAL_INPUT_CMD_PS2_VARIANT_COH,
    SERIAL_INPUT_CMD_CHANNEL_UP,
    SERIAL_INPUT_CMD_CHANNEL_DOWN,
    SERIAL_INPUT_CMD_CARD_UP,
    SERIAL_INPUT_CMD_CARD_DOWN,
    SERIAL_INPUT_CMD_CARD_IDX,
    SERIAL_INPUT_CMD_CHANNEL_IDX,
    SERIAL_INPUT_CMD_GAMEID,
    SERIAL_INPUT_CMD_VERSION,
    SERIAL_INPUT_CMD_HELP
} serial_input_cmd_t;

typedef struct {
    serial_input_cmd_t cmd;
    int idx;
    int channel;
    char gameid[16];
    const char* error;
} serial_input_cmd_data_t;

static const char prompt[] = "> ";

static const char help_text[] =
    "Serial Input Commands:\n"
    "  help                                - Show this help\n"
    "  version                             - Show firmware version and settings\n"
    "  reset bl                            - Reset to bootloader\n"
    "  reset dev                           - Reset device\n"
    "  channel up                          - Channel up\n"
    "  channel down                        - Channel down\n"
    "  set channel <idx>                   - Set channel index\n"
    "  card up                             - Card up\n"
    "  card down                           - Card down\n"
    "  set card <idx> [channel <idx>]      - Set card with optional channel\n"
    "  set game <id> [channel <idx>]       - Set game with optional channel\n"
    "  set mode <mode>                     - Set mode: ps1, ps2\n"
    "  set variant <variant>               - Set PS2 variant: retail, proto, conquest, arcade\n";

static char in_buffer[SERIAL_INPUT_BUFFER_SIZE];
static size_t in_len;
static bool prompt_started;
static bool prompt_needs_redraw;
static bool ignore_next_lf;

static void cmd_data_init(serial_input_cmd_data_t* cmd_data) {
    cmd_data->cmd = SERIAL_INPUT_CMD_NONE;
    cmd_data->idx = -1;
    cmd_data->channel = -1;
    cmd_data->gameid[0] = '\0';
    cmd_data->error = NULL;
}

static void terminal_prompt(void) {
    printf("%s", prompt);
    prompt_started = true;
    prompt_needs_redraw = false;
}

static void terminal_redraw_input_line(void) {
    if (!prompt_started) {
        terminal_prompt();
        return;
    }

    printf("\r\n%s%s", prompt, in_buffer);
    prompt_needs_redraw = false;
}

static void terminal_erase_last_char(void) {
    /*
     * Most terminals send either BS (^H, 0x08) or DEL (^?, 0x7F) for the
     * Backspace key.  Erase visually with ANSI cursor-left instead of \b so
     * terminals that don't treat BS as a cursor movement still redraw cleanly.
     */
    printf("\x1b[D \x1b[D");
}

void serial_input_notify_output(void) {
    if (prompt_started) {
        prompt_needs_redraw = true;
    }
}

void serial_input_begin_output(void) {
    if (prompt_started) {
        printf("\r\n");
        prompt_started = false;
        prompt_needs_redraw = true;
    }
}

static int tokenize(char* input, char* argv[], int max_args) {
    int argc = 0;
    char* p = input;

    while (*p != '\0') {
        while (isspace((unsigned char)*p)) {
            *p++ = '\0';
        }

        if (*p == '\0') {
            break;
        }

        if (argc >= max_args) {
            return -1;
        }

        argv[argc++] = p;

        while ((*p != '\0') && !isspace((unsigned char)*p)) {
            ++p;
        }
    }

    return argc;
}

static bool parse_int_arg(const char* text, int* value) {
    char* end = NULL;
    long parsed = strtol(text, &end, 10);

    if ((text == end) || (*end != '\0') || (parsed < 0) || (parsed > UINT16_MAX)) {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static bool parse_optional_channel(int argc, char* argv[], int start_arg, int* channel, const char** error) {
    *channel = -1;

    if (argc == start_arg) {
        return true;
    }

    if ((argc != start_arg + 2) || (strcmp(argv[start_arg], "channel") != 0)) {
        *error = "Expected optional syntax: channel <idx>";
        return false;
    }

    if (!parse_int_arg(argv[start_arg + 1], channel)) {
        *error = "Invalid channel index";
        return false;
    }

    return true;
}

static void parse_command(char* input, serial_input_cmd_data_t* cmd_data) {
    char* argv[SERIAL_INPUT_MAX_ARGS];
    int argc;

    cmd_data_init(cmd_data);
    argc = tokenize(input, argv, SERIAL_INPUT_MAX_ARGS);

    if (argc == 0) {
        return;
    }

    if (argc < 0) {
        cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
        cmd_data->error = "Too many arguments";
        return;
    }

    if ((argc == 1) && (strcmp(argv[0], "help") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_HELP;
    } else if ((argc == 1) && (strcmp(argv[0], "version") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_VERSION;
    } else if ((argc == 2) && (strcmp(argv[0], "reset") == 0) && (strcmp(argv[1], "bl") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_RESET_TO_BOOTLOADER;
    } else if ((argc == 2) && (strcmp(argv[0], "reset") == 0) && (strcmp(argv[1], "dev") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_RESET;
    } else if ((argc == 2) && (strcmp(argv[0], "channel") == 0) && (strcmp(argv[1], "up") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CHANNEL_UP;
    } else if ((argc == 2) && (strcmp(argv[0], "channel") == 0) && (strcmp(argv[1], "down") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CHANNEL_DOWN;
    } else if ((argc == 2) && (strcmp(argv[0], "card") == 0) && (strcmp(argv[1], "up") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CARD_UP;
    } else if ((argc == 2) && (strcmp(argv[0], "card") == 0) && (strcmp(argv[1], "down") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CARD_DOWN;
    } else if ((argc >= 3) && (strcmp(argv[0], "set") == 0) && (strcmp(argv[1], "channel") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CHANNEL_IDX;
        if ((argc != 3) || !parse_int_arg(argv[2], &cmd_data->idx)) {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
            cmd_data->error = "Usage: set channel <idx>";
        }
    } else if ((argc >= 3) && (strcmp(argv[0], "set") == 0) && (strcmp(argv[1], "card") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_CARD_IDX;
        if (!parse_int_arg(argv[2], &cmd_data->idx)) {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
            cmd_data->error = "Invalid card index";
        } else if (!parse_optional_channel(argc, argv, 3, &cmd_data->channel, &cmd_data->error)) {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
        }
    } else if ((argc >= 3) && (strcmp(argv[0], "set") == 0) && (strcmp(argv[1], "game") == 0)) {
        cmd_data->cmd = SERIAL_INPUT_CMD_GAMEID;
        if (strlen(argv[2]) >= sizeof(cmd_data->gameid)) {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
            cmd_data->error = "Game ID is too long";
        } else if (!parse_optional_channel(argc, argv, 3, &cmd_data->channel, &cmd_data->error)) {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
        } else {
            strcpy(cmd_data->gameid, argv[2]);
        }
    } else if ((argc == 3) && (strcmp(argv[0], "set") == 0) && (strcmp(argv[1], "mode") == 0)) {
        if (strcmp(argv[2], "ps1") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS1_MODE;
        } else if (strcmp(argv[2], "ps2") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS2_MODE;
        } else {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
            cmd_data->error = "Mode must be one of: ps1, ps2";
        }
    } else if ((argc == 3) && (strcmp(argv[0], "set") == 0) && (strcmp(argv[1], "variant") == 0)) {
        if (strcmp(argv[2], "retail") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS2_VARIANT_RETAIL;
        } else if (strcmp(argv[2], "proto") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS2_VARIANT_PROTO;
        } else if (strcmp(argv[2], "conquest") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS2_VARIANT_SC2;
        } else if (strcmp(argv[2], "arcade") == 0) {
            cmd_data->cmd = SERIAL_INPUT_CMD_PS2_VARIANT_COH;
        } else {
            cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
            cmd_data->error = "Variant must be one of: retail, proto, conquest, arcade";
        }
    } else {
        cmd_data->cmd = SERIAL_INPUT_CMD_INVALID;
        cmd_data->error = "Unknown command. Type 'help' for available commands.";
    }
}

static void execute_command(const serial_input_cmd_data_t* cmd_data) {
    switch (cmd_data->cmd) {
        case SERIAL_INPUT_CMD_NONE:
            break;
        case SERIAL_INPUT_CMD_INVALID:
            printf("%s\n", cmd_data->error ? cmd_data->error : "Invalid command");
            break;
        case SERIAL_INPUT_CMD_RESET_TO_BOOTLOADER:
            printf("Resetting to Bootloader\n");
            reset_usb_boot(0, 0);
            break;
        case SERIAL_INPUT_CMD_RESET:
            printf("Resetting\n");
            watchdog_reboot(0, 0, 0);
            break;
        case SERIAL_INPUT_CMD_CHANNEL_UP:
            printf("Channel Up\n");
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_next_ch(false);
            } else {
                ps1_mmce_next_ch(false);
            }
            break;
        case SERIAL_INPUT_CMD_CHANNEL_DOWN:
            printf("Channel Down\n");
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_prev_ch(false);
            } else {
                ps1_mmce_prev_ch(false);
            }
            break;
        case SERIAL_INPUT_CMD_CHANNEL_IDX:
            printf("Set Channel Index: %d\n", cmd_data->idx);
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_set_channel((uint16_t)cmd_data->idx, false);
            } else {
                ps1_mmce_set_channel((uint16_t)cmd_data->idx, false);
            }
            break;
        case SERIAL_INPUT_CMD_CARD_UP:
            printf("Card Up\n");
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_next_idx(false);
            } else {
                ps1_mmce_next_idx(false);
            }
            break;
        case SERIAL_INPUT_CMD_CARD_DOWN:
            printf("Card Down\n");
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_prev_idx(false);
            } else {
                ps1_mmce_prev_idx(false);
            }
            break;
        case SERIAL_INPUT_CMD_CARD_IDX:
            printf("Set Card Index: %d\n", cmd_data->idx);
            if (settings_get_mode(true) == MODE_PS2) {
                ps2_mmceman_set_card((uint16_t)cmd_data->idx, false);
                if (cmd_data->channel >= 0) {
                    ps2_mmceman_set_channel((uint16_t)cmd_data->channel, false);
                }
            } else {
                ps1_mmce_set_card((uint16_t)cmd_data->idx, false);
                if (cmd_data->channel >= 0) {
                    ps1_mmce_set_channel((uint16_t)cmd_data->channel, false);
                }
            }
            break;
        case SERIAL_INPUT_CMD_GAMEID:
            printf("Set Game ID: %s\n", cmd_data->gameid);
            if (settings_get_mode(true) == MODE_PS2) {
                if (!ps2_mmceman_set_gameid((const uint8_t*)cmd_data->gameid)) {
                    printf("Invalid Game ID: %s\n", cmd_data->gameid);
                } else if (cmd_data->channel >= 0) {
                    ps2_mmceman_set_channel((uint16_t)cmd_data->channel, false);
                }
            } else {
                if (!ps1_mmce_set_gameid((const uint8_t*)cmd_data->gameid)) {
                    printf("Invalid Game ID: %s\n", cmd_data->gameid);
                } else if (cmd_data->channel >= 0) {
                    ps1_mmce_set_channel((uint16_t)cmd_data->channel, false);
                }
            }
            break;
        case SERIAL_INPUT_CMD_PS1_MODE:
            printf("Set Mode: PS1\n");
            settings_set_mode(MODE_PS1);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_PS2_MODE:
            printf("Set Mode: PS2\n");
            settings_set_mode(MODE_PS2);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_PS2_VARIANT_RETAIL:
            printf("Set PS2 Variant: Retail\n");
            settings_set_ps2_variant(PS2_VARIANT_RETAIL);
            settings_set_mode(MODE_PS2);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_PS2_VARIANT_PROTO:
            printf("Set PS2 Variant: Proto\n");
            settings_set_ps2_variant(PS2_VARIANT_PROTO);
            settings_set_mode(MODE_PS2);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_PS2_VARIANT_SC2:
            printf("Set PS2 Variant: Conquest\n");
            settings_set_ps2_variant(PS2_VARIANT_SC2);
            settings_set_mode(MODE_PS2);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_PS2_VARIANT_COH:
            printf("Set PS2 Variant: Arcade\n");
            settings_set_ps2_variant(PS2_VARIANT_COH);
            settings_set_mode(MODE_PS2);
            #if WITH_GUI
            gui_request_refresh();
            #endif
            break;
        case SERIAL_INPUT_CMD_VERSION: {
            const char* variant_name = "Unknown";
            switch (settings_get_ps2_variant()) {
                case PS2_VARIANT_RETAIL: variant_name = "Retail"; break;
                case PS2_VARIANT_PROTO: variant_name = "Proto"; break;
                case PS2_VARIANT_COH: variant_name = "Arcade"; break;
                case PS2_VARIANT_SC2: variant_name = "Conquest"; break;
                default: break;
            }
            printf("Version: %s\n", sd2psx_version);
            printf("Commit: %s\n", sd2psx_commit);
            printf("Branch: %s\n", sd2psx_branch);
            printf("HW Variant: %s\n", sd2psx_variant);
            printf("Mode: %s\n", (settings_get_mode(true) == MODE_PS2) ? "PS2" : "PS1");
            printf("PS2 Variant: %s\n", variant_name);
            break;
        }
        case SERIAL_INPUT_CMD_HELP:
            printf("%s", help_text);
            break;
    }
}

static void submit_line(void) {
    char parse_buffer[SERIAL_INPUT_BUFFER_SIZE];
    serial_input_cmd_data_t cmd_data;

    printf("\r\n");

    if (in_len == 0) {
        terminal_prompt();
        return;
    }

    memcpy(parse_buffer, in_buffer, in_len + 1);
    parse_command(parse_buffer, &cmd_data);

    in_len = 0;
    in_buffer[0] = '\0';

    execute_command(&cmd_data);
    terminal_prompt();
}

void serial_input_process(void) {
    int charin;

    if (!prompt_started) {
        terminal_prompt();
    } else if (prompt_needs_redraw && (in_len > 0)) {
        terminal_redraw_input_line();
    } else {
        prompt_needs_redraw = false;
    }

    charin = getchar_timeout_us(0);
    while (charin != PICO_ERROR_TIMEOUT) {
        if ((charin == '\n') && ignore_next_lf) {
            ignore_next_lf = false;
        } else if ((charin == '\n') || (charin == '\r')) {
            ignore_next_lf = (charin == '\r');
            submit_line();
        } else if ((charin == ASCII_BS) || (charin == ASCII_DEL)) {
            if (in_len > 0) {
                --in_len;
                in_buffer[in_len] = '\0';
                terminal_erase_last_char();
            }
        } else if ((charin >= 0x20) && (charin <= 0x7E)) {
            if (in_len < sizeof(in_buffer) - 1) {
                in_buffer[in_len++] = (char)charin;
                in_buffer[in_len] = '\0';
                printf("%c", (char)charin);
            } else {
                printf("\a");
            }
        }

        charin = getchar_timeout_us(0);
    }
}
