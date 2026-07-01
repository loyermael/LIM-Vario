/* ============================================================
 *  L!M Vario - CALCULATOR UNIT (ESP32 DevKit V4)
 *  V0.8 : BMP388 -> altitude + instantaneous/integrated vario,
 *         2 rotary encoders read locally, all streamed to display via UART.
 *
 *  -> MS4525 airspeed sensor is AUTO-DETECTED: unequipped = uncompensated
 *     barometric vario; connected later = total energy (TE) compensation
 *     activates seamlessly without modification.
 *
 *  DevKit Pinout :
 *    I2C sensors : SDA 18, SCL 19     (BMP388 0x77, MS4525 0x28)
 *    Encoder 1   : A 32, B 33, SW 4   (MacCready / UI navigation)
 *    Encoder 2   : A 26, B 27, SW 14  (Volume / Flight mode)
 *    UART -> disp: TX 17, RX 16       (Serial2 @ LIM_BAUD)
 *    USB Debug   : Serial @ 115200
 * ============================================================ */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BMP3XX.h>
#include <ESP32Encoder.h>
#include "MS4525DO.h"
#include "lim_link.h"        // Shared IPC protocol (../Shared)
#include "VarioSound.h"
#include "GpsLink.h"         // WiFi GPS bridge (Reliable AP host on calculator unit)

// ---- Pin Definitions ----
#define PIN_SDA   18   // (moved from 21 due to suspected hardware damage on GPIO21)
#define PIN_SCL   19   // (moved from 22)
#define LINK_TX   17
#define LINK_RX   16
#define ENC1_A    32
#define ENC1_B    33
#define ENC1_SW   4    // moved from GPIO25 (DAC1 reserved for acoustic output) to GPIO4
#define ENC2_A    26
#define ENC2_B    27
#define ENC2_SW   14

// SPEAKER TEST: when BMP sensor is missing, injects synthetic vertical velocity
// to verify acoustic tone generation (Larus cadence). 0 = off (avoids overriding Condor/real vario).
#define SOUND_TEST 0

// Encoder rotation inversion: 1 = inverted direction
#define ENC1_REVERSE 1
#define ENC2_REVERSE 1

// WiFi GPS AP bridge: 0 = disabled (no AP broadcast). 1 = active.
#define GPS_WIFI 1

// BENCH SIMULATION: synthetic thermal spiral generator for testing without Condor.
// 0 = off. CONDOR UDP telemetry overrides this automatically upon receiving packets.
#define SIM_VARIO 0

// ---- Sensor Handlers ----
Adafruit_BMP3XX bmp;
MS4525DO        ms4525(0x28);
static bool     bmpOk    = false;
static bool     hasSpeed = false;

// ---- Encoders ----
ESP32Encoder enc1, enc2;

// ---- Audio Synthesizer ----
VarioSound varioSound(25); // DAC1 GPIO25 -> true analog sine wave output (clean, zero PWM rasp)

// ---- Physical Constants ----
static const float P0_PA = 101325.0f;
static const float G     = 9.80665f;
static const float R_AIR = 287.05f;

// ---- Filtering & Derivative State ----
static float    alt_f     = NAN;
static float    spd_f     = 0.0f;
static float    vario_f   = 0.0f;
static float    vario_te  = 0.0f;
static float    vario_int = 0.0f;
static uint32_t lastUs    = 0;
static uint32_t bootMs    = 0;   // Startup delay timestamp to ignore sensor boot transients
static float    g_simVz    = 0.0f;   // Simulated thermal vario (SIM_VARIO)
static float    g_simTrack = NAN;    // Simulated ground track heading streamed to display

// ---- Volume Control (ENC2) ----
static uint8_t  sndVol   = 0;   // Current acoustic volume 0..20 (default 0 = muted at boot)
static int32_t  enc2Prev = 0;   // Previous encoder detent position

static float altitude_from_p(float p_pa) {
  return 44330.0f * (1.0f - powf(p_pa / P0_PA, 0.1902949f));
}
static inline float ema(float val, float cible, float dt, float tau) {
  float a = dt / (tau + dt);
  return val + a * (cible - val);
}

