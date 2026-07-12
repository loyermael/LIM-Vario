/* Serveur HTTP minimal (Winsock2, non-bloquant, une requete a la fois) pour tester
 * l'app companion contre l'etat simule sans avoir le vrai vario sous la main.
 * Routes identiques a Firmware/src/FlightLog.cpp (memes noms de champs JSON), mais
 * lit/ecrit les globales de sim_menu.c au lieu du vrai firmware ESP32. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define LIM_SIM_BUILD
#include "CompanionApp_HTML.h"
#include "sim_server.h"

/* ---- Globales exposees par sim_menu.c (mêmes signatures que dans FlightLog.cpp) ---- */
extern int g_brightness, g_tonePitch, g_toneSpread, g_varioRange, g_screenRot;
extern uint8_t g_waveform, g_varioFilter, g_avgClimb, g_uAlt, g_uSpeed, g_uVert;
extern bool g_helperEnable, g_condorSim;
extern bool FlightLog_ServerActive(void);
extern void FlightLog_ServerToggle(void);
extern void Config_Save(void);

extern int g_gliderIdx, g_gliderEmptyWt, g_gliderMaxBal, g_gliderV1, g_gliderV2, g_gliderV3;
extern float g_gliderSi1, g_gliderSi2, g_gliderSi3;
extern int Glider_Count(void);
extern const char* Glider_Name(int i);
extern int   Glider_EmptyWt(int i);
extern int   Glider_MaxBal(int i);
extern int   Glider_V1(int i);
extern float Glider_Si1(int i);
extern int   Glider_V2(int i);
extern float Glider_Si2(int i);
extern int   Glider_V3(int i);
extern float Glider_Si3(int i);

extern int  g_profileIdx;
extern void Profile_Load(int idx);
extern void Profile_Save(int idx);
extern void Profile_Delete(int idx);
extern void Profile_RefreshName(void);
extern bool Profile_IsUsed(int idx);
extern void Profile_SetName(int idx, const char* name);
extern void Profile_GetName(int idx, char* out, size_t outLen);

extern uint8_t g_ibConfigClimb[6];
extern uint8_t g_ibConfigCruise[6];
extern uint8_t g_centerConfigClimb;
extern uint8_t g_centerConfigCruise;

static SOCKET g_listenSock = INVALID_SOCKET;

/* ---- Utilitaires JSON (memes recherches manuelles que FlightLog.cpp, en C) ---- */
static bool json_has(const char* body, const char* key) { return strstr(body, key) != NULL; }
static int json_int(const char* body, const char* key, int def) {
  const char* p = strstr(body, key);
  return p ? atoi(p + strlen(key)) : def;
}
static float json_float(const char* body, const char* key, float def) {
  const char* p = strstr(body, key);
  return p ? (float)atof(p + strlen(key)) : def;
}
static void parse_ib_array(const char* body, const char* key, uint8_t* out) {
  const char* p = strstr(body, key);
  if (!p) return;
  const char* start = strchr(p, '[');
  if (!start) return;
  start++;
  for (int i = 0; i < 6; i++) {
    out[i] = (uint8_t)atoi(start);
    const char* comma = strchr(start, ',');
    if (!comma) break;
    start = comma + 1;
  }
}
static int body_get_idx(const char* body) {
  int idx = json_int(body, "\"idx\":", -1);
  return (idx >= 0 && idx < 5) ? idx : -1;
}
static void safe_name(const char* in, char* out, int outCap) {
  int j = 0;
  for (int i = 0; in[i] && j < outCap - 1; i++) out[j++] = (in[i] == '"') ? '\'' : in[i];
  out[j] = 0;
}

/* ---- Reseau : envoi/reception ---- */
static void send_all(SOCKET s, const char* data, int len) {
  int sent = 0;
  while (sent < len) {
    int n = send(s, data + sent, len - sent, 0);
    if (n <= 0) break;
    sent += n;
  }
}

static void send_response(SOCKET s, int code, const char* contentType, const char* body, int bodyLen) {
  char header[256];
  const char* statusText = (code == 200) ? "OK" : (code == 404) ? "Not Found" : "Error";
  int hlen = snprintf(header, sizeof(header),
    "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
    code, statusText, contentType, bodyLen);
  send_all(s, header, hlen);
  if (bodyLen > 0) send_all(s, body, bodyLen);
}

