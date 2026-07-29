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
// Magnetic yaw correction (9th axis, added to the gravity-only filter above). Deliberately
// lower than KP_RUN: a bad/disturbed mag sample must not be able to slew yaw as fast as a
// bad accel sample can slew roll/pitch -- yaw has no accelerometer-based fallback to lean on.
#define KP_MAG       0.15f
// Kalman Filter (Process and measurement noise covariances; tune in flight if needed)
#define Q_ACC        1.0f     // (m/s^2)^2/s : Vario agility / responsiveness (higher = faster response)
#define Q_BIAS       1e-4f    // Slow drift rate of vertical accelerometer bias
#define R_ALT        0.25f    // m^2        : Barometric altitude measurement noise (~0.5 m std dev)
#define R_ACC        0.36f    // (m/s^2)^2  : Vertical acceleration noise (~0.6 m/s^2 std dev)
// Horizontal Kalman filter (TE compensation, {V,A,bias} -- same sensor/noise character as
// the vertical filter above, so same starting values; tune independently in flight if needed)
#define Q_ACC_H      1.0f     // (m/s^2)^2/s : forward-accel agility
#define Q_BIAS_H     1e-4f    // Slow drift rate of forward-accelerometer bias
#define R_TAS        0.36f    // (m/s)^2    : MS4525 airspeed noise (~0.6 m/s std dev)
#define R_ACC_H      0.36f    // (m/s^2)^2  : forward acceleration noise (~0.6 m/s^2 std dev)
// DISPLAY time constant (output smoothing filter, as on soaring instruments:
// internal Kalman state tracks rapidly, while output needle display is gently smoothed)
#define OUT_TAU      0.7f     // seconds : 0.4 = responsive/snappy, 1.0 = smooth/calm

#define G_MS2        9.80665f
#define DEG2RAD      0.017453293f
#define RAD2DEG      57.29577951f

// IMU forward axis = glider nose direction, in board body frame.
// The board +X axis is ASSUMED to point toward the nose. If the real mounting differs,
// change this: use +1/-1 to flip, or edit forward_accel() to pick the Y or Z component.
// Bench check: push the unit forward (accelerate along the nose) -> GetFwdAccel() must go +.
#define IMU_FWD_SIGN  (+1.0f)

// ------------------------------------------------------------
//  Internal State Variables
// ------------------------------------------------------------
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;  // Body-to-earth orientation quaternion
static bool     ahrsInit  = false;
static uint32_t startMs   = 0;
static uint32_t lastUs    = 0;

// Latest magnetic heading measurement (see VarioFusion_SetMagHeading()). Read by
// mahony_update() every step; only acted on while s_magHeadingValid is true.
static float s_magHeadingDeg   = 0.0f;
static bool  s_magHeadingValid = false;

// Kalman Filter state vector: x = { h, v, a, bias } ; covariance matrix P
static float    x[4];
static float    P[4][4];
static bool     kalInit = false;

// Horizontal Kalman filter state vector (TE compensation): xh = { V, A, bias } ; see
// hkalman_* functions below -- structurally the bottom-right 3x3 submatrix of the vertical
// filter's dynamics above (V takes the role of v, no "position" state needed: the pitot
// measures V directly, one integration level up from A, same as v is from a above).
static float    xh[3];
static float    Ph[3][3];
static bool     hkalInit = false;
static float    s_tasMs   = 0.0f;   // last airspeed fed via VarioFusion_SetAirspeed()
static bool     s_tasValid = false;

static float altitude_std(float p_pa)   // Standard pressure altitude (QNH-independent)
{
  return 44330.0f * (1.0f - powf(p_pa / 101325.0f, 0.1902949f));
}

