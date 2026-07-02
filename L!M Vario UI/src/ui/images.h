#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_arrow_mc;
extern const lv_img_dsc_t img_arrow_thermal;
extern const lv_img_dsc_t img_needle_main;
extern const lv_img_dsc_t img_vario_backgrounf;
extern const lv_img_dsc_t img_loading_screen;
extern const lv_img_dsc_t img_gps_connected;
extern const lv_img_dsc_t img_gps_waiting;
extern const lv_img_dsc_t img_glider_th;
extern const lv_img_dsc_t img_center_hub;
extern const lv_img_dsc_t img_img_glider_wind;
extern const lv_img_dsc_t img_img_wind_arrow;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[11];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/