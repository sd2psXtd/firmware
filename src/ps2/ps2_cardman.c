#include "ps2_cardman.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "mmceman/ps2_mmceman.h"
#include "debug.h"
#include "game_db/game_db.h"
#include "hardware/timer.h"
#include "mmceman/ps2_mmceman_fs.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#if WITH_PSRAM
    #include "ps2_dirty.h"
    #include "psram/psram.h"
#endif
#include "sd.h"
#include "settings.h"
#include "util.h"
#include "card_config.h"

#if LOG_LEVEL_PS2_CM == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_CM, level, fmt, ##x)
#endif

#define CARD_HOME_ARCADE     "MemoryCards/COH"
#define CARD_HOME_CONQUEST   "MemoryCards/SC2"
#define CARD_HOME_PS2        "MemoryCards/PS2"
#define CARD_HOME_PROTO      "MemoryCards/PROT"
#define CARD_HOME_LENGTH    (17)

static int sector_count = -1;

#if WITH_PSRAM
#define SECTOR_COUNT_8MB (PS2_CARD_SIZE_8M / PS2_PAGE_SIZE)
uint8_t available_sectors[SECTOR_COUNT_8MB / 8];  // bitmap
#define PSRAM_AVAILABLE true
#else
#define PSRAM_AVAILABLE false
#endif
static uint8_t flushbuf[PS2_PAGE_SIZE];
int cardman_fd = -1;

int current_read_sector = 0, priority_sector = -1;

#define MAX_GAME_NAME_LENGTH (127)
#define MAX_PREFIX_LENGTH    (4)
#define MAX_PATH_LENGTH      (256)

#define MAX_SLICE_LENGTH     (30 * 1000)


static int card_variant;
static int card_idx;
static int card_chan;
static bool needs_update;
static uint32_t card_size;
static cardman_cb_t cardman_cb;
static char folder_name[MAX_FOLDER_NAME_LENGTH];
static char cardhome[CARD_HOME_LENGTH];
static uint64_t cardprog_start;
static int cardman_sectors_done;
static uint32_t cardprog_pos;

static ps2_cardman_state_t cardman_state;

static enum { CARDMAN_CREATE, CARDMAN_OPEN, CARDMAN_IDLE, CARDMAN_UNKNOWN } cardman_operation = CARDMAN_UNKNOWN;

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
    uint8_t max_chan = card_config_get_max_channels(folder_name, folder_name);
    if (card_chan > max_chan)
        card_chan = max_chan;
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
    card_config_get_card_folder(parent_id, folder_name, sizeof(folder_name));
    if (folder_name[0] == 0x00)
        snprintf(folder_name, sizeof(folder_name), "%s", parent_id);

    return true;
}

int ps2_cardman_read_sector(int sector, void *buf512) {
    if (cardman_fd < 0)
        return -1;

    if (sd_seek(cardman_fd, sector * PS2_PAGE_SIZE, SEEK_SET) != 0)
        return -1;

    if (sd_read(cardman_fd, buf512, PS2_PAGE_SIZE) != PS2_PAGE_SIZE)
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
    if (cardman_fd < 0)
        return -1;

    if (sd_seek(cardman_fd, sector * PS2_PAGE_SIZE, SEEK_SET) != 0)
        return -1;

    if (sd_write(cardman_fd, buf512, PS2_PAGE_SIZE) != PS2_PAGE_SIZE)
        return -1;

    return 0;
}

int ps2_cardman_write_sectors(void *buffer, int sectors_count, int first_sector) {
    if (cardman_fd < 0)
        return -1;

    if (sd_seek(cardman_fd, first_sector * PS2_PAGE_SIZE, SEEK_SET) != 0)
        return -1;

    if (sd_write(cardman_fd, buffer, sectors_count * PS2_PAGE_SIZE) != sectors_count * PS2_PAGE_SIZE)
        return -1;

    return 0;
}

int ps2_cardman_write_isolated_sector(void *buffer, int sector) {
    if (cardman_fd < 0)
        return -1;

    if (sd_seek(cardman_fd, sector * PS2_PAGE_SIZE, SEEK_SET) != 0)
        return -1;

    if (sd_write_isolated(cardman_fd, buffer) != PS2_PAGE_SIZE)
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
    if (cardman_fd >= 0)
        sd_flush(cardman_fd);
}