// ------------------------------------------------------------
//  Mahony AHRS (Gravity vector correction + optional magnetic yaw correction)
// ------------------------------------------------------------
static void mahony_update(float gx, float gy, float gz,   // rad/s
                          float ax, float ay, float az,   // g (arbitrary norm)
                          float kp, float dt)
{
  // Estimated "up" vector in body frame (3rd row of transposed R matrix), needed by the
  // gravity correction below AND by the magnetic yaw correction further down.
  float vx = 2.0f*(q1*q3 - q0*q2);
  float vy = 2.0f*(q0*q1 + q2*q3);
  float vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;

  float n = sqrtf(ax*ax + ay*ay + az*az);
  if (n > 0.5f && n < 1.5f) {            // Apply correction only if |a| ~ 1 g
    ax /= n; ay /= n; az /= n;
    // Error = measured acceleration cross estimated gravity
    float ex = ay*vz - az*vy;
    float ey = az*vx - ax*vz;
    float ez = ax*vy - ay*vx;
    gx += kp * ex;  gy += kp * ey;  gz += kp * ez;
  }

  // Magnetic yaw correction (9th axis). Rather than a full 3-axis vector fusion (which
  // would need the field's inclination -- not available, see WorldMagModel.h), this nudges
  // ONLY yaw: the heading error (measured magnetic heading vs. the quaternion's own yaw,
  // both scalars) is applied as a small-angle rotation around the current "up" axis
  // (vx,vy,vz) -- i.e. a pure yaw-rate correction that cannot leak into roll/pitch, which
  // stay entirely owned by the gravity term above.
  if (s_magHeadingValid) {
    float yawEst = atan2f(2.0f*(q0*q3 + q1*q2), q0*q0 + q1*q1 - q2*q2 - q3*q3) * RAD2DEG;
    float err = s_magHeadingDeg - yawEst;
    while (err > 180.0f)  err -= 360.0f;
    while (err < -180.0f) err += 360.0f;
    float errRad = err * DEG2RAD;
    gx += KP_MAG * errRad * vx;
    gy += KP_MAG * errRad * vy;
    gz += KP_MAG * errRad * vz;
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

// Forward (along-fuselage) linear acceleration (m/s^2, gravity removed).
// The accelerometer reads specific force = a_linear + gravity_reaction. The gravity
// reaction, in body frame, is the earth-up unit vector expressed in body coords = 3rd row
// of R = (2(q1q3-q0q2), 2(q2q3+q0q1), q0^2-q1^2-q2^2+q3^2). Subtracting its X component
// from ax leaves the pure forward linear acceleration (= dV/dt along the flight path).
static float forward_accel(float ax, float ay, float az)
{
  float gravX_body = 2.0f*(q1*q3 - q0*q2);        // gravity projection on body +X (g units)
  (void)ay; (void)az;
  return IMU_FWD_SIGN * (ax - gravX_body) * G_MS2; // m/s^2, + when accelerating nose-forward
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
//  3-State Horizontal Kalman Filter (TE compensation)
//  Measurements : z_v = V    |  z_a = A + bias
//  Same structure as the 4-state filter above, minus the "position" row/col (the pitot
//  measures V directly -- no extra integration level needed, unlike altitude vs. vario).
// ------------------------------------------------------------
static void hkalman_reset(float v0)
{
  xh[0] = v0; xh[1] = 0.0f; xh[2] = 0.0f;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) Ph[i][j] = 0.0f;
  Ph[0][0] = 100.0f; Ph[1][1] = 10.0f; Ph[2][2] = 1.0f;
  hkalInit = true;
}

static void hkalman_predict(float dt)
{
  // x = F x   (F = [[1,dt,0],[0,1,0],[0,0,1]])
  xh[0] += dt * xh[1];
  float FP[3][3];
  for (int j = 0; j < 3; j++) {
    FP[0][j] = Ph[0][j] + dt * Ph[1][j];
    FP[1][j] = Ph[1][j];
    FP[2][j] = Ph[2][j];
  }
  for (int i = 0; i < 3; i++) {
    Ph[i][0] = FP[i][0] + dt * FP[i][1];
    Ph[i][1] = FP[i][1];
    Ph[i][2] = FP[i][2];
  }
  Ph[1][1] += Q_ACC_H  * dt;
  Ph[2][2] += Q_BIAS_H * dt;
}

// Airspeed measurement update : H = [1 0 0]
static void hkalman_update_v(float z)
{
  float S = Ph[0][0] + R_TAS;
  float y = z - xh[0];
  float K[3];
  for (int i = 0; i < 3; i++) K[i] = Ph[i][0] / S;
  for (int i = 0; i < 3; i++) xh[i] += K[i] * y;
  float HP[3];
  for (int j = 0; j < 3; j++) HP[j] = Ph[0][j];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) Ph[i][j] -= K[i] * HP[j];
}

// Forward acceleration measurement update : H = [0 1 1]  (measurement includes bias)
static void hkalman_update_a(float z)
{
  float S = Ph[1][1] + Ph[2][2] + 2.0f*Ph[1][2] + R_ACC_H;
  float y = z - xh[1] - xh[2];
  float K[3];
  for (int i = 0; i < 3; i++) K[i] = (Ph[i][1] + Ph[i][2]) / S;
  for (int i = 0; i < 3; i++) xh[i] += K[i] * y;
  float HP[3];
  for (int j = 0; j < 3; j++) HP[j] = Ph[1][j] + Ph[2][j];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) Ph[i][j] -= K[i] * HP[j];
}

