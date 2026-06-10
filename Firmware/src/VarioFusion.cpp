/* ============================================================
 *  L!M Vario - Fusion IMU + baro "facon Larus"
 *  (voir VarioFusion.h pour l'architecture)
 * ============================================================ */
#include "VarioFusion.h"
#include <Arduino.h>
#include <math.h>

// ------------------------------------------------------------
//  Reglages
// ------------------------------------------------------------
// AHRS Mahony
#define KP_ALIGN     8.0f     // gain fort pendant l'alignement initial
#define KP_RUN       0.35f    // gain faible en vol (l'accel "ment" en virage)
#define ALIGN_MS     3000     // duree d'alignement au demarrage
// Kalman (bruits ; a ajuster en vol si besoin)
#define Q_ACC        3.0f     // (m/s^2)^2/s : agilite du vario (grand = + vif)
#define Q_BIAS       1e-4f    // derive lente du biais accelero
#define R_ALT        0.25f    // m^2        : bruit altitude baro (~0.5 m)
#define R_ACC        0.09f    // (m/s^2)^2  : bruit accel verticale (~0.3 m/s^2)

#define G_MS2        9.80665f
#define DEG2RAD      0.017453293f

// ------------------------------------------------------------
//  Etat
// ------------------------------------------------------------
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;  // quaternion corps->terre
static bool     ahrsInit  = false;
static uint32_t startMs   = 0;
static uint32_t lastUs    = 0;

// Kalman : x = { h, v, a, biais } ; P covariance
static float    x[4];
static float    P[4][4];
static bool     kalInit = false;

static float altitude_std(float p_pa)   // altitude pression standard (QNH-independant)
{
  return 44330.0f * (1.0f - powf(p_pa / 101325.0f, 0.1902949f));
}

// ------------------------------------------------------------
//  AHRS Mahony (correction gravite seule, sans magnetometre)
// ------------------------------------------------------------
static void mahony_update(float gx, float gy, float gz,   // rad/s
                          float ax, float ay, float az,   // g (norme qcq)
                          float kp, float dt)
{
  float n = sqrtf(ax*ax + ay*ay + az*az);
  if (n > 0.5f && n < 1.5f) {            // corrige seulement si |a| ~ 1 g
    ax /= n; ay /= n; az /= n;
    // gravite estimee dans le repere corps (3e ligne de R transposee)
    float vx = 2.0f*(q1*q3 - q0*q2);
    float vy = 2.0f*(q0*q1 + q2*q3);
    float vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;
    // erreur = a_mesure x g_estimee
    float ex = ay*vz - az*vy;
    float ey = az*vx - ax*vz;
    float ez = ax*vy - ay*vx;
    gx += kp * ex;  gy += kp * ey;  gz += kp * ez;
  }
  // integration du quaternion : dq = 0.5 * q * omega
  float halfdt = 0.5f * dt;
  float dq0 = (-q1*gx - q2*gy - q3*gz) * halfdt;
  float dq1 = ( q0*gx + q2*gz - q3*gy) * halfdt;
  float dq2 = ( q0*gy - q1*gz + q3*gx) * halfdt;
  float dq3 = ( q0*gz + q1*gy - q2*gx) * halfdt;
  q0 += dq0; q1 += dq1; q2 += dq2; q3 += dq3;
  float qn = sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
  q0 /= qn; q1 /= qn; q2 /= qn; q3 /= qn;
}

// acceleration verticale terre (m/s^2, + vers le haut, gravite retiree)
static float vertical_accel(float ax, float ay, float az)
{
  float ez = 2.0f*(q1*q3 - q0*q2)*ax
           + 2.0f*(q2*q3 + q0*q1)*ay
           + (q0*q0 - q1*q1 - q2*q2 + q3*q3)*az;  // en g, ~+1 a l'arret
  return (ez - 1.0f) * G_MS2;
}

// ------------------------------------------------------------
//  Kalman 4 etats (structure Larus, covariance en ligne)
//  Mesures : z_alt = h    |  z_acc = a + biais
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

// mesure altitude : H = [1 0 0 0]
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

// mesure accel : H = [0 0 1 1]  (la mesure contient le biais)
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
//  API
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

  // --- premiere passe : init quaternion grossiere depuis l'accelero ---
  if (!ahrsInit) {
    ahrsInit = true;
    startMs  = nowMs;
    lastUs   = nowUs;
    return baroVario;
  }

  float dt = (nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt <= 0.0f || dt > 0.2f) return kalInit ? x[1] : baroVario;

  // --- AHRS : Kp fort au sol (alignement), faible ensuite ---
  float kp = (nowMs - startMs < ALIGN_MS) ? KP_ALIGN : KP_RUN;
  mahony_update(gx * DEG2RAD, gy * DEG2RAD, gz * DEG2RAD, ax, ay, az, kp, dt);
  float aVert = vertical_accel(ax, ay, az);
  g_lastAVert = aVert;

  // --- Kalman ---
  bool baroOk = (p_pa > 30000.0f && p_pa < 110000.0f);
  if (!kalInit) {
    if (baroOk) kalman_reset(altitude_std(p_pa));
    return baroVario;
  }
  kalman_predict(dt);
  kalman_update_acc(aVert);
  if (newBaro && baroOk) kalman_update_alt(altitude_std(p_pa));

  // tant que l'alignement AHRS n'est pas fini -> baro pur (plus sur)
  if (nowMs - startMs < ALIGN_MS) return baroVario;
  return x[1];
}
