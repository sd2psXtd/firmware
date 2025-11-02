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
#include <stdio.h>

#include "ssd1306.h"

inline static void fancy_write(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, char *name) {
    switch(i2c_write_blocking(i2c, addr, src, len, false)) {
    case PICO_ERROR_GENERIC:
        printf("[%s] addr not acknowledged!\n", name);
        break;
    case PICO_ERROR_TIMEOUT:
        printf("[%s] timeout!\n", name);
        break;
    default:
        //printf("[%s] wrote successfully %lu bytes!\n", name, len);
        break;
    }
}

inline static void ssd1306_write(ssd1306_t *p, uint8_t val) {
    uint8_t d[2]= {0x00, val};
    fancy_write(p->i2c_i, p->address, d, 2, "ssd1306_write");
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
    int8_t cmds[]= {
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
        SET_DISP | 0x01
    };

    for(size_t i=0; i<sizeof(cmds); ++i)
        ssd1306_write(p, cmds[i]);

    return true;
}

inline void ssd1306_deinit(ssd1306_t *p) {
    free(p->buffer-1);
}

inline void ssd1306_poweroff(ssd1306_t *p) {
    ssd1306_write(p, SET_DISP|0x00);
}

inline void ssd1306_poweron(ssd1306_t *p) {
    ssd1306_write(p, SET_DISP|0x01);
}

inline void ssd1306_contrast(ssd1306_t *p, uint8_t val) {
    ssd1306_write(p, SET_CONTRAST);
    ssd1306_write(p, val);
}

inline void ssd1306_set_vcomh(ssd1306_t *p, uint8_t val) {
    ssd1306_write(p, SET_VCOM_DESEL);
    ssd1306_write(p, val);
}

inline void ssd1306_flip_display(ssd1306_t *p, bool flip) {
    ssd1306_write(p, SET_SEG_REMAP | (flip?0x00:0x01));
    ssd1306_write(p, SET_COM_OUT_DIR| (flip?0x00:0x08));
}

inline void ssd1306_clear(ssd1306_t *p) {
    memset(p->buffer, 0, p->bufsize);
}

void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y) {
    if(x>=p->width || y>=p->height) return;

    p->buffer[x+p->width*(y>>3)]|=0x1<<(y&0x07); // y>>3==y/8 && y&0x7==y%8
}

void ssd1306_show(ssd1306_t *p) {
    uint8_t payload[]= {SET_COL_ADDR, 0, p->width-1, SET_PAGE_ADDR, 0, p->pages-1};
    if(p->width==64) {
        payload[1]+=32;
        payload[2]+=32;
    }

    for(size_t i=0; i<sizeof(payload); ++i)
        ssd1306_write(p, payload[i]);

    *(p->buffer-1)=0x40;

    fancy_write(p->i2c_i, p->address, p->buffer-1, p->bufsize+1, "ssd1306_show");
}
