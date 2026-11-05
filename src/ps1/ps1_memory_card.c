#include "hardware/timer.h"
#include "pico.h"
#include "pico/multicore.h"
#include "ps1_mc_data_interface.h"
#include "ps1_mc_internal.h"
#include "ps1_mmce.h"
#include "string.h"
#include <settings.h>
#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "debug.h"
#include "ps1/ps1_memory_card.h"
#include "ps1_card_backend.h"

static uint64_t us_startup;

#if LOG_LEVEL_PS1_MC == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_PS1_MC, level, fmt, ##x)
#endif


static uint64_t us_startup;
static uint8_t flag;
static uint8_t* curr_page = NULL;
static bool ps2_multitap = false;

volatile bool ps1_card_active = false;
volatile int ps1_mc_exit_request, ps1_mc_reset;
static volatile int mc_exit_response, mc_enter_request, mc_enter_response;

static ps1_memory_card_backend_t* ps1_card_backend = NULL;


#define receiveOrNextCmd(cmd)          \
    if ((ps1_card_backend->recv_mc(cmd) == PS1_RECEIVE_RESET) || !ps1_card_active) \
    { \
    return;}

#define receiveOrNextCntrl(cmd)          \
    if ((ps1_card_backend->recv_cntrl(cmd) == PS1_RECEIVE_RESET) || !ps1_card_active) \
    { \
    return;}


#define respondOrNextCmd(cmd)          \
    if (ps1_card_active) ps1_card_backend->respond(cmd);\
    else {DPRINTF("!RR: %s:%u\n", __func__, __LINE__); return;}

/*
  Send Reply Comment
  81h  N/A   Memory card address
  53h  FLAG  Send Get ID Command (ASCII "S"), Receive FLAG Byte
  00h  5Ah   Receive Memory Card ID1
  00h  5Dh   Receive Memory Card ID2
  00h  5Ch   Receive Command Acknowledge 1
  00h  5Dh   Receive Command Acknowledge 2
  00h  04h   Receive 04h
  00h  00h   Receive 00h
  00h  00h   Receive 00h
  00h  80h   Receive 80h
   */

static void __time_critical_func(mc_cmd_get_card_id)(void) {
    uint8_t _;
    respondOrNextCmd(0x5A); receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D); receiveOrNextCmd(&_);
    respondOrNextCmd(0x5C); receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D); receiveOrNextCmd(&_);
    respondOrNextCmd(0x04); receiveOrNextCmd(&_);
    respondOrNextCmd(0x00); receiveOrNextCmd(&_);
    respondOrNextCmd(0x00); receiveOrNextCmd(&_);
    respondOrNextCmd(0x80); receiveOrNextCmd(&_);
}

/*
  Send Reply Comment
  81h  N/A   Memory card address
  52h  FLAG  Send Read Command (ASCII "R"), Receive FLAG Byte
  00h  5Ah   Receive Memory Card ID1
  00h  5Dh   Receive Memory Card ID2
  MSB  (00h) Send Address MSB  ;\sector number (0..3FFh)
  LSB  (pre) Send Address LSB  ;/
  00h  5Ch   Receive Command Acknowledge 1  ;<-- late /ACK after this byte-pair
  00h  5Dh   Receive Command Acknowledge 2
  00h  MSB   Receive Confirmed Address MSB
  00h  LSB   Receive Confirmed Address LSB
  00h  ...   Receive Data Sector (128 bytes)
  00h  CHK   Receive Checksum (MSB xor LSB xor Data bytes)
  00h  47h   Receive Memory End Byte (should be always 47h="G"=Good for Read)
  */
