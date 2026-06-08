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
static volatile int g_volume = 10;   // 0..20

static uint32_t g_pktCount = 0;

// ============================================================
//  SON VARIO (GPIO0 → MOSFET → buzzer piezo passif)
//  Algorithme style Larus/LX :
//    Montee  : freq 700→2000 Hz, cadence 1200→120 ms
//    Neutre  : silence (-0.3 à +0.15 m/s)
//    Descente: 350 Hz continu (si Full) ou silence (si Mute)
// ============================================================
#define VARIO_PIN          0          // GPIO0 → buzzer externe (futur)
// Buzzer interne Waveshare = EXIO_PIN8 via TCA9554 (I2C, software toggle)
#define VARIO_DEAD_LOW    -0.30f      // m/s : seuil descente
#define VARIO_DEAD_HIGH    0.15f      // m/s : seuil montee
#define VARIO_VMAX         3.0f       // m/s : vario max (au-dela = cadence max)
#define VARIO_FREQ_LOW     700        // Hz a DEAD_HIGH
#define VARIO_FREQ_HIGH    2000       // Hz a VMAX
#define VARIO_FREQ_SINK    350        // Hz descente
#define VARIO_PERIOD_SLOW  1000       // ms : 1 bip/s a DEAD_HIGH
#define VARIO_PERIOD_FAST   150       // ms : ~6 bips/s a VMAX
#define VARIO_DUTY_ON       0.50f     // 50% bip / 50% silence (plus audible)

// Arc de volume (affiche temporairement dans le moyeu quand enc2 tourne)
static lv_obj_t*  g_arcVol     = NULL;   // l'arc LVGL
static lv_obj_t*  g_lblVolNum  = NULL;   // le chiffre au centre de l'arc
static uint32_t   g_volShownAt = 0;      // timestamp du dernier changement
#define VOL_HIDE_MS  2000                // disparait apres 2s d'inactivite

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
#define MENU_SOUND  4     // Sink Snd. est visuellement a la position 4 dans EEZ (y≈176)
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
  if (d1 != 0) {
    enc1Last = p->enc1_count;
    // Anti-rebond : ignore les micro-oscillations (+1 puis -1 dans les 80ms)
    static int32_t lastDir1 = 0;
    static uint32_t lastRot1Ms = 0;
    int32_t dir = (d1 > 0) ? 1 : -1;
    uint32_t nowMs = millis();
    bool sameDir = (dir == lastDir1);
    bool slowEnough = (nowMs - lastRot1Ms) > 80;  // ignore rebond < 80ms
    if (sameDir || slowEnough) {
      menu_onRotate(d1);
      lastDir1  = dir;
      lastRot1Ms = nowMs;
    } else {
      // Rebond detecte : on met a jour lastDir sans appliquer
      lastDir1 = dir;
    }
  }
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
    if (g_volume < 0)  g_volume = 0;
    if (g_volume > 20) g_volume = 20;
    g_volShownAt = millis();  // declenche l'affichage de l'arc
  }
  bool b2 = p->enc2_btn;
  if (!b2 && enc2BtnLast) {
    // Appui court enc2 = bascule mute (volume 0 ↔ derniere valeur)
    static int savedVol = 10;
    if (g_volume > 0) { savedVol = g_volume; g_volume = 0; }
    else               { g_volume = savedVol; }
    g_volShownAt = millis();  // affiche l'arc volume
  }
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