static void ensuredirs(void) {
    char cardpath[CARD_HOME_LENGTH + MAX_FOLDER_NAME_LENGTH + 2];

    switch (settings_get_ps2_variant()) {
        case PS2_VARIANT_COH:
            snprintf(cardhome, sizeof(cardhome), CARD_HOME_ARCADE);
            break;
        case PS2_VARIANT_SC2:
            snprintf(cardhome, sizeof(cardhome), CARD_HOME_CONQUEST);
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
        fatal(ERR_CARDMAN, "error creating directories");
}


static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}


#define CARD_CLUSTER_SIZE       (1024)
#define CARD_CLUSTERS_PER_BLOCK (8)
#define CARD_RESERVED_CLUSTERS  (16)

#define CARD_OFFS_SUPERBLOCK (0)
#define CARD_OFFS_IND_FAT_0  (0x4000)
#define CARD_OFFS_IND_FAT_1  (0x4200)
#define CARD_OFFS_IND_FAT_2  (0x4400)
#define CARD_OFFS_IND_FAT_3  (0x4600)
#define CARD_OFFS_IND_FAT(X) (0x4000 + (X) * 0x400)
#define CARD_OFFS_FAT_NORMAL (0x4400)
#define CARD_OFFS_FAT(X)     (CARD_OFFS_IND_FAT(X))
#define CARD_OFFS_FAT_BIG    (0x4800)

#define CARD_SIZE_MB         (card_size / (1024 * 1024))
#define CARD_CLUST_CNT       (card_size / CARD_CLUSTER_SIZE)
#define CARD_FAT_LENGTH_PAD  (CARD_CLUST_CNT * 4)
#define CARD_IND_FAT_SIZE    (CARD_SIZE_MB * 16)
#define CARD_FAT_LENGTH      ((CARD_CLUST_CNT * 4 + 1023) / 1024)
#define CARD_IFC_LENGTH      ((CARD_FAT_LENGTH * 4 + 1023) / 1024)
#define CARD_ALLOC_START     (CARD_RESERVED_CLUSTERS + CARD_IFC_LENGTH + CARD_FAT_LENGTH)
#define CARD_BACKUP_BLOCK1   ((CARD_CLUST_CNT / CARD_CLUSTERS_PER_BLOCK) - 1)
#define CARD_BACKUP_BLOCK2   (CARD_BACKUP_BLOCK1 - 1)
#define CARD_ALLOC_END       ((CARD_BACKUP_BLOCK2 * CARD_CLUSTERS_PER_BLOCK) - CARD_ALLOC_START)

static void build_superblock(uint8_t *buf) {
    static const char magic[] = "Sony PS2 Memory Card Format ";
    static const char version[12] = "1.2.0.0";

    memset(buf, 0xFF, PS2_PAGE_SIZE);
    memset(buf, 0x00, 0xD0);

    memcpy(&buf[0x000], magic, sizeof(magic) - 1);
    memcpy(&buf[0x01C], version, sizeof(version));
    wr16(&buf[0x028], PS2_PAGE_SIZE);                 // Page size
    wr16(&buf[0x02A], 2);                             // Pages per cluster
    wr16(&buf[0x02C], 16);                            // Pages per erase block
    wr16(&buf[0x02E], 0xFF00);                        // Unused
    wr32(&buf[0x030], (uint32_t)CARD_CLUST_CNT);      // Total clusters
    wr32(&buf[0x034], (uint32_t)CARD_ALLOC_START);    // Alloc offset
    wr32(&buf[0x038], (uint32_t)CARD_ALLOC_END);      // Alloc end
    wr32(&buf[0x03C], 0);                             // Root dir cluster
    wr32(&buf[0x040], (uint32_t)CARD_BACKUP_BLOCK1);  // Backup block 1
    wr32(&buf[0x044], (uint32_t)CARD_BACKUP_BLOCK2);  // Backup block 2

    for (int i = 0; i < CARD_IFC_LENGTH; i++) {
        wr32(&buf[0x050 + i * 4], (uint32_t)(CARD_RESERVED_CLUSTERS + i));
    }

    memset(&buf[0x150], 0x00, 0x2C);
    buf[0x150] = 0x02;                                // Card type
    buf[0x151] = 0x2B;                                // Card features
    wr32(&buf[0x154], CARD_CLUSTER_SIZE);             // Cluster size
    wr32(&buf[0x158], 256);                           // FAT entries per cluster
    wr32(&buf[0x15C], CARD_CLUSTERS_PER_BLOCK);       // Clusters per block
    wr32(&buf[0x160], 0xFFFFFFFFu);                   // Card form
    // Note: for whatever weird reason, the max alloc cluster cnt needs to be calculated this way.
    wr32(&buf[0x170], (uint32_t)(((CARD_CLUST_CNT / 1000) * 1000) + 1));
}

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

