
#include "ps2_mc_auth.h"

#include <settings.h>
#include <stdint.h>

#include "debug.h"
#include "des.h"
#include "hardware/timer.h"
#include "keystore.h"
#include "ps2_mc_internal.h"

#if LOG_LEVEL_MC_AUTH == 0
#define log(x...)
#else
#define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_MC_AUTH, level, fmt, ##x)
#endif

// keysource and key are self generated values
uint8_t ps2_keysource[] = {0xf5, 0x80, 0x95, 0x3c, 0x4c, 0x84, 0xa9, 0xc0};
uint8_t coh_keysource[] = {0x03, 0x14, 0x93, 0x16, 0x27, 0x02, 0x9D, 0xA2};
uint8_t cex_key[16] = {0x06, 0x46, 0x7a, 0x6c, 0x5b, 0x9b, 0x82, 0x77, 0x0d, 0xdf, 0xe9, 0x7e, 0x24, 0x5b, 0x9f, 0xca};
uint8_t dex_key[16] = {0x17, 0x39, 0xD3, 0xBC, 0xD0, 0x2C, 0x18, 0x07, 0x0F, 0x7A, 0xF3, 0xB7, 0x9E, 0x73, 0x03, 0x1C};
uint8_t coh_key[16] = {0x05, 0x3D, 0x59, 0x77, 0xC4, 0xF7, 0xB0, 0xD4, 0x37, 0xAE, 0x66, 0xA5, 0x17, 0x71, 0xB8, 0xC0};
uint8_t prt_key[16] = {0x8C, 0x4B, 0xEF, 0xA6, 0xF4, 0x9A, 0x23, 0xA0, 0x9C, 0xF1, 0x46, 0xAA, 0x17, 0x1C, 0xFE, 0x75};

uint8_t *key = dex_key;
uint8_t *keysource = ps2_keysource;

uint8_t iv[8];
uint8_t seed[8];
uint8_t nonce[8];
uint8_t MechaChallenge3[8];
uint8_t MechaChallenge2[8];
uint8_t MechaChallenge1[8];
uint8_t CardResponse1[8];
uint8_t CardResponse2[8];
uint8_t CardResponse3[8];
uint8_t hostkey[9];

static bool request_keystore_reset = false;
enum {
    AUTH_STATE_IDLE,
    AUTH_STATE_WAIT_CONFIRM
} auth_state = AUTH_STATE_IDLE;
static bool auth_valid = false;

void __time_critical_func(desEncrypt)(void *key, void *data) {
    DesContext dc;
    desInit(&dc, (uint8_t *)key, 8);
    desEncryptBlock(&dc, (uint8_t *)data, (uint8_t *)data);
}

void __time_critical_func(desDecrypt)(void *key, void *data) {
    DesContext dc;
    desInit(&dc, (uint8_t *)key, 8);
    desDecryptBlock(&dc, (uint8_t *)data, (uint8_t *)data);
}

void __time_critical_func(doubleDesEncrypt)(void *key, void *data) {
    desEncrypt(key, data);
    desDecrypt(&((uint8_t *)key)[8], data);
    desEncrypt(key, data);
}

void __time_critical_func(doubleDesDecrypt)(void *key, void *data) {
    desDecrypt(key, data);
    desEncrypt(&((uint8_t *)key)[8], data);
    desDecrypt(key, data);
}

void __time_critical_func(xor_bit)(const void *a, const void *b, void *Result, size_t Length) {
    size_t i;
    for (i = 0; i < Length; i++) {
        ((uint8_t *)Result)[i] = ((uint8_t *)a)[i] ^ ((uint8_t *)b)[i];
    }
}

void __time_critical_func(generateIvSeedNonce)() {
    switch (settings_get_ps2_variant()) {
        case PS2_VARIANT_COH:
            keysource = coh_keysource;
            key = coh_key;
            break;
        case PS2_VARIANT_PROTO:
            keysource = ps2_keysource;
            key = prt_key;
            break;
        case PS2_VARIANT_RETAIL:
        default:
            keysource = ps2_keysource;
            key = dex_key;
            break;
        break;
    }
    for (int i = 0; i < 8; i++) {
        iv[i] = 0x42;
        seed[i] = keysource[i] ^ iv[i];
        nonce[i] = 0x42;
    }
}

