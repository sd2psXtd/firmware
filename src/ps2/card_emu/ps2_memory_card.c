#include "history_tracker/ps2_history_tracker.h"
#include "ps2_cardman.h"
#include "debug.h"
#include "hardware/timer.h"
#include "keystore.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "ps2_mc_auth.h"
#include "ps2_mc_commands.h"
#include "ps2_mc_internal.h"
#include "mmceman/ps2_mmceman.h"
#include "mmceman/ps2_mmceman_commands.h"
#include "mmceman/ps2_mmceman_fs.h"
#include "ps2_card_backend.h"

#include "ps2_card_backend.h"

#include <hardware/watchdog.h>
#include <settings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if LOG_LEVEL_PS2_MC == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_MC, level, fmt, ##x)
#endif



uint8_t term = 0xFF;

static int memcard_running;
volatile bool ps2_card_active;

volatile int ps2_mc_exit_request;
static volatile int mc_exit_response, mc_enter_request, mc_enter_response;

static int memcard_variant;

ps2_memory_card_backend_t* ps2_card_backend = NULL;

const uint8_t EccTable[] = {
    0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00, 0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0,
    0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3, 0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2, 0x11, 0x96, 0x87, 0x00,
    0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11, 0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3, 0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77,
    0x66, 0xe1, 0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22, 0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87,
    0x87, 0x00, 0x11, 0x96, 0x22, 0xa5, 0xb4, 0x33, 0xf0, 0x77, 0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0, 0xf0, 0x77,
    0x66, 0xe1, 0x55, 0xd2, 0xc3, 0x44, 0x44, 0xc3, 0xd2, 0x55, 0xe1, 0x66, 0x77, 0xf0, 0x33, 0xb4, 0xa5, 0x22, 0x96, 0x11, 0x00, 0x87, 0x87, 0x00, 0x11, 0x96,
    0x22, 0xa5, 0xb4, 0x33, 0x22, 0xa5, 0xb4, 0x33, 0x87, 0x00, 0x11, 0x96, 0x96, 0x11, 0x00, 0x87, 0x33, 0xb4, 0xa5, 0x22, 0xe1, 0x66, 0x77, 0xf0, 0x44, 0xc3,
    0xd2, 0x55, 0x55, 0xd2, 0xc3, 0x44, 0xf0, 0x77, 0x66, 0xe1, 0x11, 0x96, 0x87, 0x00, 0xb4, 0x33, 0x22, 0xa5, 0xa5, 0x22, 0x33, 0xb4, 0x00, 0x87, 0x96, 0x11,
    0xd2, 0x55, 0x44, 0xc3, 0x77, 0xf0, 0xe1, 0x66, 0x66, 0xe1, 0xf0, 0x77, 0xc3, 0x44, 0x55, 0xd2, 0xc3, 0x44, 0x55, 0xd2, 0x66, 0xe1, 0xf0, 0x77, 0x77, 0xf0,
    0xe1, 0x66, 0xd2, 0x55, 0x44, 0xc3, 0x00, 0x87, 0x96, 0x11, 0xa5, 0x22, 0x33, 0xb4, 0xb4, 0x33, 0x22, 0xa5, 0x11, 0x96, 0x87, 0x00};

void calcECC(uint8_t *ecc, const uint8_t *data) {
    int i, c;

    ecc[0] = ecc[1] = ecc[2] = 0;

    for (i = 0; i < 0x80; i++) {
        c = EccTable[data[i]];

        ecc[0] ^= c;
        if (c & 0x80) {
            ecc[1] ^= ~i;
            ecc[2] ^= i;
        }
    }
    ecc[0] = ~ecc[0];
    ecc[0] &= 0x77;

    ecc[1] = ~ecc[1];
    ecc[1] &= 0x7f;

    ecc[2] = ~ecc[2];
    ecc[2] &= 0x7f;

    return;
}

