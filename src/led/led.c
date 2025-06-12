#include "led.h"
#include <stdbool.h>
#include <stdint.h>
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "ps1/ps1_mc_data_interface.h"
#include "ps2/card_emu/ps2_mc_data_interface.h"
#include "ps1/ps1_dirty.h"
#include "ps2/mmceman/ps2_mmceman_fs.h"
#include "ps2/ps2_cardman.h"
#include "ps1/ps1_cardman.h"
#include "settings.h"
#include "ws2812.pio.h"

#define LED_REFRESH_US  (250 * 1000)    // 250 ms

#ifdef WS2812
    #define WS2812_PIN      (16U)
    uint offsetWs2813;
    uint smWs2813;

void ws2812_put_pixel(uint32_t pixel_grb) {
    sleep_ms(1);    // delay to ensure LED latch will hold data
    pio_sm_put(pio1, smWs2813, pixel_grb << 8u);
}

void ws2812_put_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t mask = (green << 16) | (red << 8) | (blue << 0);
    ws2812_put_pixel(mask);
}
#else
#define LED_R           (25U)
#define LED_G           (24U)
#define LED_B           (23U)
#endif

static uint64_t last_refresh = 0;

void led_init(void) {
#if WS2812
    offsetWs2813 = pio_add_program(pio1, &ws2812_program);
    smWs2813 = pio_claim_unused_sm(pio1, true);
    ws2812_program_init(pio1, smWs2813, offsetWs2813, 16, 800000, true);
#else
    gpio_init(LED_R);
    gpio_init(LED_G);
    gpio_init(LED_B);
    gpio_set_dir(LED_R, true);
    gpio_set_dir(LED_G, true);
    gpio_set_dir(LED_B, true);
#endif
}

void led_fatal(void) {
#if WS2812
    ws2812_put_rgb(0xFF, 0x00, 0x00);
#else
    gpio_put(LED_R, true);
#endif
}

void led_clear(void) {
    #if WS2812
    ws2812_put_rgb(0x00, 0x00, 0x00);
#else
    gpio_put(LED_R, false);
    gpio_put(LED_G, false);
    gpio_put(LED_B, false);
#endif
}

void led_task(void) {
    uint64_t time = time_us_64();
    static bool red_active = false, green_active = false, blue_active = false;
    static int last_idx = 0, last_ch = 0;

    int idx = (settings_get_mode(true) == MODE_PS2) ? ps2_cardman_get_idx() : ps1_cardman_get_idx();
    int ch = (settings_get_mode(true) == MODE_PS2) ? ps2_cardman_get_channel() : ps1_cardman_get_channel();

    if ((idx != last_idx) || (ch != last_ch)) {
        red_active |= true;
        green_active |= true;
        blue_active |= true;
        last_idx = idx;
        last_ch = ch;
    } else {
        green_active |= !ps2_mmceman_fs_idle();
        blue_active |= (settings_get_mode(true) == MODE_PS2) ? ps2_mc_data_interface_write_occured() : ps1_mc_data_interface_write_occured();
    }

    if (time - last_refresh > LED_REFRESH_US) {
#if WS2812
        ws2812_put_rgb(red_active ? 0xFF : 0x00 ,
                green_active ? 0xFF : 0x00,
                blue_active ? 0xFF : 0x00);
#else
        gpio_put(LED_R, red_active);
        gpio_put(LED_B, blue_active);
        gpio_put(LED_G, green_active);
#endif

        red_active = false;
        green_active = false;
        blue_active = false;
        last_refresh = time;
    }
}
