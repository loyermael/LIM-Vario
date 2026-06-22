/* ============================================================
 *  L!M Vario - Reception GPS via WiFi (NMEA over UDP)
 *  Cote CALCULATEUR (ESP32 classique : heap large -> AP fiable)
 *
 *  Le calculateur cree le point d'acces "LIM-Vario". Le telephone
 *  (appli NMEA/UDP) y envoie ses trames en UDP sur le port 10110.
 *  On en extrait la VITESSE SOL, transmise a l'ecran dans la trame
 *  (champ airspeed + flag LIM_FLAG_GPS_OK) pour la compensation TE.
 * ============================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     GpsLink_Begin(void);          // monte l'AP + ecoute UDP (heap large ici)
void     GpsLink_Loop(void);           // a appeler souvent dans loop()
bool     GpsLink_HasFix(void);         // fix valide recent (<3 s)
float    GpsLink_GroundSpeed(void);    // vitesse sol m/s (0 si pas de fix)
