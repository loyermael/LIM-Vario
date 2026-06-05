/* ============================================================
 *  L!M Vario - Firmware principal
 *  Carte : Waveshare ESP32-S3-Touch-LCD-2.1 (480x480 rond)
 *
 *  Drivers ecran/tactile/IMU : fournis par Waveshare
 *  Interface graphique        : generee par EEZ Studio (src/ui)
 *
 *  Tous les objets LVGL sont references par leur NOM EEZ (stable).
 *  Un "Build Files" dans EEZ ne casse plus rien.
 * ============================================================ */

#include "Wireless.h"
#include "Gyro_QMI8658.h"
#include "RTC_PCF85063.h"
#include "SD_Card.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"

#include "ui/ui.h"
#include "ui/screens.h"
#include "lim_link.h"
#include <math.h>

// ============================================================
//  LIAISON UART <- CALCULATEUR
// ============================================================
#define LINK_RX  44
#define LINK_TX  43

static volatile float g_vario    = 0.0f;
static volatile float g_varioInt = 0.0f;
static volatile float g_pressure = 0.0f;
static volatile float g_altitude = 0.0f;
static bool g_linkOk     = false;
static bool g_linkSynced = false;

static int32_t enc1Last = 0, enc2Last = 0;
static bool    enc1BtnLast = false, enc2BtnLast = false;
static volatile int g_volume = 50;

static uint32_t g_pktCount = 0;

// ============================================================
//  MacCready
// ============================================================
static volatile int g_mcTenths = 0;
#define MC_MIN_T 0
#define MC_MAX_T 50

// ============================================================
//  QUICK MENU
// ============================================================
enum MenuState { MENU_CLOSED, MENU_NAV, MENU_EDIT };
static volatile MenuState g_menuState = MENU_CLOSED;
static volatile int  g_menuIndex = 0;
static volatile bool g_menuDirty = true;

#define MENU_COUNT  7
#define MENU_SOUND  5
#define MENU_EXIT   6
#define MENU_ROW_H  44

static volatile int  g_qnh    = 1013;
static volatile int  g_water  = 0;
static volatile int  g_bugs   = 0;
static volatile int  g_weight = 70;
static volatile bool g_sinkSound = false;

static uint32_t btnDownTime  = 0;
static bool     btnLongFired = false;
#define LONG_PRESS_MS    600
#define MENU_TIMEOUT_MS  8000
static volatile uint32_t g_menuLastActivity = 0;

// ============================================================
//  LOGIQUE MENU (appelee depuis la tache encodeur, pas LVGL)
// ============================================================
static void menu_onButton()
{
  g_menuLastActivity = millis();
  switch (g_menuState) {
    case MENU_CLOSED: g_menuState = MENU_NAV; g_menuIndex = 0; break;
    case MENU_NAV:
      if (g_menuIndex == MENU_EXIT) g_menuState = MENU_CLOSED;
      else                          g_menuState = MENU_EDIT;
      break;
    case MENU_EDIT: g_menuState = MENU_NAV; break;
  }
  g_menuDirty = true;
}

static void menu_onLongPress()
{
  g_menuLastActivity = millis();
  Serial.println("== APPUI LONG -> Setup menu (a creer) ==");
  g_menuState = MENU_CLOSED;
  g_menuDirty = true;
}

static void menu_onRotate(long delta)
{
  g_menuLastActivity = millis();
  if (g_menuState == MENU_CLOSED) {
    g_mcTenths += (int)delta;
    if (g_mcTenths < MC_MIN_T) g_mcTenths = MC_MIN_T;
    if (g_mcTenths > MC_MAX_T) g_mcTenths = MC_MAX_T;
  } else if (g_menuState == MENU_NAV) {
    int i = g_menuIndex + (int)delta;
    if (i < 0)              i = 0;
    if (i > MENU_COUNT - 1) i = MENU_COUNT - 1;
    g_menuIndex = i;
    g_menuDirty = true;
  } else {
    switch (g_menuIndex) {
      case 0: g_qnh    += delta;       if (g_qnh<900)   g_qnh=900;   if (g_qnh>1100)   g_qnh=1100;   break;
      case 1: g_water  += delta * 10;  if (g_water<0)   g_water=0;   if (g_water>300)  g_water=300;  break;
      case 2: g_bugs   += delta * 10;  if (g_bugs<0)    g_bugs=0;    if (g_bugs>90)    g_bugs=90;    break;
      case 3: g_weight += delta;       if (g_weight<50) g_weight=50; if (g_weight>150) g_weight=150; break;
      case MENU_SOUND:
        if (delta > 0) g_sinkSound = true;
        else if (delta < 0) g_sinkSound = false;
        break;
    }
    g_menuDirty = true;
  }
}

