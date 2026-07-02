/* ============================================================
 *  L!M Vario - WiFi GPS Reception (calculator unit) - see GpsLink.h
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

// NORMAL MODE: "LIM-Vario" Access Point for GPS (NMEA/UDP from mobile phone) + baro sensor.
// USB BRIDGE (Condor) remains AUTO-DETECTED: if "key=value" lines arrive on serial port
// (bench testing bridge), they are ingested automatically; otherwise ignored in real flight.
// NOTE (3 juillet 2026) : le mode STA (rejoindre la box maison) a ete essaye pour le bench
// test Condor -> abandonne, ESP32 en reception UDP pure perd les paquets par intermittence
// meme avec setSleep(false)/reconnexion (RSSI correct mais paquets non fiables). La seule
// solution qui marche pour Condor = pont USB serie (voir condor_bridge.py), qui passe par
// le chemin "USB BRIDGE" ci-dessus, independant du WiFi.

static WiFiUDP   udp;
static bool      g_up     = false;
static float     g_speed  = 0.0f;   // m/s
static float     g_track  = 0.0f;   // ground track heading in degrees 0..360
static bool      g_fix    = false;
static uint32_t  g_lastMs = 0;
static float     g_cVario   = 0.0f;  // Condor total energy vario (evario, m/s)
static float     g_cAlt     = 0.0f;  // Condor altitude (m)
static uint32_t  g_condorMs = 0;     // Timestamp of last Condor packet received (key=value)

void GpsLink_Begin(void)
{
  // AP "LIM-Vario": receives GPS data (NMEA/UDP, port 10110) from mobile phone/navigation app.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(GPS_AP_SSID, GPS_AP_PASS, 1, 0, 4);
  Serial.printf("[gps] softAP %s -> %s  IP=%s  (heap=%u)\n", GPS_AP_SSID,
                ok ? "OK" : "FAIL", WiFi.softAPIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap());
  if (ok) udp.begin(GPS_UDP_PORT);
  g_up = true;   // Always active: GpsLink_Loop also reads serial port (Condor / USB bridge)
}

// Parses an NMEA string (starting with '$'). In-place modification of input buffer.
static void parseNmea(char* s)
{
  bool isRmc = (strncmp(s + 3, "RMC", 3) == 0);
  bool isVtg = (strncmp(s + 3, "VTG", 3) == 0);
  if (!isRmc && !isVtg) return;

  char* f[16]; int n = 0; char* p = s;
  while (p && n < 16) { f[n++] = p; p = strchr(p, ','); if (p) *p++ = 0; }

  if (isRmc && n > 8) {
    if (f[2][0] == 'A') {                  // 'A' = valid GPS fix
      g_fix   = true;
      g_speed = (float)atof(f[7]) * KNOT_TO_MS;
      g_track = (float)atof(f[8]);         // field 8 of RMC = ground track (deg)
      g_lastMs = millis();
    }
    // 'V' (invalid/no fix): do NOTHING. The timeout inside GpsLink_HasFix
    // handles fix loss -> prevents UI indicator flickering on isolated 'V' frames.
  } else if (isVtg && n > 7) {
    // VTG does NOT carry valid fix flag -> only update speed + ground track.
    // Validity flag relies solely on RMC 'A' (otherwise UI shows "connected"
    // as soon as mobile app streams packets, even without satellite acquisition).
    g_speed = (float)atof(f[5]) * KNOT_TO_MS;
    g_track = (float)atof(f[1]);           // field 1 of VTG = true ground track (deg)
  }
}

// Parses a "key=value" string transmitted by Condor (flight sim). Modifies input buffer.
// Reuses g_track/g_speed/g_fix so the entire downstream pipeline operates transparently.
static void parseCondor(char* s)
{
  char* eq = strchr(s, '=');
  if (!eq) return;
  *eq = 0;
  float v = (float)atof(eq + 1);
  bool known = true;
  if      (!strcmp(s, "evario"))   g_cVario = v;          // compensated TE vario (m/s)
  else if (!strcmp(s, "altitude")) g_cAlt   = v;          // altitude (m)
  else if (!strcmp(s, "compass"))  g_track  = v;          // heading (deg) -> circle/glider symbol
  else if (!strcmp(s, "airspeed")) g_speed  = v;          // true airspeed (m/s)
  else if (!strcmp(s, "time"))     { /* timestamp field, no state to update */ }
  else known = false;
  // 3 juillet 2026 : la marque "connexion active" reposait uniquement sur le champ "time=",
  // absent (ou nomme differemment) dans les trames Condor reellement recues en test -> CONDOR
  // restait bloque a 0 en permanence alors que evario/altitude/compass/airspeed arrivaient bien.
  // On considere donc la liaison active des qu'un champ Condor RECONNU (quel qu'il soit) arrive.
  if (known) {
    g_condorMs = millis();
    g_lastMs   = millis();
    g_fix      = true;                                    // Sim fix valid -> HasFix() true
  }
}

void GpsLink_Loop(void)
{
  if (!g_up) return;

  // 1) CONDOR via USB serial port (PC bench testing bridge; inactive during flight) - auto-detected
  static char sb[128]; static int sn = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (sn > 0) { sb[sn] = 0; if (strchr(sb, '=')) parseCondor(sb); sn = 0; }
    } else if (sn < (int)sizeof(sb) - 1) {
      sb[sn++] = c;
    }
  }

  // 2) GPS NMEA over UDP (mobile device connected to "LIM-Vario" Access Point)
  static char buf[600];
  while (udp.parsePacket() > 0) {
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len <= 0) break;
    buf[len] = 0;
    char* line = strtok(buf, "\r\n");
    while (line) {
      if      (line[0] == '$')     parseNmea(line);    // GPS (mobile phone / NMEA)
      else if (strchr(line, '='))  parseCondor(line);  // Condor (key=value, if sent via UDP)
      line = strtok(NULL, "\r\n");
    }
  }
}

bool  GpsLink_HasFix(void)      { return g_fix && (millis() - g_lastMs < 5000); }
float GpsLink_GroundSpeed(void) { return GpsLink_HasFix() ? g_speed : 0.0f; }
float GpsLink_Track(void)       { return GpsLink_HasFix() ? g_track : NAN; }

// --- CONDOR Sim Mode (flight sim UDP key=value stream) ---
bool  GpsLink_CondorActive(void){ return (millis() - g_condorMs) < 5000; }  // 5 s hold (absorbs WiFi jitter)
float GpsLink_Vario(void)       { return g_cVario; }   // evario (m/s)
float GpsLink_Altitude(void)    { return g_cAlt; }     // altitude (m)
int   GpsLink_RSSI(void)        { return (int)WiFi.RSSI(); }  // WiFi signal strength (dBm)
