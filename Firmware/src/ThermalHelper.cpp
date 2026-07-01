#include "ThermalHelper.h"
#include <math.h>

// Internal State (singleton instance)
static float    s_bin[TH_BINS];     // Smoothed vertical rate per bin (m/s)
static uint32_t s_binMs[TH_BINS];   // Timestamp of last update (0 = empty bin)
static int      s_turnDir = 0;      // +1 right turn / -1 left turn / 0 unknown

// Cached statistics recalculated on each Update call (across fresh bins)
static float s_min = 0.0f, s_max = 0.0f, s_avg = 0.0f;
static int   s_fresh = 0;

void ThermalHelper_Reset()
{
  for (int i = 0; i < TH_BINS; i++) { s_bin[i] = 0.0f; s_binMs[i] = 0; }
  s_min = s_max = s_avg = 0.0f;
  s_fresh   = 0;
  s_turnDir = 0;
}

void ThermalHelper_Update(float track, float vario, bool circling, int turnDir, uint32_t now)
{
  // Outside circling or without valid track -> clear state (circle vanishes when exiting turn)
  if (!circling || isnan(track)) {
    ThermalHelper_Reset();
    return;
  }

  s_turnDir = turnDir;

  // Normalize track angle onto [0, 360) and locate corresponding azimuth bin
  float a = track;
  while (a >= 360.0f) a -= 360.0f;
  while (a <  0.0f)   a += 360.0f;
  int idx = (int)(a / (360.0f / TH_BINS));
  if (idx < 0)        idx = 0;
  if (idx >= TH_BINS) idx = TH_BINS - 1;

  // Store vario: direct initialization if empty bin, otherwise exponential smoothing
  if (s_binMs[idx] == 0) s_bin[idx]  = vario;
  else                   s_bin[idx] += (vario - s_bin[idx]) * TH_BLEND;
  s_binMs[idx] = now;

  // Expire outdated bins + recalculate lift distribution statistics
  float mn = 1e9f, mx = -1e9f, sum = 0.0f;
  int n = 0;
  for (int i = 0; i < TH_BINS; i++) {
    if (s_binMs[i] == 0) continue;
    if (now - s_binMs[i] > TH_AGE_MS) { s_binMs[i] = 0; continue; }  // Expired bin
    float v = s_bin[i];
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    sum += v;
    n++;
  }
  if (n > 0) { s_min = mn; s_max = mx; s_avg = sum / (float)n; }
  else       { s_min = s_max = s_avg = 0.0f; }
  s_fresh = n;
}

int ThermalHelper_BinCount() { return TH_BINS; }

bool ThermalHelper_BinValue(int i, float* out)
{
  if (i < 0 || i >= TH_BINS) return false;
  if (s_binMs[i] == 0)       return false;   // Empty or expired bin
  if (out) *out = s_bin[i];
  return true;
}

int ThermalHelper_Stats(float* mn, float* mx, float* avg)
{
  if (mn)  *mn  = s_min;
  if (mx)  *mx  = s_max;
  if (avg) *avg = s_avg;
  return s_fresh;
}

int ThermalHelper_TurnDir() { return s_turnDir; }
