/* ============================================================
 *  L!M Vario - GPS Reception via WiFi (NMEA over UDP)
 *  CALCULATOR side (Classic ESP32: ample heap -> reliable AP host)
 *
 *  The calculator unit creates the WiFi Access Point "LIM-Vario".
 *  External devices (a phone NMEA/UDP navigation app) broadcast an NMEA stream
 *  over UDP port 10110. We parse GROUND SPEED, forwarded to display
 *  unit inside vario data packet (airspeed + LIM_FLAG_GPS_OK flag)
 *  for total energy compensation.
 * ============================================================ */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     GpsLink_Begin(void);          // Initializes WiFi AP + UDP listener socket
void     GpsLink_Loop(void);           // Polling routine to call frequently inside loop()
bool     GpsLink_HasFix(void);         // Returns true if valid GPS fix received recently (<3 s)
float    GpsLink_GroundSpeed(void);    // Ground speed in m/s (0 if no fix)
float    GpsLink_Track(void);          // Ground track heading deg 0..360 (NAN if no fix)

// --- CONDOR Simulator Mode (Flight sim data stream via UDP "key=val") ---
void     GpsLink_SetCondorEnabled(bool enabled); // Condor sim toggle (System menu): when false,
                                                  // the received Condor frames (serial/UDP) are
                                                  // completely ignored (no pollution of the shared
                                                  // GPS fix nor frozen g_cVario/g_cAlt).
bool     GpsLink_CondorActive(void);   // Returns true if Condor UDP telemetry received < 2 s ago
float    GpsLink_Vario(void);          // Condor total energy vario (evario) in m/s
float    GpsLink_Altitude(void);       // Condor barometric altitude (m)
int      GpsLink_RSSI(void);           // Received WiFi signal strength (dBm, ~ -50 strong / -85 weak)
