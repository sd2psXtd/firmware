#include "history_tracker/ps2_history_tracker.h"

#include <debug.h>
#include <game_db/game_db.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_emu/ps2_mc_data_interface.h"
#include "card_emu/ps2_mc_internal.h"
#include "card_emu/ps2_memory_card.h"
#include "card_emu/ps2_sd2psxman.h"
#include "card_emu/ps2_sd2psxman_commands.h"
#include "hardware/timer.h"
#include "mcfat.h"
#include "mcio.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "pico/types.h"
#include "ps2_cardman.h"
#if WITH_PSRAM
    #include "psram/psram.h"
#endif

#if LOG_LEVEL_PS2_HT == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_HT, level, fmt, ##x)
#endif

#define HISTORY_FILE_SIZE         462
#define HISTORY_ENTRY_COUNT       21
#define HISTORY_ENTRY_SIZE        22
#define HISTORY_ENTRY_POS_LAUNCH  16
#define HISTORY_WRITE_HYST_US     2 * 1000 * 1000
#define HISTORY_CARD_CH_HYST_US   2 * 1000 * 1000
#define HISTORY_BOOTUP_DEL        5 * 1000 * 1000
#define HISTORY_NUMBER_OF_REGIONS 4

#define CHAR_CHINA              'C'
#define CHAR_NORTHAMERICA       'A'
#define CHAR_EUROPE             'E'
#define CHAR_JAPAN              'I'
#define SYSTEMDATA_DIRNAME      "/B%cDATA-SYSTEM"
#define HISTORY_FILENAME_FORMAT "/B%cDATA-SYSTEM/history"

static enum { HISTORY_STATUS_INIT, HISTORY_STATUS_CARD_CHANGED, HISTORY_STATUS_WAITING_WRITE, HISTORY_STATUS_WAITING_REFRESH } status;

static mcfat_cardspecs_t cardspecs;
static mcfat_mcops_t mcOps;
static uint64_t lastAccess = 0U;
static bool writeOccured;

const char regionList[] = {CHAR_CHINA, CHAR_NORTHAMERICA, CHAR_EUROPE, CHAR_JAPAN};
static uint8_t slotCount[HISTORY_NUMBER_OF_REGIONS][HISTORY_ENTRY_COUNT] = {};
static uint32_t fileCluster[HISTORY_NUMBER_OF_REGIONS] = {0, 0, 0, 0};
static bool refreshRequired[HISTORY_NUMBER_OF_REGIONS];

int page_erase(mcfat_cardspecs_t* info, uint32_t page) {
    (void)info;
    (void)page;
    
    return sceMcResSucceed;
}

int page_write(mcfat_cardspecs_t* info, uint32_t page, void* buff) {
    (void)info;
    (void)page;
    (void)buff;
    
    return sceMcResSucceed;
}

int __time_critical_func(page_read)(mcfat_cardspecs_t* info, uint32_t page, uint32_t count, void* buff) {
    (void)info;
    ps2_mc_data_interface_setup_read_page(page, false, true);
    volatile ps2_mcdi_page_t* read_page = ps2_mc_data_interface_get_page(page);
    if (read_page != NULL) {
        ps2_mc_data_interface_wait_for_byte(count);
    
        memcpy(buff, read_page->data, count);
    
        ps2_mc_data_interface_invalidate_read(page);
    } else {
        return sceMcResFailReadCluster;
    }

    return sceMcResSucceed;
}

int __time_critical_func(ecc_write)(mcfat_cardspecs_t* info, uint32_t page, void* buff) {
    (void)info;
    (void)page;
    (void)buff;
    return 0;
}

int __time_critical_func(ecc_read)(mcfat_cardspecs_t* info, uint32_t page, uint32_t count, void* buff) {
    (void)info;
    (void)page;
    (void)buff;
    (void)count;
    return 0;
}

static bool fileExists(char* filename) {
    int fd = mcio_mcOpen(filename, sceMcFileAttrReadable);
    log(LOG_TRACE, "File %s status %d\n", filename, fd);
    if (fd < 0)
        return false;
    else
        mcio_mcClose(fd);

    return true;
}

static bool dirExists(char* dirname) {
    int fd = mcio_mcDopen(dirname);
    log(LOG_TRACE, "Dir %s status %d\n", dirname, fd);

    if (fd < 0)
        return false;
    else
        mcio_mcDclose(fd);
    return true;
}

static void readSlots(uint8_t historyFile[HISTORY_FILE_SIZE], uint8_t slots[HISTORY_ENTRY_COUNT]) {
    for (int i = 0; i < HISTORY_ENTRY_COUNT; i++) {
        if (historyFile[i * HISTORY_ENTRY_SIZE]) {
            for (int j = i * HISTORY_ENTRY_SIZE + HISTORY_ENTRY_POS_LAUNCH; j < (i + 1) * HISTORY_ENTRY_SIZE; j++) {
                slots[i] ^= historyFile[j];
            }
            log(LOG_INFO, "Found game %s with %02x XOR\n", (char*)&historyFile[i * HISTORY_ENTRY_SIZE], historyFile[i * HISTORY_ENTRY_SIZE + HISTORY_ENTRY_POS_LAUNCH]);
        } else {
            slots[i] = 0;
        }
    }
}

void __time_critical_func(ps2_history_tracker_registerPageWrite)(uint32_t page) {
    uint32_t cluster = page / 2;
    if (status != HISTORY_STATUS_CARD_CHANGED) {
        for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
            log(LOG_TRACE, "%u vs %u\n", fileCluster[i], cluster);
            if ((cluster == fileCluster[i]) || (fileCluster[i] == 0)) {
                refreshRequired[i] = true;
                lastAccess = time_us_64();
                status = HISTORY_STATUS_WAITING_REFRESH;
            }
        }
    }
}

