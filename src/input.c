#include "input.h"

#include <inttypes.h>
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "config.h"

#if WITH_GUI
#include "gui.h"
#include "oled.h"
#include "lvgl.h"
#endif

#define HOLD_START_MS 250
#define HOLD_END_MS 500

typedef struct {
    uint8_t pin;
    uint8_t raw;
    uint8_t state;
    uint8_t prev_state;
    uint64_t hold_start;
    uint8_t suppressed;
} btn_t;

static btn_t buttons[2] = {
    { .pin = PIN_BTN_LEFT },
    { .pin = PIN_BTN_RIGHT }
};

static int last_pressed;

static void input_scan(void) {
    static uint64_t debounce_start;
    static int debounce;

    for (int i = 0; i < 2; ++i) {
        int state = !gpio_get(buttons[i].pin);
        /* restart debounce if the pin state changed */
        if (buttons[i].raw != state) {
            debounce = 1;
            debounce_start = time_us_64();
        }
        buttons[i].raw = state;
    }

    /* if no pin changes within the debounce interval, commit these changes to pin_state */
    if (debounce && time_us_64() - debounce_start > DEBOUNCE_MS * 1000) {
        debounce = 0;
#if WITH_GUI
        if (buttons[0].raw || buttons[1].raw) {
            oled_update_last_action_time();

            /* if one of the buttons was pressed, but display is off, ignore the input and wake it up first */
            if (!oled_is_powered_on())
                return;
        }
#endif

        for (int i = 0; i < 2; ++i)
            buttons[i].state = buttons[i].raw;
    }
}

static void input_process(void) {
    uint64_t timer = time_us_64();

    /* if the button got held down during this update, start tracking the hold start timer for it */
    for (int i = 0; i < 2; ++i)
        if (buttons[i].state && !buttons[i].prev_state)
            buttons[i].hold_start = timer;

    /* if both buttons are down at once, it's a menu key */
    if (!buttons[0].suppressed && !buttons[1].suppressed) {
        if (buttons[0].state && buttons[1].state) {
            last_pressed = INPUT_KEY_MENU;
            buttons[0].suppressed = buttons[1].suppressed = 1;
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (!buttons[i].suppressed) {
            uint64_t diff = timer - buttons[i].hold_start;

            /* if the button was held longer than HOLD_END_MS it's a hold */
            if (buttons[i].state) {
                if (diff > HOLD_END_MS * 1000) {
                    if (i == 0)
                        last_pressed = INPUT_KEY_BACK;
                    else
                        last_pressed = INPUT_KEY_ENTER;
                    buttons[i].suppressed = 1;
                }
            }

            /* if the button was released faster than HOLD_START_MS it's a press */
            if (!buttons[i].state && buttons[i].prev_state) {
                if (diff < HOLD_START_MS * 1000) {
                    last_pressed = (i == 0) ? INPUT_KEY_PREV : INPUT_KEY_NEXT;
                }
            }
        }
        }

    for (int i = 0; i < 2; ++i) {
        buttons[i].prev_state = buttons[i].state;
        /* un-suppress buttons that get released */
        if (!buttons[i].state)
            buttons[i].suppressed = 0;
    }

}
#if WITH_GUI
void input_update_display(lv_obj_t *line) {
    static lv_point_t line_points[2] = { {0, DISPLAY_HEIGHT-1}, {0, DISPLAY_HEIGHT-1} };
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);

    uint64_t timer = time_us_64();

    for (int i = 0; i < 2; ++i) {
        if (!buttons[i].suppressed && buttons[i].state) {
            uint64_t diff = timer - buttons[i].hold_start;
            if (diff > HOLD_START_MS * 1000) {
                uint64_t end = (DISPLAY_WIDTH-1) * (diff - HOLD_START_MS * 1000) / ((HOLD_END_MS - HOLD_START_MS) * 1000);
                if (end > DISPLAY_WIDTH-1) end = DISPLAY_WIDTH-1;

                if (i == 1) {
                    line_points[0].x = 0;
                    line_points[1].x = end;
                } else {
                    line_points[0].x = DISPLAY_WIDTH - end - 1;
                    line_points[1].x = DISPLAY_WIDTH - 1;
                }
                lv_obj_clear_flag(line, LV_OBJ_FLAG_HIDDEN);
                lv_line_set_points(line, line_points, 2);
            }
        }
    }
}
#endif

void input_flip(void) {
    for (int i = 0; i < 2; ++i) {
        buttons[i].pin = (buttons[i].pin == PIN_BTN_LEFT) ? PIN_BTN_RIGHT : PIN_BTN_LEFT;
    }
}

void input_init(void) {
    gpio_set_dir(PIN_BTN_LEFT, 0);
    gpio_set_dir(PIN_BTN_RIGHT, 0);
    gpio_pull_up(PIN_BTN_LEFT);
    gpio_pull_up(PIN_BTN_RIGHT);
}

void input_task(void) {
    input_scan();
    input_process();
}

int input_get_pressed(void) {
    int pressed = last_pressed;
    last_pressed = 0;
    return pressed;
}

void input_flush(void) {
    last_pressed = 0;
}

int input_is_down_raw(int idx) {
    return buttons[idx].raw;
}

int input_is_any_down(void) {
    return buttons[0].state || buttons[1].state;
}
