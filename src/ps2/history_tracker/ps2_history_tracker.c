#include "history_tracker/ps2_history_tracker.h"

#include <debug.h>
#include <game_db/game_db.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "card_emu/ps2_sd2psxman.h"
#include "card_emu/ps2_sd2psxman_commands.h"
#include "hardware/timer.h"
#include "mcfat.h"
#include "mcio.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "pico/types.h"
#include "ps2_cardman.h"
#include "ps2_dirty.h"
#include "psram/psram.h"

//#define USE_INJECT_LOGIC

#define DEBUG(fmt , x...) // printf(fmt, ##x)

#define HISTORY_FILE_SIZE           462
#define HISTORY_ENTRY_COUNT         21
#define HISTORY_ENTRY_SIZE          22
#define HISTORY_ENTRY_POS_LAUNCH    16
#define HISTORY_WRITE_HYST_US       2 * 1000 * 1000
#define HISTORY_NUMBER_OF_REGIONS   4
#define HISTORY_ICON_SIZE           1776

#define CHAR_CHINA              'C'
#define CHAR_NORTHAMERICA       'A'
#define CHAR_EUROPE             'E'
#define CHAR_JAPAN              'I'
#define SYSTEMDATA_DIRNAME      "/B%cDATA-SYSTEM"
#define HISTORY_FILENAME_FORMAT "/B%cDATA-SYSTEM/history"
#define HISTORY_ICON_NAME       "/B%cDATA-SYSTEM/icon.sys"

#ifdef USE_INJECT_LOGIC
extern const char   _binary_icon_A_sys_start, 
                    _binary_icon_A_sys_size;
extern const char   _binary_icon_C_sys_start, 
                    _binary_icon_C_sys_size;
extern const char   _binary_icon_J_sys_start, 
                    _binary_icon_J_sys_size;
#endif

static mcfat_cardspecs_t cardspecs;
static mcfat_mcops_t mcOps;
static uint64_t lastAccess = 0U;

const char regionList[] = {CHAR_CHINA, CHAR_NORTHAMERICA, CHAR_EUROPE, CHAR_JAPAN};
static uint8_t slotCount[HISTORY_NUMBER_OF_REGIONS][HISTORY_ENTRY_COUNT] = {};
static uint32_t fileCluster[HISTORY_NUMBER_OF_REGIONS] = { 0, 0, 0, 0};
static bool refreshRequired[HISTORY_NUMBER_OF_REGIONS];

int page_erase(mcfat_cardspecs_t* info, uint32_t page) {
    (void)info;
    (void)page;
    #ifdef USE_INJECT_LOGIC
    if (page * info->pagesize + info->pagesize <= ps2_cardman_get_card_size()) {
        uint8_t buff[info->pagesize];
        memset((void*)buff, 0xFF, info->pagesize);
        ps2_dirty_lockout_renew();
        ps2_dirty_lock();
        psram_write_dma(page * info->pagesize, buff, info->pagesize, NULL);
        psram_wait_for_dma();
        ps2_dirty_mark(page);
        ps2_dirty_unlock();
    }
    #endif
    return sceMcResSucceed;
}

int page_write(mcfat_cardspecs_t* info, uint32_t page, void* buff) {
    (void)info;
    (void)page;
    (void)buff;
    #ifdef USE_INJECT_LOGIC
    if (page * info->pagesize + info->pagesize <= ps2_cardman_get_card_size()) {
        ps2_dirty_lockout_renew();
        ps2_dirty_lock();
        psram_write_dma(page * info->pagesize, buff, info->pagesize, NULL);
        psram_wait_for_dma();
        ps2_dirty_mark(page);
        ps2_dirty_unlock();
    }
    #endif
    return sceMcResSucceed;
}

int __time_critical_func(page_read)(mcfat_cardspecs_t* info, uint32_t page, uint32_t count, void* buff) {
    if (!ps2_cardman_is_sector_available(page)) {
        ps2_cardman_set_priority_sector(page);
        while (!ps2_cardman_is_sector_available(page)) {sleep_us(1);} // wait for core 0 to load the sector into PSRAM
    }
    ps2_dirty_lockout_renew();
    ps2_dirty_lock();
    psram_read_dma(page * info->pagesize, buff, count, NULL);
    psram_wait_for_dma();
    ps2_dirty_unlock();
    return sceMcResSucceed;
}


