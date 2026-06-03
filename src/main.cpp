/* ============================================================
 *  L!M Vario - Firmware principal
 *  Carte : Waveshare ESP32-S3-Touch-LCD-2.1 (480x480 rond)
 *
 *  Drivers ecran/tactile/IMU : fournis par Waveshare
 *  Interface graphique        : generee par EEZ Studio (src/ui)
 * ============================================================ */

#include "Wireless.h"
#include "Gyro_QMI8658.h"
#include "RTC_PCF85063.h"
#include "SD_Card.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"

#include "ui/ui.h"        // Interface EEZ Studio (ui_init / ui_tick)
#include "ui/screens.h"   // Acces aux objets LVGL (meter = obj6)

#include <ESP32Encoder.h> // Encodeur MC (EC11)

// ============================================================
//  ENCODEUR (EC11) : A=GPIO43, B=GPIO44, bouton=GPIO0
// ============================================================
#define ENC_A   43
#define ENC_B   44
#define ENC_SW  0
static ESP32Encoder enc;
static long encLastSteps = 0;
static bool encBtnLast   = true;

// ============================================================
//  MacCready (regle par l'encodeur quand le menu est ferme)
// ============================================================
static volatile float g_mc = 0.0f;
#define MC_STEP  0.1f
#define MC_MIN   0.0f
#define MC_MAX   5.0f

// ============================================================
//  QUICK MENU
// ============================================================
enum MenuState { MENU_CLOSED, MENU_NAV, MENU_EDIT };
static volatile MenuState g_menuState = MENU_CLOSED;
static volatile int  g_menuIndex = 0;
static volatile bool g_menuDirty = true;   // demande d'appliquer a LVGL

#define MENU_COUNT  6     // QNH, Water, Bugs, PilotWt, Profil, Exit
#define MENU_EXIT   5
#define MENU_ROW_H  44    // espacement des lignes (px)

// Valeurs editables
static volatile int g_qnh    = 1013;
static volatile int g_water  = 0;
static volatile int g_bugs   = 0;
static volatile int g_weight = 70;   // poids de base 70 kg

// Appui court / long + auto-fermeture
static uint32_t btnDownTime  = 0;
static bool     btnLongFired = false;
#define LONG_PRESS_MS    600     // au-dela = appui long
#define MENU_TIMEOUT_MS  8000    // fermeture auto apres 8s d'inactivite
static volatile uint32_t g_menuLastActivity = 0;

// ---- Logique appelee depuis la TACHE encodeur : MET A JOUR L'ETAT SEULEMENT
//      (interdit d'appeler LVGL ici -> pas thread-safe)
// Appui COURT
static void menu_onButton()
{
  g_menuLastActivity = millis();
  switch (g_menuState) {
    case MENU_CLOSED:
      g_menuState = MENU_NAV;
      g_menuIndex = 0;
      break;
    case MENU_NAV:
      if (g_menuIndex == MENU_EXIT) g_menuState = MENU_CLOSED;
      else                          g_menuState = MENU_EDIT;
      break;
    case MENU_EDIT:
      g_menuState = MENU_NAV;
      break;
  }
  g_menuDirty = true;
}

// Appui LONG -> Setup menu (UI a creer). N'ouvre PAS le quick menu.
static void menu_onLongPress()
{
  g_menuLastActivity = millis();
  Serial.println("== APPUI LONG -> Setup menu (a creer) ==");
  // Si le quick menu etait ouvert, on le ferme
  g_menuState = MENU_CLOSED;
  g_menuDirty = true;
  // TODO: ouvrir l'ecran Setup quand il sera cree
}

static void menu_onRotate(long delta)
{
  g_menuLastActivity = millis();
  if (g_menuState == MENU_CLOSED) {
    // Reglage MacCready
    g_mc += (float)delta * MC_STEP;
    if (g_mc < MC_MIN) g_mc = MC_MIN;
    if (g_mc > MC_MAX) g_mc = MC_MAX;
  }
  else if (g_menuState == MENU_NAV) {
    int i = g_menuIndex + (int)delta;
    if (i < 0)             i = 0;
    if (i > MENU_COUNT - 1) i = MENU_COUNT - 1;
    g_menuIndex = i;
    g_menuDirty = true;
  }
  else { // MENU_EDIT
    switch (g_menuIndex) {
      case 0: g_qnh    += delta;      if (g_qnh<900)  g_qnh=900;   if (g_qnh>1100) g_qnh=1100; break;
      case 1: g_water  += delta * 10; if (g_water<0)  g_water=0;   if (g_water>300) g_water=300; break; // par 10
      case 2: g_bugs   += delta * 10; if (g_bugs<0)   g_bugs=0;    if (g_bugs>90)  g_bugs=90;   break; // par 10
      case 3: g_weight += delta;      if (g_weight<50) g_weight=50; if (g_weight>150) g_weight=150; break;
      // 4 = Profil (pas d'edition pour l'instant), 5 = Exit
    }
    g_menuDirty = true;
  }
}

