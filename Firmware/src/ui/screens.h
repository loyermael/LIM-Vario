#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SPLASH = 1,
    SCREEN_ID_MAIN = 2,
    _SCREEN_ID_LAST = 2
};

typedef struct _objects_t {
    lv_obj_t *splash;
    lv_obj_t *main;
    lv_obj_t *vario_meter;
    lv_obj_t *center_hub;
    lv_obj_t *infobox_display_container;
    lv_obj_t *lbl_ib_haut_sup;
    lv_obj_t *lbl_ib_haut_inf;
    lv_obj_t *lbl_ib_bas_cent;
    lv_obj_t *lbl_ib_bas_sup;
    lv_obj_t *lbl_ib_bas_inf;
    lv_obj_t *img_gps;
    lv_obj_t *img_wind_arrow;
    lv_obj_t *img_glider_wind;
    lv_obj_t *lbl_wind_value_speed;
    lv_obj_t *lbl_wind_dir;
    lv_obj_t *qr_panel;
    lv_obj_t *lbl_qr_hint;
    lv_obj_t *lbl_qr_title;
    lv_obj_t *qr_slot;
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
    lv_obj_t *setup_panel;
    lv_obj_t *setup_frame;
    lv_obj_t *settings;
    lv_obj_t *selection_frame_1;
    lv_obj_t *confirm_panel;
    lv_obj_t *confirm_panel_selection;
    lv_obj_t *confirm_msg;
    lv_obj_t *confirm_no;
    lv_obj_t *confirm_yes;
    lv_obj_t *item0;
    lv_obj_t *item1;
    lv_obj_t *item2;
    lv_obj_t *item3;
    lv_obj_t *item5;
    lv_obj_t *item6;
    lv_obj_t *item4;
    lv_obj_t *display_list;
    lv_obj_t *dname0;
    lv_obj_t *dname1;
    lv_obj_t *dname2;
    lv_obj_t *dname3;
    lv_obj_t *dname4;
    lv_obj_t *dval2;
    lv_obj_t *dval3;
    lv_obj_t *infobox_mode_list;
    lv_obj_t *imname0;
    lv_obj_t *imname1;
    lv_obj_t *imname2;
    lv_obj_t *units_list;
    lv_obj_t *uname0;
    lv_obj_t *uval0;
    lv_obj_t *uname1;
    lv_obj_t *uname2;
    lv_obj_t *uname4;
    lv_obj_t *uval2;
    lv_obj_t *uval1;
    lv_obj_t *sound_list;
    lv_obj_t *sname0;
    lv_obj_t *sval0;
    lv_obj_t *sname1;
    lv_obj_t *sval1;
    lv_obj_t *sname2;
    lv_obj_t *sval2;
    lv_obj_t *sname4;
    lv_obj_t *vario_list;
    lv_obj_t *vname0;
    lv_obj_t *vval0;
    lv_obj_t *vname1;
    lv_obj_t *vval1;
    lv_obj_t *vname2;
    lv_obj_t *vval2;
    lv_obj_t *vname3;
    lv_obj_t *system_list;
    lv_obj_t *syname0;
    lv_obj_t *syval0;
    lv_obj_t *syname1;
    lv_obj_t *syval1;
    lv_obj_t *syname3_;
    lv_obj_t *syname6;
    lv_obj_t *syname4;
    lv_obj_t *syname5;
    lv_obj_t *about_list;
    lv_obj_t *abname0;
    lv_obj_t *abval0;
    lv_obj_t *abname1;
    lv_obj_t *abval1;
    lv_obj_t *abname2;
    lv_obj_t *abval2;
    lv_obj_t *abname5;
    lv_obj_t *glider_list;
    lv_obj_t *glname0;
    lv_obj_t *glval0;
    lv_obj_t *glname1;
    lv_obj_t *glval1;
    lv_obj_t *glname2;
    lv_obj_t *glval2;
    lv_obj_t *glname3;
    lv_obj_t *glval3;
    lv_obj_t *glname4;
    lv_obj_t *glval4;
    lv_obj_t *glname5;
    lv_obj_t *glval5;
    lv_obj_t *glname6;
    lv_obj_t *glval6;
    lv_obj_t *glname7;
    lv_obj_t *glval7;
    lv_obj_t *glname8;
    lv_obj_t *glval8;
    lv_obj_t *abname5_1;
    lv_obj_t *profil_list;
    lv_obj_t *prname0;
    lv_obj_t *prval0;
    lv_obj_t *prname1;
    lv_obj_t *prname2;
    lv_obj_t *prname3;
    lv_obj_t *prname4;
    lv_obj_t *prname5;
    lv_obj_t *center_info_list;
    lv_obj_t *cname0;
    lv_obj_t *cname1;
    lv_obj_t *cname2;
    lv_obj_t *prname5_1;
    lv_obj_t *infobox_list;
    lv_obj_t *ibname15;
    lv_obj_t *ibname0;
    lv_obj_t *ibname1;
    lv_obj_t *ibname2;
    lv_obj_t *ibname3;
    lv_obj_t *ibname4;
    lv_obj_t *ibname5;
    lv_obj_t *ibname6;
    lv_obj_t *ibname7;
    lv_obj_t *ibname8;
    lv_obj_t *ibname9;
    lv_obj_t *ibname10;
    lv_obj_t *ibname11;
    lv_obj_t *ibname13;
    lv_obj_t *ibname14;
    lv_obj_t *infobox_editor_container;
    lv_obj_t *ib_frame_0;
    lv_obj_t *ib_val_0;
    lv_obj_t *ib_frame_1;
    lv_obj_t *ib_val_1;
    lv_obj_t *ib_frame_2;
    lv_obj_t *ib_val_2;
    lv_obj_t *ib_frame_3;
    lv_obj_t *ib_val_3;
    lv_obj_t *ib_frame_4;
    lv_obj_t *ib_val_4;
    lv_obj_t *ib_frame_5;
    lv_obj_t *ib_frame_6;
} objects_t;

extern objects_t objects;

typedef struct {
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *indicator;
    lv_meter_indicator_t *indicator1;
    lv_meter_indicator_t *indicator2;
} screen_main_state_t;

extern screen_main_state_t screen_main_state;

void create_screen_splash();
void tick_screen_splash();

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/