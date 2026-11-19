#include "oled.h"

#include "config.h"
#include "settings.h"
#include "ssd1306.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "pico/mutex.h"
#include <stdatomic.h>

#define blit16_ARRAY_ONLY
#define blit16_NO_HELPERS
#include "blit16.h"

#define FRAME_TIMEOUT_US 50000u

enum {
    UPDATE_CONTRAST = 1,
    UPDATE_VCOMH = 2,
    UPDATE_FLIP = 4,
};

typedef enum {
    TRANSFER_IDLE,
    TRANSFER_BUSY,
    TRANSFER_DONE,
    TRANSFER_ERROR,
} transfer_state_t;

static ssd1306_t oled_disp = { .external_vcc = 0 };
static bool initialized, powered, frame_pending, frame_retried;
static atomic_bool showing_error, transfer_active;
static uint64_t last_action_time_us;
static uint32_t transfer_start;
static int dma_channel = -1;
static uint8_t updates, contrast, vcomh;
static bool flipped;
auto_init_mutex(oled_lock);

/* Co=1 precedes each command byte. Co=0, D/C=1 starts the framebuffer data. */
static const uint8_t frame_prefix[] = {
    0x80, SET_COL_ADDR, 0x80, 0, 0x80, DISPLAY_WIDTH - 1,
    0x80, SET_PAGE_ADDR, 0x80, 0, 0x80, DISPLAY_HEIGHT / 8 - 1,
    0x40,
};
static uint16_t frame_words[sizeof(frame_prefix) + DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];

static void init_bus(void) {
    i2c_init(OLED_I2C_PERIPH, OLED_I2C_CLOCK);
    i2c_hw_t *hw = i2c_get_hw(OLED_I2C_PERIPH);
    hw->enable = 0;
    hw->tar = OLED_I2C_ADDR;
    hw->enable = 1;
}

void oled_update_last_action_time(void) {
    last_action_time_us = time_us_64();
}

int oled_init(void) {
    mutex_enter_blocking(&oled_lock);
    if (!initialized) {
        initialized = true;
        gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
        gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
        gpio_pull_up(OLED_I2C_SDA);
        gpio_pull_up(OLED_I2C_SCL);
        init_bus();
        oled_update_last_action_time();

        powered = ssd1306_init(
            &oled_disp, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_I2C_ADDR, OLED_I2C_PERIPH,
            settings_get_display_contrast(), settings_get_display_vcomh(), settings_get_display_flipped()
        );
        if (powered) {
            dma_channel = dma_claim_unused_channel(false);
            if (dma_channel >= 0) {
                dma_channel_config cfg = dma_channel_get_default_config(dma_channel);
                channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
                channel_config_set_dreq(&cfg, i2c_get_dreq(OLED_I2C_PERIPH, true));
                channel_config_set_irq_quiet(&cfg, true);
                dma_channel_configure(dma_channel, &cfg, &i2c_get_hw(OLED_I2C_PERIPH)->data_cmd,
                                      frame_words, 0, false);
                for (size_t i = 0; i < sizeof(frame_prefix); ++i)
                    frame_words[i] = frame_prefix[i];
            }
        }
    }
    mutex_exit(&oled_lock);
    return oled_disp.buffer != NULL;
}

static transfer_state_t transfer_state(void) {
    if (!atomic_load_explicit(&transfer_active, memory_order_relaxed))
        return TRANSFER_IDLE;

    uint32_t status = i2c_get_hw(OLED_I2C_PERIPH)->raw_intr_stat;
    if (status & I2C_IC_RAW_INTR_STAT_TX_ABRT_BITS)
        return TRANSFER_ERROR;
    /* DMA can finish while the last bytes are still in the I2C FIFO. */
    if ((status & I2C_IC_RAW_INTR_STAT_STOP_DET_BITS) && !dma_channel_is_busy(dma_channel))
        return TRANSFER_DONE;
    return time_us_32() - transfer_start < FRAME_TIMEOUT_US ? TRANSFER_BUSY : TRANSFER_ERROR;
}

static void finish_transfer(transfer_state_t state) {
    if (state == TRANSFER_ERROR) {
        dma_channel_abort(dma_channel);
        init_bus();
    } else if (state == TRANSFER_DONE) {
        (void)i2c_get_hw(OLED_I2C_PERIPH)->clr_stop_det;
    }
    atomic_store_explicit(&transfer_active, false, memory_order_relaxed);
}

