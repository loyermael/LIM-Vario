/* ============================================================
 *  L!M Vario - WiFi GPS Reception (calculator unit) - see GpsLink.h
 * ============================================================ */
#include "GpsLink.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <stdlib.h>

// SSID DISTINCT from the SCREEN's companion AP ("LIM-Vario", FlightLog.cpp): both ESP32s
// were creating a "LIM-Vario" AP at 192.168.4.1 -> COLLISION. The phone would randomly land
// on the calculator's one (no web server) -> the companion app would not load (6 July 2026).
// This one = GPS RECEPTION AP (phone -> NMEA/UDP). The companion app stays on "LIM-Vario".
#define GPS_AP_SSID  "LIM-GPS"
#define GPS_AP_PASS  "limvario"
#define GPS_UDP_PORT 10110
#define KNOT_TO_MS   0.514444f

// NORMAL MODE: "LIM-GPS" Access Point for GPS (NMEA/UDP from mobile phone) + baro sensor.
// USB BRIDGE (Condor) remains AUTO-DETECTED: if "key=value" lines arrive on serial port
// (bench testing bridge), they are ingested automatically; otherwise ignored in real flight.
// NOTE (3 July 2026): STA mode (joining the home router) was tried for the Condor
// bench test -> abandoned, an ESP32 doing pure UDP reception drops packets intermittently
// even with setSleep(false)/reconnect (RSSI fine but packets unreliable). The only
// solution that works for Condor = USB serial bridge (see condor_bridge.py), which goes
// through the "USB BRIDGE" path above, independent of WiFi.

static WiFiUDP   udp;
static bool      g_up     = false;
static float     g_speed  = 0.0f;   // m/s
static float     g_track  = 0.0f;   // ground track heading in degrees 0..360
static float     g_lat    = NAN;    // decimal degrees, +N/-S (NAN if never fixed)
static float     g_lon    = NAN;    // decimal degrees, +E/-W (NAN if never fixed)
static int       g_numSat = 0;      // satellites used, from GGA field 7 (0 if no GGA received yet)
static int       g_fixQuality = 0;  // GGA field 6: 0=none, 1=GPS, 2=DGPS, 4=RTK fixed, 5=RTK float
static bool      g_fix    = false;
static uint32_t  g_lastMs = 0;
static float     g_cVario   = 0.0f;  // Condor total energy vario (evario, m/s)
static float     g_cAlt     = 0.0f;  // Condor altitude (m)
static uint32_t  g_condorMs = 0;     // Timestamp of last Condor packet received (key=value)
static bool      g_condorEnabled = false;  // Condor sim toggle (System menu), via GpsLink_SetCondorEnabled

void GpsLink_SetCondorEnabled(bool enabled) { g_condorEnabled = enabled; }

// ============================================================
//  PHYSICAL GPS - u-blox GPSM10 on hardware UART1 (schematic v2)
//  RX = GPIO34 (input-only, module TX), TX = GPIO13 (config, module RX).
//  Feeds the SAME parseNmea() / g_speed / g_track / g_fix state as the
//  WiFi/UDP path: both sources coexist, the 10 Hz module dominates.
// ============================================================
#define GPS_RX_PIN 34
#define GPS_TX_PIN 13

static HardwareSerial GpsSerial(1);   // UART1 (UART0=USB debug, UART2=display link)
static bool           g_hwGps = false;

static void parseNmea(char* s);       // forward decl (defined below)

// Sends a UBX frame with a code-computed Fletcher-8 checksum (no hand math -> no error).
static void ubxSend(uint8_t cls, uint8_t id, const uint8_t* p, uint16_t len)
{
  uint8_t head[6] = { 0xB5, 0x62, cls, id, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; i++) { ckA += head[i]; ckB += ckA; }
  for (uint16_t i = 0; i < len; i++) { ckA += p[i]; ckB += ckA; }
  uint8_t ck[2] = { ckA, ckB };
  GpsSerial.write(head, 6);
  if (len) GpsSerial.write(p, len);
  GpsSerial.write(ck, 2);
}

// Appends a config item: key (U4, little-endian) + value (sz bytes, little-endian).
static void ubxAddKey(uint8_t* b, uint16_t& n, uint32_t key, uint32_t val, uint8_t sz)
{
  b[n++] = key & 0xFF;        b[n++] = (key >> 8)  & 0xFF;
  b[n++] = (key >> 16) & 0xFF; b[n++] = (key >> 24) & 0xFF;
  for (uint8_t i = 0; i < sz; i++) { b[n++] = val & 0xFF; val >>= 8; }
}

// Waits briefly for a UBX-ACK-ACK / ACK-NAK and logs it (on-hardware config check).
static void gpsWaitAck(const char* what)
{
  uint32_t t0 = millis();
  uint8_t  m[4] = { 0, 0, 0, 0 };   // rolling window: B5 62 05 (01=ACK | 00=NAK)
  while (millis() - t0 < 300) {
    if (!GpsSerial.available()) continue;
    m[0] = m[1]; m[1] = m[2]; m[2] = m[3]; m[3] = (uint8_t)GpsSerial.read();
    if (m[0] == 0xB5 && m[1] == 0x62 && m[2] == 0x05) {
      Serial.printf("[gps] %s -> %s\n", what, m[3] == 0x01 ? "ACK" : "NAK");
      return;
    }
  }
  Serial.printf("[gps] %s -> no ack\n", what);
}

