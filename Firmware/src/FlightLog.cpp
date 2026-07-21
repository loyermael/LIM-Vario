/* ============================================================
 *  L!M Vario - Flight log (see FlightLog.h)
 *
 *  AUTOMATIC per-flight splitting (like an IGC logger):
 *   - on the GROUND: nothing on SD, but a RAM buffer of the last 30 s
 *   - TAKEOFF (alt deviates by +/-15 m from the ground reference)
 *       -> new file, pre-takeoff buffer written into it
 *   - LANDING (alt stable +/-8 m for 3 min)
 *       -> file closed. Next flight = new file.
 * ============================================================ */
#include "FlightLog.h"
#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include "esp_netif.h"   // captive portal: offer 192.168.4.1 as DNS via DHCP
#include "esp_heap_caps.h"  // internal-RAM diag before/after softAP
#include "RTC_PCF85063.h"
#include "CompanionApp_HTML.h"

extern int g_brightness;
extern int g_tonePitch;
extern uint8_t g_waveform;
extern int g_toneSpread;
extern int g_varioRange;
extern uint8_t g_varioFilter;
extern uint8_t g_avgClimb;
extern bool g_helperEnable;
extern uint8_t g_uAlt;
extern uint8_t g_uSpeed;
extern uint8_t g_uVert;
extern int g_screenRot;
extern bool g_condorSim;
extern void Set_Backlight(uint8_t Light);
extern void Config_Save();

// Active glider (profile being edited)
extern int   g_gliderIdx;
extern int   g_gliderEmptyWt;
extern int   g_gliderMaxBal;
extern int   g_gliderV1;
extern float g_gliderSi1;
extern int   g_gliderV2;
extern float g_gliderSi2;
extern int   g_gliderV3;
extern float g_gliderSi3;

// Glider database (read-only, accessors -- struct GliderData stays private to main.cpp)
extern int         Glider_Count();
extern const char*  Glider_Name(int i);
extern int          Glider_EmptyWt(int i);
extern int          Glider_MaxBal(int i);
extern int          Glider_V1(int i);
extern float         Glider_Si1(int i);
extern int          Glider_V2(int i);
extern float         Glider_Si2(int i);
extern int          Glider_V3(int i);
extern float         Glider_Si3(int i);

// Profiles (5 slots)
extern int  g_profileIdx;
extern void Profile_Load(int idx);
extern void Profile_Save(int idx);
extern void Profile_Delete(int idx);
extern void Profile_RefreshName();
extern bool Profile_IsUsed(int idx);
extern void Profile_SetName(int idx, const char* name);
extern void Profile_GetName(int idx, char* out, size_t outLen);

// Screen layout (climb/cruise info-boxes + center)
extern uint8_t g_ibConfigClimb[6];
extern uint8_t g_ibConfigCruise[6];
extern uint8_t g_centerConfigClimb;
extern uint8_t g_centerConfigCruise;

#define LOG_DIR        "/logs"
#define LOG_PERIOD_MS  100        // 10 Hz
#define LOG_FLUSH_MS   2000       // flush SD every 2 s
#define AP_SSID        "LIM-Vario"
#define AP_PASS        "limvario"

// Flight detection
#define TAKEOFF_DELTA_M   15.0f   // altitude deviation => takeoff
#define LANDED_BAND_M      8.0f   // "still" altitude band
#define LANDED_HOLD_MS  180000UL  // 3 min stable => landed
#define GROUND_TAU_S      30.0f   // ground reference smoothing

// Pre-takeoff buffer: 30 s at 10 Hz
#define PREBUF_LINES   300
#define LINE_MAX       96

static File       g_file;
static bool       g_sdOk      = false;
static bool       g_srvOn     = false;
static bool       g_flying    = false;
static uint32_t   g_lastLine  = 0;
static uint32_t   g_lastFlush = 0;
static WebServer  g_server(80);
static DNSServer  g_dnsServer;   // captive portal: resolves every domain to the vario IP

