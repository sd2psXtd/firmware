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
#include "splash.h"
#include "settings.h"
#include "gui_menu.h"
#include "ui_theme_mono.h"

#if LOG_LEVEL_GUI == 0
    #define log(x...)
#else
    #define log(level, fmt, x...) LOG_PRINT(LOG_LEVEL_GUI, level, fmt, ##x)
#endif

static uint64_t time_screen;

/* Displays the line at the bottom for long pressing buttons */
static lv_obj_t *g_navbar, *g_progress_bar, *g_progress_text, *g_activity_frame;

static lv_obj_t *scr_switch_nag, *scr_card_switch, *scr_main, *scr_splash, *scr_menu, *main_header;
static lv_style_t style_inv, src_main_label_style;
static lv_anim_t src_main_animation_template;
static lv_obj_t *scr_main_idx_lbl, *scr_main_channel_lbl, *src_main_title_lbl, *lbl_channel;

static int have_oled;
static lv_obj_t *splash_image;
static bool usb_screen, wait_button_release;
static bool usb_nav_hidden, usb_activity_hidden;
static enum {
    UI_STATE_SPLASH,
    UI_STATE_MAIN,
    UI_STATE_MENU,
    UI_STATE_SWITCHING,
    UI_STATE_GAME_IMG
} ui_state;
static int current_progress;
static bool refresh_gui;
static bool installing_exploit;

#define COLOR_FG lv_color_white()
#define COLOR_BG lv_color_black()

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

