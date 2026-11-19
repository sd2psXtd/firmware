#include "gui.h"

#include <debug.h>
#include <game_db/game_db.h>
#include <ps1/ps1_mmce.h>
#include <ps2/card_emu/ps2_mc_data_interface.h>
#include <ps2/mmceman/ps2_mmceman.h>
#include <ps2/history_tracker/ps2_history_tracker.h>
#include <src/core/lv_disp.h>
#include <src/core/lv_obj.h>
#include <src/core/lv_obj_class.h>
#include <src/core/lv_obj_style.h>
#include <src/core/lv_obj_tree.h>
#include <src/hal/lv_hal_disp.h>
#include <src/misc/lv_anim.h>
#include <src/misc/lv_style.h>
#include <src/widgets/lv_label.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "card_config.h"
#include "config.h"
#include "debug.h"
#include "hardware/timer.h"
#include "input.h"
#include "keystore.h"
#include "oled.h"
#include "ps1/ps1_cardman.h"
#include "ps1/ps1_mc_data_interface.h"
#include "ps2/ps2_cardman.h"
#include "sd.h"
#include "splash.h"
#include "settings.h"
#include "ui_menu.h"
#include "ui_theme_mono.h"
#include "version/version.h"

#if LOG_LEVEL_GUI == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_GUI, level, fmt, ##x)
#endif

static uint64_t time_screen;

/* Displays the line at the bottom for long pressing buttons */
static lv_obj_t *g_navbar, *g_progress_bar, *g_progress_text, *g_progress_pct, *g_write_indicator;

static lv_obj_t *scr_card_switch, *scr_main, *scr_splash, *scr_menu, *menu, *main_page;
static lv_obj_t *main_status, *main_capacity;
static lv_style_t style_inv, main_scroll_style;
static lv_anim_t main_scroll_animation;
static lv_obj_t *scr_main_idx_lbl, *scr_main_channel_lbl, *src_main_title_lbl, *lbl_ps1_autoboot, *lbl_ps1_game_id, *lbl_ps1_controllercombo,
    *lbl_ps2_autoboot, *lbl_ps2_cardsize, *lbl_ps2_variant, *lbl_ps2_game_id, *lbl_civ_err, *auto_off_lbl, *contrast_lbl, *vcomh_lbl, *lbl_mode, *lbl_scrn_flip, *lbl_usb_mode;

static struct {
    uint8_t value;
    lv_obj_t *selection_lbl;
} auto_off_options[6];

static struct {
    uint16_t value;
    lv_obj_t *selection_lbl;
} cardsize_options[8];

static struct {
    uint8_t value;
    uint8_t label_value;
    lv_obj_t *selection_lbl;
} contrast_options[10];

static struct {
    uint8_t value;
    char label_text[4];
    char selection_text[16];
    lv_obj_t *selection_lbl;
} vcomh_options[3];

static int have_oled;
static lv_obj_t *splash_image;
static bool usb_screen, wait_button_release;
static enum {
    UI_STATE_SPLASH,
    UI_STATE_MAIN,
    UI_STATE_MENU,
    UI_STATE_SWITCHING,
    UI_STATE_GAME_IMG
} ui_state;
static int current_progress;
static int displayed_progress = -1;
static bool refresh_gui;

#define COLOR_FG lv_color_white()
#define COLOR_BG lv_color_black()

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define PROGRESS_WIDTH (DISPLAY_WIDTH - 10)

/* Large numerals share the normal font for any non-numeric text. */
#define NUMBER(a, b, c, d, e, f, g) {a, a, b, b, c, c, d, d, e, e, f, f, g, g}
static const uint8_t number_glyphs[][14] = {
    NUMBER(0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c),
    NUMBER(0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3c),
    NUMBER(0x3c, 0x66, 0x06, 0x0c, 0x30, 0x60, 0x7e),
    NUMBER(0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c),
    NUMBER(0x0c, 0x1c, 0x2c, 0x4c, 0x7e, 0x0c, 0x0c),
    NUMBER(0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c),
    NUMBER(0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c),
    NUMBER(0x7e, 0x06, 0x0c, 0x18, 0x30, 0x30, 0x30),
    NUMBER(0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c),
    NUMBER(0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38),
    NUMBER(0x62, 0x64, 0x08, 0x10, 0x20, 0x46, 0x86),
};
#undef NUMBER

static const uint8_t *number_bitmap(const lv_font_t *font, uint32_t c) {
    (void)font;
    if (c >= '0' && c <= '9')
        return number_glyphs[c - '0'];
    return c == '%' ? number_glyphs[10] : NULL;
}

static bool number_metrics(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t c, uint32_t next) {
    (void)next;
    if (!number_bitmap(font, c))
        return false;
    *dsc = (lv_font_glyph_dsc_t){.adv_w = 11, .box_w = 8, .box_h = 14, .bpp = 1};
    return true;
}

static const lv_font_t number_font = {
    .get_glyph_dsc = number_metrics,
    .get_glyph_bitmap = number_bitmap,
    .line_height = 16,
    .fallback = LV_FONT_DEFAULT,
};

/* Private-use characters for the OLED status icons. */
#define ICON_PS1  "\xee\x80\x80"
#define ICON_PS2  "\xee\x80\x81"
#define ICON_MCA  "\xee\x80\x82"
#define ICON_CARD "\xee\x80\x83"
enum { STATUS_ICON_HEIGHT = 12, CONSOLE_ICON_WIDTH = 24, USB_ICON_WIDTH = 8, CARD_ICON_WIDTH = 10 };

/* Row-major monochrome bitmaps with three bytes per row and the left pixel in bit 7.
 * References: https://commons.wikimedia.org/wiki/File:PlayStation_logo.svg
 *             https://commons.wikimedia.org/wiki/File:PlayStation_2_logo.svg
 */
static const uint8_t console_icons[][CONSOLE_ICON_WIDTH / 8 * STATUS_ICON_HEIGHT] = {
    { /* PlayStation symbol */
        0x00, 0x30, 0x00,
        0x00, 0x3e, 0x00,
        0x00, 0x3b, 0x80,
        0x00, 0x3b, 0x80,
        0x00, 0x3b, 0x80,
        0x00, 0x3b, 0x80,
        0x00, 0x3b, 0x00,
        0x01, 0xb9, 0xc0,
        0x0f, 0x3f, 0x38,
        0x1c, 0xb8, 0xf0,
        0x03, 0xbf, 0xc0,
        0x00, 0x1c, 0x00,
    },
    { /* PlayStation 2 symbol */
        0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
        0xff, 0x1e, 0xff,
        0x01, 0x10, 0x01,
        0x01, 0x10, 0x01,
        0xff, 0x10, 0xff,
        0x80, 0x10, 0x80,
        0x80, 0x10, 0x80,
        0x81, 0xf0, 0xff,
        0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
    },
};
/* USB 2.0 trident, rotated upright and downscaled with one byte per row.
 * Reference: https://commons.wikimedia.org/wiki/File:USB_icon.svg
 */
static const uint8_t usb_icon[USB_ICON_WIDTH / 8 * STATUS_ICON_HEIGHT] = {
    0x10, 0x38, 0x16, 0x16,
    0xd4, 0xdc, 0x58, 0x70,
    0x10, 0x10, 0x38, 0x38,
};