// ------------------------------------------------------------
//  API Implementation
// ------------------------------------------------------------
static float g_lastAVert = 0.0f;
static float g_lastAFwd  = 0.0f;

bool VarioFusion_Ready(void)
{
  return kalInit && (millis() - startMs > ALIGN_MS);
}

float VarioFusion_GetVertAccel(void) { return g_lastAVert; }
float VarioFusion_GetFwdAccel(void)  { return g_lastAFwd; }

void VarioFusion_SetAirspeed(float tasMs, bool valid)
{
  s_tasMs    = tasMs;
  s_tasValid = valid;
}

float VarioFusion_GetCompTerm(void)
{
  if (!hkalInit) return 0.0f;
  return xh[0] * xh[1] / G_MS2;   // V_filtered * A_debiased / g
}

void VarioFusion_GetRollPitch(float* rollDeg, float* pitchDeg)
{
  float vx = 2.0f*(q1*q3 - q0*q2);
  float vy = 2.0f*(q0*q1 + q2*q3);
  float vz = q0*q0 - q1*q1 - q2*q2 + q3*q3;
  *rollDeg  = atan2f(vy, vz) * RAD2DEG;
  *pitchDeg = atan2f(-vx, sqrtf(vy*vy + vz*vz)) * RAD2DEG;
}

void VarioFusion_SetMagHeading(float magHeadingDeg, bool valid)
{
  s_magHeadingDeg   = magHeadingDeg;
  s_magHeadingValid = valid;
}

float VarioFusion_GetHeading(void)
{
  float yaw = atan2f(2.0f*(q0*q3 + q1*q2), q0*q0 + q1*q1 - q2*q2 - q3*q3) * RAD2DEG;
  if (yaw < 0.0f) yaw += 360.0f;
  return yaw;
}

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
  g_lastAFwd  = forward_accel(ax, ay, az);   // for inertial TE compensation

  // --- Horizontal Kalman Filter (TE compensation: separates true forward accel from a
  // slowly-drifting accelerometer bias, using the pitot airspeed as the independent
  // reference -- same principle as the vertical filter's bias state, applied along-track) ---
  if (!hkalInit) {
    if (s_tasValid) hkalman_reset(s_tasMs);
  } else {
    hkalman_predict(dt);
    hkalman_update_a(g_lastAFwd);
    if (s_tasValid) {
      // Anti-glitch guard (same spirit as the altitude one below): a pitot/link glitch
      // reads as an implausible instantaneous airspeed jump -- ignore transiently, only
      // re-anchor the filter if the jump is sustained (a real airspeed change that large
      // over one 50Hz tick doesn't happen).
      static const float MAX_TAS_JUMP = 15.0f;   // m/s per tick
      static const int   MAX_REJECT_H = 8;
      static int s_tasReject = 0;
      if (fabsf(s_tasMs - xh[0]) > MAX_TAS_JUMP) {
        if (s_tasReject < MAX_REJECT_H) { s_tasReject++; }
        else { s_tasReject = 0; hkalman_reset(s_tasMs); }
      } else {
        s_tasReject = 0;
        hkalman_update_v(s_tasMs);
      }
    }
  }

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
    // Pressure anti-glitch guard (defense in depth, on top of the calculator-side reject):
    // an aberrant pressure (e.g. 101325 Pa sent on a BMP388 read failure, OR a link
    // corruption) translates to an altitude jump of several tens of meters.
    // Without a guard, the Kalman abruptly corrects x[1] (vertical speed) -> vario spike of
    // +/-100 m/s (root cause of the "crazy vario" seen on the 1st real flight). A real flight
    // NEVER jumps by MAX_ALT_JUMP between two baro measurements. While the deviation stays
    // transient we IGNORE the measurement (we hold the estimate). Beyond MAX_REJECT consecutive
    // rejects (a real sustained change / prolonged link loss), we cleanly RE-INITIALIZE the
    // filter on the new altitude (position re-anchored, speed reset to 0 -> no spike).
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
