/*

MIT License

Copyright (c) 2021 David Schramm

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <pico/stdlib.h>
#include <hardware/i2c.h>
#include <pico/binary_info.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ssd1306.h"

static bool ssd1306_write(ssd1306_t *p, const uint8_t *src, size_t len) {
    return i2c_write_timeout_us(p->i2c_i, p->address, src, len, false, 50000) == (int)len;
}

static void ssd1306_write_pair(ssd1306_t *p, uint8_t first, uint8_t second) {
    const uint8_t bytes[] = {0x00, first, second};
    ssd1306_write(p, bytes, sizeof(bytes));
}

static bool ssd1306_set_power(ssd1306_t *p, bool enabled) {
    const uint8_t bytes[] = {0x00, SET_DISP | enabled};
    return ssd1306_write(p, bytes, sizeof(bytes));
}

/* Share the blocking transfer between startup and fallback to save flash. */
static __attribute__((noinline)) bool ssd1306_flush(ssd1306_t *p) {
    uint8_t start_col = p->width == 64 ? 32 : 0;
    const uint8_t payload[] = {0x00, SET_COL_ADDR, start_col, start_col + p->width - 1,
                               SET_PAGE_ADDR, 0, p->pages - 1};
    if (!ssd1306_write(p, payload, sizeof(payload)))
        return false;
    *(p->buffer - 1) = 0x40;
    return ssd1306_write(p, p->buffer - 1, p->bufsize + 1);
}

void ssd1306_deinit(ssd1306_t *p) {
    free(p->buffer - 1);
    p->buffer = NULL;
    p->bufsize = 0;
}

bool ssd1306_init(ssd1306_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance, uint8_t contrast, uint8_t vcomh, bool flipped) {
    p->width=(uint8_t)width;
    p->height=(uint8_t)height;
    p->pages=(uint8_t)(height/8);
    p->address=(uint8_t)address;

    p->i2c_i=i2c_instance;


    p->bufsize=(p->pages)*(p->width);
    if((p->buffer=malloc(p->bufsize+1))==NULL) {
        p->bufsize=0;
        return false;
    }

    ++(p->buffer);

    // from https://github.com/makerportal/rpi-pico-ssd1306
    const uint8_t cmds[]= {
        0x00,             // command stream
        SET_DISP | 0x00,  // off
        // address setting
        SET_MEM_ADDR,
        0x00,  // horizontal
        // resolution and layout
        SET_DISP_START_LINE | 0x00,
        SET_SEG_REMAP | (flipped?0x00:0x01),  // column addr 127 mapped to SEG0
        SET_MUX_RATIO,
        height - 1,
        SET_COM_OUT_DIR | (flipped?0x00:0x08),  // scan from COM[N] to COM0
        SET_DISP_OFFSET,
        0x00,
        SET_COM_PIN_CFG,
        width>2*height?0x02:0x12,
        // timing and driving scheme
        SET_DISP_CLK_DIV,
        0x80,
        SET_PRECHARGE,
        p->external_vcc?0x22:0xF1,
        SET_VCOM_DESEL,
        vcomh,
        // display
        SET_CONTRAST,
        contrast,
        SET_ENTIRE_ON,  // output follows RAM contents
        SET_NORM_INV,  // not inverted
        // charge pump
        SET_CHARGE_PUMP,
        p->external_vcc?0x10:0x14,
    };

    /* GDDRAM contents at power-up are undefined. Clear it while output is off. */
    ssd1306_clear(p);
    if (ssd1306_write(p, cmds, sizeof(cmds)) && ssd1306_flush(p) && ssd1306_set_power(p, true))
        return true;
    ssd1306_deinit(p);
    return false;
}

inline void ssd1306_poweroff(ssd1306_t *p) {
    ssd1306_set_power(p, false);
}

inline void ssd1306_poweron(ssd1306_t *p) {
    ssd1306_set_power(p, true);
}

inline void ssd1306_contrast(ssd1306_t *p, uint8_t val) {
    ssd1306_write_pair(p, SET_CONTRAST, val);
}

inline void ssd1306_set_vcomh(ssd1306_t *p, uint8_t val) {
    ssd1306_write_pair(p, SET_VCOM_DESEL, val);
}

inline void ssd1306_flip_display(ssd1306_t *p, bool flip) {
    ssd1306_write_pair(p, SET_SEG_REMAP | (flip?0x00:0x01), SET_COM_OUT_DIR | (flip?0x00:0x08));
}

inline void ssd1306_clear(ssd1306_t *p) {
    memset(p->buffer, 0, p->bufsize);
}

void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y) {
    if(x>=p->width || y>=p->height) return;

    p->buffer[x+p->width*(y>>3)]|=0x1<<(y&0x07); // y>>3==y/8 && y&0x7==y%8
}

void ssd1306_show(ssd1306_t *p) {
    ssd1306_flush(p);
}
