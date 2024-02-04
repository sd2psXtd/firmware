#include "ps1_cardman.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "sd.h"
#include "debug.h"
#include "settings.h"
#include "bigmem.h"
#include "ps1_empty_card.h"

#include "hardware/timer.h"

#define CARD_SIZE (128 * 1024)
#define BLOCK_SIZE 128
static uint8_t flushbuf[BLOCK_SIZE];
static int fd = -1;

#define IDX_MIN 1
#define IDX_GAMEID 0
#define CHAN_MIN 1
#define CHAN_MAX 8

#define MAX_GAME_NAME_LENGTH    (127)
#define MAX_PREFIX_LENGTH       (4)
#define MAX_GAME_ID_LENGTH      (16)

extern const char _binary_gamedbps1_dat_start, _binary_gamedbps1_dat_size;

static int card_idx;
static int card_chan;
static char card_game_id[MAX_GAME_ID_LENGTH];
static const char* card_game_name;

static bool ps1_cardman_sanity_check_game_id(const char* const game_id) {
    uint8_t i = 0U;

    char splittable_game_id[MAX_GAME_ID_LENGTH];
    strlcpy(splittable_game_id, game_id, MAX_GAME_ID_LENGTH);
    char* prefix = strtok(splittable_game_id, "-");
    char* id = strtok(NULL, "-");

    while (prefix[i] != 0x00) {
        if (!isalpha((int)prefix[i])) {
            return false;
        }
        i++;
    }
    if (i == 0) {
        return false;
    } else {
        i = 0;
    }

    while (prefix[i] != 0x00) {
        if (!isdigit((int)id[i])) {
            return false;
        }
        i++;
    }

    return (i > 0);
}

#pragma GCC diagnostic ignored "-Warray-bounds"
static uint32_t ps1_cardman_char_array_to_uint32(const char in[4]) {
    char inter[4] = {in[3], in[2], in[1], in[0]};
    uint32_t tgt;
    memcpy(&tgt, inter, sizeof(tgt));
    return tgt;
}
#pragma GCC diagnostic pop

static uint32_t ps1_cardman_find_prefix_offset(uint32_t numericPrefix) {
    uint32_t offset = 0;

    const char* pointer = &_binary_gamedbps1_dat_start;

    while (offset == 0) {
        uint32_t currentprefix = ps1_cardman_char_array_to_uint32(pointer), currentoffset = ps1_cardman_char_array_to_uint32(&pointer[4]);

        if (currentprefix == numericPrefix) {
            offset = currentoffset;
        }
        if ((currentprefix == 0U) && (currentoffset == 0U)) {
            break;
        }
        pointer += 8;
    }

    return offset;
}