static lv_obj_t *ui_label_create_at(lv_obj_t *parent, int x, int y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_align(label, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(label, (lv_coord_t)x, (lv_coord_t)y);
    return label;
}

static lv_obj_t *ui_header_create(lv_obj_t *parent, const char *text, bool inverted) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_align(lbl, LV_ALIGN_TOP_MID);
    if (inverted)
        lv_obj_add_style(lbl, &style_inv, 0);
    lv_obj_set_width(lbl, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void update_bar(void) {
    static lv_point_t line_points[2] = {{0, DISPLAY_HEIGHT / 2}, {0, DISPLAY_HEIGHT / 2}};
    static int prev_progress;
    if (current_progress / 5 == prev_progress / 5)
        return;
    prev_progress = current_progress;
    line_points[1].x = (lv_coord_t)(DISPLAY_WIDTH * current_progress / 100);
    lv_line_set_points(g_progress_bar, line_points, 2);
    lv_label_set_text(g_progress_text, ps2_cardman_get_progress_text());
}

static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (have_oled) {
        oled_clear();

        for (int y = area->y1; y <= area->y2; y++) {
            for (int x = area->x1; x <= area->x2; x++) {
                if (color_p->full)
                    oled_draw_pixel(x, y);
                color_p++;
            }
        }

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
    lv_style_set_line_color(&style_line, lv_palette_main(LV_PALETTE_BLUE));

    g_navbar = lv_line_create(lv_layer_top());
    lv_obj_add_style(g_navbar, &style_line, 0);

    static lv_style_t style_frame;
    lv_style_init(&style_frame);
    lv_style_set_line_width(&style_frame, 20);
    lv_style_set_line_color(&style_frame, lv_palette_main(LV_PALETTE_BLUE));

    g_activity_frame = lv_line_create(lv_layer_top());
    lv_obj_add_style(g_activity_frame, &style_frame, 0);

    static lv_point_t line_points[5] = {{0, 0}, {DISPLAY_WIDTH, 0}, {DISPLAY_WIDTH, DISPLAY_HEIGHT}, {0, DISPLAY_HEIGHT}, {0, 0}};
    lv_line_set_points(g_activity_frame, line_points, 5);

    lv_obj_add_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
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
        gui_menu_collect();
    } else {
        // log(LOG_TRACE, "%s delay %u not reached\n", __func__, delay);
    }
}

static void reload_card_cb(int progress, bool done) {
    current_progress = progress;
    if (done) {
        ps2_cardman_set_progress_cb(NULL);
        input_flush();
        if (ui_state != UI_STATE_SPLASH)
            ui_state = UI_STATE_MAIN;
    } else if (time_us_64() > GUI_SCREEN_IMAGE_TIMEOUT_US){
        ui_state = UI_STATE_SWITCHING;
    }
}

static void evt_scr_main(lv_event_t *event) {
    if (event->code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        log(LOG_INFO, "main screen got key %d\n", (int)key);
        if (key == INPUT_KEY_MENU) {
            log(LOG_INFO, "activate menu!\n");
            lv_scr_load(scr_menu);
            gui_menu_reset();
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

static void update_main_header(void) {
    if (settings_get_mca_enabled()) {
        lv_label_set_text(main_header, settings_get_mode(true) == MODE_PS2 ? "USB MCA: PS2" : "USB MCA: PS1");
    } else if (settings_get_mode(true) == MODE_PS1) {
        lv_label_set_text(main_header, "PS1 Memory Card");
    } else if (settings_get_mode(true) == MODE_PS2){
        if (!ps2_magicgate)
            lv_label_set_text(main_header, "PS2: No CIV!");
        else if (PS2_VARIANT_RETAIL == settings_get_ps2_variant())
            lv_label_set_text(main_header, "PS2 Memory Card");
        else if (PS2_VARIANT_PROTO == settings_get_ps2_variant())
            lv_label_set_text(main_header, "Prototype Card");
        else if (PS2_VARIANT_COH == settings_get_ps2_variant())
            lv_label_set_text(main_header, "Security Dongle");
        else if (PS2_VARIANT_SC2 == settings_get_ps2_variant())
            lv_label_set_text(main_header, "Conquest Card");
    } else {
        lv_label_set_text(main_header, "USB Mode");
    }
}

void gui_menu_select_mode(uint8_t mode) {
    settings_set_mode(mode);
    gui_request_refresh();
    if (mode == MODE_PS2) {
        keystore_init();
        update_main_header();
    }
    ui_state = UI_STATE_MAIN;
}

void gui_menu_select_variant(int variant) {
    ps2_cardman_set_variant(variant);
    update_main_header();
    gui_request_refresh();
    ui_state = UI_STATE_MAIN;
}

const char *gui_menu_deploy_civ(void) {
    int result = keystore_deploy();
    refresh_gui = true;
    return result == 0 ? "Success!\n" : keystore_error(result);
}

void gui_menu_start_usb_storage(void) {
    if (settings_get_usb_mode(true) == USB_MODE_MASS)
        return;
    settings_set_usb_mode(USB_MODE_MASS);
    gui_usb_storage(true);
}

void gui_mca_mode_changed(void) {
    gui_menu_refresh_usb();
    update_main_header();
    gui_request_refresh();
}

static void create_main_screen(void) {
    lv_obj_t *lbl;

    /* Main screen listing current memcard, status, etc */
    scr_main = ui_scr_create();
    lv_obj_add_event_cb(scr_main, evt_scr_main, LV_EVENT_ALL, NULL);
    main_header = ui_header_create(scr_main, "", true);
    update_main_header();

    ui_label_create_at(scr_main, 0, 24, "Card");

    scr_main_idx_lbl = ui_label_create_at(scr_main, 0, 24, "");
    lv_obj_set_align(scr_main_idx_lbl, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_width(scr_main_idx_lbl, 11 * 8);  // 11 characters, assuming 8px monospace font
    lv_obj_set_style_text_align(scr_main_idx_lbl, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(scr_main_idx_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_anim_init(&src_main_animation_template);
    lv_anim_set_delay(&src_main_animation_template, 1000); /*Wait 1 second to start the first scroll*/
    lv_anim_set_repeat_count(&src_main_animation_template, 0);

    lv_obj_remove_style(scr_main_idx_lbl, &src_main_label_style, LV_STATE_DEFAULT);
    lv_style_init(&src_main_label_style);
    lv_style_set_anim(&src_main_label_style, &src_main_animation_template);  //<
    lv_obj_add_style(scr_main_idx_lbl, &src_main_label_style, LV_STATE_DEFAULT);

    lbl_channel = ui_label_create_at(scr_main, 0, 32, "Channel");

    scr_main_channel_lbl = ui_label_create_at(scr_main, 0, 32, "");
    lv_obj_set_align(scr_main_channel_lbl, LV_ALIGN_TOP_RIGHT);

    src_main_title_lbl = lv_label_create(scr_main);
    lv_obj_set_align(src_main_title_lbl, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(src_main_title_lbl, 0, 40);
    lv_label_set_text(src_main_title_lbl, "");
    lv_label_set_long_mode(src_main_title_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(src_main_title_lbl, 128);

    {
        lv_anim_init(&src_main_animation_template);
        lv_anim_set_delay(&src_main_animation_template, 1000); /*Wait 1 second to start the first scroll*/
        lv_anim_set_repeat_count(&src_main_animation_template, 0);

        /*Initialize the label style with the animation template*/
        lv_style_init(&src_main_label_style);
        lv_style_set_anim(&src_main_label_style, &src_main_animation_template);

        lv_obj_add_style(src_main_title_lbl, &src_main_label_style, LV_STATE_DEFAULT);
    }

    lbl = lv_label_create(scr_main);
    lv_obj_set_align(lbl, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_pos(lbl, 0, -2);
    lv_label_set_text(lbl, "<");
    lv_obj_add_style(lbl, &style_inv, 0);

    lbl = lv_label_create(scr_main);
    lv_obj_set_align(lbl, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_pos(lbl, 0, -2);
    lv_label_set_text(lbl, "Menu");
    lv_obj_set_width(lbl, 4 * 8 + 2);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_style(lbl, &style_inv, 0);

    lbl = lv_label_create(scr_main);
    lv_obj_set_align(lbl, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_pos(lbl, 0, -2);
    lv_label_set_text(lbl, ">");
    lv_obj_add_style(lbl, &style_inv, 0);
}

static void create_cardswitch_screen(void) {
    scr_card_switch = ui_scr_create();

    ui_header_create(scr_card_switch, "Loading card", true);

    static lv_style_t style_progress;
    lv_style_init(&style_progress);
    lv_style_set_line_width(&style_progress, 12);
    lv_style_set_line_color(&style_progress, lv_palette_main(LV_PALETTE_BLUE));

    g_progress_bar = lv_line_create(scr_card_switch);
    lv_obj_set_width(g_progress_bar, DISPLAY_WIDTH);
    lv_obj_add_style(g_progress_bar, &style_progress, 0);

    g_progress_text = lv_label_create(scr_card_switch);
    lv_obj_set_align(g_progress_text, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(g_progress_text, 0, DISPLAY_HEIGHT - 9);
    lv_label_set_text(g_progress_text, "Read XXX kB/s");
}

static void create_switch_nag_screen(void) {
    scr_switch_nag = ui_scr_create();

    ui_header_create(scr_switch_nag, "Mode switch", false);

    ui_label_create_at(scr_switch_nag, 0, 24, "Now unplug the");
    ui_label_create_at(scr_switch_nag, 0, 32, "card and then");
    ui_label_create_at(scr_switch_nag, 0, 40, "plug it back in");
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
        usb_nav_hidden = lv_obj_has_flag(g_navbar, LV_OBJ_FLAG_HIDDEN);
        usb_activity_hidden = lv_obj_has_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_navbar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
        splash_load_usb();
        lv_img_cache_invalidate_src(lv_img_get_src(splash_image));
        lv_obj_invalidate(splash_image);
        lv_scr_load(scr_splash);
        oled_update_last_action_time();
        lv_refr_now(NULL);
    } else {
        if (!usb_nav_hidden) lv_obj_clear_flag(g_navbar, LV_OBJ_FLAG_HIDDEN);
        if (!usb_activity_hidden) lv_obj_clear_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
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
    scr_menu = ui_scr_create();
    lv_obj_add_event_cb(scr_menu, evt_scr_menu, LV_EVENT_ALL, NULL);
    gui_menu_create(scr_menu);
    create_cardswitch_screen();
    create_switch_nag_screen();
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
        // TODO: Causes a 31ms delay that causes issues with mmce fs
        if (write_occured) {
            input_flush();
            if (!visible) {
                lv_obj_clear_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
                visible = true;
            }
            last_update = time;
        } else if (visible) {
            lv_obj_add_flag(g_activity_frame, LV_OBJ_FLAG_HIDDEN);
            last_update = time;
            visible = false;
        }
        write_occured = false;
    }
}

void gui_init(void) {
    if (!lv_is_initialized()) {
        if ((have_oled = oled_init())) {
            oled_clear();
            oled_show();
        }
        if (settings_get_display_flipped())
            input_flip();

        lv_init();

        log(LOG_INFO, "lv_init done \n");

        static lv_disp_draw_buf_t disp_buf;
        static lv_color_t buf_1[DISPLAY_WIDTH * DISPLAY_HEIGHT];
        lv_disp_draw_buf_init(&disp_buf, buf_1, NULL, DISPLAY_WIDTH * DISPLAY_HEIGHT);

        static lv_disp_drv_t disp_drv;
        lv_disp_drv_init(&disp_drv);
        disp_drv.draw_buf = &disp_buf;
        disp_drv.flush_cb = flush_cb;
        disp_drv.hor_res = DISPLAY_WIDTH;
        disp_drv.ver_res = DISPLAY_HEIGHT;
        disp_drv.direct_mode = disp_drv.full_refresh = 1;

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
        installing_exploit = false;
        ui_state = UI_STATE_SPLASH;
    } else {
        refresh_gui = true;
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

    update_bar();

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
                update_main_header();
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

                if (card_name[0]) {
                    lv_label_set_text(src_main_title_lbl, card_name);
                } else {
                    lv_label_set_text(src_main_title_lbl, "");
                }
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
                update_main_header();
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

                if (card_name[0]) {
                    lv_label_set_text(src_main_title_lbl, card_name);
                    lv_anim_init(&src_main_animation_template);
                    lv_anim_set_delay(&src_main_animation_template, 1000); /*Wait 1 second to start the first scroll*/
                    lv_anim_set_repeat_count(&src_main_animation_template, 0);

                    lv_obj_remove_style(src_main_title_lbl, &src_main_label_style, LV_STATE_DEFAULT);
                    lv_style_init(&src_main_label_style);
                    lv_style_set_anim(&src_main_label_style, &src_main_animation_template);

                    lv_obj_add_style(src_main_title_lbl, &src_main_label_style, LV_STATE_DEFAULT);
                } else {
                    lv_label_set_text(src_main_title_lbl, "");
                }

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
    // log(LOG_TRACE, "repeat count %u, time %u\n", src_main_animation_template.repeat_cnt, src_main_animation_template.playback_time);
}
