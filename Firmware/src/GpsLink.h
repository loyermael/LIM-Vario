/* ============================================================
 *  L!M Vario - Reception GPS via WiFi (NMEA over UDP)
 *
 *  L'ecran cree un point d'acces WiFi "LIM-Vario". Le telephone
 *  (appli type "Share GPS" / BlueNMEA) y envoie ses trames NMEA
 *  en UDP sur le port 10110. On en extrait la VITESSE SOL, qui
 *  sert a la compensation TE du vario (V0.7).
 *
 *  Plus tard : remplacable par un module GPS cable (NEO-M9N).
 * ============================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     GpsLink_Begin(void);          // monte le point d'acces + ecoute UDP
void     GpsLink_Loop(void);           // a appeler dans loop()
bool     GpsLink_HasFix(void);         // true si fix valide recent (<3 s)
float    GpsLink_GroundSpeed(void);    // vitesse sol en m/s (0 si pas de fix)
float    GpsLink_Course(void);         // cap en degres
uint32_t GpsLink_AgeMs(void);          // ms depuis la derniere trame valide
