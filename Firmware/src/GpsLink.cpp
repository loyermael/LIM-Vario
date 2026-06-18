/* ============================================================
 *  L!M Vario - Reception GPS via WiFi (voir GpsLink.h)
 * ============================================================ */
#include "GpsLink.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <stdlib.h>

#define GPS_AP_SSID  "LIM-Vario"
#define GPS_AP_PASS  "limvario"
#define GPS_UDP_PORT 10110
#define KNOT_TO_MS   0.514444f

static WiFiUDP   udp;
static bool      g_bound  = false;
static float     g_speed  = 0.0f;   // m/s
static float     g_course = 0.0f;   // deg
static bool      g_fix    = false;
static uint32_t  g_lastMs = 0;

void GpsLink_Begin(void)
{
  // Point d'acces (le tel s'y connecte et envoie le NMEA en UDP)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(GPS_AP_SSID, GPS_AP_PASS);
  udp.begin(GPS_UDP_PORT);
  g_bound = true;
}

// Parse une phrase NMEA (commence par '$'). Modifie la chaine (tokenise).
static void parseNmea(char* s)
{
  // talker ID sur 2 car. : $GP / $GN / $GL ... -> type a partir de s+3
  bool isRmc = (strncmp(s + 3, "RMC", 3) == 0);
  bool isVtg = (strncmp(s + 3, "VTG", 3) == 0);
  if (!isRmc && !isVtg) return;

  // decoupe par virgules
  char* f[16]; int n = 0; char* p = s;
  while (p && n < 16) { f[n++] = p; p = strchr(p, ','); if (p) *p++ = 0; }

  if (isRmc && n > 8) {
    // $..RMC,time,status(A/V),lat,N,lon,E,SOG(kn),COG,date,...
    g_fix = (f[2][0] == 'A');
    if (g_fix) {
      g_speed  = (float)atof(f[7]) * KNOT_TO_MS;
      g_course = (float)atof(f[8]);
      g_lastMs = millis();
    }
  } else if (isVtg && n > 7) {
    // $..VTG,COG_T,T,COG_M,M,SOG(kn),N,SOG(kmh),K
    g_speed  = (float)atof(f[5]) * KNOT_TO_MS;
    g_course = (float)atof(f[1]);
    g_fix    = true;
    g_lastMs = millis();
  }
}

void GpsLink_Loop(void)
{
  if (!g_bound) return;
  int sz = udp.parsePacket();
  if (sz <= 0) return;

  static char buf[600];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;

  // une trame UDP peut contenir plusieurs phrases NMEA
  char* line = strtok(buf, "\r\n");
  while (line) {
    if (line[0] == '$') parseNmea(line);
    line = strtok(NULL, "\r\n");
  }
}

bool GpsLink_HasFix(void)
{
  return g_fix && (millis() - g_lastMs < 3000);
}

float GpsLink_GroundSpeed(void) { return GpsLink_HasFix() ? g_speed : 0.0f; }
float GpsLink_Course(void)      { return g_course; }
uint32_t GpsLink_AgeMs(void)    { return millis() - g_lastMs; }