static void Encoder_Read()
{
  long steps = enc.getCount() / 4;          // FullQuad : 4 comptages / cran
  if (steps != encLastSteps) {
    long delta = steps - encLastSteps;
    encLastSteps = steps;
    menu_onRotate(delta);
  }
  bool btn = digitalRead(ENC_SW);
  uint32_t now = millis();
  if (btn == LOW && encBtnLast == HIGH) {                 // appui (front descendant)
    btnDownTime = now;
    btnLongFired = false;
  }
  if (btn == LOW && !btnLongFired && (now - btnDownTime) > LONG_PRESS_MS) {
    btnLongFired = true;
    menu_onLongPress();                                   // APPUI LONG
  }
  if (btn == HIGH && encBtnLast == LOW) {                 // relache
    if (!btnLongFired) menu_onButton();                   // APPUI COURT
  }
  encBtnLast = btn;
}

static void Encoder_Task(void *p)
{
  for (;;) { Encoder_Read(); vTaskDelay(pdMS_TO_TICKS(2)); }
}

static void Encoder_Init()
{
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc.attachFullQuad(ENC_A, ENC_B);
  enc.setFilter(1023);
  enc.setCount(0);
  pinMode(ENC_SW, INPUT_PULLUP);
}

// ============================================================
//  APPLICATION LVGL  (appelee dans loop() = thread LVGL -> SAFE)
// ============================================================
static void Menu_LvglSetup()
{
  // La liste remplit tout le panneau -> clip au bord du cercle (pas avant)
  lv_obj_set_pos(objects.item_list, -23, 0);
  lv_obj_set_size(objects.item_list, 360, 345);
  lv_obj_set_scroll_snap_y(objects.item_list, LV_SCROLL_SNAP_NONE);
  lv_obj_set_scrollbar_mode(objects.item_list, LV_SCROLLBAR_MODE_OFF); // pas de barre
  // Padding : centre l'item dans le cadre (cadre a ~y157 dans le panneau)
  lv_obj_set_style_pad_top(objects.item_list, 140, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(objects.item_list, 175, LV_PART_MAIN | LV_STATE_DEFAULT);
  // Menu cache au demarrage
  lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
  // Le cadre garde son look EEZ (fond blanc transparent + contour) -> on n'y touche pas
}

static void Menu_Apply()
{
  if (!g_menuDirty) return;
  g_menuDirty = false;

  if (g_menuState == MENU_CLOSED) {
    lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  // Defilement INSTANTANE (pas d'anim) : reouverture directe sur QNH + moins de charge
  lv_obj_scroll_to_y(objects.item_list, g_menuIndex * MENU_ROW_H, LV_ANIM_OFF);

  // Couleur des VALEURS : jaune sur l'item en EDITION, blanc sinon
  lv_obj_t* vals[5] = { objects.val_qnh, objects.val_water, objects.val_bugs,
                        objects.val_weight, objects.val_profil };
  for (int i = 0; i < 5; i++)
    lv_obj_set_style_text_color(vals[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  if (g_menuState == MENU_EDIT && g_menuIndex < 5)
    lv_obj_set_style_text_color(vals[g_menuIndex], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);

  // Mise a jour des valeurs
  char buf[16];
  snprintf(buf, sizeof(buf), "%d",    g_qnh);    lv_label_set_text(objects.val_qnh,    buf);
  snprintf(buf, sizeof(buf), "%d L",  g_water);  lv_label_set_text(objects.val_water,  buf);
  snprintf(buf, sizeof(buf), "%d %%", g_bugs);   lv_label_set_text(objects.val_bugs,   buf);
  snprintf(buf, sizeof(buf), "%d kg", g_weight); lv_label_set_text(objects.val_weight, buf);

  // Afficher le menu (apres avoir tout positionne -> pas de flash)
  lv_obj_clear_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
}

// Fleche MC verte sur le meter (obj6), indicateur arrow_mc = state->indicator
static void MC_Apply()
{
  if (screen_main_state.indicator) {
    lv_meter_set_indicator_value(objects.obj6, screen_main_state.indicator,
                                 (int32_t)(g_mc * 1000.0f));
  }
}

// Fermeture automatique du menu apres inactivite
static void Menu_AutoClose()
{
  if (g_menuState != MENU_CLOSED &&
      (millis() - g_menuLastActivity) > MENU_TIMEOUT_MS) {
    g_menuState = MENU_CLOSED;
    g_menuDirty = true;
  }
}

// ============================================================
//  Tache de fond : capteurs lents (IMU, RTC, batterie)
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

  Serial.println(">> Encoder_Init");   Encoder_Init();
  xTaskCreatePinnedToCore(Encoder_Task, "encoder", 8192, NULL, 4, NULL, 0);

  Serial.println(">> setup TERMINE OK");
}

void loop()
{
  Lvgl_Loop();      // rendu LVGL
  ui_tick();        // vario TOUJOURS actif (lisible meme menu ouvert)
  MC_Apply();       // fleche MC verte
  Menu_AutoClose(); // ferme le menu apres inactivite
  Menu_Apply();     // applique l'etat du menu (thread LVGL, safe)
  vTaskDelay(pdMS_TO_TICKS(5));
}
