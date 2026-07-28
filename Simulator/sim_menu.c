/* Port pour le simulateur PC de toute la logique de menu de Firmware/src/main.cpp
 * (Setup menu, Quick menu, editeur Info Boxes, clavier de saisie profil).
 * Copie fonction-par-fonction du vrai code (memes noms, meme comportement, y compris
 * les bugs connus non encore corriges cote firmware reel), en remplacant uniquement
 * ce qui touche au materiel :
 *   - Preferences (NVS)      -> stubs en RAM (pas de persistance entre lancements)
 *   - LIM_CMD_SEND / SoundCfg_Send / Cmd_SendState -> no-op (pas de calculateur connecte)
 *   - FlightLog_ServerToggle/Active -> simple bascule locale
 *   - RTC (datetime)         -> horloge systeme PC
 *   - ESP.restart()          -> reinitialise les globals, pas de vrai redemarrage
 *
 * Les evenements d'entree (rotation/clic/appui long des 2 encodeurs) sont exposes via
 * SimMenu_OnRotate1/OnButton1/OnLongPress1/OnRotate2/OnLongPress2, appeles par
 * sim_main.c depuis les evenements clavier Win32 -- meme dispatch que
 * menu_onRotate/menu_onButton/menu_onLongPress dans le vrai firmware (Link_HandleEncoders).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <ctype.h>

#include "lvgl.h"
#include "screens.h"
#include "images.h"

/* ---- Horloge (remplace millis()/micros() Arduino) ---- */
static uint32_t millis(void) { return (uint32_t)GetTickCount64(); }
static uint64_t micros(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    return (uint64_t)(now.QuadPart * 1000000ULL / freq.QuadPart);
}

/* ---- RTC simule : horloge systeme PC (memes champs que RTC_PCF85063::datetime_t) ---- */
typedef struct { uint8_t hour, minute, second; } sim_datetime_t;
static sim_datetime_t datetime = {0, 0, 0};
static void RTC_Loop(void) {
    time_t t = time(NULL);
    struct tm lt; localtime_s(&lt, &t);
    datetime.hour = (uint8_t)lt.tm_hour;
    datetime.minute = (uint8_t)lt.tm_min;
    datetime.second = (uint8_t)lt.tm_sec;
}

/* ============================================================
 *  MacCready
 * ============================================================ */
static volatile int g_mcTenths = 15;
#define MC_MIN_T 0
#define MC_MAX_T 50
// Computed each frame by STF_Apply_Sim() (real polar fit on the selected glider + MC),
// see below Labels_Apply -- not a stub anymore, just missing the netto/dolphin term.
static float g_stfSpeed = NAN;

/* ============================================================
 *  QUICK MENU
 * ============================================================ */
typedef enum { MENU_CLOSED, MENU_NAV, MENU_EDIT } MenuState;
static volatile MenuState g_menuState = MENU_CLOSED;
static volatile int  g_menuIndex = 0;
static volatile bool g_menuDirty = true;

#define MENU_COUNT  7
#define MENU_SOUND  4
#define MENU_EXIT   6

static volatile int  g_qnh    = 1013;
static volatile int  g_water  = 0;
static volatile int  g_bugs   = 0;
static volatile int  g_weight = 70;
static volatile bool g_sinkSound = false;
static volatile int  g_volume = 10;
static uint32_t g_volShownAt = 0;
#define VOL_HIDE_MS 2000

static uint32_t btnDownTime  = 0;
static bool     btnLongFired = false;
#define LONG_PRESS_MS 600

static lv_obj_t* g_arcVol    = NULL;
static lv_obj_t* g_lblVolNum = NULL;

/* ============================================================
 *  SETUP MENU
 * ============================================================ */
static volatile bool g_setupOpen = false;
int  g_brightness   = 20;
bool g_helperEnable = true;
bool g_loggerEnable = true;
int  g_varioRange   = 5;
int  g_screenRot    = 0;
uint8_t g_uVert     = 0;
uint8_t g_uAlt      = 0;
uint8_t g_uSpeed    = 0;
int  g_tonePitch    = 700;
uint8_t g_waveform  = 0;
int  g_toneSpread   = 5;
uint8_t g_varioFilter = 1;
uint8_t g_avgClimb    = 1;
static bool g_updateMode = false;
bool g_condorSim  = false;
static bool g_serverOn   = false;   /* stub FlightLog_ServerActive */

void FlightLog_ServerToggle(void) { g_serverOn = !g_serverOn; }
bool FlightLog_ServerActive(void) { return g_serverOn; }

enum InfoBoxMetric {
  IB_VARIO_INST=0, IB_VARIO_INT=1, IB_MACCREADY=2, IB_ALT_BARO=3, IB_ALT_GPS=4,
  IB_AIRSPEED=5, IB_GND_SPEED=6, IB_TIME=7, IB_FLIGHT_TIME=8, IB_WIND=9,
  IB_CLIMB_GAIN=10, IB_FLIGHT_LVL=11, IB_GLIDE=12, IB_EMPTY=13,
  IB_NETTO=14, IB_STF=15, IB_ALERTS=16, IB_MODE=17, IB_METRIC_MAX=18
};
enum CenterZoneMetric { CENTER_THERMAL_HELPER=0, CENTER_WIND_DIR=1, CENTER_EMPTY=2, CENTER_METRIC_MAX=3 };

uint8_t g_ibConfigClimb[6]  = { IB_VARIO_INST, IB_VARIO_INT, IB_EMPTY, IB_ALT_BARO, IB_CLIMB_GAIN, IB_WIND };
uint8_t g_ibConfigCruise[6] = { IB_VARIO_INST, IB_MACCREADY, IB_EMPTY, IB_ALT_BARO, IB_GLIDE, IB_GND_SPEED };
uint8_t g_centerConfigClimb = CENTER_THERMAL_HELPER;
uint8_t g_centerConfigCruise = CENTER_WIND_DIR;
static bool    g_ibEditCruiseMode  = true;
static uint8_t* g_infoBoxConfig    = g_ibConfigCruise;

typedef enum { IBEDIT_NONE=0, IBEDIT_SELECT_MODE=1, IBEDIT_SELECT_ZONE=2, IBEDIT_CHOOSE_METRIC=3 } InfoBoxEditState;
static InfoBoxEditState g_ibEditState = IBEDIT_NONE;
static int s_ibZoneSel = 0;
static lv_obj_t* s_ibFrames[7]    = {0};
static lv_obj_t* s_ibLabels[6]    = {0};
static lv_obj_t* s_ibValLabels[7] = {0};

static const char* const s_ibMetricAbbrev[IB_METRIC_MAX] = {
  "Inst. Vario","Avg. Vario","MacCready","Baro Alt.","GPS Alt.","Airspeed","Gnd Speed",
  "Time","Flight Time","Wind","Climb Gain","Flight Lvl","Glide Ratio","",
  "Netto","STF","Alerts","Mode"
};
/* Meme liste, pour la zone 5 (pod 62px) seulement : voir le commentaire dans main.cpp. */
static const char* const s_ibMetricAbbrevTiny[IB_METRIC_MAX] = {
  "Vario","AvgV","MC","Alt","GAlt","IAS","GS",
  "Time","FTime","Wind","Gain","FL","L/D","",
  "Netto","STF","Alert","Mode"
};
static const char* const s_centerMetricAbbrev[CENTER_METRIC_MAX] = { "Thermal Help", "Wind Dir.", "" };

/* ---- Glider polar database + profils ---- */
typedef struct {
  const char* name;
  int empty_wt; int max_bal;
  int v1; float si1; int v2; float si2; int v3; float si3;
} GliderData;
#include "GliderPolars.h"
static GliderData g_gliderDb[300];
static int g_gliderDbCount = 0;
static char g_gliderNamesBuf[300 * 32];
static int g_gliderNamesBufOffset = 0;

static void GliderDB_LoadDefault(void) {
  g_gliderDbCount = GLIDER_POLARS_COUNT;
  g_gliderNamesBufOffset = 0;
  for (int i = 0; i < g_gliderDbCount; i++) {
    g_gliderDb[i] = GLIDER_POLARS[i];
    int len = (int)strlen(GLIDER_POLARS[i].name);
    if (g_gliderNamesBufOffset + len + 1 < (int)sizeof(g_gliderNamesBuf)) {
      strcpy(&g_gliderNamesBuf[g_gliderNamesBufOffset], GLIDER_POLARS[i].name);
      g_gliderDb[i].name = &g_gliderNamesBuf[g_gliderNamesBufOffset];
      g_gliderNamesBufOffset += len + 1;
    }
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
static char  g_profileName[16] = "P1";

/* Accesseurs pour g_gliderDb (reste static -- utilise par /api/gliders dans sim_server.c) */
int Glider_Count(void) { return g_gliderDbCount; }
const char* Glider_Name(int i)   { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].name : ""; }
int   Glider_EmptyWt(int i) { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].empty_wt : 0; }
int   Glider_MaxBal(int i)  { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].max_bal  : 0; }
int   Glider_V1(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v1  : 0; }
float Glider_Si1(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si1 : 0; }
int   Glider_V2(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v2  : 0; }
float Glider_Si2(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si2 : 0; }
int   Glider_V3(int i)      { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].v3  : 0; }
float Glider_Si3(int i)     { return (i >= 0 && i < g_gliderDbCount) ? g_gliderDb[i].si3 : 0; }

/* Profils : persistance en RAM seulement (pas de NVS sur PC) */
typedef struct {
  bool used; char name[16];
  int gliderIdx, emptyWt, maxBal, v1; float si1; int v2; float si2; int v3; float si3;
} ProfileRec;
static ProfileRec g_profiles[5] = {0};

void Profile_Load(int idx) {
  ProfileRec* p = &g_profiles[idx];
  if (!p->used) return;
  g_gliderIdx = p->gliderIdx; g_gliderEmptyWt = p->emptyWt; g_gliderMaxBal = p->maxBal;
  g_gliderV1 = p->v1; g_gliderSi1 = p->si1; g_gliderV2 = p->v2; g_gliderSi2 = p->si2;
  g_gliderV3 = p->v3; g_gliderSi3 = p->si3;
}
void Profile_Save(int idx) {
  ProfileRec* p = &g_profiles[idx];
  p->gliderIdx = g_gliderIdx; p->emptyWt = g_gliderEmptyWt; p->maxBal = g_gliderMaxBal;
  p->v1 = g_gliderV1; p->si1 = g_gliderSi1; p->v2 = g_gliderV2; p->si2 = g_gliderSi2;
  p->v3 = g_gliderV3; p->si3 = g_gliderSi3;
}
void Profile_Delete(int idx) {
  memset(&g_profiles[idx], 0, sizeof(ProfileRec));
  if (idx == g_profileIdx) Profile_Load(idx);
}

bool Profile_IsUsed(int idx) { return g_profiles[idx].used && g_profiles[idx].name[0] != 0; }

/* Ecrit/lit le nom d'un profil independamment de Profile_Save (identique a main.cpp,
 * utilise par /api/profiles dans sim_server.c). */
void Profile_SetName(int idx, const char* name) {
  strncpy(g_profiles[idx].name, name, sizeof(g_profiles[idx].name) - 1);
  g_profiles[idx].name[sizeof(g_profiles[idx].name) - 1] = 0;
  g_profiles[idx].used = (name[0] != 0);
}
void Profile_GetName(int idx, char* out, size_t outLen) {
  strncpy(out, g_profiles[idx].used ? g_profiles[idx].name : "", outLen - 1);
  out[outLen - 1] = 0;
}
void Profile_RefreshName(void) {
  Profile_GetName(g_profileIdx, g_profileName, sizeof(g_profileName));
  if (g_profileName[0] == 0) snprintf(g_profileName, sizeof(g_profileName), "Empty");
}

/* Fait defiler "Profile" en ne montrant que les profils reellement nommes (identique
 * a main.cpp, corrige 3 juillet 2026) -- masque les emplacements "Empty" en navigation
 * normale, "New" reste le seul moyen d'en atteindre un vide pour lui donner un nom. */
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

void Config_Save(void) { /* no-op : pas de NVS sur PC, tout reste en RAM */ }
static void Config_Load(void) { GliderDB_LoadDefault(); }

/* ============================================================
 *  ARBRE DE MENU (donnees) -- identique a main.cpp
 * ============================================================ */
enum { SM_ROOT, SM_VARIO, SM_SOUND, SM_DISPLAY, SM_SYSTEM, SM_INFOBOX, SM_UNITS, SM_ABOUT, SM_GLIDER, SM_PROFILE, SM_INFOBOX_METRIC, SM_N };
enum { ST_SUB, ST_TOGGLE, ST_VALUE, ST_CHOICE, ST_INFO, ST_BACK };
enum { SET_NONE, SET_HELPER, SET_BRIGHT, SET_VOLUME, SET_SINK, SET_LOGGER, SET_CONDOR, SET_RANGE,
       SET_ROT, SET_U_VERT, SET_U_ALT, SET_U_SPEED, SET_PITCH, SET_WAVE, SET_TONE_SPREAD,
       SET_VFILTER, SET_VAVG, SET_UPDATE, SET_CONDORSIM, SET_FWVER, SET_BUILD, SET_LINKVER, SET_CREATOR, SET_ALGO,
       SET_APPCONNECT, SET_SHOW_QR, SET_RESET_CFG, SET_FACTORY_RESET,
       SET_GLIDER_MODEL, SET_GLIDER_EMPTY_WT, SET_GLIDER_MAX_BAL, SET_GLIDER_V1, SET_GLIDER_SI1, SET_GLIDER_V2, SET_GLIDER_SI2, SET_GLIDER_V3, SET_GLIDER_SI3,
       SET_PROFILE_SELECT, SET_PROFILE_EDIT, SET_PROFILE_NEW, SET_PROFILE_SAVE, SET_PROFILE_DELETE };

#define LIM_FW_SCREEN "0.8.0"
#define LIM_VERSION 4

