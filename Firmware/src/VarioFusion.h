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
