/* ============================================================
 *  L!M Vario - Journal de vol (voir FlightLog.h)
 *
 *  Decoupage AUTOMATIQUE par vol (comme un logger IGC) :
 *   - au SOL : rien sur SD, mais tampon RAM des 30 dernieres s
 *   - DECOLLAGE (alt s'ecarte de +/-15 m de la reference sol)
 *       -> nouveau fichier, tampon pre-decollage ecrit dedans
 *   - ATTERRISSAGE (alt stable +/-8 m pendant 3 min)
 *       -> fichier clos. Vol suivant = nouveau fichier.
 * ============================================================ */
#include "FlightLog.h"
#include <Arduino.h>
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include "esp_netif.h"   // portail captif : offrir 192.168.4.1 comme DNS via DHCP
#include "esp_heap_caps.h"  // diag RAM interne avant/apres softAP
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

// Glider actif (profil en cours d'edition)
extern int   g_gliderIdx;
extern int   g_gliderEmptyWt;
extern int   g_gliderMaxBal;
extern int   g_gliderV1;
extern float g_gliderSi1;
extern int   g_gliderV2;
extern float g_gliderSi2;
extern int   g_gliderV3;
extern float g_gliderSi3;

// Base de donnees planeurs (lecture seule, accesseurs -- struct GliderData reste privee a main.cpp)
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

// Profils (5 slots)
extern int  g_profileIdx;
extern void Profile_Load(int idx);
extern void Profile_Save(int idx);
extern void Profile_Delete(int idx);
extern void Profile_RefreshName();
extern bool Profile_IsUsed(int idx);
extern void Profile_SetName(int idx, const char* name);
extern void Profile_GetName(int idx, char* out, size_t outLen);

// Layout ecran (info-boxes climb/cruise + centre)
extern uint8_t g_ibConfigClimb[6];
extern uint8_t g_ibConfigCruise[6];
extern uint8_t g_centerConfigClimb;
extern uint8_t g_centerConfigCruise;

#define LOG_DIR        "/logs"
#define LOG_PERIOD_MS  100        // 10 Hz
#define LOG_FLUSH_MS   2000       // flush SD toutes les 2 s
#define AP_SSID        "LIM-Vario"
#define AP_PASS        "limvario"

// Detection de vol
#define TAKEOFF_DELTA_M   15.0f   // ecart d'altitude => decollage
#define LANDED_BAND_M      8.0f   // bande d'altitude "immobile"
#define LANDED_HOLD_MS  180000UL  // 3 min stable => atterri
#define GROUND_TAU_S      30.0f   // lissage reference sol

// Tampon pre-decollage : 30 s a 10 Hz
#define PREBUF_LINES   300
#define LINE_MAX       96

static File       g_file;
static bool       g_sdOk      = false;
static bool       g_srvOn     = false;
static bool       g_flying    = false;
static uint32_t   g_lastLine  = 0;
static uint32_t   g_lastFlush = 0;
static WebServer  g_server(80);
static DNSServer  g_dnsServer;   // portail captif : resout tous les domaines vers l'IP du vario

// detection
static float    g_groundRef   = NAN;   // altitude de reference au sol
static float    g_anchorAlt   = NAN;   // ancre de stabilite (en vol)
static uint32_t g_anchorMs    = 0;

// tampon circulaire pre-decollage
// Tampon circulaire pre-decollage (300 lignes) : ~28 Ko places en PSRAM et
// NON en RAM interne (.bss), sinon ils privent le softAP WiFi de la RAM
// interne dont il a besoin pour accepter un client (cf FlightLog_Init).
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
  if (!g_file) { Serial.printf("[log] ECHEC %s\n", path); return; }
  g_file.println("ms,p_pa,alt_std_m,vario_baro,vario_fused,accel_vert,volume");

  // vide le tampon pre-decollage dans le fichier (les 30 s avant)
  if (g_pre) {
    for (uint16_t i = 0; i < g_preCount; i++) {
      uint16_t idx = (g_preHead + PREBUF_LINES - g_preCount + i) % PREBUF_LINES;
      g_file.print(g_pre[idx]);
    }
  }
  g_preCount = 0;
  Serial.printf("[log] DECOLLAGE -> %s\n", path);
}

