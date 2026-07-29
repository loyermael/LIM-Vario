/* ============================================================
 *  L!M Vario - Journal de vol (CSV sur carte SD)
 *
 *  - FlightLog_Init()  : ouvre /logs/LOG_<date>_<heure>.csv
 *  - FlightLog_Tick()  : ecrit une ligne a 10 Hz (appeler depuis loop)
 *  - FlightLog_ServerToggle() : demarre/arrete le point d'acces WiFi
 *      SSID "LIM-Vario" / mdp "limvario"  ->  http://192.168.4.1
 *      (liste, telechargement et suppression des logs)
 *      Le log est suspendu pendant que le serveur tourne.
 *  - FlightLog_ServerLoop()   : a appeler depuis loop()
 * ============================================================ */
#pragma once
#include <stdbool.h>

void FlightLog_Init(void);
void FlightLog_Tick(float p_pa, float alt_m, float gpsAlt_m,
                    float varioBaro, float varioComp, float varioFused,
                    float varioNetto, float varioAvg, float accelVert,
                    float airspeed_ms, float gndSpeed_ms,
                    bool gpsFix, float gpsTrack_deg, float gpsLat, float gpsLon,
                    bool circling, int turnDir,
                    float windSpeed_ms, float windDir_deg,
                    float windAvgSpeed_ms, float windAvgDir_deg,
                    float energyMag, float energyDir_deg,
                    float climbGain_m, float stfSpeed_kmh,
                    int volume, long utcHhmmss);   // utcHhmmss: -1 si pas encore d'heure GPS
void FlightLog_ServerToggle(void);
void FlightLog_ServerLoop(void);
bool FlightLog_ServerActive(void);
bool FlightLog_Active(void);
bool FlightLog_SdOk(void);      // SD card detected at boot (else logging impossible)
void FlightLog_AddError(const char* module, const char* msg);
