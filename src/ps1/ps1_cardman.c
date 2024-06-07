#include "ps1_cardman.h"

#include <ctype.h>
#include <ps1/ps1_memory_card.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sd.h"
#include "debug.h"
#include "settings.h"
#include <psram/psram.h>
#include "ps1_empty_card.h"

#include "game_names/game_names.h"

#include "hardware/timer.h"

#define CARD_SIZE (128 * 1024)
#define BLOCK_SIZE 128
static uint8_t flushbuf[BLOCK_SIZE];
static int fd = -1;

#define IDX_MIN 1
#define CHAN_MIN 1
#define CHAN_MAX 8

#define MAX_GAME_ID_LENGTH      (16)

static int card_idx;
static int card_chan;
static char folder_name[MAX_GAME_ID_LENGTH];
static ps1_cardman_state_t cardman_state;

static void set_boot_card() {
    card_idx = PS1_CARD_IDX_SPECIAL;
    card_chan = CHAN_MIN;
    cardman_state = PS1_CM_STATE_BOOT;
    snprintf(folder_name, sizeof(folder_name), "BOOT");
}

static void set_default_card() {
    card_idx = settings_get_ps1_card();
    card_chan = settings_get_ps1_channel();
    cardman_state = PS1_CM_STATE_NORMAL;
    snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
}

static bool try_set_game_id_card() {
    const char *game_id = ps1_memory_card_get_game_id();
    if (!game_id[0])
        return false;

    char parent_id[MAX_GAME_ID_LENGTH];
    game_names_get_parent(game_id, parent_id);

    card_idx = PS1_CARD_IDX_SPECIAL;
    card_chan = CHAN_MIN;
    cardman_state = PS1_CM_STATE_GAMEID;
    snprintf(folder_name, sizeof(folder_name), "%s", parent_id);

    return true;
}

void ps1_cardman_init(void) {
    if (settings_get_ps1_autoboot()) {
        set_boot_card();
    } else {
        set_default_card();
    }
}

int ps1_cardman_write_sector(int sector, void *buf512) {
    if (fd < 0)
        return -1;

    if (sd_seek(fd, sector * BLOCK_SIZE) != 0)
        return -1;

    if (sd_write(fd, buf512, BLOCK_SIZE) != BLOCK_SIZE)
        return -1;

    return 0;
}

void ps1_cardman_flush(void) {
    if (fd >= 0)
        sd_flush(fd);
}

static void ensuredirs(void) {
    char cardpath[32];
    
    snprintf(cardpath, sizeof(cardpath), "MemoryCards/PS1/%s", folder_name);
    
    sd_mkdir("MemoryCards");
    sd_mkdir("MemoryCards/PS1");
    sd_mkdir(cardpath);

    if (!sd_exists("MemoryCards") || !sd_exists("MemoryCards/PS1") || !sd_exists(cardpath))
        fatal("error creating directories");
}

static void genblock(size_t pos, void *buf) {
    memset(buf, 0xFF, BLOCK_SIZE);

    if (pos < 0x2000)
        memcpy(buf, &ps1_empty_card[pos], BLOCK_SIZE);
}

