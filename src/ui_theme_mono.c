#include "ui_theme_mono.h"
#include "ui_menu.h"

#if LV_USE_THEME_MONO

static lv_theme_t theme;
static lv_style_t screen, panel, inverted, menu_row, scrollbar;
static bool inited;

static void theme_apply(lv_theme_t *th, lv_obj_t *obj) {
    LV_UNUSED(th);

    if (!lv_obj_get_parent(obj)) {
        lv_obj_add_style(obj, &screen, 0);
    } else if (lv_obj_check_type(obj, &ui_menu_cont_class)) {
        lv_obj_add_style(obj, &menu_row, 0);
        lv_obj_add_style(obj, &inverted, LV_STATE_FOCUS_KEY);
    } else if (lv_obj_check_type(obj, &ui_menu_page_class)) {
        lv_obj_add_style(obj, &scrollbar, LV_PART_SCROLLBAR);
    } else if (lv_obj_check_type(obj, &lv_obj_class) || lv_obj_check_type(obj, &lv_btn_class)) {
        lv_obj_add_style(obj, &panel, 0);
    }
}

lv_theme_t *ui_theme_mono_init(lv_disp_t *disp, bool dark_bg, const lv_font_t *font) {
    if (inited) {
        lv_style_reset(&screen);
        lv_style_reset(&panel);
        lv_style_reset(&inverted);
        lv_style_reset(&menu_row);
        lv_style_reset(&scrollbar);
    } else {
        lv_style_init(&screen);
        lv_style_init(&panel);
        lv_style_init(&inverted);
        lv_style_init(&menu_row);
        lv_style_init(&scrollbar);
    }

    lv_color_t fg = dark_bg ? lv_color_white() : lv_color_black();
    lv_color_t bg = dark_bg ? lv_color_black() : lv_color_white();

    lv_style_set_bg_opa(&screen, LV_OPA_COVER);
    lv_style_set_bg_color(&screen, bg);
    lv_style_set_text_color(&screen, fg);
    lv_style_set_text_font(&screen, font);

    lv_style_set_bg_opa(&panel, LV_OPA_COVER);
    lv_style_set_bg_color(&panel, bg);
    lv_style_set_border_color(&panel, fg);
    lv_style_set_border_width(&panel, 1);
    lv_style_set_pad_all(&panel, 4);

    lv_style_set_bg_opa(&inverted, LV_OPA_COVER);
    lv_style_set_bg_color(&inverted, fg);
    lv_style_set_text_color(&inverted, bg);

    lv_style_set_min_height(&menu_row, 10);
    lv_style_set_pad_left(&menu_row, 2);
    lv_style_set_pad_right(&menu_row, 3);

    lv_style_set_bg_opa(&scrollbar, LV_OPA_COVER);
    lv_style_set_bg_color(&scrollbar, fg);
    lv_style_set_width(&scrollbar, 1);

    theme.disp = disp;
    theme.font_small = font;
    theme.font_normal = font;
    theme.font_large = font;
    theme.apply_cb = theme_apply;

    if (!disp || lv_disp_get_theme(disp) == &theme)
        lv_obj_report_style_change(NULL);
    inited = true;
    return &theme;
}

#endif
