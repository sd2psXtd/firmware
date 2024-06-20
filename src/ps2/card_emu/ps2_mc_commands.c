#include "ps2_mc_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "history_tracker/ps2_history_tracker.h"
#include "ps2_cardman.h"
#include "ps2_dirty.h"
#include "ps2_mc_internal.h"
#include "psram/psram.h"
#include "debug.h"

//#define DEBUG_MC_PROTOCOL

uint32_t read_sector, write_sector, erase_sector;
uint8_t readtmp[528];
uint8_t writetmp[528];
int is_write;
bool dma_in_progress = false;
uint32_t readptr, writeptr;
uint8_t *eccptr;

static void __time_critical_func(psram_dma_rx_done)() {
    dma_in_progress = false;
    ps2_dirty_unlock();
}

static void __time_critical_func(start_read_dma)() {
    if (read_sector * 512 + 512 <= ps2_cardman_get_card_size()) {
        ps2_dirty_lockout_renew();
        /* the spinlock will be unlocked by the DMA irq once all data is tx'd */
        ps2_dirty_lock();
        dma_in_progress = true;
        psram_read_dma(read_sector * 512, &readtmp, 512, psram_dma_rx_done);
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_0x11)(void) {
    uint8_t _ = 0U;
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_0x12)(void) {
    uint8_t _ = 0U;
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setEraseAddress)(void) {
    uint8_t _ = 0;
    /* set address for erase */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[0]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&ck);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum
    erase_sector = raw.addr;
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setWriteAddress)(void) {
    uint8_t _ = 0;
    /* set address for write */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[0]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&ck);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum
    write_sector = raw.addr;
    is_write = 1;
    writeptr = 0;
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setReadAddress)(void) {
    uint8_t _ = 0U;
    /* set address for read */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[0]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&raw.a[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&ck);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum
    read_sector = raw.addr;
    if (!ps2_cardman_is_sector_available(read_sector)) {
        ps2_cardman_set_priority_sector(read_sector);
    } else {
        start_read_dma();
    }
    readptr = 0;

    eccptr = &readtmp[512];
    memset(eccptr, 0, 16);

    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_getSpecs)(void) {
    uint8_t _ = 0;
    /* GET_SPECS ? */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    uint32_t sector_count = (flash_mode) ? PS2_CARD_SIZE_1M / 512 : (uint32_t)(ps2_cardman_get_card_size() / 512);

    uint8_t specs[] = {0x00, 0x02, ERASE_SECTORS, 0x00, 0x00, 0x40, 0x00, 0x00};
    specs[4] = (uint8_t)(sector_count & 0xFF);
    specs[5] = (uint8_t)((sector_count >> 8) & 0xFF);
    specs[6] = (uint8_t)((sector_count >> 16) & 0xFF);
    specs[7] = (uint8_t)((sector_count >> 24) & 0xFF);

    mc_respond(specs[0]);
    receiveOrNextCmd(&_);
    mc_respond(specs[1]);
    receiveOrNextCmd(&_);
    mc_respond(specs[2]);
    receiveOrNextCmd(&_);
    mc_respond(specs[3]);
    receiveOrNextCmd(&_);
    mc_respond(specs[4]);
    receiveOrNextCmd(&_);
    mc_respond(specs[5]);
    receiveOrNextCmd(&_);
    mc_respond(specs[6]);
    receiveOrNextCmd(&_);
    mc_respond(specs[7]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(specs));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setTerminator)(void) {
    uint8_t _ = 0U;
    /* SET_TERMINATOR */
    mc_respond(0xFF);
    receiveOrNextCmd(&term);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_getTerminator)(void) {
    uint8_t _ = 0U;
    /* GET_TERMINATOR */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_writeData)(void) {
    uint8_t ck2 = 0U;
    uint8_t _ = 0U;
    /* write data */
    uint8_t sz;
    mc_respond(0xFF);
    receiveOrNextCmd(&sz);
    mc_respond(0xFF);

#ifdef DEBUG_MC_PROTOCOL
    debug_printf("> %02X %02X\n", _, sz);
#endif

    uint8_t ck = 0;
    uint8_t b;

    for (int i = 0; i < sz; ++i) {
        receiveOrNextCmd(&b);
        if (writeptr < sizeof(writetmp)) {
            writetmp[writeptr] = b;
            ++writeptr;
        }
        ck ^= b;
        mc_respond(0xFF);
    }
    // this should be checksum?
    receiveOrNextCmd(&ck2);
    (void)ck2;  // TODO: validate checksum

    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_readData)(void) {
    uint8_t _ = 0U;
    /* read data */
    uint8_t sz;
    mc_respond(0xFF);
    receiveOrNextCmd(&sz);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);

