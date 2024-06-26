#include "ps1_dirty.h"
#include "ps1_cardman.h"
#include <psram/psram.h>

#include "bigmem.h"
#define dirty_heap bigmem.ps1.dirty_heap
#define dirty_map bigmem.ps1.dirty_map

#include <stdio.h>
#include <string.h>

spin_lock_t *ps1_dirty_spin_lock;
volatile uint32_t ps1_dirty_lockout;
int ps1_dirty_activity;

static int num_dirty;

#define SWAP(a, b) do { \
    uint16_t tmp = a; \
    a = b; \
    b = tmp; \
} while (0);

void ps1_dirty_init(void) {
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

void ps1_dirty_task(void) {
    static uint8_t flushbuf[128];

    int num_after = 0;
    int hit = 0;
    uint64_t start = time_us_64();
    while (1) {
        if (!ps1_dirty_lockout_expired())
            break;
        /* do up to 100ms of work per call to dirty_taks */
        if ((time_us_64() - start) > 100 * 1000)
            break;

        ps1_dirty_lock();
        int sector = ps1_dirty_get_marked();
        num_after = num_dirty;
        if (sector == -1) {
            ps1_dirty_unlock();
            break;
        }
        psram_read_dma(sector * 128, flushbuf, 128, NULL);
        psram_wait_for_dma();
        ps1_dirty_unlock();

        ++hit;

        printf("ps1 - write sector %d\n", sector);

        if (ps1_cardman_write_sector(sector, flushbuf) != 0) {
            // TODO: do something if we get too many errors?
            // for now lets push it back into the heap and try again later
            printf("!! writing sector 0x%x failed\n", sector);

            ps1_dirty_lock();
            ps1_dirty_mark(sector);
            ps1_dirty_unlock();
        }
    }
    /* to make sure writes hit the storage medium */
    ps1_cardman_flush();

    uint64_t end = time_us_64();

    if (hit)
        printf("remain to flush - %d - this one flushed %d and took %d ms\n", num_after, hit, (int)((end - start) / 1000));

    if (num_after || !ps1_dirty_lockout_expired())
        ps1_dirty_activity = 1;
    else
        ps1_dirty_activity = 0;
}