static bool ps1_cardman_update_game_data(const char* const id) {
    char prefixString[MAX_PREFIX_LENGTH + 1] = {};
    char idString[10] = {};

    uint32_t numericPrefix = 0, prefixOffset = 0, currentId = 0, numericId = 0;

    if (ps1_cardman_sanity_check_game_id(id)) {
        char* copy = strdup(id);
        char* split = strtok(copy, "-");

        if (strlen(split) > 0) {
            strlcpy(prefixString, split, MAX_PREFIX_LENGTH + 1);
            for (uint8_t i = 0; i < MAX_PREFIX_LENGTH; i++) {
                prefixString[i] = toupper((unsigned char)prefixString[i]);
            }
            numericPrefix = ps1_cardman_char_array_to_uint32(prefixString);
        }

        split = strtok(NULL, "-");

        if (strlen(split) > 0) {
            strlcpy(idString, split, 11);
            numericId = atoi(idString);
        }

        prefixOffset = ps1_cardman_find_prefix_offset(numericPrefix);

        if (prefixOffset < (size_t)&_binary_gamedbps1_dat_size) {
            uint32_t offset = prefixOffset;
            do {
                currentId = ps1_cardman_char_array_to_uint32(&(&_binary_gamedbps1_dat_start)[offset]);
                if (currentId == numericId) {
                    uint32_t name_offset = ps1_cardman_char_array_to_uint32(&(&_binary_gamedbps1_dat_start)[offset + 4]);

                    debug_printf("Found ID - Name Offset: %d, Parent ID: %d\n", (int)name_offset,
                                 (int)ps1_cardman_char_array_to_uint32(&(&_binary_gamedbps1_dat_start)[offset + 8]));

                    snprintf(card_game_id, MAX_GAME_ID_LENGTH, "%s-%0*d", prefixString, (int)strlen(idString),
                             (int)ps1_cardman_char_array_to_uint32(&(&_binary_gamedbps1_dat_start)[offset + 8]));

                    debug_printf("Parent ID: %s\n", card_game_id);

                    if ((name_offset < (size_t)&_binary_gamedbps1_dat_size) && (&_binary_gamedbps1_dat_start + name_offset) != 0x00) {
                        card_game_name = (&_binary_gamedbps1_dat_start + name_offset);
                        debug_printf("Name:%s\n", card_game_name);

                        return true;
                    }
                    else
                    {
                        return false;
                    }
                }
                offset += 12;
            } while (currentId != 0);
        }
    }

    return false;
}

void ps1_cardman_init(void) {
    card_idx = settings_get_ps1_card();
    card_chan = settings_get_ps1_channel();
    memset(card_game_id, 0, sizeof(card_game_id));
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
    if ((card_game_id[0] != 0x00) && (card_idx < IDX_MIN) ) {
        snprintf(cardpath, sizeof(cardpath), "MemoryCards/PS1/%s", card_game_id);
    } else {
        snprintf(cardpath, sizeof(cardpath), "MemoryCards/PS1/Card%d", card_idx);
    }

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
    if ((card_game_id[0] != 0x00) && (card_idx < IDX_MIN)) {
        snprintf(path, sizeof(path), "MemoryCards/PS1/%s/%s-%d.mcd", card_game_id, card_game_id, card_chan);
    } else {
        snprintf(path, sizeof(path), "MemoryCards/PS1/Card%d/Card%d-%d.mcd", card_idx, card_idx, card_chan);
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
            memcpy(&bigmem.ps1.card_image[pos], flushbuf, BLOCK_SIZE);
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
            memcpy(&bigmem.ps1.card_image[pos], flushbuf, BLOCK_SIZE);
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
    card_chan += 1;
    if (card_chan > CHAN_MAX)
        card_chan = CHAN_MIN;
}

void ps1_cardman_prev_channel(void) {
    card_chan -= 1;
    if (card_chan < CHAN_MIN)
        card_chan = CHAN_MAX;
}

void ps1_cardman_next_idx(void) {
    card_idx += 1;
    card_chan = CHAN_MIN;
}

void ps1_cardman_prev_idx(void) {
    card_idx -= 1;
    card_chan = CHAN_MIN;
    if ((card_idx == IDX_GAMEID) 
        && (card_game_id[0] == 0x00))
        card_idx = IDX_MIN;
    else if (card_idx < IDX_GAMEID)
        card_idx = IDX_GAMEID;
}

int ps1_cardman_get_idx(void) {
    return card_idx;
}

int ps1_cardman_get_channel(void) {
    return card_chan;
}

void ps1_cardman_set_gameid(const char* game_id) {
    if (!ps1_cardman_update_game_data(game_id))
    {
        strlcpy(card_game_id, game_id, sizeof(card_game_id));
        card_game_name = NULL;
        card_idx = IDX_MIN;
        card_chan = CHAN_MIN;
    }
    else
    {
        card_idx = IDX_GAMEID;
        card_chan = CHAN_MIN;
    }
}

const char* ps1_cardman_get_gameid(void) {
    return card_game_id;
}

const char* ps1_cardman_get_gamename(void) {
    return card_game_name;
}