void setup() {
  Serial.begin(115200);                                   // USB debug serial port
  Serial2.begin(LIM_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);  // Inter-processor UART link to display
  delay(300);
  Serial.println("\n=== L!M Vario - Calculator Unit V0.8 ===");

  // --- I2C + BMP388 Barometric Sensor ---
  Wire.begin(PIN_SDA, PIN_SCL);
  delay(100);             // Allow I2C bus lines to stabilize
  Wire.setClock(100000);  // 100kHz standard mode
  bmpOk = bmp.begin_I2C(0x77);
  if (!bmpOk) bmpOk = bmp.begin_I2C(0x76);
  if (bmpOk) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp.setOutputDataRate(BMP3_ODR_50_HZ);
    Serial.println("BMP388 OK");
  } else {
    Serial.println("!! BMP388 NOT FOUND");
  }

  // --- MS4525 Airspeed Sensor (Auto-detected) ---
  hasSpeed = ms4525.begin();
  Serial.println(hasSpeed ? "MS4525 present -> TE compensation active"
                          : "MS4525 absent -> uncompensated baro vario");

  // --- Encoders ---
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc1.attachFullQuad(ENC1_A, ENC1_B); enc1.setFilter(1023); enc1.clearCount();
  enc2.attachFullQuad(ENC2_A, ENC2_B); enc2.setFilter(1023); enc2.clearCount();
  pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_SW, INPUT_PULLUP);

  Serial.println("Ready. Streaming UART telemetry to display unit...");
  bootMs = millis();
  
  // --- Acoustic Vario Synthesizer ---
  varioSound.begin();
  varioSound.setVolume(sndVol);
  varioSound.setSinkAlarm(false);

  // --- WiFi GPS Access Point ("LIM-Vario") ---
#if GPS_WIFI
  GpsLink_Begin();
#endif

  lastUs = micros();
}

// ---- Process incoming control commands from display unit (lim_cmd_t) ----
// UART link is full-duplex: while calculator streams vario data frames,
// display sends UI configuration updates (e.g. Sink Sound mute/full toggle)
static void Cmd_Poll() {
  static uint8_t buf[16];
  static size_t  idx  = 0;
  static size_t  need = 0;   // Expected frame payload size depending on header sync bytes
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (idx == 0) {                                   // Wait for first known sync byte
      if (b == LIM_CMD_SYNC0 || b == LIM_SCFG_SYNC0) buf[idx++] = b;
      continue;
    }
    if (idx == 1) {                                   // Validate second sync byte matching first
      if      (buf[0] == LIM_CMD_SYNC0  && b == LIM_CMD_SYNC1)  { buf[idx++] = b; need = sizeof(lim_cmd_t);  }
      else if (buf[0] == LIM_SCFG_SYNC0 && b == LIM_SCFG_SYNC1) { buf[idx++] = b; need = sizeof(lim_scfg_t); }
      else if (b == LIM_CMD_SYNC0 || b == LIM_SCFG_SYNC0)       { buf[0] = b; idx = 1; }
      else                                                       { idx = 0; }
      continue;
    }
    if (idx >= sizeof(buf)) idx = 0;
    buf[idx++] = b;
    if (idx == need) {
      idx = 0;
      if (buf[0] == LIM_CMD_SYNC0) {                  // ---- Control Command (sink sound) ----
        const lim_cmd_t* c = (const lim_cmd_t*)buf;
        if (lim_cmd_check(c)) {
          bool sinkOn = (c->cmd & LIM_CMD_SINK_SOUND) != 0;
          varioSound.setSinkAlarm(sinkOn);
          Serial.printf("[cmd] SinkSound=%s\n", sinkOn ? "Full" : "Mute");
        }
      } else {                                        // ---- Audio Settings (pitch/wave/spread) ----
        const lim_scfg_t* s = (const lim_scfg_t*)buf;
        if (lim_scfg_check(s)) {
          varioSound.setCenterFreq((float)s->pitch);
          varioSound.setWaveform(s->wave);
          varioSound.setSpread(s->spread);
          Serial.printf("[cfg] pitch=%uHz wave=%u spread=%u\n", s->pitch, s->wave, s->spread);
        }
      }
    }
  }
}

