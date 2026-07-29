/* ============================================================
 *  L!M Vario - Magnetic declination lookup (World Magnetic Model)
 *
 *  Thin wrapper around the vendored WMM_Tinier library (WMM2025
 *  coefficients, valid 2025-2030). Only declination is exposed:
 *  inclination/total intensity are not needed here (see
 *  MagCal_Apply() in main.cpp, which self-calibrates against the
 *  measured field's own magnitude instead of an absolute WMM value).
 * ============================================================ */
#pragma once
#include <stdint.h>

// Looks up magnetic declination (degrees, positive = East) at the given position, using the
// given UTC date (day/month, year2 = 2-digit year e.g. 26 for 2026) if day != 0, otherwise
// falling back to the firmware's build date (see WorldMagModel.cpp) -- the real GPS date is
// preferred when available (main.cpp only has it once a fix has carried a valid RMC date).
// Returns false (leaves *declDeg untouched) if lat/lon are NaN.
bool WMM_GetDeclination(float latDeg, float lonDeg, float* declDeg,
                        uint8_t day = 0, uint8_t month = 0, uint8_t year2 = 0);