// detection
static float    g_groundRef   = NAN;   // ground reference altitude
static float    g_anchorAlt   = NAN;   // stability anchor (in flight)
static uint32_t g_anchorMs    = 0;

// pre-takeoff ring buffer
// Pre-takeoff ring buffer (300 lines): ~28 KB placed in PSRAM and NOT in
// internal RAM (.bss), otherwise it starves the WiFi SoftAP of the internal
// RAM it needs to accept a client (see FlightLog_Init).
static char     (*g_pre)[LINE_MAX] = nullptr;
static uint16_t g_preHead = 0, g_preCount = 0;

// ------------------------------------------------------------
static void file_open_new(void)
{
  if (!SD_MMC.exists(LOG_DIR)) SD_MMC.mkdir(LOG_DIR);

  datetime_t t;
  PCF85063_Read_Time(&t);
  char path[64];
  if (t.year >= 2020 && t.year <= 2099) {
    snprintf(path, sizeof(path), LOG_DIR "/VOL_%04d%02d%02d_%02d%02d.csv",
             t.year, t.month, t.day, t.hour, t.minute);
    int n = 1;
    while (SD_MMC.exists(path) && n < 100)
      snprintf(path, sizeof(path), LOG_DIR "/VOL_%04d%02d%02d_%02d%02d_%d.csv",
               t.year, t.month, t.day, t.hour, t.minute, n++);
  } else {
    int n = 0;
    do { snprintf(path, sizeof(path), LOG_DIR "/VOL_%04d.csv", n++); }
    while (SD_MMC.exists(path) && n < 10000);
  }

  g_file = SD_MMC.open(path, FILE_WRITE);
  if (!g_file) { Serial.printf("[log] FAILED %s\n", path); return; }
  g_file.println("ms,p_pa,alt_std_m,vario_baro,vario_fused,accel_vert,volume");

  // flush the pre-takeoff buffer into the file (the previous 30 s)
  if (g_pre) {
    for (uint16_t i = 0; i < g_preCount; i++) {
      uint16_t idx = (g_preHead + PREBUF_LINES - g_preCount + i) % PREBUF_LINES;
      g_file.print(g_pre[idx]);
    }
  }
  g_preCount = 0;
  Serial.printf("[log] TAKEOFF -> %s\n", path);
}

static void file_close(void)
{
  if (g_file) { g_file.flush(); g_file.close(); }
  Serial.println("[log] LANDING: file closed");
}

void FlightLog_Init(void)
{
  g_sdOk = (SD_MMC.cardType() != CARD_NONE);
  if (!g_sdOk) Serial.println("[log] no SD card: logging disabled");
  else         Serial.println("[log] ready (waiting for takeoff)");

  // Pre-takeoff buffer in PSRAM (frees ~28 KB of internal RAM for WiFi).
  g_pre = (char (*)[LINE_MAX])heap_caps_malloc((size_t)PREBUF_LINES * LINE_MAX,
                                               MALLOC_CAP_SPIRAM);
  if (!g_pre) {
    // Fallback to internal RAM if no PSRAM (should not happen on N16R8).
    g_pre = (char (*)[LINE_MAX])malloc((size_t)PREBUF_LINES * LINE_MAX);
    Serial.println("[log] WARNING: pre-takeoff buffer in internal RAM (PSRAM failed)");
  }
}

bool FlightLog_Active(void)       { return g_sdOk && g_flying && !g_srvOn; }
bool FlightLog_ServerActive(void) { return g_srvOn; }
bool FlightLog_SdOk(void)         { return g_sdOk; }

void FlightLog_AddError(const char* module, const char* msg)
{
  Serial.printf("[ERR:%s] %s\n", module ? module : "SYS", msg ? msg : "");
  if (!g_sdOk || g_srvOn) return;
  if (!SD_MMC.exists(LOG_DIR)) SD_MMC.mkdir(LOG_DIR);

  datetime_t t;
  PCF85063_Read_Time(&t);
  char path[] = LOG_DIR "/ERRORS.LOG";
  File f = SD_MMC.open(path, FILE_APPEND);
  if (f) {
    if (t.year >= 2020 && t.year <= 2099) {
      f.printf("[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
               t.year, t.month, t.day, t.hour, t.minute, t.second,
               module ? module : "SYS", msg ? msg : "");
    } else {
      f.printf("[%lums] [%s] %s\n", (unsigned long)millis(),
               module ? module : "SYS", msg ? msg : "");
    }
    f.close();
  }
}

