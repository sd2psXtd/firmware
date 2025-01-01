#include "config.h"
#include "settings.h"
#include "ssd1306.h"

#define blit16_ARRAY_ONLY
#define blit16_NO_HELPERS
#include "blit16.h"


static ssd1306_t oled_disp = { .external_vcc = 0 };
static int oled_init_done, have_oled;
static uint64_t last_action_time_us;

void oled_update_last_action_time() {
    last_action_time_us = time_us_64();
}

int oled_init(void) {
    if (oled_init_done)
        return have_oled;
    oled_init_done = 1;

    i2c_init(OLED_I2C_PERIPH, OLED_I2C_CLOCK);
    gpio_set_function(OLED_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(OLED_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(OLED_I2C_SDA);
    gpio_pull_up(OLED_I2C_SCL);
    oled_update_last_action_time();

    have_oled = ssd1306_init(
        &oled_disp, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_I2C_ADDR, OLED_I2C_PERIPH,
        settings_get_display_contrast(), settings_get_display_vcomh(), settings_get_display_flipped()
    );

    return have_oled;
}

void oled_clear(void) {
    ssd1306_clear(&oled_disp);
}

void oled_draw_pixel(int x, int y) {
    ssd1306_draw_pixel(&oled_disp, x, y);
}

void oled_show(void) {
    ssd1306_show(&oled_disp);
}

void oled_set_contrast(uint8_t v) {
    ssd1306_contrast(&oled_disp, v);
}

void oled_set_vcomh(uint8_t v) {
    ssd1306_set_vcomh(&oled_disp, v);
}

static int text_x, text_y;

static void draw_char(char c) {
    if (c == '\n') {
        text_x = 0;
        text_y += blit16_HEIGHT + 1;
        return;
    }

    if (c < 32 || c >= 32 + blit_NUM_GLYPHS)
        c = ' ';

    blit16_glyph g = blit16_Glyphs[c - 32];
    for (int y = 0; y < blit16_HEIGHT; ++y)
        for (int x = 0; x < blit16_WIDTH; ++x)
            if (g & (1 << (x + y * blit16_WIDTH)))
                oled_draw_pixel(text_x + x, text_y + y);

    text_x += blit16_WIDTH + 1;
    if (text_x >= DISPLAY_WIDTH) {
        text_x = 0;
        text_y += blit16_HEIGHT + 1;
    }
}

void oled_draw_text(const char *s) {
    for (; *s; s++)
        draw_char(*s);
}

bool oled_is_powered_on(void) {
    return have_oled;
}

static void oled_power_off(void) {
    if (!oled_is_powered_on())
        return;

    ssd1306_poweroff(&oled_disp);
    have_oled = 0;
}

static void oled_power_on(void) {
    if (oled_is_powered_on())
        return;

    ssd1306_poweron(&oled_disp);
    have_oled = 1;
}

void oled_task(void) {
    uint8_t display_timeout = settings_get_display_timeout();
    if (!display_timeout)
        return;

    uint64_t now_us = time_us_64();
    uint64_t diff_s = (now_us - last_action_time_us) / 1000000;

    if (diff_s > display_timeout) {
        oled_power_off();
    } else {
        oled_power_on();
    }
}
