/* ============================================================
 *  L!M Vario - IMU + Barometric Sensor Fusion
 *  (see VarioFusion.h for system architecture details)
 * ============================================================ */
#include "VarioFusion.h"
#include <Arduino.h>
#include <math.h>

// ------------------------------------------------------------
//  Configuration Tuning Parameters
// ------------------------------------------------------------
// Mahony AHRS
#define KP_ALIGN     8.0f     // High proportional gain during initial alignment
#define KP_RUN       0.35f    // Low proportional gain during flight (accelerometer vector deflects in turns)
#define ALIGN_MS     3000     // Initial alignment duration at startup (milliseconds)
// Kalman Filter (Process and measurement noise covariances; tune in flight if needed)
#define Q_ACC        1.0f     // (m/s^2)^2/s : Vario agility / responsiveness (higher = faster response)
#define Q_BIAS       1e-4f    // Slow drift rate of vertical accelerometer bias
#define R_ALT        0.25f    // m^2        : Barometric altitude measurement noise (~0.5 m std dev)
#define R_ACC        0.36f    // (m/s^2)^2  : Vertical acceleration noise (~0.6 m/s^2 std dev)
// DISPLAY time constant (output smoothing filter, similar to Larus/LX instruments:
// internal Kalman state tracks rapidly, while output needle display is gently smoothed)
#define OUT_TAU      0.7f     // seconds : 0.4 = responsive/snappy, 1.0 = smooth/calm

#define G_MS2        9.80665f
#define DEG2RAD      0.017453293f

// ------------------------------------------------------------
//  Internal State Variables
// ------------------------------------------------------------
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;  // Body-to-earth orientation quaternion
static bool     ahrsInit  = false;
static uint32_t startMs   = 0;
static uint32_t lastUs    = 0;

// Kalman Filter state vector: x = { h, v, a, bias } ; covariance matrix P
static float    x[4];
static float    P[4][4];
static bool     kalInit = false;

static float altitude_std(float p_pa)   // Standard pressure altitude (QNH-independent)
{
  return 44330.0f * (1.0f - powf(p_pa / 101325.0f, 0.1902949f));
}

// ------------------------------------------------------------
//  Mahony AHRS (Gravity vector correction only, no magnetometer)
// ------------------------------------------------------------
static void mahony_update(float gx, float gy, float gz,   // rad/s
                          float ax, float ay, float az,   // g (arbitrary norm)
                          float kp, float dt)
{
  float n = sqrtf(ax*ax + ay*ay + az*az);
  if (n > 0.5f && n < 1.5f) {            // Apply correction only if |a| ~ 1 g
    ax /= n; ay /= n; az /= n;
    // Estimated gravity vector in body frame (3rd row of transposed R matrix)
    float vx = 2.0f*(q1*q3 - q0*q2);
    float vy = 2.0f*(q0*q1 + q2*q3);
    float vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;
    // Error = measured acceleration cross estimated gravity
    float ex = ay*vz - az*vy;
    float ey = az*vx - ax*vz;
    float ez = ax*vy - ay*vx;
    gx += kp * ex;  gy += kp * ey;  gz += kp * ez;
  }
  // Quaternion integration: dq = 0.5 * q * omega
  float halfdt = 0.5f * dt;
  float dq0 = (-q1*gx - q2*gy - q3*gz) * halfdt;
  float dq1 = ( q0*gx + q2*gz - q3*gy) * halfdt;
  float dq2 = ( q0*gy - q1*gz + q3*gx) * halfdt;
  float dq3 = ( q0*gz + q1*gy - q2*gx) * halfdt;
  q0 += dq0; q1 += dq1; q2 += dq2; q3 += dq3;
  float qn = sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
  q0 /= qn; q1 /= qn; q2 /= qn; q3 /= qn;
}

// Earth-frame vertical acceleration (m/s^2, + upwards, gravity removed)
static float vertical_accel(float ax, float ay, float az)
{
  float ez = 2.0f*(q1*q3 - q0*q2)*ax
           + 2.0f*(q2*q3 + q0*q1)*ay
           + (q0*q0 - q1*q1 - q2*q2 + q3*q3)*az;  // in g units, ~+1 at rest
  return (ez - 1.0f) * G_MS2;
}

// ------------------------------------------------------------
//  4-State Kalman Filter (online covariance propagation)
//  Measurements : z_alt = h    |  z_acc = a + bias
// ------------------------------------------------------------
static void kalman_reset(float h0)
{
  x[0] = h0;  x[1] = 0.0f;  x[2] = 0.0f;  x[3] = 0.0f;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) P[i][j] = 0.0f;
  P[0][0] = 100.0f; P[1][1] = 10.0f; P[2][2] = 10.0f; P[3][3] = 1.0f;
  kalInit = true;
}

static void kalman_predict(float dt)
{
  float dt2 = dt * dt * 0.5f;
  // x = F x
  x[0] += dt * x[1] + dt2 * x[2];
  x[1] += dt * x[2];
  // P = F P F' + Q   (F = [[1 dt dt2 0][0 1 dt 0][0 0 1 0][0 0 0 1]])
  float FP[4][4];
  for (int j = 0; j < 4; j++) {
    FP[0][j] = P[0][j] + dt * P[1][j] + dt2 * P[2][j];
    FP[1][j] = P[1][j] + dt * P[2][j];
    FP[2][j] = P[2][j];
    FP[3][j] = P[3][j];
  }
  for (int i = 0; i < 4; i++) {
    P[i][0] = FP[i][0] + dt * FP[i][1] + dt2 * FP[i][2];
    P[i][1] = FP[i][1] + dt * FP[i][2];
    P[i][2] = FP[i][2];
    P[i][3] = FP[i][3];
  }
  P[2][2] += Q_ACC  * dt;
  P[3][3] += Q_BIAS * dt;
}