void FlightLog_Tick(float p_pa, float alt_m, float varioBaro,
                    float varioFused, float accelVert, int volume)
{
  if (!g_sdOk || g_srvOn || isnan(alt_m)) return;
  uint32_t now = millis();
  if (now - g_lastLine < LOG_PERIOD_MS) return;
  float dt = (now - g_lastLine) * 1e-3f;
  g_lastLine = now;

  // ---- CSV line ----
  char line[LINE_MAX];
  snprintf(line, sizeof(line), "%lu,%.1f,%.1f,%.2f,%.2f,%.2f,%d\n",
           (unsigned long)now, p_pa, alt_m, varioBaro, varioFused,
           accelVert, volume);

  if (!g_flying) {
    // ---- ON THE GROUND: RAM buffer + takeoff detection ----
    if (g_pre) {
      memcpy(g_pre[g_preHead], line, LINE_MAX);
      g_preHead = (g_preHead + 1) % PREBUF_LINES;
      if (g_preCount < PREBUF_LINES) g_preCount++;
    }

    if (isnan(g_groundRef)) g_groundRef = alt_m;
    // reference sol lissee (suit la meteo, pas le decollage)
    g_groundRef += (alt_m - g_groundRef) * (dt / (GROUND_TAU_S + dt));

    if (fabsf(alt_m - g_groundRef) > TAKEOFF_DELTA_M) {
      g_flying  = true;
      file_open_new();
      g_anchorAlt = alt_m;
      g_anchorMs  = now;
    }
  } else {
    // ---- EN VOL : ecriture + detection atterrissage ----
    if (g_file) {
      g_file.print(line);
      if (now - g_lastFlush >= LOG_FLUSH_MS) {
        g_lastFlush = now;
        g_file.flush();
      }
    }
    if (isnan(g_anchorAlt) || fabsf(alt_m - g_anchorAlt) > LANDED_BAND_M) {
      g_anchorAlt = alt_m;          // ca bouge encore : on re-arme
      g_anchorMs  = now;
    } else if (now - g_anchorMs > LANDED_HOLD_MS) {
      file_close();                  // 3 min immobile : atterri
      g_flying    = false;
      g_groundRef = alt_m;
      g_preCount  = 0;
    }
  }
}

// ------------------------------------------------------------
//  Serveur WiFi de recuperation des logs
// ------------------------------------------------------------
static void srv_app(void)
{
  g_server.send_P(200, "text/html", COMPANION_APP_HTML);
}

// --- PWA: manifest + icon (home-screen icon, standalone mode) ---
static void srv_manifest(void)
{
  static const char MANIFEST[] PROGMEM =
    "{\"name\":\"L!M Vario\",\"short_name\":\"L!M Vario\","
    "\"start_url\":\"/\",\"scope\":\"/\",\"display\":\"standalone\","
    "\"background_color\":\"#f4f6f9\",\"theme_color\":\"#2563eb\","
    "\"icons\":[{\"src\":\"/icon.svg\",\"sizes\":\"any\","
    "\"type\":\"image/svg+xml\",\"purpose\":\"any maskable\"}]}";
  g_server.send_P(200, "application/manifest+json", MANIFEST);
}

static void srv_icon(void)
{
  // Vector icon: rounded blue badge, "L!M" + small vario needle.
  static const char ICON_SVG[] PROGMEM =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 512 512'>"
    "<defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>"
    "<stop offset='0' stop-color='#2563eb'/><stop offset='1' stop-color='#1d4ed8'/>"
    "</linearGradient></defs>"
    "<rect width='512' height='512' rx='112' fill='url(#g)'/>"
    "<path d='M256 96 L188 300 h136 Z' fill='#ffffff' opacity='0.28'/>"
    "<path d='M256 132 L212 300 h88 Z' fill='#ffffff'/>"
    "<text x='256' y='430' font-family='Arial,Helvetica,sans-serif' font-size='150' "
    "font-weight='800' fill='#ffffff' text-anchor='middle'>L!M</text></svg>";
  g_server.send_P(200, "image/svg+xml", ICON_SVG);
}