static void file_close(void)
{
  if (g_file) { g_file.flush(); g_file.close(); }
  Serial.println("[log] ATTERRISSAGE : fichier clos");
}

void FlightLog_Init(void)
{
  g_sdOk = (SD_MMC.cardType() != CARD_NONE);
  if (!g_sdOk) Serial.println("[log] pas de carte SD : log desactive");
  else         Serial.println("[log] pret (attente decollage)");

  // Tampon pre-decollage en PSRAM (libere ~28 Ko de RAM interne pour le WiFi).
  g_pre = (char (*)[LINE_MAX])heap_caps_malloc((size_t)PREBUF_LINES * LINE_MAX,
                                               MALLOC_CAP_SPIRAM);
  if (!g_pre) {
    // Repli en RAM interne si pas de PSRAM (ne devrait pas arriver sur N16R8).
    g_pre = (char (*)[LINE_MAX])malloc((size_t)PREBUF_LINES * LINE_MAX);
    Serial.println("[log] ATTENTION : tampon pre-decollage en RAM interne (PSRAM KO)");
  }
}

bool FlightLog_Active(void)       { return g_sdOk && g_flying && !g_srvOn; }
bool FlightLog_ServerActive(void) { return g_srvOn; }

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

  // ---- ligne CSV ----
  char line[LINE_MAX];
  snprintf(line, sizeof(line), "%lu,%.1f,%.1f,%.2f,%.2f,%.2f,%d\n",
           (unsigned long)now, p_pa, alt_m, varioBaro, varioFused,
           accelVert, volume);

  if (!g_flying) {
    // ---- AU SOL : tampon RAM + detection decollage ----
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

// --- PWA : manifeste + icone (icone sur l'ecran d'accueil, mode standalone) ---
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
  // Icone vectorielle : pastille bleue arrondie, "L!M" + petite aiguille vario.
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
//  Portail captif : les OS (iOS/macOS/Android/Windows) sondent une URL fixe pour
//  detecter si le reseau a un portail. Repondre par autre chose que la reponse
//  "internet OK" attendue declenche l'ouverture automatique d'un navigateur par
//  l'OS lui-meme (Captive Network Assistant / notification "Se connecter au reseau").
//  Combine avec g_dnsServer (toutes les requetes DNS -> IP du vario), n'importe quel
//  domaine sonde par l'OS aboutit ici.
//  On renvoie une PAGE D'ACCUEIL MINIMALE (200, sans aucune ressource externe -> se charge
//  instantanement dans le mini-navigateur du portail captif) qui redirige toute seule vers
//  l'app + un gros bouton de secours. Un simple 302 vide ne declenchait pas l'ouverture
//  auto de facon fiable sur tous les telephones (6 juillet 2026, retour Mael).
// ------------------------------------------------------------
// PORTAIL CAPTIF (re-active le 11 juillet 2026) : le desactiver faisait qu'Android
// voyait "connexion limitee" et routait le trafic par la 4G -> 192.168.4.1
// injoignable. Le portail captif force l'OS a garder le trafic sur le WiFi du
// vario et ouvre l'app dans une WebView epinglee au reseau. On renvoie une page
// d'accueil minimale (200, sans ressource externe) qui redirige vers l'app.
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
    "<p style='color:#94a3b8;margin:0 0 28px'>Ouverture de l'application...</p>"
    "<a href='" + app + "' style='display:inline-block;padding:16px 28px;background:#2563eb;"
    "color:#fff;text-decoration:none;border-radius:12px;font-size:18px;font-weight:600'>"
    "Ouvrir l'application</a>"
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
  if (!f) { g_server.send(404, "text/plain", "introuvable"); return; }
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
// Portail captif : par defaut le DHCP du softAP ESP32 n'annonce AUCUN
// serveur DNS aux clients. Resultat : le telephone/PC ne sait pas qu'il
// doit interroger 192.168.4.1:53, les domaines de detection de portail
// (msftconnecttest / connectivitycheck / captive.apple) ne se resolvent
// pas vers nous, l'OS conclut "pas d'internet, pas de portail" et QUITTE
// automatiquement le reseau LIM-Vario. On force donc le DHCP a offrir
// notre propre IP comme DNS (option DHCP 6), en plus du DNSServer deja
// en place qui repond a toutes les requetes.
static void ap_offer_captive_dns(void)
{
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) { Serial.println("[log] AP netif introuvable, DNS DHCP non configure"); return; }

  esp_netif_dns_info_t dns = {};
  dns.ip.type          = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = (uint32_t)WiFi.softAPIP();   // 192.168.4.1 en ordre reseau

  // Il faut arreter le serveur DHCP pour reconfigurer, puis le relancer.
  esp_netif_dhcps_stop(ap);
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer_dns = 0x02;   // OFFER_DNS : le DHCP inclut l'option 6 (DNS)
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                         &offer_dns, sizeof(offer_dns));
  esp_netif_dhcps_start(ap);
  Serial.println("[log] DHCP softAP : DNS annonce -> 192.168.4.1 (portail captif)");
}

