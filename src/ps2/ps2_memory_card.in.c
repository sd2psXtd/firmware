#include <ps2/ps2_cardman.h>
#include <stdint.h>
#define XOR8(a) (a[0] ^ a[1] ^ a[2] ^ a[3] ^ a[4] ^ a[5] ^ a[6] ^ a[7])
#define ARG8(a) a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]

/* resp to 0x81 */
send(0xFF);

/* sub cmd */
recv();
ch = cmd;
#ifdef DEBUG_MC_PROTOCOL
if (ch != 0x42 && ch != 0x43)
    debug_printf("> %02X\n", ch);
#endif

if (ch == 0x11) {
    send(0x2B); recv();
    send(term);
} else if (ch == 0x12) {
    send(0x2B); recv();
    send(term);
} else if (ch == 0x21) {
    /* set address for erase */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    send(0xFF); recv(); raw.a[0] = cmd;
    send(0xFF); recv(); raw.a[1] = cmd;
    send(0xFF); recv(); raw.a[2] = cmd;
    send(0xFF); recv(); raw.a[3] = cmd;
    send(0xFF); recv(); ck = cmd;
    send(0x2B); recv();
    (void)ck; // TODO: validate checksum
    erase_sector = raw.addr;
    send(term);
} else if (cmd == 0x22) {
    /* set address for write */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    send(0xFF); recv(); raw.a[0] = cmd;
    send(0xFF); recv(); raw.a[1] = cmd;
    send(0xFF); recv(); raw.a[2] = cmd;
    send(0xFF); recv(); raw.a[3] = cmd;
    send(0xFF); recv(); ck = cmd;
    send(0x2B); recv();
    (void)ck; // TODO: validate checksum
    write_sector = raw.addr;
    is_write = 1;
    writeptr = 0;
    send(term);
} else if (ch == 0x23) {
    /* set address for read */
    union {
        uint8_t a[4];
        uint32_t addr;
    } raw;
    uint8_t ck;
    send(0xFF); recv(); raw.a[0] = cmd;
    send(0xFF); recv(); raw.a[1] = cmd;
    send(0xFF); recv(); raw.a[2] = cmd;
    send(0xFF); recv(); raw.a[3] = cmd;
    send(0xFF); recv(); ck = cmd;
    send(0x2B); recv();
    (void)ck; // TODO: validate checksum
    read_sector = raw.addr;
    if (read_sector * 512 + 512 <= ps2_cardman_get_card_size()) {
        ps2_dirty_lockout_renew();
        /* the spinlock will be unlocked by the DMA irq once all data is tx'd */
        ps2_dirty_lock();
        read_mc(read_sector * 512, &readtmp, 512+4);
        // dma_channel_wait_for_finish_blocking(0);
        // dma_channel_wait_for_finish_blocking(1);
    }
    readptr = 0;

    eccptr = &readtmp.buf[512];
    memset(eccptr, 0, 16);

    send(term);
} else if (ch == 0x26) {
    /* GET_SPECS ? */
    send(0x2B); recv();
    uint32_t sector_count = (flash_mode) ? PS2_CARD_SIZE_1M / 512 : (uint32_t)(ps2_cardman_get_card_size() / 512);

    uint8_t specs[] = { 0x00, 0x02, ERASE_SECTORS, 0x00, 0x00, 0x40, 0x00, 0x00 };
    specs[4] = (uint8_t)(sector_count & 0xFF);
    specs[5] = (uint8_t)((sector_count >> 8)  & 0xFF);
    specs[6] = (uint8_t)((sector_count >> 16) & 0xFF);
    specs[7] = (uint8_t)((sector_count >> 24) & 0xFF);

    send(specs[0]); recv();
    send(specs[1]); recv();
    send(specs[2]); recv();
    send(specs[3]); recv();
    send(specs[4]); recv();
    send(specs[5]); recv();
    send(specs[6]); recv();
    send(specs[7]); recv();
    send(XOR8(specs)); recv();
    send(term);
} else if (ch == 0x27) {
    /* SET_TERMINATOR */
    send(0xFF);
    recv(); term = cmd;
    send(0x2B); recv();
    send(term);
} else if (ch == 0x28) {
    /* GET_TERMINATOR */
    send(0x2B); recv();
    send(term); recv();
    send(term);
} else if (ch == 0x42) {
    /* write data */
    uint8_t sz;
    send(0xFF); recv(); sz = cmd;
    send(0xFF);

#ifdef DEBUG_MC_PROTOCOL
    debug_printf("> %02X %02X\n", ch, sz);
#endif

    uint8_t ck = 0;
    uint8_t b;

    for (int i = 0; i < sz; ++i) {
        recv(); b = cmd;
        if (writeptr < sizeof(writetmp)) {
            writetmp[writeptr] = b;
            ++writeptr;
        }
        ck ^= b;
        send(0xFF);
    }
    // this should be checksum?
    recv();
    uint8_t ck2 = cmd;
    (void)ck2; // TODO: validate checksum

    send(0x2B); recv();
    send(term);
} else if (ch == 0x43) {
    /* read data */
    uint8_t sz;
    send(0xFF); recv(); sz = cmd;
    send(0x2B); recv();

#ifdef DEBUG_MC_PROTOCOL
    debug_printf("> %02X %02X\n", ch, sz);
#endif

    uint8_t ck = 0;
    uint8_t b = 0xFF;

    for (int i = 0; i < sz; ++i) {
        if (readptr == sizeof(readtmp.buf)) {
            /* a game may read more than one 528-byte sector in a sequence of read ops, e.g. re4 */
            ++read_sector;
            if (read_sector * 512 + 512 <= ps2_cardman_get_card_size()) {
                ps2_dirty_lockout_renew();
                /* the spinlock will be unlocked by the DMA irq once all data is tx'd */
                ps2_dirty_lock();
                read_mc(read_sector * 512, &readtmp, 512+4);
                // TODO: remove this if safe
                // must make sure the dma completes for first byte before we start reading below
                dma_channel_wait_for_finish_blocking(PIO_SPI_DMA_RX_CHAN);
                dma_channel_wait_for_finish_blocking(PIO_SPI_DMA_TX_CHAN);
            }
            readptr = 0;

            eccptr = &readtmp.buf[512];
            memset(eccptr, 0, 16);
        }

        if (readptr < sizeof(readtmp.buf)) {
            b = readtmp.buf[readptr];
            send(b);

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
        } else send(b);
        ck ^= b;
        recv();
    }

    send(ck); recv();
    send(term);
} else if (ch == 0x81) {
    /* commit for read/write? */
    if (is_write) {
        is_write = 0;
        if (write_sector * 512 + 512 <= ps2_cardman_get_card_size()) {
            ps2_dirty_lockout_renew();
            ps2_dirty_lock();
            write_mc(write_sector * 512, writetmp, 512);
            ps2_dirty_mark(write_sector);
            ps2_dirty_unlock();
#ifdef DEBUG_MC_PROTOCOL
            debug_printf("WR 0x%08X : %02X %02X .. %08X %08X %08X\n",
                write_sector * 512, writetmp[0], writetmp[1],
                *(uint32_t*)&writetmp[512], *(uint32_t*)&writetmp[516], *(uint32_t*)&writetmp[520]);
#endif
        }
    } else {
#ifdef DEBUG_MC_PROTOCOL
        debug_printf("RD 0x%08X : %02X %02X .. %08X %08X %08X\n",
            read_sector * 512, readtmp.buf[0], readtmp.buf[1],
            *(uint32_t*)&readtmp.buf[512], *(uint32_t*)&readtmp.buf[516], *(uint32_t*)&readtmp.buf[520]);
#endif
    }

    send(0x2B); recv();
    send(term);
} else if (ch == 0x82) {
    /* do erase */
    if (erase_sector * 512 + 512 * ERASE_SECTORS <= ps2_cardman_get_card_size()) {
        memset(readtmp.buf, 0xFF, 512);
        ps2_dirty_lockout_renew();
        ps2_dirty_lock();
        for (int i = 0; i < ERASE_SECTORS; ++i) {
            write_mc((erase_sector + i) * 512, readtmp.buf, 512);
            ps2_dirty_mark(erase_sector + i);
        }
        ps2_dirty_unlock();
#ifdef DEBUG_MC_PROTOCOL
        debug_printf("ER 0x%08X\n", erase_sector * 512);
#endif
    }
    send(0x2B); recv();
    send(term);
} else if (ch == 0xBF) {
    send(0xFF); recv();
    send(0x2B); recv();
    send(term);
} else if (ch == 0xF3) {
    send(0xFF); recv();
    send(0x2B); recv();
    send(term);
} else if ((ch == 0xF7) && ps2_magicgate) { // TODO: it fails to get detected at all when ps2_magicgate==0, check if it's intentional
    /* SIO_MEMCARD_KEY_SELECT */
    send(0xFF); recv();
    send(0x2B); recv();
    send(term);
} else if ((ch == 0xF0) && ps2_magicgate) {
    /* auth stuff */
    send(0xFF);
    recv();
    int subcmd = cmd;
    if (subcmd == 0) {
        /* probe support ? */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 1) {
        debug_printf("iv : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(iv));

        /* get IV */
        send(0x2B); recv();
        send(iv[7]); recv();
        send(iv[6]); recv();
        send(iv[5]); recv();
        send(iv[4]); recv();
        send(iv[3]); recv();
        send(iv[2]); recv();
        send(iv[1]); recv();
        send(iv[0]); recv();
        send(XOR8(iv)); recv();
        send(term);
    } else if (subcmd == 2) {
        debug_printf("seed : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(seed));

        /* get seed */
        send(0x2B); recv();
        send(seed[7]); recv();
        send(seed[6]); recv();
        send(seed[5]); recv();
        send(seed[4]); recv();
        send(seed[3]); recv();
        send(seed[2]); recv();
        send(seed[1]); recv();
        send(seed[0]); recv();
        send(XOR8(seed)); recv();
        send(term);
    } else if (subcmd == 3) {
        /* dummy 3 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 4) {
        debug_printf("nonce : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(nonce));

        /* get nonce */
        send(0x2B); recv();
        send(nonce[7]); recv();
        send(nonce[6]); recv();
        send(nonce[5]); recv();
        send(nonce[4]); recv();
        send(nonce[3]); recv();
        send(nonce[2]); recv();
        send(nonce[1]); recv();
        send(nonce[0]); recv();
        send(XOR8(nonce)); recv();
        send(term);
    } else if (subcmd == 5) {
        /* dummy 5 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 6) {
        /* MechaChallenge3 */
        send(0xFF);
        recv(); MechaChallenge3[7] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[6] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[5] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[4] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[3] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[2] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[1] = cmd;
        send(0xFF);
        recv(); MechaChallenge3[0] = cmd;
        /* TODO: checksum below */
        send(0xFF); recv();
        send(0x2B); recv();
        send(term);

        debug_printf("MechaChallenge3 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge3));
    } else if (subcmd == 7) {
        /* MechaChallenge2 */
        send(0xFF);
        recv(); MechaChallenge2[7] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[6] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[5] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[4] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[3] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[2] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[1] = cmd;
        send(0xFF);
        recv(); MechaChallenge2[0] = cmd;
        /* TODO: checksum below */
        send(0xFF); recv();
        send(0x2B); recv();
        send(term);

        debug_printf("MechaChallenge2 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge2));
    } else if (subcmd == 8) {
        /* dummy 8 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 9) {
        /* dummy 9 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0xA) {
        /* dummy A */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0xB) {
        /* MechaChallenge1 */
        send(0xFF);
        recv(); MechaChallenge1[7] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[6] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[5] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[4] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[3] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[2] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[1] = cmd;
        send(0xFF);
        recv(); MechaChallenge1[0] = cmd;
        /* TODO: checksum below */
        send(0xFF); recv();
        send(0x2B); recv();
        send(term);

        debug_printf("MechaChallenge1 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge1));
    } else if (subcmd == 0xC) {
        /* dummy C */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0xD) {
        /* dummy D */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0xE) {
        /* dummy E */
        generateResponse();
        debug_printf("CardResponse1 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse1));
        debug_printf("CardResponse2 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse2));
        debug_printf("CardResponse3 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse3));
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0xF) {
        /* CardResponse1 */
        send(0x2B); recv();
        send(CardResponse1[7]); recv();
        send(CardResponse1[6]); recv();
        send(CardResponse1[5]); recv();
        send(CardResponse1[4]); recv();
        send(CardResponse1[3]); recv();
        send(CardResponse1[2]); recv();
        send(CardResponse1[1]); recv();
        send(CardResponse1[0]); recv();
        send(XOR8(CardResponse1)); recv();
        send(term);
    } else if (subcmd == 0x10) {
        /* dummy 10 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0x11) {
        /* CardResponse2 */
        send(0x2B); recv();
        send(CardResponse2[7]); recv();
        send(CardResponse2[6]); recv();
        send(CardResponse2[5]); recv();
        send(CardResponse2[4]); recv();
        send(CardResponse2[3]); recv();
        send(CardResponse2[2]); recv();
        send(CardResponse2[1]); recv();
        send(CardResponse2[0]); recv();
        send(XOR8(CardResponse2)); recv();
        send(term);
    } else if (subcmd == 0x12) {
        /* dummy 12 */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0x13) {
        /* CardResponse3 */
        send(0x2B); recv();
        send(CardResponse3[7]); recv();
        send(CardResponse3[6]); recv();
        send(CardResponse3[5]); recv();
        send(CardResponse3[4]); recv();
        send(CardResponse3[3]); recv();
        send(CardResponse3[2]); recv();
        send(CardResponse3[1]); recv();
        send(CardResponse3[0]); recv();
        send(XOR8(CardResponse3)); recv();
        send(term);
    } else if (subcmd == 0x14) {
        /* dummy 14 */
        send(0x2B); recv();
        send(term);
    } else {
        debug_printf("unknown %02X -> %02X\n", ch, subcmd);
    }
} else if ((ch == 0xF1 || ch == 0xF2) && ps2_magicgate) {
    /* session key encrypt */
    send(0xFF);
    recv();
    int subcmd = cmd;
    if (subcmd == 0x50 || subcmd == 0x40) {
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0x51 || subcmd == 0x41) {
        /* host sends key to us */
        for (size_t i = 0; i < sizeof(hostkey); ++i) {
            send(0xFF);
            recv();
            hostkey[i] = cmd;
        }
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0x52 || subcmd == 0x42) {
        /* now we encrypt/decrypt the key */
        send(0x2B); recv();
        send(term);
    } else if (subcmd == 0x53 || subcmd == 0x43) {
        send(0x2B); recv();
        /* we send key to the host */
        for (size_t i = 0; i < sizeof(hostkey); ++i) {
            send(hostkey[i]);
            recv();
        }
        send(term);
    } else {
        debug_printf("!! unknown subcmd %02X -> %02X\n", 0xF2, subcmd);
    }
} else {
    debug_printf("!! unknown %02X\n", ch);
}

#undef XOR8