// ------------------------------------------------------------
//  Captive portal: OSes (iOS/macOS/Android/Windows) probe a fixed URL to detect
//  whether the network has a portal. Answering with anything other than the
//  expected "internet OK" response makes the OS automatically open a browser
//  itself (Captive Network Assistant / "Sign in to network" notification).
//  Combined with g_dnsServer (all DNS requests -> vario IP), any domain the OS
//  probes ends up here.
//  We return a MINIMAL LANDING PAGE (200, no external resource -> loads instantly
//  in the captive-portal mini-browser) that redirects to the app on its own,
//  plus a big fallback button. A bare empty 302 did not reliably trigger the
//  auto-open on all phones (6 July 2026, Mael feedback).
// ------------------------------------------------------------
// CAPTIVE PORTAL (re-enabled 11 July 2026): disabling it made Android see a
// "limited connection" and route traffic over cellular -> 192.168.4.1
// unreachable. The captive portal forces the OS to keep traffic on the vario's
// WiFi and opens the app in a WebView pinned to the network. We return a minimal
// landing page (200, no external resource) that redirects to the app.
static void srv_captive_redirect(void)
{
  String ip = WiFi.softAPIP().toString();
  String app = "http://" + ip + "/";
  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta http-equiv='refresh' content='0;url=" + app + "'>"
    "<title>L!M Vario</title></head>"
    "<body style='font-family:-apple-system,sans-serif;text-align:center;padding:48px 24px;"
    "background:#0f172a;color:#fff'>"
    "<h2 style='margin:0 0 8px'>L!M Vario</h2>"
    "<p style='color:#94a3b8;margin:0 0 28px'>Opening the application...</p>"
    "<a href='" + app + "' style='display:inline-block;padding:16px 28px;background:#2563eb;"
    "color:#fff;text-decoration:none;border-radius:12px;font-size:18px;font-weight:600'>"
    "Open the application</a>"
    "<script>location.replace('" + app + "');</script>"
    "</body></html>";
  g_server.send(200, "text/html", html);
}

static void srv_api_files(void)
{
  String json = "[";
  File dir = SD_MMC.open(LOG_DIR);
  if (dir) {
    File f = dir.openNextFile();
    bool first = true;
    while (f) {
      if (!first) json += ",";
      first = false;
      String name = String(f.name());
      int slashIdx = name.lastIndexOf('/');
      if (slashIdx >= 0) name = name.substring(slashIdx + 1);
      json += "{\"name\":\"" + name + "\",\"size\":" + String(f.size()) + "}";
      f = dir.openNextFile();
    }
    dir.close();
  }
  json += "]";
  g_server.send(200, "application/json", json);
}

static void srv_api_config_get(void)
{
  String json = "{\"bright\":" + String(g_brightness) +
                ",\"pitch\":" + String(g_tonePitch) +
                ",\"wave\":" + String(g_waveform) +
                ",\"spread\":" + String(g_toneSpread) +
                ",\"range\":" + String(g_varioRange) +
                ",\"filter\":" + String(g_varioFilter) +
                ",\"avg\":" + String(g_avgClimb) +
                ",\"helper\":" + String(g_helperEnable ? 1 : 0) +
                ",\"ualt\":" + String(g_uAlt) +
                ",\"uspeed\":" + String(g_uSpeed) +
                ",\"uvert\":" + String(g_uVert) +
                ",\"rot\":" + String(g_screenRot) +
                ",\"condor\":" + String(g_condorSim ? 1 : 0) +
                ",\"appconn\":" + String(FlightLog_ServerActive() ? 1 : 0) +
                ",\"glidx\":" + String(g_gliderIdx) +
                ",\"glewt\":" + String(g_gliderEmptyWt) +
                ",\"glmbal\":" + String(g_gliderMaxBal) +
                ",\"v1\":" + String(g_gliderV1) +
                ",\"si1\":" + String(g_gliderSi1, 2) +
                ",\"v2\":" + String(g_gliderV2) +
                ",\"si2\":" + String(g_gliderSi2, 2) +
                ",\"v3\":" + String(g_gliderV3) +
                ",\"si3\":" + String(g_gliderSi3, 2) + "}";
  g_server.send(200, "application/json", json);
}

