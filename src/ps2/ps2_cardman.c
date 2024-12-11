#include "ps2_cardman.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "card_emu/ps2_sd2psxman.h"
#include "debug.h"
#include "game_db/game_db.h"
#include "hardware/timer.h"
#include "mmce_fs/ps2_mmce_fs.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#if WITH_PSRAM
    #include "ps2_dirty.h"
    #include "psram/psram.h"
#endif
#include "sd.h"
#include "settings.h"
#include "util.h"

#if LOG_LEVEL_PS2_CM == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_CM, level, fmt, ##x)
#endif

#define BLOCK_SIZE   (512)

#define CARD_HOME_ARCADE     "MemoryCards/COH"
#define CARD_HOME_PS2        "MemoryCards/PS2"
#define CARD_HOME_PROTO      "MemoryCards/PROT"
#define CARD_HOME_LENGTH    (17)

static int sector_count = -1;

#if WITH_PSRAM
#define SECTOR_COUNT_8MB (PS2_CARD_SIZE_8M / BLOCK_SIZE)
uint8_t available_sectors[SECTOR_COUNT_8MB / 8];  // bitmap
#endif
static uint8_t flushbuf[BLOCK_SIZE];
static int fd = -1;
int current_read_sector = 0, priority_sector = -1;

#define MAX_GAME_NAME_LENGTH (127)
#define MAX_PREFIX_LENGTH    (4)
#define MAX_SLICE_LENGTH     (30 * 1000)

static int card_variant;
static int card_idx;
static int card_chan;
static bool needs_update;
static uint32_t card_size;
static cardman_cb_t cardman_cb;
static char folder_name[MAX_GAME_ID_LENGTH];
static char cardhome[CARD_HOME_LENGTH];
static uint64_t cardprog_start;
static int cardman_sectors_done;
static uint32_t cardprog_pos;

static ps2_cardman_state_t cardman_state;

static enum { CARDMAN_CREATE, CARDMAN_OPEN, CARDMAN_IDLE } cardman_operation;

static bool try_set_boot_card() {
    if (!settings_get_ps2_autoboot())
        return false;

    card_idx = PS2_CARD_IDX_SPECIAL;
    card_chan = settings_get_ps2_boot_channel();
    cardman_state = PS2_CM_STATE_BOOT;
    snprintf(folder_name, sizeof(folder_name), "BOOT");
    return true;
}

