#include "ThermalDraw.h"
#include "ThermalHelper.h"
#include "ui/images.h"   // img_glider_th
#include <Arduino.h>
#include <math.h>

// --- Geometry (480x480 round display) ---
#define TH_CX       240      // Center X coordinate
#define TH_CY       240      // Center Y coordinate
#define TH_RING_R   75       // Radius of the thermal dot circle (pixels)
#define TH_DOT_MIN  5        // Minimum dot diameter (px) -> vario near 0
#define TH_DOT_MAX  20       // Maximum dot diameter (px) -> strong lift/sink
#define TH_GLIDER_SZ 70      // Glider symbol image dimensions (70x70)

// Canvas bounding box (covers dot ring + maximum dot expansion)
#define TH_HALF     (TH_RING_R + TH_DOT_MAX)   // Half side length
#define TH_LAYER    (2 * TH_HALF)

// Dot diameter scales with vertical speed INTENSITY (|v|): large dot = active air
// (strong lift OR strong sink). Small dot = calm air (~0).
// COLOR indicates direction (red = lift / blue = sink).
#define TH_V_SPAN  3.0f      // |vario| (m/s) at which dot reaches maximum diameter

// --- Palette (color scales with lift sign; neutral band around 0) ---
#define TH_COL_MAX  0xFBD500 // Yellow : strongest thermal core peak
#define TH_COL_UP   0xE53935 // Red    : clear lift
#define TH_COL_DOWN 0x2196F3 // Blue   : clear sink
#define TH_COL_NEUT 0x606060 // Gray   : neutral air mass (near zero)
#define TH_NEUTRAL  0.25f    // m/s    : |vario| below this threshold rendered neutral gray

// Per-dot state calculated during Update step, rendered by LVGL draw callback
typedef struct { int16_t x, y; uint8_t sz; uint32_t col; bool on; } th_dot_t;
static th_dot_t  s_dots[TH_BINS];
static lv_obj_t* s_layer  = NULL;   // Single canvas -> single redrawn bounding box
static lv_obj_t* s_glider = NULL;   // Glider symbol image (fixed orientation pointing up)

// Draws all 24 thermal dots in a single pass directly into LVGL render draw context
static void layer_draw_cb(lv_event_t* e)
{
  lv_draw_ctx_t* dc = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_COVER;
  d.radius = LV_RADIUS_CIRCLE;
  for (int i = 0; i < TH_BINS; i++) {
    if (!s_dots[i].on) continue;
    int s = s_dots[i].sz;
    d.bg_color = lv_color_hex(s_dots[i].col);
    lv_area_t a;
    a.x1 = s_dots[i].x - s / 2;
    a.y1 = s_dots[i].y - s / 2;
    a.x2 = a.x1 + s - 1;
    a.y2 = a.y1 + s - 1;
    lv_draw_rect(dc, &d, &a);
  }
}

void ThermalDraw_Init(lv_obj_t* parent)
{
  // Hide any static mock glider symbol created in EEZ Studio on this screen
  uint32_t nch = lv_obj_get_child_cnt(parent);
  for (uint32_t i = 0; i < nch; i++) {
    lv_obj_t* ch = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(ch, &lv_img_class) &&
        lv_img_get_src(ch) == (const void*)&img_glider_th) {
      lv_obj_add_flag(ch, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Transparent, non-interactive overlay canvas for rendering all dots
  s_layer = lv_obj_create(parent);
  lv_obj_remove_style_all(s_layer);
  lv_obj_set_pos(s_layer, TH_CX - TH_HALF, TH_CY - TH_HALF);
  lv_obj_set_size(s_layer, TH_LAYER, TH_LAYER);
  lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_layer, layer_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
  lv_obj_add_flag(s_layer, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < TH_BINS; i++) s_dots[i].on = false;

  // Glider symbol image (fixed orientation pointing up)
  s_glider = lv_img_create(parent);
  lv_img_set_src(s_glider, &img_glider_th);
  lv_obj_clear_flag(s_glider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_glider, LV_OBJ_FLAG_HIDDEN);
}

void ThermalDraw_Update(bool circling, int turnDir, float track)
{
  static uint32_t last = 0;
  static float    lastThetaG = -1.0f;
  static bool     active = false;
  static float    lastTrackDrawn = NAN;   // Last rendered ground track angle
  static int      lastTurnDrawn  = 99;
  uint32_t now = millis();
  if (now - last < 100) return;     // ~10 Hz rendering ceiling
  last = now;

  // Outside circling mode -> hide visual overlay
  if (!circling || isnan(track)) {
    if (active) {
      for (int i = 0; i < TH_BINS; i++) s_dots[i].on = false;
      lv_obj_add_flag(s_layer,  LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_glider, LV_OBJ_FLAG_HIDDEN);
      active = false; lastThetaG = -1.0f; lastTrackDrawn = NAN;
    }
    return;
  }

  bool justActivated = false;
  if (!active) { lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_HIDDEN); active = true; justActivated = true; }

  // In flight, GPS ground track only updates at ~1 Hz: redraw ONLY when track angle
  // has actually changed (zero overhead between consecutive NMEA frames).
  if (!justActivated && !isnan(lastTrackDrawn) &&
      turnDir == lastTurnDrawn && fabsf(track - lastTrackDrawn) < 0.5f) {
    return;
  }
  lastTrackDrawn = track; lastTurnDrawn = turnDir;

  // Peak lift bin (highlighted with yellow dot)
  int idxMax = -1; float vMax = -1e9f;
  for (int i = 0; i < TH_BINS; i++) {
    float v; if (!ThermalHelper_BinValue(i, &v)) continue;
    if (v > vMax) { vMax = v; idxMax = i; }
  }

  float thetaG = (turnDir < 0) ? 90.0f : 270.0f;
  const float binW = 360.0f / TH_BINS;
  for (int i = 0; i < TH_BINS; i++) {
    float v;
    if (!ThermalHelper_BinValue(i, &v)) { s_dots[i].on = false; continue; }
    float t = fabsf(v) / TH_V_SPAN;   // Size reflects intensity; color indicates sign
    if (t > 1) t = 1;
    int sz = TH_DOT_MIN + (int)(t * (TH_DOT_MAX - TH_DOT_MIN));
    uint32_t col;
    if      (i == idxMax && v > TH_NEUTRAL) col = TH_COL_MAX;  // Yellow = peak thermal core
    else if (v >  TH_NEUTRAL) col = TH_COL_UP;    // Red = positive lift
    else if (v < -TH_NEUTRAL) col = TH_COL_DOWN;  // Blue = sink
    else                      col = TH_COL_NEUT;  // Gray = neutral air
    float th = ((i + 0.5f) * binW - track + thetaG) * 0.01745329f;
    s_dots[i].x  = TH_CX + (int)(TH_RING_R * sinf(th));
    s_dots[i].y  = TH_CY - (int)(TH_RING_R * cosf(th));
    s_dots[i].sz = (uint8_t)sz; s_dots[i].col = col; s_dots[i].on = true;
  }

  lv_obj_invalidate(s_layer);

  // Glider symbol: reposition (and show) only when turn direction changes
  if (thetaG != lastThetaG) {
    float r = thetaG * 0.01745329f;
    int gx = TH_CX + (int)(TH_RING_R * sinf(r));
    int gy = TH_CY - (int)(TH_RING_R * cosf(r));
    lv_obj_set_pos(s_glider, gx - TH_GLIDER_SZ / 2, gy - TH_GLIDER_SZ / 2);
    lv_obj_clear_flag(s_glider, LV_OBJ_FLAG_HIDDEN);
    lastThetaG = thetaG;
  }
}