static void srv_api_config_post(void)
{
  if (g_server.hasArg("plain")) {
    String body = g_server.arg("plain");
    auto getInt = [&](const char* key, int def) -> int {
      int idx = body.indexOf(key);
      if (idx < 0) return def;
      return body.substring(idx + strlen(key)).toInt();
    };
    auto getFloat = [&](const char* key, float def) -> float {
      int idx = body.indexOf(key);
      if (idx < 0) return def;
      return body.substring(idx + strlen(key)).toFloat();
    };
    if (body.indexOf("\"bright\":") >= 0) {
      g_brightness = getInt("\"bright\":", g_brightness);
      if (g_brightness < 0) g_brightness = 0;
      if (g_brightness > 20) g_brightness = 20;
      Set_Backlight((uint8_t)(g_brightness * 5));
    }
    if (body.indexOf("\"pitch\":") >= 0) {
      g_tonePitch = getInt("\"pitch\":", g_tonePitch);
      if (g_tonePitch < 200) g_tonePitch = 200;
      if (g_tonePitch > 1500) g_tonePitch = 1500;
    }
    if (body.indexOf("\"wave\":") >= 0) g_waveform = getInt("\"wave\":", g_waveform);
    if (body.indexOf("\"spread\":") >= 0) g_toneSpread = getInt("\"spread\":", g_toneSpread);
    if (body.indexOf("\"range\":") >= 0) g_varioRange = getInt("\"range\":", g_varioRange);
    if (body.indexOf("\"filter\":") >= 0) g_varioFilter = getInt("\"filter\":", g_varioFilter);
    if (body.indexOf("\"avg\":") >= 0) g_avgClimb = getInt("\"avg\":", g_avgClimb);
    if (body.indexOf("\"helper\":") >= 0) g_helperEnable = (getInt("\"helper\":", 1) == 1);
    if (body.indexOf("\"ualt\":") >= 0) g_uAlt = getInt("\"ualt\":", g_uAlt);
    if (body.indexOf("\"uspeed\":") >= 0) g_uSpeed = getInt("\"uspeed\":", g_uSpeed);
    if (body.indexOf("\"uvert\":") >= 0) g_uVert = getInt("\"uvert\":", g_uVert);
    if (body.indexOf("\"rot\":") >= 0) g_screenRot = getInt("\"rot\":", g_screenRot);
    if (body.indexOf("\"condor\":") >= 0) g_condorSim = (getInt("\"condor\":", g_condorSim ? 1 : 0) == 1);
    if (body.indexOf("\"appconn\":") >= 0) {
      bool want = (getInt("\"appconn\":", FlightLog_ServerActive() ? 1 : 0) == 1);
      if (want != FlightLog_ServerActive()) FlightLog_ServerToggle();
    }
    if (body.indexOf("\"glidx\":") >= 0) g_gliderIdx = getInt("\"glidx\":", g_gliderIdx);
    if (body.indexOf("\"glewt\":") >= 0) g_gliderEmptyWt = getInt("\"glewt\":", g_gliderEmptyWt);
    if (body.indexOf("\"glmbal\":") >= 0) g_gliderMaxBal = getInt("\"glmbal\":", g_gliderMaxBal);
    if (body.indexOf("\"v1\":") >= 0) g_gliderV1 = getInt("\"v1\":", g_gliderV1);
    if (body.indexOf("\"si1\":") >= 0) g_gliderSi1 = getFloat("\"si1\":", g_gliderSi1);
    if (body.indexOf("\"v2\":") >= 0) g_gliderV2 = getInt("\"v2\":", g_gliderV2);
    if (body.indexOf("\"si2\":") >= 0) g_gliderSi2 = getFloat("\"si2\":", g_gliderSi2);
    if (body.indexOf("\"v3\":") >= 0) g_gliderV3 = getInt("\"v3\":", g_gliderV3);
    if (body.indexOf("\"si3\":") >= 0) g_gliderSi3 = getFloat("\"si3\":", g_gliderSi3);
    Config_Save();
  }
  g_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ------------------------------------------------------------
//  /api/screen -- layout des info-boxes (climb/cruise + centre)
// ------------------------------------------------------------
static String ib_array_json(uint8_t* arr)
{
  String s = "[";
  for (int i = 0; i < 6; i++) { if (i) s += ","; s += String(arr[i]); }
  s += "]";
  return s;
}

static void srv_api_screen_get(void)
{
  String json = "{\"climb\":" + ib_array_json(g_ibConfigClimb) +
                ",\"cruise\":" + ib_array_json(g_ibConfigCruise) +
                ",\"center_climb\":" + String(g_centerConfigClimb) +
                ",\"center_cruise\":" + String(g_centerConfigCruise) + "}";
  g_server.send(200, "application/json", json);
}

// Parse un tableau JSON "[a,b,c,d,e,f]" trouve apres `key` dans `body`, ecrit dans out[6].
static void parseIbArray(const String& body, const char* key, uint8_t* out)
{
  int idx = body.indexOf(key);
  if (idx < 0) return;
  int start = body.indexOf('[', idx);
  int end   = body.indexOf(']', start);
  if (start < 0 || end < 0) return;
  String inner = body.substring(start + 1, end);
  int pos = 0;
  for (int i = 0; i < 6; i++) {
    int comma = inner.indexOf(',', pos);
    String tok = (comma < 0) ? inner.substring(pos) : inner.substring(pos, comma);
    out[i] = (uint8_t)tok.toInt();
    if (comma < 0) break;
    pos = comma + 1;
  }
}

static void srv_api_screen_post(void)
{
  if (g_server.hasArg("plain")) {
    String body = g_server.arg("plain");
    parseIbArray(body, "\"climb\":", g_ibConfigClimb);
    parseIbArray(body, "\"cruise\":", g_ibConfigCruise);
    int idx;
    if ((idx = body.indexOf("\"center_climb\":")) >= 0)
      g_centerConfigClimb = (uint8_t)body.substring(idx + strlen("\"center_climb\":")).toInt();
    if ((idx = body.indexOf("\"center_cruise\":")) >= 0)
      g_centerConfigCruise = (uint8_t)body.substring(idx + strlen("\"center_cruise\":")).toInt();
    Config_Save();
  }
  g_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ------------------------------------------------------------
//  /api/gliders -- base de donnees planeurs (lecture seule)
// ------------------------------------------------------------
static void srv_api_gliders_get(void)
{
  int n = Glider_Count();
  g_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  g_server.send(200, "application/json", "");
  g_server.sendContent("[");
  for (int i = 0; i < n; i++) {
    String name = String(Glider_Name(i));
    name.replace("\"", "'");
    String entry = String(i ? "," : "") + "{\"name\":\"" + name + "\"" +
                   ",\"empty_wt\":" + String(Glider_EmptyWt(i)) +
                   ",\"max_bal\":" + String(Glider_MaxBal(i)) +
                   ",\"v1\":" + String(Glider_V1(i)) +
                   ",\"si1\":" + String(Glider_Si1(i), 2) +
                   ",\"v2\":" + String(Glider_V2(i)) +
                   ",\"si2\":" + String(Glider_Si2(i), 2) +
                   ",\"v3\":" + String(Glider_V3(i)) +
                   ",\"si3\":" + String(Glider_Si3(i), 2) + "}";
    g_server.sendContent(entry);
  }
  g_server.sendContent("]");
}

// ------------------------------------------------------------
//  /api/profiles -- 5 slots (list / select / save / delete)
// ------------------------------------------------------------
static void srv_api_profiles_get(void)
{
  String json = "[";
  char name[16];
  for (int i = 0; i < 5; i++) {
    if (i) json += ",";
    Profile_GetName(i, name, sizeof(name));
    json += "{\"idx\":" + String(i) + ",\"name\":\"" + String(name) + "\",\"used\":" +
            String(Profile_IsUsed(i) ? "true" : "false") + "}";
  }
  json += "]";
  g_server.send(200, "application/json", json);
}

static int bodyGetIdx(const String& body)
{
  int idx = body.indexOf("\"idx\":");
  if (idx < 0) return -1;
  int v = body.substring(idx + strlen("\"idx\":")).toInt();
  return (v >= 0 && v < 5) ? v : -1;
}

static void srv_api_profiles_select(void)
{
  if (g_server.hasArg("plain")) {
    int idx = bodyGetIdx(g_server.arg("plain"));
    if (idx >= 0) {
      g_profileIdx = idx;
      Profile_Load(idx);
      Profile_RefreshName();
      Config_Save();
    }
  }
  g_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void srv_api_profiles_save(void)
{
  if (g_server.hasArg("plain")) {
    String body = g_server.arg("plain");
    int idx = bodyGetIdx(body);
    if (idx >= 0) {
      int nIdx = body.indexOf("\"name\":\"");
      if (nIdx >= 0) {
        int start = nIdx + strlen("\"name\":\"");
        int end = body.indexOf('"', start);
        String name = (end > start) ? body.substring(start, end) : "";
        Profile_SetName(idx, name.c_str());
      }
      Profile_Save(idx);
      if (idx == g_profileIdx) Profile_RefreshName();
    }
  }
  g_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void srv_api_profiles_delete(void)
{
  if (g_server.hasArg("plain")) {
    int idx = bodyGetIdx(g_server.arg("plain"));
    if (idx >= 0) {
      Profile_Delete(idx);
      if (idx == g_profileIdx) Profile_RefreshName();
    }
  }
  g_server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void srv_download(void)
{
  String name = g_server.arg("f");
  if (name.indexOf("..") >= 0) { g_server.send(400, "text/plain", "?"); return; }
  String path = String(LOG_DIR) + "/" + name;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { g_server.send(404, "text/plain", "not found"); return; }
  g_server.sendHeader("Content-Disposition", "attachment; filename=" + name);
  String mime = name.endsWith(".csv") ? "text/csv" : "text/plain";
  g_server.streamFile(f, mime);
  f.close();
}

static void srv_delete(void)
{
  String name = g_server.arg("f");
  if (name.indexOf("..") >= 0) { g_server.send(400, "text/plain", "?"); return; }
  SD_MMC.remove(String(LOG_DIR) + "/" + name);
  g_server.sendHeader("Location", "/");
  g_server.send(303);
}

static void srv_update_post(void)
{
  g_server.sendHeader("Connection", "close");
  g_server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  ESP.restart();
}

static void srv_update_upload(void)
{
  HTTPUpload& upload = g_server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Update Start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Update Success: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

// ------------------------------------------------------------------
// Captive portal: by default the ESP32 SoftAP DHCP announces NO DNS server to
// clients. Result: the phone/PC does not know it must query 192.168.4.1:53, the
// portal-detection domains (msftconnecttest / connectivitycheck / captive.apple)
// do not resolve to us, the OS concludes "no internet, no portal" and
// automatically LEAVES the LIM-Vario network. So we force the DHCP to offer our
// own IP as DNS (DHCP option 6), on top of the DNSServer already in place that
// answers every request.
static void ap_offer_captive_dns(void)
{
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) { Serial.println("[log] AP netif not found, DHCP DNS not configured"); return; }

  esp_netif_dns_info_t dns = {};
  dns.ip.type          = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = (uint32_t)WiFi.softAPIP();   // 192.168.4.1 in network order

  // The DHCP server must be stopped to reconfigure it, then restarted.
  esp_netif_dhcps_stop(ap);
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer_dns = 0x02;   // OFFER_DNS: DHCP includes option 6 (DNS)
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                         &offer_dns, sizeof(offer_dns));
  esp_netif_dhcps_start(ap);
  Serial.println("[log] SoftAP DHCP: DNS announced -> 192.168.4.1 (captive portal)");
}

void FlightLog_ServerToggle(void)
{
  if (!g_srvOn) {
    // suspend logging: if a flight is in progress, close it cleanly
    if (g_flying) { file_close(); g_flying = false; g_groundRef = NAN; }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    ap_offer_captive_dns();                        // DHCP announces 192.168.4.1 as DNS
    g_dnsServer.start(53, "*", WiFi.softAPIP());   // captive portal: every domain -> us
    g_server.on("/",            srv_app);
    g_server.on("/manifest.webmanifest", srv_manifest);   // PWA
    g_server.on("/icon.svg",             srv_icon);        // PWA
    // Captive-portal detection probes -> redirect to the app (forces the OS to
    // open the app and keep traffic on the vario's WiFi).
    g_server.on("/generate_204",            srv_captive_redirect);  // Android
    g_server.on("/gen_204",                 srv_captive_redirect);  // Android (legacy)
    g_server.on("/hotspot-detect.html",     srv_captive_redirect);  // iOS/macOS
    g_server.on("/library/test/success.html", srv_captive_redirect); // iOS/macOS (legacy)
    g_server.on("/connecttest.txt",         srv_captive_redirect);  // Windows
    g_server.on("/ncsi.txt",                srv_captive_redirect);  // Windows NCSI
    g_server.on("/canonical.html",          srv_captive_redirect);  // Firefox/Android
    g_server.onNotFound(srv_captive_redirect);  // everything else (domain resolved by DNS) -> app
    g_server.on("/api/files",   srv_api_files);
    g_server.on("/api/config",  HTTP_GET,  srv_api_config_get);
    g_server.on("/api/config",  HTTP_POST, srv_api_config_post);
    g_server.on("/api/screen",  HTTP_GET,  srv_api_screen_get);
    g_server.on("/api/screen",  HTTP_POST, srv_api_screen_post);
    g_server.on("/api/gliders", HTTP_GET,  srv_api_gliders_get);
    g_server.on("/api/profiles",         HTTP_GET,  srv_api_profiles_get);
    g_server.on("/api/profiles/select",  HTTP_POST, srv_api_profiles_select);
    g_server.on("/api/profiles/save",    HTTP_POST, srv_api_profiles_save);
    g_server.on("/api/profiles/delete",  HTTP_POST, srv_api_profiles_delete);
    g_server.on("/dl",          srv_download);
    g_server.on("/del",         srv_delete);
    g_server.on("/update",      HTTP_POST, srv_update_post, srv_update_upload);
    g_server.begin();
    g_srvOn = true;
    // WiFi INIT also disables the PSRAM cache -> corrupts/desyncs the RGB panel.
    // We resync + redraw right after all the WiFi init.
    { extern void Display_Restart(void); extern void Lvgl_ForceFullRedraw(void);
      Display_Restart(); Lvgl_ForceFullRedraw(); }
    Serial.printf("[log] WiFi ON : %s / %s -> http://%s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  } else {
    g_dnsServer.stop();
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    // Fully stopping WiFi briefly disables the PSRAM cache -> the RGB panel
    // (framebuffer in PSRAM) desyncs AND its content stays corrupted. We resync
    // the scan-out (Display_Restart) AND force LVGL to redraw everything
    // (otherwise partial mode leaves the static areas corrupted).
    extern void Display_Restart(void);
    extern void Lvgl_ForceFullRedraw(void);
    Display_Restart();
    Lvgl_ForceFullRedraw();
    g_srvOn = false;
    g_preCount = 0;
    Serial.println("[log] WiFi OFF (waiting for takeoff)");
  }
}

void FlightLog_ServerLoop(void)
{
  if (g_srvOn) { g_server.handleClient(); g_dnsServer.processNextRequest(); }
}