static void set_default_card() {
    card_idx = settings_get_ps2_card();
    card_chan = settings_get_ps2_channel();
    cardman_state = PS2_CM_STATE_NORMAL;
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

static bool try_set_game_id_card() {
    if (!settings_get_ps2_game_id())
        return false;

    char parent_id[MAX_GAME_ID_LENGTH] = {};

    (void)game_db_get_current_parent(parent_id);

    if (!parent_id[0])
        return false;

    card_idx = PS2_CARD_IDX_SPECIAL;
    card_chan = CHAN_MIN;
    cardman_state = PS2_CM_STATE_GAMEID;
    snprintf(folder_name, sizeof(folder_name), "%s", parent_id);

    return true;
}

int ps2_cardman_read_sector(int sector, void *buf512) {
    if (fd < 0)
        return -1;

    if (sd_seek_set_new(fd, sector * BLOCK_SIZE) != 0)
        return -1;

    if (sd_read(fd, buf512, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    return 0;
}

static bool try_set_next_named_card() {
    bool ret = false;
    if (cardman_state != PS2_CM_STATE_NAMED) {
        ret = try_set_named_card_folder(cardhome, 0, folder_name, sizeof(folder_name));
        if (ret)
            card_idx = 1;
    } else {
        ret = try_set_named_card_folder(cardhome, card_idx, folder_name, sizeof(folder_name));
        if (ret)
            card_idx++;
    }

    if (ret) {
        card_chan = CHAN_MIN;
        cardman_state = PS2_CM_STATE_NAMED;
    }

    return ret;
}

static bool try_set_prev_named_card() {
    bool ret = false;
    if (card_idx > 1) {
        ret = try_set_named_card_folder(cardhome, card_idx - 2, folder_name, sizeof(folder_name));
        if (ret) {
            card_idx--;
            card_chan = CHAN_MIN;
            cardman_state = PS2_CM_STATE_NAMED;
        }
    }
    return ret;
}

int ps2_cardman_write_sector(int sector, void *buf512) {
    if (fd < 0)
        return -1;

    if (sd_seek_set_new(fd, sector * BLOCK_SIZE) != 0)
        return -1;

    if (sd_write(fd, buf512, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    return 0;
}

bool ps2_cardman_is_sector_available(int sector) {
#if WITH_PSRAM
    return available_sectors[sector / 8] & (1 << (sector % 8));
#else
    return true;
#endif
}

void ps2_cardman_mark_sector_available(int sector) {
#if WITH_PSRAM
    available_sectors[sector / 8] |= (1 << (sector % 8));
#endif
}

void ps2_cardman_set_priority_sector(int sector) {
    priority_sector = sector;
}

void ps2_cardman_flush(void) {
    if (fd >= 0)
        sd_flush(fd);
}

static void ensuredirs(void) {
    char cardpath[32];

    switch (settings_get_ps2_variant()) {
        case PS2_VARIANT_COH:
            snprintf(cardhome, sizeof(cardhome), CARD_HOME_ARCADE);
            break;
        case PS2_VARIANT_PROTO:
            snprintf(cardhome, sizeof(cardhome), CARD_HOME_PROTO);
            break;
        case PS2_VARIANT_RETAIL:
        default:
            snprintf(cardhome, sizeof(cardhome), CARD_HOME_PS2);
            break;
    }

    snprintf(cardpath, sizeof(cardpath), "%s/%s", cardhome, folder_name);

    sd_mkdir("MemoryCards");
    sd_mkdir(cardhome);
    sd_mkdir(cardpath);

    if (!sd_exists("MemoryCards") || !sd_exists(cardhome) || !sd_exists(cardpath))
        fatal("error creating directories");
}
#define CARD_OFFS_SUPERBLOCK (0)
#define CARD_OFFS_IND_FAT_0  (0x4000)
#define CARD_OFFS_IND_FAT_1  (0x4200)
#define CARD_OFFS_IND_FAT_2  (0x4400)
#define CARD_OFFS_IND_FAT_3  (0x4600)
#define CARD_OFFS_FAT_NORMAL (0x4400)
#define CARD_OFFS_FAT_BIG    (0x4800)

uint8_t block0[0xD0] = {
    0x53, 0x6F, 0x6E, 0x79, 0x20, 0x50, 0x53, 0x32, 0x20, 0x4D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x20, 0x43, 0x61, 0x72, 0x64, 0x20, 0x46, 0x6F, 0x72, 0x6D, 0x61,
    0x74, 0x20, 0x31, 0x2E, 0x32, 0x2E, 0x30, 0x2E, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x01, 0x00,
    0x11, 0x01, 0x00, 0x00, 0xDF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x1F, 0x00, 0x00, 0xFE, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

uint8_t blockRoot[1024] = {
    0x27, 0x84, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x06, 0x0C, 0x01, 0xD0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29,
    0x00, 0x06, 0x0C, 0x01, 0xD0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0xA4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x29, 0x00, 0x06, 0x0C, 0x01, 0xD0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x06, 0x0C, 0x01, 0xD0, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2E, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static void genblock(size_t pos, void *vbuf) {
    uint8_t *buf = vbuf;

    uint8_t ind_cnt = 1;

#define CARD_SIZE_MB         (card_size / (1024 * 1024))
#define CARD_CLUST_CNT       (card_size / 1024)
#define CARD_FAT_LENGTH_PAD  (CARD_CLUST_CNT * 4)
#define CARD_SUPERBLOCK_SIZE (16)
#define CARD_IND_FAT_SIZE    (CARD_SIZE_MB * 16)
#define CARD_IFC_SIZE        (CARD_SIZE_MB > 64 ? 2 : 1)
#define CARD_ALLOC_START     ((CARD_FAT_LENGTH_PAD / 1024) + CARD_IFC_SIZE + 16)
#define CARD_ALLOC_CLUSTERS  (CARD_CLUST_CNT - ((CARD_ALLOC_START - 1) + 16))
#define CARD_FAT_LENGTH      (CARD_ALLOC_CLUSTERS * 4)

    // printf("CARD_SIZE_MB: %.08x\n", CARD_SIZE_MB);
    // printf("CARD_CLUST_CNT: %.08x\n", CARD_CLUST_CNT);
    // printf("CARD_FAT_LENGTH_PAD: %.08x\n", CARD_FAT_LENGTH_PAD);
    // printf("CARD_SUPERBLOCK_SIZE: %.08x\n", CARD_SUPERBLOCK_SIZE);
    // printf("CARD_IND_FAT_SIZE: %.08x\n", CARD_IND_FAT_SIZE);
    // printf("CARD_IFC_SIZE: %.08x\n", CARD_IFC_SIZE);
    // printf("CARD_ALLOC_START: %.08x\n", CARD_ALLOC_START);
    // printf("CARD_ALLOC_CLUSTERS: %.08x\n", CARD_ALLOC_CLUSTERS);
    // printf("CARD_FAT_LENGTH: %.08x\n", CARD_FAT_LENGTH);

    switch (CARD_SIZE_MB) {
        case 1:
        case 2:
        case 4:
        case 8:
        case 16:
        case 32: ind_cnt = 1; break;
        case 64: ind_cnt = 2; break;
        case 128: ind_cnt = 4; break;
    }

    memset(buf, 0xFF, PS2_PAGE_SIZE);

    if (pos == CARD_OFFS_SUPERBLOCK) {  // Superblock
        // 0x30: Clusters Total (2 Bytes): card_size / 1024
        // 0x34: Alloc start: 0x49
        // 0x38: Alloc end: ((((card_size / 8) / 1024) - 2) * 8) - 41
        // 0x40: BBlock 1 - ((card_size / 8) / 1024) - 1
        // 0x44: BBlock 2 - ((card_size / 8) / 1024) - 2
        memset(buf, 0x00, 0xD0);
        memcpy(buf, block0, sizeof(block0));
        memset(&buf[0x150], 0x00, 0x2C);
        (*(uint32_t *)&buf[0x30]) = (uint32_t)(CARD_CLUST_CNT);                // Total clusters
        (*(uint32_t *)&buf[0x34]) = CARD_ALLOC_START;                          // Alloc Start
        (*(uint32_t *)&buf[0x38]) = (uint32_t)(CARD_ALLOC_CLUSTERS - 1);       // Alloc End
        (*(uint32_t *)&buf[0x40]) = (uint32_t)(((card_size / 8) / 1024) - 1);  // BB1
        (*(uint32_t *)&buf[0x44]) = (uint32_t)(((card_size / 8) / 1024) - 2);  // BB2
        buf[0x150] = 0x02;                                                     // Card Type
        buf[0x151] = 0x2B;                                                     // Card Features
        buf[0x152] = 0x00;                                                     // Card Features
        (*(uint32_t *)&buf[0x154]) = (uint32_t)(2 * PS2_PAGE_SIZE);            // ClusterSize
        (*(uint32_t *)&buf[0x158]) = (uint32_t)(256);                          // FAT Entries per Cluster
        (*(uint32_t *)&buf[0x15C]) = (uint32_t)(8);                            // Clusters per Block
        (*(uint32_t *)&buf[0x160]) = (uint32_t)(0xFFFFFFFF);                   // CardForm
        // Note: for whatever weird reason, the max alloc cluster cnt needs to be calculated this way.
        (*(uint32_t *)&buf[0x170]) = (uint32_t)(((CARD_CLUST_CNT/1000) * 1000) + 1); // Max Alloc Cluster


    } else if (pos == CARD_OFFS_IND_FAT_0) {
        // Indirect FAT
        uint8_t byte = 0x11;
        int32_t count = CARD_IND_FAT_SIZE % PS2_PAGE_SIZE;
        if (count == 0)
            count = PS2_PAGE_SIZE;
        for (int i = 0; i < count; i++) {
            if (i % 4 == 0) {
                buf[i] = byte++;
            } else {
                buf[i] = 0;
            }
        }
    } else if ((pos == CARD_OFFS_IND_FAT_1) && (ind_cnt >= 2)) {
        uint32_t entry = 0x91;
        for (int i = 0; i < PS2_PAGE_SIZE; i += 4) {
            *(uint32_t *)(&buf[i]) = entry;
            entry++;
        }
    } else if (pos >= CARD_OFFS_FAT_NORMAL && pos < CARD_OFFS_FAT_NORMAL + CARD_FAT_LENGTH) {
        const uint32_t val = 0x7FFFFFFF;
        size_t i = 0;
        // FAT Table
        if (pos == CARD_OFFS_FAT_NORMAL) {  // First cluster is used for root dir
            i = 4;
        }
        for (; i < PS2_PAGE_SIZE; i += 4) {
            if (pos + i < (CARD_OFFS_FAT_NORMAL + CARD_FAT_LENGTH) - 4) {  // -4 because last fat entry is FFFFFFFF
                memcpy(&buf[i], &val, sizeof(val));
            } else {
                break;
            }
        }

    } else if (pos == (CARD_ALLOC_START * 1024)) {
        memcpy(buf, blockRoot, PS2_PAGE_SIZE);
    } else if (pos == (CARD_ALLOC_START * 1024) + PS2_PAGE_SIZE) {
        memcpy(buf, &blockRoot[PS2_PAGE_SIZE], PS2_PAGE_SIZE);
    }
}

static int next_sector_to_load() {
    if (priority_sector != -1) {
        if (ps2_cardman_is_sector_available(priority_sector))
            priority_sector = -1;
        else
            return priority_sector;
    }

    while (current_read_sector < sector_count) {
        if (!ps2_cardman_is_sector_available(current_read_sector))
            return current_read_sector++;
        else
            current_read_sector++;
    }

    return -1;
}

static void ps2_cardman_continue(void) {
    if (cardman_operation == CARDMAN_OPEN) {
        uint64_t slice_start = time_us_64();
        if (settings_get_sd_mode() || card_size > PS2_CARD_SIZE_8M) {
            uint64_t end = time_us_64();
            log(LOG_INFO, "took = %.2f s; SD read speed = %.2f kB/s\n", (end - cardprog_start) / 1e6, 1000000.0 * card_size / (end - cardprog_start) / 1024);
            if (cardman_cb)
                cardman_cb(100, true);
            cardman_operation = CARDMAN_IDLE;
        } else {
#if WITH_PSRAM
            log(LOG_TRACE, "%s:%u\n", __func__, __LINE__);
            while ((ps2_mmce_fs_idle()) && (time_us_64() - slice_start < MAX_SLICE_LENGTH)) {
                log(LOG_TRACE, "Slice!\n");

                ps2_dirty_lock();
                int sector_idx = next_sector_to_load();
                if (sector_idx == -1) {
                    ps2_dirty_unlock();
                    cardman_operation = CARDMAN_IDLE;
                    uint64_t end = time_us_64();
                    log(LOG_INFO, "took = %.2f s; SD read speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
                        1000000.0 * card_size / (end - cardprog_start) / 1024);
                    if (cardman_cb)
                        cardman_cb(100, true);
                    break;
                }

                size_t pos = sector_idx * BLOCK_SIZE;
                if (sd_seek_new(fd, pos, 0) != 0)
                    fatal("cannot read memcard\nseek");

                if (sd_read(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                    fatal("cannot read memcard\nread %u", pos);

                log(LOG_TRACE, "Writing pos %u\n", pos);
                psram_write_dma(pos, flushbuf, BLOCK_SIZE, NULL);

                psram_wait_for_dma();

                ps2_cardman_mark_sector_available(sector_idx);
                ps2_dirty_unlock();

                cardprog_pos = cardman_sectors_done * BLOCK_SIZE;

                if (cardman_cb)
                    cardman_cb(100U * (uint64_t)cardprog_pos / (uint64_t)card_size, false);

                cardman_sectors_done++;
            }
            log(LOG_TRACE, "%s:%u\n", __func__, __LINE__);

#endif
        }
    } else if (cardman_operation == CARDMAN_CREATE) {
        uint64_t slice_start = time_us_64();
        while ((ps2_mmce_fs_idle()) && (time_us_64() - slice_start < MAX_SLICE_LENGTH)) {
            cardprog_pos = cardman_sectors_done * BLOCK_SIZE;
            if (cardprog_pos >= card_size) {
                sd_flush(fd);
                log(LOG_INFO, "OK!\n");

                cardman_operation = CARDMAN_IDLE;
                uint64_t end = time_us_64();

                log(LOG_INFO, "took = %.2f s; SD write speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
                    1000000.0 * card_size / (end - cardprog_start) / 1024);
                if (cardman_cb)
                    cardman_cb(100, true);

                break;
            }
            if (settings_get_sd_mode() || (settings_get_ps2_cardsize() > 8)) {
                genblock(cardprog_pos, flushbuf);
                sd_write(fd, flushbuf, BLOCK_SIZE);
            } else {
#if WITH_PSRAM
                ps2_dirty_lock();
                psram_wait_for_dma();

                // read back from PSRAM to make sure to retain already rewritten sectors, if any
                psram_read_dma(cardprog_pos, flushbuf, BLOCK_SIZE, NULL);
                psram_wait_for_dma();

                if (sd_write(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                    fatal("cannot init memcard");

                ps2_dirty_unlock();
#endif
            }

            if (cardman_cb)
                cardman_cb(100U * (uint64_t)cardprog_pos / (uint64_t)card_size, cardman_operation == CARDMAN_IDLE);

            cardman_sectors_done++;
        }

    } else if (cardman_cb) {
        cardman_cb(100, true);
    }
}

void ps2_cardman_open(void) {
    char path[64];

    needs_update = false;

    sd_init();
    ensuredirs();

    switch (cardman_state) {
        case PS2_CM_STATE_BOOT:
            if (card_chan == 1) {
                snprintf(path, sizeof(path), "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
                if (!sd_exists(path)) {
                    // before boot card channels, boot card was located at BOOT/BootCard.mcd, for backwards compatibility check if it exists
                    snprintf(path, sizeof(path), "%s/%s/BootCard.mcd", cardhome, folder_name);
                }
                if (!sd_exists(path)) {
                    // go back to BootCard-1.mcd if it doesn't
                    snprintf(path, sizeof(path), "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
                }
            } else {
                snprintf(path, sizeof(path), "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
            }

            settings_set_ps2_boot_channel(card_chan);
            break;
        case PS2_CM_STATE_NAMED:
        case PS2_CM_STATE_GAMEID: snprintf(path, sizeof(path), "%s/%s/%s-%d.mcd", cardhome, folder_name, folder_name, card_chan); break;
        case PS2_CM_STATE_NORMAL:
            snprintf(path, sizeof(path), "%s/%s/%s-%d.mcd", cardhome, folder_name, folder_name, card_chan);

            /* this is ok to do on every boot because it wouldn't update if the value is the same as currently stored */
            settings_set_ps2_card(card_idx);
            settings_set_ps2_channel(card_chan);
            break;
    }

    log(LOG_INFO, "Switching to card path = %s\n", path);
    ps2_mc_data_interface_card_changed();

    if (!sd_exists(path)) {
        card_size = settings_get_ps2_cardsize() * 1024 * 1024;
        cardman_operation = CARDMAN_CREATE;
        fd = sd_open(path, O_RDWR | O_CREAT | O_TRUNC);
        cardman_sectors_done = 0;
        cardprog_pos = 0;
        if (card_size > PS2_CARD_SIZE_8M) {
            ps2_mc_data_interface_set_sdmode(true);
        } else {
            ps2_mc_data_interface_set_sdmode(settings_get_sd_mode());
        }

        if (fd < 0)
            fatal("cannot open for creating new card");

        log(LOG_INFO, "create new image at %s... ", path);

        if (cardman_cb)
            cardman_cb(0, false);
#if WITH_PSRAM
        if (card_size <= PS2_CARD_SIZE_8M) {
            // quickly generate and write an empty card into PSRAM so that it's immediately available, takes about ~0.6s
            for (size_t pos = 0; pos < card_size; pos += BLOCK_SIZE) {
                if (card_size == PS2_CARD_SIZE_8M)
                    genblock(pos, flushbuf);
                else
                    memset(flushbuf, 0xFF, BLOCK_SIZE);

                ps2_dirty_lock();
                psram_write_dma(pos, flushbuf, BLOCK_SIZE, NULL);
                psram_wait_for_dma();
                ps2_cardman_mark_sector_available(pos / BLOCK_SIZE);
                ps2_dirty_unlock();
            }
            log(LOG_TRACE, "%s created empty PSRAM image... \n", __func__);
        }
#endif
        cardprog_start = time_us_64();

    } else {
        fd = sd_open(path, O_RDWR);
        card_size = sd_filesize(fd);
        cardman_operation = CARDMAN_OPEN;
        cardprog_pos = 0;
        cardman_sectors_done = 0;

        if (fd < 0)
            fatal("cannot open card");

        switch (card_size) {
            case PS2_CARD_SIZE_512K:
            case PS2_CARD_SIZE_1M:
            case PS2_CARD_SIZE_2M:
            case PS2_CARD_SIZE_4M:
            case PS2_CARD_SIZE_8M: ps2_mc_data_interface_set_sdmode(settings_get_sd_mode()); break;
            case PS2_CARD_SIZE_16M:
            case PS2_CARD_SIZE_32M:
            case PS2_CARD_SIZE_64M: ps2_mc_data_interface_set_sdmode(true); break;
            default: fatal("Card %d Channel %d is corrupted", card_idx, card_chan); break;
        }

        /* read 8 megs of card image */
        log(LOG_INFO, "reading card (%lu KB).... ", (uint32_t)(card_size / 1024));
        cardprog_start = time_us_64();
        if (cardman_cb)
            cardman_cb(0, false);
    }

    sector_count = card_size / BLOCK_SIZE;

    log(LOG_INFO, "Open Finished!\n");
}

void ps2_cardman_close(void) {
    if (fd < 0)
        return;
    ps2_cardman_flush();
    sd_close(fd);
    fd = -1;
    current_read_sector = 0;
    priority_sector = -1;
#if WITH_PSRAM
    memset(available_sectors, 0, sizeof(available_sectors));
#endif
}

void ps2_cardman_set_channel(uint16_t chan_num) {
    if (chan_num != card_chan)
        needs_update = true;
    if (chan_num <= CHAN_MAX && chan_num >= CHAN_MIN) {
        card_chan = chan_num;
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_next_channel(void) {
    card_chan += 1;
    if (card_chan > CHAN_MAX)
        card_chan = CHAN_MIN;
    needs_update = true;
}

void ps2_cardman_prev_channel(void) {
    card_chan -= 1;
    if (card_chan < CHAN_MIN)
        card_chan = CHAN_MAX;
    needs_update = true;
}

void ps2_cardman_set_idx(uint16_t idx_num) {
    if (idx_num != card_idx)
        needs_update = true;
    if ((idx_num >= IDX_MIN) && (idx_num < UINT16_MAX)) {
        card_idx = idx_num;
        card_chan = CHAN_MIN;
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_next_idx(void) {
    switch (cardman_state) {
        case PS2_CM_STATE_NAMED:
            if (!try_set_prev_named_card() && !try_set_boot_card() && !try_set_game_id_card())
                set_default_card();
            break;
        case PS2_CM_STATE_BOOT:
            if (!try_set_game_id_card())
                set_default_card();
            break;
        case PS2_CM_STATE_GAMEID: set_default_card(); break;
        case PS2_CM_STATE_NORMAL:
            card_idx += 1;
            card_chan = CHAN_MIN;
            if (card_idx > UINT16_MAX)
                card_idx = UINT16_MAX;
            snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
            break;
    }

    needs_update = true;
}

void ps2_cardman_prev_idx(void) {
    switch (cardman_state) {
        case PS2_CM_STATE_NAMED:
        case PS2_CM_STATE_BOOT:
            if (!try_set_next_named_card())
                set_default_card();
            break;
        case PS2_CM_STATE_GAMEID:
            if (!try_set_boot_card())
                if (!try_set_next_named_card())
                    set_default_card();
            break;
        case PS2_CM_STATE_NORMAL:
            card_idx -= 1;
            card_chan = CHAN_MIN;
            if (card_idx <= PS2_CARD_IDX_SPECIAL) {
                if (!try_set_game_id_card() && !try_set_boot_card() && !try_set_next_named_card())
                    set_default_card();
            } else {
                snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
            }
            break;
    }

    needs_update = true;
}

int ps2_cardman_get_idx(void) {
    return card_idx;
}

int ps2_cardman_get_channel(void) {
    return card_chan;
}

void ps2_cardman_set_gameid(const char *const card_game_id) {
    if (!settings_get_ps2_game_id())
        return;

    char new_folder_name[MAX_GAME_ID_LENGTH];
    if (card_game_id[0]) {
        snprintf(new_folder_name, sizeof(new_folder_name), "%s", card_game_id);
        if ((strcmp(new_folder_name, folder_name) != 0) || (PS2_CM_STATE_GAMEID != cardman_state)) {
            card_idx = PS2_CARD_IDX_SPECIAL;
            cardman_state = PS2_CM_STATE_GAMEID;
            card_chan = CHAN_MIN;
            snprintf(folder_name, sizeof(folder_name), "%s", card_game_id);
            needs_update = true;
        }
    }
}

void ps2_cardman_set_progress_cb(cardman_cb_t func) {
    cardman_cb = func;
}

char *ps2_cardman_get_progress_text(void) {
    static char progress[32];

    if (cardman_operation != CARDMAN_IDLE)
        snprintf(progress, sizeof(progress), "%s %.2f kB/s", cardman_operation == CARDMAN_CREATE ? "Wr" : "Rd",
                 1000000.0 * cardprog_pos / (time_us_64() - cardprog_start) / 1024);
    else
        snprintf(progress, sizeof(progress), "Switching...");

    return progress;
}

uint32_t ps2_cardman_get_card_size(void) {
    return card_size;
}

const char *ps2_cardman_get_folder_name(void) {
    return folder_name;
}

ps2_cardman_state_t ps2_cardman_get_state(void) {
    return cardman_state;
}

void ps2_cardman_force_update(void) {
    needs_update = true;
}

void ps2_cardman_set_variant(int variant) {
    if (variant != card_variant) {
        settings_set_ps2_variant(variant);
        if (!try_set_boot_card()) {
            card_idx = 1;
            card_chan = 1;
            cardman_state = PS2_CM_STATE_NORMAL;
            snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
        }
        card_variant = variant;
    }
    needs_update = true;
}

bool ps2_cardman_needs_update(void) {
    return needs_update;
}

bool __time_critical_func(ps2_cardman_is_accessible)(void) {
    // SD: X IDLE   => X
    // SD: X CREATE => /
    // SD: X OPEN =>   /
    // SD: / IDLE   => X
    // SD: / CREATE => X
    // SD: / OPEN   => X
    if ((card_size > PS2_CARD_SIZE_8M) || (settings_get_sd_mode()))
        return (cardman_operation == CARDMAN_IDLE);
    else
        return true;
}

bool ps2_cardman_is_idle(void) {
    return cardman_operation == CARDMAN_IDLE;
}

void ps2_cardman_init(void) {
    if (!try_set_boot_card())
        set_default_card();

    cardman_operation = CARDMAN_IDLE;
}

void ps2_cardman_task(void) {
    ps2_cardman_continue();
}
