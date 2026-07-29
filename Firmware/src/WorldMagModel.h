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

// Looks up magnetic declination (degrees, positive = East) at the given
// position. Returns false (leaves *declDeg untouched) if lat/lon are NaN.
bool WMM_GetDeclination(float latDeg, float lonDeg, float* declDeg);
