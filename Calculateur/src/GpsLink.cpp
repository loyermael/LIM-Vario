/* ============================================================
 *  L!M Vario - Reception GPS WiFi (calculateur) - voir GpsLink.h
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
static bool      g_up     = false;
static float     g_speed  = 0.0f;   // m/s
static bool      g_fix    = false;
static uint32_t  g_lastMs = 0;

void GpsLink_Begin(void)
{
  // Heap large sur le calculateur -> softAP fiable, montee directe au boot.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(GPS_AP_SSID, GPS_AP_PASS, 1, 0, 4);
  Serial.printf("[gps] softAP %s -> %s  IP=%s  (heap=%u)\n", GPS_AP_SSID,
                ok ? "OK" : "ECHEC", WiFi.softAPIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap());
  if (ok) { udp.begin(GPS_UDP_PORT); g_up = true; }
}

// Parse une phrase NMEA (commence par '$'). Modifie la chaine.
static void parseNmea(char* s)
{
  bool isRmc = (strncmp(s + 3, "RMC", 3) == 0);
  bool isVtg = (strncmp(s + 3, "VTG", 3) == 0);
  if (!isRmc && !isVtg) return;

  char* f[16]; int n = 0; char* p = s;
  while (p && n < 16) { f[n++] = p; p = strchr(p, ','); if (p) *p++ = 0; }

  if (isRmc && n > 8) {
    g_fix = (f[2][0] == 'A');
    if (g_fix) { g_speed = (float)atof(f[7]) * KNOT_TO_MS; g_lastMs = millis(); }
  } else if (isVtg && n > 7) {
    g_speed = (float)atof(f[5]) * KNOT_TO_MS;
    g_fix = true; g_lastMs = millis();
  }
}

void GpsLink_Loop(void)
{
  if (!g_up) return;
  int sz = udp.parsePacket();
  if (sz <= 0) return;
  static char buf[600];
  int len = udp.read(buf, sizeof(buf) - 1);
  if (len <= 0) return;
  buf[len] = 0;
  char* line = strtok(buf, "\r\n");
  while (line) {
    if (line[0] == '$') parseNmea(line);
    line = strtok(NULL, "\r\n");
  }
}

bool  GpsLink_HasFix(void)      { return g_fix && (millis() - g_lastMs < 3000); }
float GpsLink_GroundSpeed(void) { return GpsLink_HasFix() ? g_speed : 0.0f; }
