#include "ps1_dirty.h"
#include "ps1_cardman.h"
#include "ps1_mc_data_interface.h"
#include "debug.h"
#ifdef WITH_PSRAM
#include <psram/psram.h>
#endif

#include "bigmem.h"
#define dirty_heap bigmem.ps1.dirty_heap
#define dirty_map bigmem.ps1.dirty_map
#define flushbuf dirty_flushbuf
#define FLUSHBUF_SIZE DIRTY_FLUSHBUF_SIZE

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

spin_lock_t *ps1_dirty_spin_lock;
volatile uint32_t ps1_dirty_lockout;
int ps1_dirty_activity;

static int num_dirty;

static int flushbuf_sd_blocks_count = 0;
static int flushbuf_first_sd_block = -1;
static int flushbuf_last_sd_block = -1;
static int flushbuf_unmarked_sectors[FLUSHBUF_SIZE / PS1_PAGE_SIZE];
static int flushbuf_unmarked_sectors_count = 0;
static int unmarked_sectors[SD_BLOCK_SIZE / PS1_PAGE_SIZE];
static int unmarked_sectors_count = 0;

#define SWAP(a, b) do { \
    uint16_t tmp = a; \
    a = b; \
    b = tmp; \
} while (0);

void ps1_dirty_init(void) {
    if (!ps1_dirty_spin_lock)
        ps1_dirty_spin_lock = spin_lock_init(spin_lock_claim_unused(1));
}

void __time_critical_func(ps1_dirty_mark)(uint32_t sector) {
    if (sector < sizeof(dirty_map)) {
        /* already marked? */
        if (dirty_map[sector])
            return;

        /* update map */
        dirty_map[sector] = 1;

        /* update heap */
        int cur = num_dirty++;
        dirty_heap[cur] = sector;
        while (dirty_heap[cur] < dirty_heap[(cur-1)/2]) {
            SWAP(dirty_heap[cur], dirty_heap[(cur-1)/2]);
            cur = (cur-1)/2;
        }
    }
}

static void heapify(int i) {
    int l = i * 2 + 1;
    int r = i * 2 + 2;
    int best = i;
    if (l < num_dirty && dirty_heap[l] < dirty_heap[best])
        best = l;
    if (r < num_dirty && dirty_heap[r] < dirty_heap[best])
        best = r;
    if (best != i) {
        SWAP(dirty_heap[i], dirty_heap[best]);
        heapify(best);
    }
}

int ps1_dirty_get_marked(void) {
    if (num_dirty == 0)
        return -1;

    uint16_t ret = dirty_heap[0];

    /* update heap */
    dirty_heap[0] = dirty_heap[--num_dirty];
    heapify(0);

    /* update map */
    dirty_map[ret] = 0;

    return ret;
}

static void write_flushbuf(bool isolated) {
    int write;
    if (isolated)
        write = ps1_cardman_write_isolated_sd_block(flushbuf, flushbuf_first_sd_block);
    else
        write = ps1_cardman_write_sd_blocks(flushbuf, flushbuf_sd_blocks_count, flushbuf_first_sd_block);

    if (write != 0) {
        // TODO: do something if we get too many errors?
        // for now lets push it back into the heap and try again later
        QPRINTF("!! writing sd blocks 0x%x to 0x%x failed\n", flushbuf_first_sd_block, flushbuf_last_sd_block);
        ps1_dirty_lock();
        for (int i = 0; i < flushbuf_unmarked_sectors_count; i++) {
            ps1_dirty_mark(flushbuf_unmarked_sectors[i]);
        }
        ps1_dirty_unlock();
    }
    flushbuf_unmarked_sectors_count = 0;
    flushbuf_sd_blocks_count = 0;
    flushbuf_first_sd_block = -1;
    flushbuf_last_sd_block = -1;
}

static void register_flushbuf_sd_block(int sd_block) {
    ++flushbuf_sd_blocks_count;
    if (flushbuf_first_sd_block < 0) flushbuf_first_sd_block = sd_block;
    flushbuf_last_sd_block = sd_block;
    memcpy(flushbuf_unmarked_sectors + flushbuf_unmarked_sectors_count,
           unmarked_sectors,
           unmarked_sectors_count * sizeof(unmarked_sectors[0]));
    flushbuf_unmarked_sectors_count += unmarked_sectors_count;
    unmarked_sectors_count = 0;
}

static int sector_sd_block(int sector) {
    int sector_offset = sector * PS1_PAGE_SIZE;
    int sd_block_offset = sector_offset - (sector_offset % SD_BLOCK_SIZE);
    return sd_block_offset / SD_BLOCK_SIZE;
}

void ps1_dirty_task(void) {
    int num_after = 0;
    int hit = 0;
    uint64_t start = time_us_64();
    while (1) {
        if (!ps1_dirty_lockout_expired())
            break;
        /* do up to 100ms of work per call to dirty_task */
        if ((time_us_64() - start) > 100 * 1000)
            break;

        ps1_dirty_lock();
        int sector = -1;
        int sd_block = -1;
        int next_sd_block = -1;
        do {
            sector = ps1_dirty_get_marked();
            num_after = num_dirty;
            if (sector == -1) break;

            unmarked_sectors[unmarked_sectors_count] = sector;
            ++unmarked_sectors_count;
            sd_block = sector_sd_block(sector);
            next_sd_block = num_after == 0 ? -1 : sector_sd_block(dirty_heap[0]);
        } while (next_sd_block == sd_block);

        if (sector == -1) {
            ps1_dirty_unlock();
            break;
        }

        uint8_t *sd_block_slot = flushbuf + (flushbuf_sd_blocks_count * SD_BLOCK_SIZE);
#if WITH_PSRAM
        psram_read_dma(sd_block * SD_BLOCK_SIZE, sd_block_slot, SD_BLOCK_SIZE, NULL);
        psram_wait_for_dma();
#else
        uint8_t *page = ps1_mc_data_interface_get_page((sd_block * SD_BLOCK_SIZE) / PS1_PAGE_SIZE);
        memcpy(sd_block_slot, page, SD_BLOCK_SIZE);
#endif

        ps1_dirty_unlock();

        ++hit;

        // defer next block if not contiguous
        if (flushbuf_sd_blocks_count > 0 && sd_block != flushbuf_last_sd_block + 1) {
            write_flushbuf(false);
            memcpy(flushbuf, sd_block_slot, SD_BLOCK_SIZE);
            register_flushbuf_sd_block(sd_block);
        } else {
            register_flushbuf_sd_block(sd_block);
            if (flushbuf_sd_blocks_count == FLUSHBUF_SIZE / SD_BLOCK_SIZE) write_flushbuf(false);
        }
    }

    if (flushbuf_sd_blocks_count > 0) write_flushbuf(hit == 1);

    if (hit) {
        ps1_cardman_flush();

        uint64_t end = time_us_64();
        QPRINTF("remain to flush - %d - this one flushed %d and took %u ms\n", num_after, hit, (uint32_t)((end - start) / 1000));
    }

    if (num_after || !ps1_dirty_lockout_expired())
        ps1_dirty_activity = 1;
    else
        ps1_dirty_activity = 0;
}
