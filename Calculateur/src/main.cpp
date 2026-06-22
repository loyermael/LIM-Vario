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
#include "VarioSound.h"
#include "GpsLink.h"         // GPS via WiFi (AP fiable cote calculateur)

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

// TEST HP : si le BMP est absent, genere un vario synthetique pour
// entendre le son (bips facon Larus). Mettre a 0 une fois le BMP OK.
#define SOUND_TEST 1

// Sens des encodeurs : 1 = inverse (si l'encodeur tourne a l'envers)
#define ENC1_REVERSE 1
#define ENC2_REVERSE 0

// WiFi GPS : 0 = en pause (pas de point d'acces). 1 = actif.
#define GPS_WIFI 1

// ---- Capteurs ----
Adafruit_BMP3XX bmp;
MS4525DO        ms4525(0x28);
static bool     bmpOk    = false;
static bool     hasSpeed = false;

// ---- Encodeurs ----
ESP32Encoder enc1, enc2;

// ---- Son (Vario) ----
VarioSound varioSound(23); // Utilise la broche GPIO 23

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
static uint32_t bootMs    = 0;   // pour la temporisation de demarrage du vario

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
  bootMs = millis();
  
  // --- Son Vario ---
  varioSound.begin();
  varioSound.setSinkAlarm(false); // Par défaut: mode sink sound mute (pas de son en descente)

  // --- GPS via WiFi (point d'acces "LIM-Vario") ---
#if GPS_WIFI
  GpsLink_Begin();
#endif

  lastUs = micros();
}

// ---- Reception des commandes retour de l'ecran (lim_cmd_t) ----
// La liaison UART est full-duplex : pendant que le Calculateur envoie
// les trames vario, l'ecran envoie ses commandes (changement Sink Snd. etc.)
static void Cmd_Poll() {
  static uint8_t buf[sizeof(lim_cmd_t)];
  static size_t  idx = 0;
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (idx == 0) { if (b == LIM_CMD_SYNC0) buf[idx++] = b; continue; }
    if (idx == 1) {
      if      (b == LIM_CMD_SYNC1) buf[idx++] = b;
      else if (b == LIM_CMD_SYNC0) { buf[0] = b; idx = 1; }
      else                          idx = 0;
      continue;
    }
    buf[idx++] = b;
    if (idx == sizeof(buf)) {
      idx = 0;
      const lim_cmd_t* c = (const lim_cmd_t*)buf;
      if (lim_cmd_check(c)) {
        bool sinkOn = (c->cmd & LIM_CMD_SINK_SOUND) != 0;
        varioSound.setSinkAlarm(sinkOn);
        Serial.printf("[cmd] SinkSound=%s\n", sinkOn ? "Full" : "Mute");
      }
    }
  }
}

void loop() {
  // Reception des commandes retour de l'ecran (Sink Sound on/off, etc.)
  Cmd_Poll();

  // Le son doit etre rafraichi le plus souvent possible (non limite a 50Hz)
  varioSound.tick();

  // Reception GPS (NMEA UDP) - le plus souvent possible
#if GPS_WIFI
  GpsLink_Loop();
#endif

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
    if (isnan(alt_f)) alt_f = alt;
    // Temporisation de demarrage (~2 s) : le BMP + ses filtres se stabilisent.
    // Sinon la 1ere lecture cree un faux pic que l'integrateur 20 s garde
    // longtemps (vario tres negatif au demarrage qui remonte lentement).
    if (millis() - bootMs < 2000) {
      alt_f = alt;                          // suit l'altitude, sans deriver
      vario_f = vario_te = vario_int = 0.0f;
      spd_f = ema(spd_f, spd_raw, dt, 0.40f);
    } else {
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
    }
    // Met à jour la vitesse verticale pour le son
    varioSound.setVz(vario_te);
  } else {
    // Pas de capteur valide -> zero (jamais de NaN qui rendrait l'aiguille folle)
    vario_f = vario_te = vario_int = 0.0f;
    alt_f = 0.0f;
#if SOUND_TEST
    // TEST HP : vario synthetique oscillant (-1.5 .. +2.5 m/s sur ~8 s)
    // -> on entend les bips qui montent/accelerent (montee) puis le silence.
    float testVz = 0.5f + 2.0f * sinf(6.2832f * (millis() % 8000) / 8000.0f);
    varioSound.setVz(testVz);
#endif
  }

  // --- Construction + envoi de la trame vers l'ecran ---
  lim_packet_t pkt;
  pkt.pressure   = p_pa;                            // l'ecran calcule l'altitude avec le QNH
  pkt.vario      = vario_te;                        // = baro si pas de MS4525
  pkt.vario_int  = vario_int;
  // airspeed = MS4525 si present, sinon vitesse sol GPS (pour la compensation cote ecran)
  bool gpsOk     = GpsLink_HasFix();
  pkt.airspeed   = hasSpeed ? spd_f : (gpsOk ? GpsLink_GroundSpeed() : 0.0f);
  pkt.enc1_count = (ENC1_REVERSE ? -1 : 1) * (int32_t)(enc1.getCount() / 4); // crans
  pkt.enc2_count = (ENC2_REVERSE ? -1 : 1) * (int32_t)(enc2.getCount() / 4);
  pkt.enc1_btn   = (digitalRead(ENC1_SW) == LOW) ? 1 : 0;
  pkt.enc2_btn   = (digitalRead(ENC2_SW) == LOW) ? 1 : 0;
  uint8_t flags = 0;
  if (bmpOk)    flags |= LIM_FLAG_BMP_OK;
  if (hasSpeed) flags |= LIM_FLAG_SPD_OK;
  if (gpsOk)    flags |= LIM_FLAG_GPS_OK;
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