static void __time_critical_func(mc_cmd_read)(bool long_read) {
    uint8_t page_msb = 0U, page_lsb = 0U;
    uint8_t offset = 0;
    uint8_t chk = 0;
    uint16_t page = 0U;
    uint8_t _;
    respondOrNextCmd(0x5A);       receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D);       receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);       receiveOrNextCmd(&page_msb);
    respondOrNextCmd(page_msb);   receiveOrNextCmd(&page_lsb);
    chk = page_msb ^ page_lsb;
    respondOrNextCmd(0x5C);       receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D);       receiveOrNextCmd(&_);
    respondOrNextCmd(page_msb);   receiveOrNextCmd(&_);
    page = (page_msb << 8) | page_lsb;
    ps1_mc_data_interface_setup_read_page(page);
    respondOrNextCmd(page_lsb);   receiveOrNextCmd(&_);
    do {
        curr_page = ps1_mc_data_interface_get_page(page);
        for (offset = 0; offset < 128; offset++) {
            ps1_mc_data_interface_wait_for_byte(offset);
            respondOrNextCmd(curr_page[offset]);
            chk ^= curr_page[offset];
            receiveOrNextCmd(&_)
        }
        page++;
        ps1_mc_data_interface_setup_read_page(page);
    } while (long_read);
    respondOrNextCmd(chk);        receiveOrNextCmd(&_);
    respondOrNextCmd(0x47);
    curr_page = NULL;
    DPRINTF("Read page %d done\n", page);
}


/**
  Send Reply Comment
  81h  N/A   Memory card address
  57h  FLAG  Send Write Command (ASCII "W"), Receive FLAG Byte
  00h  5Ah   Receive Memory Card ID1
  00h  5Dh   Receive Memory Card ID2
  MSB  (00h) Send Address MSB  ;\sector number (0..3FFh)
  LSB  (pre) Send Address LSB  ;/
  ...  (pre) Send Data Sector (128 bytes)
  CHK  (pre) Send Checksum (MSB xor LSB xor Data bytes)
  00h  5Ch   Receive Command Acknowledge 1
  00h  5Dh   Receive Command Acknowledge 2
  00h  4xh   Receive Memory End Byte (47h=Good, 4Eh=BadChecksum, FFh=BadSector)
   */
static void __time_critical_func(mc_cmd_write)(void) {
    uint8_t page_msb = 0U, page_lsb = 0U;
    uint8_t offset = 0;
    uint8_t in = 0;
    uint8_t prev;
    uint8_t chk = 0;
    uint8_t _;
    flag = 0;

    respondOrNextCmd(0x5A);       receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D);       receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);       receiveOrNextCmd(&page_msb);
    respondOrNextCmd(page_msb);   receiveOrNextCmd(&page_lsb);
    chk = page_msb ^ page_lsb;
    prev = page_lsb;
    for (offset = 0; offset < PS1_PAGE_SIZE; offset++) {
        respondOrNextCmd(prev);   receiveOrNextCmd(&in);
        ps1_mc_data_interface_write_byte(((page_msb * 256) + page_lsb) * PS1_PAGE_SIZE + offset, in);
        chk ^= in;
        prev = in;
    }
    respondOrNextCmd(prev);           receiveOrNextCmd(&in);
    respondOrNextCmd(0x5C);           receiveOrNextCmd(&_);
    respondOrNextCmd(0x5D);           receiveOrNextCmd(&_);
    if (in == chk) {
        uint16_t page = (page_msb * 256) + page_lsb;
        ps1_mc_data_interface_write_mc(page);
        respondOrNextCmd(0x47);
    } else {
        respondOrNextCmd(0x4E);
    }
}

static void __time_critical_func(mc_mmce_ping)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x27);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
}

static void __time_critical_func(mc_mmce_set_game_id)(void) {
    uint8_t length = 0;
    uint8_t game_id[UINT8_MAX] = {0};
    char received_game_id[16];
    uint8_t prev = 0;
    uint8_t _;
    memset(received_game_id, 0, sizeof(received_game_id));
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);       // Reserved
    respondOrNextCmd(0x00);   receiveOrNextCmd(&length);  // length
    prev = length;

    for (uint8_t i = 0; i < length; i++) {
        respondOrNextCmd(prev);   receiveOrNextCmd(&game_id[i]);
        prev = game_id[i];
    }
    game_id[length == UINT8_MAX ? UINT8_MAX - 1 : length] = 0x00;

    ps1_mmce_set_gameid(game_id);
}

static void __time_critical_func(mc_mmce_prev_channel)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x20);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
    ps1_mmce_prev_ch(false);
}

static void __time_critical_func(mc_mmce_next_channel)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x20);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
    ps1_mmce_next_ch(false);
}