// Retourne le label NOM de chaque item (utilise pour le centrage)
// Ordre VISUEL dans EEZ (par position Y croissante) :
//   0=QNH(y=0) 1=Water(y=44) 2=Bugs(y=88) 3=PilotWt(y=132)
//   4=SinkSnd(y=176) 5=Profil(y=220) 6=Exit(y=264)
static lv_obj_t* Menu_NameLabel(int idx)
{
  switch (idx) {
    case 0: return objects.obj4;       // "QNH"       y=0
    case 1: return objects.obj3;       // "Water B."  y=44
    case 2: return objects.obj2;       // "Bugs"      y=88
    case 3: return objects.obj1;       // "Pilot Wt." y=132
    case 4: return objects.obj5;       // "Sink Snd." y=176
    case 5: return objects.obj0;       // "Profil"    y=220
    case 6: return objects._lbl_exit;  // "Exit"      y=264
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
  // Corriger le decalage de 3px de Sink Snd. (EEZ le place a y=179 au lieu de y=176)
  lv_obj_set_y(objects.obj5, 176);   // "Sink Snd." → aligne sur la grille 44px
  lv_obj_set_y(objects.obj6, 176);   // "Mute/Full" → meme ligne
  lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  // --- Arc de volume dans le moyeu central ---
  g_arcVol = lv_arc_create(objects.center_hub);
  lv_obj_set_size(g_arcVol, 180, 180);
  lv_obj_center(g_arcVol);
  lv_arc_set_rotation(g_arcVol, 135);          // demarre en bas a gauche
  lv_arc_set_bg_angles(g_arcVol, 0, 270);      // arc de 270 degres
  lv_arc_set_range(g_arcVol, 0, 20);
  lv_arc_set_value(g_arcVol, g_volume);
  lv_obj_remove_style(g_arcVol, NULL, LV_PART_KNOB); // pas de poignee
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0xfbd500), LV_PART_INDICATOR | LV_STATE_DEFAULT); // jaune
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);      // fond gris
  lv_obj_set_style_arc_width(g_arcVol, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(g_arcVol, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(g_arcVol, LV_OBJ_FLAG_CLICKABLE);
  // Chiffre au centre
  g_lblVolNum = lv_label_create(objects.center_hub);
  lv_obj_set_style_text_font(g_lblVolNum, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(g_lblVolNum, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(g_lblVolNum);
  lv_label_set_text(g_lblVolNum, "50");
  // Cache par defaut
  lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
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
  // Ordre visuel EEZ : QNH, Water, Bugs, PilotWt, SinkSnd, Profil
  lv_obj_t* vals[6] = {
    objects.val_qnh,    // 0 = QNH
    objects.val_water,  // 1 = Water B.
    objects.val_bugs,   // 2 = Bugs
    objects.val_weight, // 3 = Pilot Wt.
    objects.obj6,       // 4 = Sink Snd. (Mute/Full)
    objects.val_profil  // 5 = Profil
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
  // Double update_layout : garantit que les coords sont valides apres affichage
  lv_obj_update_layout(objects.main);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame,     &fa);
  lv_obj_get_coords(Menu_NameLabel(g_menuIndex), &la);
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  lv_coord_t label_cy = (la.y1 + la.y2) / 2;
  lv_coord_t delta    = label_cy - frame_cy;
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

// Arc de volume : visible pendant VOL_HIDE_MS apres le dernier changement
static void Sound_Init()
{
  // Buzzer interne Waveshare = EXIO_PIN8 via TCA9554 (I2C)
  // Le piezo sonne a sa frequence naturelle quand on le met a HIGH.
  // On controle uniquement l'ENVELOPPE ON/OFF du bip (pas la frequence).
  // La vitesse de bip varie avec le vario → son identifiable meme a freq fixe.
  Set_EXIO(EXIO_PIN8, Low);  // silence au demarrage (deja fait dans Driver_Init)
}

static void Sound_Apply()
{
  static bool     bipOn   = false;   // bip actif (pin HIGH) ou silence (pin LOW)
  static uint32_t bipTime = 0;       // timestamp dernier changement d'etat
  uint32_t now = millis();

  float v = isnan(g_vario) ? 0.0f : g_vario;

  // --- Volume 0 = silence total ---
  if (g_volume == 0) {
    if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
    return;
  }

  // --- Zone morte : silence ---
  if (v > VARIO_DEAD_LOW && v < VARIO_DEAD_HIGH) {
    if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
    return;
  }

  // --- Descente ---
  if (v <= VARIO_DEAD_LOW) {
    if (!g_sinkSound) {
      if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
      return;
    }
    // Full : bip continu lent en descente (1 Hz)
    uint32_t half = 500;  // 500ms on / 500ms off
    if (now - bipTime >= half) {
      bipOn = !bipOn;
      Set_EXIO(EXIO_PIN8, bipOn ? High : Low);
      bipTime = now;
    }
    return;
  }

  // --- Montee : cadence de bip qui augmente avec le vario ---
  float t = (v - VARIO_DEAD_HIGH) / (VARIO_VMAX - VARIO_DEAD_HIGH);
  t = fminf(1.0f, fmaxf(0.0f, t));

  // Periode totale du bip : 1200ms (faible montee) → 120ms (forte montee)
  uint32_t period = (uint32_t)(VARIO_PERIOD_SLOW - t * (VARIO_PERIOD_SLOW - VARIO_PERIOD_FAST));
  uint32_t onMs   = (uint32_t)(period * VARIO_DUTY_ON);   // duree du bip
  uint32_t offMs  = period - onMs;                         // duree du silence

  if (bipOn) {
    if (now - bipTime >= onMs) {
      Set_EXIO(EXIO_PIN8, Low);
      bipOn   = false;
      bipTime = now;
    }
  } else {
    if (now - bipTime >= offMs) {
      Set_EXIO(EXIO_PIN8, High);
      bipOn   = true;
      bipTime = now;
    }
  }
}

static void Vol_Apply()
{
  if (!g_arcVol || !g_lblVolNum) return;
  bool shouldShow = (g_volShownAt > 0) &&
                    ((millis() - g_volShownAt) < VOL_HIDE_MS) &&
                    (g_menuState == MENU_CLOSED);  // masque si menu ouvert
  if (shouldShow) {
    lv_arc_set_value(g_arcVol, g_volume);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_volume);
    lv_label_set_text(g_lblVolNum, buf);
    lv_obj_clear_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
  }
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
  Serial.println(">> Sound_Init");     Sound_Init();

  Serial.println(">> setup TERMINE OK");
}

void loop()
{
  Lvgl_Loop();
  Link_Poll();
  Needles_Apply();
  Labels_Apply();
  Sound_Apply(); // son vario (GPIO0 → buzzer)
  Vol_Apply();   // arc volume temporaire (encodeur 2)
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
