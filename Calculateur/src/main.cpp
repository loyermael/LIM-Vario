/* ============================================================
 *  L!M Vario - CALCULATOR UNIT (ESP32 DevKit V4)
 *  BMP388 -> altitude + instantaneous/integrated vario,
 *  2 rotary encoders read locally, all streamed to display via UART.
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
#include <Adafruit_LIS3MDL.h>
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
// to verify acoustic tone generation. 0 = off (avoids overriding Condor/real vario).
#define SOUND_TEST 0

// Encoder rotation inversion: 1 = inverted direction
#define ENC1_REVERSE 0
#define ENC2_REVERSE 1

// WiFi GPS AP bridge: 0 = disabled (no AP broadcast). 1 = active.
#define GPS_WIFI 1

// BENCH SIMULATION: synthetic thermal spiral generator for testing without Condor.
// 0 = off. CONDOR UDP telemetry overrides this automatically upon receiving packets.
#define SIM_VARIO 0

// ---- Sensor Handlers ----
Adafruit_BMP3XX bmp;
MS4525DO        ms4525(0x28);
Adafruit_LIS3MDL mag;
static bool     bmpOk    = false;
static bool     hasSpeed = false;
static bool     magOk    = false;

// ---- Encoders ----
ESP32Encoder enc1, enc2;

// ---- Audio Synthesizer ----
VarioSound varioSound(25, 22, 23); // I2S -> MAX98357A: DIN=25, BCLK=22, LRCLK=23

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
static float    g_lastGoodP_pa = NAN; // Last plausible raw pressure (outlier rejection, see below)
static bool     g_condorEnabled = false; // Condor telemetry honored only when display enables it (Condor sim toggle)

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

// One probe attempt (both possible addresses) + config on success. Shared between the
// boot-time retry loop and the hot-reconnect retry in loop(), so the oversampling/filter
// config can't drift out of sync between the two call sites.
static bool TryInitBmp() {
  bool ok = bmp.begin_I2C(0x77);
  if (!ok) ok = bmp.begin_I2C(0x76);
  if (ok) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    bmp.setOutputDataRate(BMP3_ODR_50_HZ);
  }
  return ok;
}

// Frees a slave that is holding SDA low. Happens when the ESP32 resets (flash, watchdog,
// brownout) mid-transfer: the slave was clocking out a byte whose current bit is 0, keeps
// SDA down waiting for the clocks that never come, and every later Wire call fails -> the
// sensor looks permanently absent even though it is fine (observed on the bench: SDA stuck
// at 0 with 0/127 addresses answering, recovered by this exact sequence). Bit-banging 9
// clocks lets it finish the byte, then a manual STOP releases the bus.
// Must run BEFORE Wire.begin() takes over the pins.
static void I2C_BusRecover() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delayMicroseconds(500);
  if (digitalRead(PIN_SDA) == HIGH) return;            // bus already idle
  Serial.println("[i2c] SDA held low -> bus recovery");
  pinMode(PIN_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
  }
  pinMode(PIN_SDA, OUTPUT); digitalWrite(PIN_SDA, LOW); delayMicroseconds(5);
  pinMode(PIN_SCL, INPUT_PULLUP);                       delayMicroseconds(5);
  pinMode(PIN_SDA, INPUT_PULLUP);                       delayMicroseconds(5);  // rising SDA while SCL high = STOP
  Serial.printf("[i2c] recovery %s\n", digitalRead(PIN_SDA) == HIGH ? "OK" : "FAILED");
}

// LIS3MDL magnetometer, shared bus (0x1C default, 0x1E if SDO strapped high). No calibration
// or tilt compensation here -> see mag_x/y/z comment in lim_link.h, this only brings the raw
// field up to the display, which owns the IMU and the AHRS fusion.
static bool TryInitMag() {
  bool ok = mag.begin_I2C(0x1C);
  if (!ok) ok = mag.begin_I2C(0x1E);
  if (ok) {
    mag.setPerformanceMode(LIS3MDL_MEDIUMMODE);
    mag.setOperationMode(LIS3MDL_CONTINUOUSMODE);
    mag.setDataRate(LIS3MDL_DATARATE_155_HZ);
    mag.setRange(LIS3MDL_RANGE_4_GAUSS);
  }
  return ok;
}

void setup() {
  Serial.begin(115200);                                   // USB debug serial port
  Serial2.begin(LIM_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);  // Inter-processor UART link to display
  delay(300);
  // LIM_FW_VERSION: generated at build time from Shared/VERSION + git hash by
  // Shared/version_gen.py (see extra_scripts in platformio.ini) -> bump the milestone
  // number in Shared/VERSION when you want to, the commit suffix takes care of itself.
  Serial.printf("\n=== L!M Vario - Calculator Unit v%s ===\n", LIM_FW_VERSION);

  // --- I2C + BMP388 Barometric Sensor ---
  I2C_BusRecover();
  Wire.begin(PIN_SDA, PIN_SCL);
  delay(100);             // Allow I2C bus lines to stabilize
  Wire.setClock(100000);  // 100kHz standard mode
  // Single-shot probes used to fail outright whenever the bus (or a marginal connection)
  // wasn't quite ready at this exact instant, latching bmpOk/hasSpeed false for the whole
  // session even though the sensor works fine a few hundred ms later. Retry loop absorbs
  // that startup race; it does NOT fix a genuinely bad/intermittent physical connection.
  for (int attempt = 0; attempt < 5 && !bmpOk; attempt++) {
    if (attempt > 0) delay(100);
    bmpOk = TryInitBmp();
  }
  Serial.println(bmpOk ? "BMP388 OK" : "!! BMP388 NOT FOUND");

  // --- MS4525 Airspeed Sensor (Auto-detected) ---
  for (int attempt = 0; attempt < 5 && !hasSpeed; attempt++) {
    if (attempt > 0) delay(100);
    hasSpeed = ms4525.begin();
  }
  Serial.println(hasSpeed ? "MS4525 present -> TE compensation active"
                          : "MS4525 absent -> uncompensated baro vario");

  // --- LIS3MDL Magnetometer (Auto-detected) ---
  for (int attempt = 0; attempt < 5 && !magOk; attempt++) {
    if (attempt > 0) delay(100);
    magOk = TryInitMag();
  }
  Serial.println(magOk ? "LIS3MDL present -> raw field streamed to display"
                       : "LIS3MDL absent -> no magnetic heading");

  // --- Encoders ---
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  enc1.attachFullQuad(ENC1_A, ENC1_B); enc1.setFilter(1023); enc1.clearCount();
  enc2.attachFullQuad(ENC2_A, ENC2_B); enc2.setFilter(1023); enc2.clearCount();
  pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_SW, INPUT_PULLUP);

  Serial.println("Ready. Streaming UART telemetry to display unit...");

  // --- Acoustic Vario Synthesizer ---
  varioSound.begin();
#if SOUND_TEST
  sndVol = 12;  // Forced audible level for bench speaker test (bypasses ENC2 volume state)
#endif
  varioSound.setVolume(sndVol);
  varioSound.setSinkAlarm(false);

  // --- WiFi GPS Access Point ("LIM-Vario") ---
#if GPS_WIFI
  GpsLink_Begin();
#endif

  bootMs  = millis();
  lastUs  = micros();
}

// Master vario received from the display (inertial-fused + TE vario). It drives
// the speaker so the beep matches the needle EXACTLY. Falls back to the local baro TE vario
// if the display link goes stale (link loss / display not sending yet).
static volatile float g_masterVario  = 0.0f;
static volatile bool  g_masterVarioOk = false;
static uint32_t       g_masterVarioMs = 0;
static inline bool MasterVario_Fresh() { return g_masterVarioOk && (millis() - g_masterVarioMs < 500); }

// ---- Process incoming control commands from display unit (lim_cmd_t) ----
// UART link is full-duplex: while calculator streams vario data frames, display sends UI
// config updates (Sink Sound, sound params) AND the master vario (30 Hz) for the speaker.
static void Cmd_Poll() {
  static uint8_t buf[16];
  static size_t  idx  = 0;
  static size_t  need = 0;   // Expected frame payload size depending on header sync bytes
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (idx == 0) {                                   // Wait for first known sync byte
      if (b == LIM_CMD_SYNC0 || b == LIM_SCFG_SYNC0 || b == LIM_VARIO_SYNC0) buf[idx++] = b;
      continue;
    }
    if (idx == 1) {                                   // Validate second sync byte matching first
      if      (buf[0] == LIM_CMD_SYNC0   && b == LIM_CMD_SYNC1)   { buf[idx++] = b; need = sizeof(lim_cmd_t);   }
      else if (buf[0] == LIM_SCFG_SYNC0  && b == LIM_SCFG_SYNC1)  { buf[idx++] = b; need = sizeof(lim_scfg_t);  }
      else if (buf[0] == LIM_VARIO_SYNC0 && b == LIM_VARIO_SYNC1) { buf[idx++] = b; need = sizeof(lim_vario_t); }
      else if (b == LIM_CMD_SYNC0 || b == LIM_SCFG_SYNC0 || b == LIM_VARIO_SYNC0) { buf[0] = b; idx = 1; }
      else                                                        { idx = 0; }
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
          g_condorEnabled = (c->cmd & LIM_CMD_CONDOR) != 0;
          GpsLink_SetCondorEnabled(g_condorEnabled);
          Serial.printf("[cmd] SinkSound=%s Condor=%s\n", sinkOn ? "Full" : "Mute", g_condorEnabled ? "ON" : "OFF");
        }
      } else if (buf[0] == LIM_SCFG_SYNC0) {          // ---- Audio Settings (pitch/wave/spread) ----
        const lim_scfg_t* s = (const lim_scfg_t*)buf;
        if (lim_scfg_check(s)) {
          varioSound.setCenterFreq((float)s->pitch);
          varioSound.setWaveform(s->wave);
          varioSound.setSpread(s->spread);
          Serial.printf("[cfg] pitch=%uHz wave=%u spread=%u\n", s->pitch, s->wave, s->spread);
        }
      } else {                                        // ---- Master vario (display -> speaker) ----
        const lim_vario_t* v = (const lim_vario_t*)buf;
        if (lim_vario_check(v)) {
          g_masterVario   = v->vario_cms * 0.01f;
          g_masterVarioOk = true;
          g_masterVarioMs = millis();
        }
      }
    }
  }
}

void loop() {
  // Hot-reconnect: a sensor plugged back in mid-session does NOT retrigger begin() on its
  // own (it's only called at boot) -> a currently-absent sensor would stay "NOT FOUND" for
  // the rest of the session even once the wiring is fixed. Re-probe every 3 s until it
  // succeeds; harmless once bmpOk/hasSpeed are true (the check short-circuits immediately).
  if (!bmpOk || !hasSpeed || !magOk) {
    static uint32_t lastRetryMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastRetryMs >= 3000) {
      lastRetryMs = nowMs;
      if (!bmpOk && TryInitBmp())     { bmpOk = true;    Serial.println("BMP388 OK (hot reconnect)"); }
      if (!hasSpeed && ms4525.begin()){ hasSpeed = true;  Serial.println("MS4525 present (hot reconnect) -> TE compensation active"); }
      if (!magOk && TryInitMag())     { magOk = true;    Serial.println("LIS3MDL present (hot reconnect) -> raw field streamed to display"); }
    }
  }

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
  // On a BMP388 read FAILURE (I2C hiccup), do NOT fall back to P0_PA (101325 Pa = sea
  // level): that default value was sent as-is to the display (pkt.pressure), which read it
  // as a ~200 m ALTITUDE DROP -> blow-up of the fused vario (+/-100 m/s spikes and more
  // observed in the real flight logs, 1st flight). We hold the last valid pressure instead
  // (P0_PA only if we have never had a reading).
  float p_pa = isnan(g_lastGoodP_pa) ? P0_PA : g_lastGoodP_pa;
  float tempC = 15.0f, alt = 0.0f;
  if (bmpOk && bmp.performReading()) {
    float p_raw = bmp.pressure;
    // Outlier rejection (2 July 2026): a one-off I2C hiccup on the BMP388 can return an
    // aberrant raw pressure, sent as-is at 50 Hz with no filter before this fix -> vario/
    // altitude spike on the screen side (observed: ~75 m jumps on the ground).
    // 100 Pa between two consecutive samples (dt ~20 ms) is ~625 m/s of vertical speed:
    // never reached in real flight, so only a genuine sensor glitch is rejected here
    // (the previous valid value is reused for this sample).
    // Anti-lockup counter: if the reference itself was the glitched value (e.g. the very
    // first reading at boot), ALL subsequent genuinely good readings would deviate from it
    // by +100 Pa and be rejected forever -> altitude frozen on the bad value (observed:
    // alt stuck ~20s). After 10 consecutive rejects (~200 ms), we accept the new reading
    // and reinitialize the reference.
    static uint8_t s_rejectStreak = 0;
    if (!isnan(g_lastGoodP_pa) && fabsf(p_raw - g_lastGoodP_pa) > 100.0f && s_rejectStreak < 10) {
      p_pa = g_lastGoodP_pa;
      s_rejectStreak++;
    } else {
      p_pa = p_raw;
      g_lastGoodP_pa = p_pa;
      s_rejectStreak = 0;
    }
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
  // Zero-offset calibration: MS4525 breakout boards commonly carry a small raw
  // differential-pressure bias at rest (part-to-part manufacturing spread, see
  // the warning in MS4525DO.h). Uncorrected, this reads as a nonzero airspeed
  // while stationary -> on the display side, FlightTime_Apply() mistakes a
  // sustained >11 m/s reading for a takeoff even on the ground. Learned as an
  // EMA of dp_pa during the same 2 s post-boot stabilization window already
  // used for the baro, assuming the glider is stationary (no wind on the
  // pitot) at power-on.
  static float dp_zero = 0.0f;
  float spd_raw = 0.0f;
  if (hasSpeed) {
    float dp_pa, t2;
    if (ms4525.read(dp_pa, t2)) {
      if (millis() - bootMs < 2000) dp_zero = ema(dp_zero, dp_pa, dt, 0.40f);
      float rho = p_pa / (R_AIR * (tempC + 273.15f));
      spd_raw = MS4525DO::airspeed_ms(dp_pa - dp_zero, rho);
      // Dead-band below the sensor's own noise floor: sqrt(dp) rectifies zero-mean
      // pressure noise into a small but nonzero *positive* speed (the negative half
      // of the noise is clamped to 0 by airspeed_ms, the positive half isn't) ->
      // reads as a phantom 1-2 km/h at rest even after zero-offset calibration.
      // 1.5 m/s (~5.4 km/h) is well under any operational threshold (TE switch at
      // 5 m/s, takeoff at 11 m/s in FlightTime_Apply), so clamping it away here is
      // display cleanliness, not a functional fix.
      if (spd_raw < 1.5f) spd_raw = 0.0f;
    }
  }

  // --- LIS3MDL Magnetometer -> raw field, streamed as-is (see mag_x/y/z in lim_link.h) ---
  float mx = 0.0f, my = 0.0f, mz = 0.0f;
  if (magOk) {
    sensors_event_t e;
    if (mag.getEvent(&e)) {
      mx = e.magnetic.x; my = e.magnetic.y; mz = e.magnetic.z;   // uT
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
    // Update audio synthesizer. Prefer the display's master vario (inertial+TE,
    // so the beep matches the needle); fall back to local baro TE if the link is stale.
    varioSound.setVz(MasterVario_Fresh() ? g_masterVario : vario_te);
  } else {
    // No valid sensor read -> vario to zero (never propagate NaN which would glitch display needle).
    // alt_f to NaN (not 0.0f): a transient BMP388 read failure must not fake a ground-level
    // altitude here, or the next valid read computes vario_raw=(real_alt-0)/dt -> a huge false
    // spike that the 20s integrator then bleeds off over several seconds (reads as "climbing on
    // its own"). NaN re-anchors alt_f to the real altitude via the isnan() check above instead.
    vario_f = vario_te = vario_int = 0.0f;
    alt_f = NAN;
  }

#if SOUND_TEST
  // BENCH SPEAKER TEST: unconditional oscillating synthetic vario (-1.5 .. +2.5 m/s over ~8 s
  // period), overrides whatever setVz() call happened above -> isolates the audio path
  // (I2S/MAX98357A/speaker) from sensor + display-link state.
  float testVz = 0.5f + 2.0f * sinf(6.2832f * (millis() % 8000) / 8000.0f);
  varioSound.setVz(testVz);
#endif

#if SIM_VARIO
  // Force simulated thermal vario (bypasses baro smoothing) -> clean sound + telemetry packet
  vario_te  = g_simVz;
  vario_int = ema(vario_int, vario_te, dt, 20.0f);
  varioSound.setVz(vario_te);
#endif

  // --- CONDOR Sim Mode (UDP): replaces barometric vario with Condor evario ---
  if (g_condorEnabled && GpsLink_CondorActive()) {
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
  bool gpsOk     = GpsLink_HasFix();
  // airspeed = true air speed (MS4525 pitot) only, 0 if no pitot. Ground speed sent separately.
  pkt.airspeed   = hasSpeed ? spd_f : 0.0f;
  pkt.gnd_speed  = gpsOk ? GpsLink_GroundSpeed() : 0.0f;  // GPS ground speed (screen-side TE comp)
  pkt.gps_alt    = gpsOk ? GpsLink_Altitude() : NAN;      // GPS altitude (separate from baro)
  pkt.gps_track  = gpsOk ? GpsLink_Track() : NAN;   // Ground track heading for circling/wind UI
  pkt.gps_lat    = gpsOk ? GpsLink_Lat() : NAN;     // Position, for flight log only (no UI use yet)
  pkt.gps_lon    = gpsOk ? GpsLink_Lon() : NAN;
  pkt.mag_x      = mx;                              // Raw field, uT. 0/0/0 if LIS3MDL absent
  pkt.mag_y      = my;                              // (LIM_FLAG_MAG_OK distinguishes that from
  pkt.mag_z      = mz;                              // a genuine all-zero reading).
  // UTC time/date (RMC), for flight-log timestamping -- utc_hour=0xFF sentinel if no valid
  // fix has carried a parseable time/date yet (do not trust utc_min/sec/day/month/year2 then).
  if (!GpsLink_UtcTime(&pkt.utc_hour, &pkt.utc_min, &pkt.utc_sec,
                       &pkt.utc_day, &pkt.utc_month, &pkt.utc_year2)) {
    pkt.utc_hour = 0xFF;
  }
#if SIM_VARIO
  gpsOk          = true;           // SIM provides heading + speed -> activates circling mode on display
  pkt.airspeed   = 25.0f;          // Constant airspeed (dV/dt ~ 0, avoids false TE compensation spikes)
  pkt.gnd_speed  = 25.0f;          // Constant -> preserves screen-side TE comp behavior in SIM
  pkt.gps_track  = g_simTrack;     // Rotating heading -> Circling_Apply + thermal helper ring
  pkt.gps_lat    = NAN;            // SIM has no simulated position
  pkt.gps_lon    = NAN;
#endif
  if (g_condorEnabled && GpsLink_CondorActive()) {
    // Pressure coherent with Condor altitude -> display AHRS fusion derives correct climb rate
    pkt.pressure  = P0_PA * powf(1.0f - GpsLink_Altitude() / 44330.0f, 5.255f);
    pkt.airspeed  = 30.0f;         // Constant -> avoids double TE compensation on display side
    pkt.gnd_speed = 30.0f;         // Constant -> preserves screen-side TE comp behavior in Condor
    pkt.gps_alt   = GpsLink_Altitude();   // Condor altitude
    // pkt.gps_track already assigned from GpsLink_Track() (= compass); gpsOk already true (HasFix)
    // pkt.gps_lat/lon: only real if Condor's NMEA output feeds position (GpsLink_Lat/Lon already
    // wired to the same parser as gps_track) -- left as whatever the block above set (NAN if
    // Condor is only sending compass/airspeed key=value, real if NMEA COM output is used).
  }
  pkt.enc1_count = (ENC1_REVERSE ? -1 : 1) * (int32_t)(enc1.getCount() / 4); // detent steps
  pkt.enc2_count = (ENC2_REVERSE ? -1 : 1) * (int32_t)(enc2.getCount() / 4);
  pkt.enc1_btn   = (digitalRead(ENC1_SW) == LOW) ? 1 : 0;
  pkt.enc2_btn   = (digitalRead(ENC2_SW) == LOW) ? 1 : 0;
  pkt.volume     = sndVol;   // authoritative -- display mirrors this instead of re-deriving it
  uint8_t flags = 0;
  if (bmpOk)    flags |= LIM_FLAG_BMP_OK;
  if (hasSpeed) flags |= LIM_FLAG_SPD_OK;
  if (gpsOk)    flags |= LIM_FLAG_GPS_OK;
  if (magOk)    flags |= LIM_FLAG_MAG_OK;
  lim_finalize(&pkt, flags);
  Serial2.write((const uint8_t*)&pkt, sizeof(pkt));

  // --- USB Serial Telemetry Debug output (10 Hz) ---
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint >= 100) {
    lastPrint = millis();
    // DEBUG bug "vario qui monte doucement au sol" : spd_f = airspeed MS4525 filtree,
    // vario_f = vario purement barometrique (avant compensation TE). Si spd_f derive
    // lentement alors que vario_f est stable -> c'est le MS4525 ; si vario_f derive
    // aussi -> derive barometrique du BMP388.
    Serial.printf("vario=%+5.2f baro=%+5.2f spd=%5.2f | CONDOR=%d cv=%+5.2f trk=%5.1f alt=%6.1f | RSSI=%d dBm | gps sat=%d q=%d | enc1_raw=%ld enc2_raw=%ld\n",
                  vario_te, vario_f, spd_f,
                  GpsLink_CondorActive() ? 1 : 0, GpsLink_Vario(),
                  GpsLink_Track(), GpsLink_Altitude(),
                  GpsLink_RSSI(),
                  GpsLink_NumSat(), GpsLink_FixQuality(),
                  (long)enc1.getCount(), (long)enc2.getCount());
  }
}
