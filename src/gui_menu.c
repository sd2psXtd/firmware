#include "gui_menu.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "gui.h"
#include "input.h"
#include "oled.h"
#include "sd.h"
#include "settings.h"
#include "ui_menu.h"
#include "version/version.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static lv_obj_t *scr_menu, *menu, *main_page;
static lv_obj_t *lbl_ps1_autoboot, *lbl_ps1_game_id, *lbl_ps1_controllercombo,
    *lbl_ps2_autoboot, *lbl_ps2_cardsize, *lbl_ps2_variant, *lbl_ps2_game_id,
    *lbl_civ_err, *auto_off_lbl, *contrast_lbl, *lbl_mode, *lbl_scrn_flip, *lbl_usb_mode;

static const uint8_t auto_off_options[] = {0, 5, 15, 30, 60, 120};
static const uint8_t cardsize_options[] = {1, 2, 4, 8, 16, 32, 64, 128};
#ifdef DEBUG_USB_UART
static lv_obj_t *vcomh_lbl;
static const struct {
    uint8_t value;
    const char *summary;
    const char *label;
} vcomh_options[] = {
    {0x00, "00h >", "0.65 x VCC"},
    {0x20, "20h >", "0.77 x VCC"},
    {0x30, "30h >", "0.83 x VCC"},
};
#endif

#define CONTRAST_OPTIONS 10U

/* Only one option submenu can own rows at a time. */
static lv_obj_t *option_page;
static lv_obj_t *option_labels[CONTRAST_OPTIONS];

typedef struct {
    lv_obj_t *page;
    void (*build)(lv_obj_t *page);
} option_menu_t;

static option_menu_t auto_off_menu, contrast_menu, cardsize_menu;
#ifdef DEBUG_USB_UART
static option_menu_t vcomh_menu;
#endif

static void evt_menu_page(lv_event_t *event);

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
    lv_group_add_obj(lv_group_get_default(), page);
    lv_obj_add_event_cb(page, evt_menu_page, LV_EVENT_ALL, page);
    return page;
}

