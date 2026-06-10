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
void FlightLog_Tick(float p_pa, float alt_m, float varioBaro,
                    float varioFused, float accelVert, int volume);
void FlightLog_ServerToggle(void);
void FlightLog_ServerLoop(void);
bool FlightLog_ServerActive(void);
bool FlightLog_Active(void);