static void ps2_history_tracker_readClusters(void) {
    uint8_t buff[HISTORY_FILE_SIZE] = {0x00};
    char filename[23] = {0x00};
    memset(fileCluster, 0x00, sizeof(fileCluster));
    mcio_init();
    log(LOG_INFO, "%s post init \n", __func__);
    for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
        // Read current history file for each region
        snprintf(filename, 23, HISTORY_FILENAME_FORMAT, regionList[i]);
        memset((void*)buff, 0x00, HISTORY_FILE_SIZE);

        log(LOG_INFO, "%s Start reading\n", __func__);
        int fh = mcio_mcOpen(filename, sceMcFileAttrReadable);
        log(LOG_INFO, "Initially reading filename %s, fd %d\n", filename, fh);
        if (fh >= 0) {
            int cluster = mcio_mcGetCluster(fh);
            fileCluster[i] = cluster;
            mcio_mcRead(fh, buff, HISTORY_FILE_SIZE);
            readSlots(buff, slotCount[i]);

            log(LOG_INFO, "Registering Cluster %i\n", cluster);
            mcio_mcClose(fh);
        } else {
            memset(slotCount[i], 0x00, HISTORY_ENTRY_COUNT);
        }
    }
}

void ps2_history_tracker_card_changed() {
    log(LOG_INFO, "%s\n", __func__);

    mcfat_setCardChanged(true);
    cardspecs.cardsize = ps2_cardman_get_card_size();
    mcfat_setConfig(mcOps, cardspecs);

    status = HISTORY_STATUS_CARD_CHANGED;

    lastAccess = time_us_64();

    log(LOG_INFO, "%sCard changed finished\n", __func__);
}

void ps2_history_tracker_init() {
    mcOps.page_erase = &page_erase;
    mcOps.page_read = &page_read;
    mcOps.page_write = &page_write;
    mcOps.ecc_write = &ecc_write;
    mcOps.ecc_read = &ecc_read;

    cardspecs.pagesize = 512;
    cardspecs.blocksize = 16;
    cardspecs.flags = 0x08 | 0x10;

    status = HISTORY_STATUS_INIT;
    writeOccured = false;
    memset(slotCount, 0x00, sizeof(slotCount));
}

void ps2_history_tracker_task() {
    uint64_t micros = time_us_64();
    
    if ((micros < HISTORY_BOOTUP_DEL) 
            || writeOccured
            || !ps2_cardman_is_idle()) {
            lastAccess = micros;
    } else if ((status == HISTORY_STATUS_CARD_CHANGED) 
        && (micros - lastAccess) > HISTORY_CARD_CH_HYST_US) {
        log(LOG_TRACE, "%s:%u\n", __func__, __LINE__);
        ps2_history_tracker_readClusters();
        status = HISTORY_STATUS_WAITING_WRITE;
    
    } else if ((status == HISTORY_STATUS_WAITING_REFRESH) 
        && (micros - lastAccess) > HISTORY_WRITE_HYST_US) {
        // If Writing to MC has just finished...
        uint8_t buff[HISTORY_FILE_SIZE] = {0x00};
        char filename[23] = {0x00};
        char dirname[15] = {0x00};
        log(LOG_INFO, "%s refreshing history...\n", __func__);
        writeOccured = false;

        mcio_init();  // Call init to invalidate caches...

        for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
            uint8_t slots_new[21] = {};
            // Read current history file for each region
            memset((void*)buff, 0x00, HISTORY_FILE_SIZE);
            snprintf(dirname, 15, SYSTEMDATA_DIRNAME, regionList[i]);
            snprintf(filename, 23, HISTORY_FILENAME_FORMAT, regionList[i]);
            log(LOG_INFO, "Checking %s and %s\n", filename, dirname);
            if (refreshRequired[i] && dirExists(dirname) && fileExists(filename)) {
                int fh = mcio_mcOpen(filename, sceMcFileAttrReadable);
                if (fileCluster[i] == 0x0)
                    fileCluster[i] = mcio_mcGetCluster(fh);

                log(LOG_INFO, "Updating filename %s, fd %d, new cluster %u\n", filename, fh, fileCluster[i]);
                if (fh >= 0) {
                    mcio_mcRead(fh, buff, HISTORY_FILE_SIZE);
                    readSlots(buff, slots_new);
                    for (int j = 0; j < HISTORY_ENTRY_COUNT; j++) {
                        if (slots_new[j] != slotCount[i][j]) {
                            char sanitized_game_id[11] = {0};
                            game_db_extract_title_id(&buff[j * HISTORY_ENTRY_SIZE], sanitized_game_id, 16, sizeof(sanitized_game_id));
                            log(LOG_INFO, "Game ID: %s\n", sanitized_game_id);
                            if (game_db_sanity_check_title_id(sanitized_game_id)) {
                                ps2_sd2psxman_set_gameid(sanitized_game_id);
                                break;
                            }
                        }
                    }
                    mcio_mcClose(fh);
                    memcpy((void*)slotCount[i], (void*)slots_new, HISTORY_ENTRY_COUNT);
                } else {
                    log(LOG_INFO, "File exists, but handle returned %d\n", fh);
                }
            }
            refreshRequired[i] = false;
        }
        status = HISTORY_STATUS_WAITING_WRITE;
    }
}