static lv_obj_t *ui_label_create(lv_obj_t *parent, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
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

static lv_obj_t *ui_header_create(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_align(lbl, LV_ALIGN_TOP_MID);
    lv_obj_set_width(lbl, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void update_auto_off_summary(void) {
    uint8_t value = settings_get_display_timeout();
    char text[8];
    if (value == 0)
        lv_label_set_text(auto_off_lbl, "Off >");
    else {
        snprintf(text, sizeof(text), "%us >", value);
        lv_label_set_text(auto_off_lbl, text);
    }
}

static uint8_t contrast_value(size_t index) {
    return (uint8_t)(255U * (index + 1U) / CONTRAST_OPTIONS);
}

static void update_contrast_summary(void) {
    uint8_t value = settings_get_display_contrast();
    for (size_t i = 0; i < CONTRAST_OPTIONS; ++i) {
        if (contrast_value(i) == value) {
            char text[8];
            snprintf(text, sizeof(text), "%u%% >", (unsigned)(i + 1U) * 10U);
            lv_label_set_text(contrast_lbl, text);
            break;
        }
    }
}

#ifdef DEBUG_USB_UART
static void update_vcomh_summary(void) {
    uint8_t value = settings_get_display_vcomh();
    for (size_t i = 0; i < ARRAY_SIZE(vcomh_options); ++i) {
        if (vcomh_options[i].value == value) {
            lv_label_set_text(vcomh_lbl, vcomh_options[i].summary);
            break;
        }
    }
}
#endif

static void update_cardsize_summary(void) {
    uint8_t value = settings_get_ps2_cardsize();
    char text[10];
    snprintf(text, sizeof(text), "%u MB%s>", value, value <= 8 ? "" : "*");
    lv_label_set_text(lbl_ps2_cardsize, text);
}

static void update_auto_off_options(void) {
    uint8_t value = settings_get_display_timeout();
    for (size_t i = 0; i < ARRAY_SIZE(auto_off_options); ++i) {
        uint8_t seconds = auto_off_options[i];
        char selected = seconds == value ? '>' : ' ';
        char text[8];
        if (seconds == 0)
            snprintf(text, sizeof(text), "%c Off", selected);
        else
            snprintf(text, sizeof(text), "%c %us", selected, seconds);
        lv_label_set_text(option_labels[i], text);
    }
}

static void update_contrast_options(void) {
    uint8_t value = settings_get_display_contrast();
    for (size_t i = 0; i < CONTRAST_OPTIONS; ++i) {
        char text[8];
        snprintf(text, sizeof(text), "%c %u%%", contrast_value(i) == value ? '>' : ' ',
                 (unsigned)(i + 1U) * 10U);
        lv_label_set_text(option_labels[i], text);
    }
}

#ifdef DEBUG_USB_UART
static void update_vcomh_options(void) {
    uint8_t value = settings_get_display_vcomh();
    for (size_t i = 0; i < ARRAY_SIZE(vcomh_options); ++i) {
        char text[16];
        snprintf(text, sizeof(text), "%c %s", vcomh_options[i].value == value ? '>' : ' ',
                 vcomh_options[i].label);
        lv_label_set_text(option_labels[i], text);
    }
}
#endif

static void evt_menu_page(lv_event_t *event) {
    if (event->code == LV_EVENT_KEY) {
        lv_obj_t *page = event->user_data;
        uint32_t key = lv_indev_get_key(lv_indev_get_act());
        lv_obj_t *cur = lv_group_get_focused(lv_group_get_default());
        if (!cur || lv_obj_get_parent(cur) != page)
            return;
        uint32_t idx = lv_obj_get_index(cur);
        uint32_t count = lv_obj_get_child_cnt(page);
        if (key == INPUT_KEY_NEXT || key == INPUT_KEY_PREV) {
            lv_obj_t *row = key == INPUT_KEY_NEXT
                ? ui_menu_find_next_focusable(page, (idx + 1) % count)
                : ui_menu_find_prev_focusable(page, (idx + count - 1) % count);
            lv_group_focus_obj(row);
            if (key == INPUT_KEY_NEXT && lv_obj_get_index(row) <= idx)
                lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
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

    update_cardsize_summary();
    ui_menu_go_back(menu);
}

static void evt_do_civ_deploy(lv_event_t *event) {
    (void)event;
    lv_label_set_text(lbl_civ_err, gui_menu_deploy_civ());
}

static void evt_switch_mode(lv_event_t *event) {
    uint8_t mode = (intptr_t)event->user_data;
    lv_label_set_text(lbl_mode, mode == MODE_PS1 ? "PS1" : "PS2");
    gui_menu_select_mode(mode);
}

static const char *variant_label(void) {
    switch (settings_get_ps2_variant()) {
        case PS2_VARIANT_RETAIL: return "Retail>";
        case PS2_VARIANT_PROTO: return "Proto>";
        case PS2_VARIANT_COH: return "Arcade>";
        case PS2_VARIANT_SC2: return "Conquest>";
        default: return "";
    }
}

static void evt_switch_variant(lv_event_t *event) {
    gui_menu_select_variant((intptr_t)event->user_data);
    lv_label_set_text(lbl_ps2_variant, variant_label());
}

static void evt_set_usb_mode(lv_event_t *event) {
    settings_set_usb_mode((intptr_t)event->user_data);
    gui_mca_mode_changed();
    ui_menu_go_back(menu);
    lv_event_stop_bubbling(event);
}

void gui_menu_refresh_usb(void) {
    if (lbl_usb_mode)
        lv_label_set_text(lbl_usb_mode, settings_get_mca_enabled() ? "MCA>" : "Serial>");
}

static void evt_set_display_timeout(lv_event_t *event) {
    uint8_t display_timeout = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_timeout(display_timeout);
    update_auto_off_summary();
    update_auto_off_options();
}

static void evt_set_display_contrast(lv_event_t *event) {
    uint8_t display_contrast = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_contrast(display_contrast);
    oled_set_contrast(display_contrast);
    update_contrast_summary();
    update_contrast_options();
}

#ifdef DEBUG_USB_UART
static void evt_set_display_vcomh(lv_event_t *event) {
    uint8_t display_vcomh = (uint8_t)(intptr_t)event->user_data;
    settings_set_display_vcomh(display_vcomh);
    oled_set_vcomh(display_vcomh);
    update_vcomh_summary();
    update_vcomh_options();
}
#endif

static lv_obj_t *create_option(lv_obj_t *page, const char *text, lv_event_cb_t callback, uint8_t value) {
    lv_obj_t *row = ui_menu_cont_create_nav(page);
    lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, (void *)(intptr_t)value);
    return ui_label_create_grow(row, text);
}

static void build_auto_off(lv_obj_t *page) {
    ui_header_create(page, "Auto off");
    for (size_t i = 0; i < ARRAY_SIZE(auto_off_options); ++i)
        option_labels[i] = create_option(page, NULL, evt_set_display_timeout, auto_off_options[i]);
    update_auto_off_options();
}

static void build_contrast(lv_obj_t *page) {
    ui_header_create(page, "Contrast");
    for (size_t i = 0; i < CONTRAST_OPTIONS; ++i)
        option_labels[i] = create_option(page, NULL, evt_set_display_contrast, contrast_value(i));
    update_contrast_options();
}

#ifdef DEBUG_USB_UART
static void build_vcomh(lv_obj_t *page) {
    ui_header_create(page, "VCOMH");
    for (size_t i = 0; i < ARRAY_SIZE(vcomh_options); ++i)
        option_labels[i] = create_option(page, NULL, evt_set_display_vcomh, vcomh_options[i].value);
    update_vcomh_options();
}
#endif

static void build_cardsize(lv_obj_t *page) {
    ui_header_create(page, "Default Size");
    uint8_t selected = settings_get_ps2_cardsize();
    for (size_t i = 0; i < ARRAY_SIZE(cardsize_options); ++i) {
        uint8_t value = cardsize_options[i];
        char text[10];
        snprintf(text, sizeof(text), "%c %u MB%s", value == selected ? '>' : ' ', value,
                 value <= 8 ? "" : "*");
        create_option(page, text, evt_set_ps2_cardsize, value);
    }
}

static void clear_option_page(void) {
    lv_obj_clean(option_page);
    option_page = NULL;
    memset(option_labels, 0, sizeof(option_labels));
}

void gui_menu_collect(void) {
    /* Back and menu-exit callbacks can still touch the outgoing page. */
    if (option_page && (lv_scr_act() != scr_menu || ui_menu_get_cur_main_page(menu) != option_page))
        clear_option_page();
}

static void prepare_option_page(lv_event_t *event) {
    const option_menu_t *option = lv_event_get_user_data(event);
    if (option_page == option->page)
        return;

    /* A second submenu can be opened in the same LVGL timer pass. */
    gui_menu_collect();
    option_page = option->page;
    option->build(option_page);
}

static void link_option_page(lv_obj_t *row, option_menu_t *option, void (*build)(lv_obj_t *)) {
    option->page = ui_menu_subpage_create(menu, NULL);
    option->build = build;
    /* Build the rows before the navigation handler restores focus. */
    lv_obj_add_event_cb(row, prepare_option_page, LV_EVENT_CLICKED, option);
    ui_menu_set_load_page_event(menu, row, option->page);
}

static void evt_usb_storage(lv_event_t *event) {
    (void)event;
    gui_menu_start_usb_storage();
}

void gui_menu_create(lv_obj_t *screen) {
    /* Menu screen accessible by pressing the menu button at main */
    scr_menu = screen;

    menu = ui_menu_create(scr_menu);
    lv_obj_add_flag(menu, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(menu, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL) - 2);

    lv_obj_t *cont;

    /* deploy submenu */
    lv_obj_t *mode_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(mode_page, "Mode");
    {
        lv_obj_t *ps2_switch_warn = ui_menu_subpage_create(menu, NULL);
        {
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create(cont, "Avoid insert in");
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create(cont, "PS1 while in PS2");
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create(cont, "mode. Safeguards");
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create(cont, "may not prevent");
            cont = ui_menu_cont_create(ps2_switch_warn);
            ui_label_create(cont, "all damage.");

            cont = ui_menu_cont_create_nav(ps2_switch_warn);
            ui_label_create(cont, "Confirm");
            lv_obj_add_event_cb(cont, evt_switch_mode, LV_EVENT_CLICKED, (void *)(intptr_t)MODE_PS2);

            cont = ui_menu_cont_create_nav(ps2_switch_warn);
            ui_label_create(cont, "Back");
            lv_obj_add_event_cb(cont, evt_go_back, LV_EVENT_CLICKED, NULL);
        }

        cont = ui_menu_cont_create_nav(mode_page);
        ui_label_create(cont, "PS1");
        lv_obj_add_event_cb(cont, evt_switch_mode, LV_EVENT_CLICKED, (void *)(intptr_t)MODE_PS1);

        cont = ui_menu_cont_create_nav(mode_page);
        ui_label_create(cont, "PS2");
        ui_menu_set_load_page_event(menu, cont, ps2_switch_warn);
    }

    /* display config */
    lv_obj_t *display_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(display_page, "Display");
    {
        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Auto off");
        auto_off_lbl = ui_label_create(cont, NULL);
        link_option_page(cont, &auto_off_menu, build_auto_off);
        update_auto_off_summary();

        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Contrast");
        contrast_lbl = ui_label_create(cont, NULL);
        link_option_page(cont, &contrast_menu, build_contrast);
        update_contrast_summary();

#ifdef DEBUG_USB_UART
        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "VCOMH");
        vcomh_lbl = ui_label_create(cont, NULL);
        link_option_page(cont, &vcomh_menu, build_vcomh);
        update_vcomh_summary();
#endif

        cont = ui_menu_cont_create_nav(display_page);
        ui_label_create_grow(cont, "Flip");
        lbl_scrn_flip = ui_label_create(cont, settings_get_display_flipped() ? " Yes" : " No");
        lv_obj_add_event_cb(cont, evt_screen_flip, LV_EVENT_CLICKED, NULL);
    }

    /* ps1 */
    lv_obj_t *ps1_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(ps1_page, "PS1 Settings");
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
    lv_obj_t *ps2_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(ps2_page, "PS2 Settings");
    {
        /* deploy submenu */
        lv_obj_t *civ_page = ui_menu_subpage_create(menu, NULL);
        ui_header_create(civ_page, "Deploy CIV.bin");
        {
            cont = ui_menu_cont_create(civ_page);
            ui_label_create(cont, "");
            cont = ui_menu_cont_create(civ_page);
            lbl_civ_err = ui_label_create(cont, "");

            cont = ui_menu_cont_create_nav(civ_page);
            ui_label_create(cont, "Back");
            lv_obj_add_event_cb(cont, evt_go_back, LV_EVENT_CLICKED, NULL);
        }

        /* Variant submenu */
        lv_obj_t *variant_page = ui_menu_subpage_create(menu, NULL);
        ui_header_create(variant_page, "Variant");
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
            cont = ui_menu_cont_create_nav(ps2_page);
            ui_label_create_grow(cont, "Variant");
            lbl_ps2_variant = ui_label_create(cont, variant_label());
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

        cont = ui_menu_cont_create_nav(ps2_page);
        ui_label_create_grow(cont, "Size");
        lbl_ps2_cardsize = ui_label_create(cont, NULL);
        link_option_page(cont, &cardsize_menu, build_cardsize);
        update_cardsize_summary();
    }

    /* Info submenu */
    lv_obj_t *info_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(info_page, "Info");
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
        ui_label_create(cont, text);
        memset(text, 0, sizeof(text));

        snprintf(text, sizeof(text), "%02X%02X", cid.oid[0], cid.oid[1]);
        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "SD OID");
        ui_label_create(cont, text);
        memset(text, 0, sizeof(text));

        snprintf(text, sizeof(text), "%02d/%04d",
            cid.mdt_month, 2000 + ((cid.mdt_year_high << 4) | cid.mdt_year_low));
        cont = ui_menu_cont_create_nav(info_page);
        ui_label_create_grow_scroll(cont, "SD Date");
        ui_label_create(cont, text);
    }

    lv_obj_t *usb_mode_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(usb_mode_page, "USB Mode");
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

    /* Main menu */
    main_page = ui_menu_subpage_create(menu, NULL);
    ui_header_create(main_page, "Main Menu");
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
    }

    ui_menu_set_page(menu, main_page);
}


void gui_menu_reset(void) {
    ui_menu_set_page(menu, NULL);
    ui_menu_set_page(menu, main_page);
    lv_group_focus_obj(lv_obj_get_child(main_page, 0));
}