// ============================================================
//  LIAISON UART
// ============================================================
static void Link_Init()
{
  Serial1.setRxBufferSize(512);
  Serial1.begin(LIM_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
}

static float altitude_from_qnh(float p_pa, int qnh_hpa)
{
  if (p_pa <= 0.0f || qnh_hpa <= 0) return 0.0f;
  return 44330.0f * (1.0f - powf(p_pa / ((float)qnh_hpa * 100.0f), 0.1902949f));
}

static void Link_HandleEncoders(const lim_packet_t* p)
{
  if (!g_linkSynced) {
    enc1Last = p->enc1_count; enc2Last = p->enc2_count;
    enc1BtnLast = p->enc1_btn; enc2BtnLast = p->enc2_btn;
    g_linkSynced = true;
    return;
  }
  uint32_t now = millis();
  long d1 = (long)p->enc1_count - (long)enc1Last;
  if (d1 != 0) { enc1Last = p->enc1_count; menu_onRotate(d1); }
  bool b1 = p->enc1_btn;
  if (b1 && !enc1BtnLast)  { btnDownTime = now; btnLongFired = false; }
  if (b1 && !btnLongFired && (now - btnDownTime) > LONG_PRESS_MS) {
    btnLongFired = true; menu_onLongPress();
  }
  if (!b1 && enc1BtnLast && !btnLongFired) menu_onButton();
  enc1BtnLast = b1;

  long d2 = (long)p->enc2_count - (long)enc2Last;
  if (d2 != 0) {
    enc2Last = p->enc2_count;
    g_volume += (int)d2;
    if (g_volume < 0)   g_volume = 0;
    if (g_volume > 100) g_volume = 100;
  }
  bool b2 = p->enc2_btn;
  if (!b2 && enc2BtnLast) { /* TODO bouton mode */ }
  enc2BtnLast = b2;
}

static void Link_Poll()
{
  static uint8_t buf[sizeof(lim_packet_t)];
  static size_t  idx = 0;
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();
    if (idx == 0) { if (b == LIM_SYNC0) buf[idx++] = b; continue; }
    if (idx == 1) {
      if (b == LIM_SYNC1)      buf[idx++] = b;
      else if (b == LIM_SYNC0) { buf[0] = b; idx = 1; }
      else                       idx = 0;
      continue;
    }
    buf[idx++] = b;
    if (idx == sizeof(buf)) {
      idx = 0;
      const lim_packet_t* p = (const lim_packet_t*)buf;
      if (lim_check(p)) {
        g_pktCount++;
        g_vario    = p->vario;
        g_varioInt = p->vario_int;
        g_pressure = p->pressure;
        g_altitude = altitude_from_qnh(g_pressure, g_qnh);
        g_linkOk   = true;
        Link_HandleEncoders(p);
      }
    }
  }
}

// ============================================================
//  APPLICATION LVGL (dans loop() = thread LVGL = safe)
// ============================================================

// Retourne le label "nom" de l'item de menu (pour le centrage)
static lv_obj_t* Menu_NameLabel(int idx)
{
  switch (idx) {
    case 0: return objects.obj4;      // QNH  (nom, x=25)
    case 1: return objects.obj3;      // Water B.
    case 2: return objects.obj2;      // Bugs
    case 3: return objects.obj1;      // Pilot Wt.
    case 4: return objects.obj0;      // Profil
    case 5: return objects.obj5;      // Sink Snd. (EEZ)
    case 6: return objects._lbl_exit; // Exit
  }
  return objects.obj4;
}

