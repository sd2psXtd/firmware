#include "led.h"
#include <stdbool.h>
#include <stdint.h>
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "ps1/ps1_mc_data_interface.h"
#include "ps2/card_emu/ps2_mc_data_interface.h"
#include "ps1/ps1_dirty.h"
#include "ps2/mmceman/ps2_mmceman_fs.h"

#define LED_R           (25U)
#define LED_G           (24U)
#define LED_B           (23U)
#define LED_REFRESH_US  (250 * 1000)    // 250 ms

static uint64_t last_refresh = 0;

void led_init(void) {
    gpio_init(LED_R);
    gpio_init(LED_G);
    gpio_init(LED_B);
    gpio_set_dir(LED_R, true);
    gpio_set_dir(LED_G, true);
    gpio_set_dir(LED_B, true);
}

void led_fatal(void) {
    gpio_put(LED_R, true);
}

void led_task(void) {
    uint64_t time = time_us_64();
    static bool red_active = false, green_active = false, blue_active = false;

    green_active |= !ps2_mmceman_fs_idle();
    blue_active |= (ps1_mc_data_interface_write_occured() || ps2_mc_data_interface_write_occured());

    if (time - last_refresh > LED_REFRESH_US) {
        gpio_put(LED_R, red_active);
        gpio_put(LED_B, blue_active);
        gpio_put(LED_G, green_active);

        red_active = false;
        green_active = false;
        blue_active = false;
        last_refresh = time;
    }


}
