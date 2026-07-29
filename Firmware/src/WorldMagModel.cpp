#include "WorldMagModel.h"
#include <WMM_Tinier.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static WMM_Tinier s_wmm;
static bool       s_init = false;

// Parses the compiler's __DATE__ ("Mmm dd yyyy") into the 2-digit-year/month/day format
// WMM_Tinier expects. Refreshes automatically on every firmware build -- no epoch constant
// to remember to bump; the WMM2025 coefficients stay valid through 2030.
static void BuildDate_Get(uint8_t* year2, uint8_t* month, uint8_t* day)
{
  static const char monthsAbbrev[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char monStr[4] = { __DATE__[0], __DATE__[1], __DATE__[2], 0 };
  const char* p = strstr(monthsAbbrev, monStr);
  *month = p ? (uint8_t)((p - monthsAbbrev) / 3 + 1) : 1;
  *day   = (uint8_t)atoi(__DATE__ + 4);
  *year2 = (uint8_t)(atoi(__DATE__ + 7) % 100);
}

bool WMM_GetDeclination(float latDeg, float lonDeg, float* declDeg,
                        uint8_t day, uint8_t month, uint8_t year2)
{
  if (isnan(latDeg) || isnan(lonDeg)) return false;
  if (!s_init) { s_wmm.begin(); s_init = true; }

  uint8_t y = year2, m = month, d = day;
  if (d == 0) BuildDate_Get(&y, &m, &d);   // pas de date GPS reelle fournie -> date de build
  *declDeg = s_wmm.magneticDeclination(latDeg, lonDeg, y, m, d);
  return true;
}