void FlightLog_ServerToggle(void)
{
  if (!g_srvOn) {
    // suspend le log : si un vol est en cours, on le clot proprement
    if (g_flying) { file_close(); g_flying = false; g_groundRef = NAN; }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    ap_offer_captive_dns();                        // DHCP annonce 192.168.4.1 comme DNS
    g_dnsServer.start(53, "*", WiFi.softAPIP());   // portail captif : tout domaine -> nous
    g_server.on("/",            srv_app);
    g_server.on("/manifest.webmanifest", srv_manifest);   // PWA
    g_server.on("/icon.svg",             srv_icon);        // PWA
    // Endpoints de detection de portail captif (Android/iOS/macOS/Windows/Firefox) :
    // repondre par une redirection au lieu de la reponse "internet OK" attendue force
    // l'OS a ouvrir automatiquement un navigateur sur notre page.
    // Sondes de detection de portail captif -> redirigent vers l'app (force
    // l'OS a ouvrir l'app et a garder le trafic sur le WiFi du vario).
    g_server.on("/generate_204",            srv_captive_redirect);  // Android
    g_server.on("/gen_204",                 srv_captive_redirect);  // Android (ancien)
    g_server.on("/hotspot-detect.html",     srv_captive_redirect);  // iOS/macOS
    g_server.on("/library/test/success.html", srv_captive_redirect); // iOS/macOS (ancien)
    g_server.on("/connecttest.txt",         srv_captive_redirect);  // Windows
    g_server.on("/ncsi.txt",                srv_captive_redirect);  // Windows NCSI
    g_server.on("/canonical.html",          srv_captive_redirect);  // Firefox/Android
    g_server.onNotFound(srv_captive_redirect);  // tout le reste (domaine resolu par le DNS) -> app
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
    // L'INIT du WiFi desactive aussi le cache PSRAM -> corrompt/desynchronise la
    // dalle RGB. On resynchronise + redessine juste apres toute l'init WiFi.
    { extern void Display_Restart(void); extern void Lvgl_ForceFullRedraw(void);
      Display_Restart(); Lvgl_ForceFullRedraw(); }
    Serial.printf("[log] WiFi ON : %s / %s -> http://%s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  } else {
    g_dnsServer.stop();
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    // L'arret complet du WiFi desactive brievement le cache PSRAM -> la dalle RGB
    // (framebuffer en PSRAM) se desynchronise ET son contenu reste corrompu. On
    // resynchronise le balayage (Display_Restart) ET on force LVGL a tout
    // redessiner (sinon le mode partiel laisse les zones statiques abimees).
    extern void Display_Restart(void);
    extern void Lvgl_ForceFullRedraw(void);
    Display_Restart();
    Lvgl_ForceFullRedraw();
    g_srvOn = false;
    g_preCount = 0;
    Serial.println("[log] WiFi OFF (attente decollage)");
  }
}

void FlightLog_ServerLoop(void)
{
  if (g_srvOn) { g_server.handleClient(); g_dnsServer.processNextRequest(); }
}