/* Four 10-bit rows per five bytes, packed from the top left. */
static const uint8_t card_icon[CARD_ICON_WIDTH * STATUS_ICON_HEIGHT / 8] = {
    0x7f, 0xa3, 0x19, 0xe6, 0x01,
    0x9e, 0x60, 0x18, 0x06, 0x01,
    0x80, 0x60, 0x18, 0x05, 0xfe,
};

static const uint8_t *status_bitmap(const lv_font_t *font, uint32_t c) {
    (void)font;
    if (c >= 0xe000 && c <= 0xe001)
        return console_icons[c - 0xe000];
    if (c == 0xe002)
        return usb_icon;
    return c == 0xe003 ? card_icon : NULL;
}

static bool status_metrics(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t c, uint32_t next) {
    (void)next;
    if (c == ' ') {
        *dsc = (lv_font_glyph_dsc_t){.adv_w = 4};
        return true;
    }
    if (!status_bitmap(font, c))
        return false;
    uint8_t width = USB_ICON_WIDTH;
    if (c <= 0xe001)
        width = CONSOLE_ICON_WIDTH;
    else if (c == 0xe003)
        width = CARD_ICON_WIDTH;
    *dsc = (lv_font_glyph_dsc_t){
        .adv_w = width + 2,
        .box_w = width,
        .box_h = STATUS_ICON_HEIGHT,
        .ofs_y = -2,
        .bpp = 1,
    };
    return true;
}

static const lv_font_t status_font = {
    .get_glyph_dsc = status_metrics,
    .get_glyph_bitmap = status_bitmap,
    .line_height = STATUS_ICON_HEIGHT,
    .base_line = 2,
    .fallback = LV_FONT_DEFAULT,
};


static void ui_goto_screen(lv_obj_t *scr) {
    if (lv_scr_act() != scr) {
        UI_GOTO_SCREEN(scr);
        time_screen = time_us_64();
    }
}

static lv_obj_t *ui_scr_create(void) {
    lv_obj_t *obj = lv_obj_create(NULL);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_group_add_obj(lv_group_get_default(), obj);
    return obj;
}

/* create a navigatable UI menu container, so that the item (label) inside can be selected and clicked */
static lv_obj_t *ui_menu_cont_create_nav(lv_obj_t *parent) {
    lv_obj_t *cont = ui_menu_cont_create(parent);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_group_add_obj(lv_group_get_default(), cont);
    return cont;
}

static lv_obj_t *ui_menu_subpage_create(lv_obj_t *menu, const char *title) {
    lv_obj_t *page = ui_menu_page_create(menu, title);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_add_event_cb(page, evt_menu_page, LV_EVENT_KEY, page);
    return page;
}

static lv_obj_t *ui_label_create(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text_static(label, text ? text : "");
    return label;
}

static lv_obj_t *ui_label_create_value(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *ui_label_create_at(lv_obj_t *parent, int x, int y, const char *text) {
    lv_obj_t *label = ui_label_create(parent, text);
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, (lv_coord_t)x, (lv_coord_t)y);
    return label;
}

static lv_obj_t *ui_label_create_grow(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = ui_label_create(parent, text);
    lv_obj_set_flex_grow(label, 1);
    return label;
}

static void scrollable_label(lv_event_t *event) {
    if (event->code == LV_EVENT_FOCUSED)
        lv_label_set_long_mode(event->user_data, LV_LABEL_LONG_SCROLL);
    else
        lv_label_set_long_mode(event->user_data, LV_LABEL_LONG_CLIP);
}

static void ui_make_scrollable(lv_obj_t *cont, lv_obj_t *label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_add_event_cb(cont, scrollable_label, LV_EVENT_FOCUSED, label);
    lv_obj_add_event_cb(cont, scrollable_label, LV_EVENT_DEFOCUSED, label);
}

static lv_obj_t *ui_label_create_grow_scroll(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = ui_label_create_grow(parent, text);
    ui_make_scrollable(parent, label);
    return label;
}

static lv_obj_t *ui_band(lv_obj_t *parent, int y, const char *text) {
    lv_obj_t *label = ui_label_create_at(parent, 0, y, text);
    lv_obj_add_style(label, &style_inv, 0);
    lv_obj_set_size(label, DISPLAY_WIDTH, 12);
    lv_obj_set_style_pad_top(label, 2, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static void update_bar(void) {
    int progress = LV_CLAMP(0, current_progress, 100);
    if (progress == displayed_progress)
        return;
    displayed_progress = progress;
    lv_obj_set_width(g_progress_bar, PROGRESS_WIDTH * progress / 100);
    lv_label_set_text_fmt(g_progress_pct, "%d%%", progress);
    lv_label_set_text(g_progress_text, ps2_cardman_get_progress_text());
}

static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (have_oled) {
        oled_write_pixels(area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area), (const uint8_t *)color_p);
        if (lv_disp_flush_is_last(disp_drv))
            oled_show();
    }
    lv_disp_flush_ready(disp_drv);
}

static void keypad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int pressed;

    data->state = LV_INDEV_STATE_RELEASED;
    if (usb_screen || wait_button_release) {
        input_flush();
        if (!usb_screen && !input_is_any_down()) wait_button_release = false;
        return;
    }
    if (!oled_is_powered_on())
        return;

    data->state = LV_INDEV_STATE_RELEASED;

    pressed = input_get_pressed();
    if (pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = (lv_key_t)pressed;
    }
}

static void create_nav(void) {
    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 1);
    lv_style_set_line_color(&style_line, COLOR_FG);

    g_navbar = lv_line_create(lv_layer_top());
    lv_obj_add_style(g_navbar, &style_line, 0);
}

static void gui_tick(void) {
    static uint64_t prev_time;
    static uint32_t delay = 0;

    if (!prev_time)
        prev_time = time_us_64();
    uint64_t now_time = time_us_64();
    uint32_t diff_ms = (uint32_t)((now_time - prev_time) / 1000);

    if (diff_ms > delay) {
        prev_time = now_time;
        lv_tick_inc(diff_ms);
        delay = lv_timer_handler();
    } else {
        // log(LOG_TRACE, "%s delay %u not reached\n", __func__, delay);
    }
}

static void reload_card_cb(int progress, bool done) {
    current_progress = progress;
    if (done) {
        ps2_cardman_set_progress_cb(NULL);
        input_flush();
        refresh_gui = true;
        if (ui_state != UI_STATE_SPLASH)
            ui_state = UI_STATE_MAIN;
    } else if (time_us_64() > GUI_SCREEN_IMAGE_TIMEOUT_US){
        ui_state = UI_STATE_SWITCHING;
    }
}