static void Menu_LvglSetup()
{
  lv_obj_set_pos(objects.item_list, -23, 0);
  lv_obj_set_size(objects.item_list, 360, 345);
  lv_obj_set_scroll_snap_y(objects.item_list, LV_SCROLL_SNAP_NONE);
  lv_obj_set_scrollbar_mode(objects.item_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_top(objects.item_list,    200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(objects.item_list, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_clip_corner(objects.quick_menu_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
}

static void Menu_Apply()
{
  if (!g_menuDirty) return;
  g_menuDirty = false;

  if (g_menuState == MENU_CLOSED) {
    lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  // Valeurs en blanc, jaune sur l'item en edition
  lv_obj_t* vals[6] = {
    objects.val_qnh, objects.val_water, objects.val_bugs,
    objects.val_weight, objects.val_profil, objects.obj6  // obj6 = val_sound (Mute/Full)
  };
  for (int i = 0; i < 6; i++)
    lv_obj_set_style_text_color(vals[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  if (g_menuState == MENU_EDIT && g_menuIndex < 6)
    lv_obj_set_style_text_color(vals[g_menuIndex], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);

  // Mise a jour des valeurs
  char buf[16];
  snprintf(buf, sizeof(buf), "%d",    g_qnh);    lv_label_set_text(objects.val_qnh,    buf);
  snprintf(buf, sizeof(buf), "%d L",  g_water);  lv_label_set_text(objects.val_water,  buf);
  snprintf(buf, sizeof(buf), "%d %%", g_bugs);   lv_label_set_text(objects.val_bugs,   buf);
  snprintf(buf, sizeof(buf), "%d kg", g_weight); lv_label_set_text(objects.val_weight, buf);
  lv_label_set_text(objects.obj6, g_sinkSound ? "Full" : "Mute"); // val_sound

  lv_obj_clear_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  // Centrage exact de l'item selectionne sur le cadre jaune
  lv_obj_update_layout(objects.quick_menu_panel);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame,       &fa);
  lv_obj_get_coords(Menu_NameLabel(g_menuIndex),   &la);
  lv_coord_t delta = ((la.y1 + la.y2) - (fa.y1 + fa.y2)) / 2;
  if (delta != 0) lv_obj_scroll_by(objects.item_list, 0, -delta, LV_ANIM_OFF);
}

static void Needles_Apply()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (g_menuState != MENU_CLOSED && (now - last) < 160) return;
  last = now;
  float v  = isnan(g_vario)    ? 0.0f : g_vario;
  float vi = isnan(g_varioInt) ? 0.0f : g_varioInt;
  if (screen_main_state.indicator2)
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator2,
                                 (int32_t)(v * 1000.0f));
  if (screen_main_state.indicator1)
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator1,
                                 (int32_t)(vi * 1000.0f));
}

static void MC_Apply()
{
  if (screen_main_state.indicator)
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator,
                                 (int32_t)g_mcTenths * 100);
}

// Mise a jour des labels numeriques (altitude, vario, vario integre)
// Maj uniquement quand la valeur change (evite des redraws inutiles)
static void Labels_Apply()
{
  if (g_menuState != MENU_CLOSED) return;  // masques par le menu

  // Altitude (arrondi au metre)
  if (objects.lbl_alt) {
    static int lastA = -999999;
    int a = (int)(g_altitude + (g_altitude >= 0 ? 0.5f : -0.5f));
    if (a != lastA) {
      lastA = a;
      char buf[16];
      snprintf(buf, sizeof(buf), "%d m", a);
      lv_label_set_text(objects.lbl_alt, buf);
    }
  }

  // Vario instantane (arrondi a 0.1 m/s)
  if (objects.lbl_vario) {
    static int lastV = -99999;
    float v = isnan(g_vario) ? 0.0f : g_vario;
    int vt = (int)(v * 10.0f + (v >= 0 ? 0.5f : -0.5f));
    if (vt != lastV) {
      lastV = vt;
      char buf[16];
      snprintf(buf, sizeof(buf), "%+.1f", v);
      lv_label_set_text(objects.lbl_vario, buf);
    }
  }

  // Vario integre (arrondi a 0.1 m/s)
  if (objects.lbl_vario_int) {
    static int lastVi = -99999;
    float vi = isnan(g_varioInt) ? 0.0f : g_varioInt;
    int vit = (int)(vi * 10.0f + (vi >= 0 ? 0.5f : -0.5f));
    if (vit != lastVi) {
      lastVi = vit;
      char buf[16];
      snprintf(buf, sizeof(buf), "%+.1f", vi);
      lv_label_set_text(objects.lbl_vario_int, buf);
    }
  }
}

static void Menu_AutoClose()
{
  if (g_menuState != MENU_CLOSED &&
      (millis() - g_menuLastActivity) > MENU_TIMEOUT_MS) {
    g_menuState = MENU_CLOSED;
    g_menuDirty = true;
  }
}

// ============================================================
//  Tache de fond : capteurs lents
// ============================================================
void Driver_Loop(void *parameter)
{
  while (1) {
    QMI8658_Loop();
    RTC_Loop();
    BAT_Get_Volts();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void Driver_Init()
{
  Flash_test();
  BAT_Init();
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Set_EXIO(EXIO_PIN8, Low);
  PCF85063_Init();
  QMI8658_Init();
  xTaskCreatePinnedToCore(Driver_Loop, "Other Driver task", 4096, NULL, 3, NULL, 0);
}

// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== L!M Vario boot ===");

  Serial.println(">> Wireless_Test2"); Wireless_Test2();
  Serial.println(">> Driver_Init");    Driver_Init();
  Serial.println(">> LCD_Init");       LCD_Init();
  Serial.println(">> SD_Init");        SD_Init();
  Serial.println(">> Lvgl_Init");      Lvgl_Init();
  Serial.println(">> ui_init");        ui_init();

  Serial.println(">> Menu_LvglSetup"); Menu_LvglSetup();
  Serial.println(">> Link_Init");      Link_Init();

  Serial.println(">> setup TERMINE OK");
}

void loop()
{
  Lvgl_Loop();
  Link_Poll();
  Needles_Apply();
  Labels_Apply();
  MC_Apply();
  Menu_AutoClose();
  Menu_Apply();

  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 2000) {
    lastDbg = millis();
    Serial.printf("[link] trames=%lu ok=%d | vario=%+.2f alt=%.0f vol=%d\n",
                  (unsigned long)g_pktCount, g_linkOk ? 1 : 0,
                  g_vario, g_altitude, g_volume);
  }

  vTaskDelay(pdMS_TO_TICKS(5));
}
