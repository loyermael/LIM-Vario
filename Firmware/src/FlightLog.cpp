/* ============================================================
 *  L!M Vario - Journal de vol (voir FlightLog.h)
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

static File       g_file;
static bool       g_logOk     = false;
static bool       g_srvOn     = false;
static uint32_t   g_lastLine  = 0;
static uint32_t   g_lastFlush = 0;
static WebServer  g_server(80);

// ------------------------------------------------------------
//  Ouverture d'un nouveau fichier de log
// ------------------------------------------------------------
static void log_open(void)
{
  if (!SD_MMC.exists(LOG_DIR)) SD_MMC.mkdir(LOG_DIR);

  // Nom base sur le RTC : LOG_20260610_1432.csv (fallback : numero)
  datetime_t t;
  PCF85063_Read_Time(&t);
  char path[64];
  if (t.year >= 2020 && t.year <= 2099) {
    snprintf(path, sizeof(path), LOG_DIR "/LOG_%04d%02d%02d_%02d%02d.csv",
             t.year, t.month, t.day, t.hour, t.minute);
    // si deux boots dans la meme minute : suffixe
    int n = 1;
    while (SD_MMC.exists(path) && n < 100) {
      snprintf(path, sizeof(path), LOG_DIR "/LOG_%04d%02d%02d_%02d%02d_%d.csv",
               t.year, t.month, t.day, t.hour, t.minute, n++);
    }
  } else {
    int n = 0;
    do {
      snprintf(path, sizeof(path), LOG_DIR "/LOG_%04d.csv", n++);
    } while (SD_MMC.exists(path) && n < 10000);
  }

  g_file = SD_MMC.open(path, FILE_WRITE);
  if (!g_file) {
    Serial.printf("[log] ECHEC ouverture %s\n", path);
    g_logOk = false;
    return;
  }
  g_file.println("ms,p_pa,alt_std_m,vario_baro,vario_fused,accel_vert,volume");
  g_logOk = true;
  Serial.printf("[log] -> %s\n", path);
}

void FlightLog_Init(void)
{
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[log] pas de carte SD : log desactive");
    g_logOk = false;
    return;
  }
  log_open();
}

bool FlightLog_Active(void) { return g_logOk && !g_srvOn; }

void FlightLog_Tick(float p_pa, float alt_m, float varioBaro,
                    float varioFused, float accelVert, int volume)
{
  if (!g_logOk || g_srvOn) return;
  uint32_t now = millis();
  if (now - g_lastLine < LOG_PERIOD_MS) return;
  g_lastLine = now;

  char line[128];
  int len = snprintf(line, sizeof(line), "%lu,%.1f,%.1f,%.2f,%.2f,%.2f,%d\n",
                     (unsigned long)now, p_pa, alt_m,
                     varioBaro, varioFused, accelVert, volume);
  g_file.write((const uint8_t*)line, len);

  if (now - g_lastFlush >= LOG_FLUSH_MS) {
    g_lastFlush = now;
    g_file.flush();              // garantit les donnees en cas de coupure
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
    // suspend le log (fichier clos proprement -> telechargeable)
    if (g_logOk && g_file) { g_file.flush(); g_file.close(); }
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
    Serial.println("[log] WiFi OFF");
    if (g_logOk) log_open();     // reprend le log dans un nouveau fichier
  }
}

void FlightLog_ServerLoop(void)
{
  if (g_srvOn) g_server.handleClient();
}

bool FlightLog_ServerActive(void) { return g_srvOn; }
