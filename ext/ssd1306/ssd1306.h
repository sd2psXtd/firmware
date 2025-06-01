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

/**
* @file ssd1306.h
*
* simple driver for ssd1306 displays
*/

#ifndef _inc_ssd1306
#define _inc_ssd1306
#include <pico/stdlib.h>
#include <hardware/i2c.h>

/**
*	@brief defines commands used in ssd1306
*/
typedef enum {
    SET_CONTRAST = 0x81,
    SET_ENTIRE_ON = 0xA4,
    SET_NORM_INV = 0xA6,
    SET_DISP = 0xAE,
    SET_MEM_ADDR = 0x20,
    SET_COL_ADDR = 0x21,
    SET_PAGE_ADDR = 0x22,
    SET_DISP_START_LINE = 0x40,
    SET_SEG_REMAP = 0xA0,
    SET_MUX_RATIO = 0xA8,
    SET_COM_OUT_DIR = 0xC0,
    SET_DISP_OFFSET = 0xD3,
    SET_COM_PIN_CFG = 0xDA,
    SET_DISP_CLK_DIV = 0xD5,
    SET_PRECHARGE = 0xD9,
    SET_VCOM_DESEL = 0xDB,
    SET_CHARGE_PUMP = 0x8D
} ssd1306_command_t;

/**
*	@brief holds the configuration
*/
typedef struct {
    uint8_t width; 		/**< width of display */
    uint8_t height; 	/**< height of display */
    uint8_t pages;		/**< stores pages of display (calculated on initialization*/
    uint8_t address; 	/**< i2c address of display*/
    i2c_inst_t *i2c_i; 	/**< i2c connection instance */
    bool external_vcc; 	/**< whether display uses external vcc */
    uint16_t *buffer;	/**< display buffer */
    size_t bufsize;		/**< buffer size */
} ssd1306_t;

/**
*	@brief initialize display
*
*	@param[in] p : pointer to instance of ssd1306_t
*	@param[in] width : width of display
*	@param[in] height : heigth of display
*	@param[in] address : i2c address of display
*	@param[in] i2c_instance : instance of i2c connection
*
* 	@return bool.
*	@retval true for Success
*	@retval false if initialization failed
*/
bool ssd1306_init(ssd1306_t *p, uint16_t width, uint16_t height, uint8_t address, i2c_inst_t *i2c_instance, uint8_t contrast, uint8_t vcomh, bool flipped);

/**
*	@brief turn off display
*
*	@param[in] p : instance of display
*
*/
void ssd1306_poweroff(ssd1306_t *p);

/**
	@brief turn on display

	@param[in] p : instance of display

*/
void ssd1306_poweron(ssd1306_t *p);

/**
	@brief set contrast of display

	@param[in] p : instance of display
	@param[in] val : contrast

*/
void ssd1306_contrast(ssd1306_t *p, uint8_t val);

/**
	@brief set VCOMH deselect level of display

	@param[in] p : instance of display
	@param[in] val : vcomh

*/
void ssd1306_set_vcomh(ssd1306_t *p, uint8_t val);

/**
    @brief flip screen
    @param[in] p : instance of display
    @param[in] flip : flip screen
 */
void ssd1306_flip_display(ssd1306_t *p, bool flip);

/**
	@brief display buffer, should be called on change

	@param[in] p : instance of display

*/
void ssd1306_show(ssd1306_t *p);

/**
	@brief clear display buffer

	@param[in] p : instance of display

*/
void ssd1306_clear(ssd1306_t *p);

/**
	@brief draw pixel on buffer

	@param[in] p : instance of display
	@param[in] x : x position
	@param[in] y : y position
*/
void ssd1306_draw_pixel(ssd1306_t *p, uint32_t x, uint32_t y);

#endif