static void ui_set_display_timeout(uint8_t display_timeout) {
    char text[8];

    if (auto_off_options[0].value == display_timeout) {
        snprintf(text, sizeof(text), "Off >"), lv_label_set_text(auto_off_lbl, text);

        snprintf(text, sizeof(text), "> Off"), lv_label_set_text(auto_off_options[0].selection_lbl, text);
    } else {
        snprintf(text, sizeof(text), "  Off"), lv_label_set_text(auto_off_options[0].selection_lbl, text);
    }

    for (size_t i = 1; i < ARRAY_SIZE(auto_off_options); i++) {
        if (auto_off_options[i].value == display_timeout) {
            snprintf(text, sizeof(text), "%hhus >", auto_off_options[i].value), lv_label_set_text(auto_off_lbl, text);

            snprintf(text, sizeof(text), "> %hhus", auto_off_options[i].value), lv_label_set_text(auto_off_options[i].selection_lbl, text);
        } else {
            snprintf(text, sizeof(text), "  %hhus", auto_off_options[i].value), lv_label_set_text(auto_off_options[i].selection_lbl, text);
        }
    }
}

static void ui_set_display_contrast(uint8_t display_contrast) {
    char text[8];

    for (size_t i = 0; i < ARRAY_SIZE(contrast_options); i++) {
        if (contrast_options[i].value == display_contrast) {
            snprintf(text, sizeof(text), "%hhu%% >", contrast_options[i].label_value), lv_label_set_text(contrast_lbl, text);

            snprintf(text, sizeof(text), "> %hhu%%", contrast_options[i].label_value), lv_label_set_text(contrast_options[i].selection_lbl, text);
        } else {
            snprintf(text, sizeof(text), "  %hhu%%", contrast_options[i].label_value), lv_label_set_text(contrast_options[i].selection_lbl, text);
        }
    }
}

static void ui_set_cardsize(void) {
    for (size_t i = 0; i < ARRAY_SIZE(cardsize_options); i++) {
        uint8_t value = cardsize_options[i].value;
        char text[10] = {};
        if (value <= 8)
            snprintf(text, ARRAY_SIZE(text), "%c %u MB", settings_get_ps2_cardsize() == value ? '>' : ' ', value);
        else
            snprintf(text, ARRAY_SIZE(text), "%c %u MB*", settings_get_ps2_cardsize() == value ? '>' : ' ', value);
        lv_label_set_text(cardsize_options[i].selection_lbl, text);
    }
}

static void ui_set_display_vcomh(uint8_t display_vcomh) {
    char text[16];

    for (size_t i = 0; i < ARRAY_SIZE(vcomh_options); i++) {
        if (vcomh_options[i].value == display_vcomh) {
            snprintf(text, sizeof(text), "%s >", vcomh_options[i].label_text), lv_label_set_text(vcomh_lbl, text);

            snprintf(text, sizeof(text), "> %s", vcomh_options[i].selection_text), lv_label_set_text(vcomh_options[i].selection_lbl, text);
        } else {
            snprintf(text, sizeof(text), "  %s", vcomh_options[i].selection_text), lv_label_set_text(vcomh_options[i].selection_lbl, text);
        }
    }
}

static void evt_scr_main(lv_event_t *event) {
    if (event->code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        log(LOG_INFO, "main screen got key %d\n", (int)key);
        if (key == INPUT_KEY_MENU) {
            log(LOG_INFO, "activate menu!\n");
            lv_scr_load(scr_menu);
            ui_menu_set_page(menu, NULL);
            ui_menu_set_page(menu, main_page);
            lv_event_stop_bubbling(event);
            ui_state = UI_STATE_MENU;
        }
        if (key == INPUT_KEY_PREV || key == INPUT_KEY_NEXT || key == INPUT_KEY_BACK || key == INPUT_KEY_ENTER) {
            if (ui_state == UI_STATE_GAME_IMG) {
                ui_state = UI_STATE_MAIN;
            } else {
                if (settings_get_mode(true) == MODE_PS1) {
                    switch (key) {
                        case INPUT_KEY_PREV: ps1_mmce_prev_ch(true); break;
                        case INPUT_KEY_NEXT: ps1_mmce_next_ch(true); break;
                        case INPUT_KEY_BACK: ps1_mmce_prev_idx(true); break;
                        case INPUT_KEY_ENTER: ps1_mmce_next_idx(true); break;
                    }
                } else {
                    switch (key) {
                        case INPUT_KEY_PREV: ps2_mmceman_prev_ch(true); break;
                        case INPUT_KEY_NEXT: ps2_mmceman_next_ch(true); break;
                        case INPUT_KEY_BACK: ps2_mmceman_prev_idx(true); break;
                        case INPUT_KEY_ENTER: ps2_mmceman_next_idx(true); break;
                    }
                }
                ui_state = UI_STATE_MAIN;
            }

        }
        time_screen = time_us_64();
    }
}

static void evt_scr_menu(lv_event_t *event) {
    if (event->code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        log(LOG_INFO, "menu screen got key %d\n", (int)key);
        if (key == INPUT_KEY_BACK || key == INPUT_KEY_MENU) {
            ui_state = UI_STATE_MAIN;
            lv_event_stop_bubbling(event);
        }
        time_screen = time_us_64();
    }
}

void evt_menu_page(lv_event_t *event) {
    if (event->code == LV_EVENT_KEY) {
        lv_obj_t *page = event->user_data;
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        lv_obj_t *cur = lv_group_get_focused(lv_group_get_default());
        if (lv_obj_get_parent(cur) != page)
            return;
        int idx = (int)lv_obj_get_index(cur);
        int count = (int)lv_obj_get_child_cnt(page);
        if (key == INPUT_KEY_PREV || key == INPUT_KEY_NEXT) {
            lv_obj_t *next = key == INPUT_KEY_PREV
                ? ui_menu_find_prev_focusable(page, (idx + count - 1) % count)
                : ui_menu_find_next_focusable(page, (idx + 1) % count);
            lv_group_focus_obj(next);
            lv_obj_scroll_to_view(next, LV_ANIM_OFF);
            lv_event_stop_bubbling(event);
        } else if (key == INPUT_KEY_ENTER) {
            lv_event_send(cur, LV_EVENT_CLICKED, NULL);
            lv_event_stop_bubbling(event);
        } else if (key == INPUT_KEY_BACK) {
            /* going back from the root page - let it handle in evt_scr_menu */
            if (ui_menu_get_cur_main_page(menu) == main_page)
                return;
            ui_menu_go_back(menu);
            lv_obj_scroll_to_y(page, 0, false);  // reset scroll on the way out
            lv_event_stop_bubbling(event);
        } else if (key == INPUT_KEY_MENU) {
            lv_obj_scroll_to_y(page, 0, false);  // reset scroll on the way out
        }
    }
}

static void update_main_status(void) {
    if (!main_status)
        return;

    int mode = settings_get_mode(true);
    const char *console = ICON_PS1;
    if (mode == MODE_PS2) {
        switch (settings_get_ps2_variant()) {
            case PS2_VARIANT_PROTO: console = "PRO"; break;
            case PS2_VARIANT_COH: console = "COH"; break;
            case PS2_VARIANT_SC2: console = "SC2"; break;
            default: console = ICON_PS2; break;
        }
    }
    if (mode == MODE_PS2 && !ps2_magicgate)
        lv_label_set_text_fmt(main_status, "%s !CIV", console);
    else
        lv_label_set_text_static(main_status, console);

    bool mca = settings_get_mca_enabled();
    const char *icon = mca ? ICON_MCA : ICON_CARD;
    uint32_t size = mode == MODE_PS2 ? ps2_cardman_get_card_size() : 128u * 1024;
    if (size >= PS2_CARD_SIZE_1G)
        lv_label_set_text_fmt(main_capacity, "%s%uGB", icon, (unsigned)(size >> 30));
    else if (size >= PS2_CARD_SIZE_1M)
        lv_label_set_text_fmt(main_capacity, "%s%uMB", icon, (unsigned)(size >> 20));
    else if (size)
        lv_label_set_text_fmt(main_capacity, "%s%uKB", icon, (unsigned)(size >> 10));
    else
        lv_label_set_text_static(main_capacity, mca ? ICON_MCA "--" : ICON_CARD "--");
}

