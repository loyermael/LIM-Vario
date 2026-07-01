#pragma once
#include <stdint.h>

// ============================================================
//  Thermal Helper - Data Layer (Larus style: 24 bins of 15 deg)
//  Binned vertical speed categorized by GPS ground track during circling.
//  PURE logic layer (no LVGL dependency) -> verifiable on bench / serial simulator.
//  The renderer layer reads the bins + statistics + turn direction.
// ============================================================

#define TH_BINS    24        // 24 bins of 15 degrees each (Larus architecture)
#define TH_AGE_MS  30000     // Bin expiration threshold (accounts for wind drift)
#define TH_BLEND   0.30f     // Exponential smoothing filter factor: bin = (1-a)*bin + a*vario

// Clears all bins (called automatically upon thermaling exit)
void  ThermalHelper_Reset();

// Updates data layer. Must be called every loop iteration.
//   track    : GPS ground track angle (deg 0..360, NaN if no fix)
//   vario    : Vertical speed rate (m/s) -> typically TE compensated vario
//   circling : true if thermaling/circling state is detected
//   turnDir  : +1 right turn / -1 left turn / 0 unknown
//   now      : Current system timestamp (millis)
void  ThermalHelper_Update(float track, float vario, bool circling, int turnDir, uint32_t now);

// Number of azimuth bins (constant TH_BINS)
int   ThermalHelper_BinCount();

// Retrieves bin value if fresh. Returns false if empty or expired.
bool  ThermalHelper_BinValue(int i, float* out);

// Computes statistics across fresh bins (min/max/average). Returns fresh bin count.
int   ThermalHelper_Stats(float* mn, float* mx, float* avg);

// Current circling turn direction (+1 right / -1 left / 0 unknown)
int   ThermalHelper_TurnDir();
