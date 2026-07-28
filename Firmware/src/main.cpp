/* ============================================================
 *  L!M Vario - Firmware principal
 *  Carte : Waveshare ESP32-S3-Touch-LCD-2.1 (480x480 rond)
 *
 *  Screen/touch/IMU drivers: provided by Waveshare
 *  Interface graphique        : generee par EEZ Studio (src/ui)
 *
 *  All LVGL objects are referenced by their EEZ NAME (stable).
 *  A "Build Files" in EEZ no longer breaks anything.
 * ============================================================ */

#include "Wireless.h"
#include "Gyro_QMI8658.h"
#include "RTC_PCF85063.h"
#include "SD_Card.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"

#include "ui/ui.h"
#include "ui/screens.h"
#include "ui/images.h"   // img_gps_connected / img_gps_waiting
#include "lim_link.h"
#include "VarioFusion.h"
#include "FlightLog.h"
#include "ThermalHelper.h"
#include "ThermalDraw.h"
#include <Preferences.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

// ============================================================
//  ⚠️ BENCH SIMULATION - SET TO 0 BEFORE A REAL FLIGHT ⚠️
//  1 = injects a fake glider circling in an off-center thermal
//      to see the thermal helper without GPS or flight (bench test).
// ============================================================
#define SIM_THERMAL  0

// ============================================================
//  LIAISON UART <- CALCULATEUR
// ============================================================
#define LINK_RX  44
#define LINK_TX  43

static volatile float g_vario    = 0.0f;
static volatile float g_varioInt = 0.0f;
static volatile float g_pressure = 0.0f;
static volatile float g_altitude = 0.0f;
static bool g_linkOk     = false;
static bool g_linkSynced = false;

static int32_t enc1Last = 0, enc2Last = 0;
static bool    enc1BtnLast = false, enc2BtnLast = false;
static volatile int g_volume = 0;    // 0..20 (default 0 = silent; aligned with the calculator's sndVol)

static uint32_t g_pktCount = 0;

// ============================================================
//  FUSION IMU + BARO (voir VarioFusion.cpp)
//  AHRS Mahony + Kalman 4 etats {alt, vario, accel, biais}.
//  Tourne dans Driver_Loop (core 0, cadence reguliere ~50 Hz).
// ============================================================
static volatile float g_varioFused = 0.0f;   // vario fusionne (m/s)
static volatile float g_varioComp  = 0.0f;   // vario apres compensation GPS (m/s)
static volatile float g_airspeed   = 0.0f;   // vitesse AIR (MS4525 ; 0 si pas de pitot)
static volatile float g_gndSpeed   = 0.0f;   // GPS ground speed (same unit as g_airspeed)
static volatile float g_gpsAlt     = NAN;    // altitude GPS (m ; NaN si pas de fix)
static volatile float g_gpsLat     = NAN;    // latitude GPS (deg decimaux, +N/-S ; NaN si pas de fix)
static volatile float g_gpsLon     = NAN;    // longitude GPS (deg decimaux, +E/-W ; NaN si pas de fix)
static volatile bool  g_gpsOk      = false;  // flag fix GPS valide (recu du calculateur)
static volatile float g_gpsTrack   = NAN;    // cap sol GPS (deg 0..360, NaN si pas de fix)
// LIS3MDL magnetometer (calculateur, bus I2C partage). Champ brut non calibre, aucune
// compensation d'assiette ici -> pas encore de cap magnetique exploitable, juste la
// plomberie liaison -> reception. Fusion avec l'IMU (deja sur cette carte) a faire plus tard.
static volatile float g_magX       = 0.0f;   // champ magnetique, uT, repere capteur
static volatile float g_magY       = 0.0f;
static volatile float g_magZ       = 0.0f;
static volatile bool  g_magOk      = false;  // LIS3MDL detecte cote calculateur
static volatile bool  g_circling   = false;  // true = spiral detected (else straight flight)
static volatile int   g_turnDir    = 0;      // turn direction: +1 right / -1 left / 0
static float g_varioFiltered = NAN;   // vario apres filtre utilisateur (Fast/Med/Slow), EMA
static float g_varioAvg      = 0.0f;  // moving average of the vario (Avg climb 15/20/30 s), recomputed on screen
static float g_climbGain     = 0.0f;  // altitude gain in the current thermal (m)
static uint32_t g_takeoffMs  = 0;     // takeoff instant in ms (0 = not airborne yet)
static bool     g_inFlight   = false; // flight state (takeoff / landing detection)

// ============================================================
//  SON VARIO (GPIO0 → MOSFET → buzzer piezo passif)
//  Algorithme de fusion inertielle :
//    Montee  : freq 700→2000 Hz, cadence 1200→120 ms
//    Neutral : silence (-0.3 to +0.15 m/s)
//    Descente: 350 Hz continu (si Full) ou silence (si Mute)
// ============================================================
#define VARIO_PIN          0          // GPIO0 → buzzer externe (futur)
// Buzzer interne Waveshare = EXIO_PIN8 via TCA9554 (I2C, software toggle)
#define VARIO_DEAD_LOW    -0.30f      // m/s: sink threshold
#define VARIO_DEAD_HIGH    0.15f      // m/s: lift threshold
#define VARIO_VMAX         3.0f       // m/s : vario max (au-dela = cadence max)
#define VARIO_FREQ_LOW     700        // Hz a DEAD_HIGH
#define VARIO_FREQ_HIGH    2000       // Hz a VMAX
#define VARIO_FREQ_SINK    350        // Hz descente
#define VARIO_PERIOD_SLOW  1000       // ms : 1 bip/s a DEAD_HIGH
#define VARIO_PERIOD_FAST   150       // ms : ~6 bips/s a VMAX
#define VARIO_DUTY_ON       0.50f     // 50% bip / 50% silence (plus audible)

// Volume arc (shown temporarily in the free right-side zone when enc2 turns)
static lv_obj_t*  g_arcVol     = NULL;   // l'arc LVGL
static lv_obj_t*  g_lblVolNum  = NULL;   // the number at the center of the arc
static uint32_t   g_volShownAt = 0;      // timestamp du dernier changement
#define VOL_HIDE_MS  2000                // disparait apres 2s d'inactivite

// ============================================================
//  MacCready
// ============================================================
static volatile int g_mcTenths = 0;
#define MC_MIN_T 0
#define MC_MAX_T 50

// ============================================================
//  QUICK MENU
// ============================================================
enum MenuState { MENU_CLOSED, MENU_NAV, MENU_EDIT };
static volatile MenuState g_menuState = MENU_CLOSED;
static volatile int  g_menuIndex = 0;
static volatile bool g_menuDirty = true;

#define MENU_COUNT  7
#define MENU_SOUND  4     // Sink Snd. is visually at position 4 in EEZ (y≈176)
#define MENU_EXIT   6
#define MENU_ROW_H  44

static volatile int  g_qnh    = 1013;
static volatile int  g_water  = 0;
static volatile int  g_bugs   = 0;
static volatile int  g_weight = 70;
static volatile bool g_sinkSound = false;

static uint32_t btnDownTime  = 0;
static bool     btnLongFired = false;
#define LONG_PRESS_MS    600
#define MENU_TIMEOUT_MS  8000
static volatile uint32_t g_menuLastActivity = 0;

// ============================================================
//  SETUP MENU (appui long ENC1) - navigation generique
//  UN seul panneau EEZ (setup_panel), 5 cases item0..item4 en
//  "centered selection" (the current item is always in the middle).
// ============================================================
static volatile bool g_setupOpen = false;
// Settings controllable from the menu
int  g_brightness   = 20;     // 0..20 (mappe x5 -> 0..100 retroeclairage), defaut = max
bool g_helperEnable = true;   // thermal assistant on/off (handled via Info Boxes, no longer in the Display menu)
bool g_loggerEnable = true;   // logger SD
int  g_varioRange   = 5;      // +/-5 or +/-10 m/s (needle scale)
int  g_screenRot    = 0;      // 0/90/180/270 (plomberie ; rotation pas encore appliquee a la dalle)
uint8_t g_uVert     = 0;      // 0=m/s 1=kt
uint8_t g_uAlt      = 0;      // 0=m   1=ft
uint8_t g_uSpeed    = 0;      // 0=km/h 1=kt
int  g_tonePitch    = 700;    // Hz, frequence de base du son vario (plomberie ; effet son = calc)
uint8_t g_waveform  = 0;      // 0=Sine 1=Square 2=Triangle
int  g_toneSpread   = 5;      // 0-10, intensity of the sound variation with the vario
uint8_t g_varioFilter = 1;    // 0=Fast 1=Med 2=Slow (display only for now)
uint8_t g_avgClimb    = 1;    // 0=15s 1=20s 2=30s (display only for now)
static bool g_updateMode     = false;// WiFi OTA update (effet a cabler)
bool g_condorSim       = false;// mode simulation Condor (bypass fusion IMU/baro dans Comp_Apply)

enum InfoBoxMetric {
  IB_VARIO_INST  = 0,
  IB_VARIO_INT   = 1,
  IB_MACCREADY   = 2,
  IB_ALT_BARO    = 3,
  IB_ALT_GPS     = 4,
  IB_AIRSPEED    = 5,
  IB_GND_SPEED   = 6,
  IB_TIME        = 7,
  IB_FLIGHT_TIME = 8,
  IB_WIND        = 9,
  IB_CLIMB_GAIN  = 10,
  IB_FLIGHT_LVL  = 11,
  IB_GLIDE       = 12,
  IB_EMPTY       = 13,
  IB_NETTO       = 14,  // polar-compensated vario (air-mass movement)
  IB_STF         = 15,  // optimal cruise speed (MacCready + polar)
  IB_ALERTS      = 16,  // active faults (link / battery / SD / GPS) else "OK"
  IB_MODE        = 17,  // profil d'info-boxes actif : Climb ou Cruise
  IB_STF_CMD     = 18,  // speed-to-fly command: fly faster (+) / slower (-)
  IB_METRIC_MAX  = 19
};

enum CenterZoneMetric {
  CENTER_THERMAL_HELPER = 0,
  CENTER_WIND_DIR       = 1,
  CENTER_EMPTY          = 2,
  CENTER_METRIC_MAX     = 3
};

uint8_t g_ibConfigClimb[6]  = { IB_VARIO_INST, IB_VARIO_INT, IB_EMPTY, IB_ALT_BARO, IB_CLIMB_GAIN, IB_WIND };
uint8_t g_ibConfigCruise[6] = { IB_VARIO_INST, IB_MACCREADY, IB_EMPTY, IB_ALT_BARO, IB_GLIDE, IB_GND_SPEED };
uint8_t g_centerConfigClimb = CENTER_THERMAL_HELPER;
uint8_t g_centerConfigCruise = CENTER_WIND_DIR;
static bool    g_ibEditCruiseMode  = true; // false=Climb, true=Cruise (Cruise = default displayed profile)
static uint8_t* g_infoBoxConfig    = g_ibConfigCruise; // pointeur vers profil actif

enum InfoBoxEditState {
  IBEDIT_NONE = 0,
  IBEDIT_SELECT_MODE = 1,
  IBEDIT_SELECT_ZONE = 2,
  IBEDIT_CHOOSE_METRIC = 3
};
static InfoBoxEditState g_ibEditState = IBEDIT_NONE;
static int s_ibZoneSel = 0;
static int s_ibChooseSel = 0;
static lv_obj_t* s_ibFrames[7] = {0};   // [6] = ib_frame_6 = "Back" (ajoute 2 juillet 2026)
static lv_obj_t* s_ibLabels[6] = {0};
static lv_obj_t* s_ibValLabels[7] = {0};  // [6] never assigned: "Back" has fixed text, no dynamic value

static const char* const s_ibMetricNames[IB_METRIC_MAX] = {
  "Inst. Vario",
  "Avg. Vario",
  "MacCready",
  "Baro Alt.",
  "GPS Alt.",
  "Airspeed (IAS)",
  "Ground Speed",
  "Time",
  "Flight Time",
  "Wind",
  "Climb Gain",
  "Flight Level",
  "Glide Ratio",
  "Disabled",
  "Netto",
  "Speed to Fly",
  "Alerts",
  "Mode",
  "STF Command"
};

static const char* const s_ibMetricAbbrev[IB_METRIC_MAX] = {
  "Inst. Vario",
  "Avg. Vario",
  "MacCready",
  "Baro Alt.",
  "GPS Alt.",
  "Airspeed",
  "Gnd Speed",
  "Time",
  "Flight Time",
  "Wind",
  "Climb Gain",
  "Flight Lvl",
  "Glide Ratio",
  "",
  "Netto",
  "STF",
  "Alerts",
  "Mode",
  "STF Cmd"
};

static const char* const s_centerMetricAbbrev[CENTER_METRIC_MAX] = {
  "Thermal Help",
  "Wind Dir.",
  ""
};

typedef struct {
  const char* name;
  int empty_wt;
  int max_bal;
  int v1; float si1;
  int v2; float si2;
  int v3; float si3;
} GliderData;

#include "GliderPolars.h"

static GliderData g_gliderDb[300];
static int g_gliderDbCount = 0;
static char g_gliderNamesBuf[300 * 32];
static int g_gliderNamesBufOffset = 0;

static void GliderDB_LoadDefault() {
  g_gliderDbCount = GLIDER_POLARS_COUNT;
  g_gliderNamesBufOffset = 0;
  for (int i = 0; i < g_gliderDbCount; i++) {
    g_gliderDb[i] = GLIDER_POLARS[i];
    int len = strlen(GLIDER_POLARS[i].name);
    if (g_gliderNamesBufOffset + len + 1 < (int)sizeof(g_gliderNamesBuf)) {
      strcpy(&g_gliderNamesBuf[g_gliderNamesBufOffset], GLIDER_POLARS[i].name);
      g_gliderDb[i].name = &g_gliderNamesBuf[g_gliderNamesBufOffset];
      g_gliderNamesBufOffset += len + 1;
    }
  }
}

static void GliderDB_LoadSD() {
  GliderDB_LoadDefault();
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("[GLIDER] No SD card found, using defaults");
    return;
  }
  const char* filename = "/polars.txt";
  if (!SD_MMC.exists(filename)) {
    if (SD_MMC.exists("/polars.plr")) filename = "/polars.plr";
    else if (SD_MMC.exists("/polar.txt")) filename = "/polar.txt";
    else {
      Serial.println("[GLIDER] /polars.txt not found on SD, using defaults");
      return;
    }
  }
  File f = SD_MMC.open(filename, FILE_READ);
  if (!f) {
    Serial.printf("[GLIDER] Failed to open %s\n", filename);
    return;
  }
  g_gliderDbCount = 0;
  g_gliderNamesBufOffset = 0;
  Serial.printf("[GLIDER] Loading from SD card (%s)...\n", filename);
  while (f.available() && g_gliderDbCount < 299) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("*") || line.startsWith("#")) {
      continue;
    }
    char name[32];
    name[0] = '\0';
    int commaIdx = line.indexOf(',');
    if (commaIdx <= 0) continue;
    String namePart = line.substring(0, commaIdx);
    namePart.trim();
    strncpy(name, namePart.c_str(), sizeof(name)-1);
    name[sizeof(name)-1] = '\0';
    String dataPart = line.substring(commaIdx + 1);
    dataPart.trim();

    float vals[8];
    int valIdx = 0;
    int start = 0;
    while (valIdx < 8 && start <= (int)dataPart.length()) {
      int idx = dataPart.indexOf(',', start);
      String token;
      if (idx == -1) {
        token = dataPart.substring(start);
        start = dataPart.length() + 1;
      } else {
        token = dataPart.substring(start, idx);
        start = idx + 1;
      }
      token.trim();
      vals[valIdx++] = token.toFloat();
    }
    if (valIdx == 8) {
      int len = strlen(name);
      if (g_gliderNamesBufOffset + len + 1 < (int)sizeof(g_gliderNamesBuf)) {
        strcpy(&g_gliderNamesBuf[g_gliderNamesBufOffset], name);
        g_gliderDb[g_gliderDbCount].name = &g_gliderNamesBuf[g_gliderNamesBufOffset];
        g_gliderNamesBufOffset += len + 1;
        g_gliderDb[g_gliderDbCount].empty_wt = (int)round(vals[0]);
        g_gliderDb[g_gliderDbCount].max_bal  = (int)round(vals[1]);
        g_gliderDb[g_gliderDbCount].v1       = (int)round(vals[2]);
        g_gliderDb[g_gliderDbCount].si1      = vals[3];
        g_gliderDb[g_gliderDbCount].v2       = (int)round(vals[4]);
        g_gliderDb[g_gliderDbCount].si2      = vals[5];
        g_gliderDb[g_gliderDbCount].v3       = (int)round(vals[6]);
        g_gliderDb[g_gliderDbCount].si3      = vals[7];
        g_gliderDbCount++;
      }
    }
  }
  f.close();
  Serial.printf("[GLIDER] Loaded %d gliders from SD card\n", g_gliderDbCount);
  if (g_gliderDbCount == 0) {
    GliderDB_LoadDefault();
  }
}

int   g_gliderIdx      = 0;
int   g_gliderEmptyWt  = 240;
int   g_gliderMaxBal   = 185;
int   g_gliderV1       = 80;
float g_gliderSi1      = -0.59f;
int   g_gliderV2       = 115;
float g_gliderSi2      = -0.76f;
int   g_gliderV3       = 173;
float g_gliderSi3      = -2.00f;

int   g_profileIdx     = 0;

// Accessors for g_gliderDb (stays static: avoids exposing GliderData/the raw array
// to the other .cpp files, used by /api/gliders in FlightLog.cpp).
int Glider_Count() { return g_gliderDbCount; }
const char* Glider_Name(int i)   { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].name : ""; }
int   Glider_EmptyWt(int i) { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].empty_wt : 0; }
int   Glider_MaxBal(int i)  { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].max_bal  : 0; }
int   Glider_V1(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v1  : 0; }
float Glider_Si1(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si1 : 0; }
int   Glider_V2(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v2  : 0; }
float Glider_Si2(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si2 : 0; }
int   Glider_V3(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v3  : 0; }
float Glider_Si3(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si3 : 0; }

void Profile_Load(int idx) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, true);
  g_gliderIdx     = p.getInt("glidx", 0);
  g_gliderEmptyWt = p.getInt("glewt", 240);
  g_gliderMaxBal  = p.getInt("glmbal", 185);
  g_gliderV1      = p.getInt("glv1", 80);
  g_gliderSi1     = p.getFloat("glsi1", -0.59f);
  g_gliderV2      = p.getInt("glv2", 115);
  g_gliderSi2     = p.getFloat("glsi2", -0.76f);
  g_gliderV3      = p.getInt("glv3", 173);
  g_gliderSi3     = p.getFloat("glsi3", -2.00f);
  p.end();
}

void Profile_Save(int idx) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, false);
  p.putInt("glidx", g_gliderIdx);
  p.putInt("glewt", g_gliderEmptyWt);
  p.putInt("glmbal", g_gliderMaxBal);
  p.putInt("glv1", g_gliderV1);
  p.putFloat("glsi1", g_gliderSi1);
  p.putInt("glv2", g_gliderV2);
  p.putFloat("glsi2", g_gliderSi2);
  p.putInt("glv3", g_gliderV3);
  p.putFloat("glsi3", g_gliderSi3);
  p.end();
}

void Profile_Delete(int idx) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, false);
  p.putString("name", "");
  p.putInt("glidx", 0);
  p.putInt("glewt", 240);
  p.putInt("glmbal", 185);
  p.putInt("glv1", 80);
  p.putFloat("glsi1", -0.59f);
  p.putInt("glv2", 115);
  p.putFloat("glsi2", -0.76f);
  p.putInt("glv3", 173);
  p.putFloat("glsi3", -2.00f);
  p.end();
  if (idx == g_profileIdx) {
    Profile_Load(idx);
  }
}

// Writes a profile's name without touching its other data (used by the companion API).
void Profile_SetName(int idx, const char* name) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, false);
  p.putString("name", name);
  p.end();
}

// Reads a profile's raw name (empty string if unused) -- used by /api/profiles.
void Profile_GetName(int idx, char* out, size_t outLen) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, true);
  String n = p.getString("name", "");
  p.end();
  strncpy(out, n.c_str(), outLen - 1);
  out[outLen - 1] = 0;
}

static Preferences prefs;

void Config_Save() {
  prefs.begin("limvario", false);
  prefs.putInt("bright", g_brightness);
  prefs.putBool("helper", g_helperEnable);
  prefs.putBool("logger", g_loggerEnable);
  prefs.putInt("range", g_varioRange);
  prefs.putInt("rot", g_screenRot);
  prefs.putUChar("uvert", g_uVert);
  prefs.putUChar("ualt", g_uAlt);
  prefs.putUChar("uspeed", g_uSpeed);
  prefs.putInt("pitch", g_tonePitch);
  prefs.putUChar("wave", g_waveform);
  prefs.putInt("spread", g_toneSpread);
  prefs.putUChar("vfilter", g_varioFilter);
  prefs.putUChar("vavg", g_avgClimb);
  prefs.putBool("upd", g_updateMode);
  prefs.putBool("condor", g_condorSim);
  prefs.putInt("glidx", g_gliderIdx);
  prefs.putInt("glewt", g_gliderEmptyWt);
  prefs.putInt("glmbal", g_gliderMaxBal);
  prefs.putInt("glv1", g_gliderV1);
  prefs.putFloat("glsi1", g_gliderSi1);
  prefs.putInt("glv2", g_gliderV2);
  prefs.putFloat("glsi2", g_gliderSi2);
  prefs.putInt("glv3", g_gliderV3);
  prefs.putFloat("glsi3", g_gliderSi3);
  prefs.putInt("profidx", g_profileIdx);
  prefs.putBytes("ib_climb", g_ibConfigClimb, 6);
  prefs.putBytes("ib_cruise", g_ibConfigCruise, 6);
  prefs.putUChar("c_climb", g_centerConfigClimb);
  prefs.putUChar("c_cruise", g_centerConfigCruise);
  prefs.end();
}

