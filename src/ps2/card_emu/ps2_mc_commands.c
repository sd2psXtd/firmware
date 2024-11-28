#include "ps2_mc_commands.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/timer.h"
#include "history_tracker/ps2_history_tracker.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "ps2_cardman.h"
#include "ps2_mc_internal.h"
#include "ps2_mc_data_interface.h"
#include "debug.h"


#if LOG_LEVEL_PS2_MC == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS2_MC, level, fmt, ##x)
#endif

#define PS2_MAX_ACK_DELAY_SHORT         ( 1300 )
#define PS2_MAX_ACK_DELAY_MID           ( 1500 )
#define PS2_MAX_ACK_DELAY_LONG          ( 2000 )

uint32_t read_sector, write_sector, erase_sector;
uint8_t readecc[16];
uint8_t writetmp[528];
int is_write;
uint32_t readptr, writeptr;
uint8_t *eccptr;
uint64_t last_response;

static void __time_critical_func(delayed_response)(char ch, uint32_t delay, const char* func) {
#ifdef DEBUG_USB_UART
    if (!card_active) {log(LOG_ERROR, "%s Card already deselected - byte %02x after %u \n", func, ch, (uint32_t)(time_us_64() - last_response));}
#endif

    while ((ps2_mc_data_interface_delay_required()) && (time_us_64() - last_response < delay)) {
        sleep_us(10);
#ifdef DEBUG_USB_UART
        if (!card_active) {log(LOG_ERROR, "%s Card deselected at byte %02x after %u \n", func, ch, (uint32_t)(time_us_64() - last_response)); break;}
#endif
    }
    mc_respond(ch);
    last_response = time_us_64();
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
    last_response = time_us_64();
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&raw.a[0]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&raw.a[1]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&raw.a[2]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&raw.a[3]);
    erase_sector = raw.addr;
    ps2_mc_data_interface_erase(erase_sector);

    delayed_response(0xFF, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&ck);

    delayed_response(0x2B, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum
    delayed_response(term, PS2_MAX_ACK_DELAY_LONG, __func__);

    log(LOG_TRACE, "> EA %u\n", raw.addr);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setWriteAddress)(void) {
    uint8_t _ = 0;
    /* set address for write */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    last_response = time_us_64();
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&raw.a[0]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&raw.a[1]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&raw.a[2]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&raw.a[3]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&ck);
    delayed_response(0x2B, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum
    write_sector = raw.addr;
    is_write = 1;
    writeptr = 0;
    delayed_response(term, PS2_MAX_ACK_DELAY_MID, __func__);
    log(LOG_TRACE, "> WA %u\n", raw.addr);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_setReadAddress)(void) {
    uint8_t _ = 0U;
    /* set address for read */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    last_response = time_us_64();
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&raw.a[0]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&raw.a[1]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&raw.a[2]);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&raw.a[3]);
    read_sector = raw.addr;
    ps2_mc_data_interface_setup_read_page(read_sector, true, false);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&ck);
    delayed_response(0x2B, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&_);
    (void)ck;  // TODO: validate checksum

    readptr = 0;

    eccptr = readecc;
    memset(eccptr, 0, 16);

    delayed_response(term, PS2_MAX_ACK_DELAY_SHORT, __func__);
    log(LOG_TRACE, "> RA %u\n", raw.addr);
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

    log(LOG_TRACE, "> WD\n");

    mc_respond(0xFF);
    receiveOrNextCmd(&sz);
    mc_respond(0xFF);
    last_response = time_us_64();

    uint8_t ck = 0;
    uint8_t b;

    for (int i = 0; i < sz; ++i) {
        receiveOrNextCmd(&b);
        if (writeptr < sizeof(writetmp)) {
            writetmp[writeptr] = b;
            ++writeptr;
        }
        ck ^= b;
        delayed_response(0xFF, PS2_MAX_ACK_DELAY_MID, __func__);
    }

    // this should be checksum?
    receiveOrNextCmd(&ck2);
    (void)ck2;  // TODO: validate checksum
    if (ck != ck2)
        log(LOG_WARN, "%s Checksum mismatch\n", __func__);

    delayed_response(0x2B, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&_);
    delayed_response(term, PS2_MAX_ACK_DELAY_MID, __func__);

}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_readData)(void) {
    bool ecc_delay = false;
    uint8_t _ = 0U;
    /* read data */
    uint8_t sz;
    volatile ps2_mcdi_page_t* page;
    uint8_t ck = 0;
    uint8_t b = 0xFF;
    last_response = time_us_64();
    if (readptr < PS2_PAGE_SIZE)
        page = ps2_mc_data_interface_get_page(read_sector);
    delayed_response(0xFF, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&sz);
    log(LOG_TRACE, "> RD %u readptr %u sz %u\n", read_sector, readptr, sz);
    delayed_response(0x2B, PS2_MAX_ACK_DELAY_SHORT, __func__);
    receiveOrNextCmd(&_);

#ifdef DEBUG_USB_UART
    if (!card_active) {log(LOG_ERROR, "%s Card already deselected - pre\n", __func__);}
#endif

    for (int i = 0; i < sz; ++i) {
        if (readptr < PS2_PAGE_SIZE + 16) {

            if (readptr < PS2_PAGE_SIZE) {
                // ensure the requested byte is available
                ps2_mc_data_interface_wait_for_byte(readptr);
                b = page->data[readptr];
            } else {
                b = readecc[readptr - PS2_PAGE_SIZE];
            }

            delayed_response(b, PS2_MAX_ACK_DELAY_MID, __func__);

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
                if (ecc_delay && !ps2_mc_data_interface_data_available()) sleep_us(PS2_MAX_ACK_DELAY_MID * 2);
            }
        } else
            delayed_response(b, PS2_MAX_ACK_DELAY_MID, __func__);

        if (!card_active) log(LOG_ERROR, "%s Card already deselected %i\n", __func__, i);

        ck ^= b;
        receiveOrNextCmd(&_);

        if (readptr == PS2_PAGE_SIZE) {
            /* a game may read more than one 528-byte sector in a sequence of read ops, e.g. re4 */
            ps2_mc_data_interface_setup_read_page(read_sector + 1, true, false);
            if (sz - i > 16) {
                log(LOG_TRACE, "> read beyond page\n");
                ecc_delay = true;
            }
        } else if (readptr == PS2_PAGE_SIZE + 16) {
            readptr = 0;
            ++read_sector;

            eccptr = readecc;
            memset(readecc, 0, 16);
            ecc_delay = false;
            if (sz - i > 1) {
                log(LOG_TRACE, "> increasing page to %u (sz %u i %u)\n", read_sector, sz, i);
                page = ps2_mc_data_interface_get_page(read_sector);
            }
        }
    }
#ifdef DEBUG_USB_UART
    if (!card_active) log(LOG_ERROR, "%s Card already deselected end\n", __func__);
#endif

    delayed_response(ck, PS2_MAX_ACK_DELAY_MID, __func__);
    receiveOrNextCmd(&_);
    delayed_response(term, PS2_MAX_ACK_DELAY_MID, __func__);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_commitData)(void) {
    uint8_t _ = 0;
    /* commit for read/write? */
    last_response = time_us_64();
    if (is_write) {
        is_write = 0;
        log(LOG_TRACE, "> C %u\n", write_sector);
        ps2_mc_data_interface_write_mc(write_sector, writetmp);
    }

    delayed_response(0x2B, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&_);
    delayed_response(term, 1700, __func__);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_cmd_erase(void)) {
    uint8_t _ = 0U;
    last_response = time_us_64();

    /* do erase */
    log(LOG_TRACE, "> E %u\n", erase_sector);

    delayed_response(0x2B, PS2_MAX_ACK_DELAY_LONG, __func__);
    receiveOrNextCmd(&_);
    delayed_response(term, PS2_MAX_ACK_DELAY_LONG, __func__);

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
