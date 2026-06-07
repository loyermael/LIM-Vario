#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *vario_meter;
    lv_obj_t *center_hub;
    lv_obj_t *lbl_alt;
    lv_obj_t *lbl_vario_int;
    lv_obj_t *lbl_vario;
    lv_obj_t *quick_menu_panel;
    lv_obj_t *item_list;
    lv_obj_t *_lbl_exit;
    lv_obj_t *val_profil;
    lv_obj_t *obj0;
    lv_obj_t *val_weight;
    lv_obj_t *obj1;
    lv_obj_t *val_bugs;
    lv_obj_t *obj2;
    lv_obj_t *val_water;
    lv_obj_t *obj3;
    lv_obj_t *val_qnh;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *selection_frame;
} objects_t;

extern objects_t objects;

typedef struct {
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *indicator;
    lv_meter_indicator_t *indicator1;
    lv_meter_indicator_t *indicator2;
} screen_main_state_t;

extern screen_main_state_t screen_main_state;

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/