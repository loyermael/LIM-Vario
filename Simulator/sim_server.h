#pragma once
/* Petit serveur HTTP local (localhost uniquement) qui sert l'app companion
 * (Firmware/src/CompanionApp_HTML.h) et repond aux memes routes /api/... que le
 * vrai firmware (Firmware/src/FlightLog.cpp), en lisant/ecrivant les globales
 * exposees par sim_menu.c -- permet de tester l'app companion sans hardware. */

void SimServer_Init(int port);
void SimServer_Tick(void);
void SimServer_Shutdown(void);