void loop() {
  // Poll incoming feedback commands from display unit (Sink Sound toggle, acoustic setup)
  Cmd_Poll();

  // Audio tone generator must be refreshed as frequently as possible (not capped to 50Hz)
  varioSound.tick();

  // GPS Reception (NMEA UDP) - polled as frequently as possible
#if GPS_WIFI
  GpsLink_Loop();
#endif

  // Main processing loop rate ~50 Hz
  uint32_t nowUs = micros();
  float dt = (nowUs - lastUs) * 1e-6f;
  if (dt < 0.020f) return;
  lastUs = nowUs;

  // --- BMP388 Barometric Readings ---
  bool  gotBaro = false;
  float p_pa = P0_PA, tempC = 15.0f, alt = 0.0f;
  if (bmpOk && bmp.performReading()) {
    p_pa    = bmp.pressure;
    tempC   = bmp.temperature;
    alt     = altitude_from_p(p_pa);
    gotBaro = true;
  }
#if SIM_VARIO
  // FLIGHT SIMULATION: thermal circling inside an off-center updraft core.
  // Drives the ENTIRE chain: thermal velocity -> acoustic vario (here) + heading + pressure -> display
  // (needle movement via AHRS fusion + thermal helper visualization). Off-center offset oscillates (0..50 m).
  {
    float ts = millis() * 0.001f;
    const float period = 10.0f;                  // 1 circle every 10 s
    const float circR = 45.0f, Rt = 60.0f, Wmax = 3.5f;
    float aa = 2.0f * PI * ts / period;          // Angular position around circle
    float offset = 25.0f + 25.0f * sinf(ts * 2.0f * PI / 24.0f);  // Core offset 0..50 m
    float gx = offset + circR * cosf(aa);
    float gy = circR * sinf(aa);
    float xr = sqrtf(gx * gx + gy * gy) / Rt;
    g_simVz = Wmax * expf(-xr * xr) - 2.5f * expf(-1.2f * (xr - 1.7f) * (xr - 1.7f));
    // Ground track heading (tangent to circling flight path, right turn spiral) -> heading rotates
    float vx = -sinf(aa), vy = cosf(aa);
    g_simTrack = atan2f(vx, vy) * 180.0f / PI;
    if (g_simTrack < 0.0f) g_simTrack += 360.0f;
    // Integrated altitude from simulated vertical rate -> coherent pressure for screen AHRS fusion
    static float simAlt = 300.0f;
    simAlt += g_simVz * dt;
    if (simAlt < 50.0f)   simAlt = 50.0f;
    if (simAlt > 3000.0f) simAlt = 3000.0f;
    alt   = simAlt;
    p_pa  = P0_PA * powf(1.0f - alt / 44330.0f, 5.255f);
    gotBaro = true;
  }
#endif

  // --- MS4525 Airspeed Sensor -> true airspeed (if present) ---
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
    // Startup stabilization delay (~2 s): BMP sensor + hardware IIR filters settle down.
    // Otherwise the first reading creates a false spike that the 20-second integrator retains
    // for a long time (causing large false sink/climb transients upon boot).
    if (millis() - bootMs < 2000) {
      alt_f = alt;                          // Track raw altitude without deriving vertical speed
      vario_f = vario_te = vario_int = 0.0f;
      spd_f = ema(spd_f, spd_raw, dt, 0.40f);
    } else {
      float alt_prev = alt_f;
      alt_f = ema(alt_f, alt, dt, 0.30f);
      float vario_raw = (alt_f - alt_prev) / dt;
      vario_f = ema(vario_f, vario_raw, dt, 0.80f);

      // Total Energy (TE) compensation (zero effect if no airspeed: dV/dt = 0)
      float spd_prev = spd_f;
      spd_f = ema(spd_f, spd_raw, dt, 0.40f);
      float dVdt   = (spd_f - spd_prev) / dt;
      float te_raw = vario_f + (spd_f / G) * dVdt;
      vario_te = ema(vario_te, te_raw, dt, 0.80f);
      vario_int = ema(vario_int, vario_te, dt, 20.0f);
    }
    // Update audio synthesizer with current total energy vertical speed
    varioSound.setVz(vario_te);
  } else {
    // No valid sensor read -> default to zero (never propagate NaN which would glitch display needle)
    vario_f = vario_te = vario_int = 0.0f;
    alt_f = 0.0f;
#if SOUND_TEST
    // SPEAKER TEST: oscillating synthetic vario (-1.5 .. +2.5 m/s over ~8 s period)
    // -> audible ascending beep cadence followed by silence.
    float testVz = 0.5f + 2.0f * sinf(6.2832f * (millis() % 8000) / 8000.0f);
    varioSound.setVz(testVz);
#endif
  }