// Configures 10 Hz + RMC/GGA only via the M10 configuration interface (UBX-CFG-VALSET,
// RAM layer). The genuine u-blox NEO-M10 IGNORES the legacy CFG-MSG/CFG-RATE messages,
// so the modern key/value interface is mandatory. RMC carries ground speed + track (all
// the TE pipeline needs); the rest is muted so the 10 Hz stream fits the UART budget.
static void gpsConfigure(void)
{
  uint8_t p[96]; uint16_t n = 0;
  p[n++] = 0x00; p[n++] = 0x01; p[n++] = 0x00; p[n++] = 0x00;   // version, layers=RAM, reserved
  ubxAddKey(p, n, 0x30210001, 100, 2);   // CFG-RATE-MEAS = 100 ms  -> 10 Hz
  ubxAddKey(p, n, 0x30210002, 1,   2);   // CFG-RATE-NAV  = 1 cycle
  ubxAddKey(p, n, 0x209100AC, 1,   1);   // CFG-MSGOUT-NMEA_RMC_UART1 = on
  ubxAddKey(p, n, 0x209100BB, 1,   1);   // CFG-MSGOUT-NMEA_GGA_UART1 = on
  ubxAddKey(p, n, 0x209100CA, 0,   1);   // GLL off
  ubxAddKey(p, n, 0x209100C0, 0,   1);   // GSA off
  ubxAddKey(p, n, 0x209100C5, 0,   1);   // GSV off
  ubxAddKey(p, n, 0x209100B1, 0,   1);   // VTG off
  ubxSend(0x06, 0x8A, p, n);
  gpsWaitAck("cfg 10Hz/RMC");
}

