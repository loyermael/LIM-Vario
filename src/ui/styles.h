#ifndef EEZ_LVGL_UI_STYLES_H
#define EEZ_LVGL_UI_STYLES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Style: MenuItemText
lv_style_t *get_style_menu_item_text_MAIN_DEFAULT();
lv_style_t *get_style_menu_item_text_MAIN_CHECKED();
void add_style_menu_item_text(lv_obj_t *obj);
void remove_style_menu_item_text(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_STYLES_H*/