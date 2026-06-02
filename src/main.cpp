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
#include "ui/screens.h"   // Acces aux objets LVGL (objects.obj0 = meter)

#include <ESP32Encoder.h> // Encodeur MC (EC11)

// --- Encodeur MC (EC11) : A=GPIO43, B=GPIO44, bouton=GPIO0 ---
#define ENC_MC_A    43
#define ENC_MC_B    44
#define ENC_MC_SW   0
static ESP32Encoder mcEnc;
static long mcLastSteps = 0;
static bool mcBtnLast = true;

// Valeur MacCready reglee par l'encodeur (m/s)
static float g_mc = 0.0f;
#define MC_STEP   0.1f
#define MC_MIN    0.0f
#define MC_MAX    5.0f

static void Encoder_MC_Init()
{
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  mcEnc.attachFullQuad(ENC_MC_A, ENC_MC_B);
  mcEnc.setFilter(1023);          // filtre anti-rebond hardware
  mcEnc.setCount(0);
  pinMode(ENC_MC_SW, INPUT_PULLUP);
}

static void Encoder_MC_Read()
{
  // FullQuad = 4 comptages par cran
  long steps = mcEnc.getCount() / 4;
  if (steps != mcLastSteps) {
    long delta = steps - mcLastSteps;
    mcLastSteps = steps;

    // Ajuste le MacCready
    g_mc += (float)delta * MC_STEP;
    if (g_mc < MC_MIN) g_mc = MC_MIN;
    if (g_mc > MC_MAX) g_mc = MC_MAX;
    Serial.printf("MC = %.1f m/s\n", g_mc);
  }
  bool btn = digitalRead(ENC_MC_SW);
  if (btn == LOW && mcBtnLast == HIGH) {
    Serial.println("MC bouton: APPUI");
  }
  mcBtnLast = btn;
}

// Applique la valeur MC sur la fleche verte (apres le tick du flow)
static void Encoder_MC_Apply()
{
  if (screen_main_state.indicator1) {
    lv_meter_set_indicator_value(objects.obj0, screen_main_state.indicator1,
                                 (int32_t)(g_mc * 1000.0f));
  }
}

// Tache dediee : lit l'encodeur toutes les 2 ms (independant du rendu)
static void Encoder_Task(void *param)
{
  for (;;) {
    Encoder_MC_Read();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// --- Tache de fond : capteurs lents (IMU, RTC, batterie) ---
void Driver_Loop(void *parameter)
{
  while (1)
  {
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

  xTaskCreatePinnedToCore(
    Driver_Loop,
    "Other Driver task",
    4096,
    NULL,
    3,
    NULL,
    0
  );
}

void setup()
{
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== L!M Vario boot ===");

  Serial.println(">> Wireless_Test2"); Wireless_Test2();
  Serial.println(">> Driver_Init");    Driver_Init();
  Serial.println(">> LCD_Init");       LCD_Init();        // Init ecran (avant SD)
  Serial.println(">> SD_Init");        SD_Init();
  Serial.println(">> Lvgl_Init");      Lvgl_Init();       // Init LVGL + buffers
  Serial.println(">> ui_init");        ui_init();         // Interface L!M Vario

  // Correctif arc arrondi (meter lit arc_rounded sur LV_PART_ITEMS)
  lv_obj_set_style_arc_rounded(objects.obj0, true, LV_PART_ITEMS | LV_STATE_DEFAULT);

  Serial.println(">> Encoder_MC_Init"); Encoder_MC_Init();
  // Tache rapide de lecture encodeur (core 0, toutes les 2 ms)
  xTaskCreatePinnedToCore(Encoder_Task, "encoder", 2048, NULL, 4, NULL, 0);

  Serial.println(">> setup TERMINE OK");
}

void loop()
{
  Lvgl_Loop();       // Gere le rendu LVGL
  ui_tick();         // <-- Moteur Flow EEZ (anime l'aiguille)
  Encoder_MC_Apply();// <-- Applique MC sur la fleche verte (apres le flow)
  vTaskDelay(pdMS_TO_TICKS(5));
}