// Altitude measurement update : H = [1 0 0 0]
static void kalman_update_alt(float z)
{
  float S = P[0][0] + R_ALT;
  float y = z - x[0];
  float K[4];
  for (int i = 0; i < 4; i++) K[i] = P[i][0] / S;
  for (int i = 0; i < 4; i++) x[i] += K[i] * y;
  float HP[4];
  for (int j = 0; j < 4; j++) HP[j] = P[0][j];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) P[i][j] -= K[i] * HP[j];
}

// Acceleration measurement update : H = [0 0 1 1]  (measurement includes bias)
static void kalman_update_acc(float z)
{
  float S = P[2][2] + P[3][3] + 2.0f*P[2][3] + R_ACC;
  float y = z - x[2] - x[3];
  float K[4];
  for (int i = 0; i < 4; i++) K[i] = (P[i][2] + P[i][3]) / S;
  for (int i = 0; i < 4; i++) x[i] += K[i] * y;
  float HP[4];
  for (int j = 0; j < 4; j++) HP[j] = P[2][j] + P[3][j];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) P[i][j] -= K[i] * HP[j];
}

// ------------------------------------------------------------
//  API Implementation
// ------------------------------------------------------------
static float g_lastAVert = 0.0f;

bool VarioFusion_Ready(void)
{
  return kalInit && (millis() - startMs > ALIGN_MS);
}

float VarioFusion_GetVertAccel(void) { return g_lastAVert; }

float VarioFusion_Step(float ax, float ay, float az,
                       float gx, float gy, float gz,
                       float p_pa, bool newBaro, float baroVario)
{
  uint32_t nowUs = micros();
  uint32_t nowMs = millis();

  // --- First pass: rough quaternion initialization from accelerometer ---
  if (!ahrsInit) {
    ahrsInit = true;
    startMs  = nowMs;
    lastUs   = nowUs;
    return baroVario;
  }

  float dt = (nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt <= 0.0f || dt > 0.2f) return kalInit ? x[1] : baroVario;

  // --- AHRS: High Kp on bench (alignment), low Kp afterwards ---
  float kp = (nowMs - startMs < ALIGN_MS) ? KP_ALIGN : KP_RUN;
  mahony_update(gx * DEG2RAD, gy * DEG2RAD, gz * DEG2RAD, ax, ay, az, kp, dt);
  float aVert = vertical_accel(ax, ay, az);
  g_lastAVert = aVert;

  // --- Kalman Filter ---
  bool baroOk = (p_pa > 30000.0f && p_pa < 110000.0f);
  if (!kalInit) {
    if (baroOk) kalman_reset(altitude_std(p_pa));
    return baroVario;
  }
  kalman_predict(dt);
  kalman_update_acc(aVert);
  if (newBaro && baroOk) {
    float zAlt = altitude_std(p_pa);
    // Garde-fou anti-glitch pression (defense en profondeur, en plus du rejet cote calc) :
    // une pression aberrante (ex : 101325 Pa transmis sur echec de lecture BMP388, OU une
    // corruption de liaison) se traduit par un saut d'altitude de plusieurs dizaines de m.
    // Sans garde-fou, le Kalman corrige brutalement x[1] (vitesse verticale) -> pic de vario
    // +/-100 m/s (cause racine du "vario abuse" observe au 1er vol reel). Un vol reel ne
    // saute JAMAIS de MAX_ALT_JUMP entre deux mesures baro. Tant que l'ecart reste transitoire
    // on IGNORE la mesure (on tient l'estimation). Au-dela de MAX_REJECT rejets consecutifs
    // (vrai changement soutenu / trou de liaison prolonge), on RE-INITIALISE proprement le
    // filtre sur la nouvelle altitude (position recalee, vitesse remise a 0 -> pas de pic).
    static const float MAX_ALT_JUMP = 60.0f;   // m
    static const int   MAX_REJECT   = 8;        // ~ quelques 100 ms
    static int s_altReject = 0;
    if (fabsf(zAlt - x[0]) > MAX_ALT_JUMP) {
      if (s_altReject < MAX_REJECT) { s_altReject++; }          // glitch transitoire -> on tient
      else { s_altReject = 0; kalman_reset(zAlt); }             // soutenu -> re-init propre
    } else {
      s_altReject = 0;
      kalman_update_alt(zAlt);
    }
  }

  static bool  aligned = false;
  static float out     = 0.0f;

  // During AHRS alignment -> pure baro vario (Kalman tracks in background without outputting)
  if (nowMs - startMs < ALIGN_MS) { aligned = false; out = baroVario; return baroVario; }

  // End of alignment: reset Kalman cleanly -> prevents negative startup transient
  // (otherwise accumulated orientation errors during alignment result in artificial negative lift).
  if (!aligned) {
    aligned = true;
    if (baroOk) kalman_reset(altitude_std(p_pa));
    out = 0.0f;
  }

  // Output smoothing filter (mechanical needle inertia ~0.5-1.0 s)
  out += (x[1] - out) * (dt / (OUT_TAU + dt));
  return out;
}
