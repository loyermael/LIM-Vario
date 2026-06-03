#include "styles.h"
#include "images.h"
#include "fonts.h"

#include "ui.h"
#include "screens.h"

//
// Style: MenuItemText
//

void init_style_menu_item_text_MAIN_DEFAULT(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xffffff));
    lv_style_set_text_font(style, &lv_font_montserrat_34);
};

lv_style_t *get_style_menu_item_text_MAIN_DEFAULT() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_menu_item_text_MAIN_DEFAULT(style);
    }
    return style;
};

void init_style_menu_item_text_MAIN_CHECKED(lv_style_t *style) {
    lv_style_set_text_color(style, lv_color_hex(0xfbd500));
    lv_style_set_text_font(style, &lv_font_montserrat_34);
};

lv_style_t *get_style_menu_item_text_MAIN_CHECKED() {
    static lv_style_t *style;
    if (!style) {
        style = (lv_style_t *)lv_mem_alloc(sizeof(lv_style_t));
        lv_style_init(style);
        init_style_menu_item_text_MAIN_CHECKED(style);
    }
    return style;
};

void add_style_menu_item_text(lv_obj_t *obj) {
    (void)obj;
    lv_obj_add_style(obj, get_style_menu_item_text_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(obj, get_style_menu_item_text_MAIN_CHECKED(), LV_PART_MAIN | LV_STATE_CHECKED);
};

void remove_style_menu_item_text(lv_obj_t *obj) {
    (void)obj;
    lv_obj_remove_style(obj, get_style_menu_item_text_MAIN_DEFAULT(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_style(obj, get_style_menu_item_text_MAIN_CHECKED(), LV_PART_MAIN | LV_STATE_CHECKED);
};

//
//
//

void add_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*AddStyleFunc)(lv_obj_t *obj);
    static const AddStyleFunc add_style_funcs[] = {
        add_style_menu_item_text,
    };
    add_style_funcs[styleIndex](obj);
}

void remove_style(lv_obj_t *obj, int32_t styleIndex) {
    typedef void (*RemoveStyleFunc)(lv_obj_t *obj);
    static const RemoveStyleFunc remove_style_funcs[] = {
        remove_style_menu_item_text,
    };
    remove_style_funcs[styleIndex](obj);
}