static void __time_critical_func(mc_mmce_prev_index)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x20);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
    ps1_mmce_prev_idx(false);
}

static void __time_critical_func(mc_mmce_next_index)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x20);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
    ps1_mmce_next_idx(false);
}

static void __time_critical_func(mc_mmce_reset)(void) {
    uint8_t _;
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x00);   receiveOrNextCmd(&_);
    respondOrNextCmd(0x20);   receiveOrNextCmd(&_);
    respondOrNextCmd(0xFF);   receiveOrNextCmd(&_);
    ps1_mmce_reset(false);
}

/**
  01h  Hi-Z  Controller address
  42h  idlo  Receive ID bit0..7 (variable) and Send Read Command (ASCII "B")
  TAP  idhi  Receive ID bit8..15 (usually/always 5Ah)
  MOT  swlo  Receive Digital Switches bit0..7
  MOT  swhi  Receive Digital Switches bit8..15
  --------

  Switch Bits:
  0   Select Button    (0=Pressed, 1=Released)
  1   L3/Joy-button    (0=Pressed, 1=Released/None/Disabled) ;analog mode only
  2   R3/Joy-button    (0=Pressed, 1=Released/None/Disabled) ;analog mode only
  3   Start Button     (0=Pressed, 1=Released)
  4   Joypad Up        (0=Pressed, 1=Released)
  5   Joypad Right     (0=Pressed, 1=Released)
  6   Joypad Down      (0=Pressed, 1=Released)
  7   Joypad Left      (0=Pressed, 1=Released)


  8   L2 Button        (0=Pressed, 1=Released) (Lower-left shoulder)
  9   R2 Button        (0=Pressed, 1=Released) (Lower-right shoulder)
  10  L1 Button        (0=Pressed, 1=Released) (Upper-left shoulder)
  11  R1 Button        (0=Pressed, 1=Released) (Upper-right shoulder)
  12  /\ Button        (0=Pressed, 1=Released) (Triangle, upper button)
  13  () Button        (0=Pressed, 1=Released) (Circle, right button)
  14  >< Button        (0=Pressed, 1=Released) (Cross, lower button)
  15  [] Button        (0=Pressed, 1=Released) (Square, left button)
 */
static void mc_read_controller(void) {
    static uint8_t prevCommand = 0;
    uint8_t controller_in[2];
    uint8_t _ = 0x00;

    receiveOrNextCmd(&_);
    if (_ == (uint8_t)'B') {    // Only reactive to "read buttons" command
        receiveOrNextCntrl(&_); // Hi-Z
        receiveOrNextCntrl(&_);
        if ((_== 0x00) || (_ == 0xFF))
            return;
        receiveOrNextCntrl(&_);
        if (_ != 0x5A)
            return;
        for (uint8_t i = 0; i < 2; i++) {
            receiveOrNextCntrl(&controller_in[i]);
        }
        #define HOTKEYS 0b00001111
        #define BTN_UP  0b00010000
        #define BTN_DWN 0b01000000
        #define BTN_LFT 0b10000000
        #define BTN_RGT 0b00100000
        #define BTN_SEL 0b00000001
        #define IS_PRESSED(x,y) ((x&y) == 0)

        if (IS_PRESSED(controller_in[1], HOTKEYS)) {
            if (prevCommand == 0) {
                if (IS_PRESSED(controller_in[0], BTN_UP)) {
                    prevCommand = MMCE_PS1_NXT_CARD;
                } else if (IS_PRESSED(controller_in[0], BTN_DWN)) {
                    prevCommand = MMCE_PS1_PRV_CARD;
                } else if (IS_PRESSED(controller_in[0], BTN_LFT)) {
                    prevCommand = MMCE_PS1_PRV_CH;
                } else if (IS_PRESSED(controller_in[0], BTN_RGT)) {
                    prevCommand = MMCE_PS1_NXT_CH;
                } else if (IS_PRESSED(controller_in[0], BTN_SEL)) {
                    prevCommand = MMCE_PS1_SWITCH_BOOTCARD;
                }
            }
        } else if (prevCommand != 0){
            switch (prevCommand) {
                case MMCE_PS1_NXT_CARD:
                    ps1_mmce_next_idx(false);
                    break;
                case MMCE_PS1_PRV_CARD:
                    ps1_mmce_prev_idx(false);
                    break;
                case MMCE_PS1_NXT_CH:
                    ps1_mmce_next_ch(false);
                    break;
                case MMCE_PS1_PRV_CH:
                    ps1_mmce_prev_ch(false);
                    break;
                case MMCE_PS1_SWITCH_BOOTCARD:
                    ps1_mmce_switch_bootcard(false);
                    break;
            }
            prevCommand = 0;
        }
    }

}