typedef struct { const char* label; uint8_t type; uint8_t arg; } SmItem;
typedef struct { const char* title; const SmItem* items; uint8_t n; } SmMenu;

static const SmItem RIT[]  = { {"Display",ST_SUB,SM_DISPLAY},{"Sound",ST_SUB,SM_SOUND},{"Vario",ST_SUB,SM_VARIO},{"System",ST_SUB,SM_SYSTEM},{"Glider infos",ST_SUB,SM_GLIDER},{"Profile",ST_SUB,SM_PROFILE},{"Exit",ST_BACK,0} };
static const SmItem VIT[]  = { {"Vario range",ST_CHOICE,SET_RANGE},{"Vario filter",ST_CHOICE,SET_VFILTER},{"Avg climb",ST_CHOICE,SET_VAVG},{"Back",ST_BACK,0} };
static const SmItem SIT[]  = { {"Tone pitch",ST_VALUE,SET_PITCH},{"Waveform",ST_CHOICE,SET_WAVE},{"Tone spread",ST_VALUE,SET_TONE_SPREAD},{"Back",ST_BACK,0} };
static const SmItem DIT[]  = { {"Info boxes",ST_SUB,SM_INFOBOX},{"Units",ST_SUB,SM_UNITS},{"Brightness",ST_VALUE,SET_BRIGHT},{"Screen rot.",ST_CHOICE,SET_ROT},{"Back",ST_BACK,0} };
static const SmItem SYIT[] = {
  {"App connect",   ST_TOGGLE, SET_APPCONNECT}, {"Show QR Code", ST_INFO, SET_SHOW_QR},
  {"Condor sim",    ST_TOGGLE, SET_CONDORSIM},
  {"Reset config",  ST_INFO,   SET_RESET_CFG},  {"Factory reset", ST_INFO, SET_FACTORY_RESET},
  {"About", ST_SUB, SM_ABOUT}, {"Back", ST_BACK, 0}
};
static const SmItem ABT[]  = { {"Version",ST_INFO,SET_FWVER},{"Build",ST_INFO,SET_BUILD},{"Link Prot.",ST_INFO,SET_LINKVER},{"Back",ST_BACK,0} };
static const SmItem IBIT_MODE[] = { {"Climb Mode",ST_INFO,0},{"Cruise Mode",ST_INFO,1},{"Back",ST_BACK,0} };
static const SmItem IBIT_LIST[] = {
  {"Inst. Vario", ST_INFO, IB_VARIO_INST}, {"Avg. Vario", ST_INFO, IB_VARIO_INT}, {"MacCready", ST_INFO, IB_MACCREADY},
  {"Baro Alt.", ST_INFO, IB_ALT_BARO}, {"GPS Alt.", ST_INFO, IB_ALT_GPS}, {"Time", ST_INFO, IB_TIME},
  {"Flight Time", ST_INFO, IB_FLIGHT_TIME}, {"Wind", ST_INFO, IB_WIND}, {"Climb Gain", ST_INFO, IB_CLIMB_GAIN},
  {"Flight Level", ST_INFO, IB_FLIGHT_LVL}, {"Glide Ratio", ST_INFO, IB_GLIDE}, {"Airspeed", ST_INFO, IB_AIRSPEED},
  {"Ground Speed", ST_INFO, IB_GND_SPEED}, {"Speed to Fly", ST_INFO, IB_STF},
  {"Alerts", ST_INFO, IB_ALERTS}, {"Mode", ST_INFO, IB_MODE},
  {"Disabled", ST_INFO, IB_EMPTY},
  {"Back", ST_BACK, 0}
};
static const SmItem CI_LIST[] = { {"Thermal Helper",ST_INFO,0},{"Wind Direction",ST_INFO,1},{"Disabled",ST_INFO,2},{"Back",ST_BACK,0} };
static const SmItem UIT[]  = { {"Vertical",ST_CHOICE,SET_U_VERT},{"Altitude",ST_CHOICE,SET_U_ALT},{"Speed",ST_CHOICE,SET_U_SPEED},{"Back",ST_BACK,0} };
static const SmItem GLIT[] = {
  {"Glider",ST_CHOICE,SET_GLIDER_MODEL},{"Empty weight",ST_VALUE,SET_GLIDER_EMPTY_WT},{"Max ballast",ST_VALUE,SET_GLIDER_MAX_BAL},
  {"Polar V1",ST_VALUE,SET_GLIDER_V1},{"Polar Si1",ST_VALUE,SET_GLIDER_SI1},{"Polar V2",ST_VALUE,SET_GLIDER_V2},
  {"Polar Si2",ST_VALUE,SET_GLIDER_SI2},{"Polar V3",ST_VALUE,SET_GLIDER_V3},{"Polar Si3",ST_VALUE,SET_GLIDER_SI3},{"Back",ST_BACK,0}
};
static const SmItem PRIT[] = { {"Profile",ST_CHOICE,SET_PROFILE_SELECT},{"Edit",ST_INFO,SET_PROFILE_EDIT},{"New",ST_INFO,SET_PROFILE_NEW},{"Save",ST_INFO,SET_PROFILE_SAVE},{"Delete",ST_INFO,SET_PROFILE_DELETE},{"Back",ST_BACK,0} };

static const SmMenu SM[SM_N] = {
  {"Settings",RIT,7},{"Vario",VIT,4},{"Sound",SIT,4},{"Display",DIT,5},
  {"System",SYIT,7},{"Info Boxes",IBIT_MODE,3},{"Units",UIT,4},{"About",ABT,4},
  {"Glider infos",GLIT,10},{"Profile",PRIT,6},{"Select Metric",IBIT_LIST,18}
};

static uint8_t g_smMenu = SM_ROOT;
static int8_t  g_smSel  = 0;
static uint8_t g_smStk[6]; static int8_t g_smStkSel[6]; static int g_smDepth = 0;
static bool    g_smEdit = false;
static bool    g_smDirty = true;

static lv_obj_t* s_smVal[7] = {0};
static lv_obj_t* s_dName[5] = {0};  static lv_obj_t* s_dVal[5]  = {0};
static lv_obj_t* s_uName[4] = {0};  static lv_obj_t* s_uVal[4]  = {0};
static lv_obj_t* s_sName[4] = {0};  static lv_obj_t* s_sVal[4]  = {0};
static lv_obj_t* s_vName[4] = {0};  static lv_obj_t* s_vVal[4]  = {0};
static lv_obj_t* s_syName[7] = {0}; static lv_obj_t* s_syVal[7]  = {0};
static lv_obj_t* s_abName[4] = {0}; static lv_obj_t* s_abVal[4]  = {0};
static lv_obj_t* s_glName[10] = {0};static lv_obj_t* s_glVal[10]  = {0};
static lv_obj_t* s_prName[6]  = {0};static lv_obj_t* s_prVal[6]   = {0};
static lv_obj_t* s_imName[3] = {0}; static lv_obj_t* s_imVal[3] = {0};
static lv_obj_t* s_ibListNames[18] = {0}; static lv_obj_t* s_ibListVals[18] = {0};
static lv_obj_t* s_ciListNames[4] = {0};  static lv_obj_t* s_ciListVals[4] = {0};

static lv_obj_t* s_confirmPanel = NULL;
static lv_obj_t* s_confirmMsg   = NULL;
static lv_obj_t* s_confirmYes   = NULL;
static lv_obj_t* s_confirmNo    = NULL;
static int8_t g_smConfirm  = -1;
static bool   g_confirmSel = false;
static bool   g_infoOpen   = false;  /* popup d'info a 1 bouton, reutilise confirm_panel */
static uint32_t g_infoShownMs = 0;
#define INFO_POPUP_MS 1800

/* Editeur de nom de profil : 5 cases de caractere + cases OK/Cancel, navigation
 * 100% encodeur (remplace l'ancien clavier LVGL AZERTY -- identique a main.cpp). */
static lv_obj_t* s_pnContainer = NULL;
static lv_obj_t* s_pnBox[5]    = {0};
static lv_obj_t* s_pnSlot[5]   = {0};
static lv_obj_t* s_pnOkBox     = NULL;
static lv_obj_t* s_pnCancelBox = NULL;
static lv_obj_t* s_pnWarn      = NULL;
static char      s_pnBuf[6]    = {0};
static int8_t    s_pnCursor    = 0;
static bool      s_pnCharEdit  = false;

/* Espace en premier = case "vide" (permet un nom < 5 caracteres). */
static const char PN_CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
#define PN_CHARSET_LEN ((int)(sizeof(PN_CHARSET) - 1))

/* Forward declarations (ordre d'appel croise) */
static void SetupMenu_Back(void);
static void SetupMenu_HideLists(void);
static void InfoBox_ShowSelect(void);
static void InfoBox_CloseEdit(void);
static void Confirm_Show(int8_t action);
static void Confirm_Hide(void);
static void Info_Show(const char* msg);
static void Info_Hide(void);
static void Profile_ShowKeyboard(bool isNew);
static void QrScreen_Show(void);

static bool ProfileName_IsDuplicate(const char* candidate) {
  for (int i = 0; i < 5; i++) {
    if (i == g_profileIdx || !g_profiles[i].used) continue;
    const char* a = g_profiles[i].name;
    const char* b = candidate;
    bool eq = true;
    while (*a && *b) { if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) { eq = false; break; } a++; b++; }
    if (eq && *a == 0 && *b == 0) return true;
  }
  return false;
}

static void ProfileName_Render(void) {
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

static void ProfileName_Close(void) {
  if (s_pnContainer) { lv_obj_del(s_pnContainer); s_pnContainer = NULL; }
  for (int i = 0; i < 5; i++) { s_pnBox[i] = NULL; s_pnSlot[i] = NULL; }
  s_pnOkBox = NULL; s_pnCancelBox = NULL; s_pnWarn = NULL;
  g_smDirty = true;
}

static void ProfileName_Confirm(void) {
  char out[6];
  memcpy(out, s_pnBuf, 6);
  for (int i = 4; i >= 0; i--) { if (out[i] == ' ') out[i] = 0; else break; }
  if (strlen(out) == 0) { ProfileName_Close(); return; }
  if (ProfileName_IsDuplicate(out)) {
    if (s_pnWarn) { lv_label_set_text(s_pnWarn, "Name already exists"); lv_obj_clear_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN); }
    return;
  }
  Profile_SetName(g_profileIdx, out);
  strncpy(g_profileName, out, sizeof(g_profileName) - 1);
  Profile_Save(g_profileIdx);
  ProfileName_Close();
}

