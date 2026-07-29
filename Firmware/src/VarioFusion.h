/* ============================================================
 *  L!M Vario - IMU + Barometric Sensor Fusion
 *
 *  Estimation and filtering algorithms:
 *    1) Mahony AHRS (gyro + accelerometer) -> continuous attitude
 *       => EARTH-FRAME vertical acceleration, accurate during coordinated turns
 *    2) 4-State Kalman Filter { altitude, vario, accel, accel_bias }
 *       => IMU vertical bias is continuously estimated online
 *
 *  Filter state matrices are propagated dynamically based on
 *  the real-time loop delta interval (dt ~50 Hz).
 *
 *  Call VarioFusion_Step() at a regular cadence (~50 Hz),
 *  ideally inside a real-time driver task (Driver_Loop on Core 0).
 * ============================================================ */
#pragma once
#include <stdbool.h>

// Sensor fusion iteration. Returns the fused vertical speed (m/s).
//  ax..az    : Accelerometer reading in g (board body frame)
//  gx..gz    : Gyroscope reading in deg/sec (board body frame)
//  p_pa      : Static pressure in Pa received from the calculator unit
//  newBaro   : true if a NEW barometric telemetry packet arrived this step
//  baroVario : Calculator baro vario (fallback if fusion AHRS not yet aligned)
float VarioFusion_Step(float ax, float ay, float az,
                       float gx, float gy, float gz,
                       float p_pa, bool newBaro, float baroVario);

// Returns true when AHRS orientation is aligned and Kalman state initialized
bool VarioFusion_Ready(void);

// Latest earth-frame vertical acceleration (m/s^2, gravity removed) - for SD data log
float VarioFusion_GetVertAccel(void);

// Latest FORWARD (along-fuselage) linear acceleration (m/s^2, gravity removed).
// Used for inertial total-energy compensation: comp = TAS * a_forward / g, which
// replaces the noisy numerical derivative of airspeed with a direct IMU measurement.
// Assumes the board +X axis points toward the glider nose (see IMU_FWD_SIGN in .cpp).
float VarioFusion_GetFwdAccel(void);

// Current roll/pitch estimate (degrees), derived from the gravity-only AHRS quaternion.
// Used to tilt-compensate the magnetometer reading (see MagCal_Apply() in main.cpp) --
// independent of any magnetic correction, so available even before the compass is
// calibrated or trusted.
void VarioFusion_GetRollPitch(float* rollDeg, float* pitchDeg);

// Feeds a MAGNETIC (not true) heading measurement, degrees 0..360, into the AHRS as a
// yaw correction, extending the gravity-only Mahony filter to 9 axes. The caller (see
// MagCal_Apply() in main.cpp) is responsible for hard/soft-iron correction and tilt
// compensation; declination is deliberately NOT applied here, so this stays a pure
// magnetic-north reference -- true-heading conversion happens where the heading is
// consumed (main.cpp adds g_wmmDecl). Pass valid=false to leave yaw free-running on
// gyro alone, e.g. before the compass calibration is trusted (see g_magCalValid).
void VarioFusion_SetMagHeading(float magHeadingDeg, bool valid);

// Latest fused heading (degrees, 0..360, MAGNETIC -- add declination for true heading).
// Meaningless (pure gyro-integrated, drifting) until VarioFusion_SetMagHeading() has
// been fed valid data at least once.
float VarioFusion_GetHeading(void);

// Feeds the latest known pitot airspeed (m/s, density-corrected TAS) into the horizontal
// Kalman filter used for total-energy compensation (see VarioFusion_GetCompTerm()). Pass
// valid=false when no pitot is present -- Comp_Apply() then falls back to the classic
// GPS-ground-speed-derivative method instead of calling VarioFusion_GetCompTerm().
void VarioFusion_SetAirspeed(float tasMs, bool valid);

// Total-energy compensation term, ready to add to the raw vario: V_filtered * A_debiased / g.
// Unlike a plain "TAS * a_forward / g" computation, this comes from a 3-state Kalman filter
// {V, A, accelerometer bias} (same structure as the vertical vario's own bias state) that
// continuously separates true forward acceleration from a slowly-drifting accelerometer
// offset, using the pitot airspeed as an independent reference. Returns 0.0f until
// VarioFusion_SetAirspeed() has fed a valid reading at least once.
float VarioFusion_GetCompTerm(void);
