#pragma once

#define PIN_BTN_LEFT 23
#define PIN_BTN_RIGHT 21

// SD2PSX
#ifndef UART_TX
    #define UART_TX 8
#endif
#ifndef UART_RX
    #define UART_RX 9
#endif
#ifndef UART_PERIPH
    #define UART_PERIPH uart1
#endif
#ifndef UART_BAUD
    #define UART_BAUD 3000000
#endif

//BitFunX
//#define UART_TX 0
//#define UART_RX 1
//#define UART_PERIPH uart0
//#define UART_BAUD 115200

#define OLED_I2C_SDA 28
#define OLED_I2C_SCL 25
#define OLED_I2C_ADDR 0x3C
#define OLED_I2C_PERIPH i2c0
#define OLED_I2C_CLOCK 400000

#define PSRAM_CS 2
#define PSRAM_CLK 3
#define PSRAM_DAT 4  /* IO0-IO3 must be sequential! */
#define PSRAM_CLKDIV 2

// SD2PSX
#ifndef SD_PERIPH
    #define SD_PERIPH SPI1
#endif
#ifndef SD_MISO
    #define SD_MISO 24
#endif
#ifndef SD_MOSI
    #define SD_MOSI 27
#endif
#ifndef SD_SCK
    #define SD_SCK 26
#endif
#ifndef SD_CS
    #define SD_CS 29
#endif

// BitFunX
//#define SD_PERIPH SPI
//#define SD_MISO 16
//#define SD_MOSI 19
//#define SD_SCK 18
//#define SD_CS 17

#define SD_BAUD 45 * 1000000

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

#define DEBOUNCE_MS 5