static void Profile_ShowKeyboard(bool isNew) {
  if (s_pnContainer) return;
  if (isNew) {
    int found = -1;
    for (int i = 0; i < 5; i++) if (!g_profiles[i].used) { found = i; break; }
    g_profileIdx = (found != -1) ? found : (g_profileIdx + 1) % 5;
    Profile_Load(g_profileIdx);
  }

  const char* src;
  char defaultName[16];
  if (g_profiles[g_profileIdx].used && !isNew) {
    src = g_profiles[g_profileIdx].name;
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

static void DbgLog(const char* msg);  /* debug temporaire, definie plus bas */

static void SmToggle(uint8_t s) {
  switch (s) {
    case SET_HELPER:     g_helperEnable = !g_helperEnable; break;
    case SET_SINK:       g_sinkSound = !g_sinkSound; break;
    case SET_LOGGER:     g_loggerEnable = !g_loggerEnable; break;
    case SET_UPDATE:     g_updateMode = !g_updateMode; break;
    case SET_CONDORSIM:  g_condorSim  = !g_condorSim;  break;
    case SET_APPCONNECT: DbgLog("SmToggle SET_APPCONNECT AVANT"); FlightLog_ServerToggle(); g_updateMode = FlightLog_ServerActive(); DbgLog("SmToggle SET_APPCONNECT APRES"); break;
  }
  Config_Save();
}

static void SmAdjust(uint8_t s, long d) {
  switch (s) {
    case SET_BRIGHT: g_brightness += (int)d; if (g_brightness<0) g_brightness=0; if (g_brightness>20) g_brightness=20; break;
    case SET_VOLUME: g_volume += (int)d; if (g_volume<0) g_volume=0; if (g_volume>20) g_volume=20; break;
    case SET_RANGE:  g_varioRange = (g_varioRange == 5) ? 10 : 5;
                     if (screen_main_state.scale) lv_meter_set_scale_range(objects.vario_meter, screen_main_state.scale, -g_varioRange*1000, g_varioRange*1000, 250, 55); break;
    case SET_ROT:    g_screenRot = (g_screenRot + (d > 0 ? 90 : 270)) % 360; break;
    case SET_U_VERT: g_uVert  ^= 1; break;
    case SET_U_ALT:  g_uAlt   ^= 1; break;
    case SET_U_SPEED:g_uSpeed ^= 1; break;
    case SET_PITCH:  g_tonePitch += (int)d * 50; if (g_tonePitch<200) g_tonePitch=200; if (g_tonePitch>1500) g_tonePitch=1500; break;
    case SET_WAVE:   g_waveform = (uint8_t)((g_waveform + (d > 0 ? 1 : 2)) % 3); break;
    case SET_TONE_SPREAD: g_toneSpread += (int)d; if (g_toneSpread<0) g_toneSpread=0; if (g_toneSpread>10) g_toneSpread=10; break;
    case SET_VFILTER: g_varioFilter = (uint8_t)((g_varioFilter + (d > 0 ? 1 : 2)) % 3); break;
    case SET_VAVG:    g_avgClimb    = (uint8_t)((g_avgClimb    + (d > 0 ? 1 : 2)) % 3); break;
    case SET_GLIDER_MODEL:
      g_gliderIdx = (g_gliderIdx + (int)d + g_gliderDbCount) % g_gliderDbCount;
      g_gliderEmptyWt = g_gliderDb[g_gliderIdx].empty_wt; g_gliderMaxBal = g_gliderDb[g_gliderIdx].max_bal;
      g_gliderV1 = g_gliderDb[g_gliderIdx].v1; g_gliderSi1 = g_gliderDb[g_gliderIdx].si1;
      g_gliderV2 = g_gliderDb[g_gliderIdx].v2; g_gliderSi2 = g_gliderDb[g_gliderIdx].si2;
      g_gliderV3 = g_gliderDb[g_gliderIdx].v3; g_gliderSi3 = g_gliderDb[g_gliderIdx].si3;
      break;
    case SET_GLIDER_EMPTY_WT: g_gliderEmptyWt += (int)d * 5; if (g_gliderEmptyWt<100) g_gliderEmptyWt=100; if (g_gliderEmptyWt>1000) g_gliderEmptyWt=1000; break;
    case SET_GLIDER_MAX_BAL:  g_gliderMaxBal  += (int)d * 5; if (g_gliderMaxBal<0) g_gliderMaxBal=0; if (g_gliderMaxBal>500) g_gliderMaxBal=500; break;
    case SET_GLIDER_V1:       g_gliderV1 += (int)d; if (g_gliderV1<40) g_gliderV1=40; if (g_gliderV1>300) g_gliderV1=300; break;
    case SET_GLIDER_SI1:      g_gliderSi1 += (float)d * 0.01f; break;
    case SET_GLIDER_V2:       g_gliderV2 += (int)d; if (g_gliderV2<40) g_gliderV2=40; if (g_gliderV2>300) g_gliderV2=300; break;
    case SET_GLIDER_SI2:      g_gliderSi2 += (float)d * 0.01f; break;
    case SET_GLIDER_V3:       g_gliderV3 += (int)d; if (g_gliderV3<40) g_gliderV3=40; if (g_gliderV3>300) g_gliderV3=300; break;
    case SET_GLIDER_SI3:      g_gliderSi3 += (float)d * 0.01f; break;
    case SET_PROFILE_SELECT:
      Profile_SelectNext((int)d);
      break;
  }
}

static void SmValTxt(uint8_t s, char* b, int n) {
  switch (s) {
    case SET_HELPER: snprintf(b, n, "%s", g_helperEnable ? "ON" : "OFF"); break;
    case SET_SINK:   snprintf(b, n, "%s", g_sinkSound   ? "ON" : "OFF"); break;
    case SET_LOGGER: snprintf(b, n, "%s", g_loggerEnable ? "ON" : "OFF"); break;
    case SET_BRIGHT: snprintf(b, n, "%d", g_brightness); break;
    case SET_VOLUME: snprintf(b, n, "%d", g_volume); break;
    case SET_RANGE:  snprintf(b, n, "+/-%d", g_varioRange); break;
    case SET_ROT:    snprintf(b, n, "%d", g_screenRot); break;
    case SET_U_VERT: snprintf(b, n, "%s", g_uVert  ? "kt"   : "m/s");  break;
    case SET_U_ALT:  snprintf(b, n, "%s", g_uAlt   ? "ft"   : "m");    break;
    case SET_U_SPEED:snprintf(b, n, "%s", g_uSpeed ? "kt"   : "km/h"); break;
    case SET_PITCH:  snprintf(b, n, "%d Hz", g_tonePitch); break;
    case SET_WAVE:   snprintf(b, n, "%s", g_waveform == 0 ? "Sine" : (g_waveform == 1 ? "Square" : "Triangle")); break;
    case SET_TONE_SPREAD: snprintf(b, n, "%d", g_toneSpread); break;
    case SET_VFILTER:snprintf(b, n, "%s", g_varioFilter == 0 ? "Fast" : (g_varioFilter == 1 ? "Med" : "Slow")); break;
    case SET_VAVG:   snprintf(b, n, "%ds", g_avgClimb == 0 ? 15 : (g_avgClimb == 1 ? 20 : 30)); break;
    case SET_UPDATE:    snprintf(b, n, "%s", g_updateMode ? "ON" : "OFF"); break;
    case SET_CONDORSIM: snprintf(b, n, "%s", g_condorSim  ? "ON" : "OFF"); break;
    case SET_APPCONNECT:snprintf(b, n, "%s", FlightLog_ServerActive() ? "ON" : "OFF"); break;
    case SET_FWVER:     snprintf(b, n, "v%s", LIM_FW_SCREEN); break;
    case SET_BUILD:     snprintf(b, n, "%s %s", __DATE__, __TIME__); break;
    case SET_LINKVER:   snprintf(b, n, "v%d", LIM_VERSION); break;
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

/* ============================================================
 *  EDITEUR INFOBOX INTERACTIF
 * ============================================================ */
static void InfoBox_RenderSelect(void) {
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
        lv_label_set_text(s_ibValLabels[i], (i == 5) ? s_ibMetricAbbrevTiny[mIdx] : s_ibMetricAbbrev[mIdx]);
      }
      if (s_ibFrames[i]) lv_obj_align_to(s_ibValLabels[i], s_ibFrames[i], LV_ALIGN_CENTER, 0, 0);
    }
  }
}
static void InfoBox_ShowSelect(void) {
  g_ibEditState = IBEDIT_SELECT_ZONE;
  if (objects.setup_panel) lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_editor_container) lv_obj_clear_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  InfoBox_RenderSelect();
}
static void InfoBox_CloseEdit(void) {
  g_ibEditState = IBEDIT_NONE;
  if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  if (objects.setup_panel) lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
  if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
  else { g_smMenu = SM_ROOT; }
  g_smDirty = true;
}

static void SetupMenu_Open(void)  { g_setupOpen = true; g_menuState = MENU_CLOSED; g_menuDirty = true;
                                g_smMenu = SM_ROOT; g_smSel = 0; g_smDepth = 0; g_smEdit = false; g_smDirty = true; }
static void SetupMenu_Close(void) {
  if (g_smConfirm != -1) { g_smConfirm = -1; lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN); }
  if (g_infoOpen) Info_Hide();
  if (g_ibEditState != IBEDIT_NONE) { InfoBox_CloseEdit(); }
  if (s_pnContainer) ProfileName_Close();
  g_setupOpen = false; g_smEdit = false; g_smDirty = true; Config_Save();
}
static void SetupMenu_Back(void)  {
  if (g_infoOpen) { Info_Hide(); return; }
  if (g_smConfirm != -1) { g_smConfirm = -1; lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN); g_smDirty = true; return; }
  if (g_smMenu == SM_INFOBOX_METRIC || g_ibEditState == IBEDIT_CHOOSE_METRIC) {
    g_ibEditState = IBEDIT_SELECT_ZONE;
    if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
    InfoBox_ShowSelect();
    return;
  }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) { InfoBox_CloseEdit(); return; }
  if (g_smEdit) { g_smEdit = false; Config_Save(); }
  else if (g_smDepth > 0) { g_smDepth--; g_smMenu = g_smStk[g_smDepth]; g_smSel = g_smStkSel[g_smDepth]; }
  else SetupMenu_Close();
  g_smDirty = true;
}