#define REQ_BUF_SIZE 8192
static int recv_full_request(SOCKET s, char* buf, int cap) {
  int total = 0, headerEnd = -1, contentLength = 0;
  bool haveCL = false;
  while (total < cap - 1) {
    int n = recv(s, buf + total, cap - 1 - total, 0);
    if (n <= 0) break; /* timeout / ferme / erreur -- on traite ce qu'on a deja recu */
    total += n;
    buf[total] = 0;
    if (headerEnd < 0) {
      char* p = strstr(buf, "\r\n\r\n");
      if (p) {
        headerEnd = (int)(p - buf) + 4;
        char* cl = strstr(buf, "Content-Length:");
        if (!cl) cl = strstr(buf, "content-length:");
        if (cl) { contentLength = atoi(cl + 15); haveCL = true; }
      }
    }
    if (headerEnd >= 0 && (!haveCL || (total - headerEnd) >= contentLength)) break;
  }
  return total;
}

/* ---- Handlers ---- */
static void handle_app(SOCKET c) {
  send_response(c, 200, "text/html", COMPANION_APP_HTML, (int)strlen(COMPANION_APP_HTML));
}

static void handle_config_get(SOCKET c) {
  char buf[768];
  int len = snprintf(buf, sizeof(buf),
    "{\"bright\":%d,\"pitch\":%d,\"wave\":%d,\"spread\":%d,\"range\":%d,\"filter\":%d,\"avg\":%d,"
    "\"helper\":%d,\"ualt\":%d,\"uspeed\":%d,\"uvert\":%d,\"rot\":%d,\"condor\":%d,\"appconn\":%d,"
    "\"glidx\":%d,\"glewt\":%d,\"glmbal\":%d,\"v1\":%d,\"si1\":%.2f,\"v2\":%d,\"si2\":%.2f,\"v3\":%d,\"si3\":%.2f}",
    g_brightness, g_tonePitch, g_waveform, g_toneSpread, g_varioRange, g_varioFilter, g_avgClimb,
    g_helperEnable ? 1 : 0, g_uAlt, g_uSpeed, g_uVert, g_screenRot, g_condorSim ? 1 : 0,
    FlightLog_ServerActive() ? 1 : 0, g_gliderIdx, g_gliderEmptyWt, g_gliderMaxBal,
    g_gliderV1, g_gliderSi1, g_gliderV2, g_gliderSi2, g_gliderV3, g_gliderSi3);
  send_response(c, 200, "application/json", buf, len);
}

static void handle_config_post(SOCKET c, const char* body) {
  if (json_has(body, "\"bright\":")) { g_brightness = json_int(body, "\"bright\":", g_brightness); if (g_brightness < 0) g_brightness = 0; if (g_brightness > 20) g_brightness = 20; }
  if (json_has(body, "\"pitch\":"))  { g_tonePitch = json_int(body, "\"pitch\":", g_tonePitch); if (g_tonePitch < 200) g_tonePitch = 200; if (g_tonePitch > 1500) g_tonePitch = 1500; }
  if (json_has(body, "\"wave\":"))   g_waveform = (uint8_t)json_int(body, "\"wave\":", g_waveform);
  if (json_has(body, "\"spread\":")) g_toneSpread = json_int(body, "\"spread\":", g_toneSpread);
  if (json_has(body, "\"range\":"))  g_varioRange = json_int(body, "\"range\":", g_varioRange);
  if (json_has(body, "\"filter\":")) g_varioFilter = (uint8_t)json_int(body, "\"filter\":", g_varioFilter);
  if (json_has(body, "\"avg\":"))    g_avgClimb = (uint8_t)json_int(body, "\"avg\":", g_avgClimb);
  if (json_has(body, "\"helper\":")) g_helperEnable = (json_int(body, "\"helper\":", 1) == 1);
  if (json_has(body, "\"ualt\":"))   g_uAlt = (uint8_t)json_int(body, "\"ualt\":", g_uAlt);
  if (json_has(body, "\"uspeed\":")) g_uSpeed = (uint8_t)json_int(body, "\"uspeed\":", g_uSpeed);
  if (json_has(body, "\"uvert\":"))  g_uVert = (uint8_t)json_int(body, "\"uvert\":", g_uVert);
  if (json_has(body, "\"rot\":"))    g_screenRot = json_int(body, "\"rot\":", g_screenRot);
  if (json_has(body, "\"condor\":")) g_condorSim = (json_int(body, "\"condor\":", g_condorSim ? 1 : 0) == 1);
  if (json_has(body, "\"appconn\":")) {
    bool want = (json_int(body, "\"appconn\":", FlightLog_ServerActive() ? 1 : 0) == 1);
    if (want != FlightLog_ServerActive()) FlightLog_ServerToggle();
  }
  if (json_has(body, "\"glidx\":"))  g_gliderIdx = json_int(body, "\"glidx\":", g_gliderIdx);
  if (json_has(body, "\"glewt\":"))  g_gliderEmptyWt = json_int(body, "\"glewt\":", g_gliderEmptyWt);
  if (json_has(body, "\"glmbal\":")) g_gliderMaxBal = json_int(body, "\"glmbal\":", g_gliderMaxBal);
  if (json_has(body, "\"v1\":"))     g_gliderV1 = json_int(body, "\"v1\":", g_gliderV1);
  if (json_has(body, "\"si1\":"))    g_gliderSi1 = json_float(body, "\"si1\":", g_gliderSi1);
  if (json_has(body, "\"v2\":"))     g_gliderV2 = json_int(body, "\"v2\":", g_gliderV2);
  if (json_has(body, "\"si2\":"))    g_gliderSi2 = json_float(body, "\"si2\":", g_gliderSi2);
  if (json_has(body, "\"v3\":"))     g_gliderV3 = json_int(body, "\"v3\":", g_gliderV3);
  if (json_has(body, "\"si3\":"))    g_gliderSi3 = json_float(body, "\"si3\":", g_gliderSi3);
  Config_Save();
  send_response(c, 200, "application/json", "{\"status\":\"ok\"}", 16);
}

