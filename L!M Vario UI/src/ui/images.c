#include "images.h"

const ext_img_desc_t images[20] = {
    { "arrow_mc", &img_arrow_mc },
    { "arrow_thermal", &img_arrow_thermal },
    { "needle_main", &img_needle_main },
    { "Vario_Backgrounf", &img_vario_backgrounf },
    { "Loading_Screen", &img_loading_screen },
    { "GPS_Connected", &img_gps_connected },
    { "GPS_Waiting", &img_gps_waiting },
    { "glider_th", &img_glider_th },
    { "center_hub", &img_center_hub },
    { "img_glider_wind", &img_img_glider_wind },
    { "img_wind_arrow", &img_img_wind_arrow },
    { "indicator_space", &img_indicator_space },
    { "Wifi_ON", &img_wifi_on },
    { "Wifi_OFF", &img_wifi_off },
    { "Battery_Full", &img_battery_full },
    { "Battery_Med", &img_battery_med },
    { "Battery_Low", &img_battery_low },
    { "Vario_Backroug_Indic", &img_vario_backroug_indic },
    { "img_wind_arrow_avg", &img_img_wind_arrow_avg },
    { "img_wind_arrow_energy", &img_img_wind_arrow_energy },
};