static void start_frame(void) {
    if (dma_channel < 0) {
        ssd1306_show(&oled_disp);
        return;
    }

    const size_t word_count = sizeof(frame_words) / sizeof(frame_words[0]);
    for (size_t i = 0; i < oled_disp.bufsize; ++i)
        frame_words[sizeof(frame_prefix) + i] = oled_disp.buffer[i];
    frame_words[word_count - 1] |= I2C_IC_DATA_CMD_STOP_BITS;

    (void)i2c_get_hw(OLED_I2C_PERIPH)->clr_intr;
    transfer_start = time_us_32();
    atomic_store_explicit(&transfer_active, true, memory_order_relaxed);
    dma_channel_transfer_from_buffer_now(dma_channel, frame_words, word_count);
}

/* Merge the LVGL area into the framebuffer while DMA reads its own snapshot. */
void oled_write_pixels(int x, int y, int width, int height, const uint8_t *pixels) {
    if (!mutex_try_enter(&oled_lock, NULL))
        return;
    if (oled_disp.buffer && !showing_error) {
        for (int row = y; row < y + height; ++row) {
            uint8_t *dst = oled_disp.buffer + (row / 8) * DISPLAY_WIDTH + x;
            uint8_t mask = 1u << (row % 8);
            for (int col = 0; col < width; ++col) {
                if (*pixels++)
                    dst[col] |= mask;
                else
                    dst[col] &= (uint8_t)~mask;
            }
        }
    }
    mutex_exit(&oled_lock);
}

void oled_show(void) {
    frame_pending = true;
    frame_retried = false;
}

void oled_set_contrast(uint8_t v) {
    contrast = v;
    updates |= UPDATE_CONTRAST;
}

void oled_set_vcomh(uint8_t v) {
    vcomh = v;
    updates |= UPDATE_VCOMH;
}

void oled_flip(bool flip) {
    flipped = flip;
    updates |= UPDATE_FLIP;
}

static int text_x, text_y;

static void draw_text(const char *text) {
    for (; *text; ++text) {
        char c = *text;
        if (c == '\n') {
            text_x = 0;
            text_y += blit16_HEIGHT + 1;
            continue;
        }
        if (c < 32 || c >= 32 + blit_NUM_GLYPHS)
            c = ' ';

        blit16_glyph g = blit16_Glyphs[c - 32];
        for (int y = 0; y < blit16_HEIGHT; ++y)
            for (int x = 0; x < blit16_WIDTH; ++x)
                if (g & (1 << (x + y * blit16_WIDTH)))
                    ssd1306_draw_pixel(&oled_disp, text_x + x, text_y + y);

        text_x += blit16_WIDTH + 1;
        if (text_x >= DISPLAY_WIDTH) {
            text_x = 0;
            text_y += blit16_HEIGHT + 1;
        }
    }
}

void oled_show_error(const char *message) {
    if (!oled_init())
        return;
    mutex_enter_blocking(&oled_lock);
    showing_error = true;
    transfer_state_t state;
    while ((state = transfer_state()) == TRANSFER_BUSY)
        tight_loop_contents();
    finish_transfer(state);
    if (!powered)
        ssd1306_poweron(&oled_disp);
    ssd1306_clear(&oled_disp);
    text_x = text_y = 0;
    draw_text("FATAL ERROR\n\n");
    draw_text(message);
    ssd1306_show(&oled_disp);
    mutex_exit(&oled_lock);
}

bool oled_is_powered_on(void) {
    return powered;
}

void oled_task(void) {
    if (!oled_disp.buffer || atomic_load_explicit(&showing_error, memory_order_relaxed))
        return;
    transfer_state_t state = transfer_state();
    if (state == TRANSFER_BUSY)
        return;

    uint8_t timeout = settings_get_display_timeout();
    bool should_power = !timeout || time_us_64() - last_action_time_us < (timeout + 1u) * 1000000u;
    if (state == TRANSFER_IDLE && !updates && powered == should_power && !(powered && frame_pending))
        return;
    if (!mutex_try_enter(&oled_lock, NULL))
        return;
    if (!showing_error) {
        finish_transfer(state);
        if (state == TRANSFER_ERROR && !frame_pending && !frame_retried) {
            frame_pending = true;
            frame_retried = true;
        }
        if (updates & UPDATE_CONTRAST)
            ssd1306_contrast(&oled_disp, contrast);
        if (updates & UPDATE_VCOMH)
            ssd1306_set_vcomh(&oled_disp, vcomh);
        if (updates & UPDATE_FLIP)
            ssd1306_flip_display(&oled_disp, flipped);
        updates = 0;

        if (powered != should_power) {
            if (should_power)
                ssd1306_poweron(&oled_disp);
            else
                ssd1306_poweroff(&oled_disp);
            powered = should_power;
        }

        if (powered && frame_pending) {
            frame_pending = false;
            start_frame();
        }
    }
    mutex_exit(&oled_lock);
}