// Detects the module's current baud, forces 115200 if needed, then applies 10 Hz config.
static void gpsSerialBegin(void)
{
  const uint32_t cand[3] = { 115200, 9600, 38400 };  // u-blox default is 9600; 10Hz needs 115200
  uint32_t baud = 115200;
  bool     locked = false;

  for (uint8_t k = 0; k < 3 && !locked; k++) {
    GpsSerial.begin(cand[k], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    uint32_t t0 = millis();
    while (millis() - t0 < 350) {                     // listen for an NMEA sentence start
      if (GpsSerial.available() && GpsSerial.read() == '$') { baud = cand[k]; locked = true; break; }
    }
  }
  g_hwGps = locked;
  Serial.printf("[gps] hw serial %s @ %u baud\n",
                locked ? "detected" : "NOT FOUND (assuming 115200)", (unsigned)baud);
  if (!locked) GpsSerial.begin(115200, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Push the module to 115200 if it was slower, so 10 Hz NMEA fits the link.
  // NEO-M10 baud is set via CFG-VALSET (CFG-UART1-BAUDRATE), NOT legacy CFG-PRT.
  if (baud != 115200) {
    uint8_t p[12]; uint16_t n = 0;
    p[n++] = 0x00; p[n++] = 0x01; p[n++] = 0x00; p[n++] = 0x00;   // version, layers=RAM, reserved
    ubxAddKey(p, n, 0x40520001, 115200, 4);   // CFG-UART1-BAUDRATE = 115200
    ubxSend(0x06, 0x8A, p, n);
    GpsSerial.flush(); delay(100);
    GpsSerial.updateBaudRate(115200);
    delay(50);
  }
  gpsConfigure();
}

// Assembles NMEA lines from UART1 and feeds them to the shared parser.
static void gpsSerialLoop(void)
{
  static char gb[100]; static int gn = 0;
  while (GpsSerial.available()) {
    char c = (char)GpsSerial.read();
    if (c == '\n' || c == '\r') {
      if (gn > 0) { gb[gn] = 0; if (gb[0] == '$') parseNmea(gb); gn = 0; }
    } else if (gn < (int)sizeof(gb) - 1) {
      gb[gn++] = c;
    }
  }
}

void GpsLink_Begin(void)
{
  // AP "LIM-GPS": receives GPS data (NMEA/UDP, port 10110) from mobile phone/navigation app.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(GPS_AP_SSID, GPS_AP_PASS, 1, 0, 4);
  Serial.printf("[gps] softAP %s -> %s  IP=%s  (heap=%u)\n", GPS_AP_SSID,
                ok ? "OK" : "FAIL", WiFi.softAPIP().toString().c_str(),
                (unsigned)ESP.getFreeHeap());
  if (ok) udp.begin(GPS_UDP_PORT);
  gpsSerialBegin();   // Physical GPSM10 on UART1 (10 Hz), coexists with the UDP path
  g_up = true;   // Always active: GpsLink_Loop also reads serial port (Condor / USB bridge)
}

// Converts an NMEA DDMM.MMMM (lat) / DDDMM.MMMM (lon) field to signed decimal
// degrees. Works for both since dividing by 100 always peels off exactly the
// last 2 digits as minutes, regardless of how many degree digits precede.
static float nmeaToDecimal(const char* raw, char hemi)
{
  if (!raw || !raw[0]) return NAN;
  float v   = (float)atof(raw);
  int   deg = (int)(v / 100.0f);
  float min = v - deg * 100.0f;
  float dec = deg + min / 60.0f;
  return (hemi == 'S' || hemi == 'W') ? -dec : dec;
}

// Parses an NMEA string (starting with '$'). In-place modification of input buffer.
static void parseNmea(char* s)
{
  bool isRmc = (strncmp(s + 3, "RMC", 3) == 0);
  bool isVtg = (strncmp(s + 3, "VTG", 3) == 0);
  bool isGga = (strncmp(s + 3, "GGA", 3) == 0);
  if (!isRmc && !isVtg && !isGga) return;

  char* f[16]; int n = 0; char* p = s;
  while (p && n < 16) { f[n++] = p; p = strchr(p, ','); if (p) *p++ = 0; }

  if (isRmc && n > 8) {
    if (f[2][0] == 'A') {                  // 'A' = valid GPS fix
      g_fix   = true;
      g_speed = (float)atof(f[7]) * KNOT_TO_MS;
      g_track = (float)atof(f[8]);         // field 8 of RMC = ground track (deg)
      g_lat   = nmeaToDecimal(f[3], f[4][0]);   // fields 3/4 = lat + N/S
      g_lon   = nmeaToDecimal(f[5], f[6][0]);   // fields 5/6 = lon + E/W
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
  } else if (isGga && n > 7) {
    // GGA field 6 = fix quality, field 7 = satellite count. Kept independent of the
    // RMC 'A'/'V' validity flag: satellite count is informative (e.g. "0 sat" explains
    // a stuck fix) even while RMC still reports no valid fix.
    g_fixQuality = atoi(f[6]);
    g_numSat     = atoi(f[7]);
  }
}

// Parses a "key=value" string transmitted by Condor (flight sim). Modifies input buffer.
// Reuses g_track/g_speed/g_fix so the entire downstream pipeline operates transparently.
static void parseCondor(char* s)
{
  // Condor sim disabled (System menu) -> completely ignore the received frames (the serial
  // bridge stays active and keeps sending): otherwise they would still set g_fix
  // (shared with the real GPS) and freeze g_cVario/g_cAlt, even with the toggle OFF.
  if (!g_condorEnabled) return;
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
  // 3 July 2026: the "connection active" marker relied only on the "time=" field, which is
  // absent (or named differently) in the Condor frames actually received in testing -> CONDOR
  // stayed stuck at 0 permanently even though evario/altitude/compass/airspeed came through.
  // So we consider the link active as soon as ANY RECOGNIZED Condor field arrives.
  if (known) {
    g_condorMs = millis();
    g_lastMs   = millis();
    g_fix      = true;                                    // Sim fix valid -> HasFix() true
  }
}

void GpsLink_Loop(void)
{
  if (!g_up) return;

  // 0) PHYSICAL GPS (GPSM10 on UART1) - real-flight source, 10 Hz NMEA
  gpsSerialLoop();

  // 1) CONDOR via USB serial port (PC bench testing bridge; inactive during flight) - auto-detected.
  // Same dispatch as the UDP path below: '$' = NMEA (Condor's native "NMEA output" COM port,
  // gives true ground track/speed -> real wind estimation), '=' = key=value (evario/altitude/
  // compass/airspeed bridge, kept for existing bench setups).
  static char sb[128]; static int sn = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (sn > 0) {
        sb[sn] = 0;
        if      (sb[0] == '$')    parseNmea(sb);
        else if (strchr(sb, '=')) parseCondor(sb);
        sn = 0;
      }
    } else if (sn < (int)sizeof(sb) - 1) {
      sb[sn++] = c;
    }
  }

  // 2) GPS NMEA over UDP (mobile device connected to "LIM-GPS" Access Point)
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
float GpsLink_Lat(void)         { return GpsLink_HasFix() ? g_lat : NAN; }
float GpsLink_Lon(void)         { return GpsLink_HasFix() ? g_lon : NAN; }
// Not gated by GpsLink_HasFix(): satellite count/quality are diagnostic (e.g. "0 sat, quality 0"
// explains WHY there's no fix yet), unlike position/speed which must not be trusted without one.
int   GpsLink_NumSat(void)      { return g_numSat; }
int   GpsLink_FixQuality(void)  { return g_fixQuality; }

// --- CONDOR Sim Mode (flight sim UDP key=value stream) ---
bool  GpsLink_CondorActive(void){ return (millis() - g_condorMs) < 5000; }  // 5 s hold (absorbs WiFi jitter)
float GpsLink_Vario(void)       { return g_cVario; }   // evario (m/s)
float GpsLink_Altitude(void)    { return g_cAlt; }     // altitude (m)
int   GpsLink_RSSI(void)        { return (int)WiFi.RSSI(); }  // WiFi signal strength (dBm)