#define CURR_BLOCK           (pos / PS2_PAGE_SIZE)

    ind_cnt = CARD_IFC_LENGTH;

    memset(buf, 0xFF, PS2_PAGE_SIZE);

    //printf("pos: %zu, CURR_BLOCK: %d, ind_cnt: %d\n", pos, CURR_BLOCK, ind_cnt);

    if (pos == CARD_OFFS_SUPERBLOCK) {  // Superblock
        build_superblock(buf);


    } else if ((pos >= CARD_OFFS_IND_FAT_0) && (pos < (size_t)CARD_OFFS_IND_FAT(ind_cnt))) {

        // Indirect FAT: IFC entry j stores absolute cluster of FAT cluster j,
        // which is (reserved_clusters + ifc_length + j). Each IFC page holds 128 entries.
        uint32_t page_in_ifc = (uint32_t)((pos - CARD_OFFS_IND_FAT_0) / PS2_PAGE_SIZE);
        uint32_t base_j       = page_in_ifc * 128;
        uint32_t first_fat_cl = (uint32_t)(CARD_RESERVED_CLUSTERS + ind_cnt);
        int32_t  n            = (int32_t)CARD_FAT_LENGTH - (int32_t)base_j;
        if (n > 128) n = 128;
        if (n < 0)   n = 0;
        for (int k = 0; k < n; k++) {
            wr32(&buf[k * 4], first_fat_cl + base_j + (uint32_t)k);
        }
    } else if (pos >= (size_t)CARD_OFFS_FAT(ind_cnt) &&
               pos <  (size_t)CARD_OFFS_FAT(ind_cnt) + (size_t)CARD_FAT_LENGTH * 1024) {
        // FAT region: pages of 128 uint32 entries each, indexed by relative cluster.
        //   rel == 0                    -> 0xFFFFFFFF  (root dir end-of-chain)
        //   1 <= rel < alloc_end        -> 0x7FFFFFFF  (free)
        //   rel >= alloc_end            -> leave 0xFFFFFFFF (reserved/out-of-range)
        const uint32_t free_val = 0x7FFFFFFFu;
        const uint32_t end_val  = 0xFFFFFFFFu;
        uint32_t page_in_fat = (uint32_t)((pos - (size_t)CARD_OFFS_FAT(ind_cnt)) / PS2_PAGE_SIZE);
        uint32_t base_entry  = page_in_fat * 128;
        for (uint32_t k = 0; k < 128; k++) {
            uint32_t rel = base_entry + k;
            if (rel == 0) {
                wr32(&buf[k * 4], end_val);
            } else if (rel < (uint32_t)CARD_ALLOC_END) {
                wr32(&buf[k * 4], free_val);
            }
            // else: leave memset 0xFFFFFFFF
        }

    } else if (pos == (CARD_ALLOC_START * CARD_CLUSTER_SIZE)) {
        memcpy(buf, blockRoot, PS2_PAGE_SIZE);
    } else if (pos == (CARD_ALLOC_START * CARD_CLUSTER_SIZE) + PS2_PAGE_SIZE) {
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

        if (ps2_mc_data_interface_get_sdmode()) {
            uint64_t end = time_us_64();
            log(LOG_INFO, "took = %.2f s; SD read speed = %.2f kB/s\n", (end - cardprog_start) / 1e6, 1000000.0 * card_size / (end - cardprog_start) / 1024);
            if (cardman_cb)
                cardman_cb(100, true);
            cardman_operation = CARDMAN_IDLE;
        } else {
#if WITH_PSRAM
            uint8_t thisIter = 0;
            log(LOG_TRACE, "%s:%u\n", __func__, __LINE__);
            while ((ps2_mmceman_fs_idle()) && (time_us_64() - slice_start < MAX_SLICE_LENGTH)) {
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

                size_t pos = sector_idx * PS2_PAGE_SIZE;
                if (sd_seek(cardman_fd, pos, 0) != 0)
                    fatal(ERR_CARDMAN, "cannot read memcard\nseek");

                if (sd_read(cardman_fd, flushbuf, PS2_PAGE_SIZE) != PS2_PAGE_SIZE)
                    fatal(ERR_CARDMAN, "cannot read memcard\nread %u", pos);

                log(LOG_TRACE, "Writing pos %u\n", pos);
                psram_write_dma(pos, flushbuf, PS2_PAGE_SIZE, NULL);

                psram_wait_for_dma();

                ps2_cardman_mark_sector_available(sector_idx);
                ps2_dirty_unlock();

                cardprog_pos = cardman_sectors_done * PS2_PAGE_SIZE;

                if (cardman_cb)
                    cardman_cb(100U * (uint64_t)cardprog_pos / (uint64_t)card_size, false);

                cardman_sectors_done++;
                thisIter++;
            }
            log(LOG_INFO, "ps2_cardman_continue: thisIter = %u\n", thisIter);
            log(LOG_TRACE, "%s:%u\n", __func__, __LINE__);

#endif
        }
    } else if (cardman_operation == CARDMAN_CREATE) {
        uint64_t slice_start = time_us_64();
        while ((ps2_mmceman_fs_idle()) && (time_us_64() - slice_start < MAX_SLICE_LENGTH)) {
            cardprog_pos = cardman_sectors_done * PS2_PAGE_SIZE;
            if (cardprog_pos >= card_size) {
                sd_flush(cardman_fd);
                log(LOG_INFO, "OK!\n");

                cardman_operation = CARDMAN_IDLE;
                uint64_t end = time_us_64();

                log(LOG_INFO, "took = %.2f s; SD write speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
                    1000000.0 * card_size / (end - cardprog_start) / 1024);
                if (cardman_cb)
                    cardman_cb(100, true);

                break;
            }
            if (ps2_mc_data_interface_get_sdmode()) {
                genblock(cardprog_pos, flushbuf);
                sd_write(cardman_fd, flushbuf, PS2_PAGE_SIZE);
            } else {
#if WITH_PSRAM
                ps2_dirty_lock();
                psram_wait_for_dma();

                // read back from PSRAM to make sure to retain already rewritten sectors, if any
                psram_read_dma(cardprog_pos, flushbuf, PS2_PAGE_SIZE, NULL);
                psram_wait_for_dma();

                if (sd_write(cardman_fd, flushbuf, PS2_PAGE_SIZE) != PS2_PAGE_SIZE)
                    fatal(ERR_CARDMAN, "cannot init memcard");

                ps2_dirty_unlock();
#endif
            }

            if (cardman_cb)
                cardman_cb(100U * (uint64_t)cardprog_pos / (uint64_t)card_size, cardman_operation == CARDMAN_IDLE);

            cardman_sectors_done++;
        }
        sd_flush(cardman_fd);

    } else if (cardman_cb) {
        cardman_cb(100, true);
    }
}