static void __time_critical_func(mc_main_loop)(void) {
    while (1) {
        uint8_t cmd = 0;

        while (!ps2_card_backend->transactionAborted()) {
            if (ps2_mc_exit_request) {
                return;
            }
        }

        if (mmceman_timeout_detected) {
            log(LOG_WARN, "Timeout detected during MMCEMAN OP, PS2 likely reset\n");
            mmceman_callback = NULL;
            mmceman_transfer_stage = 0;

            //Only set if we're in a read
            if (ps2_mmceman_fs_get_operation() == 0x3) {
                mmceman_fs_abort_read = true;
            }
            mmceman_timeout_detected = false;
        }

        ps2_card_backend->signalTransactionReady();
        if (mmceman_callback != NULL) {
            mmceman_callback();
            continue;
        }

        uint8_t received = ps2_card_backend->receiveFirst(&cmd);

        if (received == PS2_RECEIVE_EXIT) {
            break;
        }
        if (received == PS2_RECEIVE_RESET)
            continue;


        if (cmd == PS2_SIO2_CMD_IDENTIFIER) {
            if (ps2_card_backend->probe()) {
                continue;
            }

            //Don't respond to mcman after a card switch to trigger its internal reset
            if (mmceman_mcman_retry_counter > 0) {
                log(LOG_WARN, "Ignoring mcman for another %i requests\n", mmceman_mcman_retry_counter);
                mmceman_mcman_retry_counter--;
                continue;
            } else if (!ps2_cardman_is_accessible() && (ps2_cardman_get_state() == PS2_CM_STATE_GAMEID)) {
                /* game id card is not yet accessible */
                continue;
            }

            /* resp to 0x81 */
            ps2_card_backend->respond(0xFF);

            /* sub cmd */
            if (ps2_card_backend->receive(&cmd) == PS2_RECEIVE_RESET)
                continue;

            log(LOG_TRACE, "%s: 0x81 %.02x\n", __func__, cmd);
            switch (cmd) {
                case PS2_SIO2_CMD_0x11: ps2_mc_cmd_0x11(); break;
                case PS2_SIO2_CMD_0x12: ps2_mc_cmd_0x12(); break;
                case PS2_SIO2_CMD_SET_ERASE_ADDRESS: ps2_mc_cmd_setEraseAddress(); break;
                case PS2_SIO2_CMD_SET_WRITE_ADDRESS: ps2_mc_cmd_setWriteAddress(); break;
                case PS2_SIO2_CMD_SET_READ_ADDRESS:
                    if (ps2_cardman_is_accessible())
                        ps2_mc_cmd_setReadAddress();
                    break;
                case PS2_SIO2_CMD_GET_SPECS: ps2_mc_cmd_getSpecs(); break;
                case PS2_SIO2_CMD_SET_TERMINATOR: ps2_mc_cmd_setTerminator(); break;
                case PS2_SIO2_CMD_GET_TERMINATOR: ps2_mc_cmd_getTerminator(); break;
                case PS2_SIO2_CMD_WRITE_DATA: ps2_mc_cmd_writeData(); break;
                case PS2_SIO2_CMD_READ_DATA:
                    if (ps2_cardman_is_accessible())
                        ps2_mc_cmd_readData();
                    break;
                case PS2_SIO2_CMD_COMMIT_DATA:
                    if (ps2_cardman_is_accessible())
                        ps2_mc_cmd_commitData();
                    break;
                case PS2_SIO2_CMD_ERASE:
                    if (ps2_cardman_is_accessible())
                        ps2_mc_cmd_erase();
                    break;
                case PS2_SIO2_CMD_BF: ps2_mc_cmd_0xBF(); break;
                case PS2_SIO2_CMD_AUTH_RESET: ps2_mc_auth_reset(); break;
                case PS2_SIO2_CMD_KEY_SELECT: ps2_mc_auth_keySelect(); break;
                case PS2_SIO2_CMD_AUTH:
                    if (ps2_magicgate)
                        ps2_mc_auth();
                    break;
                case PS2_SIO2_CMD_SESSION_KEY_0:
                case PS2_SIO2_CMD_SESSION_KEY_1: ps2_mc_sessionKeyEncr(); break;
                default: DPRINTF("Unknown Subcommand: %02x\n", cmd); break;
            }
        } else if (cmd == PS2_MMCEMAN_CMD_IDENTIFIER) {
            /* resp to 0x8B */
            ps2_card_backend->respond(0xAA);

            /* sub cmd */
            receiveOrNextCmd(&cmd);

            switch (cmd)
            {
                case MMCEMAN_PING: ps2_mmceman_cmd_ping(); break;
                case MMCEMAN_GET_STATUS: ps2_mmceman_cmd_get_status(); break;
                case MMCEMAN_GET_CARD: ps2_mmceman_cmd_get_card(); break;
                case MMCEMAN_SET_CARD: ps2_mmceman_cmd_set_card(); break;
                case MMCEMAN_GET_CHANNEL: ps2_mmceman_cmd_get_channel(); break;
                case MMCEMAN_SET_CHANNEL: ps2_mmceman_cmd_set_channel(); break;
                case MMCEMAN_GET_GAMEID: ps2_mmceman_cmd_get_gameid(); break;
                case MMCEMAN_SET_GAMEID: ps2_mmceman_cmd_set_gameid(); break;
                case MMCEMAN_UNMOUNT_BOOTCARD: ps2_mmceman_cmd_unmount_bootcard(); break;
                case MMCEMAN_RESET: ps2_mmceman_cmd_reset(); break;
                case MMCEMAN_SET_CARD_CHANNEL: ps2_mmceman_cmd_set_card_channel(); break;
                case MMCEMAN_CMD_FS_OPEN: ps2_mmceman_cmd_fs_open(); break;
                case MMCEMAN_CMD_FS_CLOSE: ps2_mmceman_cmd_fs_close(); break;
                case MMCEMAN_CMD_FS_READ: ps2_mmceman_cmd_fs_read(); break;
                case MMCEMAN_CMD_FS_WRITE: ps2_mmceman_cmd_fs_write(); break;
                case MMCEMAN_CMD_FS_LSEEK: ps2_mmceman_cmd_fs_lseek(); break;
                case MMCEMAN_CMD_FS_REMOVE: ps2_mmceman_cmd_fs_remove(); break;
                case MMCEMAN_CMD_FS_MKDIR: ps2_mmceman_cmd_fs_mkdir(); break;
                case MMCEMAN_CMD_FS_RMDIR: ps2_mmceman_cmd_fs_rmdir(); break;
                case MMCEMAN_CMD_FS_DOPEN: ps2_mmceman_cmd_fs_dopen(); break;
                case MMCEMAN_CMD_FS_DCLOSE: ps2_mmceman_cmd_fs_dclose(); break;
                case MMCEMAN_CMD_FS_DREAD: ps2_mmceman_cmd_fs_dread(); break;
                case MMCEMAN_CMD_FS_GETSTAT: ps2_mmceman_cmd_fs_getstat(); break;
                case MMCEMAN_CMD_FS_RENAME: ps2_mmceman_cmd_fs_rename(); break;

                case MMCEMAN_CMD_FS_LSEEK64: ps2_mmceman_cmd_fs_lseek64(); break;
                case MMCEMAN_CMD_FS_READ_SECTOR: ps2_mmceman_cmd_fs_read_sector(); break;
                default: log(LOG_WARN, "Unknown Subcommand: %02x\n", cmd); break;
            }
        } else if (cmd == PS1_SIO2_CMD_IDENTIFIER) {
            settings_set_mode(MODE_TEMP_PS1);
            continue;
        } else {
            // not for us
            ps2_card_backend->abort();
            continue;
        }

        ps2_card_backend->finish();
    }
}