static bool fileExists(char* filename) {
    int fd = mcio_mcOpen(filename, sceMcFileAttrReadable);
    DEBUG("File %s status %d\n", filename, fd);
    if ( fd < 0 )
        return false;
    else
        mcio_mcClose(fd);

    return true;
}

static bool dirExists(char* dirname) {
    int fd = mcio_mcDopen(dirname);
    DEBUG("Dir %s status %d\n", dirname, fd);

    if ( fd < 0 )
        return false;
    else
        mcio_mcDclose(fd);
    return true;
}
#ifdef USE_INJECT_LOGIC

static void checkInjectHistoryIcon(char region)
{
    char filename[24] = {0x00};

    snprintf(filename, 24, HISTORY_ICON_NAME, region);

    if (!fileExists(filename)) {
        int flag = sceMcFileAttrWriteable | sceMcFileCreateFile;
        int fd = mcio_mcOpen(filename, flag);
        if (fd >= 0) {
            uint8_t buff[128] = {0};
            size_t icon_size, remaining = HISTORY_ICON_SIZE;
            void* icon_ptr;
            switch(region) {
                case 'C':
                    icon_size = ((size_t)&_binary_icon_C_sys_size);
                    icon_ptr = (void*) &_binary_icon_C_sys_start;
                    break;
                case 'I':
                    icon_size = ((size_t)&_binary_icon_J_sys_size);
                    icon_ptr = (void*) &_binary_icon_J_sys_start;
                    break;
                default:
                    icon_size = ((size_t)&_binary_icon_A_sys_size);
                    icon_ptr = (void*) &_binary_icon_A_sys_start;
                    break;
            }
            DEBUG("Icon size is %i, filename is %s\n", icon_size, filename);
            remaining -= mcio_mcWrite(fd, icon_ptr, icon_size);
            while (remaining > 0) {
                remaining -= mcio_mcWrite(fd, buff, remaining > 128 ? 128 : remaining);
            }

            mcio_mcClose(fd);
        }
    } else {
        DEBUG("Icon: %s already exists\n", filename);
    }

}
#endif

static void readSlots(uint8_t historyFile[HISTORY_FILE_SIZE], uint8_t slots[HISTORY_ENTRY_COUNT]) {
    for (int i = 0; i < HISTORY_ENTRY_COUNT; i++) {
        if (historyFile[i * HISTORY_ENTRY_SIZE]) {
            
            for (int j = i * HISTORY_ENTRY_SIZE + HISTORY_ENTRY_POS_LAUNCH; j < (i+1) * HISTORY_ENTRY_SIZE; j++) {
                slots[i] ^= historyFile[j];
            }
            DEBUG("Found game %s with %02x XOR\n", (char*)&historyFile[i * HISTORY_ENTRY_SIZE],
                   historyFile[i * HISTORY_ENTRY_SIZE + HISTORY_ENTRY_POS_LAUNCH]);
        } else {
            slots[i] = 0;
        }
    }
}

void ps2_history_tracker_registerPageWrite(uint32_t page) {
    uint32_t cluster = page / 2;
    for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
        if ((cluster == fileCluster[i]) || (fileCluster[i] == 0)) {
            refreshRequired[i] = true;
        }
    }
}