static bool ps2_check_cardsize(uint32_t filesize) {
    switch (filesize) {
        case PS2_CARD_SIZE_512K:
        case PS2_CARD_SIZE_1M:
        case PS2_CARD_SIZE_2M:
        case PS2_CARD_SIZE_4M:
        case PS2_CARD_SIZE_8M:
        case PS2_CARD_SIZE_16M:
        case PS2_CARD_SIZE_32M:
        case PS2_CARD_SIZE_64M:
        case PS2_CARD_SIZE_128M:
        case PS2_CARD_SIZE_256M:
        case PS2_CARD_SIZE_512M:
        case PS2_CARD_SIZE_1G:
        case PS2_CARD_SIZE_2G: return true;
        default: return false;
    }
}

#if WITH_PSRAM
static void ps2_cardman_initializePSRAMCard(void) {
        // quickly generate and write an empty card into PSRAM so that it's immediately available, takes about ~0.6s
    for (size_t pos = 0; pos < card_size; pos += PS2_PAGE_SIZE) {
        if (card_size == PS2_CARD_SIZE_8M)
            genblock(pos, flushbuf);
        else
            memset(flushbuf, 0xFF, PS2_PAGE_SIZE);

        ps2_dirty_lock();
        psram_write_dma(pos, flushbuf, PS2_PAGE_SIZE, NULL);
        psram_wait_for_dma();
        ps2_cardman_mark_sector_available(pos / PS2_PAGE_SIZE);
        ps2_dirty_unlock();
    }
    log(LOG_TRACE, "%s created empty PSRAM image... \n", __func__);
}
#endif