#if SIM_VARIO
  // Force simulated thermal vario (bypasses baro smoothing) -> clean sound + telemetry packet
  vario_te  = g_simVz;
  vario_int = ema(vario_int, vario_te, dt, 20.0f);
  varioSound.setVz(vario_te);
#endif

  // --- CONDOR Sim Mode (UDP): replaces barometric vario with Condor evario ---
  if (GpsLink_CondorActive()) {
    vario_te  = GpsLink_Vario();                       // Condor evario
    vario_int = ema(vario_int, vario_te, dt, 20.0f);
    varioSound.setVz(vario_te);                        // -> speaker follows Condor
  }

  // --- Acoustic Volume adjustment via ENC2 rotation ---
  {
    int32_t enc2Now = (ENC2_REVERSE ? -1 : 1) * (int32_t)(enc2.getCount() / 4);
    int32_t delta   = enc2Now - enc2Prev;
    if (delta != 0) {
      enc2Prev = enc2Now;
      int v = (int)sndVol + delta;
      if (v < 0)  v = 0;
      if (v > 20) v = 20;
      sndVol = (uint8_t)v;
      varioSound.setVolume(sndVol);
      Serial.printf("[vol] %d/20\n", sndVol);
    }
  }


  // --- Assemble + transmit telemetry frame to display unit ---
  lim_packet_t pkt;
  pkt.pressure   = p_pa;                            // Display unit computes altitude from QNH
  pkt.vario      = vario_te;                        // = uncompensated baro vario if no MS4525
  pkt.vario_int  = vario_int;
  // airspeed = MS4525 pitot speed if equipped, otherwise GPS ground speed (for screen-side TE comp)
  bool gpsOk     = GpsLink_HasFix();
  pkt.airspeed   = hasSpeed ? spd_f : (gpsOk ? GpsLink_GroundSpeed() : 0.0f);
  pkt.gps_track  = gpsOk ? GpsLink_Track() : NAN;   // Ground track heading for circling/wind UI
#if SIM_VARIO
  gpsOk          = true;           // SIM provides heading + speed -> activates circling mode on display
  pkt.airspeed   = 25.0f;          // Constant airspeed (dV/dt ~ 0, avoids false TE compensation spikes)
  pkt.gps_track  = g_simTrack;     // Rotating heading -> Circling_Apply + thermal helper ring
#endif
  if (GpsLink_CondorActive()) {
    // Pressure coherent with Condor altitude -> display AHRS fusion derives correct climb rate
    pkt.pressure = P0_PA * powf(1.0f - GpsLink_Altitude() / 44330.0f, 5.255f);
    pkt.airspeed = 30.0f;          // Constant -> avoids double TE compensation on display side
    // pkt.gps_track already assigned from GpsLink_Track() (= compass); gpsOk already true (HasFix)
  }
  pkt.enc1_count = (ENC1_REVERSE ? -1 : 1) * (int32_t)(enc1.getCount() / 4); // detent steps
  pkt.enc2_count = (ENC2_REVERSE ? -1 : 1) * (int32_t)(enc2.getCount() / 4);
  pkt.enc1_btn   = (digitalRead(ENC1_SW) == LOW) ? 1 : 0;
  pkt.enc2_btn   = (digitalRead(ENC2_SW) == LOW) ? 1 : 0;
  uint8_t flags = 0;
  if (bmpOk)    flags |= LIM_FLAG_BMP_OK;
  if (hasSpeed) flags |= LIM_FLAG_SPD_OK;
  if (gpsOk)    flags |= LIM_FLAG_GPS_OK;
  lim_finalize(&pkt, flags);
  Serial2.write((const uint8_t*)&pkt, sizeof(pkt));

  // --- USB Serial Telemetry Debug output (10 Hz) ---
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    Serial.printf("vario=%+5.2f | CONDOR=%d cv=%+5.2f trk=%5.1f alt=%6.1f | RSSI=%d dBm\n",
                  vario_te,
                  GpsLink_CondorActive() ? 1 : 0, GpsLink_Vario(),
                  GpsLink_Track(), GpsLink_Altitude(),
                  GpsLink_RSSI());
  }
}
