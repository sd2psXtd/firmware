#include "ps2_mc_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "history_tracker/ps2_history_tracker.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "ps2_cardman.h"
#include "ps2_mc_internal.h"
#include "ps2_mc_data_interface.h"
#include "debug.h"

//#define DEBUG_MC_PROTOCOL

#if LOG_LEVEL_MC_DATA == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_MC_DATA, level, fmt, ##x)
#endif


uint32_t read_sector, write_sector, erase_sector;
uint8_t readecc[16];
uint8_t writetmp[528];
int is_write;
uint32_t readptr, writeptr;
uint8_t *eccptr;



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
    ps2_mc_data_interface_erase(erase_sector);
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
    if (raw.addr != read_sector) {
        ps2_mc_data_interface_invalidate_read(read_sector);
        ps2_mc_data_interface_invalidate_readahead();
    }

    read_sector = raw.addr;
    ps2_mc_data_interface_setup_read_page(read_sector, true, true);

    readptr = 0;

    eccptr = readecc;
    memset(eccptr, 0, 16);

    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_getSpecs)(void) {
    uint8_t _ = 0;
    /* GET_SPECS ? */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    uint32_t sector_count = (uint32_t)(ps2_cardman_get_card_size() / 512);

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
    if (ck == ck2)
        log(LOG_TRACE, "%s Checksum match\n", __func__);

    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_readData)(void) {

    uint8_t _ = 0U;
    /* read data */
    uint8_t sz;
    volatile ps2_mcdi_page_t* page;
    mc_respond(0xFF);
    receiveOrNextCmd(&sz);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);

#ifdef DEBUG_MC_PROTOCOL
    debug_printf("> %02X %02X\n", _, sz);
#endif

    uint8_t ck = 0;
    uint8_t b = 0xFF;

    page = ps2_mc_data_interface_get_page(read_sector);

    if (!page) {
        ps2_mc_data_interface_setup_read_page(read_sector, false, true);
        page = ps2_mc_data_interface_get_page(read_sector);
    } else {
        uint32_t timeout = 1000;
        if ((page->page_state != PAGE_DATA_AVAILABLE) && (page->page_state != PAGE_READ_AHEAD_AVAILABLE)) {
            log(LOG_WARN, "%s Page %u not read - state: %u\n", __func__, page->page, page->page_state);
        }
        while ((page->page_state != PAGE_DATA_AVAILABLE) && (page->page_state != PAGE_READ_AHEAD_AVAILABLE) && (timeout-- > 0)) {sleep_us(10);printf(".");};
        if (timeout == 0) {
            log(LOG_ERROR, "%s timeout reading for %u - state: %u\n", __func__, page->page, page->page_state);
        }
    }
    if (!page) {
        log(LOG_ERROR, "%s Didn't get page %u\n", __func__, read_sector);
        return;
    }

    for (int i = 0; i < sz; ++i) {
        if (readptr < PS2_PAGE_SIZE + 16) {
            // ensure the requested byte is available
            ps2_mc_data_interface_wait_for_byte(readptr);

            if (readptr < PS2_PAGE_SIZE)
                b = page->data[readptr];
            else
                b = readecc[readptr - PS2_PAGE_SIZE];

            mc_respond(b);

            if (readptr <= PS2_PAGE_SIZE) {
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
    if (readptr == PS2_PAGE_SIZE + 16) {
        /* a game may read more than one 528-byte sector in a sequence of read ops, e.g. re4 */
        ps2_mc_data_interface_invalidate_read(read_sector);
        ++read_sector;

        ps2_mc_data_interface_setup_read_page(read_sector, true, false);

        readptr = 0;

        eccptr = readecc;
        memset(readecc, 0, 16);
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
        log(LOG_INFO, "%sWrite Sector %u\n", __func__, write_sector);
        //while (ps2_mc_data_interface_write_busy()) {};
        ps2_mc_data_interface_write_mc(write_sector, writetmp);
#ifdef DEBUG_MC_PROTOCOL
            debug_printf("WR 0x%08X : %02X %02X .. %08X %08X %08X\n", write_sector * 512, writetmp[0], writetmp[1], *(uint32_t *)&writetmp[512],
                         *(uint32_t *)&writetmp[516], *(uint32_t *)&writetmp[520]);
#endif

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
    //ps2_mc_data_interface_erase(erase_sector);
#ifdef DEBUG_MC_PROTOCOL
        debug_printf("ER 0x%08X\n", erase_sector * 512);
#endif

    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
    erase_sector = UINT32_MAX;
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
/**
  * Official retail memory cards use both developer and retail keys.
  * they use developer keys untill 0xF7 command (this function) is called. then they switch to retail keys
  * the ideal approach is just to respond to this command, but never expect it.
  * retail SECRMAN expects an answer to this, but the others wont. 
  * arcade cards support this command but dont perform a key change bc they were not intended to do so.
  */
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