static void handle_screen_get(SOCKET c) {
  char buf[256];
  int len = snprintf(buf, sizeof(buf),
    "{\"climb\":[%d,%d,%d,%d,%d,%d],\"cruise\":[%d,%d,%d,%d,%d,%d],\"center_climb\":%d,\"center_cruise\":%d}",
    g_ibConfigClimb[0], g_ibConfigClimb[1], g_ibConfigClimb[2], g_ibConfigClimb[3], g_ibConfigClimb[4], g_ibConfigClimb[5],
    g_ibConfigCruise[0], g_ibConfigCruise[1], g_ibConfigCruise[2], g_ibConfigCruise[3], g_ibConfigCruise[4], g_ibConfigCruise[5],
    g_centerConfigClimb, g_centerConfigCruise);
  send_response(c, 200, "application/json", buf, len);
}

static void handle_screen_post(SOCKET c, const char* body) {
  parse_ib_array(body, "\"climb\":", g_ibConfigClimb);
  parse_ib_array(body, "\"cruise\":", g_ibConfigCruise);
  if (json_has(body, "\"center_climb\":"))  g_centerConfigClimb = (uint8_t)json_int(body, "\"center_climb\":", g_centerConfigClimb);
  if (json_has(body, "\"center_cruise\":")) g_centerConfigCruise = (uint8_t)json_int(body, "\"center_cruise\":", g_centerConfigCruise);
  Config_Save();
  send_response(c, 200, "application/json", "{\"status\":\"ok\"}", 16);
}

static void handle_gliders_get(SOCKET c) {
  int n = Glider_Count();
  int cap = n * 140 + 16;
  char* buf = (char*)malloc(cap);
  if (!buf) { send_response(c, 500, "text/plain", "OOM", 3); return; }
  int off = 0;
  buf[off++] = '[';
  for (int i = 0; i < n; i++) {
    char name[40];
    safe_name(Glider_Name(i), name, sizeof(name));
    if (i) buf[off++] = ',';
    off += snprintf(buf + off, cap - off,
      "{\"name\":\"%s\",\"empty_wt\":%d,\"max_bal\":%d,\"v1\":%d,\"si1\":%.2f,\"v2\":%d,\"si2\":%.2f,\"v3\":%d,\"si3\":%.2f}",
      name, Glider_EmptyWt(i), Glider_MaxBal(i), Glider_V1(i), Glider_Si1(i), Glider_V2(i), Glider_Si2(i), Glider_V3(i), Glider_Si3(i));
  }
  buf[off++] = ']';
  send_response(c, 200, "application/json", buf, off);
  free(buf);
}

static void handle_profiles_get(SOCKET c) {
  char buf[512];
  int off = snprintf(buf, sizeof(buf), "[");
  for (int i = 0; i < 5; i++) {
    char name[16];
    Profile_GetName(i, name, sizeof(name));
    off += snprintf(buf + off, sizeof(buf) - off, "%s{\"idx\":%d,\"name\":\"%s\",\"used\":%s}",
      i ? "," : "", i, name, Profile_IsUsed(i) ? "true" : "false");
  }
  off += snprintf(buf + off, sizeof(buf) - off, "]");
  send_response(c, 200, "application/json", buf, off);
}

