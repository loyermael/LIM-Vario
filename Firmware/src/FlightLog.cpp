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
#include "RTC_PCF85063.h"

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

// detection
static float    g_groundRef   = NAN;   // altitude de reference au sol
static float    g_anchorAlt   = NAN;   // ancre de stabilite (en vol)
static uint32_t g_anchorMs    = 0;

// tampon circulaire pre-decollage
static char     g_pre[PREBUF_LINES][LINE_MAX];
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
  for (uint16_t i = 0; i < g_preCount; i++) {
    uint16_t idx = (g_preHead + PREBUF_LINES - g_preCount + i) % PREBUF_LINES;
    g_file.print(g_pre[idx]);
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
}

bool FlightLog_Active(void)       { return g_sdOk && g_flying && !g_srvOn; }
bool FlightLog_ServerActive(void) { return g_srvOn; }

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
    memcpy(g_pre[g_preHead], line, LINE_MAX);
    g_preHead = (g_preHead + 1) % PREBUF_LINES;
    if (g_preCount < PREBUF_LINES) g_preCount++;

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
static void srv_list(void)
{
  String html = "<html><head><meta name=viewport content='width=device-width'>"
                "<title>L!M Vario - Logs</title></head><body>"
                "<h2>L!M Vario - Journaux de vol</h2><ul>";
  File dir = SD_MMC.open(LOG_DIR);
  if (dir) {
    File f = dir.openNextFile();
    while (f) {
      String name = String(f.name());
      html += "<li><a href='/dl?f=" + name + "'>" + name + "</a> ("
            + String(f.size() / 1024) + " ko) "
            + "<a href='/del?f=" + name + "' "
              "onclick=\"return confirm('Supprimer ?')\">[X]</a></li>";
      f = dir.openNextFile();
    }
    dir.close();
  }
  html += "</ul><p>Appui long encodeur 2 : couper le WiFi.</p></body></html>";
  g_server.send(200, "text/html", html);
}

static void srv_download(void)
{
  String name = g_server.arg("f");
  if (name.indexOf("..") >= 0) { g_server.send(400, "text/plain", "?"); return; }
  String path = String(LOG_DIR) + "/" + name;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { g_server.send(404, "text/plain", "introuvable"); return; }
  g_server.sendHeader("Content-Disposition", "attachment; filename=" + name);
  g_server.streamFile(f, "text/csv");
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

void FlightLog_ServerToggle(void)
{
  if (!g_srvOn) {
    // suspend le log : si un vol est en cours, on le clot proprement
    if (g_flying) { file_close(); g_flying = false; g_groundRef = NAN; }
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    g_server.on("/",    srv_list);
    g_server.on("/dl",  srv_download);
    g_server.on("/del", srv_delete);
    g_server.begin();
    g_srvOn = true;
    Serial.printf("[log] WiFi ON : %s / %s -> http://%s\n",
                  AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
  } else {
    g_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    g_srvOn = false;
    g_preCount = 0;
    Serial.println("[log] WiFi OFF (attente decollage)");
  }
}

void FlightLog_ServerLoop(void)
{
  if (g_srvOn) g_server.handleClient();
}
