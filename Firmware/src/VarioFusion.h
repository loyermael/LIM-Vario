/* ============================================================
 *  L!M Vario - Fusion IMU + baro "facon Larus"
 *
 *  Architecture copiee du projet Larus (GPL-3.0) :
 *    1) AHRS Mahony (gyro + accelero) -> attitude continue
 *       => acceleration VERTICALE terre, valable en virage
 *    2) Kalman 4 etats { altitude, vario, accel, biais_accel }
 *       => le biais de l'IMU est estime en permanence
 *       (cf. larus-breeze/sw_algorithms_lib NAV_Algorithms/KalmanVario)
 *
 *  Difference avec Larus : nos gains Kalman sont calcules en
 *  ligne (covariance propagee), donc dt peut varier (~50 Hz).
 *
 *  Appeler VarioFusion_Step() a cadence reguliere (~50 Hz),
 *  idealement depuis une tache temps-reel (Driver_Loop core 0).
 * ============================================================ */
#pragma once
#include <stdbool.h>

// Etape de fusion. Retourne le vario fusionne (m/s).
//  ax..az  : accelero en g          (repere carte)
//  gx..gz  : gyro en degres/seconde (repere carte)
//  p_pa    : pression statique (Pa) recue du calculateur
//  newBaro : true si une NOUVELLE trame baro est arrivee
//  baroVario : vario baro du calculateur (fallback si fusion pas prete)
float VarioFusion_Step(float ax, float ay, float az,
                       float gx, float gy, float gz,
                       float p_pa, bool newBaro, float baroVario);

// true quand l'AHRS est aligne et le Kalman initialise
bool VarioFusion_Ready(void);

// derniere acceleration verticale terre (m/s^2, gravite retiree) - pour le log
float VarioFusion_GetVertAccel(void);