static void handle_profiles_select(SOCKET c, const char* body) {
  int idx = body_get_idx(body);
  if (idx >= 0) { g_profileIdx = idx; Profile_Load(idx); Profile_RefreshName(); Config_Save(); }
  send_response(c, 200, "application/json", "{\"status\":\"ok\"}", 16);
}

static void handle_profiles_save(SOCKET c, const char* body) {
  int idx = body_get_idx(body);
  if (idx >= 0) {
    const char* p = strstr(body, "\"name\":\"");
    if (p) {
      p += strlen("\"name\":\"");
      const char* end = strchr(p, '"');
      char name[16] = {0};
      if (end) { int n = (int)(end - p); if (n > 15) n = 15; memcpy(name, p, n); name[n] = 0; }
      Profile_SetName(idx, name);
    }
    Profile_Save(idx);
    if (idx == g_profileIdx) Profile_RefreshName();
  }
  send_response(c, 200, "application/json", "{\"status\":\"ok\"}", 16);
}

static void handle_profiles_delete(SOCKET c, const char* body) {
  int idx = body_get_idx(body);
  if (idx >= 0) { Profile_Delete(idx); if (idx == g_profileIdx) Profile_RefreshName(); }
  send_response(c, 200, "application/json", "{\"status\":\"ok\"}", 16);
}

/* Meme logique que srv_captive_redirect dans Firmware/src/FlightLog.cpp -- reproduite ici
 * pour pouvoir verifier via curl que les endpoints de detection de portail captif
 * redirigent bien vers la racine de l'app, sans avoir besoin d'un vrai point d'acces WiFi. */
static void handle_captive_redirect(SOCKET c) {
  char header[256];
  int hlen = snprintf(header, sizeof(header),
    "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:8080/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  send_all(c, header, hlen);
}

/* ---- Dispatch + cycle de vie ---- */
void SimServer_Init(int port) {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
  g_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (g_listenSock == INVALID_SOCKET) return;
  u_long nonblock = 1;
  ioctlsocket(g_listenSock, FIONBIO, &nonblock);
  int opt = 1;
  setsockopt(g_listenSock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* localhost uniquement */
  addr.sin_port = htons((u_short)port);
  if (bind(g_listenSock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    closesocket(g_listenSock);
    g_listenSock = INVALID_SOCKET;
    return;
  }
  listen(g_listenSock, 4);
  printf("[sim_server] Companion app: http://127.0.0.1:%d\n", port);
}

void SimServer_Tick(void) {
  if (g_listenSock == INVALID_SOCKET) return;
  SOCKET c = accept(g_listenSock, NULL, NULL);
  if (c == INVALID_SOCKET) return; /* WSAEWOULDBLOCK attendu : rien en attente */

  DWORD timeout = 300;
  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
  setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

  static char reqBuf[REQ_BUF_SIZE];
  int n = recv_full_request(c, reqBuf, sizeof(reqBuf));
  if (n <= 0) { closesocket(c); return; }

  char method[8] = {0}, path[128] = {0};
  sscanf(reqBuf, "%7s %127s", method, path);

  const char* body = strstr(reqBuf, "\r\n\r\n");
  body = body ? body + 4 : (reqBuf + n);

  if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) handle_app(c);
  else if (strcmp(path, "/api/config") == 0)  { if (strcmp(method, "GET") == 0) handle_config_get(c); else handle_config_post(c, body); }
  else if (strcmp(path, "/api/screen") == 0)  { if (strcmp(method, "GET") == 0) handle_screen_get(c); else handle_screen_post(c, body); }
  else if (strcmp(path, "/api/gliders") == 0) handle_gliders_get(c);
  else if (strcmp(path, "/api/profiles") == 0) handle_profiles_get(c);
  else if (strcmp(path, "/api/profiles/select") == 0) handle_profiles_select(c, body);
  else if (strcmp(path, "/api/profiles/save") == 0)   handle_profiles_save(c, body);
  else if (strcmp(path, "/api/profiles/delete") == 0) handle_profiles_delete(c, body);
  else handle_captive_redirect(c);  /* sondes captive portal (is_captive_probe_path) + onNotFound cote reel : tout chemin inconnu -> redirige vers l'app */

  closesocket(c);
}

void SimServer_Shutdown(void) {
  if (g_listenSock != INVALID_SOCKET) { closesocket(g_listenSock); g_listenSock = INVALID_SOCKET; }
  WSACleanup();
}