static void ps2_cardman_createCard(char* path) {
    card_size = card_config_get_ps2_cardsize(folder_name, (cardman_state == PS2_CM_STATE_BOOT) ? "BootCard" : folder_name) * 1024 * 1024;
    if (card_size == 0U) {
        card_size = settings_get_ps2_cardsize() * 1024 * 1024;
    }
    cardman_fd = sd_open(path, O_RDWR | O_CREAT | O_TRUNC);
    cardman_sectors_done = 0;
    cardprog_pos = 0;
    if (card_size > PS2_CARD_SIZE_8M) {
        ps2_mc_data_interface_set_sdmode(true);
    } else {
        ps2_mc_data_interface_set_sdmode(!PSRAM_AVAILABLE);
    }

    if (cardman_fd < 0)
        fatal(ERR_CARDMAN, "cannot open for creating new card");

    log(LOG_INFO, "create new image at %s... ", path);

    if (cardman_cb)
        cardman_cb(0, false);
#if WITH_PSRAM
    if (card_size <= PS2_CARD_SIZE_8M) {
        ps2_cardman_initializePSRAMCard();
    }
#endif
}

static void ps2_cardman_resolveCardPath(char* path) {
    switch (cardman_state) {
        case PS2_CM_STATE_BOOT:
            if (card_chan == 1) {
                snprintf(path, MAX_PATH_LENGTH, "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
                if (!sd_exists(path)) {
                    // before boot card channels, boot card was located at BOOT/BootCard.mcd, for backwards compatibility check if it exists
                    snprintf(path, MAX_PATH_LENGTH, "%s/%s/BootCard.mcd", cardhome, folder_name);
                }
                if (!sd_exists(path)) {
                    // go back to BootCard-1.mcd if it doesn't
                    snprintf(path, MAX_PATH_LENGTH, "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
                }
            } else {
                snprintf(path, MAX_PATH_LENGTH, "%s/%s/BootCard-%d.mcd", cardhome, folder_name, card_chan);
            }

            settings_set_ps2_boot_channel(card_chan);
            break;
        case PS2_CM_STATE_NAMED:
        case PS2_CM_STATE_GAMEID: snprintf(path, MAX_PATH_LENGTH, "%s/%s/%s-%d.mcd", cardhome, folder_name, folder_name, card_chan); break;
        case PS2_CM_STATE_NORMAL:
            snprintf(path, MAX_PATH_LENGTH, "%s/%s/%s-%d.mcd", cardhome, folder_name, folder_name, card_chan);

            /* this is ok to do on every boot because it wouldn't update if the value is the same as currently stored */
            settings_set_ps2_card(card_idx);
            settings_set_ps2_channel(card_chan);
            break;
    }
}

static void ps2_cardman_move_card_crp(char* path) {
    char crp_path[MAX_PATH_LENGTH] = {};
    for (int i = 0; i < 512; i++) {
        snprintf(crp_path, sizeof(crp_path), "%s/%s/%s-%d.crp.%i", cardhome, folder_name, folder_name, card_chan, i);
        if (!sd_exists(crp_path))
            break;
    }
    if (!crp_path[0])
        fatal(ERR_CARDMAN, "Card %d Chann %d invalid size and cannot create .crp file", card_idx, card_chan);
    sd_close(cardman_fd);
    sd_rename(path, crp_path);
    log(LOG_ERROR, "Card %d Chann %d has invalid size and was renamed to %s\n", card_idx, card_chan, crp_path);
}

void ps2_cardman_open(void) {
    char path[MAX_PATH_LENGTH];

    needs_update = false;

    ensuredirs();

    ps2_cardman_resolveCardPath(path);

    log(LOG_INFO, "Switching to card path = %s\n", path);

    ps2_mc_data_interface_card_changed();

    if (!sd_exists(path)) {
        cardman_operation = CARDMAN_CREATE;
        ps2_cardman_createCard(path);
    } else {
        cardman_fd = sd_open(path, O_RDWR);
        card_size = sd_filesize(cardman_fd);
        cardman_operation = CARDMAN_OPEN;
        cardprog_pos = 0;
        cardman_sectors_done = 0;

        if (cardman_fd < 0)
            fatal(ERR_CARDMAN, "cannot open card");

        if (ps2_check_cardsize(card_size)) {
            cardman_operation = CARDMAN_OPEN;
            cardprog_pos = 0;
            cardman_sectors_done = 0;
            if (card_size > PS2_CARD_SIZE_8M) {
                ps2_mc_data_interface_set_sdmode(true);
            } else {
                ps2_mc_data_interface_set_sdmode(!PSRAM_AVAILABLE);
            }
            log(LOG_INFO, "reading card (%lu KB).... ", (uint32_t)(card_size / 1024));
            if (cardman_cb)
                cardman_cb(0, false);
        } else {
            cardman_operation = CARDMAN_CREATE;
            ps2_cardman_move_card_crp(path);
            ps2_cardman_createCard(path);
        }
    }
    cardprog_start = time_us_64();

    sector_count = card_size / PS2_PAGE_SIZE;

    log(LOG_INFO, "Open Finished!\n");
}

void ps2_cardman_close(void) {
    if (cardman_fd < 0)
        return;
    ps2_cardman_flush();
    sd_close(cardman_fd);
    cardman_fd = -1;
    current_read_sector = 0;
    priority_sector = -1;
#if WITH_PSRAM
    memset(available_sectors, 0, sizeof(available_sectors));
#endif
}

void ps2_cardman_set_channel(uint16_t chan_num) {
    uint8_t max_chan = card_config_get_max_channels(folder_name, (cardman_state == PS2_CM_STATE_BOOT) ? "BootCard" : folder_name);
    if (chan_num <= max_chan && chan_num >= CHAN_MIN) {
        needs_update |= (chan_num != card_chan);
        card_chan = chan_num;
    }
}

void ps2_cardman_next_channel(void) {
    uint8_t max_chan = card_config_get_max_channels(folder_name, (cardman_state == PS2_CM_STATE_BOOT) ? "BootCard" : folder_name);
    card_chan += 1;
    if (card_chan > max_chan)
#if WITH_GUI
        card_chan = CHAN_MIN;
#else
        card_chan = max_chan; //dont jump to CHAN_MIN. Otherwise without display, you cant see where you actually are.
#endif
    needs_update = true;
}

void ps2_cardman_prev_channel(void) {
    uint8_t max_chan = card_config_get_max_channels(folder_name, (cardman_state == PS2_CM_STATE_BOOT) ? "BootCard" : folder_name);
    card_chan -= 1;
    if (card_chan < CHAN_MIN)
#if WITH_GUI
        card_chan = max_chan;
#else
        card_chan = CHAN_MIN; //dont jump to max_chan. Otherwise without display, you cant see where you actually are.
#endif
    needs_update = true;
}

//TEMP
void ps2_cardman_switch_bootcard(void) {
    if (try_set_boot_card())
        needs_update = true;
}

void ps2_cardman_set_idx(uint16_t idx_num) {
    if ((idx_num >= IDX_MIN) && (idx_num < UINT16_MAX)) {
        needs_update |= (idx_num != card_idx);
        card_idx = idx_num;
        card_chan = CHAN_MIN;
        cardman_state = PS2_CM_STATE_NORMAL;
    }
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

void ps2_cardman_next_idx(void) {
    switch (cardman_state) {
        case PS2_CM_STATE_NAMED:
            if (!try_set_prev_named_card()
                && !try_set_boot_card()
                && !try_set_game_id_card())
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
            uint8_t maxcards = settings_get_ps2_maxcardidx();
            if (maxcards != 0) //0 = unlimited cards UINT16_MAX
            {
                if (card_idx > maxcards)
                    card_idx = maxcards;
            }
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

    char new_folder_name[MAX_FOLDER_NAME_LENGTH] = {};
    if (card_game_id[0]) {
        card_config_get_card_folder(card_game_id, new_folder_name, sizeof(new_folder_name));
        if (new_folder_name[0] == 0x00)
            snprintf(new_folder_name, sizeof(new_folder_name), "%s", card_game_id);
        log(LOG_TRACE, "Folder: %s\n", new_folder_name);
        if ((strcmp(new_folder_name, folder_name) != 0) || (PS2_CM_STATE_GAMEID != cardman_state)) {
            card_idx = PS2_CARD_IDX_SPECIAL;
            cardman_state = PS2_CM_STATE_GAMEID;
            card_chan = CHAN_MIN;
            memcpy(folder_name, new_folder_name, sizeof(folder_name));
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
    if (cardman_operation == CARDMAN_UNKNOWN)
        return false;
    else if (ps2_mc_data_interface_get_sdmode())
        return (cardman_operation != CARDMAN_CREATE);
    else
        return true;
}

bool ps2_cardman_is_idle(void) {
    return cardman_operation == CARDMAN_IDLE;
}

void ps2_cardman_init(void) {
    card_variant = settings_get_ps2_variant();
    if (!try_set_boot_card())
        set_default_card();

    cardman_operation = CARDMAN_UNKNOWN;
}

void ps2_cardman_task(void) {
    ps2_cardman_continue();
}
