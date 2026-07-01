#pragma once
#include <lvgl.h>

// ============================================================
//  Thermal Helper - RENDERER LAYER (LVGL programmatic rendering)
//  Reads data layer (ThermalHelper) and draws Larus-style lift circle:
//    - Glider symbol FIXED pointing up: offset left for right turns, right for left
//    - The circular ring of lift dots rotates around (relative to track)
//    - Dot radius proportional to vertical rate; color scales relative to average:
//      Yellow = peak lift, Black = min sink, Red = above average, Blue = below
//  Visible only during thermaling (circling state); hidden during cruise.
// ============================================================

// Call once after ui_init(), parent = objects.main
void ThermalDraw_Init(lv_obj_t* parent);

// Call repeatedly inside loop() (internally rate-limited to ~10 Hz)
//   circling : show lift circle visualization?
//   turnDir  : +1 right turn / -1 left turn / 0 unknown
//   track    : current GPS ground track (degrees)
void ThermalDraw_Update(bool circling, int turnDir, float track);