static void Config_Load() {
  GliderDB_LoadDefault();
  prefs.begin("limvario", true);
  g_brightness  = prefs.getInt("bright", 20);
  g_helperEnable= prefs.getBool("helper", true);
  g_loggerEnable= prefs.getBool("logger", true);
  g_varioRange  = prefs.getInt("range", 5);
  g_screenRot   = prefs.getInt("rot", 0);
  g_uVert       = prefs.getUChar("uvert", 0);
  g_uAlt        = prefs.getUChar("ualt", 0);
  g_uSpeed      = prefs.getUChar("uspeed", 0);
  g_tonePitch   = prefs.getInt("pitch", 700);
  g_waveform    = prefs.getUChar("wave", 0);
  g_toneSpread  = prefs.getInt("spread", 5);
  g_varioFilter = prefs.getUChar("vfilter", 1);
  g_avgClimb    = prefs.getUChar("vavg", 1);
  g_updateMode  = prefs.getBool("upd", false);
  g_condorSim   = prefs.getBool("condor", false);
  g_gliderIdx   = prefs.getInt("glidx", 0);
  g_gliderEmptyWt= prefs.getInt("glewt", 240);
  g_gliderMaxBal = prefs.getInt("glmbal", 185);
  g_gliderV1    = prefs.getInt("glv1", 80);
  g_gliderSi1   = prefs.getFloat("glsi1", -0.59f);
  g_gliderV2    = prefs.getInt("glv2", 115);
  g_gliderSi2   = prefs.getFloat("glsi2", -0.76f);
  g_gliderV3    = prefs.getInt("glv3", 173);
  g_gliderSi3   = prefs.getFloat("glsi3", -2.00f);
  g_profileIdx  = prefs.getInt("profidx", 0);
  if (prefs.getBytes("ib_climb", g_ibConfigClimb, 6) != 6) {
    uint8_t defClimb[6] = { IB_VARIO_INST, IB_VARIO_INT, IB_EMPTY, IB_ALT_BARO, IB_CLIMB_GAIN, IB_WIND };
    memcpy(g_ibConfigClimb, defClimb, 6);
  }
  if (prefs.getBytes("ib_cruise", g_ibConfigCruise, 6) != 6) {
    uint8_t defCruise[6] = { IB_VARIO_INST, IB_MACCREADY, IB_EMPTY, IB_ALT_BARO, IB_GLIDE, IB_GND_SPEED };
    memcpy(g_ibConfigCruise, defCruise, 6);
  }
  g_centerConfigClimb = prefs.getUChar("c_climb", CENTER_THERMAL_HELPER);
  g_centerConfigCruise = prefs.getUChar("c_cruise", CENTER_WIND_DIR);
  for (int i = 0; i < 6; i++) {
    if (g_ibConfigClimb[i] >= IB_METRIC_MAX) g_ibConfigClimb[i] = IB_EMPTY;
    if (g_ibConfigCruise[i] >= IB_METRIC_MAX) g_ibConfigCruise[i] = IB_EMPTY;
  }
  if (g_centerConfigClimb >= CENTER_METRIC_MAX) g_centerConfigClimb = CENTER_THERMAL_HELPER;
  if (g_centerConfigCruise >= CENTER_METRIC_MAX) g_centerConfigCruise = CENTER_WIND_DIR;
  g_infoBoxConfig = g_ibEditCruiseMode ? g_ibConfigCruise : g_ibConfigClimb;
  if (g_gliderIdx < 0 || g_gliderIdx >= g_gliderDbCount) g_gliderIdx = 0;
  if (g_profileIdx < 0 || g_profileIdx >= 5) g_profileIdx = 0;
  prefs.end();
}

enum { SM_ROOT, SM_VARIO, SM_SOUND, SM_DISPLAY, SM_SYSTEM, SM_INFOBOX, SM_UNITS, SM_ABOUT, SM_GLIDER, SM_PROFILE, SM_INFOBOX_METRIC, SM_N };
enum { ST_SUB, ST_TOGGLE, ST_VALUE, ST_CHOICE, ST_INFO, ST_BACK };
enum { SET_NONE, SET_HELPER, SET_BRIGHT, SET_VOLUME, SET_SINK, SET_LOGGER, SET_CONDOR, SET_RANGE,
       SET_ROT, SET_U_VERT, SET_U_ALT, SET_U_SPEED, SET_PITCH, SET_WAVE, SET_SPREAD,
       SET_VFILTER, SET_VAVG, SET_UPDATE, SET_CONDORSIM, SET_FWVER, SET_BUILD, SET_LINKVER, SET_CREATOR, SET_ALGO,
       SET_APPCONNECT, SET_RESET_CFG, SET_FACTORY_RESET,
       SET_GLIDER_MODEL, SET_GLIDER_EMPTY_WT, SET_GLIDER_MAX_BAL, SET_GLIDER_V1, SET_GLIDER_SI1, SET_GLIDER_V2, SET_GLIDER_SI2, SET_GLIDER_V3, SET_GLIDER_SI3,
       SET_PROFILE_SELECT, SET_PROFILE_EDIT, SET_PROFILE_NEW, SET_PROFILE_SAVE, SET_PROFILE_DELETE };

#define LIM_FW_SCREEN "0.9.0"   // screen firmware version (About menu)

typedef struct { const char* label; uint8_t type; uint8_t arg; } SmItem;
typedef struct { const char* title; const SmItem* items; uint8_t n; } SmMenu;

static const SmItem RIT[]  = { {"Display",ST_SUB,SM_DISPLAY},{"Sound",ST_SUB,SM_SOUND},{"Vario",ST_SUB,SM_VARIO},{"System",ST_SUB,SM_SYSTEM},{"Glider infos",ST_SUB,SM_GLIDER},{"Profile",ST_SUB,SM_PROFILE},{"Exit",ST_BACK,0} };
static const SmItem VIT[]  = { {"Vario range",ST_CHOICE,SET_RANGE},{"Vario filter",ST_CHOICE,SET_VFILTER},{"Avg climb",ST_CHOICE,SET_VAVG},{"Back",ST_BACK,0} };
static const SmItem SIT[]  = { {"Tone pitch",ST_VALUE,SET_PITCH},{"Waveform",ST_CHOICE,SET_WAVE},{"Tone spread",ST_VALUE,SET_SPREAD},{"Back",ST_BACK,0} };
static const SmItem DIT[]  = { {"Info boxes",ST_SUB,SM_INFOBOX},{"Units",ST_SUB,SM_UNITS},{"Brightness",ST_VALUE,SET_BRIGHT},{"Screen rot.",ST_CHOICE,SET_ROT},{"Back",ST_BACK,0} };
static const SmItem SYIT[] = {
  {"App connect",   ST_TOGGLE, SET_APPCONNECT},
  {"Condor sim",    ST_TOGGLE, SET_CONDORSIM},
  {"Reset config",  ST_INFO,   SET_RESET_CFG},
  {"Factory reset", ST_INFO,   SET_FACTORY_RESET},
  {"About",         ST_SUB,    SM_ABOUT},
  {"Back",          ST_BACK,   0}
};
static const SmItem ABT[]  = {
  {"Version",    ST_INFO, SET_FWVER},
  {"Build",      ST_INFO, SET_BUILD},
  {"Link Prot.", ST_INFO, SET_LINKVER},
  {"Back",       ST_BACK, 0}
};
static const SmItem IBIT_MODE[] = {
  {"Climb Mode",  ST_INFO, 0},
  {"Cruise Mode", ST_INFO, 1},
  {"Back",        ST_BACK, 0}
};
// .arg = the ACTUAL InfoBoxMetric enum value (not the list index) -> read via it->arg,
// never via the raw g_smSel (see off-by-one bug fixed on 2 July 2026: Ground Speed
// missing from the list shifted everything else by one).
static const SmItem IBIT_LIST[] = {
  {"Inst. Vario", ST_INFO, IB_VARIO_INST}, {"Avg. Vario", ST_INFO, IB_VARIO_INT}, {"MacCready", ST_INFO, IB_MACCREADY},
  {"Baro Alt.", ST_INFO, IB_ALT_BARO}, {"GPS Alt.", ST_INFO, IB_ALT_GPS}, {"Time", ST_INFO, IB_TIME},
  {"Flight Time", ST_INFO, IB_FLIGHT_TIME}, {"Wind", ST_INFO, IB_WIND}, {"Climb Gain", ST_INFO, IB_CLIMB_GAIN},
  {"Flight Level", ST_INFO, IB_FLIGHT_LVL}, {"Glide Ratio", ST_INFO, IB_GLIDE}, {"Airspeed", ST_INFO, IB_AIRSPEED},
  {"Ground Speed", ST_INFO, IB_GND_SPEED},
  {"Netto", ST_INFO, IB_NETTO}, {"Speed to Fly", ST_INFO, IB_STF}, {"STF Command", ST_INFO, IB_STF_CMD},
  {"Alerts", ST_INFO, IB_ALERTS}, {"Mode", ST_INFO, IB_MODE},
  {"Disabled", ST_INFO, IB_EMPTY},
  {"Back", ST_BACK, 0}
};
static const SmItem CI_LIST[] = {
  {"Thermal Helper", ST_INFO, 0}, {"Wind Direction", ST_INFO, 1}, {"Disabled", ST_INFO, 2}, {"Back", ST_BACK, 0}
};
static const SmItem UIT[]  = { {"Vertical",ST_CHOICE,SET_U_VERT},{"Altitude",ST_CHOICE,SET_U_ALT},{"Speed",ST_CHOICE,SET_U_SPEED},{"Back",ST_BACK,0} };
static const SmItem GLIT[] = {
  {"Glider",       ST_CHOICE, SET_GLIDER_MODEL},
  {"Empty weight", ST_VALUE,  SET_GLIDER_EMPTY_WT},
  {"Max ballast",  ST_VALUE,  SET_GLIDER_MAX_BAL},
  {"Polar V1",     ST_VALUE,  SET_GLIDER_V1},
  {"Polar Si1",    ST_VALUE,  SET_GLIDER_SI1},
  {"Polar V2",     ST_VALUE,  SET_GLIDER_V2},
  {"Polar Si2",    ST_VALUE,  SET_GLIDER_SI2},
  {"Polar V3",     ST_VALUE,  SET_GLIDER_V3},
  {"Polar Si3",    ST_VALUE,  SET_GLIDER_SI3},
  {"Back",         ST_BACK,   0}
};
static const SmItem PRIT[] = {
  {"Profile",  ST_CHOICE, SET_PROFILE_SELECT},
  {"Edit",     ST_INFO,   SET_PROFILE_EDIT},
  {"New",      ST_INFO,   SET_PROFILE_NEW},
  {"Save",     ST_INFO,   SET_PROFILE_SAVE},
  {"Delete",   ST_INFO,   SET_PROFILE_DELETE},
  {"Back",     ST_BACK,   0}
};

static const SmMenu SM[SM_N] = {
  {"Settings",RIT,7},{"Vario",VIT,4},{"Sound",SIT,4},{"Display",DIT,5},
  {"System",SYIT,6},{"Info Boxes",IBIT_MODE,3},{"Units",UIT,4},{"About",ABT,4},
  {"Glider infos",GLIT,10},{"Profile",PRIT,6},{"Select Metric",IBIT_LIST,17}
};

static uint8_t g_smMenu = SM_ROOT;
static int8_t  g_smSel  = 0;
static uint8_t g_smStk[6]; static int8_t g_smStkSel[6]; static int g_smDepth = 0;
static bool    g_smEdit = false;
static bool    g_smDirty = true;
static lv_obj_t* s_smVal[7] = {0};   // "value" labels (on the right), created by code
// Display submenu = list hand-built in EEZ (display_list)
static lv_obj_t* s_dName[5] = {0};   // names: dname0..dname4 (text written in EEZ)
static lv_obj_t* s_dVal[5]  = {0};   // valeurs : seuls [2]=Brightness (dval2) et [3]=Screen rot. (dval3)
static lv_obj_t* s_uName[4] = {0};   // sous-menu Units : noms uname0..uname3
static lv_obj_t* s_uVal[4]  = {0};   // values uval0..uval2 ([3]=Back with no value)
static lv_obj_t* s_sName[4] = {0};   // sous-menu Sound : sname0..sname2 + Back (dname4_2)
static lv_obj_t* s_sVal[4]  = {0};   // values sval0..sval2 ([3]=Back with no value)
static lv_obj_t* s_vName[4] = {0};   // sous-menu Vario : vname0..vname2 + Back (vname3)
static lv_obj_t* s_vVal[4]  = {0};   // values vval0/vval1/vval2 ([3]=Back with no value)
static lv_obj_t* s_syName[6] = {0};  // sous-menu System : syname0,1,3_,4,5,6 (Back)
static lv_obj_t* s_syVal[6]  = {0};  // syval0 (App connect), syval1 (Condor), reste NULL
static lv_obj_t* s_abName[4] = {0};  // about_list: abname0,1,2,abname5(Back)
static lv_obj_t* s_abVal[4]  = {0};  // abval0,1,2, NULL(Back)
static lv_obj_t* s_glName[10] = {0}; // sous-menu Glider info
static lv_obj_t* s_glVal[10]  = {0};
static lv_obj_t* s_prName[6]  = {0}; // sous-menu Profile (panel EEZ profil_list)
static lv_obj_t* s_prVal[6]   = {0};
char             g_profileName[8] = {0}; // active profile name (shown in prval0 + quick menu)

// Refreshes g_profileName from the current profile's Preferences (g_profileIdx).
// Used by the setup menu (prval0) and the quick menu (val_profil).
void Profile_RefreshName() {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", g_profileIdx);
  Preferences p;
  p.begin(ns, true);
  String n = p.getString("name", "");
  p.end();
  if (n.length() == 0) snprintf(g_profileName, sizeof(g_profileName), "Empty");
  else { strncpy(g_profileName, n.c_str(), sizeof(g_profileName) - 1); g_profileName[sizeof(g_profileName) - 1] = 0; }
}

bool Profile_IsUsed(int idx) {
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", idx);
  Preferences p;
  p.begin(ns, true);
  bool used = p.getString("name", "").length() > 0;
  p.end();
  return used;
}

// Scrolls "Profile" showing ONLY the actually-named profiles (hides the
// "Empty" slots during normal navigation -- "New" stays the only way to
// reach an empty one to name it). If no profile is named yet, it scrolls
// simplement (rien a sauter).
static void Profile_SelectNext(int d) {
  int step = (d > 0) ? 1 : -1;
  int idx  = g_profileIdx;
  bool anyUsed = false;
  for (int i = 0; i < 5; i++) if (Profile_IsUsed(i)) { anyUsed = true; break; }
  if (anyUsed) {
    for (int tries = 0; tries < 5; tries++) {
      idx = (idx + step + 5) % 5;
      if (Profile_IsUsed(idx)) break;
    }
  } else {
    idx = (idx + step + 5) % 5;
  }
  g_profileIdx = idx;
  Profile_Load(g_profileIdx);
}
static lv_obj_t* s_imName[3] = {0}; static lv_obj_t* s_imVal[3] = {0};
static lv_obj_t* s_ibListNames[15] = {0}; static lv_obj_t* s_ibListVals[15] = {0};
static lv_obj_t* s_ciListNames[4] = {0}; static lv_obj_t* s_ciListVals[4] = {0};

// Etat de confirmation (reset config / factory reset)
static int8_t   g_smConfirm  = -1;   // -1=inactif ; SET_RESET_CFG ou SET_FACTORY_RESET
static bool     g_confirmSel = false; // false=Non (defaut, securitaire), true=Oui
static lv_obj_t* s_confirmPanel = NULL;
static lv_obj_t* s_confirmMsg   = NULL;
static lv_obj_t* s_confirmYes   = NULL;
static lv_obj_t* s_confirmNo    = NULL;

// Sends the sound config (pitch/waveform/spread) to the calculator via lim_scfg_t.
// Called on every Sound setting change + once at boot (link established).
static void SoundCfg_Send() { LIM_SCFG_SEND(Serial1, g_tonePitch, g_waveform, g_toneSpread); }

// Sends the command state to the calculator via lim_cmd_t: sink sound + Condor sim
// (combined bitfield, everything resent on each change so as not to overwrite the other state).
static void Cmd_SendState() {
  uint8_t cmd = (g_sinkSound ? LIM_CMD_SINK_SOUND : 0) | (g_condorSim ? LIM_CMD_CONDOR : 0);
  LIM_CMD_SEND(Serial1, cmd);
}

static void SmToggle(uint8_t s) {
  switch (s) {
    case SET_HELPER:     g_helperEnable = !g_helperEnable; break;
    case SET_SINK:       g_sinkSound = !g_sinkSound; Cmd_SendState(); break;
    case SET_LOGGER:     g_loggerEnable = !g_loggerEnable; break;
    case SET_UPDATE:     g_updateMode = !g_updateMode; break;
    case SET_CONDORSIM:  g_condorSim  = !g_condorSim;  Cmd_SendState(); break;   // enables/disables Condor handling on the calculator side
    case SET_APPCONNECT: FlightLog_ServerToggle(); g_updateMode = FlightLog_ServerActive(); break;  // AP WiFi + companion app + OTA
  }
  Config_Save();
}
// Editeur de nom de profil : 5 cases de caractere + cases OK/Cancel, navigation
// 100% encodeur (remplace l'ancien clavier LVGL AZERTY, trop lent a l'encodeur).
static lv_obj_t* s_pnContainer = NULL;
static lv_obj_t* s_pnBox[5]    = {0};   // frames of the 5 slots
static lv_obj_t* s_pnSlot[5]   = {0};   // labels des 5 cases
static lv_obj_t* s_pnOkBox     = NULL;
static lv_obj_t* s_pnCancelBox = NULL;
static lv_obj_t* s_pnWarn      = NULL;  // "Name already exists"
static char      s_pnBuf[6]    = {0};   // 5 caracteres + \0
static int8_t    s_pnCursor    = 0;     // 0..4 = character slot, 5 = OK, 6 = Cancel
static bool      s_pnCharEdit  = false; // true = scrolling the character of the current slot

// Space first = "empty" slot (allows a name < 5 characters).
static const char PN_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
#define PN_CHARSET_LEN ((int)(sizeof(PN_CHARSET) - 1))

static bool ProfileName_IsDuplicate(const char* candidate) {
  for (int i = 0; i < 5; i++) {
    if (i == g_profileIdx) continue;
    char ns[16];
    snprintf(ns, sizeof(ns), "prof_%d", i);
    Preferences p;
    p.begin(ns, true);
    String n = p.getString("name", "");
    p.end();
    if (n.length() > 0 && n.equalsIgnoreCase(candidate)) return true;
  }
  return false;
}