static void __time_critical_func(mc_main_loop)(void) {
    flag = 8;

    while (1) {
        uint8_t ch = 0x00;

        uint8_t received = ps1_card_backend->receive_first(&ch);

        if (received != PS1_RECEIVE_OK) {
            if (received == PS1_RECEIVE_EXIT) {
                break;
            }
            /* If this ch belongs to the next command sequence */
            if (received == PS1_RECEIVE_RESET) {
                continue;
            }
        }

        if (0x81 == ch) { /* Command is for MC - process! */
            ps1_card_backend->respond(flag);

            if (ps1_card_backend->recv_mc(&ch) == PS1_RECEIVE_RESET)
                continue;

            switch (ch) {
                case 0x20: mc_mmce_ping(); break;
                case 0x21: mc_mmce_set_game_id(); break;
                case 0x22: mc_mmce_prev_channel(); break;
                case 0x23: mc_mmce_next_channel(); break;
                case 0x24: mc_mmce_prev_index(); break;
                case 0x25: mc_mmce_next_index(); break;
                case 0x27: mc_mmce_reset(); break;
                case 'B': mc_cmd_read(true); break;
                case 'R': mc_cmd_read(false); break;
                case 'S': mc_cmd_get_card_id(); break;
                case 'W': mc_cmd_write(); break;
                default: DPRINTF("Unknown command: 0x%.02x\n", ch); break;
            }
        } else if ((0x01 == ch) && (settings_get_ps1_controllercombo())) {
            mc_read_controller();
        } else if ((0x21 == ch) && !ps2_multitap) {
            ps1_card_backend->respond(0x00);

            if (PS1_RECEIVE_RESET == ps1_card_backend->recv_mc(&ch))
                continue;

            if (0x53 == ch) {
                ps1_card_backend->respond(0x0F);
            } else if (ch == 0x21) {      // PS2 multitap is also sending 0x21 as configuration command
                ps2_multitap = true;
            }
        } else {
        }

    }

}

static void __no_inline_not_in_flash_func(mc_main)(void) {
    while (1) {
        while (!mc_enter_request)
        {}
        mc_enter_response = 1;

        ps1_card_backend->reset();
        mc_main_loop();

        log(LOG_TRACE, "%s exit\n", __func__);
        if (settings_get_mode(true) != MODE_PS1) {
            log(LOG_TRACE, "Mode or variant changed, exiting memcard core\n");
            return;
        } else {
            mc_exit_response = 1;
            log(LOG_TRACE, "Mode still PS1, reinitializing memcard core\n");
        }
    }
}


void ps1_memory_card_main(void) {
    multicore_lockout_victim_init();

    ps1_card_backend = ps1_card_backend_get();
    ps1_card_backend->init();
    ps1_card_backend->startup();

    us_startup = time_us_64();
    mc_main();
    ps1_card_backend->shutdown();
    ps1_card_backend->deinit();
    log(LOG_TRACE, "Secondary core exit!\n");
    mc_exit_response = 1;
}
static int memcard_running;

void ps1_memory_card_exit(void) {
    if (!memcard_running)
        return;

    ps1_mc_exit_request = 1;
    while (!mc_exit_response)
    {}
    ps1_mc_exit_request = mc_exit_response = 0;
    memcard_running = 0;
}

void ps1_memory_card_enter(void) {
    if (memcard_running)
        return;

    mc_enter_request = 1;
    while (!mc_enter_response)
    {}
    mc_enter_request = mc_enter_response = 0;
    memcard_running = 1;
}