void ps2_history_tracker_card_changed() {
    mcfat_setCardChanged(true);
    mcio_init();
    uint8_t buff[HISTORY_FILE_SIZE] = {0x00};
    char filename[23] = {0x00};
    char dirname[15] = {0x00};
    for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
        // Read current history file for each region
        snprintf(dirname, 15, SYSTEMDATA_DIRNAME, regionList[i]);
        snprintf(filename, 23, HISTORY_FILENAME_FORMAT, regionList[i]);
        memset((void*)buff, 0x00, HISTORY_FILE_SIZE);
        #ifdef USE_INJECT_LOGIC
        if (!dirExists(dirname)) {
            int dirsts = mcio_mcMkDir(dirname);
            DEBUG("Dir Creating Status is %d\n", dirsts);
        }
        #endif
        if (fileExists(filename)) {
            int fh = mcio_mcOpen(filename, sceMcFileAttrReadable);
            DEBUG("Initially reading filename %s, fd %d\n", filename, fh);
            if (fh >= 0) {
                mcio_mcRead(fh, buff, HISTORY_FILE_SIZE);
                readSlots(buff, slotCount[i]);
                fileCluster[i] = mcio_mcGetCluster(fh);
                DEBUG("Registering Cluster %d\n", fileCluster[i]);
                mcio_mcClose(fh);
            }
        } else {
        #ifdef USE_INJECT_LOGIC
            DEBUG("Writing history to %s\n", filename);
            int flag = sceMcFileAttrWriteable | sceMcFileCreateFile;
            int fh = mcio_mcOpen(filename, flag);
            if (fh >= 0)
            {
                mcio_mcWrite(fh, (void*)buff, HISTORY_FILE_SIZE);
                mcio_mcClose(fh);
                fh = mcio_mcOpen(filename, sceMcFileAttrReadable);
                fileCluster[i] = mcio_mcGetCluster(fh);
                mcio_mcClose(fh);
                DEBUG("Registering new Cluster %d\n", fileCluster[i]);
            } else {
                DEBUG("File handle is %d\n", fh);
            }

        #endif
            memset(slotCount[i], 0x00, HISTORY_ENTRY_COUNT);
            fileCluster[i] = 0;
        }
        #ifdef USE_INJECT_LOGIC
        checkInjectHistoryIcon(regionList[i]);
        #endif
    }
}

void ps2_history_tracker_init() {
    mcOps.page_erase = &page_erase;
    mcOps.page_read = &page_read;
    mcOps.page_write = &page_write;
    
    cardspecs.pagesize = 512;
    cardspecs.blocksize = 16;
    cardspecs.cardsize = ps2_cardman_get_card_size() ;
    cardspecs.flags = 0x08 | 0x10;

    mcfat_setConfig(mcOps, cardspecs);
}

void ps2_history_tracker_task() {
    static bool prevDirty = false;
    uint64_t micros = time_us_64();
    if (ps2_dirty_activity) {
        prevDirty = true;
        lastAccess = micros;
    } else if (prevDirty && ((micros - lastAccess)  > HISTORY_WRITE_HYST_US)) {
        // If Writing to MC has just finished...
        uint8_t buff[HISTORY_FILE_SIZE] = {0x00};
        char filename[23] = {0x00};
        char dirname[15] = {0x00};        

        mcio_init(); // Call init to invalidate caches...

        for (int i = 0; i < HISTORY_NUMBER_OF_REGIONS; i++) {
            uint8_t slots_new[21] = {};
            // Read current history file for each region
            memset((void*)buff, 0x00, HISTORY_FILE_SIZE);
            snprintf(dirname, 15, SYSTEMDATA_DIRNAME, regionList[i]);
            snprintf(filename, 23, HISTORY_FILENAME_FORMAT, regionList[i]);
            if (refreshRequired[i] && dirExists(dirname) && fileExists(filename)) {
                int fh = mcio_mcOpen(filename, sceMcFileAttrReadable);
                if (fileCluster[i] == 0x0)
                    fileCluster[i] = mcio_mcGetCluster(fh);

                DEBUG("Updating filename %s, fd %d, new cluster %u\n", filename, fh, fileCluster[i]);
                if (fh >= 0) {
                    mcio_mcRead(fh, buff, HISTORY_FILE_SIZE);
                    readSlots(buff, slots_new);
                    for (int j = 0; j < HISTORY_ENTRY_COUNT; j++) {
                        if (slots_new[j] != slotCount[i][j]) {
                            char sanitized_game_id[11] = {0};
                            game_db_extract_title_id(&buff[j * HISTORY_ENTRY_SIZE], sanitized_game_id, 16, sizeof(sanitized_game_id));
                            DEBUG("Game ID: %s\n", sanitized_game_id);
                            if (game_db_sanity_check_title_id(sanitized_game_id)) {
                                ps2_sd2psxman_set_gameid(sanitized_game_id);
                                break;
                            }
                        }
                    }
                    mcio_mcClose(fh);
                    memcpy((void*)slotCount[i], (void*)slots_new, HISTORY_ENTRY_COUNT);
                } else {
                    DEBUG("File exists, but handle returned %d\n", fh);
                }
            }
            refreshRequired[i] = false;
        }
        prevDirty = false;
    }
}