void __time_critical_func(generateResponse)() {
    doubleDesDecrypt(key, MechaChallenge1);
    uint8_t random[8] = {0};
    xor_bit(MechaChallenge1, ps2_civ, random, 8);

    // MechaChallenge2 and MechaChallenge3 let's the card verify the console

    xor_bit(nonce, ps2_civ, CardResponse1, 8);

    doubleDesEncrypt(key, CardResponse1);

    xor_bit(random, CardResponse1, CardResponse2, 8);
    doubleDesEncrypt(key, CardResponse2);

    /* Generates the session key */
    uint8_t CardKey[] = {'M', 'e', 'c', 'h', 'a', 'P', 'w', 'n'};
    xor_bit(CardKey, CardResponse2, CardResponse3, 8);
    doubleDesEncrypt(key, CardResponse3);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_probe)(void) {
    uint8_t _;
    /* probe support ? */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_getIv)(void) {
    uint8_t _;
    log(LOG_INFO, "iv : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(iv));

    /* get IV */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(iv[7]);
    receiveOrNextCmd(&_);
    mc_respond(iv[6]);
    receiveOrNextCmd(&_);
    mc_respond(iv[5]);
    receiveOrNextCmd(&_);
    mc_respond(iv[4]);
    receiveOrNextCmd(&_);
    mc_respond(iv[3]);
    receiveOrNextCmd(&_);
    mc_respond(iv[2]);
    receiveOrNextCmd(&_);
    mc_respond(iv[1]);
    receiveOrNextCmd(&_);
    mc_respond(iv[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(iv));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_getSeed)(void) {
    uint8_t _;
    log(LOG_INFO, "seed : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(seed));

    /* get seed */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(seed[7]);
    receiveOrNextCmd(&_);
    mc_respond(seed[6]);
    receiveOrNextCmd(&_);
    mc_respond(seed[5]);
    receiveOrNextCmd(&_);
    mc_respond(seed[4]);
    receiveOrNextCmd(&_);
    mc_respond(seed[3]);
    receiveOrNextCmd(&_);
    mc_respond(seed[2]);
    receiveOrNextCmd(&_);
    mc_respond(seed[1]);
    receiveOrNextCmd(&_);
    mc_respond(seed[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(seed));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy3)(void) {
    uint8_t _;
    /* dummy 3 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_getNonce)(void) {
    uint8_t _;
    log(LOG_INFO, "nonce : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(nonce));

    /* get nonce */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(nonce[7]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[6]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[5]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[4]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[3]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[2]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[1]);
    receiveOrNextCmd(&_);
    mc_respond(nonce[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(nonce));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy5)(void) {
    uint8_t _;
    /* dummy 5 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_mechaChallenge3)(void) {
    uint8_t _;
    /* MechaChallenge3 */
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[7]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[6]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[5]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[4]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge3[0]);
    /* TODO: checksum below */
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);

    log(LOG_INFO, "MechaChallenge3 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge3));
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_mechaChallenge2)(void) {
    uint8_t _ = 0U;
    /* MechaChallenge2 */
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[7]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[6]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[5]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[4]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge2[0]);
    /* TODO: checksum below */
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);

    log(LOG_INFO, "MechaChallenge2 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge2));
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy8)(void) {
    uint8_t _ = 0U;
    /* dummy 8 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy9)(void) {
    uint8_t _ = 0U;
    /* dummy 9 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummyA)(void) {
    uint8_t _ = 0U;
    /* dummy A */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_mechaChallenge1)(void) {
    uint8_t _ = 0;
    /* MechaChallenge1 */
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[7]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[6]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[5]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[4]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[3]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[2]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[1]);
    mc_respond(0xFF);
    receiveOrNextCmd(&MechaChallenge1[0]);
    /* TODO: checksum below */
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);

    log(LOG_INFO, "MechaChallenge1 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(MechaChallenge1));
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummyC)(void) {
    uint8_t _ = 0;
    /* dummy C */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummyD)(void) {
    uint8_t _ = 0;
    /* dummy D */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummyE)(void) {
    uint8_t _ = 0;
    /* dummy E */
    generateResponse();
    log(LOG_INFO, "CardResponse1 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse1));
    log(LOG_INFO, "CardResponse2 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse2));
    log(LOG_INFO, "CardResponse3 : %02X %02X %02X %02X %02X %02X %02X %02X\n", ARG8(CardResponse3));
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_cardResponse1)(void) {
    uint8_t _ = 0;
    /* CardResponse1 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[7]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[6]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[5]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[4]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[3]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[2]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[1]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse1[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(CardResponse1));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy10)(void) {
    uint8_t _ = 0;
    /* dummy 10 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_cardResponse2)(void) {
    uint8_t _ = 0;
    /* CardResponse2 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[7]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[6]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[5]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[4]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[3]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[2]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[1]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse2[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(CardResponse2));
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_dummy12)(void) {
    uint8_t _ = 0;
    /* dummy 12 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_cardResponse3)(void) {
    uint8_t _ = 0;
    /* CardResponse3 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[7]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[6]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[5]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[4]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[3]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[2]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[1]);
    receiveOrNextCmd(&_);
    mc_respond(CardResponse3[0]);
    receiveOrNextCmd(&_);
    mc_respond(XOR8(CardResponse3));
    receiveOrNextCmd(&_);
    mc_respond(term);

    auth_state = AUTH_STATE_WAIT_CONFIRM;
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_ack)(void) {
    uint8_t _ = 0;
    auth_state = AUTH_STATE_IDLE;
    auth_valid = true;
    /* dummy 14 */
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_sessionKeyEncr)(void) {
    uint8_t _ = 0;
    uint8_t subcmd = 0;
    /* session key encrypt */
    mc_respond(0xFF);
    receiveOrNextCmd(&subcmd);
    if (subcmd == 0x50 || subcmd == 0x40) {
        mc_respond(0x2B);
        receiveOrNextCmd(&_);
        mc_respond(term);
    } else if (subcmd == 0x51 || subcmd == 0x41) {
        /* host mc_responds key to us */
        for (size_t i = 0; i < sizeof(hostkey); ++i) {
            mc_respond(0xFF);
            receiveOrNextCmd(&hostkey[i]);
        }
        mc_respond(0x2B);
        receiveOrNextCmd(&_);
        mc_respond(term);
    } else if (subcmd == 0x52 || subcmd == 0x42) {
        /* now we encrypt/decrypt the key */
        mc_respond(0x2B);
        receiveOrNextCmd(&_);
        mc_respond(term);
    } else if (subcmd == 0x53 || subcmd == 0x43) {
        mc_respond(0x2B);
        receiveOrNextCmd(&_);
        /* we mc_respond key to the host */
        for (size_t i = 0; i < sizeof(hostkey); ++i) {
            mc_respond(hostkey[i]);
            receiveOrNextCmd(&_);
        }
        mc_respond(term);
    } else {
        log(LOG_WARN, "!! unknown subcmd %02X -> %02X\n", 0xF2, subcmd);
    }
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth)(void) {
    uint8_t subcmd = 0;
    mc_respond(0xFF);

    receiveOrNextCmd(&subcmd);
    log(LOG_TRACE, "MC Auth: %02X\n", subcmd);
    switch (subcmd) {
        case 0x0: ps2_mc_auth_probe(); break;
        case 0x1: ps2_mc_auth_getIv(); break;
        case 0x2: ps2_mc_auth_getSeed(); break;
        case 0x3: ps2_mc_auth_dummy3(); break;
        case 0x4: ps2_mc_auth_getNonce(); break;
        case 0x5: ps2_mc_auth_dummy5(); break;
        case 0x6: ps2_mc_auth_mechaChallenge3(); break;
        case 0x7: ps2_mc_auth_mechaChallenge2(); break;
        case 0x8: ps2_mc_auth_dummy8(); break;
        case 0x9: ps2_mc_auth_dummy9(); break;
        case 0xA: ps2_mc_auth_dummyA(); break;
        case 0xB: ps2_mc_auth_mechaChallenge1(); break;
        case 0xC: ps2_mc_auth_dummyC(); break;
        case 0xD: ps2_mc_auth_dummyD(); break;
        case 0xE: ps2_mc_auth_dummyE(); break;
        case 0xF: ps2_mc_auth_cardResponse1(); break;
        case 0x10: ps2_mc_auth_dummy10(); break;
        case 0x11: ps2_mc_auth_cardResponse2(); break;
        case 0x12: ps2_mc_auth_dummy12(); break;
        case 0x13: ps2_mc_auth_cardResponse3(); break;
        case 0x14: ps2_mc_auth_ack(); break;
        default:
            // log("unknown %02X -> %02X\n", ch, subcmd);
            break;
    }
}

/**
  * Official retail memory cards use both developer and retail keys.
  * they use developer keys untill 0xF7 command (this function) is called. then they switch to retail keys
  * the ideal approach is just to respond to this command, but never expect it.
  * retail SECRMAN expects an answer to this, but the others wont.
  * arcade cards support this command but dont perform a key change bc they were not intended to do so.
  */
inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_keySelect)(void) {
    // TODO: it fails to get detected at all when ps2_magicgate==0, check if it's intentional
    uint8_t _ = 0U;
    /* SIO_MEMCARD_KEY_SELECT */
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
    log(LOG_TRACE, "Switching to CEX\n");
    if (PS2_VARIANT_RETAIL == settings_get_ps2_variant())
        key = cex_key;
}

inline __attribute__((always_inline)) void __time_critical_func(ps2_mc_auth_reset)(void) {
    uint8_t _ = 0U;
    if (auth_state == AUTH_STATE_WAIT_CONFIRM) {
        log(LOG_ERROR, "MG Auth failed!!\n");
        auth_state = AUTH_STATE_IDLE;
        request_keystore_reset = !auth_valid;
    }
    mc_respond(0xFF);
    receiveOrNextCmd(&_);
    mc_respond(0x2B);
    receiveOrNextCmd(&_);
    mc_respond(term);
}

bool ps2_mc_auth_keyStoreResetRequired() {
    return request_keystore_reset;
}

void ps2_mc_auth_keyStoreResetAck() {
    request_keystore_reset = false;
}