void ps1_cardman_open(void) {
    char path[64];
    ensuredirs();

    if (PS1_CM_STATE_BOOT == cardman_state)
        snprintf(path, sizeof(path), "MemoryCards/PS1/%s/BootCard.mcd", folder_name);
    else
        snprintf(path, sizeof(path), "MemoryCards/PS1/%s/%s-%d.mcd", folder_name, folder_name, card_chan);

    if (card_idx != PS1_CARD_IDX_SPECIAL) {
        /* this is ok to do on every boot because it wouldn't update if the value is the same as currently stored */
        settings_set_ps1_card(card_idx);
        settings_set_ps1_channel(card_chan);
    }

    printf("Switching to card path = %s\n", path);
    
    if (!sd_exists(path)) {
        fd = sd_open(path, O_RDWR | O_CREAT | O_TRUNC);

        if (fd < 0)
            fatal("cannot open for creating new card");

        printf("create new image at %s... ", path);
        uint64_t cardprog_start = time_us_64();

        for (size_t pos = 0; pos < CARD_SIZE; pos += BLOCK_SIZE) {
            genblock(pos, flushbuf);
            if (sd_write(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                fatal("cannot init memcard");
            psram_read(pos, flushbuf, BLOCK_SIZE);
        }
        sd_flush(fd);

        uint64_t end = time_us_64();
        printf("OK!\n");

        printf("took = %.2f s; SD write speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
            1000000.0 * CARD_SIZE / (end - cardprog_start) / 1024);
    } else {
        fd = sd_open(path, O_RDWR);

        if (fd < 0)
            fatal("cannot open card");

        /* read 8 megs of card image */
        printf("reading card.... ");
        uint64_t cardprog_start = time_us_64();
        for (size_t pos = 0; pos < CARD_SIZE; pos += BLOCK_SIZE) {
            if (sd_read(fd, flushbuf, BLOCK_SIZE) != BLOCK_SIZE)
                fatal("cannot read memcard");
            psram_write(pos, flushbuf, BLOCK_SIZE);
        }
        uint64_t end = time_us_64();
        printf("OK!\n");

        printf("took = %.2f s; SD read speed = %.2f kB/s\n", (end - cardprog_start) / 1e6,
            1000000.0 * CARD_SIZE / (end - cardprog_start) / 1024);
    }
}

void ps1_cardman_close(void) {
    if (fd < 0)
        return;
    ps1_cardman_flush();
    sd_close(fd);
    fd = -1;
}

void ps1_cardman_next_channel(void) {
    switch (cardman_state) {
        case PS1_CM_STATE_BOOT:
            set_default_card();
            break;
        case PS1_CM_STATE_GAMEID:
        case PS1_CM_STATE_NORMAL:
            card_chan += 1;
            if (card_chan > CHAN_MAX)
                card_chan = CHAN_MIN;
            break;
    }
}

void ps1_cardman_prev_channel(void) {
    switch (cardman_state) {
        case PS1_CM_STATE_BOOT:
            set_default_card();
            break;
        case PS1_CM_STATE_GAMEID:
        case PS1_CM_STATE_NORMAL:
            card_chan -= 1;
            if (card_chan < CHAN_MIN)
                card_chan = CHAN_MAX;
            break;
    }
}

void ps1_cardman_next_idx(void) {
    switch (cardman_state) {
        case PS1_CM_STATE_BOOT:
            if (!try_set_game_id_card())
                set_default_card();
            break;
        case PS1_CM_STATE_GAMEID:
            set_default_card();
            break;
        case PS1_CM_STATE_NORMAL:
            card_idx += 1;
            card_chan = CHAN_MIN;
            if (card_idx > UINT16_MAX)
                card_idx = UINT16_MAX;
            snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
            break;
    }
}

void ps1_cardman_prev_idx(void) {
    switch (cardman_state) {
        case PS1_CM_STATE_BOOT:
            set_default_card();
            break;
        case PS1_CM_STATE_GAMEID:
            if (settings_get_ps1_autoboot())
                set_boot_card();
            else
                set_default_card();
            break;
        case PS1_CM_STATE_NORMAL:
            card_idx -= 1;
            card_chan = CHAN_MIN;
            if (card_idx <= PS1_CARD_IDX_SPECIAL) {
                if (!try_set_game_id_card()) {
                    if (settings_get_ps1_autoboot()) {
                        set_boot_card();
                    } else {
                        set_default_card();
                    }
                }
            } else {
                snprintf(folder_name, sizeof(folder_name), "Card%d", card_idx);
            }
            break;
    }
}

int ps1_cardman_get_idx(void) {
    return card_idx;
}

int ps1_cardman_get_channel(void) {
    return card_chan;
}

void ps1_cardman_set_ode_idx(void) {
    try_set_game_id_card();
}

const char* ps1_cardman_get_folder_name(void) {
    return folder_name;
}

ps1_cardman_state_t ps1_cardman_get_state(void) {
    return cardman_state;
}