static void evt_go_back(lv_event_t *event) {
    ui_menu_go_back(menu);
    lv_event_stop_bubbling(event);
}

static void evt_screen_flip(lv_event_t *event) {
    bool current = settings_get_display_flipped();
    settings_set_display_flipped(!current);
    oled_flip(!current);
    input_flip();
    lv_label_set_text(lbl_scrn_flip, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_ps1_autoboot(lv_event_t *event) {
    bool current = settings_get_ps1_autoboot();
    settings_set_ps1_autoboot(!current);
    lv_label_set_text(lbl_ps1_autoboot, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_ps1_gameid(lv_event_t *event) {
    bool current = settings_get_ps1_game_id();
    settings_set_ps1_game_id(!current);
    lv_label_set_text(lbl_ps1_game_id, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_ps1_controllercombo(lv_event_t *event) {
    bool current = settings_get_ps1_controllercombo();
    settings_set_ps1_controllercombo(!current);
    lv_label_set_text(lbl_ps1_controllercombo, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_ps2_autoboot(lv_event_t *event) {
    bool current = settings_get_ps2_autoboot();
    settings_set_ps2_autoboot(!current);
    lv_label_set_text(lbl_ps2_autoboot, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_ps2_gameid(lv_event_t *event) {
    bool current = settings_get_ps2_game_id();
    settings_set_ps2_game_id(!current);
    lv_label_set_text(lbl_ps2_game_id, !current ? "Yes" : "No");
    lv_event_stop_bubbling(event);
}

static void evt_set_ps2_cardsize(lv_event_t *event) {
    uint8_t cardsize = (uint8_t)(intptr_t)event->user_data;
    settings_set_ps2_cardsize(cardsize);

    char text[10] = {};
    if (cardsize <= 8)
        snprintf(text, ARRAY_SIZE(text), "%u MB>", cardsize);
    else
        snprintf(text, ARRAY_SIZE(text), "%u MB*>", cardsize);
    lv_label_set_text(lbl_ps2_cardsize, text);
    ui_set_cardsize();
    ui_menu_go_back(menu);
}

static void evt_do_civ_deploy(lv_event_t *event) {
    (void)event;

    int ret = keystore_deploy();
    if (ret == 0) {
        lv_label_set_text(lbl_civ_err, "Success!\n");
    } else {
        lv_label_set_text(lbl_civ_err, keystore_error(ret));
    }
    refresh_gui = true;
}

static void evt_switch_to_ps1(lv_event_t *event) {
    (void)event;

    settings_set_mode(MODE_PS1);
    lv_label_set_text(lbl_mode, "PS1");
    gui_request_refresh();

    /* start at the main screen */
    ui_state = UI_STATE_MAIN;
}

static void evt_switch_variant(lv_event_t *event) {
    (void)event;
    int variant = (intptr_t)event->user_data;

    ps2_cardman_set_variant(variant);

    update_main_status();

    {
        if (settings_get_ps2_variant() == PS2_VARIANT_RETAIL)
            lv_label_set_text(lbl_ps2_variant, "Retail>");
        else if (settings_get_ps2_variant() == PS2_VARIANT_PROTO)
            lv_label_set_text(lbl_ps2_variant, "Proto>");
        else if (settings_get_ps2_variant() == PS2_VARIANT_COH)
            lv_label_set_text(lbl_ps2_variant, "Arcade>");
        else if (settings_get_ps2_variant() == PS2_VARIANT_SC2)
            lv_label_set_text(lbl_ps2_variant, "Conquest>");
    }

    gui_request_refresh();


    /* start at the main screen */
    ui_state = UI_STATE_MAIN;
}

static void evt_switch_to_ps2(lv_event_t *event) {
    (void)event;

    settings_set_mode(MODE_PS2);
    lv_label_set_text(lbl_mode, "PS2");
    gui_request_refresh();
    keystore_init();

    update_main_status();

    /* start at the main screen */
    ui_state = UI_STATE_MAIN;
}

static void evt_set_usb_mode(lv_event_t *event) {
    settings_set_usb_mode((intptr_t)event->user_data);
    gui_mca_mode_changed();
    ui_menu_go_back(menu);
    lv_event_stop_bubbling(event);
}

void gui_mca_mode_changed(void) {
    if (lbl_usb_mode)
        lv_label_set_text(lbl_usb_mode, settings_get_mca_enabled() ? "MCA>" : "Serial>");
    update_main_status();
    gui_request_refresh();
}

static void evt_set_display_timeout(lv_event_t *event) {
    uint8_t display_timeout = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_timeout(display_timeout);
    ui_set_display_timeout(display_timeout);
}

static void evt_set_display_contrast(lv_event_t *event) {
    uint8_t display_contrast = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_contrast(display_contrast);
    oled_set_contrast(display_contrast);
    ui_set_display_contrast(display_contrast);
}

static void evt_set_display_vcomh(lv_event_t *event) {
    uint8_t display_vcomh = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_vcomh(display_vcomh);
    oled_set_vcomh(display_vcomh);
    ui_set_display_vcomh(display_vcomh);
}


static lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, COLOR_FG, 0);
    return panel;
}

static lv_obj_t *ui_panel_label(lv_obj_t *panel, int y, const char *text) {
    lv_obj_t *label = ui_label_create(panel, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static void create_main_screen(void) {
    scr_main = ui_scr_create();
    lv_obj_add_event_cb(scr_main, evt_scr_main, LV_EVENT_ALL, NULL);
    main_status = ui_label_create_at(scr_main, 2, 1, "");
    main_capacity = ui_label_create(scr_main, "");
    lv_obj_set_style_text_font(main_status, &status_font, 0);
    lv_obj_set_style_text_font(main_capacity, &status_font, 0);
    lv_obj_align(main_capacity, LV_ALIGN_TOP_RIGHT, -2, 1);
    update_main_status();

    lv_obj_t *card = ui_panel(scr_main, 1, 14, 85, 29);
    lv_obj_t *channel = ui_panel(scr_main, 88, 14, 39, 29);
    ui_panel_label(card, 2, "Card");
    ui_panel_label(channel, 2, "Chan");

    lv_anim_init(&main_scroll_animation);
    lv_anim_set_delay(&main_scroll_animation, 1000);
    lv_style_init(&main_scroll_style);
    lv_style_set_anim(&main_scroll_style, &main_scroll_animation);

    scr_main_idx_lbl = ui_panel_label(card, 13, "");
    lv_label_set_long_mode(scr_main_idx_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_style(scr_main_idx_lbl, &main_scroll_style, 0);

    scr_main_channel_lbl = ui_panel_label(channel, 13, "");

    src_main_title_lbl = ui_label_create_at(scr_main, 2, 44, "");
    lv_label_set_long_mode(src_main_title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(src_main_title_lbl, DISPLAY_WIDTH - 4);
    lv_obj_add_style(src_main_title_lbl, &main_scroll_style, 0);

    ui_label_create_at(scr_main, 2, 54, "< Ch");
    ui_label_create_at(scr_main, 48, 54, "Menu");
    ui_label_create_at(scr_main, 94, 54, "Ch >");

    g_write_indicator = lv_obj_create(scr_main);
    lv_obj_remove_style_all(g_write_indicator);
    lv_obj_set_pos(g_write_indicator, 79, 18);
    lv_obj_set_size(g_write_indicator, 4, 4);
    lv_obj_set_style_bg_color(g_write_indicator, COLOR_FG, 0);
    lv_obj_set_style_bg_opa(g_write_indicator, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_write_indicator, LV_OBJ_FLAG_HIDDEN);
}

static void create_cardswitch_screen(void) {
    scr_card_switch = ui_scr_create();
    ui_band(scr_card_switch, 0, "Loading card");

    g_progress_pct = ui_label_create_at(scr_card_switch, 4, 15, "0%");
    lv_obj_set_style_text_font(g_progress_pct, &number_font, 0);

    ui_panel(scr_card_switch, 3, 36, DISPLAY_WIDTH - 6, 11);
    g_progress_bar = lv_obj_create(scr_card_switch);
    lv_obj_remove_style_all(g_progress_bar);
    lv_obj_set_pos(g_progress_bar, 5, 38);
    lv_obj_set_size(g_progress_bar, 0, 7);
    lv_obj_set_style_bg_color(g_progress_bar, COLOR_FG, 0);
    lv_obj_set_style_bg_opa(g_progress_bar, LV_OPA_COVER, 0);

    g_progress_text = ui_label_create_at(scr_card_switch, 4, 51, "");
}

static void evt_usb_storage(lv_event_t *event) {
    (void)event;
    if (settings_get_usb_mode(true) == USB_MODE_MASS) return;
    settings_set_usb_mode(USB_MODE_MASS);
    gui_usb_storage(true);
}

static void create_menu_screen(void) {
    /* Menu screen accessible by pressing the menu button at main */
    scr_menu = ui_scr_create();
    lv_obj_add_event_cb(scr_menu, evt_scr_menu, LV_EVENT_ALL, NULL);

    menu = ui_menu_create(scr_menu);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(menu, DISPLAY_WIDTH, DISPLAY_HEIGHT - 2);
    lv_obj_t *header = ui_menu_get_main_header(menu);
    lv_obj_set_height(header, 12);

    lv_obj_t *cont;

    /* deploy submenu */
    lv_obj_t *mode_page = ui_menu_subpage_create(menu, "Mode");
    {
        lv_obj_t *ps2_switch_warn = ui_menu_subpage_create(menu, "Switch to PS2");
        {
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create_grow(cont, "PS2 mode can\ndamage a PS1.");

            cont = ui_menu_cont_create_nav(ps2_switch_warn);
            ui_label_create(cont, "Confirm");
            lv_obj_add_event_cb(cont, evt_switch_to_ps2, LV_EVENT_CLICKED, NULL);

            cont = ui_menu_cont_create_nav(ps2_switch_warn);
            ui_label_create(cont, "Back");
            lv_obj_add_event_cb(cont, evt_go_back, LV_EVENT_CLICKED, NULL);
        }

        cont = ui_menu_cont_create_nav(mode_page);
        ui_label_create(cont, "PS1");
        lv_obj_add_event_cb(cont, evt_switch_to_ps1, LV_EVENT_CLICKED, NULL);

        cont = ui_menu_cont_create_nav(mode_page);
        ui_label_create(cont, "PS2");
        ui_menu_set_load_page_event(menu, cont, ps2_switch_warn);
    }

    /* display / auto off submenu */
    lv_obj_t *auto_off_page = ui_menu_subpage_create(menu, "Auto off");
    {
        auto_off_options[0].value = 0;
        auto_off_options[1].value = 5;
        auto_off_options[2].value = 15;
        auto_off_options[3].value = 30;
        auto_off_options[4].value = 60;
        auto_off_options[5].value = 120;

        for (size_t i = 0; i < ARRAY_SIZE(auto_off_options); i++) {
            uint8_t value = auto_off_options[i].value;

            cont = ui_menu_cont_create_nav(auto_off_page);
            auto_off_options[i].selection_lbl = ui_label_create_grow(cont, NULL);
            lv_obj_add_event_cb(cont, evt_set_display_timeout, LV_EVENT_CLICKED, (void *)(intptr_t)value);
        }
    }

    /* display / contrast submenu */
    lv_obj_t *contrast_page = ui_menu_subpage_create(menu, "Contrast");
    {

        for (size_t i = 0; i < ARRAY_SIZE(contrast_options); i++) {
            uint8_t percentage = (uint8_t)((i + 1) * 10);
            uint8_t value = (uint8_t)((255 * percentage) / 100);
            contrast_options[i].value = value;
            contrast_options[i].label_value = percentage;

            cont = ui_menu_cont_create_nav(contrast_page);
            contrast_options[i].selection_lbl = ui_label_create_grow(cont, NULL);
            lv_obj_add_event_cb(cont, evt_set_display_contrast, LV_EVENT_CLICKED, (void *)(intptr_t)value);
        }
    }

    /* display / vcomh submenu */
    lv_obj_t *vcomh_page = ui_menu_subpage_create(menu, "VCOMH");
    {
        vcomh_options[0].value = 0x00;
        vcomh_options[1].value = 0x20;
        vcomh_options[2].value = 0x30;

        strcpy(vcomh_options[0].label_text, "00h");
        strcpy(vcomh_options[1].label_text, "20h");
        strcpy(vcomh_options[2].label_text, "30h");

        strcpy(vcomh_options[0].selection_text, "0.65 x VCC");
        strcpy(vcomh_options[1].selection_text, "0.77 x VCC");
        strcpy(vcomh_options[2].selection_text, "0.83 x VCC");

        for (size_t i = 0; i < ARRAY_SIZE(vcomh_options); i++) {
            cont = ui_menu_cont_create_nav(vcomh_page);
            vcomh_options[i].selection_lbl = ui_label_create_grow(cont, vcomh_options[i].label_text);
            lv_obj_add_event_cb(cont, evt_set_display_vcomh, LV_EVENT_CLICKED, (void *)(intptr_t)vcomh_options[i].value);
        }
    }

    /* display config */
    lv_obj_t *display_page = ui_menu_subpage_create(menu, "Display");
    {
        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Auto off");
        auto_off_lbl = ui_label_create(cont, NULL);
        ui_menu_set_load_page_event(menu, cont, auto_off_page);
        ui_set_display_timeout(settings_get_display_timeout());

        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Contrast");
        contrast_lbl = ui_label_create(cont, NULL);
        ui_menu_set_load_page_event(menu, cont, contrast_page);
        ui_set_display_contrast(settings_get_display_contrast());

        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "VCOMH");
        vcomh_lbl = ui_label_create(cont, NULL);
        ui_menu_set_load_page_event(menu, cont, vcomh_page);
        ui_set_display_vcomh(settings_get_display_vcomh());

        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Flip");
        lbl_scrn_flip = ui_label_create(cont, settings_get_display_flipped() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_screen_flip, LV_EVENT_CLICKED, NULL);
    }

    /* ps1 */
    lv_obj_t *ps1_page = ui_menu_subpage_create(menu, "PS1 Settings");
    {
        cont = ui_menu_cont_create_nav(ps1_page);
        ui_label_create_grow_scroll(cont, "Autoboot");
        lbl_ps1_autoboot = ui_label_create(cont, settings_get_ps1_autoboot() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_ps1_autoboot, LV_EVENT_CLICKED, NULL);

        cont = ui_menu_cont_create_nav(ps1_page);
        ui_label_create_grow_scroll(cont, "Game ID");
        lbl_ps1_game_id = ui_label_create(cont, settings_get_ps1_game_id() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_ps1_gameid, LV_EVENT_CLICKED, NULL);

        cont = ui_menu_cont_create_nav(ps1_page);
        ui_label_create_grow_scroll(cont, "Combos");
        lbl_ps1_controllercombo = ui_label_create(cont, settings_get_ps1_controllercombo() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_ps1_controllercombo, LV_EVENT_CLICKED, NULL);
    }

    /* ps2 */
    lv_obj_t *ps2_page = ui_menu_subpage_create(menu, "PS2 Settings");
    {
        /* deploy submenu */
        lv_obj_t *civ_page = ui_menu_subpage_create(menu, "Deploy CIV.bin");
        {
            cont = ui_menu_cont_create(civ_page);
            lbl_civ_err = ui_label_create(cont, "");

            cont = ui_menu_cont_create_nav(civ_page);
            ui_label_create(cont, "Back");
            lv_obj_add_event_cb(cont, evt_go_back, LV_EVENT_CLICKED, NULL);
        }

        /* cardsize submenu */
        lv_obj_t *cardsize_page = ui_menu_subpage_create(menu, "Default Size");
        {
            cardsize_options[0].value = 1;
            cardsize_options[1].value = 2;
            cardsize_options[2].value = 4;
            cardsize_options[3].value = 8;
            cardsize_options[4].value = 16;
            cardsize_options[5].value = 32;
            cardsize_options[6].value = 64;
            cardsize_options[7].value = 128;

            for (size_t i = 0; i < ARRAY_SIZE(cardsize_options); i++) {
                uint8_t value = cardsize_options[i].value;
                cont = ui_menu_cont_create_nav(cardsize_page);
                cardsize_options[i].selection_lbl = ui_label_create_grow(cont, NULL);
                lv_obj_add_event_cb(cont, evt_set_ps2_cardsize, LV_EVENT_CLICKED, (void *)(intptr_t)value);
            }
        }

        ui_set_cardsize();

        /* Variant submenu */
        lv_obj_t *variant_page = ui_menu_subpage_create(menu, "Variant");
        {
            cont = ui_menu_cont_create_nav(variant_page);
            ui_label_create(cont, "Retail");
            lv_obj_add_event_cb(cont, evt_switch_variant, LV_EVENT_CLICKED, (void*)(intptr_t)PS2_VARIANT_RETAIL);

            cont = ui_menu_cont_create_nav(variant_page);
            ui_label_create(cont, "Proto");
            lv_obj_add_event_cb(cont, evt_switch_variant, LV_EVENT_CLICKED, (void*)(intptr_t)PS2_VARIANT_PROTO);

            cont = ui_menu_cont_create_nav(variant_page);
            ui_label_create(cont, "Arcade");
            lv_obj_add_event_cb(cont, evt_switch_variant, LV_EVENT_CLICKED, (void*)(intptr_t)PS2_VARIANT_COH);

            cont = ui_menu_cont_create_nav(variant_page);
            ui_label_create(cont, "Conquest");
            lv_obj_add_event_cb(cont, evt_switch_variant, LV_EVENT_CLICKED, (void*)(intptr_t)PS2_VARIANT_SC2);
        }
        {
            char text[10] = {};
            if (settings_get_ps2_variant() == PS2_VARIANT_RETAIL)
                snprintf(text, ARRAY_SIZE(text), "Retail>");
            else if (settings_get_ps2_variant() == PS2_VARIANT_PROTO)
                snprintf(text, ARRAY_SIZE(text), "Proto>");
            else if (settings_get_ps2_variant() == PS2_VARIANT_COH)
                snprintf(text, ARRAY_SIZE(text), "Arcade>");
            else if (settings_get_ps2_variant() == PS2_VARIANT_SC2)
                snprintf(text, ARRAY_SIZE(text), "Conquest>");
            cont = ui_menu_cont_create_nav(ps2_page);
            ui_label_create_grow(cont, "Variant");
            lbl_ps2_variant = ui_label_create_value(cont, text);
            ui_menu_set_load_page_event(menu, cont, variant_page);
        }


        cont = ui_menu_cont_create_nav(ps2_page);
        ui_label_create_grow_scroll(cont, "Autoboot");
        lbl_ps2_autoboot = ui_label_create(cont, settings_get_ps2_autoboot() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_ps2_autoboot, LV_EVENT_CLICKED, NULL);

        cont = ui_menu_cont_create_nav(ps2_page);
        ui_label_create_grow_scroll(cont, "Game ID");
        lbl_ps2_game_id = ui_label_create(cont, settings_get_ps2_game_id() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_ps2_gameid, LV_EVENT_CLICKED, NULL);

        cont = ui_menu_cont_create_nav(ps2_page);
        ui_label_create_grow(cont, "Deploy CIV.bin");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, civ_page);
        lv_obj_add_event_cb(cont, evt_do_civ_deploy, LV_EVENT_CLICKED, NULL);

        {
            char text[10] = {};
            if (settings_get_ps2_cardsize() <= 8)
                snprintf(text, ARRAY_SIZE(text), "%u MB>", settings_get_ps2_cardsize());
            else
                snprintf(text, ARRAY_SIZE(text), "%u MB*>", settings_get_ps2_cardsize());
            cont = ui_menu_cont_create_nav(ps2_page);
            ui_label_create_grow(cont, "Size");
            lbl_ps2_cardsize = ui_label_create_value(cont, text);
            ui_menu_set_load_page_event(menu, cont, cardsize_page);
        }
    }

    /* Info submenu */
    lv_obj_t *info_page = ui_menu_subpage_create(menu, "Info");
    {
        sd_cid_t cid = sd_get_CID();
        char text[32];

        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "Version");
        ui_label_create(cont, sd2psx_version);

        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "Commit");
        ui_label_create(cont, sd2psx_commit);

        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "Debug");
#ifdef DEBUG_USB_UART
        ui_label_create(cont, "Yes");
#else
        ui_label_create(cont, "No");
#endif
        snprintf(text, sizeof(text), "%04X", cid.mid);
        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "SD MID");
        ui_label_create_value(cont, text);
        memset(text, 0, sizeof(text));

        snprintf(text, sizeof(text), "%02X%02X", cid.oid[0], cid.oid[1]);
        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "SD OID");
        ui_label_create_value(cont, text);
        memset(text, 0, sizeof(text));

        snprintf(text, sizeof(text), "%02d/%04d",
            cid.mdt_month, 2000 + ((cid.mdt_year_high << 4) | cid.mdt_year_low));
        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "SD Date");
        ui_label_create_value(cont, text);
    }

    lv_obj_t *usb_mode_page = ui_menu_subpage_create(menu, "USB Mode");
    {
        cont = ui_menu_cont_create_nav(usb_mode_page);
        ui_label_create(cont, "Serial");
        lv_obj_add_event_cb(cont, evt_set_usb_mode, LV_EVENT_CLICKED, (void *)(intptr_t)USB_MODE_UART);

        cont = ui_menu_cont_create_nav(usb_mode_page);
        ui_label_create(cont, "PS3 MCA");
        lv_obj_add_event_cb(cont, evt_set_usb_mode, LV_EVENT_CLICKED, (void *)(intptr_t)USB_MODE_MCA);

        cont = ui_menu_cont_create_nav(usb_mode_page);
        ui_label_create(cont, "Mass");
        lv_obj_add_event_cb(cont, evt_usb_storage, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *controls_page = ui_menu_subpage_create(menu, "Controls");
    cont = ui_menu_cont_create(controls_page);
    lv_obj_t *help = ui_label_create_grow(cont,
        "Home tap: Chan\nHome hold: Card\nBoth: menu/home\nMenu hold L:Back\nMenu hold R: OK");
    lv_obj_set_style_text_letter_space(help, -1, 0);
    lv_obj_set_style_text_line_space(help, -1, 0);
    cont = ui_menu_cont_create_nav(controls_page);
    lv_obj_set_style_min_height(cont, 9, 0);
    ui_label_create(cont, "Back");
    lv_obj_add_event_cb(cont, evt_go_back, LV_EVENT_CLICKED, NULL);

    /* Main menu */
    main_page = ui_menu_subpage_create(menu, "Main Menu");
    {
        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "Boot Mode");
        lbl_mode = ui_label_create(cont, (settings_get_mode(false) == MODE_PS1) ? "PS1" : "PS2");
        ui_menu_set_load_page_event(menu, cont, mode_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "USB Mode");
        lbl_usb_mode = ui_label_create(cont, (settings_get_mca_enabled() ? "MCA>" : "Serial>"));
        ui_menu_set_load_page_event(menu, cont, usb_mode_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "PS1 Settings");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, ps1_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "PS2 Settings");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, ps2_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "Display");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, display_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow_scroll(cont, "Info");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, info_page);

        cont = ui_menu_cont_create_nav(main_page);
        ui_label_create_grow(cont, "Controls");
        ui_label_create(cont, ">");
        ui_menu_set_load_page_event(menu, cont, controls_page);
    }

    ui_menu_set_page(menu, main_page);
}


static void create_splash(void) {
    // Create the splash screen object
    scr_splash = ui_scr_create();

    // Create an lv_img_dsc_t for the buffer
    static const lv_img_dsc_t splash_img_dsc = {
        .header.always_zero = 0,
        .header.w = 128,
        .header.h = 64,
        .data_size =  sizeof(splash_img),
        .header.cf = LV_IMG_CF_INDEXED_1BIT,
        .data = splash_img,
    };

    // Add the image to the splash screen
    splash_image = lv_img_create(scr_splash);
    lv_img_set_src(splash_image, &splash_img_dsc);
    lv_obj_center(splash_image);

    lv_obj_add_event_cb(scr_splash, evt_scr_main, LV_EVENT_ALL, NULL);

}

void gui_usb_storage(bool enabled) {
    if (usb_screen == enabled)
        return;
    usb_screen = enabled;
    input_flush();
    if (enabled) {
        lv_obj_add_flag(g_navbar, LV_OBJ_FLAG_HIDDEN);
        splash_load_usb();
        lv_img_cache_invalidate_src(lv_img_get_src(splash_image));
        lv_obj_invalidate(splash_image);
        lv_scr_load(scr_splash);
        oled_update_last_action_time();
        lv_refr_now(NULL);
    } else {
        wait_button_release = true;
        ui_state = UI_STATE_MAIN;
        refresh_gui = true;
        ui_goto_screen(scr_main);
    }
}

static void create_ui(void) {
    lv_style_init(&style_inv);
    lv_style_set_bg_opa(&style_inv, LV_OPA_COVER);
    lv_style_set_bg_color(&style_inv, COLOR_FG);
    lv_style_set_border_color(&style_inv, COLOR_BG);
    lv_style_set_line_color(&style_inv, COLOR_BG);
    lv_style_set_arc_color(&style_inv, COLOR_BG);
    lv_style_set_text_color(&style_inv, COLOR_BG);
    lv_style_set_outline_color(&style_inv, COLOR_BG);

    create_nav();
    create_main_screen();
    create_menu_screen();
    create_cardswitch_screen();
    create_splash();

    /* start at the main screen */
    ui_state = UI_STATE_SPLASH;
    ui_goto_screen(scr_splash);
}

static void update_activity(void) {
    static uint64_t last_update = 0U;
    static bool write_occured = false;
    static bool visible = false;
    uint64_t time = time_us_64();
    write_occured |= (ps1_mc_data_interface_write_occured() || ps2_mc_data_interface_write_occured());
    if ((time - last_update) > 200 * 1000) {
        if (write_occured) {
            if (!visible) {
                lv_obj_clear_flag(g_write_indicator, LV_OBJ_FLAG_HIDDEN);
                visible = true;
            }
            last_update = time;
        } else if (visible) {
            lv_obj_add_flag(g_write_indicator, LV_OBJ_FLAG_HIDDEN);
            last_update = time;
            visible = false;
        }
        write_occured = false;
    }
}

void gui_init(void) {
    if (!lv_is_initialized()) {
        have_oled = oled_init();
        if (settings_get_display_flipped())
            input_flip();

        lv_init();

        log(LOG_INFO, "lv_init done \n");

        static lv_disp_draw_buf_t disp_buf;
        /* Sixteen rows at a time. The OLED driver retains the complete 1-bit frame. */
        _Static_assert(sizeof(lv_color_t) == 1, "The OLED flush uses 8-bit LVGL pixels");
        static lv_color_t draw_buf[DISPLAY_WIDTH * 16];
        lv_disp_draw_buf_init(&disp_buf, draw_buf, NULL, ARRAY_SIZE(draw_buf));

        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.draw_buf = &disp_buf;
        disp_drv.flush_cb = flush_cb;
        disp_drv.hor_res = DISPLAY_WIDTH;
        disp_drv.ver_res = DISPLAY_HEIGHT;

        lv_disp_t *disp;
        disp = lv_disp_drv_register(&disp_drv);

        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_KEYPAD;
        indev_drv.read_cb = keypad_read;

        lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
        lv_group_t *g = lv_group_create();
        lv_group_set_default(g);
        lv_indev_set_group(indev, g);

        lv_theme_t *th = ui_theme_mono_init(disp, 1, LV_FONT_DEFAULT);
        lv_disp_set_theme(disp, th);

        create_ui();
        splash_init();

        refresh_gui = false;
        ui_state = UI_STATE_SPLASH;
    }
}

void gui_request_refresh(void) {
    refresh_gui = true;
    oled_update_last_action_time();
}

void gui_do_ps1_card_switch(void) {
    log(LOG_INFO, "switching the card now!\n");

    if (ui_state != UI_STATE_SPLASH) {
        ui_state = UI_STATE_MAIN;
    }
    time_screen = time_us_64();
    oled_update_last_action_time();
}

void gui_do_ps2_card_switch(void) {
    current_progress = 0;
    displayed_progress = -1;

    oled_update_last_action_time();

    ps2_cardman_set_progress_cb(reload_card_cb);

    if (ui_state != UI_STATE_SPLASH) {
        ui_state = UI_STATE_SWITCHING;
    }
}

void gui_task(void) {
    if (usb_screen) {
        input_flush();
        oled_update_last_action_time();
        gui_tick();
        return;
    }
    if (!wait_button_release)
        input_update_display(g_navbar);

    char card_name[127];
    const char *folder_name = NULL;

    if (time_us_64() > GUI_SCREEN_IMAGE_TIMEOUT_US) {
        switch (ui_state) {
            case UI_STATE_GAME_IMG:
                ui_goto_screen(scr_splash);
                break;
            case UI_STATE_SPLASH:
                // After initial splash time - move to main screen
                ui_state = UI_STATE_MAIN;
                __attribute__((fallthrough));
            case UI_STATE_MAIN:
                ui_goto_screen(scr_main);
                break;
            case UI_STATE_SWITCHING:
                ui_goto_screen(scr_card_switch);
                update_bar();
                oled_update_last_action_time();
                break;
            case UI_STATE_MENU:
                ui_goto_screen(scr_menu);
                break;
            default:
                break;
        }
    }

    if (ui_state == UI_STATE_MAIN) {

        if (settings_get_mode(true) == MODE_PS1) {
            static int displayed_card_idx = -1;
            static int displayed_card_channel = -1;
            static ps1_cardman_state_t cardman_state = PS1_CM_STATE_NORMAL;
            static char card_idx_s[8];
            static char card_channel_s[8];


            if (displayed_card_idx != ps1_cardman_get_idx() || displayed_card_channel != ps1_cardman_get_channel() || cardman_state != ps1_cardman_get_state() ||
                refresh_gui) {
                update_main_status();
                displayed_card_idx = ps1_cardman_get_idx();
                displayed_card_channel = ps1_cardman_get_channel();
                folder_name = ps1_cardman_get_folder_name();
                cardman_state = ps1_cardman_get_state();
                memset(card_name, 0, sizeof(card_name));

                switch (cardman_state) {
                    case PS1_CM_STATE_BOOT: lv_label_set_text(scr_main_idx_lbl, "BOOT"); break;
                    case PS1_CM_STATE_NAMED:
                    case PS1_CM_STATE_GAMEID: lv_label_set_text(scr_main_idx_lbl, folder_name); break;
                    case PS1_CM_STATE_NORMAL:
                    default:
                        snprintf(card_idx_s, sizeof(card_idx_s), "%d", displayed_card_idx);
                        lv_label_set_text(scr_main_idx_lbl, card_idx_s);
                        break;
                }

                snprintf(card_channel_s, sizeof(card_channel_s), "%d", displayed_card_channel);
                lv_label_set_text(scr_main_channel_lbl, card_channel_s);

                card_config_read_channel_name(folder_name,
                                                cardman_state == PS1_CM_STATE_BOOT ? "BootCard" : folder_name,
                                                card_channel_s,
                                                card_name,
                                                sizeof(card_name));
                if (!card_name[0] && cardman_state == PS1_CM_STATE_GAMEID) {
                    game_db_get_current_name(card_name);
                }
                if (!card_name[0] && cardman_state == PS1_CM_STATE_NAMED) {
                    game_db_get_game_name(folder_name, card_name);
                }

                lv_label_set_text(src_main_title_lbl, card_name);
                splash_update_current(folder_name, cardman_state == PS1_CM_STATE_BOOT ? "BootCard" : folder_name, ps1_cardman_get_channel());
                // Make sure to go back to main screen here in case we were on the splash screen due to a game image being shown, otherwise the screen won't update until the next timeout
                time_screen = time_us_64();
            }

            refresh_gui = false;
            update_activity();
        } else if (settings_get_mode(true) == MODE_PS2){
            static int displayed_card_idx = -1;
            static int displayed_card_channel = -1;
            static ps2_cardman_state_t cardman_state = PS2_CM_STATE_NORMAL;
            static char card_idx_s[8];
            static char card_channel_s[8];


            if (displayed_card_idx != ps2_cardman_get_idx() || displayed_card_channel != ps2_cardman_get_channel() || cardman_state != ps2_cardman_get_state() ||
                refresh_gui) {
                displayed_card_idx = ps2_cardman_get_idx();
                displayed_card_channel = ps2_cardman_get_channel();
                folder_name = ps2_cardman_get_folder_name();
                cardman_state = ps2_cardman_get_state();
                update_main_status();
                memset(card_name, 0, sizeof(card_name));

                switch (cardman_state) {
                    case PS2_CM_STATE_BOOT: lv_label_set_text(scr_main_idx_lbl, "BOOT"); break;
                    case PS2_CM_STATE_NAMED:
                    case PS2_CM_STATE_GAMEID: lv_label_set_text(scr_main_idx_lbl, folder_name); break;
                    case PS2_CM_STATE_NORMAL:
                    default:
                        snprintf(card_idx_s, sizeof(card_idx_s), "%d", displayed_card_idx);
                        lv_label_set_text(scr_main_idx_lbl, card_idx_s);
                        break;
                }

                snprintf(card_channel_s, sizeof(card_channel_s), "%d", displayed_card_channel);
                lv_label_set_text(scr_main_channel_lbl, card_channel_s);

                card_config_read_channel_name(folder_name,
                                                cardman_state == PS2_CM_STATE_BOOT ? "BootCard" : folder_name,
                                                card_channel_s,
                                                card_name,
                                                sizeof(card_name));
                if (!card_name[0] && cardman_state == PS2_CM_STATE_GAMEID) {
                    game_db_get_current_name(card_name);
                }
                if (!card_name[0] && cardman_state == PS2_CM_STATE_NAMED) {
                    game_db_get_game_name(folder_name, card_name);
                }

                lv_label_set_text(src_main_title_lbl, card_name);
                splash_update_current(folder_name, cardman_state == PS2_CM_STATE_BOOT ? "BootCard" : folder_name, ps2_cardman_get_channel());

                refresh_gui = false;
            }


            update_activity();
        } else {

        }
    }
    if (splash_game_image_available
        && (ui_state == UI_STATE_MAIN)
        && (time_us_64() - time_screen > GUI_SCREEN_IMAGE_TIMEOUT_US)) {
            ui_state = UI_STATE_GAME_IMG;
    }

    gui_tick();
}
