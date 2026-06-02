#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

static const char *screen_names[] = { "Main" };
static const char *object_names[] = { "main", "obj0", "obj1" };

screen_main_state_t screen_main_state;

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_main() {
    screen_main_state_t *state = &screen_main_state;
    (void)state;
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 480, 480);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_meter_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, -20, -20);
            lv_obj_set_size(obj, 520, 520);
            {
                lv_meter_scale_t *scale = lv_meter_add_scale(obj);
                state->scale = scale;
                lv_meter_set_scale_ticks(obj, scale, 11, 1, 0, lv_color_hex(0x666666));
                lv_meter_set_scale_major_ticks(obj, scale, 2, 7, 18, lv_color_hex(0x212121), 25);
                lv_meter_set_scale_range(obj, scale, 5, 0, 125, 55);
            }
            {
                lv_meter_scale_t *scale = lv_meter_add_scale(obj);
                state->scale1 = scale;
                lv_meter_set_scale_ticks(obj, scale, 11, 1, 0, lv_color_hex(0x666666));
                lv_meter_set_scale_major_ticks(obj, scale, 2, 7, 18, lv_color_hex(0x212121), 25);
                lv_meter_set_scale_range(obj, scale, 0, 5, 125, 180);
            }
            {
                lv_meter_scale_t *scale = lv_meter_add_scale(obj);
                state->scale2 = scale;
                lv_meter_set_scale_ticks(obj, scale, 2, 0, 0, lv_color_hex(0x000000));
                lv_meter_set_scale_major_ticks(obj, scale, 99999, 0, 0, lv_color_hex(0x000000), -500);
                lv_meter_set_scale_range(obj, scale, -5000, 5000, 250, 55);
                {
                    lv_meter_indicator_t *indicator = lv_meter_add_arc(obj, scale, 70, lv_color_hex(0xffffff), 0);
                    state->indicator = indicator;
                    lv_meter_set_indicator_start_value(obj, indicator, -4970);
                    lv_meter_set_indicator_end_value(obj, indicator, 4970);
                }
                {
                    state->indicator1 = lv_meter_add_needle_img(obj, scale, &img_arrow_mc, -215, 15);
                }
                {
                    state->indicator2 = lv_meter_add_needle_img(obj, scale, &img_arrow_thermal, -170, 15);
                }
                {
                    state->indicator3 = lv_meter_add_needle_img(obj, scale, &img_needle_main, -35, 10);
                }
            }
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_32, LV_PART_TICKS | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_rounded(obj, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj1 = obj;
            lv_obj_set_pos(obj, 68, 68);
            lv_obj_set_size(obj, 345, 345);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_WITH_ARROW);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 172.5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
    screen_main_state_t *state = &screen_main_state;
    (void)state;
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    {
        if (state->indicator1) {
            int32_t new_val = evalIntegerProperty(flowState, 0, 3, "Failed to evaluate Value in Meter widget");
            int32_t cur_val = state->indicator1->start_value;
            if (new_val != cur_val) {
                tick_value_change_obj = objects.obj0;
                lv_meter_set_indicator_value(objects.obj0, state->indicator1, new_val);
                tick_value_change_obj = NULL;
            }
        }
    }
    {
        if (state->indicator2) {
            int32_t new_val = evalIntegerProperty(flowState, 0, 4, "Failed to evaluate Value in Meter widget");
            int32_t cur_val = state->indicator2->start_value;
            if (new_val != cur_val) {
                tick_value_change_obj = objects.obj0;
                lv_meter_set_indicator_value(objects.obj0, state->indicator2, new_val);
                tick_value_change_obj = NULL;
            }
        }
    }
    {
        if (state->indicator3) {
            int32_t new_val = evalIntegerProperty(flowState, 0, 5, "Failed to evaluate Value in Meter widget");
            int32_t cur_val = state->indicator3->start_value;
            if (new_val != cur_val) {
                tick_value_change_obj = objects.obj0;
                lv_meter_set_indicator_value(objects.obj0, state->indicator3, new_val);
                tick_value_change_obj = NULL;
            }
        }
    }
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_main,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 1) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
//
//

void create_screens() {
    
    eez_flow_init_fonts(fonts, sizeof(fonts) / sizeof(ext_font_desc_t));

// Set default LVGL theme
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    // Initialize screens
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    
    // Create screens
    create_screen_main();
}