static void Confirm_Render(void) {
  if (!s_confirmYes || !s_confirmNo) return;
  if (g_confirmSel) {
    lv_obj_set_style_text_color(s_confirmYes, lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_confirmNo,  lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (objects.confirm_panel_selection) lv_obj_set_x(objects.confirm_panel_selection, 47);
  } else {
    lv_obj_set_style_text_color(s_confirmYes, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_confirmNo,  lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (objects.confirm_panel_selection) lv_obj_set_x(objects.confirm_panel_selection, 195);
  }
}
static void Confirm_Show(int8_t action) {
  g_smConfirm  = action;
  g_confirmSel = false;
  Confirm_Render();
  if (s_confirmMsg) {
    if (action == SET_RESET_CFG)      lv_label_set_text(s_confirmMsg, "Reset config?");
    else if (action == SET_FACTORY_RESET) lv_label_set_text(s_confirmMsg, "Factory reset?");
    else if (action == SET_PROFILE_DELETE) lv_label_set_text(s_confirmMsg, "Delete profile?");
  }
  lv_obj_clear_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_confirmPanel);
}
static void Confirm_Hide(void) {
  g_smConfirm = -1;
  if (s_confirmPanel) lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);
  g_smDirty = true;
}
static void Info_Show(const char* msg) {
  g_infoOpen = true;
  g_infoShownMs = millis();
  if (s_confirmMsg) { lv_label_set_text(s_confirmMsg, msg); lv_obj_align(s_confirmMsg, LV_ALIGN_CENTER, 0, 0); }
  if (s_confirmYes) lv_obj_add_flag(s_confirmYes, LV_OBJ_FLAG_HIDDEN);
  if (s_confirmNo)  lv_obj_add_flag(s_confirmNo,  LV_OBJ_FLAG_HIDDEN);
  if (objects.confirm_panel_selection) lv_obj_add_flag(objects.confirm_panel_selection, LV_OBJ_FLAG_HIDDEN);
  if (s_confirmPanel) { lv_obj_clear_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(s_confirmPanel); }
}
static void Info_Hide(void) {
  g_infoOpen = false;
  if (s_confirmMsg) lv_obj_set_pos(s_confirmMsg, -41, 25);
  if (s_confirmYes) lv_obj_clear_flag(s_confirmYes, LV_OBJ_FLAG_HIDDEN);
  if (s_confirmNo)  lv_obj_clear_flag(s_confirmNo,  LV_OBJ_FLAG_HIDDEN);
  if (objects.confirm_panel_selection) lv_obj_clear_flag(objects.confirm_panel_selection, LV_OBJ_FLAG_HIDDEN);
  if (s_confirmPanel) lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);
  g_smDirty = true;
}
static void Info_Tick(void) {
  if (g_infoOpen && millis() - g_infoShownMs > INFO_POPUP_MS) Info_Hide();
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
  if (g_infoOpen) return;
  if (g_smConfirm != -1) { g_confirmSel = !g_confirmSel; Confirm_Render(); return; }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) {
    // Zone 5 (status pod) activated 19 July 2026 -- must stay aligned with main.cpp
    static const int IB_ZONE_SEQ[] = {0, 1, 2, 3, 4, 5, 6};
    const int IB_ZONE_SEQ_N = 7;
    int pos = 0;
    for (int k = 0; k < IB_ZONE_SEQ_N; k++) if (IB_ZONE_SEQ[k] == s_ibZoneSel) { pos = k; break; }
    pos = ((pos + (int)d) % IB_ZONE_SEQ_N + IB_ZONE_SEQ_N) % IB_ZONE_SEQ_N;
    s_ibZoneSel = IB_ZONE_SEQ[pos];
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

static void SetupMenu_Press(void) {
  if (s_pnContainer) {
    if (s_pnWarn) lv_obj_add_flag(s_pnWarn, LV_OBJ_FLAG_HIDDEN);
    if (s_pnCursor == 5) { ProfileName_Confirm(); return; }
    if (s_pnCursor == 6) { ProfileName_Close(); return; }
    s_pnCharEdit = !s_pnCharEdit;
    ProfileName_Render();
    return;
  }
  if (g_infoOpen) { Info_Hide(); return; }
  if (g_smConfirm != -1) {
    if (g_confirmSel) {
      if (g_smConfirm == (int8_t)SET_RESET_CFG || g_smConfirm == (int8_t)SET_FACTORY_RESET) {
        g_brightness = 20; g_helperEnable = true; g_loggerEnable = true;
        g_varioRange = 5; g_screenRot = 0; g_uVert = 0; g_uAlt = 0; g_uSpeed = 0;
        g_tonePitch = 700; g_waveform = 0; g_toneSpread = 5;
        g_varioFilter = 1; g_avgClimb = 1; g_updateMode = false; g_condorSim = false;
        Config_Save();
      } else if (g_smConfirm == (int8_t)SET_PROFILE_DELETE) {
        Profile_Delete(g_profileIdx);
      }
    }
    Confirm_Hide();
    return;
  }
  if (g_smMenu == SM_INFOBOX && g_ibEditState == IBEDIT_NONE) {
    if (g_smSel == 2) { SetupMenu_Back(); }
    else {
      g_ibEditCruiseMode = (g_smSel == 1);
      g_infoBoxConfig = g_ibEditCruiseMode ? g_ibConfigCruise : g_ibConfigClimb;
      InfoBox_ShowSelect();
    }
    return;
  }
  if (g_ibEditState == IBEDIT_SELECT_ZONE) {
    if (s_ibZoneSel == 6) { InfoBox_CloseEdit(); return; }
    g_ibEditState = IBEDIT_CHOOSE_METRIC;
    if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
    if (objects.setup_panel) lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
    g_smStk[g_smDepth] = g_smMenu; g_smStkSel[g_smDepth] = g_smSel; g_smDepth++;
    g_smMenu = SM_INFOBOX_METRIC;
    int curVal = (s_ibZoneSel == 2) ? (g_ibEditCruiseMode ? g_centerConfigCruise : g_centerConfigClimb) : g_infoBoxConfig[s_ibZoneSel];
    if (s_ibZoneSel == 2) {
      g_smSel = (curVal >= 0 && curVal < 3) ? curVal : 0;
    } else {
      const SmMenu* mm = &SM[SM_INFOBOX_METRIC];
      int found = 0;
      for (int k = 0; k < mm->n; k++) {
        if (mm->items[k].type == ST_INFO && mm->items[k].arg == curVal) { found = k; break; }
      }
      g_smSel = (int8_t)found;
    }
    g_smDirty = true;
    return;
  }
  if (g_smMenu == SM_INFOBOX_METRIC) {
    int maxIdx = (s_ibZoneSel == 2) ? 3 : (SM[SM_INFOBOX_METRIC].n - 1);
    if (g_smSel == maxIdx) { SetupMenu_Back(); return; }
    if (s_ibZoneSel == 2) {
      if (g_ibEditCruiseMode) g_centerConfigCruise = (uint8_t)g_smSel;
      else g_centerConfigClimb = (uint8_t)g_smSel;
    } else {
      g_infoBoxConfig[s_ibZoneSel] = (uint8_t)SM[SM_INFOBOX_METRIC].items[g_smSel].arg;
    }
    Config_Save();
    SetupMenu_Back();
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
      if (it->arg == SET_PROFILE_NEW || it->arg == SET_PROFILE_EDIT) { Profile_ShowKeyboard(it->arg == SET_PROFILE_NEW); return; }
      if (it->arg == SET_PROFILE_SAVE) { Profile_Save(g_profileIdx); Info_Show("Profile saved"); return; }
      if (it->arg == SET_RESET_CFG || it->arg == SET_FACTORY_RESET || it->arg == SET_PROFILE_DELETE) { Confirm_Show((int8_t)it->arg); return; }
      if (it->arg == SET_SHOW_QR) {
        if (!FlightLog_ServerActive()) { FlightLog_ServerToggle(); g_updateMode = FlightLog_ServerActive(); }
        QrScreen_Show();
        return;
      }
      break;
  }
  g_smDirty = true;
}

static const int SM_ROW_Y[5] = { 88, 143, 198, 253, 308 };

static void SetupMenu_Init(void) {
  lv_obj_t* slots[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  for (int i = 0; i < 7; i++) {
    lv_obj_set_size(slots[i], LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(slots[i], &lv_font_montserrat_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(slots[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    s_smVal[i] = lv_label_create(objects.setup_panel);
    lv_obj_set_style_text_font(s_smVal[i], &lv_font_montserrat_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_smVal[i], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_smVal[i], "");
  }
  for (int i = 0; i < 5; i++) {
    lv_obj_align(slots[i], LV_ALIGN_TOP_MID, 0, SM_ROW_Y[i]);
    lv_obj_align(s_smVal[i], LV_ALIGN_TOP_RIGHT, -50, SM_ROW_Y[i]);
  }
  lv_obj_align(objects.item5, LV_ALIGN_TOP_MID, 0, 307); lv_obj_align(s_smVal[4], LV_ALIGN_TOP_RIGHT, -50, 307);
  lv_obj_align(objects.item6, LV_ALIGN_TOP_MID, 0, 362); lv_obj_align(s_smVal[5], LV_ALIGN_TOP_RIGHT, -50, 362);
  lv_obj_align(objects.item4, LV_ALIGN_TOP_MID, 0, 417); lv_obj_align(s_smVal[6], LV_ALIGN_TOP_RIGHT, -50, 417);
  lv_obj_clear_flag(objects.setup_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(objects.setup_panel, LV_SCROLLBAR_MODE_OFF);

  s_dName[0]=objects.dname0; s_dName[1]=objects.dname1; s_dName[2]=objects.dname2; s_dName[3]=objects.dname3; s_dName[4]=objects.dname4;
  s_dVal[2]=objects.dval2; s_dVal[3]=objects.dval3;
  lv_obj_set_scrollbar_mode(objects.display_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.display_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.display_list, LV_OBJ_FLAG_HIDDEN);

  s_uName[0]=objects.uname0; s_uName[1]=objects.uname1; s_uName[2]=objects.uname2; s_uName[3]=objects.uname4;
  s_uVal[0]=objects.uval0; s_uVal[1]=objects.uval1; s_uVal[2]=objects.uval2;
  lv_obj_set_scrollbar_mode(objects.units_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.units_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.units_list, LV_OBJ_FLAG_HIDDEN);

  s_sName[0]=objects.sname0; s_sName[1]=objects.sname1; s_sName[2]=objects.sname2; s_sName[3]=objects.sname4;
  s_sVal[0]=objects.sval0; s_sVal[1]=objects.sval1; s_sVal[2]=objects.sval2;
  lv_obj_set_scrollbar_mode(objects.sound_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.sound_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.sound_list, LV_OBJ_FLAG_HIDDEN);

  s_vName[0]=objects.vname0; s_vName[1]=objects.vname1; s_vName[2]=objects.vname2; s_vName[3]=objects.vname3;
  s_vVal[0]=objects.vval0; s_vVal[1]=objects.vval1; s_vVal[2]=objects.vval2;
  lv_obj_set_scrollbar_mode(objects.vario_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.vario_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.vario_list, LV_OBJ_FLAG_HIDDEN);

  s_syName[0]=objects.syname0; s_syName[1]=objects.syname2; s_syName[2]=objects.syname1;
  s_syName[3]=objects.syname3_; s_syName[4]=objects.syname4; s_syName[5]=objects.syname5;
  s_syName[6]=objects.syname6;
  lv_obj_set_scrollbar_mode(objects.system_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.system_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.system_list, LV_OBJ_FLAG_HIDDEN);

  s_abName[0]=objects.abname0; s_abName[1]=objects.abname1; s_abName[2]=objects.abname2; s_abName[3]=objects.abname5;
  s_abVal[0]=objects.abval0; s_abVal[1]=objects.abval1; s_abVal[2]=objects.abval2;
  lv_obj_add_flag(objects.about_list, LV_OBJ_FLAG_HIDDEN);

  s_glName[0]=objects.glname0; s_glVal[0]=objects.glval0; s_glName[1]=objects.glname1; s_glVal[1]=objects.glval1;
  s_glName[2]=objects.glname2; s_glVal[2]=objects.glval2; s_glName[3]=objects.glname3; s_glVal[3]=objects.glval3;
  s_glName[4]=objects.glname4; s_glVal[4]=objects.glval4; s_glName[5]=objects.glname5; s_glVal[5]=objects.glval5;
  s_glName[6]=objects.glname6; s_glVal[6]=objects.glval6; s_glName[7]=objects.glname7; s_glVal[7]=objects.glval7;
  s_glName[8]=objects.glname8; s_glVal[8]=objects.glval8; s_glName[9]=objects.abname5_1; s_glVal[9]=NULL;
  lv_obj_set_scrollbar_mode(objects.glider_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.glider_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.glider_list, LV_OBJ_FLAG_HIDDEN);

  s_prName[0]=objects.prname0; s_prVal[0]=objects.prval0; s_prName[1]=objects.prname1; s_prVal[1]=NULL;
  s_prName[2]=objects.prname2; s_prVal[2]=NULL; s_prName[3]=objects.prname3; s_prVal[3]=NULL;
  s_prName[4]=objects.prname4; s_prVal[4]=NULL; s_prName[5]=objects.prname5; s_prVal[5]=NULL;
  lv_obj_set_scrollbar_mode(objects.profil_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(objects.profil_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_flag(objects.profil_list, LV_OBJ_FLAG_HIDDEN);

  if (objects.qr_panel) lv_obj_add_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);

  if (objects.infobox_editor_container) lv_obj_add_flag(objects.infobox_editor_container, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_mode_list)        lv_obj_add_flag(objects.infobox_mode_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.infobox_list)             lv_obj_add_flag(objects.infobox_list, LV_OBJ_FLAG_HIDDEN);
  if (objects.center_info_list)         lv_obj_add_flag(objects.center_info_list, LV_OBJ_FLAG_HIDDEN);

  s_ibFrames[0]=objects.ib_frame_0; s_ibFrames[1]=objects.ib_frame_1; s_ibFrames[2]=objects.ib_frame_2;
  s_ibFrames[3]=objects.ib_frame_3; s_ibFrames[4]=objects.ib_frame_4; s_ibFrames[5]=objects.ib_frame_5;
  s_ibFrames[6]=objects.ib_frame_6;
  s_ibValLabels[0]=objects.ib_val_0; s_ibValLabels[1]=objects.ib_val_1; s_ibValLabels[2]=objects.ib_val_2;
  s_ibValLabels[3]=objects.ib_val_3; s_ibValLabels[4]=objects.ib_val_4;
  s_ibValLabels[5]=objects.ib_val_5;   // zone 5 (status pod), ib_val_5 built in EEZ on 19/07/2026

  s_imName[0]=objects.imname0; s_imName[1]=objects.imname1; s_imName[2]=objects.imname2;
  s_ibListNames[0]=objects.ibname0; s_ibListNames[1]=objects.ibname1; s_ibListNames[2]=objects.ibname2;
  s_ibListNames[3]=objects.ibname3; s_ibListNames[4]=objects.ibname4; s_ibListNames[5]=objects.ibname5;
  s_ibListNames[6]=objects.ibname6; s_ibListNames[7]=objects.ibname7; s_ibListNames[8]=objects.ibname8;
  s_ibListNames[9]=objects.ibname9; s_ibListNames[10]=objects.ibname10; s_ibListNames[11]=objects.ibname11;
  s_ibListNames[12]=objects.ibname13; s_ibListNames[13]=objects.ibname14; s_ibListNames[14]=objects.ibname15;
  s_ibListNames[15]=objects.ibname16; s_ibListNames[16]=objects.ibname17; s_ibListNames[17]=objects.ibname18;

  s_ciListNames[0]=objects.cname0; s_ciListNames[1]=objects.cname1; s_ciListNames[2]=objects.cname2; s_ciListNames[3]=objects.prname5_1;

  if (objects.infobox_mode_list) { lv_obj_set_scrollbar_mode(objects.infobox_mode_list, LV_SCROLLBAR_MODE_OFF); lv_obj_add_flag(objects.infobox_mode_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE); }
  if (objects.infobox_list)      { lv_obj_set_scrollbar_mode(objects.infobox_list, LV_SCROLLBAR_MODE_OFF); lv_obj_add_flag(objects.infobox_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE); }
  if (objects.center_info_list)  { lv_obj_set_scrollbar_mode(objects.center_info_list, LV_SCROLLBAR_MODE_OFF); lv_obj_add_flag(objects.center_info_list, LV_OBJ_FLAG_OVERFLOW_VISIBLE); }

  s_confirmPanel = objects.confirm_panel; s_confirmMsg = objects.confirm_msg;
  s_confirmYes = objects.confirm_yes; s_confirmNo = objects.confirm_no;
  lv_obj_set_scrollbar_mode(s_confirmPanel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_confirmPanel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_bg_opa(objects.setup_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
}

static void SetupMenu_HideLists(void) {
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
  (void)cy; (void)frame_cy;
  if (!obj) return;
  lv_obj_set_style_transform_zoom(obj, 256, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static const lv_coord_t ROOT_BX[7] = { 145, 154, 168, 145, 117, 155, 181 };
static const lv_coord_t ROOT_BY[7] = {  87, 142, 197, 252, 307, 362, 417 };

static void SetupMenu_RenderRoot(void) {
  SetupMenu_HideLists();
  lv_obj_t* it[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  const SmMenu* m = &SM[SM_ROOT];
  for (int i = 0; i < 7; i++) {
    lv_label_set_text(it[i], m->items[i].label);
    lv_obj_set_style_text_color(it[i], lv_color_hex(m->items[i].type == ST_BACK ? 0xff0000 : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(it[i], LV_ALIGN_TOP_MID, 0, ROOT_BY[i]);
    lv_obj_clear_flag(it[i], LV_OBJ_FLAG_HIDDEN);
    if (s_smVal[i]) lv_obj_add_flag(s_smVal[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_update_layout(objects.main);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame_1, &fa);
  lv_obj_get_coords(it[g_smSel], &la);
  lv_coord_t delta = ((la.y1 + la.y2) / 2) - ((fa.y1 + fa.y2) / 2);
  for (int i = 0; i < 7; i++) lv_obj_align(it[i], LV_ALIGN_TOP_MID, 0, ROOT_BY[i] - delta);
  lv_obj_update_layout(objects.main);
  lv_coord_t topY = 85, botY = 460;
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  for (int i = 0; i < 7; i++) {
    lv_obj_get_coords(it[i], &la);
    lv_coord_t cy = (la.y1 + la.y2) / 2;
    if (cy < topY || cy > botY) lv_obj_add_flag(it[i], LV_OBJ_FLAG_HIDDEN);
    else SetupMenu_ApplyItemZoom(it[i], cy, frame_cy);
  }
}

static const lv_coord_t EEZ_ITEM_Y0 = 108;
static const lv_coord_t EEZ_ITEM_STEP = 55;

static void SetupMenu_RenderList(lv_obj_t* container, lv_obj_t** names, lv_obj_t** vals, const SmMenu* m) {
  (void)EEZ_ITEM_Y0; (void)EEZ_ITEM_STEP;
  lv_obj_t* slots[7] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item5, objects.item6, objects.item4 };
  for (int i = 0; i < 7; i++) { lv_obj_add_flag(slots[i], LV_OBJ_FLAG_HIDDEN); if (s_smVal[i]) lv_obj_add_flag(s_smVal[i], LV_OBJ_FLAG_HIDDEN); }
  SetupMenu_HideLists();
  if (!container) return;
  lv_obj_clear_flag(container, LV_OBJ_FLAG_HIDDEN);

  int n = m->n;
  char v[20];
  for (int i = 0; i < n; i++) {
    if (!names[i]) continue;
    lv_obj_clear_flag(names[i], LV_OBJ_FLAG_HIDDEN);
    if (vals[i]) lv_obj_clear_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
    const SmItem* it = &m->items[i];
    lv_obj_set_style_text_color(names[i], lv_color_hex(it->type == ST_BACK ? 0xff0000 : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (vals[i]) {
      if (g_smMenu != SM_ABOUT && (it->type == ST_VALUE || it->type == ST_CHOICE || it->type == ST_TOGGLE || it->type == ST_INFO)) {
        SmValTxt(it->arg, v, sizeof(v));
        lv_label_set_text(vals[i], v);
      }
      bool isOn   = (it->type == ST_TOGGLE && strcmp(v, "ON") == 0);
      bool isEdit = (g_smEdit && i == (int)g_smSel);
      lv_obj_set_style_text_color(vals[i], lv_color_hex((isEdit || isOn) ? 0xfbd500 : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }

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

  lv_obj_update_layout(objects.main);
  lv_coord_t topY = 85, botY = 460;
  lv_area_t la;
  for (int i = 0; i < n; i++) {
    if (!names[i]) continue;
    lv_obj_get_coords(names[i], &la);
    lv_coord_t cy = (la.y1 + la.y2) / 2;
    bool vis = (cy >= topY && cy <= botY);
    if (vis) { lv_obj_clear_flag(names[i], LV_OBJ_FLAG_HIDDEN); SetupMenu_ApplyItemZoom(names[i], cy, frame_cy); }
    else       lv_obj_add_flag(names[i], LV_OBJ_FLAG_HIDDEN);
    if (vals[i]) {
      if (vis) lv_obj_clear_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
      else     lv_obj_add_flag(vals[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void SetupMenu_Apply(void) {
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
  if (g_smMenu == SM_ROOT)    { SetupMenu_RenderRoot(); return; }
  if (g_smMenu == SM_DISPLAY) { SetupMenu_RenderList(objects.display_list, s_dName, s_dVal, m); return; }
  if (g_smMenu == SM_UNITS)   { SetupMenu_RenderList(objects.units_list,   s_uName, s_uVal, m); return; }
  if (g_smMenu == SM_SOUND)   { SetupMenu_RenderList(objects.sound_list,   s_sName, s_sVal, m); return; }
  if (g_smMenu == SM_VARIO)   { SetupMenu_RenderList(objects.vario_list,   s_vName, s_vVal, m); return; }
  if (g_smMenu == SM_SYSTEM)  { SetupMenu_RenderList(objects.system_list,  s_syName, s_syVal, m); return; }
  if (g_smMenu == SM_ABOUT)   { SetupMenu_RenderList(objects.about_list,   s_abName, s_abVal, m); return; }
  if (g_smMenu == SM_GLIDER)  { SetupMenu_RenderList(objects.glider_list,  s_glName, s_glVal, m); return; }
  if (g_smMenu == SM_INFOBOX) {
    if (g_ibEditState == IBEDIT_NONE) { SetupMenu_RenderList(objects.infobox_mode_list, s_imName, s_imVal, m); return; }
    SetupMenu_HideLists();
    if (objects.setup_panel) lv_obj_add_flag(objects.setup_panel, LV_OBJ_FLAG_HIDDEN);
    if (objects.vario_meter)               lv_obj_clear_flag(objects.vario_meter, LV_OBJ_FLAG_HIDDEN);
    if (objects.infobox_display_container) lv_obj_clear_flag(objects.infobox_display_container, LV_OBJ_FLAG_HIDDEN);
    if (objects.img_gps)                   lv_obj_clear_flag(objects.img_gps, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (g_smMenu == SM_INFOBOX_METRIC) {
    if (s_ibZoneSel == 2) {
      SmMenu ciMenu = {"Center Mode", CI_LIST, 4};
      lv_label_set_text(objects.settings, ciMenu.title);
      SetupMenu_RenderList(objects.center_info_list, s_ciListNames, s_ciListVals, &ciMenu);
    } else {
      char title[16];
      int n = (s_ibZoneSel < 2) ? (s_ibZoneSel + 1) : s_ibZoneSel;
      snprintf(title, sizeof(title), "Infobox %d", n);
      lv_label_set_text(objects.settings, title);
      SetupMenu_RenderList(objects.infobox_list, s_ibListNames, s_ibListVals, m);
    }
    return;
  }
  if (g_smMenu == SM_PROFILE) {
    /* SetupMenu_RenderList() ecrit un texte generique "Profile N" sur vals[0] (ST_CHOICE
     * -> SmValTxt(SET_PROFILE_SELECT)) : le nom personnalise doit etre applique APRES,
     * sinon il est aussitot ecrase (meme bug que main.cpp, corrige 3 juillet 2026). */
    SetupMenu_RenderList(objects.profil_list, s_prName, s_prVal, m);
    strncpy(g_profileName, g_profiles[g_profileIdx].used ? g_profiles[g_profileIdx].name : "", sizeof(g_profileName)-1);
    if (g_profileName[0] == 0) snprintf(g_profileName, sizeof(g_profileName), "Empty");
    lv_label_set_text(objects.prval0, g_profileName);
    return;
  }
  SetupMenu_HideLists();
  lv_obj_add_flag(objects.item5, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(objects.item6, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* slots[5] = { objects.item0, objects.item1, objects.item2, objects.item3, objects.item4 };
  lv_obj_update_layout(objects.main);
  lv_area_t fa;
  lv_obj_get_coords(objects.selection_frame_1, &fa);
  lv_coord_t frame_cy = (fa.y1 + fa.y2) / 2;
  static const int SM_ROW_Y5[5] = { 88, 143, 198, 253, 308 };
  for (int row = 0; row < 5; row++) {
    int idx = (int)g_smSel + (row - 2);
    if (idx < 0 || idx >= m->n) { lv_obj_add_flag(slots[row], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_clear_flag(slots[row], LV_OBJ_FLAG_HIDDEN);
    const SmItem* it = &m->items[idx];
    bool hasVal = !(it->type == ST_SUB || it->type == ST_BACK || it->arg == SET_NONE);
    lv_label_set_text(slots[row], it->label);
    lv_obj_set_style_text_color(slots[row], lv_color_hex(it->type == ST_BACK ? 0xff0000 : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (hasVal) {
      lv_obj_align(slots[row], LV_ALIGN_TOP_LEFT, 50, SM_ROW_Y5[row]);
      char v[16]; SmValTxt(it->arg, v, sizeof(v));
      char vb[20]; bool ed = (g_smEdit && idx == (int)g_smSel);
      snprintf(vb, sizeof(vb), ed ? "[%s]" : "%s", v);
      lv_label_set_text(s_smVal[row], vb);
      lv_obj_align(s_smVal[row], LV_ALIGN_TOP_RIGHT, -50, SM_ROW_Y5[row]);
      lv_obj_clear_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(objects.main);
      lv_area_t la; lv_obj_get_coords(slots[row], &la);
      SetupMenu_ApplyItemZoom(slots[row], (la.y1+la.y2)/2, frame_cy);
      SetupMenu_ApplyItemZoom(s_smVal[row], (la.y1+la.y2)/2, frame_cy);
    } else {
      lv_obj_align(slots[row], LV_ALIGN_TOP_MID, 0, SM_ROW_Y5[row]);
      lv_obj_add_flag(s_smVal[row], LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(objects.main);
      lv_area_t la; lv_obj_get_coords(slots[row], &la);
      SetupMenu_ApplyItemZoom(slots[row], (la.y1+la.y2)/2, frame_cy);
    }
  }
}

/* ============================================================
 *  QUICK MENU (rendu)
 * ============================================================ */
static lv_obj_t* Menu_NameLabel(int idx) {
  switch (idx) {
    case 0: return objects.obj4; case 1: return objects.obj3; case 2: return objects.obj2;
    case 3: return objects.obj1; case 4: return objects.obj5; case 5: return objects.obj0;
    case 6: return objects._lbl_exit;
  }
  return objects.obj4;
}

static void Menu_LvglSetup(void) {
  lv_obj_set_pos(objects.item_list, -23, 0);
  lv_obj_set_size(objects.item_list, 360, 345);
  lv_obj_set_scroll_snap_y(objects.item_list, LV_SCROLL_SNAP_NONE);
  lv_obj_set_scrollbar_mode(objects.item_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_top(objects.item_list,    200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(objects.item_list, 200, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_clip_corner(objects.quick_menu_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_y(objects.obj5, 176);
  lv_obj_set_y(objects.obj6, 176);
  lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  // Arc volume compact, deplace dans la zone libre a droite (meme empreinte que la
  // zone 5 "pod" reservee dans EEZ : ib_frame_5, x=369 y=192, 62x96) au lieu d'un
  // gros arc au centre de l'ecran qui masquait tout. Parente sur objects.main (plein
  // ecran, pos 0,0) et non center_hub (344x344 a 68,68) : sinon les coordonnees
  // absolues tombent hors du parent -> invisible (clip silencieux LVGL).
  // Retour a lv_arc (l'echelle graduee lv_meter etait illisible a cette taille et
  // rendait blanc sur blanc) -- juste le chiffre courant au centre, comme avant.
  // 45 = 135 (ancienne rotation) - 90 (demande).
  g_arcVol = lv_arc_create(objects.main);
  lv_obj_set_size(g_arcVol, 65, 65);
  lv_obj_set_pos(g_arcVol, 368, 208);           // centre ~(400,240)
  lv_arc_set_rotation(g_arcVol, 45);
  lv_arc_set_bg_angles(g_arcVol, 0, 270);
  lv_arc_set_range(g_arcVol, 0, 20);
  lv_arc_set_value(g_arcVol, g_volume);
  lv_obj_remove_style(g_arcVol, NULL, LV_PART_KNOB);
  // LVGL donne un fond MAIN opaque blanc par defaut sans theme -- invisible avant (l'arc
  // etait geant, centre sur la partie claire du cadran), flagrant maintenant qu'il est
  // petit sur une zone sombre. Transparent : seul l'anneau doit se voir.
  lv_obj_set_style_bg_opa(g_arcVol, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(g_arcVol, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0xfbd500), LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_color(g_arcVol, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(g_arcVol, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
  lv_obj_set_style_arc_width(g_arcVol, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(g_arcVol, LV_OBJ_FLAG_CLICKABLE);
  g_lblVolNum = lv_label_create(objects.main);
  lv_obj_set_style_bg_opa(g_lblVolNum, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(g_lblVolNum, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(g_lblVolNum, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align_to(g_lblVolNum, g_arcVol, LV_ALIGN_CENTER, 0, 0);
  lv_label_set_text(g_lblVolNum, "10");
  lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
}

static void Menu_Apply(void) {
  if (!g_menuDirty) return;
  g_menuDirty = false;
  if (g_menuState == MENU_CLOSED) { lv_obj_add_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN); return; }

  lv_obj_t* vals[6] = { objects.val_qnh, objects.val_water, objects.val_bugs, objects.val_weight, objects.obj6, objects.val_profil };
  for (int i = 0; i < 6; i++) lv_obj_set_style_text_color(vals[i], lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  if (g_menuState == MENU_EDIT && g_menuIndex < 6) lv_obj_set_style_text_color(vals[g_menuIndex], lv_color_hex(0xfbd500), LV_PART_MAIN | LV_STATE_DEFAULT);

  char buf[16];
  snprintf(buf, sizeof(buf), "%d",    g_qnh);    lv_label_set_text(objects.val_qnh,    buf);
  snprintf(buf, sizeof(buf), "%d L",  g_water);  lv_label_set_text(objects.val_water,  buf);
  snprintf(buf, sizeof(buf), "%d %%", g_bugs);   lv_label_set_text(objects.val_bugs,   buf);
  snprintf(buf, sizeof(buf), "%d kg", g_weight); lv_label_set_text(objects.val_weight, buf);
  lv_label_set_text(objects.obj6, g_sinkSound ? "Full" : "Mute");

  lv_obj_clear_flag(objects.quick_menu_panel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_update_layout(objects.main);
  lv_area_t fa, la;
  lv_obj_get_coords(objects.selection_frame,     &fa);
  lv_obj_get_coords(Menu_NameLabel(g_menuIndex), &la);
  lv_coord_t delta = ((la.y1 + la.y2) / 2) - ((fa.y1 + fa.y2) / 2);
  if (delta != 0) lv_obj_scroll_by(objects.item_list, 0, -delta, LV_ANIM_OFF);
}

static void MC_Apply(void) {
  if (screen_main_state.indicator) lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator, (int32_t)g_mcTenths * 100);
}

static void Vol_Apply(void) {
  if (!g_arcVol || !g_lblVolNum) return;
  bool shouldShow = (g_volShownAt > 0) && ((millis() - g_volShownAt) < VOL_HIDE_MS) && (g_menuState == MENU_CLOSED) && !g_setupOpen;
  if (shouldShow) {
    lv_arc_set_value(g_arcVol, g_volume);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", g_volume);
    lv_label_set_text(g_lblVolNum, buf);
    // Re-centre a chaque changement de texte : sinon decale des que le nombre de
    // chiffres change (1 chiffre vs 2, ex 5 vs 15).
    lv_obj_align_to(g_lblVolNum, g_arcVol, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
    if (s_ibLabels[5]) lv_obj_add_flag(s_ibLabels[5], LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_arcVol,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_lblVolNum, LV_OBJ_FLAG_HIDDEN);
    if (s_ibLabels[5] && g_infoBoxConfig[5] != IB_EMPTY) lv_obj_clear_flag(s_ibLabels[5], LV_OBJ_FLAG_HIDDEN);
  }
}

/* ============================================================
 *  TELEMETRIE (demo) + rendu aiguilles/labels -- fidele a main.cpp
 * ============================================================ */
static volatile float g_varioFused = 0.0f;
static volatile float g_varioComp  = 0.0f;
static volatile float g_varioAvg   = 0.0f;
static volatile float g_altitude   = 850.0f;
static volatile float g_airspeed   = 0.0f;
static volatile float g_gndSpeed   = 95.0f;
static volatile bool  g_gpsOk      = true;
static volatile float g_gpsTrack   = 45.0f;
static volatile bool  g_circling   = false;
static float g_climbGain    = 0.0f;
static uint32_t g_takeoffMs = 0;
static bool     g_inFlight  = true;
static float g_windSpeedMs  = NAN;   /* demo : anime seulement pour previsualiser l'affichage */
static float g_windDirDeg   = NAN;
static float g_windAvgSpeed = NAN;   /* demo : version retardee (1er ordre) du vent live */
static float g_windAvgDir   = NAN;
static float g_energyDir    = NAN;   /* demo : meme formule que EnergyArrow_Apply (main.cpp) */
static float g_energyMag    = NAN;

// Taille des fleches en fonction du vent : jamais quasi-invisible a 0 vent (plancher),
// deja a taille max bien avant "beaucoup de vent" (sature, ne grossit plus apres).
// Unite de zoom LVGL : 256 = 100% (taille native EEZ). Memes constantes que main.cpp.
#define WIND_ZOOM_MIN         160
#define WIND_ZOOM_MAX         256
#define WIND_ZOOM_SAT_MS       8.0f
#define ENERGY_ZOOM_SAT_MAG     6.0f
#define ENERGY_ARROW_SCALE      3.5f
#define ENERGY_SHOW_MIN         2.0f   // meme seuil que main.cpp -- cache l'energy tant que la derive n'est pas nette

static uint16_t WindArrowZoom(float mag, float satAt)
{
  if (isnan(mag) || mag <= 0.0f) return WIND_ZOOM_MIN;
  float t = mag / satAt;
  if (t > 1.0f) t = 1.0f;
  return (uint16_t)(WIND_ZOOM_MIN + t * (WIND_ZOOM_MAX - WIND_ZOOM_MIN));
}

/* ============================================================
 *  THERMAL HELPER -- port de Firmware/src/ThermalHelper.{h,cpp} + ThermalDraw.{h,cpp}
 *  (memes constantes/algorithme, pas de dependance Arduino -> copie directe en C,
 *  meme convention que le reste de ce fichier : tout est un port statique de main.cpp,
 *  pas un partage de compilation avec le vrai firmware).
 * ============================================================ */
#define TH_BINS    24
#define TH_AGE_MS  30000
#define TH_BLEND   0.30f

static float    s_thBin[TH_BINS];
static uint32_t s_thBinMs[TH_BINS];
static int      s_thTurnDir = 0;
static float    s_thMin = 0.0f, s_thMax = 0.0f, s_thAvg = 0.0f;
static int      s_thFresh = 0;

static void ThermalHelper_Reset(void) {
  for (int i = 0; i < TH_BINS; i++) { s_thBin[i] = 0.0f; s_thBinMs[i] = 0; }
  s_thMin = s_thMax = s_thAvg = 0.0f;
  s_thFresh = 0; s_thTurnDir = 0;
}

static void ThermalHelper_Update(float track, float vario, bool circling, int turnDir, uint32_t now) {
  if (!circling || isnan(track)) { ThermalHelper_Reset(); return; }
  s_thTurnDir = turnDir;
  float a = track;
  while (a >= 360.0f) a -= 360.0f;
  while (a <  0.0f)   a += 360.0f;
  int idx = (int)(a / (360.0f / TH_BINS));
  if (idx < 0) idx = 0; if (idx >= TH_BINS) idx = TH_BINS - 1;
  if (s_thBinMs[idx] == 0) s_thBin[idx]  = vario;
  else                     s_thBin[idx] += (vario - s_thBin[idx]) * TH_BLEND;
  s_thBinMs[idx] = now;
  float mn = 1e9f, mx = -1e9f, sum = 0.0f; int n = 0;
  for (int i = 0; i < TH_BINS; i++) {
    if (s_thBinMs[i] == 0) continue;
    if (now - s_thBinMs[i] > TH_AGE_MS) { s_thBinMs[i] = 0; continue; }
    float v = s_thBin[i];
    if (v < mn) mn = v; if (v > mx) mx = v;
    sum += v; n++;
  }
  if (n > 0) { s_thMin = mn; s_thMax = mx; s_thAvg = sum / (float)n; }
  else       { s_thMin = s_thMax = s_thAvg = 0.0f; }
  s_thFresh = n;
}

static bool ThermalHelper_BinValue(int i, float* out) {
  if (i < 0 || i >= TH_BINS) return false;
  if (s_thBinMs[i] == 0) return false;
  if (out) *out = s_thBin[i];
  return true;
}

#define TH_CX        240
#define TH_CY        240
#define TH_RING_R    75
#define TH_DOT_MIN   5
#define TH_DOT_MAX   20
#define TH_GLIDER_SZ 70
#define TH_HALF      (TH_RING_R + TH_DOT_MAX)
#define TH_LAYER     (2 * TH_HALF)
#define TH_V_SPAN    3.0f
#define TH_COL_MAX   0xFBD500
#define TH_COL_UP    0xE53935
#define TH_COL_DOWN  0x2196F3
#define TH_COL_NEUT  0x606060
#define TH_NEUTRAL   0.25f

typedef struct { int16_t x, y; uint8_t sz; uint32_t col; bool on; } th_dot_t;
static th_dot_t  s_thDots[TH_BINS];
static lv_obj_t* s_thLayer  = NULL;
static lv_obj_t* s_thGlider = NULL;

static void ThermalDraw_layer_cb(lv_event_t* e) {
  lv_draw_ctx_t* dc = lv_event_get_draw_ctx(e);
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_COVER;
  d.radius = LV_RADIUS_CIRCLE;
  for (int i = 0; i < TH_BINS; i++) {
    if (!s_thDots[i].on) continue;
    int s = s_thDots[i].sz;
    d.bg_color = lv_color_hex(s_thDots[i].col);
    lv_area_t a;
    a.x1 = s_thDots[i].x - s / 2; a.y1 = s_thDots[i].y - s / 2;
    a.x2 = a.x1 + s - 1; a.y2 = a.y1 + s - 1;
    lv_draw_rect(dc, &d, &a);
  }
}

/* Masque le planeur statique pose par EEZ (img_glider_th) et cree le planeur/anneau
 * geres a l'execution -- meme fixup que ThermalDraw_Init() dans le vrai firmware. */
static void ThermalDraw_Init(lv_obj_t* parent) {
  uint32_t nch = lv_obj_get_child_cnt(parent);
  for (uint32_t i = 0; i < nch; i++) {
    lv_obj_t* ch = lv_obj_get_child(parent, i);
    if (lv_obj_check_type(ch, &lv_img_class) &&
        lv_img_get_src(ch) == (const void*)&img_glider_th) {
      lv_obj_add_flag(ch, LV_OBJ_FLAG_HIDDEN);
    }
  }
  s_thLayer = lv_obj_create(parent);
  lv_obj_remove_style_all(s_thLayer);
  lv_obj_set_pos(s_thLayer, TH_CX - TH_HALF, TH_CY - TH_HALF);
  lv_obj_set_size(s_thLayer, TH_LAYER, TH_LAYER);
  lv_obj_clear_flag(s_thLayer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_thLayer, ThermalDraw_layer_cb, LV_EVENT_DRAW_MAIN_END, NULL);
  lv_obj_add_flag(s_thLayer, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < TH_BINS; i++) s_thDots[i].on = false;
  s_thGlider = lv_img_create(parent);
  lv_img_set_src(s_thGlider, &img_glider_th);
  lv_obj_clear_flag(s_thGlider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_thGlider, LV_OBJ_FLAG_HIDDEN);
}

static void ThermalDraw_Update(bool circling, int turnDir, float track) {
  static uint32_t last = 0;
  static float    lastThetaG = -1.0f;
  static bool     active = false;
  static float    lastTrackDrawn = NAN;
  static int      lastTurnDrawn  = 99;
  uint32_t now = millis();
  if (now - last < 100) return;
  last = now;

  if (!circling || isnan(track)) {
    if (active) {
      for (int i = 0; i < TH_BINS; i++) s_thDots[i].on = false;
      lv_obj_add_flag(s_thLayer,  LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_thGlider, LV_OBJ_FLAG_HIDDEN);
      active = false; lastThetaG = -1.0f; lastTrackDrawn = NAN;
    }
    return;
  }

  bool justActivated = false;
  if (!active) { lv_obj_clear_flag(s_thLayer, LV_OBJ_FLAG_HIDDEN); active = true; justActivated = true; }

  if (!justActivated && !isnan(lastTrackDrawn) &&
      turnDir == lastTurnDrawn && fabsf(track - lastTrackDrawn) < 0.5f) {
    return;
  }
  lastTrackDrawn = track; lastTurnDrawn = turnDir;

  int idxMax = -1; float vMax = -1e9f;
  for (int i = 0; i < TH_BINS; i++) {
    float v; if (!ThermalHelper_BinValue(i, &v)) continue;
    if (v > vMax) { vMax = v; idxMax = i; }
  }

  float thetaG = (turnDir < 0) ? 90.0f : 270.0f;
  const float binW = 360.0f / TH_BINS;
  for (int i = 0; i < TH_BINS; i++) {
    float v;
    if (!ThermalHelper_BinValue(i, &v)) { s_thDots[i].on = false; continue; }
    float t = fabsf(v) / TH_V_SPAN;
    if (t > 1) t = 1;
    int sz = TH_DOT_MIN + (int)(t * (TH_DOT_MAX - TH_DOT_MIN));
    uint32_t col;
    if      (i == idxMax && v > TH_NEUTRAL) col = TH_COL_MAX;
    else if (v >  TH_NEUTRAL) col = TH_COL_UP;
    else if (v < -TH_NEUTRAL) col = TH_COL_DOWN;
    else                      col = TH_COL_NEUT;
    float th = ((i + 0.5f) * binW - track + thetaG) * 0.01745329f;
    s_thDots[i].x  = TH_CX + (int)(TH_RING_R * sinf(th));
    s_thDots[i].y  = TH_CY - (int)(TH_RING_R * cosf(th));
    s_thDots[i].sz = (uint8_t)sz; s_thDots[i].col = col; s_thDots[i].on = true;
  }
  lv_obj_invalidate(s_thLayer);

  if (thetaG != lastThetaG) {
    float r = thetaG * 0.01745329f;
    int gx = TH_CX + (int)(TH_RING_R * sinf(r));
    int gy = TH_CY - (int)(TH_RING_R * cosf(r));
    lv_obj_set_pos(s_thGlider, gx - TH_GLIDER_SZ / 2, gy - TH_GLIDER_SZ / 2);
    lv_obj_clear_flag(s_thGlider, LV_OBJ_FLAG_HIDDEN);
    lastThetaG = thetaG;
  }
}

static void Comp_Apply(void) {
  static uint64_t lastUs = 0;
  static float vF = 0.0f, vPrev = 0.0f;
  float base = isnan(g_varioFused) ? 0.0f : g_varioFused;
  uint64_t nowUs = micros();
  if (lastUs == 0) { lastUs = nowUs; g_varioComp = base; return; }
  float dt = (float)(nowUs - lastUs) * 1e-6f;
  lastUs = nowUs;
  if (dt <= 0.0f || dt > 0.5f) { g_varioComp = base; return; }
  float term = 0.0f;
  if (g_gpsOk) {
    float v = (g_airspeed > 5.0f) ? g_airspeed : g_gndSpeed;
    vF += (v - vF) * (dt / (0.5f + dt));
    float dVdt = (vF - vPrev) / dt;
    vPrev = vF;
    term = (vF / 9.80665f) * dVdt;
    if (term > 5.0f) term = 5.0f; if (term < -5.0f) term = -5.0f;
  } else { vF = vPrev = 0.0f; }
  g_varioComp = base + term;
}

static void Needles_Apply(void) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if ((now - last) < 50) return;
  last = now;
  static int lastV = -1000000, lastVi = -1000000;
  float v  = isfinite(g_varioComp) ? g_varioComp : 0.0f;
  float vi = isfinite(g_varioAvg)  ? g_varioAvg  : 0.0f;
  if (v > 15.0f) v = 15.0f; else if (v < -15.0f) v = -15.0f;
  if (vi > 15.0f) vi = 15.0f; else if (vi < -15.0f) vi = -15.0f;
  int vm  = ((int)(v  * 1000.0f) / 100) * 100;
  int vim = ((int)(vi * 1000.0f) / 100) * 100;
  if (screen_main_state.indicator2 && vm != lastV)   { lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator2, vm);  lastV  = vm; }
  if (screen_main_state.indicator1 && vim != lastVi) { lv_meter_set_indicator_value(objects.vario_meter, screen_main_state.indicator1, vim); lastVi = vim; }
}

static void Labels_Init(void) {
  if (s_ibLabels[0] != NULL || !objects.lbl_ib_haut_sup) return;
  s_ibLabels[0] = objects.lbl_ib_haut_sup;
  s_ibLabels[1] = objects.lbl_ib_haut_inf;
  s_ibLabels[2] = objects.lbl_ib_bas_cent;   // reserve : toujours IB_EMPTY (g_infoBoxConfig[2])
  s_ibLabels[3] = objects.lbl_ib_bas_sup;
  s_ibLabels[4] = objects.lbl_ib_bas_inf;
  s_ibLabels[5] = objects.lbl_ib_bas_right;
  for (int i = 0; i < 6; i++) if (s_ibLabels[i]) lv_label_set_recolor(s_ibLabels[i], true);
  if (s_ibLabels[5]) {
    lv_obj_set_width(s_ibLabels[5], 60);
    lv_obj_set_style_text_align(s_ibLabels[5], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

static const lv_coord_t IB_LABEL_Y[6] = { 84, 113, 228, 338, 374, 0 };

/* Port fidele de Polar_Fit()/STF_Apply() (main.cpp) : ajuste sink(V)=aV^2+bV+c sur les 3
 * points (V1/Si1 V2/Si2 V3/Si3) de la polaire du planeur SELECTIONNE (g_gliderV1 etc,
 * deja alimentes par la base g_gliderDb / le menu Glider infos) -> le STF affiche reagit
 * au vrai MC ET au vrai choix de planeur, pas une approximation. g_varioNetto n'est pas
 * simule ici (w=0, pas de composante dauphin dynamique) -- seule difference avec le vrai
 * calcul, sans impact visible sur le test du layout. */
static void Polar_Fit_Sim(float* a, float* b, float* c) {
  float v1 = (float)g_gliderV1, v2 = (float)g_gliderV2, v3 = (float)g_gliderV3;
  float s1 = g_gliderSi1, s2 = g_gliderSi2, s3 = g_gliderSi3;
  float d1 = (v1 - v2) * (v1 - v3);
  float d2 = (v2 - v1) * (v2 - v3);
  float d3 = (v3 - v1) * (v3 - v2);
  if (fabsf(d1) < 1e-3f || fabsf(d2) < 1e-3f || fabsf(d3) < 1e-3f) { *a = 0.0f; *b = 0.0f; *c = s1; return; }
  float a1 = 1.0f/d1, b1 = -(v2+v3)/d1, c1 = (v2*v3)/d1;
  float a2 = 1.0f/d2, b2 = -(v1+v3)/d2, c2 = (v1*v3)/d2;
  float a3 = 1.0f/d3, b3 = -(v1+v2)/d3, c3 = (v1*v2)/d3;
  *a = s1*a1 + s2*a2 + s3*a3;
  *b = s1*b1 + s2*b2 + s3*b3;
  *c = s1*c1 + s2*c2 + s3*c3;
}
static void STF_Apply_Sim(void) {
  float a, b, c;
  Polar_Fit_Sim(&a, &b, &c);
  if (fabsf(a) < 1e-6f) { g_stfSpeed = NAN; return; }
  float mc = g_mcTenths / 10.0f;
  /* Signe corrige 28 juillet 2026 (meme bug que main.cpp) : v^2=(c-MC)/a, pas (c+MC)/a
   * -- voir la derivation complete dans STF_Apply() de main.cpp. */
  float v2 = (c - mc) / a;   /* w=0 : g_varioNetto pas simule dans le sim */
  float vstf = (v2 > 0.0f) ? sqrtf(v2) : NAN;
  if (!isnan(vstf) && !isnan(g_windSpeedMs) && !isnan(g_gpsTrack)) {
    float rel      = (g_windDirDeg - g_gpsTrack) * 0.01745329f;
    float headwind = g_windSpeedMs * cosf(rel);
    vstf += headwind * 3.6f * 0.5f;
  }
  g_stfSpeed = vstf;
}

static void Labels_Apply(void) {
  STF_Apply_Sim();
  /* DEBUG TEMPORAIRE : trace tout changement de metrique assignee a une zone, pour
   * diagnostiquer "Speed to Fly affiche IAS" / "Mode affiche une vitesse". */
  {
    static int lastCfg[6] = { -1,-1,-1,-1,-1,-1 };
    for (int k = 0; k < 6; k++) {
      if (g_infoBoxConfig[k] != lastCfg[k]) {
        char m[80]; snprintf(m, sizeof(m), "zone%d g_infoBoxConfig[%d] -> %d", k, k, g_infoBoxConfig[k]);
        DbgLog(m);
        lastCfg[k] = g_infoBoxConfig[k];
      }
    }
  }
  if (g_menuState != MENU_CLOSED && g_ibEditState == IBEDIT_NONE) return;
  for (int i = 0; i < 6; i++) {
    if (!s_ibLabels[i]) continue;
    if (g_infoBoxConfig[i] == IB_EMPTY) {
      lv_label_set_text(s_ibLabels[i], "");
      if (i == 5) lv_obj_align(s_ibLabels[i], LV_ALIGN_CENTER, 160, 0);
      else        lv_obj_align(s_ibLabels[i], LV_ALIGN_TOP_MID, 0, IB_LABEL_Y[i]);
      continue;
    }
    char buf[32];
    lv_obj_set_style_text_color(s_ibLabels[i], lv_color_hex(i == 5 ? 0x1f333e : 0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    switch (g_infoBoxConfig[i]) {
      case IB_VARIO_INST: { float v = isnan(g_varioComp) ? 0.0f : g_varioComp; float vd = g_uVert ? v*1.94384f : v;
        if (i==5) snprintf(buf, sizeof(buf), "%+.1f\n%s", vd, g_uVert?"kt":"m/s");
        else      snprintf(buf, sizeof(buf), g_uVert ? "%+.1f kt" : "%+.1f m/s", vd); break; }
      case IB_VARIO_INT: { float vi = isfinite(g_varioAvg) ? g_varioAvg : 0.0f; float vid = g_uVert ? vi*1.94384f : vi;
        if (i==5) snprintf(buf, sizeof(buf), "%+.1f\n%s", vid, g_uVert?"kt":"m/s");
        else      snprintf(buf, sizeof(buf), g_uVert ? "%+.1f kt" : "%+.1f m/s", vid); break; }
      case IB_MACCREADY:
        if (i==5) snprintf(buf, sizeof(buf), "MC\n%.1f", g_mcTenths / 10.0f);
        else      snprintf(buf, sizeof(buf), "MC %.1f", g_mcTenths / 10.0f);
        break;
      case IB_ALT_BARO: { float am = g_uAlt ? g_altitude*3.28084f : g_altitude; int a=(int)(am+(am>=0?0.5f:-0.5f));
        if (i==5) snprintf(buf, sizeof(buf), "%d\n%s", a, g_uAlt?"ft":"m");
        else      snprintf(buf, sizeof(buf), g_uAlt ? "%d ft" : "%d m", a); break; }
      case IB_ALT_GPS: { float am = g_uAlt ? g_altitude*3.28084f : g_altitude; int a=(int)(am+(am>=0?0.5f:-0.5f));
        if (i==5) snprintf(buf, sizeof(buf), "%d\n%s", a, g_uAlt?"ft":"m");
        else      snprintf(buf, sizeof(buf), g_uAlt ? "%d ft" : "%d m", a); break; }
      case IB_AIRSPEED: { float s = g_uSpeed ? g_airspeed*1.94384f : g_airspeed*3.6f;
        if (i==5) snprintf(buf, sizeof(buf), "%.0f\n%s", s, g_uSpeed?"kt":"km/h");
        else      snprintf(buf, sizeof(buf), g_uSpeed ? "%.0f kt" : "%.0f km/h", s); break; }
      case IB_GND_SPEED: { float gs = isfinite(g_gndSpeed)?g_gndSpeed:0.0f; float s = g_uSpeed ? gs*1.94384f : gs*3.6f;
        if (i==5) snprintf(buf, sizeof(buf), "%.0f\n%s", s, g_uSpeed?"kt":"km/h");
        else      snprintf(buf, sizeof(buf), g_uSpeed ? "%.0f kt" : "%.0f km/h", s); break; }
      case IB_TIME:
        if (i==5) snprintf(buf, sizeof(buf), "Time\n%02u:%02u", (unsigned)datetime.hour, (unsigned)datetime.minute);
        else      snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)datetime.hour, (unsigned)datetime.minute, (unsigned)datetime.second);
        break;
      case IB_FLIGHT_TIME: { unsigned long sec = g_takeoffMs ? (millis()-g_takeoffMs)/1000UL : 0UL;
        if (i==5) snprintf(buf, sizeof(buf), "Flt\n%02lu:%02lu", sec/3600UL, (sec%3600UL)/60UL);
        else      snprintf(buf, sizeof(buf), "%02lu:%02lu", sec/3600UL, (sec%3600UL)/60UL); break; }
      case IB_WIND: {
        /* Lisait une chaine figee "NW 25"/"270\n25" au lieu des vraies g_windDirDeg/
         * g_windSpeedMs (qui, elles, bougent bien via la demo -> "vent fige" signale
         * le 28 juillet 2026 venait de la, pas d'une demo statique). */
        if (isnan(g_windSpeedMs)) { snprintf(buf, sizeof(buf), i==5 ? "Wind\n---" : "Wind ---"); break; }
        float spd = g_uSpeed ? g_windSpeedMs * 1.94384f : g_windSpeedMs * 3.6f;
        if (i==5) snprintf(buf, sizeof(buf), "%03.0f\xC2\xB0\n%.0f", g_windDirDeg, spd);
        else      snprintf(buf, sizeof(buf), "%03.0f\xC2\xB0 %.0f", g_windDirDeg, spd);
        break;
      }
      case IB_CLIMB_GAIN: { int g=(int)(g_climbGain+(g_climbGain>=0?0.5f:-0.5f));
        if (i==5) snprintf(buf, sizeof(buf), "%+d\nm", g);
        else      snprintf(buf, sizeof(buf), "%+d m", g); break; }
      case IB_FLIGHT_LVL: { int fl=(int)((g_altitude/30.48f)+0.5f);
        if (i==5) snprintf(buf, sizeof(buf), "FL\n%03d", fl);
        else      snprintf(buf, sizeof(buf), "FL %03d", fl); break; }
      case IB_GLIDE: { float spd=(g_airspeed>5.0f)?g_airspeed:g_gndSpeed;   /* m/s */
        if (spd>5.5f && g_varioFused<-0.1f) { float ld=spd/(-g_varioFused); if (ld>199.0f) ld=199.0f;
          if (i==5) snprintf(buf, sizeof(buf), "L/D\n%.0f", ld); else snprintf(buf, sizeof(buf), "L/D %.0f", ld); }
        else snprintf(buf, sizeof(buf), i==5 ? "L/D\n---" : "L/D ---"); break; }
      case IB_MODE:
        /* g_circling (etat de vol reel), pas g_ibEditCruiseMode (dernier choix manuel dans
         * l'editeur, jamais mis a jour en vol -> "reste fige Climb" signale). */
        snprintf(buf, sizeof(buf), g_circling ? "Climb" : "Cruise");
        break;
      case IB_ALERTS: {
        /* STUB : g_linkOk/FlightLog_SdOk/BAT_analogVolts n'existent pas dans le simulateur
         * (specifiques au vrai materiel) -> force "actif" en permanence pour verifier
         * visuellement le clignotement jaune/rouge, cf. le vrai code dans main.cpp. */
        snprintf(buf, sizeof(buf), "TEST!");
        lv_obj_set_style_text_color(s_ibLabels[i],
          lv_color_hex(((millis() / 600) % 2 == 0) ? 0xfbd500 : 0xff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        break;
      }
      case IB_STF: {   /* cible STF en jaune + vitesse actuelle en blanc, port fidele de main.cpp */
        if (isnan(g_stfSpeed)) { snprintf(buf, sizeof(buf), "STF ---"); break; }
        float target = g_uSpeed ? g_stfSpeed * 0.539957f : g_stfSpeed;
        float curMs  = (g_airspeed > 5.0f) ? g_airspeed : g_gndSpeed;
        float cur    = g_uSpeed ? curMs * 1.94384f : curMs * 3.6f;
        if (i == 5) snprintf(buf, sizeof(buf), "#fbd500 %.0f#\n%.0f", target, cur);
        else        snprintf(buf, sizeof(buf), "#fbd500 %.0f#  %.0f %s", target, cur, g_uSpeed ? "kt" : "km/h");
        break;
      }
      default: buf[0]='\0'; break;
    }
    lv_label_set_text(s_ibLabels[i], buf);
    if (i == 5) lv_obj_align(s_ibLabels[i], LV_ALIGN_CENTER, 160, 0);
    else        lv_obj_align(s_ibLabels[i], LV_ALIGN_TOP_MID, 0, IB_LABEL_Y[i]);
  }
  if (objects.img_gps) {
    static int lastGps = -1; int g = g_gpsOk ? 1 : 0;
    if (g != lastGps) { lastGps = g; lv_img_set_src(objects.img_gps, g ? &img_gps_connected : &img_gps_waiting); }
  }
}

static void AvgClimb_Apply(void) {
  static float ring[30] = {0}; static int filled=0, head=0; static uint32_t lastMs=0;
  uint32_t now = millis();
  if (lastMs != 0 && now-lastMs < 1000) return;
  lastMs = now;
  ring[head] = isfinite(g_varioComp) ? g_varioComp : 0.0f;
  head = (head+1)%30; if (filled<30) filled++;
  int win = (g_avgClimb==0)?15:(g_avgClimb==1)?20:30; if (win>filled) win=filled;
  float sum=0.0f; for (int k=0;k<win;k++) sum += ring[(head-1-k+30)%30];
  g_varioAvg = (win>0) ? sum/win : 0.0f;
}

static void ClimbGain_Apply(void) {
  static bool prevCirc=false; static float entryAlt=0.0f;
  if (g_circling && !prevCirc) { entryAlt = g_altitude; g_climbGain = 0.0f; }
  if (g_circling) g_climbGain = g_altitude - entryAlt;
  prevCirc = g_circling;
}

/* Meme gating que WindDisplay_Update (main.cpp) : visible en vol droit seulement,
 * symetrique du Thermal Helper. Valeurs animees en demo (voir SimMenu_FeedDemoTelemetry). */
static void WindDisplay_Update(void) {
  bool show     = (g_menuState == MENU_CLOSED) && !g_setupOpen && !g_circling;
  bool haveWind = !isnan(g_windSpeedMs) && !isnan(g_windDirDeg);
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
      if (haveWind) { float spd = g_uSpeed ? g_windSpeedMs * 1.94384f : g_windSpeedMs * 3.6f; snprintf(b, sizeof(b), "%.0f", spd); }
      else          snprintf(b, sizeof(b), "---");
      lv_label_set_text(objects.lbl_wind_value_speed, b);
      lv_obj_clear_flag(objects.lbl_wind_value_speed, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(objects.lbl_wind_value_speed, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // Le live brut n'est pas affiche seul (comme LARUS/LX Hawk) : bruite tour a tour, il ne
  // sert que d'entree au calcul energy ci-dessous. Sa valeur reste visible en texte
  // (lbl_wind_dir/lbl_wind_value_speed).
  if (objects.img_wind_arrow) lv_obj_add_flag(objects.img_wind_arrow, LV_OBJ_FLAG_HIDDEN);
  if (objects.img_glider_wind) {
    if (show) lv_obj_clear_flag(objects.img_glider_wind, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(objects.img_glider_wind, LV_OBJ_FLAG_HIDDEN);
  }

  // AVG (retard 1er ordre du vent live, demo uniquement) -- meme convention de rotation.
  if (objects.img_wind_arrow_avg) {
    if (show && !isnan(g_windAvgDir)) {
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

  // ENERGY (difference vectorielle vent live - moyen, demo uniquement) -- meme formule
  // que EnergyArrow_Apply/main.cpp. g_energyDir est deja le cap A POINTER (pas de +180).
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

/* Genere une fausse telemetrie (sinusoides) pour donner vie a l'affichage. */
static void SimMenu_FeedDemoTelemetry(double t) {
  g_varioFused = 2.2f * (float)sin(t * 0.6);
  g_altitude   = 850.0f + 60.0f * (float)sin(t * 0.08);
  g_airspeed   = 0.0f;                          /* pas de pitot simule -> vitesse sol utilisee */
  g_gndSpeed   = 26.0f + 4.0f * (float)sin(t * 0.05);   /* m/s (~94 km/h) comme le vrai calc */
  g_gpsOk      = true;

  // Cycle de 40s : 15s de spirale (anneau Thermal Helper, sens alterne a chaque
  // spirale) puis 25s de vol droit, decoupe en DEUX phases qui isolent chacune des
  // deux regles independamment (rel = windDir + 180 - track) :
  //   Phase A (12s) : cap qui varie (correction/derive), vent FIGE
  //                   -> seule la fleche bouge, le chiffre ne bouge PAS.
  //   Phase B (13s) : cap FIGE, vent qui tourne
  //                   -> la fleche ET le chiffre bougent ensemble.
  static double lastT         = -1.0;
  static bool   wasCircling   = false;
  static float  dirSign       = 1.0f;    // +1 = virage a droite, -1 = a gauche
  static int    spiralCount   = 0;
  static float  s_sfHeadingBase = 0.0f;  // cap au sortir de la spirale (centre du wobble phase A)
  static bool   s_sfBEntered    = false; // deja entre en phase B pour ce passage en vol droit ?
  static float  s_sfBHeading    = 0.0f;  // cap fige pendant la phase B
  static float  s_sfBWindBase   = 200.0f;// vent au debut de la phase B (avant rotation)
  double dt = (lastT < 0.0) ? 0.0 : (t - lastT);
  if (dt < 0.0 || dt > 1.0) dt = 0.0;    // ignore un saut d'horloge (ex: fenetre deplacee)
  lastT = t;

  double cyclePos = fmod(t, 40.0);
  bool circlingNow = (cyclePos < 15.0);
  g_circling = circlingNow;

  if (circlingNow) {
    if (!wasCircling) { dirSign = (spiralCount % 2 == 0) ? 1.0f : -1.0f; spiralCount++; }
    g_gpsTrack += dirSign * 24.0f * (float)dt;   // ~24 deg/s -> un tour plein en 15s
    while (g_gpsTrack >= 360.0f) g_gpsTrack -= 360.0f;
    while (g_gpsTrack <    0.0f) g_gpsTrack += 360.0f;
  } else {
    if (wasCircling) {   // on vient de sortir de la spirale : (re)demarre la phase A
      s_sfHeadingBase = g_gpsTrack;
      s_sfBEntered    = false;
    }
    double sfT = cyclePos - 15.0;   // 0..25 dans le vol droit
    if (sfT < 12.0) {
      // Phase A : le vent ne bouge pas (g_windDirDeg/g_windSpeedMs inchanges depuis la
      // derniere spirale) ; seul le cap oscille (+/-20 deg, periode 8s, simule une
      // correction de route) -> la fleche suit le cap, le chiffre reste immobile.
      g_gpsTrack = s_sfHeadingBase + 20.0f * sinf((float)sfT * 45.0f * 0.01745329f);
    } else {
      // Phase B : cap fige (la ou le wobble de la phase A s'est arrete) ; le vent
      // tourne (15 deg/s) -> fleche ET chiffre bougent ensemble.
      if (!s_sfBEntered) {
        s_sfBHeading  = g_gpsTrack;
        s_sfBWindBase = g_windDirDeg;
        s_sfBEntered  = true;
      }
      g_gpsTrack   = s_sfBHeading;
      g_windDirDeg = fmodf((float)(s_sfBWindBase + (sfT - 12.0) * 15.0), 360.0f);
    }
    while (g_gpsTrack >= 360.0f) g_gpsTrack -= 360.0f;
    while (g_gpsTrack <    0.0f) g_gpsTrack += 360.0f;
  }
  wasCircling = circlingNow;

  if (isnan(g_windSpeedMs)) g_windSpeedMs = 6.0f;   // seed initial (avant toute spirale)
  if (isnan(g_windDirDeg))  g_windDirDeg  = 200.0f;

  // Vitesse de vent animee (0..12 m/s, periode ~20s) : sert uniquement a previsualiser
  // le zoom des fleches en fonction du vent (pas une regle physique, juste une demo).
  g_windSpeedMs = 6.0f + 6.0f * (float)sin(t * 0.314159);
  if (g_windSpeedMs < 0.0f) g_windSpeedMs = 0.0f;

  // AVG = retard 1er ordre du vent live (illustre la moyenne qui "traine" derriere le
  // live, meme principe que wind_blend/main.cpp mais sans la logique de tour complet).
  if (isnan(g_windAvgSpeed)) { g_windAvgSpeed = g_windSpeedMs; g_windAvgDir = g_windDirDeg; }
  else {
    float k = 0.05f;   // lissage lent -> l'ecart avec le live devient visible en phase B
    g_windAvgSpeed += (g_windSpeedMs - g_windAvgSpeed) * k;
    float dd = g_windDirDeg - g_windAvgDir;
    while (dd > 180.0f)  dd -= 360.0f;
    while (dd < -180.0f) dd += 360.0f;
    g_windAvgDir += dd * k;
    while (g_windAvgDir <   0.0f) g_windAvgDir += 360.0f;
    while (g_windAvgDir >= 360.0f) g_windAvgDir -= 360.0f;
  }

  // ENERGY = difference vectorielle live - avg (meme formule que EnergyArrow_Apply/main.cpp).
  {
    float dl = (g_windDirDeg + 180.0f) * 0.01745329f;
    float da = (g_windAvgDir + 180.0f) * 0.01745329f;
    float eE = g_windSpeedMs * sinf(dl) - g_windAvgSpeed * sinf(da);
    float eN = g_windSpeedMs * cosf(dl) - g_windAvgSpeed * cosf(da);
    float mag = sqrtf(eE * eE + eN * eN);
    g_energyMag = mag * ENERGY_ARROW_SCALE;
    if (mag > 0.05f) {
      g_energyDir = atan2f(eE, eN) * 57.29578f;
      if (g_energyDir < 0.0f) g_energyDir += 360.0f;
    } else {
      g_energyDir = NAN;
    }
  }

  if (g_takeoffMs == 0) g_takeoffMs = millis();
}

/* ============================================================
 *  DISPATCH ENCODEURS (equivalent menu_onButton/onLongPress/onRotate)
 * ============================================================ */
/* ---- Ecran QR code (partage WiFi "App connect"), port fidele de main.cpp ---- */
static lv_obj_t* s_qrCode = NULL;
static bool      g_qrOpen = false;

/* DEBUG TEMPORAIRE : trace la sequence app connect / QR pour diagnostiquer le bug signale. */
static void DbgLog(const char* msg) {
  FILE* f = fopen("C:\\PioBuild\\LM-Vario-Sim\\qr_debug.log", "a");
  if (f) { fprintf(f, "[%lu] %s (serverOn=%d qrOpen=%d)\n", (unsigned long)GetTickCount64(), msg, (int)FlightLog_ServerActive(), (int)g_qrOpen); fclose(f); }
}

static void QrScreen_Show(void) {
  if (!s_qrCode && objects.qr_slot) {
    lv_obj_clear_flag(objects.qr_slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(objects.qr_slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(objects.qr_slot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(objects.qr_slot, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(objects.qr_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(objects.qr_panel, lv_color_hex(0x1f333e), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_qrCode = lv_qrcode_create(objects.qr_slot, 220, lv_color_black(), lv_color_white());
    lv_obj_set_pos(s_qrCode, 0, 0);
    static const char payload[] = "WIFI:T:WPA;S:LIM-Vario;P:limvario;;";
    lv_qrcode_update(s_qrCode, payload, strlen(payload));
  }
  if (objects.qr_panel) {
    lv_obj_clear_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(objects.qr_panel);
  }
  g_qrOpen = true;
  DbgLog("QrScreen_Show APRES set g_qrOpen=true");
}
static void QrScreen_Close(void) {
  DbgLog("QrScreen_Close AVANT");
  if (objects.qr_panel) lv_obj_add_flag(objects.qr_panel, LV_OBJ_FLAG_HIDDEN);
  g_qrOpen = false;
  DbgLog("QrScreen_Close APRES");
}
/* App connect ON/OFF ne montre plus le QR tout seul (voir main.cpp) -- ferme juste
 * l'overlay si le serveur tombe pendant qu'il est affiche. */
static void QrScreen_Tick(void) {
  if (!FlightLog_ServerActive() && g_qrOpen) { DbgLog("QrScreen_Tick: server off -> force Close"); QrScreen_Close(); }
}

static void menu_onButton(void) {
  DbgLog("menu_onButton ENTREE");
  if (g_qrOpen) { QrScreen_Close(); DbgLog("menu_onButton: ferme via g_qrOpen"); return; }   /* overlay QR modal : n'importe quel clic la ferme (comme "Back") */
  g_menuDirty = true;
  if (g_setupOpen) { SetupMenu_Press(); return; }
  switch (g_menuState) {
    case MENU_CLOSED: g_menuState = MENU_NAV; g_menuIndex = 0; break;
    case MENU_NAV: if (g_menuIndex == MENU_EXIT) g_menuState = MENU_CLOSED; else g_menuState = MENU_EDIT; break;
    case MENU_EDIT: g_menuState = MENU_NAV; break;
  }
}
static void menu_onLongPress(void) {
  if (g_qrOpen) QrScreen_Close();   /* ferme juste l'overlay QR (le WiFi reste actif) */
  else if (g_setupOpen) SetupMenu_Close();
  else if (g_menuState != MENU_CLOSED) { g_menuState = MENU_CLOSED; g_menuDirty = true; }
  else SetupMenu_Open();
}
static void menu_onRotate(long delta) {
  if (g_qrOpen) return;   /* overlay QR modal : rien a regler derriere */
  if (g_setupOpen) { SetupMenu_Rotate(delta); return; }
  if (g_menuState == MENU_CLOSED) {
    g_mcTenths += (int)delta;
    if (g_mcTenths < MC_MIN_T) g_mcTenths = MC_MIN_T;
    if (g_mcTenths > MC_MAX_T) g_mcTenths = MC_MAX_T;
  } else if (g_menuState == MENU_NAV) {
    int i = g_menuIndex + (int)delta;
    if (i < 0) i = 0; if (i > MENU_COUNT-1) i = MENU_COUNT-1;
    g_menuIndex = i; g_menuDirty = true;
  } else {
    switch (g_menuIndex) {
      case 0: g_qnh += delta; if (g_qnh<900) g_qnh=900; if (g_qnh>1100) g_qnh=1100; break;
      case 1: g_water += delta*10; if (g_water<0) g_water=0; if (g_water>300) g_water=300; break;
      case 2: g_bugs += delta*10; if (g_bugs<0) g_bugs=0; if (g_bugs>90) g_bugs=90; break;
      case 3: g_weight += delta; if (g_weight<50) g_weight=50; if (g_weight>150) g_weight=150; break;
      case MENU_SOUND: if (delta>0) g_sinkSound=true; else if (delta<0) g_sinkSound=false; break;
    }
    g_menuDirty = true;
  }
}

/* ============================================================
 *  API EXPOSEE A sim_main.c
 * ============================================================ */
void SimMenu_Init(void) {
  Config_Load();
  SetupMenu_Init();
  Menu_LvglSetup();
  Labels_Init();
  ThermalDraw_Init(objects.main);   // masque le planeur statique EEZ + cree l'anneau/planeur geres
}

void SimMenu_Tick(double t) {
  RTC_Loop();
  SimMenu_FeedDemoTelemetry(t);
  Comp_Apply();
  // Port de l'auto-switch de main.cpp : jamais fait dans le simu -> g_infoBoxConfig (et donc
  // Mode) restait colle sur Cruise en permanence, meme pendant les phases de spirale demo.
  if (g_ibEditState == IBEDIT_NONE) {
    g_infoBoxConfig = g_circling ? g_ibConfigClimb : g_ibConfigCruise;
  }
  AvgClimb_Apply();
  ClimbGain_Apply();
  Needles_Apply();
  MC_Apply();
  Vol_Apply();
  Labels_Apply();

  // Sens de virage demo : signe du delta de cap depuis le tick precedent (meme principe
  // que Circling_Apply/g_turnDir dans main.cpp, simplifie -- pas besoin du seuil/hold
  // complet ici, g_circling est deja pilote par SimMenu_FeedDemoTelemetry).
  static float s_thPrevTrack = NAN;
  int thTurnDir = 0;
  if (!isnan(s_thPrevTrack)) {
    float d = g_gpsTrack - s_thPrevTrack;
    while (d > 180.0f)  d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    if      (d >  0.05f) thTurnDir = +1;
    else if (d < -0.05f) thTurnDir = -1;
  }
  s_thPrevTrack = g_gpsTrack;
  ThermalHelper_Update(g_gpsTrack, g_varioComp, g_circling, thTurnDir, millis());
  ThermalDraw_Update((g_menuState == MENU_CLOSED) && !g_setupOpen && g_circling && g_helperEnable,
                      thTurnDir, g_gpsTrack);

  WindDisplay_Update();
  Menu_Apply();
  SetupMenu_Apply();
  QrScreen_Tick();
  Info_Tick();
}

/* ENC1 = rotation navigation/edition (setup + quick menu + MacCready) */
void SimMenu_OnRotate1(long delta) { menu_onRotate(delta); }
/* ENC1 court = valider/entrer */
void SimMenu_OnButton1(void) { menu_onButton(); }
/* ENC1 long = ouvrir/fermer le setup, ou fermer le quick menu */
void SimMenu_OnLongPress1(void) { menu_onLongPress(); }
/* ENC2 = volume (toujours actif) */
void SimMenu_OnRotate2(long delta) {
  g_volume += (int)delta; if (g_volume<0) g_volume=0; if (g_volume>20) g_volume=20;
  g_volShownAt = millis();
}
/* ENC2 long = bascule serveur WiFi logs (App connect) */
void SimMenu_OnLongPress2(void) {
  DbgLog("OnLongPress2 ENTREE");
  if (g_qrOpen) { QrScreen_Close(); return; }   /* overlay QR modal : ferme au lieu de re-basculer App connect */
  FlightLog_ServerToggle();
  g_updateMode = FlightLog_ServerActive();
  DbgLog("OnLongPress2 toggle direct");
}