#ifdef DEBUG_MC_PROTOCOL
    debug_printf("> %02X %02X\n", _, sz);
#endif

    uint8_t ck = 0;
    uint8_t b = 0xFF;

    // check if sector is still unavailable, wait on it if we have to and start the DMA
    if (!ps2_cardman_is_sector_available(read_sector)) {
        ps2_cardman_set_priority_sector(read_sector);
        while (!ps2_cardman_is_sector_available(read_sector)) {} // wait for core 0 to load the sector into PSRAM
        start_read_dma();
    }
    // otherwise set read address should have already kicked off the DMA

    for (int i = 0; i < sz; ++i) {
        if (readptr == sizeof(readtmp)) {
            /* a game may read more than one 528-byte sector in a sequence of read ops, e.g. re4 */
            ++read_sector;
            if (!ps2_cardman_is_sector_available(read_sector)) {
                ps2_cardman_set_priority_sector(read_sector);
                while (!ps2_cardman_is_sector_available(read_sector)) {}
            }

            start_read_dma();
            readptr = 0;

            eccptr = &readtmp[512];
            memset(eccptr, 0, 16);
        }

        if (readptr < sizeof(readtmp)) {
            // ensure the requested byte is available
            if (readptr < 512)
                while (dma_in_progress && psram_read_dma_remaining() >= (512 - readptr)) {};
            b = readtmp[readptr];
            mc_respond(b);

            if (readptr <= 512) {
                uint8_t c = EccTable[b];
                eccptr[0] ^= c;
                if (c & 0x80) {
                    eccptr[1] ^= ~(readptr & 0x7F);
                    eccptr[2] ^= (readptr & 0x7F);
                }

                ++readptr;

                if ((readptr & 0x7F) == 0) {
                    eccptr[0] = ~eccptr[0];
                    eccptr[0] &= 0x77;

                    eccptr[1] = ~eccptr[1];
                    eccptr[1] &= 0x7f;

                    eccptr[2] = ~eccptr[2];
                    eccptr[2] &= 0x7f;

                    eccptr += 3;
                }
            } else {
                ++readptr;
            }
        } else
            mc_respond(b);
        ck ^= b;
        receiveOrNextCmd(&_);
    }

    mc_respond(ck);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_commitData)(void) {
    uint8_t _ = 0;
    /* commit for read/write? */
    if (is_write) {
        is_write = 0;
        if (write_sector * 512 + 512 <= ps2_cardman_get_card_size()) {
            ps2_dirty_lockout_renew();
            ps2_dirty_lock();
            psram_write_dma(write_sector * 512, writetmp, 512, NULL);
            psram_wait_for_dma();
            ps2_cardman_mark_sector_available(write_sector); // in case sector is yet to be loaded from sd card
            ps2_dirty_mark(write_sector);
            ps2_dirty_unlock();
#ifdef DEBUG_MC_PROTOCOL
            debug_printf("WR 0x%08X : %02X %02X .. %08X %08X %08X\n", write_sector * 512, writetmp[0], writetmp[1], *(uint32_t *)&writetmp[512],
                         *(uint32_t *)&writetmp[516], *(uint32_t *)&writetmp[520]);
#endif
        }
    } else {
#ifdef DEBUG_MC_PROTOCOL
        debug_printf("RD 0x%08X : %02X %02X .. %08X %08X %08X\n", read_sector * 512, readtmp[0], readtmp[1], *(uint32_t *)&readtmp[512],
                     *(uint32_t *)&readtmp[516], *(uint32_t *)&readtmp[520]);
#endif
    }

    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_erase(void)) {
    uint8_t _ = 0U;
    /* do erase */
    if (erase_sector * 512 + 512 * ERASE_SECTORS <= ps2_cardman_get_card_size()) {
        memset(readtmp, 0xFF, 512);
        ps2_dirty_lockout_renew();
        ps2_dirty_lock();
        for (int i = 0; i < ERASE_SECTORS; ++i) {
            psram_write_dma((erase_sector + i) * 512, readtmp, 512, NULL);
            psram_wait_for_dma();
            ps2_cardman_mark_sector_available(erase_sector + i);
            ps2_dirty_mark(erase_sector + i);
        }
        ps2_dirty_unlock();
#ifdef DEBUG_MC_PROTOCOL
        debug_printf("ER 0x%08X\n", erase_sector * 512);
#endif
    }
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_0xBF)(void) {
    uint8_t _ = 0U;
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_0xF3)(void) {
    uint8_t _ = 0U;
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_keySelect)(
    void) {  // TODO: it fails to get detected at all when ps2_magicgate==0, check if it's intentional
    uint8_t _ = 0U;
    /* SIO_MEMCARD_KEY_SELECT */
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}
