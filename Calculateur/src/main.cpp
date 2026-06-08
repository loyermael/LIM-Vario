/* ============================================================
 *  L!M Vario - CALCULATEUR (ESP32 DevKit V4)
 *  V0.5 : BMP388 -> altitude + vario + vario integre,
 *         2 encodeurs lus ici, le tout envoye a l'ecran par UART.
 *
 *  -> Le MS4525 est AUTO-DETECTE : non branche = vario baro non
 *     compense ; branche un jour = compensation TE active sans
 *     toucher au code.
 *
 *  Brochage DevKit :
 *    I2C capteurs : SDA 21, SCL 22   (BMP388 0x77, MS4525 0x28)
 *    Encodeur 1   : A 32, B 33, SW 25   (MC / menu)
 *    Encodeur 2   : A 26, B 27, SW 14   (volume / mode)
 *    UART -> ecran: TX 17, RX 16        (Serial2 @ LIM_BAUD)
 *    Debug USB    : Serial @ 115200
 * ============================================================ */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP3XX.h>
#include <ESP32Encoder.h>
#include "MS4525DO.h"
#include "lim_link.h"        // protocole partage (../Shared)

// ---- Broches ----
#define PIN_SDA   18   // (ancien 21, change car GPIO21 suspecte abimee)
#define PIN_SCL   19   // (ancien 22)
#define LINK_TX   17
#define LINK_RX   16
#define ENC1_A    32
#define ENC1_B    33
#define ENC1_SW   25
#define ENC2_A    26
#define ENC2_B    27
#define ENC2_SW   14

// ---- Capteurs ----
Adafruit_BMP3XX bmp;
MS4525DO        ms4525(0x28);
static bool     bmpOk    = false;
static bool     hasSpeed = false;

// ---- Encodeurs ----
ESP32Encoder enc1, enc2;

// ---- Constantes ----
static const float P0_PA = 101325.0f;
static const float G     = 9.80665f;
static const float R_AIR = 287.05f;

// ---- Etat filtres / derivees ----
static float    alt_f     = NAN;
static float    spd_f     = 0.0f;
static float    vario_f   = 0.0f;
static float    vario_te  = 0.0f;
static float    vario_int = 0.0f;
static uint32_t lastUs    = 0;

static float altitude_from_p(float p_pa) {
  return 44330.0f * (1.0f - powf(p_pa / P0_PA, 0.1902949f));
}
static inline float ema(float val, float cible, float dt, float tau) {
  float a = dt / (tau + dt);
  return val + a * (cible - val);
}

void setup() {
  Serial.begin(115200);                                   // debug USB
  Serial2.begin(LIM_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);  // lien vers l'ecran
  delay(300);
  Serial.println("\n=== L!M Vario - Calculateur V0.5 ===");

  // --- I2C + BMP388 ---
  Wire.begin(PIN_SDA, PIN_SCL);
  delay(100);             // laisse le bus se stabiliser
  Wire.setClock(100000);  // 100kHz
  bmpOk = bmp.begin_I2C(0x77);
  if (!bmpOk) bmpOk = bmp.begin_I2C(0x76);
  if (bmpOk) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp.setOutputDataRate(BMP3_ODR_50_HZ);
    Serial.println("BMP388 OK");
  } else {
    Serial.println("!! BMP388 INTROUVABLE");
  }

  // --- MS4525 (auto-detecte) ---
  hasSpeed = ms4525.begin();
  Serial.println(hasSpeed ? "MS4525 present -> compensation TE"
                          : "MS4525 absent -> vario baro (non compense)");

  // --- Encodeurs ---
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc1.attachFullQuad(ENC1_A, ENC1_B); enc1.setFilter(1023); enc1.clearCount();
  enc2.attachFullQuad(ENC2_A, ENC2_B); enc2.setFilter(1023); enc2.clearCount();
  pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_SW, INPUT_PULLUP);

  Serial.println("Pret. Envoi trame UART vers l'ecran...");
  lastUs = micros();
}

void loop() {
  // Cadence ~50 Hz
  uint32_t nowUs = micros();
  float dt = (nowUs - lastUs) * 1e-6f;
  if (dt < 0.020f) return;
  lastUs = nowUs;

  // --- BMP388 ---
  bool  gotBaro = false;
  float p_pa = P0_PA, tempC = 15.0f, alt = 0.0f;
  if (bmpOk && bmp.performReading()) {
    p_pa    = bmp.pressure;
    tempC   = bmp.temperature;
    alt     = altitude_from_p(p_pa);
    gotBaro = true;
  }

  // --- MS4525 -> vitesse air (si present) ---
  float spd_raw = 0.0f;
  if (hasSpeed) {
    float dp_pa, t2;
    if (ms4525.read(dp_pa, t2)) {
      float rho = p_pa / (R_AIR * (tempC + 273.15f));
      spd_raw = MS4525DO::airspeed_ms(dp_pa, rho);
    }
  }

  if (gotBaro) {
    // --- Filtres / vario ---
    if (isnan(alt_f)) alt_f = alt;
    float alt_prev = alt_f;
    alt_f = ema(alt_f, alt, dt, 0.30f);
    float vario_raw = (alt_f - alt_prev) / dt;
    vario_f = ema(vario_f, vario_raw, dt, 0.80f);

    // Compensation TE (sans effet si pas de vitesse : dV/dt = 0)
    float spd_prev = spd_f;
    spd_f = ema(spd_f, spd_raw, dt, 0.40f);
    float dVdt   = (spd_f - spd_prev) / dt;
    float te_raw = vario_f + (spd_f / G) * dVdt;
    vario_te = ema(vario_te, te_raw, dt, 0.80f);

    vario_int = ema(vario_int, vario_te, dt, 20.0f);
  } else {
    // Pas de capteur valide -> zero (jamais de NaN qui rendrait l'aiguille folle)
    vario_f = vario_te = vario_int = 0.0f;
    alt_f = 0.0f;
  }

  // --- Construction + envoi de la trame vers l'ecran ---
  lim_packet_t pkt;
  pkt.pressure   = p_pa;                            // l'ecran calcule l'altitude avec le QNH
  pkt.vario      = vario_te;                        // = baro si pas de MS4525
  pkt.vario_int  = vario_int;
  pkt.airspeed   = spd_f;
  pkt.enc1_count = (int32_t)(enc1.getCount() / 4); // crans
  pkt.enc2_count = (int32_t)(enc2.getCount() / 4);
  pkt.enc1_btn   = (digitalRead(ENC1_SW) == LOW) ? 1 : 0;
  pkt.enc2_btn   = (digitalRead(ENC2_SW) == LOW) ? 1 : 0;
  uint8_t flags = 0;
  if (bmpOk)    flags |= LIM_FLAG_BMP_OK;
  if (hasSpeed) flags |= LIM_FLAG_SPD_OK;
  lim_finalize(&pkt, flags);
  Serial2.write((const uint8_t*)&pkt, sizeof(pkt));

  // --- Debug USB (10 Hz) ---
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.printf("alt=%7.1f  vario=%+5.2f  int=%+5.2f  e1=%ld(%d)  e2=%ld(%d)\n",
                  alt_f, vario_te, vario_int,
                  (long)pkt.enc1_count, pkt.enc1_btn,
                  (long)pkt.enc2_count, pkt.enc2_btn);
  }
}