static void __no_inline_not_in_flash_func(mc_main)(void) {
    while (1) {
        while (!mc_enter_request) {}
        log(LOG_TRACE, "Entering memcard core\n");
        mc_enter_response = 1;

        ps2_history_tracker_card_changed();
        memcard_running = 1;
        mmceman_transfer_stage = 0;
        mmceman_callback = NULL;

        log(LOG_TRACE, "Resetting PIO for PS2 memory card emulation\n");
        ps2_card_backend->reset();
        mc_main_loop();

        log(LOG_TRACE, "%s exit\n", __func__);
        if (settings_get_mode(true) != MODE_PS2 || settings_get_ps2_variant() != memcard_variant) {
            log(LOG_TRACE, "Mode or variant changed, exiting memcard core\n");
            return;
        } else {
            mc_exit_response = 1;
            log(LOG_TRACE, "Mode still PS2, reinitializing memcard core\n");
        }
    }
}


void ps2_memory_card_main(void) {
    multicore_lockout_victim_init();
    memcard_variant = settings_get_ps2_variant();
    ps2_card_backend = ps2_card_backend_get();
    log(LOG_TRACE, "Secondary core!\n");
    ps2_card_backend->init();
    ps2_card_backend->startup();
    mc_main();
    ps2_card_backend->shutdown();
    ps2_card_backend->deinit();
    log(LOG_TRACE, "Secondary core exit!\n");
    mc_exit_response = 1;
}


void ps2_memory_card_exit(void) {
    uint64_t exit_timeout = time_us_64() + (1000 * 1000);
    if (!memcard_running)
        return;

    ps2_mc_exit_request = 1;
    while (!mc_exit_response) {
        if (time_us_64() > exit_timeout) {
            log(LOG_ERROR, "MEMCARD EXIT TIMEOUT! This is an unrecoverable error! Rebooting...\n");
            watchdog_reboot(0, 0, 0);
            while (true) {
                tight_loop_contents();
            }
        }
    };
    ps2_card_backend->abort();
    ps2_mc_exit_request = mc_exit_response = 0;
    memcard_running = 0;

    ps2_mmceman_rearm_retry_counter();
    log(LOG_TRACE, "MEMCARD EXIT END!\n");
}

void ps2_memory_card_enter(void) {
    if (memcard_running)
        return;

    generateIvSeedNonce();
    mc_enter_request = 1;
    while (!mc_enter_response) {}
    mc_enter_request = mc_enter_response = 0;
}