static void ProfileName_Render() {
  for (int i = 0; i < 5; i++) {
    if (!s_pnBox[i]) continue;
    char t[2] = { s_pnBuf[i], 0 };
    lv_label_set_text(s_pnSlot[i], t);
    bool sel     = (s_pnCursor == i);
    bool editing = sel && s_pnCharEdit;
    lv_obj_set_style_border_color(s_pnBox[i],
      lv_color_hex(editing ? 0xfbd500 : (sel ? 0xffffff : 0x666666)), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_pnBox[i], sel ? 3 : 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_pnBox[i],
      lv_color_hex(editing ? 0x3a3000 : 0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (s_pnOkBox) {
    bool sel = (s_pnCursor == 5);
    lv_obj_set_style_border_color(s_pnOkBox, lv_color_hex(sel ? 0xfbd500 : 0x2f8f2f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_pnOkBox, sel ? 3 : 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
  if (s_pnCancelBox) {
    bool sel = (s_pnCursor == 6);
    lv_obj_set_style_border_color(s_pnCancelBox, lv_color_hex(sel ? 0xfbd500 : 0xc0392b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_pnCancelBox, sel ? 3 : 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

static void ProfileName_Close() {
  if (s_pnContainer) { lv_obj_del(s_pnContainer); s_pnContainer = NULL; }
  for (int i = 0; i < 5; i++) { s_pnBox[i] = NULL; s_pnSlot[i] = NULL; }
  s_pnOkBox = NULL; s_pnCancelBox = NULL; s_pnWarn = NULL;
  g_smDirty = true;
}

static void ProfileName_Confirm() {
  char out[6];
  memcpy(out, s_pnBuf, 6);
  for (int i = 4; i >= 0; i--) { if (out[i] == ' ') out[i] = 0; else break; } // trim trailing spaces
  if (strlen(out) == 0) { ProfileName_Close(); return; }  // rien de saisi -> comme Cancel
  if (ProfileName_IsDuplicate(out)) {
    if (s_pnWarn) { lv_label_set_text(s_pnWarn, "Name already exists"); lv_obj_clear_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN); }
    return;  // stay in the editor, nothing saved
  }
  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", g_profileIdx);
  Preferences p;
  p.begin(ns, false);
  p.putString("name", out);
  p.end();
  Profile_Save(g_profileIdx);
  ProfileName_Close();
}

static void Profile_ShowKeyboard(bool isNew = false) {
  if (s_pnContainer) return;
  if (isNew) {
    int found = -1;
    for (int i = 0; i < 5; i++) {
      char ns[16];
      snprintf(ns, sizeof(ns), "prof_%d", i);
      Preferences p;
      p.begin(ns, true);
      String n = p.getString("name", "");
      p.end();
      if (n.length() == 0) { found = i; break; }
    }
    if (found != -1) g_profileIdx = found;
    else g_profileIdx = (g_profileIdx + 1) % 5;
    Profile_Load(g_profileIdx);
  }

  char ns[16];
  snprintf(ns, sizeof(ns), "prof_%d", g_profileIdx);
  Preferences p;
  p.begin(ns, true);
  String existingName = p.getString("name", "");
  p.end();

  char defaultName[16];
  const char* src;
  if (existingName.length() > 0 && !isNew) {
    src = existingName.c_str();
  } else {
    snprintf(defaultName, sizeof(defaultName), "PROF%d", g_profileIdx + 1);
    src = defaultName;
  }
  size_t srcLen = strlen(src);
  for (int i = 0; i < 5; i++) s_pnBuf[i] = ((size_t)i < srcLen) ? (char)toupper((unsigned char)src[i]) : ' ';
  s_pnBuf[5] = 0;
  s_pnCursor   = 0;
  s_pnCharEdit = false;

  s_pnContainer = lv_obj_create(objects.main);
  lv_obj_set_size(s_pnContainer, 480, 480);
  lv_obj_set_pos(s_pnContainer, 0, 0);
  lv_obj_set_style_bg_color(s_pnContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(s_pnContainer, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(s_pnContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(s_pnContainer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(s_pnContainer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_pnContainer);
  lv_label_set_text(title, "Profile name");
  lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 150);

  const int boxW = 50, boxH = 60, gap = 10;
  const int totalW = 5 * boxW + 4 * gap;
  const int startX = (480 - totalW) / 2;
  for (int i = 0; i < 5; i++) {
    lv_obj_t* box = lv_obj_create(s_pnContainer);
    lv_obj_set_size(box, boxW, boxH);
    lv_obj_set_pos(box, startX + i * (boxW + gap), 210);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(box, lv_color_hex(0x666666), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(box, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lbl = lv_label_create(box);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_34, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl);
    s_pnBox[i]  = box;
    s_pnSlot[i] = lbl;
  }

  const int btnW = 90, btnGap = 20;
  const int btnStartX = (480 - (2 * btnW + btnGap)) / 2;

  s_pnOkBox = lv_obj_create(s_pnContainer);
  lv_obj_set_size(s_pnOkBox, btnW, 50);
  lv_obj_set_pos(s_pnOkBox, btnStartX, 300);
  lv_obj_set_style_bg_color(s_pnOkBox, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(s_pnOkBox, lv_color_hex(0x2f8f2f), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(s_pnOkBox, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(s_pnOkBox, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(s_pnOkBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* okLbl = lv_label_create(s_pnOkBox);
  lv_label_set_text(okLbl, "OK");
  lv_obj_set_style_text_color(okLbl, lv_color_hex(0x2f8f2f), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(okLbl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(okLbl);

  s_pnCancelBox = lv_obj_create(s_pnContainer);
  lv_obj_set_size(s_pnCancelBox, btnW, 50);
  lv_obj_set_pos(s_pnCancelBox, btnStartX + btnW + btnGap, 300);
  lv_obj_set_style_bg_color(s_pnCancelBox, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(s_pnCancelBox, lv_color_hex(0xc0392b), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(s_pnCancelBox, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(s_pnCancelBox, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(s_pnCancelBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* cancelLbl = lv_label_create(s_pnCancelBox);
  lv_label_set_text(cancelLbl, "Cancel");
  lv_obj_set_style_text_color(cancelLbl, lv_color_hex(0xc0392b), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(cancelLbl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(cancelLbl);

  s_pnWarn = lv_label_create(s_pnContainer);
  lv_label_set_text(s_pnWarn, "Name already exists");
  lv_obj_set_style_text_color(s_pnWarn, lv_color_hex(0xff4040), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(s_pnWarn, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(s_pnWarn, LV_ALIGN_TOP_MID, 0, 360);
  lv_obj_add_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* hint = lv_label_create(s_pnContainer);
  lv_label_set_text(hint, "Rotate: move / Press: select - letter\nLong press: cancel");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x999999), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 395);

  ProfileName_Render();
}

static void SmAdjust(uint8_t s, long d) {
  switch (s) {
    case SET_BRIGHT: g_brightness += (int)d; if (g_brightness < 0) g_brightness = 0; if (g_brightness > 20) g_brightness = 20; Set_Backlight((uint8_t)(g_brightness * 5)); break;
    case SET_VOLUME: g_volume += (int)d; if (g_volume < 0) g_volume = 0; if (g_volume > 20) g_volume = 20; break;
    case SET_RANGE:  g_varioRange = (g_varioRange == 5) ? 10 : 5;
                     if (screen_main_state.scale) lv_meter_set_scale_range(objects.vario_meter, screen_main_state.scale, -g_varioRange*1000, g_varioRange*1000, 250, 55); break;
    case SET_ROT:    g_screenRot = (g_screenRot + (d > 0 ? 90 : 270)) % 360; break;   // cycle 0/90/180/270 (effet non applique)
    case SET_U_VERT: g_uVert  ^= 1; break;   // m/s <-> kt
    case SET_U_ALT:  g_uAlt   ^= 1; break;   // m   <-> ft
    case SET_U_SPEED:g_uSpeed ^= 1; break;   // km/h <-> kt
    case SET_PITCH:  g_tonePitch += (int)d * 50; if (g_tonePitch < 200) g_tonePitch = 200; if (g_tonePitch > 1500) g_tonePitch = 1500; SoundCfg_Send(); break;
    case SET_WAVE:   g_waveform = (uint8_t)((g_waveform + (d > 0 ? 1 : 2)) % 3); SoundCfg_Send(); break;   // cycle Sine/Square/Triangle
    case SET_SPREAD: g_toneSpread += (int)d; if (g_toneSpread < 0) g_toneSpread = 0; if (g_toneSpread > 10) g_toneSpread = 10; SoundCfg_Send(); break;
    case SET_VFILTER: g_varioFilter = (uint8_t)((g_varioFilter + (d > 0 ? 1 : 2)) % 3); break;   // Fast/Med/Slow
    case SET_VAVG:    g_avgClimb    = (uint8_t)((g_avgClimb    + (d > 0 ? 1 : 2)) % 3); break;   // 15/20/30 s
    case SET_GLIDER_MODEL:
      g_gliderIdx = (g_gliderIdx + (int)d + g_gliderDbCount) % g_gliderDbCount;
      g_gliderEmptyWt = g_gliderDb[g_gliderIdx].empty_wt;
      g_gliderMaxBal  = g_gliderDb[g_gliderIdx].max_bal;
      g_gliderV1      = g_gliderDb[g_gliderIdx].v1;
      g_gliderSi1     = g_gliderDb[g_gliderIdx].si1;
      g_gliderV2      = g_gliderDb[g_gliderIdx].v2;
      g_gliderSi2     = g_gliderDb[g_gliderIdx].si2;
      g_gliderV3      = g_gliderDb[g_gliderIdx].v3;
      g_gliderSi3     = g_gliderDb[g_gliderIdx].si3;
      break;
    case SET_GLIDER_EMPTY_WT: g_gliderEmptyWt += (int)d * 5; if (g_gliderEmptyWt < 100) g_gliderEmptyWt = 100; if (g_gliderEmptyWt > 1000) g_gliderEmptyWt = 1000; break;
    case SET_GLIDER_MAX_BAL:  g_gliderMaxBal  += (int)d * 5; if (g_gliderMaxBal < 0) g_gliderMaxBal = 0; if (g_gliderMaxBal > 500) g_gliderMaxBal = 500; break;
    case SET_GLIDER_V1:       g_gliderV1 += (int)d; if (g_gliderV1 < 40) g_gliderV1 = 40; if (g_gliderV1 > 300) g_gliderV1 = 300; break;
    case SET_GLIDER_SI1:      g_gliderSi1 += (float)d * 0.01f; break;
    case SET_GLIDER_V2:       g_gliderV2 += (int)d; if (g_gliderV2 < 40) g_gliderV2 = 40; if (g_gliderV2 > 300) g_gliderV2 = 300; break;
    case SET_GLIDER_SI2:      g_gliderSi2 += (float)d * 0.01f; break;
    case SET_GLIDER_V3:       g_gliderV3 += (int)d; if (g_gliderV3 < 40) g_gliderV3 = 40; if (g_gliderV3 > 300) g_gliderV3 = 300; break;
    case SET_GLIDER_SI3:      g_gliderSi3 += (float)d * 0.01f; break;
    case SET_PROFILE_SELECT:
      Profile_SelectNext((int)d);
      break;
  }
}
static void SmValTxt(uint8_t s, char* b, int n) {
  switch (s) {
    case SET_HELPER: snprintf(b, n, g_helperEnable ? "ON" : "OFF"); break;
    case SET_SINK:   snprintf(b, n, g_sinkSound   ? "ON" : "OFF"); break;
    case SET_LOGGER: snprintf(b, n, g_loggerEnable ? "ON" : "OFF"); break;
    case SET_BRIGHT: snprintf(b, n, "%d", g_brightness); break;
    case SET_VOLUME: snprintf(b, n, "%d", g_volume); break;
    case SET_RANGE:  snprintf(b, n, "+/-%d", g_varioRange); break;
    case SET_CONDOR: snprintf(b, n, g_gpsOk ? "detected" : "off"); break;
    case SET_ROT:    snprintf(b, n, "%d", g_screenRot); break;
    case SET_U_VERT: snprintf(b, n, g_uVert  ? "kt"   : "m/s");  break;
    case SET_U_ALT:  snprintf(b, n, g_uAlt   ? "ft"   : "m");    break;
    case SET_U_SPEED:snprintf(b, n, g_uSpeed ? "kt"   : "km/h"); break;
    case SET_PITCH:  snprintf(b, n, "%d Hz", g_tonePitch); break;
    case SET_WAVE:   snprintf(b, n, g_waveform == 0 ? "Sine" : (g_waveform == 1 ? "Square" : "Triangle")); break;
    case SET_SPREAD: snprintf(b, n, "%d", g_toneSpread); break;
    case SET_VFILTER:snprintf(b, n, g_varioFilter == 0 ? "Fast" : (g_varioFilter == 1 ? "Med" : "Slow")); break;
    case SET_VAVG:   snprintf(b, n, "%ds", g_avgClimb == 0 ? 15 : (g_avgClimb == 1 ? 20 : 30)); break;
    case SET_UPDATE:    snprintf(b, n, g_updateMode ? "ON" : "OFF"); break;
    case SET_CONDORSIM: snprintf(b, n, g_condorSim  ? "ON" : "OFF"); break;
    case SET_APPCONNECT:snprintf(b, n, FlightLog_ServerActive() ? "ON" : "OFF"); break;
    case SET_FWVER:     snprintf(b, n, "v%s", LIM_FW_SCREEN); break;
    case SET_BUILD:     snprintf(b, n, "%s %s", __DATE__, __TIME__); break;
    case SET_LINKVER:   snprintf(b, n, "v%d", LIM_VERSION); break;
    case SET_CREATOR:   snprintf(b, n, "Mael Loyer"); break;
    case SET_ALGO:      snprintf(b, n, "Kalman & Mahony"); break;
    case SET_GLIDER_MODEL:   snprintf(b, n, "%s", g_gliderDb[g_gliderIdx].name); break;
    case SET_GLIDER_EMPTY_WT:snprintf(b, n, "%d kg", g_gliderEmptyWt); break;
    case SET_GLIDER_MAX_BAL: snprintf(b, n, "%d kg", g_gliderMaxBal); break;
    case SET_GLIDER_V1:      snprintf(b, n, "%d km/h", g_gliderV1); break;
    case SET_GLIDER_SI1:     snprintf(b, n, "%.2f m/s", g_gliderSi1); break;
    case SET_GLIDER_V2:      snprintf(b, n, "%d km/h", g_gliderV2); break;
    case SET_GLIDER_SI2:     snprintf(b, n, "%.2f m/s", g_gliderSi2); break;
    case SET_GLIDER_V3:      snprintf(b, n, "%d km/h", g_gliderV3); break;
    case SET_GLIDER_SI3:     snprintf(b, n, "%.2f m/s", g_gliderSi3); break;
    case SET_PROFILE_SELECT: snprintf(b, n, "Profile %d", g_profileIdx + 1); break;
    default: b[0] = 0;
  }
}

// ============================================================
//  INTERACTIVE INFOBOX EDITOR (OVER THE VARIO BACKGROUND)
// ============================================================
// Temporary serial breadcrumbs (bisection of the 1-2 July 2026 freeze,
// same method as for the previous LVGL freeze). Remove once located.
#define IBDBG(...) do { Serial.printf(__VA_ARGS__); Serial.flush(); } while (0)
static bool g_ibJustRendered = false;  // breadcrumb: true right after a SetupMenu_RenderList

static void InfoBox_RenderSelect() {
  IBDBG("[IB] RenderSelect zone=%d\n", s_ibZoneSel);
  for (int i = 0; i < 7; i++) {
    if (!s_ibFrames[i]) continue;
    if (i == s_ibZoneSel) {
      lv_obj_set_style_border_color(s_ibFrames[i], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_border_width(s_ibFrames[i], 3, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_color(s_ibFrames[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_opa(s_ibFrames[i], 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      lv_obj_set_style_border_color(s_ibFrames[i], lv_color_hex(0x004cc0), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_border_width(s_ibFrames[i], 3, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_opa(s_ibFrames[i], LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (s_ibValLabels[i]) {
      if (i == 2) {
        int cIdx = g_ibEditCruiseMode ? g_centerConfigCruise : g_centerConfigClimb;
        if (cIdx >= CENTER_METRIC_MAX) cIdx = CENTER_EMPTY;
        lv_label_set_text(s_ibValLabels[i], s_centerMetricAbbrev[cIdx]);
      } else {
        int mIdx = g_infoBoxConfig[i];
        if (mIdx >= IB_METRIC_MAX) mIdx = IB_EMPTY;
        lv_label_set_text(s_ibValLabels[i], s_ibMetricAbbrev[mIdx]);
      }
      // Center the label (EEZ position = corner, not center) on its frame, whatever
      // the length of the displayed text (existing objects -> no freeze risk).
      if (s_ibFrames[i]) lv_obj_align_to(s_ibValLabels[i], s_ibFrames[i], LV_ALIGN_CENTER, 0, 0);
    }
  }
}

static void InfoBox_ShowSelect() {
  IBDBG("[IB] ShowSelect enter\n");
  g_ibEditState = IBEDIT_SELECT_ZONE;
  if (objects.setup_panel) lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_editor_container) lv_obj_clear_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  InfoBox_RenderSelect();
  IBDBG("[IB] ShowSelect done\n");
}

static void InfoBox_CloseEdit() {
  IBDBG("[IB] CloseEdit enter\n");
  g_ibEditState = IBEDIT_NONE;
  if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  if (objects.setup_panel) lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
  if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
  else { g_smMenu = SM_ROOT; }
  g_smDirty = true;
  IBDBG("[IB] CloseEdit done\n");
}

static void SetupMenu_Open()  { g_setupOpen = true; g_menuState = MENU_CLOSED; g_menuDirty = true;
                                g_smMenu = SM_ROOT; g_smSel = 0; g_smDepth = 0; g_smEdit = false; g_smDirty = true; }
static void SetupMenu_Close() {
  if (g_smConfirm != -1) { g_smConfirm = -1; lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN); }
  if (g_ibEditState != IBEDIT_NONE) { InfoBox_CloseEdit(); }
  // Profile name editor (New/Save/Edit): without this, a long-press while typing
  // would close the whole setup, leaving the editor shown and unreachable forever
  // (2 July 2026 - no code could delete it any more once g_setupOpen=false).
  if (s_pnContainer) ProfileName_Close();  // cancel without saving (as before with the keyboard)
  g_setupOpen = false; g_smEdit = false; g_smDirty = true; Config_Save();
}
static void SetupMenu_Back()  {
  if (g_smConfirm != -1) { g_smConfirm = -1; lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN); g_smDirty = true; return; }
  if (g_smMenu == SM_INFOBOX_METRIC || g_ibEditState == IBEDIT_CHOOSE_METRIC) {
    IBDBG("[IB] Back: METRIC -> SELECT_ZONE\n");
    g_ibEditState = IBEDIT_SELECT_ZONE;
    if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
    InfoBox_ShowSelect();
    return;
  }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) {
    IBDBG("[IB] Back: SELECT_ZONE -> CloseEdit\n");
    InfoBox_CloseEdit();
    return;
  }
  if (g_smEdit) { g_smEdit = false; Config_Save(); }
  else if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
  else SetupMenu_Close();
  g_smDirty = true;
}

// Updates the popup colors according to the selection:
//   Yes selected -> Yes=YELLOW, No=default white, frame on Yes
//   No selected  -> Yes=default white, No=YELLOW, frame on No
static void Confirm_Render() {
  if (!s_confirmYes || !s_confirmNo) return;
  if (g_confirmSel) {   // Oui selectionne
    lv_obj_set_style_text_color(s_confirmYes, lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_confirmNo,  lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (objects.confirm_panel_selection) lv_obj_set_x(objects.confirm_panel_selection, 47);
  } else {              // Non selectionne
    lv_obj_set_style_text_color(s_confirmYes, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_confirmNo,  lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (objects.confirm_panel_selection) lv_obj_set_x(objects.confirm_panel_selection, 195);
  }
}
static void Confirm_Show(int8_t action) {
  g_smConfirm  = action;
  g_confirmSel = false;   // securite : Non par defaut
  Confirm_Render();
  if (s_confirmMsg) {
    if (action == SET_RESET_CFG)      lv_label_set_text(s_confirmMsg, "Reset config?");
    else if (action == SET_FACTORY_RESET) lv_label_set_text(s_confirmMsg, "Factory reset?");
    else if (action == SET_PROFILE_DELETE) lv_label_set_text(s_confirmMsg, "Delete profile?");
  }
  lv_obj_clear_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_confirmPanel);   // bring to foreground
}
static void Confirm_Hide() {
  g_smConfirm = -1;
  if (s_confirmPanel) lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);
  g_smDirty = true;
}
static void SetupMenu_Rotate(long d) {
  if (s_pnContainer) {
    if (s_pnWarn) lv_obj_add_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN);
    if (s_pnCharEdit) {
      int idx = -1;
      for (int k = 0; k < PN_CHARSET_LEN; k++) if (PN_CHARSET[k] == s_pnBuf[s_pnCursor]) { idx = k; break; }
      idx = ((idx + (int)d) % PN_CHARSET_LEN + PN_CHARSET_LEN) % PN_CHARSET_LEN;
      s_pnBuf[s_pnCursor] = PN_CHARSET[idx];
    } else {
      int i = (int)s_pnCursor + (int)d;
      if (i < 0) i = 0;
      if (i > 6) i = 6;
      s_pnCursor = (int8_t)i;
    }
    ProfileName_Render();
    return;
  }
  if (g_smConfirm != -1) { g_confirmSel = !g_confirmSel; Confirm_Render(); return; }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) {
    // Rotation order: 0,1,2,3,4,5,6 (zone 5 = status pod, activated 19 July 2026;
    // 6 = "Back", added 2 July 2026 under ib_frame_6, excluded from g_infoBoxConfig).
    static const int IB_ZONE_SEQ[] = {0, 1, 2, 3, 4, 5, 6};
    const int IB_ZONE_SEQ_N = 7;
    int pos = 0;
    for (int k = 0; k < IB_ZONE_SEQ_N; k++) if (IB_ZONE_SEQ[k] == s_ibZoneSel) { pos = k; break; }
    pos = ((pos + (int)d) % IB_ZONE_SEQ_N + IB_ZONE_SEQ_N) % IB_ZONE_SEQ_N;
    s_ibZoneSel = IB_ZONE_SEQ[pos];
    IBDBG("[IB] Rotate zone->%d\n", s_ibZoneSel);
    InfoBox_RenderSelect();
    return;
  }
  if (g_smEdit) SmAdjust(SM[g_smMenu].items[g_smSel].arg, d);
  else {
    int maxN = (g_smMenu == SM_INFOBOX_METRIC && s_ibZoneSel == 2) ? 4 : SM[g_smMenu].n;
    int i = (int)g_smSel + (int)d;
    if (i < 0) i = 0;
    if (i >= maxN) i = maxN - 1;
    g_smSel = (int8_t)i;
  }
  g_smDirty = true;
}
static void SetupMenu_Press() {
  if (s_pnContainer) {
    if (s_pnWarn) lv_obj_add_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN);
    if (s_pnCursor == 5) { ProfileName_Confirm(); return; }
    if (s_pnCursor == 6) { ProfileName_Close(); return; }
    s_pnCharEdit = !s_pnCharEdit;
    ProfileName_Render();
    return;
  }
  if (g_smConfirm != -1) {
    if (g_confirmSel) {
      if (g_smConfirm == (int8_t)SET_RESET_CFG) {
        prefs.begin("limvario", false);
        prefs.clear();
        prefs.end();
        g_brightness = 20; g_helperEnable = true; g_loggerEnable = true;
        g_varioRange = 5; g_screenRot = 0; g_uVert = 0; g_uAlt = 0; g_uSpeed = 0;
        g_tonePitch = 700; g_waveform = 0; g_toneSpread = 5;
        g_varioFilter = 1; g_avgClimb = 1; g_updateMode = false; g_condorSim = false;
        Set_Backlight((uint8_t)(g_brightness * 5));
        SoundCfg_Send();
        Config_Save();
      } 
      else if (g_smConfirm == (int8_t)SET_FACTORY_RESET) {
        prefs.begin("limvario", false);
        prefs.clear();
        prefs.end();
        ESP.restart();
      }
      else if (g_smConfirm == (int8_t)SET_PROFILE_DELETE) {
        Profile_Delete(g_profileIdx);
      }
    }
    Confirm_Hide();
    return;
  }
  if (g_smMenu == SM_INFOBOX && g_ibEditState == IBEDIT_NONE) {
    IBDBG("[IB] Press SM_INFOBOX sel=%d\n", g_smSel);
    if (g_smSel == 2) {
      SetupMenu_Back();
    } else {
      g_ibEditCruiseMode = (g_smSel == 1);
      g_infoBoxConfig = g_ibEditCruiseMode ? g_ibConfigCruise : g_ibConfigClimb;
      InfoBox_ShowSelect();
    }
    return;
  }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) {
    if (s_ibZoneSel == 6) {   // "Back" (ib_frame_6): closes the editor without choosing a zone
      InfoBox_CloseEdit();
      return;
    }
    IBDBG("[IB] Press SELECT_ZONE zone=%d -> CHOOSE_METRIC\n", s_ibZoneSel);
    g_ibEditState = IBEDIT_CHOOSE_METRIC;
    if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
    if (objects.setup_panel) lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
    g_smStk[g_smDepth] = g_smMenu; g_smStkSel[g_smDepth] = g_smSel; g_smDepth++;
    g_smMenu = SM_INFOBOX_METRIC;
    int curVal = (s_ibZoneSel == 2) ? (g_ibEditCruiseMode ? g_centerConfigCruise : g_centerConfigClimb) : g_infoBoxConfig[s_ibZoneSel];
    if (s_ibZoneSel == 2) {
      // Centre : CI_LIST, enum == index directement (0,1,2), Back=3.
      g_smSel = (curVal >= 0 && curVal < 3) ? curVal : 0;
    } else {
      // Metric list: g_infoBoxConfig stores the ACTUAL enum value -> find
      // the IBIT_LIST index whose .arg matches (the mapping is no longer 1:1).
      const SmMenu* mm = &SM[SM_INFOBOX_METRIC];
      int found = 0;
      for (int k = 0; k < mm->n; k++) {
        if (mm->items[k].type == ST_INFO && mm->items[k].arg == curVal) { found = k; break; }
      }
      g_smSel = (int8_t)found;
    }
    g_smDirty = true;
    IBDBG("[IB] curVal=%d g_smSel=%d\n", curVal, g_smSel);
    return;
  }
  if (g_smMenu == SM_INFOBOX_METRIC) {
    IBDBG("[IB] Press SM_INFOBOX_METRIC sel=%d zone=%d\n", g_smSel, s_ibZoneSel);
    int maxIdx = (s_ibZoneSel == 2) ? 3 : (SM[SM_INFOBOX_METRIC].n - 1);
    if (g_smSel == maxIdx) {
      SetupMenu_Back();
      return;
    }
    if (s_ibZoneSel == 2) {
      if (g_ibEditCruiseMode) g_centerConfigCruise = (uint8_t)g_smSel;
      else g_centerConfigClimb = (uint8_t)g_smSel;
    } else {
      // Writes the ACTUAL enum value (it->arg), not the raw list index.
      g_infoBoxConfig[s_ibZoneSel] = (uint8_t)SM[SM_INFOBOX_METRIC].items[g_smSel].arg;
    }
    IBDBG("[IB] before Config_Save\n");
    Config_Save();
    IBDBG("[IB] before SetupMenu_Back\n");
    SetupMenu_Back();
    IBDBG("[IB] after SetupMenu_Back\n");
    return;
  }
  const SmItem* it = &SM[g_smMenu].items[g_smSel];
  if (g_smEdit) { g_smEdit = false; Config_Save(); }
  else switch (it->type) {
    case ST_SUB:    g_smStk[g_smDepth] = g_smMenu; g_smStkSel[g_smDepth] = g_smSel; g_smDepth++;
                    g_smMenu = it->arg; g_smSel = 0; break;
    case ST_BACK:   SetupMenu_Back(); break;
    case ST_TOGGLE: SmToggle(it->arg); break;
    case ST_VALUE:
    case ST_CHOICE: g_smEdit = true; break;
    case ST_INFO:
      if (it->arg == SET_PROFILE_SAVE || it->arg == SET_PROFILE_NEW || it->arg == SET_PROFILE_EDIT) {
        Profile_ShowKeyboard(it->arg == SET_PROFILE_NEW);
        return;
      }
      if (it->arg == SET_RESET_CFG || it->arg == SET_FACTORY_RESET || it->arg == SET_PROFILE_DELETE) {
        Confirm_Show((int8_t)it->arg);
        return;
      }
      break;
  }
  g_smDirty = true;
}

// Vertical position (in the panel) of the 5 slots; slot 2 = center = under the bar.
static const int SM_ROW_Y[5] = { 88, 143, 198, 253, 308 };

// Prepares the 5 slots: large font, horizontally centered, panel hidden.
static void SetupMenu_Init()
{
  lv_obj_t* slots[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  for (int i = 0; i < 7; i++) {
    lv_obj_set_size(slots[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(slots[i], &lv_font_montserrat_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(slots[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    // paired "value" label, RIGHT-aligned (filled by SetupMenu_Apply)
    s_smVal[i] = lv_label_create(objects.setup_panel);
    lv_obj_set_style_text_font(s_smVal[i], &lv_font_montserrat_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_smVal[i], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_smVal[i], "");
  }
  for (int i = 0; i < 5; i++) {
    lv_obj_align(slots[i], LV_ALIGN_TOP_MID, 0, SM_ROW_Y[i]);
    lv_obj_align(s_smVal[i], LV_ALIGN_TOP_RIGHT, -50, SM_ROW_Y[i]);
  }
  lv_obj_align(objects.item5, LV_ALIGN_TOP_MID, 0, 307);
  lv_obj_align(s_smVal[4], LV_ALIGN_TOP_RIGHT, -50, 307);
  lv_obj_align(objects.item6, LV_ALIGN_TOP_MID, 0, 362);
  lv_obj_align(s_smVal[5], LV_ALIGN_TOP_RIGHT, -50, 362);
  lv_obj_align(objects.item4, LV_ALIGN_TOP_MID, 0, 417);
  lv_obj_align(s_smVal[6], LV_ALIGN_TOP_RIGHT, -50, 417);
  // No scroll (the wide bar overflows -> otherwise scrollbars appear).
  lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(objects.setup_panel, LV_SCROLLBAR_MODE_OFF);

  // Selection bar: exactly as defined in EEZ (pos -31,193, size 500x53,
  // semi-transparent yellow outline). The code no longer touches it (position, size or style),
  // it stays FIXED for all lists.

  // --- Display submenu (display_list hand-built in EEZ) ---
  // Shortcut arrays (the code does NOT touch the name text, written in EEZ).
  s_dName[0] = objects.dname0; s_dName[1] = objects.dname1; s_dName[2] = objects.dname2;
  s_dName[3] = objects.dname3; s_dName[4] = objects.dname4;
  s_dVal[2] = objects.dval2; s_dVal[3] = objects.dval3;
  // Quick-menu-style scrolling (item_list): the frame stays FIXED, the list slides.
  // Snap off, no scrollbar, large top/bottom padding to center the first/last line
  // in the frame. The padding shifts the list but the scroll re-centers -> invisible.
  lv_obj_set_scrollbar_mode(objects.display_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.display_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.display_list, LV_OBJ_FLAG_HIDDEN);

  // --- Units submenu (units_list hand-built in EEZ) ---
  s_uName[0] = objects.uname0; s_uName[1] = objects.uname1; s_uName[2] = objects.uname2; s_uName[3] = objects.uname4;  // Back
  s_uVal[0] = objects.uval0; s_uVal[1] = objects.uval1; s_uVal[2] = objects.uval2;
  lv_obj_set_scrollbar_mode(objects.units_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.units_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.units_list, LV_OBJ_FLAG_HIDDEN);

  // --- Sound submenu (sound_list hand-built in EEZ) ---
  s_sName[0] = objects.sname0; s_sName[1] = objects.sname1; s_sName[2] = objects.sname2; s_sName[3] = objects.sname4;  // Back
  s_sVal[0] = objects.sval0; s_sVal[1] = objects.sval1; s_sVal[2] = objects.sval2;
  lv_obj_set_scrollbar_mode(objects.sound_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.sound_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.sound_list, LV_OBJ_FLAG_HIDDEN);

  // --- Sous-menu Vario (vario_list nomme dans EEZ) ---
  s_vName[0] = objects.vname0; s_vName[1] = objects.vname1; s_vName[2] = objects.vname2; s_vName[3] = objects.vname3;  // Back
  s_vVal[0] = objects.vval0; s_vVal[1] = objects.vval1; s_vVal[2] = objects.vval2;
  lv_obj_set_scrollbar_mode(objects.vario_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.vario_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.vario_list, LV_OBJ_FLAG_HIDDEN);

  // --- System submenu (system_list hand-built in EEZ) ---
  // Ordre EEZ exact (position Y croissante): syname0=App connect, syname1=Condor sim,
  // syname3_=Reset config, syname4=Factory reset, syname5=About, syname6=Back
  s_syName[0] = objects.syname0;  s_syName[1] = objects.syname1;
  s_syName[2] = objects.syname3_; s_syName[3] = objects.syname4;
  s_syName[4] = objects.syname5;  s_syName[5] = objects.syname6;  // Back (rouge dans EEZ)
  s_syVal[0] = objects.syval0; s_syVal[1] = objects.syval1;
  lv_obj_set_scrollbar_mode(objects.system_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.system_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.system_list, LV_OBJ_FLAG_HIDDEN);

  // --- Sous-menu About (about_list, 4 items: Version, Build, Link Prot., Back) ---
  s_abName[0] = objects.abname0; s_abName[1] = objects.abname1;
  s_abName[2] = objects.abname2; s_abName[3] = objects.abname5;  // Back
  s_abVal[0] = objects.abval0; s_abVal[1] = objects.abval1; s_abVal[2] = objects.abval2;
  lv_obj_set_scrollbar_mode(objects.about_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.about_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.about_list, LV_OBJ_FLAG_HIDDEN);

  // --- Sous-menu Glider info (glider_list) ---
  s_glName[0] = objects.glname0; s_glVal[0] = objects.glval0;
  s_glName[1] = objects.glname1; s_glVal[1] = objects.glval1;
  s_glName[2] = objects.glname2; s_glVal[2] = objects.glval2;
  s_glName[3] = objects.glname3; s_glVal[3] = objects.glval3;
  s_glName[4] = objects.glname4; s_glVal[4] = objects.glval4;
  s_glName[5] = objects.glname5; s_glVal[5] = objects.glval5;
  s_glName[6] = objects.glname6; s_glVal[6] = objects.glval6;
  s_glName[7] = objects.glname7; s_glVal[7] = objects.glval7;
  s_glName[8] = objects.glname8; s_glVal[8] = objects.glval8;
  s_glName[9] = objects.abname5_1; s_glVal[9] = NULL;  // Back
  lv_obj_set_scrollbar_mode(objects.glider_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.glider_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.glider_list, LV_OBJ_FLAG_HIDDEN);

  // --- Sous-menu Profile (profil_list EEZ) ---
  s_prName[0] = objects.prname0; s_prVal[0] = objects.prval0;  // "Profil" + nom actuel
  s_prName[1] = objects.prname1; s_prVal[1] = NULL;            // "Edit"
  s_prName[2] = objects.prname2; s_prVal[2] = NULL;            // "New"
  s_prName[3] = objects.prname3; s_prVal[3] = NULL;            // "Save"
  s_prName[4] = objects.prname4; s_prVal[4] = NULL;            // "Delete"
  s_prName[5] = objects.prname5; s_prVal[5] = NULL;            // "Back"
  lv_obj_set_scrollbar_mode(objects.profil_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.profil_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.profil_list, LV_OBJ_FLAG_HIDDEN);

  // --- QR code screen (WiFi sharing "App connect", see QrScreen_*) ---
  if (objects.qr_panel) lv_obj_add_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);

  // --- Editeur Info boxes construit en EEZ ---
  if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_mode_list)        lv_obj_add_flag(objects.infobox_mode_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_list)             lv_obj_add_flag(objects.infobox_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.center_info_list)         lv_obj_add_flag(objects.center_info_list, LV_OBJ_FLAG_HIDDEN);

  s_ibFrames[0] = objects.ib_frame_0;
  s_ibFrames[1] = objects.ib_frame_1;
  s_ibFrames[2] = objects.ib_frame_2;
  s_ibFrames[3] = objects.ib_frame_3;
  s_ibFrames[4] = objects.ib_frame_4;
  s_ibFrames[5] = objects.ib_frame_5;
  s_ibFrames[6] = objects.ib_frame_6;   // "Back" (ajoute 2 juillet 2026)

  s_ibValLabels[0] = objects.ib_val_0;
  s_ibValLabels[1] = objects.ib_val_1;
  s_ibValLabels[2] = objects.ib_val_2;
  s_ibValLabels[3] = objects.ib_val_3;
  s_ibValLabels[4] = objects.ib_val_4;
  // Zone 5 (status pod on the right): ib_val_5 built in EEZ on 19 July 2026
  // -> zone ACTIVE in the editor (added to IB_ZONE_SEQ).
  // NB: its DISPLAY label (s_ibLabels[5], on the main screen) does not yet exist
  // in EEZ -> the metric is selectable but nothing shows in flight.
  // The Labels_Apply loop cleanly skips a zone with no label (test !s_ibLabels[i]).
  // DO NOT create this label on the fly here: that is the pattern (lv_label_create
  // outside EEZ) that caused the "zone 1" freeze of 1 July 2026.
  s_ibValLabels[5] = objects.ib_val_5;

  s_imName[0] = objects.imname0; s_imName[1] = objects.imname1; s_imName[2] = objects.imname2;

  s_ibListNames[0]  = objects.ibname0;
  s_ibListNames[1]  = objects.ibname1;
  s_ibListNames[2]  = objects.ibname2;
  s_ibListNames[3]  = objects.ibname3;
  s_ibListNames[4]  = objects.ibname4;
  s_ibListNames[5]  = objects.ibname5;
  s_ibListNames[6]  = objects.ibname6;
  s_ibListNames[7]  = objects.ibname7;
  s_ibListNames[8]  = objects.ibname8;
  s_ibListNames[9]  = objects.ibname9;
  s_ibListNames[10] = objects.ibname10;
  s_ibListNames[11] = objects.ibname11;  // "Airspeed"
  s_ibListNames[12] = objects.ibname13;  // "Ground Speed"
  s_ibListNames[13] = objects.ibname14;  // "Disabled"
  s_ibListNames[14] = objects.ibname15;  // "Back"

  s_ciListNames[0] = objects.cname0;
  s_ciListNames[1] = objects.cname1;
  s_ciListNames[2] = objects.cname2;
  s_ciListNames[3] = objects.prname5_1;

  if (objects.infobox_mode_list) {
    lv_obj_set_scrollbar_mode(objects.infobox_mode_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(objects.infobox_mode_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  }
  if (objects.infobox_list) {
    lv_obj_set_scrollbar_mode(objects.infobox_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(objects.infobox_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  }
  if (objects.center_info_list) {
    lv_obj_set_scrollbar_mode(objects.center_info_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(objects.center_info_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  }

  Set_Backlight((uint8_t)(g_brightness * 5));

  // --- Popup de confirmation : couplee aux vrais widgets EEZ ---
  s_confirmPanel = objects.confirm_panel;
  s_confirmMsg   = objects.confirm_msg;
  s_confirmYes   = objects.confirm_yes;
  s_confirmNo    = objects.confirm_no;
  lv_obj_set_scrollbar_mode(s_confirmPanel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_bg_opa(objects.setup_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
}

// Hides all EEZ submenu containers (display/units/sound). The active one is re-shown after.
static void SetupMenu_HideLists()
{
  if (objects.display_list)             lv_obj_add_flag(objects.display_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.units_list)               lv_obj_add_flag(objects.units_list,   LV_OBJ_FLAG_HIDDEN);
  if (objects.sound_list)               lv_obj_add_flag(objects.sound_list,   LV_OBJ_FLAG_HIDDEN);
  if (objects.vario_list)               lv_obj_add_flag(objects.vario_list,   LV_OBJ_FLAG_HIDDEN);
  if (objects.system_list)              lv_obj_add_flag(objects.system_list,  LV_OBJ_FLAG_HIDDEN);
  if (objects.about_list)               lv_obj_add_flag(objects.about_list,   LV_OBJ_FLAG_HIDDEN);
  if (objects.glider_list)              lv_obj_add_flag(objects.glider_list,  LV_OBJ_FLAG_HIDDEN);
  if (objects.profil_list)              lv_obj_add_flag(objects.profil_list,  LV_OBJ_FLAG_HIDDEN);
  if (objects.center_info_list)         lv_obj_add_flag(objects.center_info_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_list)             lv_obj_add_flag(objects.infobox_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_mode_list)        lv_obj_add_flag(objects.infobox_mode_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
}

static void SetupMenu_ApplyItemZoom(lv_obj_t* obj, lv_coord_t cy, lv_coord_t frame_cy) {
  if (!obj) return;
  lv_obj_set_style_transform_zoom(obj, 256, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// EEZ positions of the root menu items (item0..item4), kept as-is.
static const lv_coord_t ROOT_BX[7] = { 145, 154, 168, 145, 117, 155, 181 };
static const lv_coord_t ROOT_BY[7] = {  87, 142, 197, 252, 307, 362, 417 };

// Rendu du menu racine "Settings" : item0..4 gardent leurs textes/positions EEZ
// (order Display/Sound/Vario/System/Exit), the list SCROLLS as a group to center the
// selected in the fixed frame, and items outside the setup_frame frame disappear.
static void SetupMenu_RenderRoot()
{
  IBDBG("[IB] RenderRoot enter smSel=%d\n", g_smSel);
  SetupMenu_HideLists();
  lv_obj_t* it[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  const SmMenu* m = &SM[SM_ROOT];
  for (int i = 0; i < 7; i++) {
    lv_label_set_text(it[i], m->items[i].label);
    lv_obj_set_style_text_color(it[i], lv_color_hex(m->items[i].type == ST_BACK ? 0xff0000 : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(it[i], LV_ALIGN_TOP_MID, 0, ROOT_BY[i]);
    lv_obj_clear_flag(it[i], LV_OBJ_FLAG_HIDDEN);
    if (s_smVal[i]) lv_obj_add_flag(s_smVal[i], LV_OBJ_FLAG_HIDDEN);   // racine = pas de valeurs
  }
  // Group scrolling: brings the selected item into the fixed frame (like Display).
  lv_obj_update_layout(objects.main);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame_1, &fa);
  lv_obj_get_coords(it[g_smSel], &la);
  lv_coord_t delta = ((la.y1 + la.y2) / 2) - ((fa.y1 + fa.y2) / 2);
  for (int i = 0; i < 7; i++) lv_obj_align(it[i], LV_ALIGN_TOP_MID, 0, ROOT_BY[i] - delta);
  // Hide items: at the TOP a bit before the title frame (setup_frame.y1),
  // at the BOTTOM only at the round edge of the screen (objects.main).
  lv_obj_update_layout(objects.main);
  lv_area_t sf, mn; lv_obj_get_coords(objects.setup_frame, &sf); lv_obj_get_coords(objects.main, &mn);
  lv_coord_t topY = 85, botY = 460;
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  for (int i = 0; i < 7; i++) {
    lv_obj_get_coords(it[i], &la);
    lv_coord_t cy = (la.y1 + la.y2) / 2;
    if (cy < topY || cy > botY) {
      lv_obj_add_flag(it[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      SetupMenu_ApplyItemZoom(it[i], cy, frame_cy);
    }
  }
  IBDBG("[IB] RenderRoot done\n");
  g_ibJustRendered = true;
}

// Rendu GENERIQUE d'un sous-menu EEZ (conteneur a positions absolues).
// Items are placed at Y = ITEM_Y0 + i*ITEM_STEP in the container (absolute pos in EEZ).
// We move the WHOLE CONTAINER in Y to center the selected item in the fixed frame.
// Pas de scroll_by (cumul de drift) : on utilise lv_obj_set_y directement.
//   ITEM_Y0  = Y position of the first item in the container (108 in all our EEZ menus)
//   ITEM_STEP = vertical step between items (55 px in all our EEZ menus)
//   container_base_y = position Y native du conteneur (tiree de EEZ, -18 ou -21)
static const lv_coord_t EEZ_ITEM_Y0   = 108;   // Y of the first item in the container
static const lv_coord_t EEZ_ITEM_STEP = 55;    // pas entre items

static void SetupMenu_RenderList(lv_obj_t* container, lv_obj_t** names, lv_obj_t** vals,
                                  const SmMenu* m)
{
  IBDBG("[IB] RenderList container=%p n=%d smSel=%d\n", (void*)container, m->n, g_smSel);
  // Hides the generic slots + all other containers; shows this one
  lv_obj_t* slots[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  for (int i = 0; i < 7; i++) {
    lv_obj_add_flag(slots[i], LV_OBJ_FLAG_HIDDEN);
    if (s_smVal[i]) lv_obj_add_flag(s_smVal[i], LV_OBJ_FLAG_HIDDEN);
  }
  SetupMenu_HideLists();
  if (!container) { IBDBG("[IB] RenderList: container NULL, abort\n"); return; }
  lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);
  IBDBG("[IB] RenderList: container shown, filling items\n");

  int n = m->n;
  char v[20];
  for (int i = 0; i < n; i++) {
    if (!names[i]) continue;
    lv_obj_clear_flag(names[i], LV_OBJ_FLAG_HIDDEN);
    if (vals[i]) lv_obj_clear_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
    const SmItem* it = &m->items[i];
    // Name color: red for Back, white otherwise
    lv_obj_set_style_text_color(names[i],
      lv_color_hex(it->type == ST_BACK ? 0xff0000 : 0xffffff),
      LV_PART_MAIN | LV_STATE_DEFAULT);
    if (vals[i]) {
      if (g_smMenu != SM_ABOUT && (it->type == ST_VALUE || it->type == ST_CHOICE || it->type == ST_TOGGLE || it->type == ST_INFO)) {
        SmValTxt(it->arg, v, sizeof(v));
        lv_label_set_text(vals[i], v);
      }
      // Toggle ON -> jaune ; en edition -> jaune ; sinon blanc
      bool isOn   = (it->type == ST_TOGGLE && strcmp(v, "ON") == 0);
      bool isEdit = (g_smEdit && i == (int)g_smSel);
      lv_obj_set_style_text_color(vals[i],
        lv_color_hex((isEdit || isOn) ? 0xfbd500 : 0xffffff),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }

  IBDBG("[IB] RenderList: items filled, centering\n");
  // Centering: we measure the real coordinates to compute the exact delta.
  lv_obj_update_layout(objects.main);
  lv_area_t fa, ia;
  lv_obj_get_coords(objects.selection_frame_1, &fa);
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  if (names[g_smSel]) {
    lv_obj_get_coords(names[g_smSel], &ia);
    lv_coord_t item_cy = (ia.y1 + ia.y2) / 2;
    lv_coord_t delta   = item_cy - frame_cy;
    if (delta != 0) lv_obj_set_y(container, lv_obj_get_y(container) - delta);
  }

  IBDBG("[IB] RenderList: centering done, masking\n");
  // Masking: visible only between the bottom of the title and the screen edge.
  lv_obj_update_layout(objects.main);
  lv_coord_t topY = 85, botY = 460;
  lv_area_t la;
  for (int i = 0; i < n; i++) {
    if (!names[i]) continue;
    lv_obj_get_coords(names[i], &la);
    lv_coord_t cy = (la.y1 + la.y2) / 2;
    bool vis = (cy >= topY && cy <= botY);
    if (vis) {
      lv_obj_clear_flag(names[i], LV_OBJ_FLAG_HIDDEN);
      SetupMenu_ApplyItemZoom(names[i], cy, frame_cy);
    } else {
      lv_obj_add_flag(names[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (vals[i]) {
      if (vis) {
        lv_obj_clear_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
        SetupMenu_ApplyItemZoom(vals[i], cy, frame_cy);
      } else {
        lv_obj_add_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  IBDBG("[IB] RenderList: masking done, return\n");
  g_ibJustRendered = true;
}

// "Centered selection" rendering: the current item is always in slot 2 (center,
// under the bar). Neighbors fill the slots above/below; beyond
// the menu -> slot hidden. Called every loop, does nothing if nothing changed.
static void SetupMenu_Apply()
{
  if (!g_smDirty) return;
  g_smDirty = false;
  if (!g_setupOpen) {
    lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
    if (objects.vario_meter)               lv_obj_clear_flag(objects.vario_meter, LV_OBJ_FLAG_HIDDEN);
    if (objects.infobox_display_container) lv_obj_clear_flag(objects.infobox_display_container, LV_OBJ_FLAG_HIDDEN);
    if (objects.img_gps)                   lv_obj_clear_flag(objects.img_gps, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
  if (objects.quick_menu_panel)          lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
  if (objects.vario_meter)               lv_obj_add_flag(objects.vario_meter, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_display_container) lv_obj_add_flag(objects.infobox_display_container, LV_OBJ_FLAG_HIDDEN);
  if (objects.img_gps)                   lv_obj_add_flag(objects.img_gps, LV_OBJ_FLAG_HIDDEN);

  const SmMenu* m = &SM[g_smMenu];
  lv_label_set_text(objects.settings, m->title);
  // Display = list hand-built in EEZ; the other menus = generic slots item0..4
  if (g_smMenu == SM_ROOT)    { SetupMenu_RenderRoot(); return; }
  if (g_smMenu == SM_DISPLAY) { SetupMenu_RenderList(objects.display_list, s_dName, s_dVal, m); return; }
  if (g_smMenu == SM_UNITS)   { SetupMenu_RenderList(objects.units_list,   s_uName, s_uVal, m); return; }
  if (g_smMenu == SM_SOUND)   { SetupMenu_RenderList(objects.sound_list,   s_sName, s_sVal, m); return; }
  if (g_smMenu == SM_VARIO)   { SetupMenu_RenderList(objects.vario_list,   s_vName, s_vVal, m); return; }
  if (g_smMenu == SM_SYSTEM)  { SetupMenu_RenderList(objects.system_list,  s_syName, s_syVal, m); return; }
  if (g_smMenu == SM_ABOUT)   { SetupMenu_RenderList(objects.about_list,   s_abName, s_abVal, m); return; }
  if (g_smMenu == SM_GLIDER)  { SetupMenu_RenderList(objects.glider_list,  s_glName, s_glVal, m); return; }
  if (g_smMenu == SM_INFOBOX) {
    if (g_ibEditState == IBEDIT_NONE) {
      SetupMenu_RenderList(objects.infobox_mode_list, s_imName, s_imVal, m);
      return;
    }
    SetupMenu_HideLists();
    if (objects.setup_panel) lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
    if (objects.vario_meter)               lv_obj_clear_flag(objects.vario_meter, LV_OBJ_FLAG_HIDDEN);
    if (objects.infobox_display_container) lv_obj_clear_flag(objects.infobox_display_container, LV_OBJ_FLAG_HIDDEN);
    if (objects.img_gps)                   lv_obj_clear_flag(objects.img_gps, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (g_smMenu == SM_INFOBOX_METRIC) {
    IBDBG("[IB] Apply SM_INFOBOX_METRIC zone=%d\n", s_ibZoneSel);
    if (s_ibZoneSel == 2) {
      SmMenu ciMenu = {"Center Mode", CI_LIST, 4};
      lv_label_set_text(objects.settings, ciMenu.title);
      SetupMenu_RenderList(objects.center_info_list, s_ciListNames, s_ciListVals, &ciMenu);
    } else {
      // Visual numbering "Infobox 1..5" that skips zone 2 (reserved for the center):
      // zones 0,1 -> 1,2 ; zones 3,4,5 -> 3,4,5.
      char title[16];
      int n = (s_ibZoneSel < 2) ? (s_ibZoneSel + 1) : s_ibZoneSel;
      snprintf(title, sizeof(title), "Infobox %d", n);
      lv_label_set_text(objects.settings, title);
      SetupMenu_RenderList(objects.infobox_list, s_ibListNames, s_ibListVals, m);
    }
    IBDBG("[IB] Apply SM_INFOBOX_METRIC done\n");
    return;
  }
  if (g_smMenu == SM_PROFILE) {
    // SetupMenu_RenderList() writes a generic "Profile N" text on vals[0] (ST_CHOICE
    // -> SmValTxt(SET_PROFILE_SELECT)): the custom name MUST be applied AFTER,
    // otherwise it is immediately overwritten (observed: Edit/Save/Delete seemed to have no effect).
    SetupMenu_RenderList(objects.profil_list, s_prName, s_prVal, m);
    Profile_RefreshName();
    lv_label_set_text(objects.prval0, g_profileName);
    return;
  }
  SetupMenu_HideLists();   // autres menus (windowing) : conteneurs caches
  lv_obj_add_flag(objects.item5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(objects.item6, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* slots[5] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item4 };
  lv_obj_update_layout(objects.main);
  lv_area_t fa;
  lv_obj_get_coords(objects.selection_frame_1, &fa);
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  for (int row = 0; row < 5; row++) {
    int idx = (int)g_smSel + (row - 2);                 // row 2 = center = selected
    if (idx < 0 || idx >= m->n) {                       // slot outside the menu -> everything hidden
      lv_obj_add_flag(slots[row],   LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(slots[row], LV_OBJ_FLAG_HIDDEN);
    const SmItem* it = &m->items[idx];
    bool hasVal = !(it->type == ST_SUB || it->type == ST_BACK || it->arg == SET_NONE);

    lv_label_set_text(slots[row], it->label);
    // red only for Exit/Back; white otherwise
    lv_obj_set_style_text_color(slots[row], lv_color_hex(it->type == ST_BACK ? 0xff0000 : 0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    if (hasVal) {                                       // setting: name on the LEFT, value on the RIGHT
      lv_obj_align(slots[row], LV_ALIGN_TOP_LEFT, 50, SM_ROW_Y[row]);
      char v[16]; SmValTxt(it->arg, v, sizeof(v));
      char vb[20];
      bool ed = (g_smEdit && idx == (int)g_smSel);      // editing: value between [ ]
      snprintf(vb, sizeof(vb), ed ? "[%s]" : "%s", v);
      lv_label_set_text(s_smVal[row], vb);
      lv_obj_align(s_smVal[row], LV_ALIGN_TOP_RIGHT, -50, SM_ROW_Y[row]);
      lv_obj_clear_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(objects.main);
      lv_area_t la; lv_obj_get_coords(slots[row], &la);
      SetupMenu_ApplyItemZoom(slots[row], (la.y1+la.y2)/2, frame_cy);
      SetupMenu_ApplyItemZoom(s_smVal[row], (la.y1+la.y2)/2, frame_cy);
    } else {                                            // category / Back: name CENTERED, no value
      lv_obj_align(slots[row], LV_ALIGN_TOP_MID, 0, SM_ROW_Y[row]);
      lv_obj_add_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(objects.main);
      lv_area_t la; lv_obj_get_coords(slots[row], &la);
      SetupMenu_ApplyItemZoom(slots[row], (la.y1+la.y2)/2, frame_cy);
    }
  }
}

// ============================================================
//  QR CODE SCREEN (WiFi sharing "App connect")
// ============================================================
static lv_obj_t* s_qrCode = NULL;
static bool      g_qrOpen = false;
static bool      s_qrServerWasActive = false;

static void QrScreen_Show() {
  if (!s_qrCode && objects.qr_slot) {
    // qr_slot is a raw lv_obj_create() (EEZ): scrollable + theme padding/border by
    // default -> the QR (same size as the slot) overflowed the content area, hence scrollbars
    // visible + QR shifted instead of centered. We neutralize the default style.
    lv_obj_clear_flag(objects.qr_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(objects.qr_slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(objects.qr_slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(objects.qr_slot, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    // qr_panel is transparent in EEZ -> the System menu background stayed visible underneath.
    lv_obj_set_style_bg_opa(objects.qr_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(objects.qr_panel, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_qrCode = lv_qrcode_create(objects.qr_slot, 220, lv_color_black(), lv_color_white());
    lv_obj_set_pos(s_qrCode, 0, 0);
    static const char payload[] = "WIFI:T:WPA;S:LIM-Vario;P:limvario;;";
    lv_qrcode_update(s_qrCode, payload, strlen(payload));
  }
  if (objects.qr_panel) {
    lv_obj_clear_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(objects.qr_panel);   // construit tot dans l'arbre EEZ (avant setup/quick menu)
                                                 // -> without this, it stays hidden behind those panels if open
                                                 // while the setup menu is still shown
  }
  g_qrOpen = true;
}

static void QrScreen_Close() {
  if (objects.qr_panel) lv_obj_add_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);
  g_qrOpen = false;
}

// Opens automatically as soon as "App connect" (System menu) goes OFF->ON; closes
// on its own if the server goes OFF again (nothing left to scan). Manual close (long
// press ENC1, see menu_onLongPress) = just closes the overlay, does NOT turn off WiFi.
static void QrScreen_Tick() {
  bool active = FlightLog_ServerActive();
  if (active && !s_qrServerWasActive) QrScreen_Show();
  else if (!active && g_qrOpen)       QrScreen_Close();
  s_qrServerWasActive = active;
}

// ============================================================
//  MENU LOGIC (called from the encoder task, not LVGL)
// ============================================================
static uint32_t g_lastLongPressTime = 0;
static uint32_t g_lastButtonTime    = 0;

static void menu_onButton()
{
  if (g_qrOpen) { QrScreen_Close(); return; }   // modal QR overlay: any click closes it (like "Back")
  if (millis() - g_lastLongPressTime < 600) return; // ignore bounces/release after a long-press
  if (millis() - g_lastButtonTime < 250)    return; // anti-rebond entre clics courts
  g_lastButtonTime = millis();
  g_menuLastActivity = millis();
  if (g_setupOpen) { SetupMenu_Press(); return; }   // setup menu prioritaire
  switch (g_menuState) {
    case MENU_CLOSED: g_menuState = MENU_NAV; g_menuIndex = 0; break;
    case MENU_NAV:
      if (g_menuIndex == MENU_EXIT) g_menuState = MENU_CLOSED;
      else                          g_menuState = MENU_EDIT;
      break;
    case MENU_EDIT:
      if (g_menuIndex == 5) Config_Save();  // persiste le profil actif (1x en sortant d'edition)
      g_menuState = MENU_NAV;
      break;
  }
  g_menuDirty = true;
}

static void menu_onLongPress()
{
  if (millis() - g_lastLongPressTime < 1000) return; // ignore continuous hold or electrical bounce
  g_lastLongPressTime = millis();
  g_menuLastActivity = millis();
  if (g_qrOpen)                     QrScreen_Close();       // just closes the QR overlay (WiFi stays active)
  else if (g_setupOpen)             SetupMenu_Close();       // in setup: closes everything and returns to the vario
  else if (g_menuState != MENU_CLOSED) { g_menuState = MENU_CLOSED; g_menuDirty = true; }  // closes the quick menu
  else                              SetupMenu_Open();        // opens the setup
}

static void menu_onRotate(long delta)
{
  if (g_qrOpen) return;   // overlay QR modal : rien a regler derriere
  g_menuLastActivity = millis();
  if (g_setupOpen) { SetupMenu_Rotate(delta); return; }   // setup menu prioritaire
  if (g_menuState == MENU_CLOSED) {
    g_mcTenths += (int)delta;
    if (g_mcTenths < MC_MIN_T) g_mcTenths = MC_MIN_T;
    if (g_mcTenths > MC_MAX_T) g_mcTenths = MC_MAX_T;
  } else if (g_menuState == MENU_NAV) {
    int i = g_menuIndex + (int)delta;
    if (i < 0)              i = 0;
    if (i > MENU_COUNT - 1) i = MENU_COUNT - 1;
    g_menuIndex = i;
    g_menuDirty = true;
  } else {
    switch (g_menuIndex) {
      case 0: g_qnh    += delta;       if (g_qnh<900)   g_qnh=900;   if (g_qnh>1100)   g_qnh=1100;   break;
      case 1: g_water  += delta * 10;  if (g_water<0)   g_water=0;   if (g_water>300)  g_water=300;  break;
      case 2: g_bugs   += delta * 10;  if (g_bugs<0)    g_bugs=0;    if (g_bugs>90)    g_bugs=90;    break;
      case 3: g_weight += delta;       if (g_weight<50) g_weight=50; if (g_weight>150) g_weight=150; break;
      case MENU_SOUND:
        if (delta > 0) g_sinkSound = true;
        else if (delta < 0) g_sinkSound = false;
        // Immediately send the command to the calculator
        Cmd_SendState();
        break;
      case 5:  // Profile: switch to another saved profile (glider + info boxes)
                // Config_Save() differe a la sortie d'edition (menu_onButton), pas ici :
                // otherwise a full NVS write on every encoder detent -> blocks LVGL.
        Profile_SelectNext((int)delta);
        break;
    }
    g_menuDirty = true;
  }
}

// ============================================================
//  LIAISON UART
// ============================================================
static void Link_Init()
{
  Serial1.setRxBufferSize(512);
  Serial1.begin(LIM_BAUD, SERIAL_8N1, LINK_RX, LINK_TX);
}

static float altitude_from_qnh(float p_pa, int qnh_hpa)
{
  if (p_pa <= 0.0f || qnh_hpa <= 0) return 0.0f;
  return 44330.0f * (1.0f - powf(p_pa / ((float)qnh_hpa * 100.0f), 0.1902949f));
}

static void Link_HandleEncoders(const lim_packet_t* p)
{
  if (!g_linkSynced) {
    enc1Last = p->enc1_count; enc2Last = p->enc2_count;
    enc1BtnLast = p->enc1_btn; enc2BtnLast = p->enc2_btn;
    g_linkSynced = true;
    return;
  }
  uint32_t now = millis();
  long d1 = (long)p->enc1_count - (long)enc1Last;
  if (d1 != 0) {
    enc1Last = p->enc1_count;
    // Debounce: ignore micro-oscillations (+1 then -1 within 80ms)
    static int32_t lastDir1 = 0;
    static uint32_t lastRot1Ms = 0;
    int32_t dir = (d1 > 0) ? 1 : -1;
    uint32_t nowMs = millis();
    bool sameDir = (dir == lastDir1);
    bool slowEnough = (nowMs - lastRot1Ms) > 80;  // ignore rebond < 80ms
    if (sameDir || slowEnough) {
      menu_onRotate(d1);
      lastDir1  = dir;
      lastRot1Ms = nowMs;
    } else {
      // Rebond detecte : on met a jour lastDir sans appliquer
      lastDir1 = dir;
    }
  }
  // --- Gestion robuste du bouton ENC1 (anti-rebond et appui long unique) ---
  static uint32_t s_b1HighStart = 0;
  static uint32_t s_b1LowStart  = 0;
  static bool     s_b1Debounced = false;

  bool raw1 = p->enc1_btn;
  if (raw1) {
    s_b1LowStart = 0;
    if (s_b1HighStart == 0) s_b1HighStart = now;
    if (!s_b1Debounced && (now - s_b1HighStart >= 30)) {
      s_b1Debounced = true;
      btnDownTime = now;
      btnLongFired = false;
    }
  } else {
    s_b1HighStart = 0;
    if (s_b1LowStart == 0) s_b1LowStart = now;
    if (s_b1Debounced && (now - s_b1LowStart >= 60)) {
      s_b1Debounced = false;
      if (!btnLongFired) {
        menu_onButton();
      }
      btnLongFired = false;
    }
  }
  if (s_b1Debounced && !btnLongFired && (now - btnDownTime) > LONG_PRESS_MS) {
    btnLongFired = true;
    menu_onLongPress();
  }
  enc1BtnLast = s_b1Debounced;

  long d2 = (long)p->enc2_count - (long)enc2Last;
  if (d2 != 0) {
    enc2Last = p->enc2_count;
    g_volume += (int)d2;
    if (g_volume < 0)  g_volume = 0;
    if (g_volume > 20) g_volume = 20;
    g_volShownAt = millis();  // declenche l'affichage de l'arc
  }

  // --- Gestion robuste du bouton ENC2 ---
  static uint32_t s_b2HighStart = 0;
  static uint32_t s_b2LowStart  = 0;
  static bool     s_b2Debounced = false;
  static uint32_t btn2DownTime  = 0;
  static bool     btn2LongFired = false;

  bool raw2 = p->enc2_btn;
  if (raw2) {
    s_b2LowStart = 0;
    if (s_b2HighStart == 0) s_b2HighStart = now;
    if (!s_b2Debounced && (now - s_b2HighStart >= 30)) {
      s_b2Debounced = true;
      btn2DownTime = now;
      btn2LongFired = false;
    }
  } else {
    s_b2HighStart = 0;
    if (s_b2LowStart == 0) s_b2LowStart = now;
    if (s_b2Debounced && (now - s_b2LowStart >= 60)) {
      s_b2Debounced = false;
      btn2LongFired = false;
    }
  }
  if (s_b2Debounced && !btn2LongFired && (now - btn2DownTime) > LONG_PRESS_MS) {
    btn2LongFired = true;
    if (g_qrOpen) {
      QrScreen_Close();   // modal QR overlay: close instead of re-toggling App connect
    } else {
      FlightLog_ServerToggle();      // appui long enc2 = WiFi logs ON/OFF
      g_updateMode = FlightLog_ServerActive();  // keeps the menu's "App connect" toggle consistent
    }
  }
  enc2BtnLast = s_b2Debounced;
}

static void Link_Poll()
{
  static uint8_t  buf[sizeof(lim_packet_t)];
  static size_t   idx = 0;
  static uint32_t lastPktMs = 0;

  // Timeout : plus de paquet depuis 3 s -> lien considere perdu
  if (g_linkOk && (millis() - lastPktMs) > 3000) {
    g_linkOk     = false;
    g_linkSynced = false;   // force encoder re-sync on reconnection
    FlightLog_AddError("LINK", "Lost UART serial link with the calculator (>3000ms)");
  }

  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();
    if (idx == 0) { if (b == LIM_SYNC0) buf[idx++] = b; continue; }
    if (idx == 1) {
      if (b == LIM_SYNC1)      buf[idx++] = b;
      else if (b == LIM_SYNC0) { buf[0] = b; idx = 1; }
      else                       idx = 0;
      continue;
    }
    if (idx >= sizeof(buf)) idx = 0;
    buf[idx++] = b;
    if (idx == sizeof(buf)) {
      idx = 0;
      const lim_packet_t* p = (const lim_packet_t*)buf;
      if (lim_check(p)) {
        lastPktMs  = millis();
        g_pktCount++;
        g_vario    = p->vario;
        g_varioInt = p->vario_int;
        g_pressure = p->pressure;
        g_altitude = altitude_from_qnh(g_pressure, g_qnh);
        g_airspeed = p->airspeed;
        g_gndSpeed = p->gnd_speed;
        g_gpsAlt   = p->gps_alt;
        g_gpsOk    = (p->flags & LIM_FLAG_GPS_OK) != 0;
        g_gpsTrack = p->gps_track;
        g_gpsLat   = p->gps_lat;
        g_gpsLon   = p->gps_lon;
        g_magOk    = (p->flags & LIM_FLAG_MAG_OK) != 0;
        g_magX     = p->mag_x;
        g_magY     = p->mag_y;
        g_magZ     = p->mag_z;
        // Reconnection: resync the command state (sink sound + Condor sim) to the calculator
        if (!g_linkOk) {
          Cmd_SendState();
        }
        g_linkOk = true;
        Link_HandleEncoders(p);
      }
    }
  }
}

// ============================================================
//  APPLICATION LVGL (dans loop() = thread LVGL = safe)
// ============================================================

// Returns the NAME label of each item (used for centering)
// VISUAL order in EEZ (by increasing Y position):
//   0=QNH(y=0) 1=Water(y=44) 2=Bugs(y=88) 3=PilotWt(y=132)
//   4=SinkSnd(y=176) 5=Profil(y=220) 6=Exit(y=264)
static lv_obj_t* Menu_NameLabel(int idx)
{
  switch (idx) {
    case 0: return objects.obj4;       // "QNH"       y=0
    case 1: return objects.obj3;       // "Water B."  y=44
    case 2: return objects.obj2;       // "Bugs"      y=88
    case 3: return objects.obj1;       // "Pilot Wt." y=132
    case 4: return objects.obj5;       // "Sink Snd." y=176
    case 5: return objects.obj0;       // "Profil"    y=220
    case 6: return objects._lbl_exit;  // "Exit"      y=264
  }
  return objects.obj4;
}

static void Menu_LvglSetup()
{
  lv_obj_set_pos(objects.item_list, -23, 0);
  lv_obj_set_size(objects.item_list, 360, 345);
  lv_obj_set_scroll_snap_y(objects.item_list, LV_SCROLL_SNAP_NONE);
  lv_obj_set_scrollbar_mode(objects.item_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_top(objects.item_list,    200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(objects.item_list, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_clip_corner(objects.quick_menu_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
  // Fix the 3px offset of Sink Snd. (EEZ places it at y=179 instead of y=176)
  lv_obj_set_y(objects.obj5, 176);   // "Sink Snd." → aligned to the 44px grid
  lv_obj_set_y(objects.obj6, 176);   // "Mute/Full" → meme ligne
  lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  // --- Volume arc: compact (65x65), in the free right-side zone (same footprint as
  // the reserved "pod" info-box zone 5 / ib_frame_5 in EEZ: x=369 y=192, 62x96) instead
  // of a big arc dead-center of the screen. Back to lv_arc (lv_meter's tick scale was
  // unreadable at this size and rendered white-on-white) -- just the current value as
  // a number in the center, like before.
  // Parented to objects.main (full 480x480, pos 0,0), NOT center_hub: center_hub is
  // only 344x344 at (68,68), so absolute-screen coordinates landed outside its bounds
  // (clipped -> invisible) when it was the parent. ---
  g_arcVol = lv_arc_create(objects.main);
  lv_obj_set_size(g_arcVol, 65, 65);
  lv_obj_set_pos(g_arcVol, 368, 208);          // centre ~(400,240), zone libre a droite
  lv_arc_set_rotation(g_arcVol, 45);           // 135 - 90 (rotation demandee)
  lv_arc_set_bg_angles(g_arcVol, 0, 270);      // arc de 270 degres
  lv_arc_set_range(g_arcVol, 0, 20);
  lv_arc_set_value(g_arcVol, g_volume);
  lv_obj_remove_style(g_arcVol, NULL, LV_PART_KNOB); // pas de poignee
  // LVGL donne un fond MAIN opaque blanc par defaut sans theme -- invisible avant (l'arc
  // etait geant, centre sur la partie claire du cadran), flagrant maintenant qu'il est
  // petit sur une zone sombre. Le rendre transparent : seul l'anneau doit se voir.
  lv_obj_set_style_bg_opa(g_arcVol, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(g_arcVol, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0xfbd500), LV_PART_INDICATOR | LV_STATE_DEFAULT); // jaune
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);      // fond gris
  lv_obj_set_style_arc_width(g_arcVol, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(g_arcVol, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(g_arcVol, LV_OBJ_FLAG_CLICKABLE);
  // Chiffre au centre de l'arc (pas du parent, vu qu'il n'est plus au milieu de l'ecran)
  g_lblVolNum = lv_label_create(objects.main);
  lv_obj_set_style_bg_opa(g_lblVolNum, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);   // idem : pas de boite blanche derriere le texte
  lv_obj_set_style_text_font(g_lblVolNum, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(g_lblVolNum, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(g_lblVolNum, g_arcVol, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(g_lblVolNum, "50");
  // Cache par defaut
  lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
}

static void Menu_Apply()
{
  if (!g_menuDirty) return;
  g_menuDirty = false;

  if (g_menuState == MENU_CLOSED) {
    lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  // Values in white, yellow on the item being edited
  // Ordre visuel EEZ : QNH, Water, Bugs, PilotWt, SinkSnd, Profil
  lv_obj_t* vals[6] = {
    objects.val_qnh,    // 0 = QNH
    objects.val_water,  // 1 = Water B.
    objects.val_bugs,   // 2 = Bugs
    objects.val_weight, // 3 = Pilot Wt.
    objects.obj6,       // 4 = Sink Snd. (Mute/Full)
    objects.val_profil  // 5 = Profil
  };
  for (int i = 0; i < 6; i++)
    lv_obj_set_style_text_color(vals[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  if (g_menuState == MENU_EDIT && g_menuIndex < 6)
    lv_obj_set_style_text_color(vals[g_menuIndex], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);

  // Update the values
  char buf[16];
  snprintf(buf, sizeof(buf), "%d",    g_qnh);    lv_label_set_text(objects.val_qnh,    buf);
  snprintf(buf, sizeof(buf), "%d L",  g_water);  lv_label_set_text(objects.val_water,  buf);
  snprintf(buf, sizeof(buf), "%d %%", g_bugs);   lv_label_set_text(objects.val_bugs,   buf);
  snprintf(buf, sizeof(buf), "%d kg", g_weight); lv_label_set_text(objects.val_weight, buf);
  lv_label_set_text(objects.obj6, g_sinkSound ? "Full" : "Mute"); // val_sound
  Profile_RefreshName();
  lv_label_set_text(objects.val_profil, g_profileName);

  lv_obj_clear_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  // Exact centering of the selected item on the yellow frame
  // Double update_layout: guarantees the coords are valid after display
  lv_obj_update_layout(objects.main);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame,     &fa);
  lv_obj_get_coords(Menu_NameLabel(g_menuIndex), &la);
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  lv_coord_t label_cy = (la.y1 + la.y2) / 2;
  lv_coord_t delta    = label_cy - frame_cy;
  if (delta != 0) lv_obj_scroll_by(objects.item_list, 0, -delta, LV_ANIM_OFF);
}

// Total-energy compensation (inertial) : vario_comp = vario_fused + compensation.
//  compensation = (V_air . a)/g  ~=  TAS * a_forward / g   (MS4525 airspeed + IMU accel)
// The along-track acceleration is measured DIRECTLY by the IMU (VarioFusion_GetFwdAccel),
// instead of differentiating the noisy airspeed signal -> instantaneous, low-noise TE
// compensation -> instantaneous and low-noise (no numerical differentiation of airspeed).
// Without a pitot (no MS4525) -> fall back to the classic GPS ground-speed derivative.
static void Comp_Apply()
{
  // Condor mode: g_vario is already the Condor evario (TE-compensated sim-side). Bypass fusion.
  if (g_condorSim) { g_varioComp = isnan(g_vario) ? 0.0f : g_vario; return; }

  static uint32_t lastUs = 0;
  static float    termF  = 0.0f;              // smoothed compensation term
  static float    vF = 0.0f, vPrev = 0.0f;    // GPS fallback derivative state
  float base = isnan(g_varioFused) ? 0.0f : g_varioFused;

  uint32_t nowUs = micros();
  if (lastUs == 0) { lastUs = nowUs; g_varioComp = base; return; }
  float dt = (nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt <= 0.0f || dt > 0.5f) { g_varioComp = base; return; }

  float term = 0.0f;
  if (g_airspeed > 5.0f) {
    // --- Scalar-product method: TAS * a_forward / g (IMU acceleration, no differentiation) ---
    float aFwd = VarioFusion_GetFwdAccel();           // m/s^2, along the flight path
    float raw  = g_airspeed * aFwd / 9.80665f;        // == (V/g)*dV/dt, but noise-free
    termF += (raw - termF) * (dt / (0.4f + dt));      // light smoothing vs IMU vibration
    term = termF;
    vF = vPrev = 0.0f;                                 // keep GPS state clean for handover
  } else if (g_gpsOk) {
    // --- Fallback (no pitot): classic GPS ground-speed derivative ---
    float v = g_gndSpeed;
    vF += (v - vF) * (dt / (0.5f + dt));
    float dVdt = (vF - vPrev) / dt;
    vPrev = vF;
    term  = (vF / 9.80665f) * dVdt;
    termF = term;                                      // seed smoother for handover to pitot
  } else {
    vF = vPrev = 0.0f;
    termF = 0.0f;
  }
  if (term >  5.0f) term =  5.0f;                      // safety clamps
  if (term < -5.0f) term = -5.0f;
  g_varioComp = base + term;
}

// Spiral / straight-flight detection from the rotation of the GPS track.
//  - samples the heading at ~1 Hz, computes the rotation rate (deg/s)
//  - switches with temporal hysteresis (HOLD_MS) to avoid flickering
//  - sans fix GPS -> vol droit (pas de detection possible)
static void Circling_Apply()
{
  static uint32_t lastMs    = 0;
  static float    prevTrack = NAN;
  static uint32_t enterMs   = 0, exitMs = 0;
  const float    TURN_THRESH   = 6.0f;   // deg/s au-dela = on tourne
  const uint32_t ENTER_HOLD_MS = 4000;   // entrer en spirale (reactif)
  const uint32_t EXIT_HOLD_MS  = 10000;  // sortir : plus long -> ne quitte pas trop tot
  // A l'arret, le bruit de position GPS fait deriver le cap de plusieurs deg/s : sans
  // garde-fou de vitesse, ca declenche une fausse spirale et Wind_Apply integre du bruit.
  // Seuil largement au-dessus du bruit sol, largement sous la vitesse sol d'une vraie
  // spirale meme avec vent fort (cf. LANDED_SPEED_MS=2.8 dans FlightLog.cpp, ici plus strict).
  const float    MIN_SPEED_MS  = 8.0f;   // m/s (~29 km/h)

  uint32_t now  = millis();
  uint32_t dtMs = now - lastMs;
  if (dtMs < 1000) return;               // echantillonnage 1 Hz
  lastMs = now;

  // pas de fix, ou trop lent pour voler -> vol droit (et cap GPS non fiable)
  if (!g_gpsOk || isnan(g_gpsTrack) || isnan(g_gndSpeed) || g_gndSpeed < MIN_SPEED_MS) {
    g_circling = false;
    g_turnDir  = 0;
    prevTrack = NAN; enterMs = exitMs = 0;
    return;
  }
  if (isnan(prevTrack)) { prevTrack = g_gpsTrack; return; }

  float d = g_gpsTrack - prevTrack;      // delta de cap, normalise [-180,180]
  while (d > 180.0f)  d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  prevTrack = g_gpsTrack;
  float turnRate = fabsf(d) / (dtMs / 1000.0f);   // deg/s

  // Turn direction: sign of the smoothed heading delta (increasing heading = right)
  static float turnSigned = 0.0f;
  float dps = d / (dtMs / 1000.0f);               // deg/s signe
  turnSigned += (dps - turnSigned) * 0.3f;        // lissage
  if (g_circling) {
    if      (turnSigned >  1.0f) g_turnDir = +1;  // droite
    else if (turnSigned < -1.0f) g_turnDir = -1;  // gauche
  } else {
    g_turnDir = 0;
  }

  if (turnRate > TURN_THRESH) {          // ca tourne
    exitMs = 0;
    if (enterMs == 0) enterMs = now;
    if (!g_circling && (now - enterMs >= ENTER_HOLD_MS)) g_circling = true;
  } else {                               // ca va droit
    enterMs = 0;
    if (exitMs == 0) exitMs = now;
    if (g_circling && (now - exitMs >= EXIT_HOLD_MS)) g_circling = false;
  }
}

// Wind estimation from GPS drift while circling. Principle: in a coordinated turn at
// roughly constant airspeed, the air-velocity vector sweeps the 360 degrees fairly
// uniformly over one full turn -> its vector average tends toward zero.
// So average(GROUND speed) over one full turn = wind vector (ground speed = airspeed
// + wind). 1 Hz sampling (aligned with Circling_Apply), dt-weighted vector accumulation,
// reset on each new full turn (cumulative rotation 360°).
// Outside a turn: accumulator reset, last estimate kept for display.
static float g_windSpeedMs = NAN;  // LIVE wind speed (m/s) -- feeds the green/instant arrow
static float g_windDirDeg   = NAN;  // LIVE direction D'OU vient le vent (deg, convention meteo)
static float g_windAvgSpeed = NAN;  // AVERAGED wind speed (m/s) -- feeds the grey/averaged wind arrow
static float g_windAvgDir    = NAN; // AVERAGED direction (deg)

// Arrow size follows wind strength: never shrunk to near-invisible at 0 wind (floor),
// already at full size well before "a lot of wind" (saturates, doesn't keep growing).
// LVGL zoom unit: 256 = 100% (native EEZ size).
#define WIND_ZOOM_MIN         160      // floor size at 0 wind (~62%)
#define WIND_ZOOM_MAX         256      // full size, reached at/above the saturation speed
#define WIND_ZOOM_SAT_MS       8.0f    // m/s (~29 km/h) -- speed at which live/avg arrows hit full size
#define ENERGY_ZOOM_SAT_MAG     6.0f   // g_energyMag (already scale-adjusted) at which the energy arrow hits full size

// Live + avg overlap enough on their own (same pivot); the energy arrow only adds to the
// clutter for a real signal, so it stays hidden until the drift is clearly non-trivial
// rather than showing for the slightest wobble (EnergyArrow_Apply's own 0.05 m/s cutoff
// is just a numerical guard for atan2, not a "worth showing" threshold).
#define ENERGY_SHOW_MIN         2.0f   // g_energyMag floor to actually display the arrow

// Maps a magnitude to an arrow zoom value, floored at WIND_ZOOM_MIN and saturating at satAt.
static uint16_t WindArrowZoom(float mag, float satAt)
{
  if (isnan(mag) || mag <= 0.0f) return WIND_ZOOM_MIN;
  float t = mag / satAt;
  if (t > 1.0f) t = 1.0f;
  return (uint16_t)(WIND_ZOOM_MIN + t * (WIND_ZOOM_MAX - WIND_ZOOM_MIN));
}

// Blends a new per-turn wind estimate into a smoothed one (k = blend weight in [0..1]),
// handling the circular wrap of the direction. Higher k = more reactive.
static void wind_blend(float* spd, float* dir, float newSpd, float newDir, float k)
{
  *spd += (newSpd - *spd) * k;
  float dd = newDir - *dir;
  while (dd > 180.0f)  dd -= 360.0f;
  while (dd < -180.0f) dd += 360.0f;
  *dir += dd * k;
  if (*dir < 0.0f)    *dir += 360.0f;
  if (*dir >= 360.0f) *dir -= 360.0f;
}

static void Wind_Apply()
{
  static uint32_t lastMs    = 0;
  static float    prevTrack = NAN;
  static float    sumEast = 0.0f, sumNorth = 0.0f, sumDt = 0.0f, rotAccum = 0.0f;

  uint32_t now  = millis();
  uint32_t dtMs = now - lastMs;
  if (dtMs < 1000) return;               // echantillonnage 1 Hz
  float dt = dtMs / 1000.0f;
  lastMs = now;

  if (!g_circling || !g_gpsOk || isnan(g_gpsTrack) || isnan(g_gndSpeed)) {
    prevTrack = NAN; sumEast = sumNorth = sumDt = rotAccum = 0.0f;
    return;
  }
  if (isnan(prevTrack)) { prevTrack = g_gpsTrack; return; }

  float d = g_gpsTrack - prevTrack;      // delta de cap, normalise [-180,180]
  while (d > 180.0f)  d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  prevTrack = g_gpsTrack;
  rotAccum += fabsf(d);

  float rad = g_gpsTrack * (PI / 180.0f);
  sumEast  += g_gndSpeed * sinf(rad) * dt;
  sumNorth += g_gndSpeed * cosf(rad) * dt;
  sumDt    += dt;

  if (rotAccum >= 360.0f && sumDt > 0.0f) {   // tour complet accumule -> nouvelle estimation
    float windEast  = sumEast  / sumDt;       // vecteur "vent VERS" (ou il souffle)
    float windNorth = sumNorth / sumDt;
    float newSpeed = sqrtf(windEast * windEast + windNorth * windNorth);
    float newDir   = atan2f(-windEast, -windNorth) * (180.0f / PI);  // "vent DE" = oppose
    if (newDir < 0.0f) newDir += 360.0f;

    if (isnan(g_windSpeedMs)) {
      g_windSpeedMs = g_windAvgSpeed = newSpeed;   // seed both estimates on the first turn
      g_windDirDeg  = g_windAvgDir   = newDir;
    } else {
      wind_blend(&g_windSpeedMs,  &g_windDirDeg, newSpeed, newDir, 0.5f);   // LIVE  (~last turns)
      wind_blend(&g_windAvgSpeed, &g_windAvgDir, newSpeed, newDir, 0.15f);  // AVERAGED (~30 s)
    }

    sumEast = sumNorth = sumDt = rotAccum = 0.0f;   // reset for the next turn
  }
}

// Energy Arrow (Joe Wurts method): the vector difference between the live wind and the
// averaged wind. Near a thermal the horizontal flow field bends the local wind, so this
// difference points toward likely lift (at low altitude; it reverses at high altitude --
// reverses at high altitude). Needs both wind estimates (circling-derived). Scaled for display.
// Exposes g_energyDir (compass bearing the arrow points TO) + g_energyMag (m/s * scale) for
// wiring to the EEZ 'img_wind_arrow_energy' image (rotate rel = g_energyDir - g_gpsTrack).
static float g_energyDir = NAN;
static float g_energyMag = NAN;
#define ENERGY_ARROW_SCALE 3.5f   // display prominence factor (0..10)
static void EnergyArrow_Apply()
{
  if (isnan(g_windSpeedMs) || isnan(g_windDirDeg) ||
      isnan(g_windAvgSpeed) || isnan(g_windAvgDir)) { g_energyDir = g_energyMag = NAN; return; }
  // Air-velocity vectors (where the wind blows TO = FROM + 180), compass frame (E=sin, N=cos).
  float dl = (g_windDirDeg + 180.0f) * 0.01745329f;
  float da = (g_windAvgDir + 180.0f) * 0.01745329f;
  float eE = g_windSpeedMs * sinf(dl) - g_windAvgSpeed * sinf(da);
  float eN = g_windSpeedMs * cosf(dl) - g_windAvgSpeed * cosf(da);
  float mag = sqrtf(eE * eE + eN * eN);
  g_energyMag = mag * ENERGY_ARROW_SCALE;
  if (mag > 0.05f) {
    g_energyDir = atan2f(eE, eN) * 57.29578f;   // bearing the arrow points TO
    if (g_energyDir < 0.0f) g_energyDir += 360.0f;
  } else {
    g_energyDir = NAN;
  }
}

// Shows wind direction/speed (2 EEZ labels) only in straight flight with a GPS fix and
// an estimate available -- symmetric to the Thermal Helper (visible only while circling).
// The fixed glider + animated arrow (EEZ images to come) will follow the same gating
// once built.
static void WindDisplay_Update()
{
  // Same principle as the Thermal Helper (symmetric): shown as soon as NOT circling,
  // not only if a GPS fix is present -- without a fix, Circling_Apply already forces
  // g_circling=false (straight flight by default), so the graphic must show right
  // next (with a placeholder while the wind estimate is not yet available),
  // pas attendre un fix comme condition supplementaire.
  bool show     = (g_menuState == MENU_CLOSED) && !g_setupOpen && !g_circling;
  bool haveWind = !isnan(g_windSpeedMs) && !isnan(g_windDirDeg);
  // No confidence/maturity fade: shown at full opacity as soon as data is available,
  // hidden otherwise. A hard show/hide reads more clearly in flight than a slow fade.
  if (objects.lbl_wind_dir) {
    if (show) {
      char b[8];
      if (haveWind) snprintf(b, sizeof(b), "%03.0f\xC2\xB0", g_windDirDeg);   // UTF-8 degree sign (font has glyph 0xB0)
      else          snprintf(b, sizeof(b), "---");
      lv_label_set_text(objects.lbl_wind_dir, b);
      lv_obj_clear_flag(objects.lbl_wind_dir, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(objects.lbl_wind_dir, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (objects.lbl_wind_value_speed) {
    if (show) {
      char b[8];
      if (haveWind) {
        float spd = g_uSpeed ? g_windSpeedMs * 1.94384f : g_windSpeedMs * 3.6f;  // m/s -> kt / km/h
        snprintf(b, sizeof(b), "%.0f", spd);
      } else snprintf(b, sizeof(b), "---");
      lv_label_set_text(objects.lbl_wind_value_speed, b);
      lv_obj_clear_flag(objects.lbl_wind_value_speed, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(objects.lbl_wind_value_speed, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // The raw LIVE estimate is noisy turn-to-turn and isn't shown on its own (matches
  // LARUS/LX Hawk): it's only a computation input for the energy arrow below, and its
  // value/direction are already exposed as text (lbl_wind_dir/lbl_wind_value_speed).
  if (objects.img_wind_arrow) lv_obj_add_flag(objects.img_wind_arrow, LV_OBJ_FLAG_HIDDEN);
  if (objects.img_glider_wind) {
    if (show) lv_obj_clear_flag(objects.img_glider_wind, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(objects.img_glider_wind, LV_OBJ_FLAG_HIDDEN);
  }

  // AVG (averaged wind) arrow: same pivot/rotation convention as the live arrow above.
  if (objects.img_wind_arrow_avg) {
    if (show && haveWind) {
      float rel = g_windAvgDir + 180.0f - g_gpsTrack;
      while (rel <   0.0f) rel += 360.0f;
      while (rel >= 360.0f) rel -= 360.0f;
      lv_img_set_angle(objects.img_wind_arrow_avg, (int16_t)(rel * 10.0f));
      lv_img_set_zoom(objects.img_wind_arrow_avg, WindArrowZoom(g_windAvgSpeed, WIND_ZOOM_SAT_MS));
      lv_obj_clear_flag(objects.img_wind_arrow_avg, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(objects.img_wind_arrow_avg, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // ENERGY arrow (Joe Wurts method, see EnergyArrow_Apply): points toward likely lift.
  // g_energyDir is already the bearing to point TO (no +180 needed, unlike the wind-from
  // convention above). Hidden while NaN (not enough data yet) or the drift isn't significant.
  if (objects.img_wind_arrow_energy) {
    if (show && !isnan(g_energyDir) && !isnan(g_energyMag) && g_energyMag > ENERGY_SHOW_MIN) {
      float rel = g_energyDir - g_gpsTrack;
      while (rel <   0.0f) rel += 360.0f;
      while (rel >= 360.0f) rel -= 360.0f;
      lv_img_set_angle(objects.img_wind_arrow_energy, (int16_t)(rel * 10.0f));
      lv_img_set_zoom(objects.img_wind_arrow_energy, WindArrowZoom(g_energyMag, ENERGY_ZOOM_SAT_MAG));
      lv_obj_clear_flag(objects.img_wind_arrow_energy, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(objects.img_wind_arrow_energy, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// Avg climb: moving average of the compensated vario over 15/20/30 s (1 Hz sample, recomputed
// on screen). Replaces the vario_int value from the calculator to reflect the Avg climb setting.
static void AvgClimb_Apply()
{
  static float    ring[30] = {0};
  static int      filled = 0, head = 0;
  static uint32_t lastMs = 0;
  uint32_t now = millis();
  if (lastMs != 0 && now - lastMs < 1000) return;
  lastMs = now;
  ring[head] = isfinite(g_varioComp) ? g_varioComp : 0.0f;
  head = (head + 1) % 30;
  if (filled < 30) filled++;
  int win = (g_avgClimb == 0) ? 15 : (g_avgClimb == 1) ? 20 : 30;
  if (win > filled) win = filled;
  float sum = 0.0f;
  for (int k = 0; k < win; k++) sum += ring[(head - 1 - k + 30) % 30];
  g_varioAvg = (win > 0) ? sum / win : 0.0f;
}

// Flight time: stopwatch since takeoff. Takeoff = speed (airspeed or ground, per the
// g_airspeed source) > 40 km/h for 3 s; landing = < 10 km/h for 30 s.
// UNIT WARNING: g_airspeed and g_gndSpeed arrive from the calculator in m/s (see GpsLink),
// NOT km/h -> the thresholds are expressed in m/s (40 km/h = 11.1 m/s, 10 km/h = 2.78 m/s).
// Historical bug: thresholds left at 40/10 "km/h" but compared to m/s -> 40 m/s = 144 km/h,
// never reached in ground speed -> takeoff never detected -> stopwatch stuck at 00:00.
static void FlightTime_Apply()
{
  static uint32_t aboveSince = 0, belowSince = 0;
  uint32_t now = millis();
  if (g_condorSim) {
    // In Condor mode, the calculator forces airspeed/gnd_speed to a constant (30) to
    // avoid double TE compensation on the display side -> unusable to detect a
    // "takeoff" here. Instead we use the state of the received Condor data (g_gpsOk,
    // active as long as a Condor packet arrives) as the flight signal.
    if (!g_inFlight && g_gpsOk)       { g_inFlight = true;  g_takeoffMs = now; }
    else if (g_inFlight && !g_gpsOk)  { g_inFlight = false; }
    return;
  }
  float spd = (g_airspeed > 5.0f) ? g_airspeed : g_gndSpeed;   // m/s; airspeed if pitot, else GPS ground
  if (!g_inFlight) {
    if (spd > 11.0f) { if (aboveSince == 0) aboveSince = now;              // 11 m/s ~ 40 km/h
                       if (now - aboveSince >= 3000) { g_inFlight = true; g_takeoffMs = aboveSince; } }
    else aboveSince = 0;
  } else {
    if (spd < 2.8f) { if (belowSince == 0) belowSince = now;              // 2.8 m/s ~ 10 km/h
                       if (now - belowSince >= 30000) g_inFlight = false; }
    else belowSince = 0;
  }
}

// Altitude gain of the current thermal: reset to 0 on each entry into a turn, tracks
// (alt - entry_alt) while circling, freezes the last value in straight flight.
static void ClimbGain_Apply()
{
  static bool  prevCirc = false;
  static float entryAlt = 0.0f;
  if (g_circling && !prevCirc) { entryAlt = g_altitude; g_climbGain = 0.0f; }
  if (g_circling) g_climbGain = g_altitude - entryAlt;
  prevCirc = g_circling;
}

// Fits a parabola sink(V) = a*V^2 + b*V + c to the 3 points of the glider's
// glider (V1/Si1, V2/Si2, V3/Si3: speed km/h -> sink rate m/s, always negative).
// Expanded Lagrange interpolation -> direct coefficients (no linear-system
// matrix). V1/V2/V3 are assumed distinct (from the glider database or the
// menu Glider infos) ; en cas de collision (valeurs egales editees a la main), on
// falls back to a flat polar at the 1st point rather than propagating a division by 0.
static void Polar_Fit(float* a, float* b, float* c)
{
  float v1 = g_gliderV1, v2 = g_gliderV2, v3 = g_gliderV3;
  float s1 = g_gliderSi1, s2 = g_gliderSi2, s3 = g_gliderSi3;
  float d1 = (v1 - v2) * (v1 - v3);
  float d2 = (v2 - v1) * (v2 - v3);
  float d3 = (v3 - v1) * (v3 - v2);
  if (fabsf(d1) < 1e-3f || fabsf(d2) < 1e-3f || fabsf(d3) < 1e-3f) {
    *a = 0.0f; *b = 0.0f; *c = s1;   // polaire degeneree -> sink constant de secours
    return;
  }
  float a1 = 1.0f / d1, b1 = -(v2 + v3) / d1, c1 = (v2 * v3) / d1;
  float a2 = 1.0f / d2, b2 = -(v1 + v3) / d2, c2 = (v1 * v3) / d2;
  float a3 = 1.0f / d3, b3 = -(v1 + v2) / d3, c3 = (v1 * v2) / d3;
  *a = s1 * a1 + s2 * a2 + s3 * a3;
  *b = s1 * b1 + s2 * b2 + s3 * b3;
  *c = s1 * c1 + s2 * c2 + s3 * c3;
}

// Glider sink rate (m/s, negative) at the given speed (km/h), from the polar.
static float Polar_Sink(float v_kmh)
{
  float a, b, c;
  Polar_Fit(&a, &b, &c);
  return a * v_kmh * v_kmh + b * v_kmh + c;
}

// Netto vario: measured TE vario minus the glider's own sink rate at the current speed
// -> estimate of the air mass's vertical movement, independent of piloting.
// Requires a true airspeed (MS4525); without a pitot -> NAN (shows "---").
static float g_varioNetto = NAN;
static void Netto_Apply()
{
  g_varioNetto = (g_airspeed > 5.0f) ? (g_varioComp - Polar_Sink(g_airspeed)) : NAN;
}

// Speed to Fly (classic MacCready theory). For sink(V)=aV^2+bV+c, the still-air tangency from
// (0,-MC) satisfies a*V^2 = c + MC. We extend it two ways, now that we have a pitot + wind:
//  - DYNAMIC (dolphin): shift the MC target by the air-mass vertical movement (netto) ->
//    sinking air = fly faster, rising air = ease off.
//  - HEADWIND: add ~50% of the along-track headwind component (fly faster into wind).
// g_stfCommand is the actionable delta (speed command): + = fly faster, - = slow down.
static float g_stfSpeed   = NAN;   // optimal cruise airspeed (km/h)
static float g_stfCommand = NAN;   // km/h to change: >0 speed up, <0 slow down
static void STF_Apply()
{
  float a, b, c;
  Polar_Fit(&a, &b, &c);
  if (fabsf(a) < 1e-6f) { g_stfSpeed = g_stfCommand = NAN; return; }
  float mc = g_mcTenths / 10.0f;

  // Dynamic (dolphin) STF: shift the MacCready target by the current netto (air mass, + up).
  float w  = isnan(g_varioNetto) ? 0.0f : g_varioNetto;
  float v2 = (c + mc - w) / a;
  float vstf = (v2 > 0.0f) ? sqrtf(v2) : NAN;

  // Headwind compensation (uses the circling wind estimate): fly faster into a headwind.
  if (!isnan(vstf) && !isnan(g_windSpeedMs) && !isnan(g_gpsTrack)) {
    float rel      = (g_windDirDeg - g_gpsTrack) * 0.01745329f;  // wind FROM vs our track
    float headwind = g_windSpeedMs * cosf(rel);                  // m/s, + = headwind
    vstf += headwind * 3.6f * 0.5f;                              // +50% of headwind, km/h
  }
  g_stfSpeed = vstf;

  // Actionable command: meaningful only in cruise (not circling) with a pitot airspeed.
  if (!isnan(vstf) && !g_circling && g_airspeed > 5.0f)
    g_stfCommand = vstf - g_airspeed * 3.6f;   // km/h
  else
    g_stfCommand = NAN;
}

#if SIM_THERMAL
// Bench simulation: fake glider circling (right) in an off-center thermal.
// Injecte track/vario synthetiques -> thermal helper visible sans GPS ni vol.
// The off-center offset oscillates (0..50 m) to show the centered <-> offset transition.
static void Sim_Thermal_Step()
{
  static uint32_t t0 = 0;
  if (t0 == 0) t0 = millis();
  float t = (millis() - t0) * 0.001f;

  const int   dir    = +1;       // right turn -> glider on the left
  const float period = 10.0f;    // 1 tour en 10 s
  const float circR  = 45.0f, Rt = 60.0f, Wmax = 3.5f;
  float offset = 25.0f + 25.0f * sinf(t * (2.0f * PI / 24.0f));  // 0..50 m

  float adot = -dir * (2.0f * PI / period);
  float a    = adot * t;     // same angle for position AND velocity (consistent)
  float gx = offset + circR * cosf(a);
  float gy = circR * sinf(a);
  float d  = sqrtf(gx * gx + gy * gy);
  float x  = d / Rt;
  // Realistic thermal: lift at the center + SINK RING around it (sink varies, from ~0
  // far away to ~-2.3 m/s in the ring) -> the blue dots will have varied sizes.
  float vario = Wmax * expf(-x * x) - 2.5f * expf(-1.2f * (x - 1.7f) * (x - 1.7f));

  // Bench: the needle and labels ALSO reflect this SIM vario -> visual consistency
  // (otherwise the needle shows IMU noise and contradicts the helper).
  g_varioComp = vario;

  float vx = -sinf(a) * adot, vy = cosf(a) * adot;
  float track = atan2f(vx, vy) * 180.0f / PI;
  if (track < 0) track += 360.0f;

  ThermalHelper_Update(track, vario, true, dir, millis());
  ThermalDraw_Update((g_menuState == MENU_CLOSED), dir, track);
}
#endif

static void Needles_Apply()
{
  // Throttle ~15 Hz (aligned to the ~58 Hz vsync): avoids redrawing the large gauge
  // needle on every loop iteration (IMU jitter).
  static uint32_t last = 0;
  uint32_t now = millis();
  if ((now - last) < 50) return;   // ~20 Hz
  last = now;

  // Deadband: only update if the displayed value changes by at least 0.1 m/s
  // (avoids the needle - and thus the gauge redraw - jittering in IMU noise)
  static int lastV = -1000000, lastVi = -1000000;
  // isnan() alone does not filter +/-Infinity (e.g. a boot transient before the filter
  // converges): casting an infinite value to int is undefined behavior in C, observed at
  // boot on 2 July 2026 (fus=+201.30 -> needle off the gauge -> freeze ~2s later).
  // isfinite() + clamp to a realistic range (glider: +/-15 m/s).
  float v  = isfinite(g_varioComp) ? g_varioComp : 0.0f;  // needle = GPS-compensated vario
  float vi = isfinite(g_varioAvg)  ? g_varioAvg  : 0.0f;
  if (v  >  15.0f) v  =  15.0f; else if (v  < -15.0f) v  = -15.0f;
  if (vi >  15.0f) vi =  15.0f; else if (vi < -15.0f) vi = -15.0f;
  int vm  = ((int)(v  * 1000.0f) / 100) * 100;            // pas de 0.1 m/s
  int vim = ((int)(vi * 1000.0f) / 100) * 100;
  if (screen_main_state.indicator2 && vm != lastV) {
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator2, vm);
    lastV = vm;
  }
  if (screen_main_state.indicator1 && vim != lastVi) {
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator1, vim);
    lastVi = vim;
  }
}

// Volume arc: visible for VOL_HIDE_MS after the last change
static void Sound_Init()
{
  // Buzzer interne Waveshare = EXIO_PIN8 via TCA9554 (I2C)
  // Disabled: the sound is now handled by the PAM8403 module on the calculator.
  Set_EXIO(EXIO_PIN8, Low);  // silence permanent
  // Send the initial sink sound state to the calculator (Mute by default)
  LIM_CMD_SEND(Serial1, 0x00);
}

static void Sound_Apply()
{
  static bool     bipOn   = false;   // bip actif (pin HIGH) ou silence (pin LOW)
  static uint32_t bipTime = 0;       // timestamp dernier changement d'etat
  uint32_t now = millis();

  float v = isnan(g_varioFused) ? 0.0f : g_varioFused;   // son = vario inertiel

  // --- Volume 0 = silence total ---
  if (g_volume == 0) {
    if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
    return;
  }

  // --- Zone morte : silence ---
  if (v > VARIO_DEAD_LOW && v < VARIO_DEAD_HIGH) {
    if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
    return;
  }

  // --- Descente ---
  if (v <= VARIO_DEAD_LOW) {
    if (!g_sinkSound) {
      if (bipOn) { Set_EXIO(EXIO_PIN8, Low); bipOn = false; }
      return;
    }
    // Full : bip continu lent en descente (1 Hz)
    uint32_t half = 500;  // 500ms on / 500ms off
    if (now - bipTime >= half) {
      bipOn = !bipOn;
      Set_EXIO(EXIO_PIN8, bipOn ? High : Low);
      bipTime = now;
    }
    return;
  }

  // --- Lift: beep cadence that increases with the vario ---
  float t = (v - VARIO_DEAD_HIGH) / (VARIO_VMAX - VARIO_DEAD_HIGH);
  t = fminf(1.0f, fmaxf(0.0f, t));

  // Total beep period: 1200ms (weak lift) → 120ms (strong lift)
  uint32_t period = (uint32_t)(VARIO_PERIOD_SLOW - t * (VARIO_PERIOD_SLOW - VARIO_PERIOD_FAST));
  uint32_t onMs   = (uint32_t)(period * VARIO_DUTY_ON);   // duree du bip
  uint32_t offMs  = period - onMs;                         // duree du silence

  if (bipOn) {
    if (now - bipTime >= onMs) {
      Set_EXIO(EXIO_PIN8, Low);
      bipOn   = false;
      bipTime = now;
    }
  } else {
    if (now - bipTime >= offMs) {
      Set_EXIO(EXIO_PIN8, High);
      bipOn   = true;
      bipTime = now;
    }
  }
}

static void Vol_Apply()
{
  if (!g_arcVol || !g_lblVolNum) return;
  bool shouldShow = (g_volShownAt > 0) &&
                    ((millis() - g_volShownAt) < VOL_HIDE_MS) &&
                    (g_menuState == MENU_CLOSED) && !g_setupOpen;  // masque si menu rapide ou setup ouvert
  if (shouldShow) {
    lv_arc_set_value(g_arcVol, g_volume);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_volume);
    lv_label_set_text(g_lblVolNum, buf);
    // Re-centre a chaque changement de texte : aligner une seule fois a l'init laisse
    // le label decale des que le nombre de chiffres change (1 chiffre vs 2, ex 5 vs 15).
    lv_obj_align_to(g_lblVolNum, g_arcVol, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
  }
}

static void MC_Apply()
{
  if (screen_main_state.indicator)
    lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator,
                                 (int32_t)g_mcTenths * 100);
}

// Creates/positions the Info Box labels once (called from setup(), BEFORE the
// first render). The 5 labels are now real EEZ objects, positioned/styled
// dans infobox_display_container (2 juillet 2026, remplace l'ancien systeme a 3 box
// in EEZ + zone 1 disabled after the intermittent freezes). Zone 2 = reserved for the future
// center graphic (thermal helper/wind), zone 5 stays unused (no EEZ label).
static void Labels_Init()
{
  if (s_ibLabels[0] != NULL || !objects.lbl_ib_haut_sup) return;
  s_ibLabels[0] = objects.lbl_ib_haut_sup;
  s_ibLabels[1] = objects.lbl_ib_haut_inf;
  s_ibLabels[2] = objects.lbl_ib_bas_cent;   // reserved: always IB_EMPTY (g_infoBoxConfig[2])
  s_ibLabels[3] = objects.lbl_ib_bas_sup;
  s_ibLabels[4] = objects.lbl_ib_bas_inf;
}

// Update the numeric labels (altitude, vario, integrated vario).
// Update only when the value changes (avoids useless redraws).
// Update the numeric Info-Box labels (4 zones on the gauge).
// Y position (top of the label) of each zone in infobox_display_container, as placed in
// EEZ. The label is LV_SIZE_CONTENT (width = text) with a fixed X -> without re-centering,
// the LEFT edge stays fixed and the text seems to shift with its length. So we re-center
// it horizontally (LV_ALIGN_TOP_MID) on every text change, as InfoBox_RenderSelect already
// does for the editor labels.
static const lv_coord_t IB_LABEL_Y[6] = { 84, 113, 228, 338, 374, 0 };

static void Labels_Apply()
{
  if (g_menuState != MENU_CLOSED && g_ibEditState == IBEDIT_NONE) return;

  for (int i = 0; i < 6; i++) {
    if (!s_ibLabels[i]) continue;
    if (g_infoBoxConfig[i] == IB_EMPTY) {
      lv_label_set_text(s_ibLabels[i], "");
      lv_obj_align(s_ibLabels[i], LV_ALIGN_TOP_MID, 0, IB_LABEL_Y[i]);
      continue;
    }
    char buf[32];
    switch (g_infoBoxConfig[i]) {
      case IB_VARIO_INST: {
        float v = isnan(g_varioComp) ? 0.0f : g_varioComp;
        float vd = g_uVert ? v * 1.94384f : v;
        snprintf(buf, sizeof(buf), g_uVert ? "%+.1f kt" : "%+.1f m/s", vd);
        break;
      }
      case IB_VARIO_INT: {
        float vi = isfinite(g_varioAvg) ? g_varioAvg : 0.0f;
        float vid = g_uVert ? vi * 1.94384f : vi;
        snprintf(buf, sizeof(buf), g_uVert ? "%+.1f kt" : "%+.1f m/s", vid);
        break;
      }
      case IB_MACCREADY: {
        snprintf(buf, sizeof(buf), "MC %.1f", g_mcTenths / 10.0f);
        break;
      }
      case IB_ALT_BARO: {
        float am = g_uAlt ? g_altitude * 3.28084f : g_altitude;
        int a = (int)(am + (am >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), g_uAlt ? "%d ft" : "%d m", a);
        break;
      }
      case IB_ALT_GPS: {
        if (!g_gpsOk || isnan(g_gpsAlt)) { snprintf(buf, sizeof(buf), "--- %s", g_uAlt ? "ft" : "m"); break; }
        float am = g_uAlt ? g_gpsAlt * 3.28084f : g_gpsAlt;
        int a = (int)(am + (am >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), g_uAlt ? "%d ft" : "%d m", a);
        break;
      }
      case IB_AIRSPEED: {
        // g_airspeed en m/s -> km/h (x3.6) ou noeuds (x1.94384)
        float s = g_uSpeed ? g_airspeed * 1.94384f : g_airspeed * 3.6f;
        snprintf(buf, sizeof(buf), g_uSpeed ? "%.0f kt" : "%.0f km/h", s);
        break;
      }
      case IB_GND_SPEED: {
        float gs = isfinite(g_gndSpeed) ? g_gndSpeed : 0.0f;   // m/s
        float s = g_uSpeed ? gs * 1.94384f : gs * 3.6f;
        snprintf(buf, sizeof(buf), g_uSpeed ? "%.0f kt" : "%.0f km/h", s);
        break;
      }
      case IB_TIME: {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)datetime.hour, (unsigned)datetime.minute, (unsigned)datetime.second);
        break;
      }
      case IB_FLIGHT_TIME: {
        unsigned long sec = g_takeoffMs ? (millis() - g_takeoffMs) / 1000UL : 0UL;
        snprintf(buf, sizeof(buf), "%02lu:%02lu", sec / 3600UL, (sec % 3600UL) / 60UL);
        break;
      }
      case IB_WIND: {
        if (isnan(g_windSpeedMs)) { snprintf(buf, sizeof(buf), "Wind ---"); break; }
        float spd = g_uSpeed ? g_windSpeedMs * 1.94384f : g_windSpeedMs * 3.6f;  // m/s -> kt / km/h
        snprintf(buf, sizeof(buf), "%03.0f %.0f", g_windDirDeg, spd);
        break;
      }
      case IB_CLIMB_GAIN: {
        int g = (int)(g_climbGain + (g_climbGain >= 0 ? 0.5f : -0.5f));
        snprintf(buf, sizeof(buf), "%+d m", g);
        break;
      }
      case IB_FLIGHT_LVL: {
        int fl = (int)((g_altitude / 30.48f) + 0.5f);
        snprintf(buf, sizeof(buf), "FL %03d", fl);
        break;
      }
      case IB_GLIDE: {
        float spd = (g_airspeed > 5.0f) ? g_airspeed : g_gndSpeed;   // m/s; airspeed if pitot, else ground
        if (spd > 5.5f && g_varioFused < -0.1f) {                    // 5.5 m/s ~ 20 km/h
          float ld = spd / (-g_varioFused);   // deja m/s / m/s -> ratio (etait /3.6 = bug km/h)
          if (ld > 199.0f) ld = 199.0f;
          snprintf(buf, sizeof(buf), "L/D %.0f", ld);
        } else {
          snprintf(buf, sizeof(buf), "L/D ---");
        }
        break;
      }
      case IB_NETTO: {
        if (isnan(g_varioNetto)) { snprintf(buf, sizeof(buf), "--- %s", g_uVert ? "kt" : "m/s"); break; }
        float v = g_uVert ? g_varioNetto * 1.94384f : g_varioNetto;
        snprintf(buf, sizeof(buf), g_uVert ? "%+.1f kt" : "%+.1f m/s", v);
        break;
      }
      case IB_STF: {
        if (isnan(g_stfSpeed)) { snprintf(buf, sizeof(buf), "STF ---"); break; }
        float s = g_uSpeed ? g_stfSpeed * 0.539957f : g_stfSpeed;
        snprintf(buf, sizeof(buf), g_uSpeed ? "%.0f kt" : "%.0f km/h", s);
        break;
      }
      case IB_STF_CMD: {
        if (isnan(g_stfCommand)) { snprintf(buf, sizeof(buf), "STF ---"); break; }
        float cmd = g_uSpeed ? g_stfCommand * 0.539957f : g_stfCommand;   // km/h -> kt
        if      (cmd >  3.0f) snprintf(buf, sizeof(buf), "Faster %.0f", cmd);   // too slow -> speed up
        else if (cmd < -3.0f) snprintf(buf, sizeof(buf), "Slower %.0f", -cmd);  // too fast -> ease off
        else                  snprintf(buf, sizeof(buf), "On Speed");
        break;
      }
      case IB_ALERTS: {
        // Most severe fault first. LINK = frozen data (the sneakiest one),
        // SD = log lost, BAT = endurance, GPS = no more wind/track.
        if      (!g_linkOk)                  snprintf(buf, sizeof(buf), "LINK!");
        else if (!FlightLog_SdOk())          snprintf(buf, sizeof(buf), "SD!");
        else if (BAT_analogVolts < 3.60f)    snprintf(buf, sizeof(buf), "BAT!");
        else if (!g_gpsOk)                   snprintf(buf, sizeof(buf), "GPS?");
        else                                 snprintf(buf, sizeof(buf), "OK");
        break;
      }
      case IB_MODE: {
        // Active info-box profile: drives the content of the other zones.
        snprintf(buf, sizeof(buf), g_ibEditCruiseMode ? "Cruise" : "Climb");
        break;
      }
      default:
        buf[0] = '\0';
        break;
    }
    lv_label_set_text(s_ibLabels[i], buf);
    lv_obj_align(s_ibLabels[i], LV_ALIGN_TOP_MID, 0, IB_LABEL_Y[i]);
  }

  // GPS indicator: connected / waiting image depending on the fix received from the calculator
  if (objects.img_gps) {
    static int lastGps = -1;
    int g = g_gpsOk ? 1 : 0;
    if (g != lastGps) {
      lastGps = g;
      lv_img_set_src(objects.img_gps, g ? &img_gps_connected : &img_gps_waiting);
    }
  }

  // WiFi indicator: ON when the companion server is running ("App connect" menu)
  if (objects.img_wifi) {
    static int lastWifi = -1;
    int w = FlightLog_ServerActive() ? 1 : 0;
    if (w != lastWifi) {
      lastWifi = w;
      lv_img_set_src(objects.img_wifi, w ? &img_wifi_on : &img_wifi_off);
    }
  }

  // Battery indicator: full / med / low from the voltage read by BAT_Driver.
  // Hysteresis: without it, a voltage right on a threshold would flicker the icon
  // (and each image change redraws the area).
  if (objects.img_battery) {
    static int lastBat = -1;            // 0 = low, 1 = med, 2 = full
    const float V_FULL = 3.95f;         // 1S LiPo thresholds (tune to the battery)
    const float V_MED  = 3.70f;
    const float HYST   = 0.04f;
    float v = BAT_analogVolts;
    int b;
    if (lastBat < 0) {                  // first evaluation: no hysteresis
      b = (v >= V_FULL) ? 2 : (v >= V_MED ? 1 : 0);
    } else {
      b = lastBat;                      // only change if we cross the threshold + margin
      if      (v >= V_FULL + HYST)                          b = 2;
      else if (v <  V_FULL - HYST && v >= V_MED + HYST)     b = 1;
      else if (v <  V_MED  - HYST)                          b = 0;
    }
    if (b != lastBat) {
      lastBat = b;
      lv_img_set_src(objects.img_battery,
                     b == 2 ? &img_battery_full :
                     b == 1 ? &img_battery_med  : &img_battery_low);
    }
  }
}

static void Menu_AutoClose()
{
  if (g_menuState != MENU_CLOSED &&
      (millis() - g_menuLastActivity) > MENU_TIMEOUT_MS) {
    g_menuState = MENU_CLOSED;
    g_menuDirty = true;
  }
}

// ============================================================
//  Tache de fond : capteurs lents
// ============================================================
void Driver_Loop(void *parameter)
{
  esp_task_wdt_add(NULL);   // watchdog explicite (2 juillet 2026, cf setup())
  uint8_t  slow    = 0;
  uint32_t lastPkt = 0;
  while (1) {
    QMI8658_Loop();                 // IMU ~50 Hz (accel + gyro for the fusion)

    // Fusion vario (AHRS + Kalman) : cadence reguliere, core 0
    uint32_t pkts   = g_pktCount;
    bool     newBaro = (pkts != lastPkt);
    lastPkt = pkts;
    g_varioFused = VarioFusion_Step(Accel.x, Accel.y, Accel.z,
                                    Gyro.x,  Gyro.y,  Gyro.z,
                                    g_pressure, newBaro, g_vario);

    // Filtre vario utilisateur (menu Vario > Vario filter : Fast/Med/Slow) : lissage EMA
    // of the fused vario. Affects the needle (via g_varioComp) AND the sound (via g_varioFused).
    {
      static uint32_t lastF = 0;
      uint32_t nf  = micros();
      float    dtf = (lastF == 0) ? 0.02f : (nf - lastF) * 1e-6f;
      lastF = nf;
      if (dtf > 0.5f) dtf = 0.5f;
      float tau = (g_varioFilter == 0) ? 0.4f : (g_varioFilter == 1) ? 1.0f : 2.0f;  // s
      float a   = dtf / (tau + dtf);
      if (isnan(g_varioFiltered) || !isfinite(g_varioFused)) g_varioFiltered = g_varioFused;
      else g_varioFiltered += a * (g_varioFused - g_varioFiltered);
      g_varioFused = g_varioFiltered;
    }

    if (++slow >= 5) {              // RTC + batterie : ~10 Hz suffit
      slow = 0;
      RTC_Loop();
      BAT_Get_Volts();
    }
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void Driver_Init()
{
  Flash_test();
  BAT_Init();
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Set_EXIO(EXIO_PIN8, Low);
  PCF85063_Init();
  QMI8658_Init();
  xTaskCreatePinnedToCore(Driver_Loop, "Other Driver task", 4096, NULL, 3, NULL, 0);
}

// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== L!M Vario boot ===");

  // Explicit watchdog (2 July 2026): no watchdog panic was ever observed despite total
  // freezes -> sign of a BLOCKING wait (semaphore/mutex/hardware register never released)
  // rather than a CPU-hogging loop (which would eventually starve the idle task and trip
  // the default watchdog). We register the loop() task AND Driver_Loop ourselves to force
  // a panic + backtrace on the next freeze.
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 6000,
    .idle_core_mask = (1 << 0) | (1 << 1),
    .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&twdt_config);
  esp_task_wdt_add(NULL);   // loop() task (setup() runs on the same task)

  Config_Load();

  // Wireless_Test2() removed (2 July 2026): vendor demo (WiFi + Bluetooth scan) with no
  // functional use in the vario. The BLE scan never fully freed its internal RAM after
  // BLEDevice::deinit() (known Arduino BLE lib bug), leaving ~5 KB of internal heap free
  // permanently -> random freezes whenever any largish allocation (menu, list, SD file)
  // hit the wall.
  Serial.println(">> Driver_Init");    Driver_Init();
  Serial.println(">> LCD_Init");       LCD_Init();
  Serial.println(">> SD_Init");        SD_Init();
  Serial.println(">> GliderDB_LoadSD"); GliderDB_LoadSD();
  Serial.println(">> FlightLog_Init"); FlightLog_Init();
  Serial.println(">> Lvgl_Init");      Lvgl_Init();
  Serial.println(">> ui_init");        ui_init();   // EEZ creates the screens (splash + main)
  // Main screen background aligned to the center_hub color (0x1f333e): the gauge background
  // image (TRUE_COLOR_ALPHA) has semi-transparent areas that, composited on the default
  // black, looked darker than the opaque hub -> visible difference in RGB565 (invisible in
  // EEZ). Composited on 0x1f333e, those areas render exactly the same color as the hub.
  // (EEZ regenerates screens.c with 0x000000 -> we force it here on every boot.)
  lv_obj_set_style_bg_color(objects.main, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);

  Serial.println(">> Menu_LvglSetup"); Menu_LvglSetup();
  Serial.println(">> SetupMenu_Init"); SetupMenu_Init();
  Serial.println(">> Labels_Init");    Labels_Init();
  Serial.println(">> ThermalDraw_Init"); ThermalDraw_Init(objects.main);
  Serial.println(">> Link_Init");      Link_Init();
  Serial.println(">> Sound_Init");     Sound_Init();

  // --- Loading screen (Splash page designed in EEZ) ---
  // Stays shown until the calculator link is established
  // (the real vario frames arrive), with a minimum time to see the
  // logo and a safety timeout. INSTANT switch to Main.
  lv_scr_load(objects.splash);
  const uint32_t SPLASH_MIN_MS = 4000;    // logo visible au moins 4 s
  const uint32_t SPLASH_MAX_MS = 15000;   // safety: never stay stuck
  uint32_t splashT0 = millis();
  while (millis() - splashT0 < SPLASH_MAX_MS) {
    lv_timer_handler();
    Link_Poll();                          // receives the frames -> updates g_linkOk
    if (g_linkOk && (millis() - splashT0 >= SPLASH_MIN_MS)) break;
#if SIM_THERMAL
    if (millis() - splashT0 >= SPLASH_MIN_MS) break;   // banc : pas d'attente liaison
#endif
    esp_task_wdt_reset();   // long loop (up to 15s) -> feed the explicit watchdog
    delay(5);
  }
  if (!g_linkOk) {
    FlightLog_AddError("BOOT", "Startup: calculator frames not detected after 15s");
  }
  lv_scr_load(objects.main);              // instantane (aucune animation)

  Serial.println(">> setup TERMINE OK");
}

void loop()
{
  bool ibgd = g_ibJustRendered;   // Lvgl_Loop breadcrumb only right after an IB render
  if (ibgd) {
    IBDBG("[IB] before Lvgl_Loop heap=%u minHeap=%u maxAlloc=%u psram=%u maxAllocPsram=%u\n",
          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
          (unsigned)ESP.getFreePsram(),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    g_ibJustRendered = false;
  }
  Lvgl_Loop();
  if (ibgd) IBDBG("[IB] after Lvgl_Loop\n");
  Link_Poll();
  // One-time send of the sound config (pitch/waveform/spread) to the calculator ~2s after boot,
  // when the calculator is ready to receive -> the sound adopts the menu defaults.
  static bool s_soundCfgSent = false;
  if (!s_soundCfgSent && millis() > 2000) { SoundCfg_Send(); s_soundCfgSent = true; }
  Comp_Apply();     // TE compensation (IMU accel scalar product) -> g_varioComp

  // Stream the master vario back to the calculator (~30 Hz) so the speaker beeps the exact
  // same clean vario as the needle (the calc has no IMU -> it can't compute it itself).
  {
    static uint32_t lastVarioTx = 0;
    uint32_t nowv = millis();
    if (nowv - lastVarioTx >= 33) {
      lastVarioTx = nowv;
      LIM_VARIO_SEND(Serial1, isfinite(g_varioComp) ? g_varioComp : 0.0f);
    }
  }

  Circling_Apply(); // detection spirale / vol droit -> g_circling + g_turnDir
  Wind_Apply();       // estimation vent (derive GPS en spirale) -> g_windSpeedMs/g_windDirDeg
  EnergyArrow_Apply(); // fleche energie (difference vectorielle vent live - moyen) -> g_energyDir/Mag
  // Auto-switch the displayed info-boxes between the Climb and Cruise profile, except
  // while editing one of the two profiles in the menu (g_infoBoxConfig then deliberately
  // points to the chosen one -> do not overwrite it from here).
  if (g_ibEditState == IBEDIT_NONE) {
    g_infoBoxConfig = g_circling ? g_ibConfigClimb : g_ibConfigCruise;
  }
  AvgClimb_Apply();  // moving average of the vario (Avg climb) -> g_varioAvg
  FlightTime_Apply();// takeoff / landing detection -> g_takeoffMs / g_inFlight
  ClimbGain_Apply(); // altitude gain of the current thermal -> g_climbGain
  Netto_Apply();     // polar-compensated vario -> g_varioNetto
  STF_Apply();       // vitesse optimale MacCready (polaire) -> g_stfSpeed
#if SIM_THERMAL
  Sim_Thermal_Step();   // banc : injecte un faux thermique (SIM_THERMAL=1)
#else
  ThermalHelper_Update(g_gpsTrack, g_varioComp, g_circling, g_turnDir, millis());
  ThermalDraw_Update((g_menuState == MENU_CLOSED) && !g_setupOpen && g_circling && g_helperEnable, g_turnDir, g_gpsTrack);
  WindDisplay_Update();
#endif
  if (!g_setupOpen) {            // when the setup is open: we no longer draw the vario behind
    Needles_Apply();
    Labels_Apply();
    // Sound_Apply() removed: sound now handled by the PAM8403 on the calculator
    Vol_Apply();   // arc volume temporaire (encodeur 2)
    MC_Apply();
  }
  Menu_AutoClose();
  Menu_Apply();
  SetupMenu_Apply();   // rend le setup menu (appui long ENC1)

  // Journal de vol (10 Hz) + serveur WiFi de recuperation
  // Bisection test 2 July 2026: temporarily disabled, freeze still present
  // -> SD/FlightLog innocente, cause = timeout I2C manquant (cf I2C_Driver.cpp). Reactive.
  FlightLog_Tick(g_pressure, g_altitude, g_gpsAlt,
                 g_vario, g_varioComp, g_varioFused,
                 g_varioNetto, g_varioAvg, VarioFusion_GetVertAccel(),
                 g_airspeed, g_gndSpeed,
                 g_gpsOk, g_gpsTrack, g_gpsLat, g_gpsLon,
                 g_circling, g_turnDir,
                 g_windSpeedMs, g_windDirDeg,
                 g_windAvgSpeed, g_windAvgDir,
                 g_energyMag, g_energyDir,
                 g_climbGain, g_stfSpeed,
                 g_volume);
  FlightLog_ServerLoop();
  QrScreen_Tick();   // opens/closes the QR screen per the "App connect" state

  // --- Rapport perf (instrumentation LVGL_Driver) ---
  {
    extern volatile uint32_t g_perfFlushCnt, g_perfFlushUs, g_perfWaitUs, g_perfPx,
                             g_perfHandlerUs, g_perfHandlerCnt;
    static uint32_t lastPerf = 0;
    if (millis() - lastPerf >= 1000) {
      lastPerf = millis();
      uint32_t fc = g_perfFlushCnt, fu = g_perfFlushUs, wu = g_perfWaitUs, px = g_perfPx;
      uint32_t hu = g_perfHandlerUs, hc = g_perfHandlerCnt;
      g_perfFlushCnt = 0; g_perfFlushUs = 0; g_perfWaitUs = 0;
      g_perfPx = 0; g_perfHandlerUs = 0; g_perfHandlerCnt = 0;
      Serial.printf("[PERF] flush=%u/s blit=%lums waitVsync=%lums px=%luk | handler=%lums (n=%u) | render~=%ldms | heap=%u minHeap=%u maxAlloc=%u psram=%u maxAllocPsram=%u\n",
                    fc, (unsigned long)(fu/1000), (unsigned long)(wu/1000), (unsigned long)(px/1000),
                    (unsigned long)(hu/1000), hc, (long)((hu - fu - wu)/1000),
                    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                    (unsigned)ESP.getFreePsram(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    }
  }

  static uint32_t lastDbg = 0;
  if (millis() - lastDbg >= 2000) {
    lastDbg = millis();
    Serial.printf("[link] ok=%d | fus=%+.2f comp=%+.2f | gps=%.1fm/s fix=%d trk=%.0f | %s | alt=%.0f vol=%d\n",
                  g_linkOk ? 1 : 0, g_varioFused, g_varioComp,
                  g_airspeed, g_gpsOk ? 1 : 0, g_gpsTrack,
                  g_circling ? "SPIRALE" : "DROIT",
                  g_altitude, g_volume);
    Serial.printf("[polar] netto=%+.2f stf=%.0fkm/h (mc=%.1f, airspeed=%.0f)\n",
                  g_varioNetto, g_stfSpeed, g_mcTenths / 10.0f, g_airspeed);
    Serial.printf("[wind] speed=%.1fkm/h dir=%.0f (circling=%d, trk=%.0f, gnd=%.1f)\n",
                  g_windSpeedMs, g_windDirDeg, g_circling ? 1 : 0, g_gpsTrack, g_gndSpeed);

    // --- Dump thermal helper (validation etape 1) ---
    if (g_circling) {
      float mn, mx, avg;
      int nf = ThermalHelper_Stats(&mn, &mx, &avg);
      Serial.printf("[TH] dir=%+d fresh=%2d/%d min=%+.2f max=%+.2f avg=%+.2f | ",
                    ThermalHelper_TurnDir(), nf, ThermalHelper_BinCount(), mn, mx, avg);
      for (int i = 0; i < ThermalHelper_BinCount(); i++) {
        float v;
        if (ThermalHelper_BinValue(i, &v))
          Serial.printf("%d:%+.1f ", i, v);   // case:vario (cap = i*15 deg)
      }
      Serial.println();
    }
  }

  esp_task_wdt_reset();
  vTaskDelay(pdMS_TO_TICKS(5));
}
