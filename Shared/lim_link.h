/* ============================================================
 *  L!M Vario - Link Protocol: CALCULATOR <-> DISPLAY
 *  SHARED header file between Calculator and Firmware projects.
 *
 *  Full-duplex UART link:
 *    Calculator -> Display : Fixed binary frame ~50 Hz (lim_packet_t)
 *    Display -> Calculator : Lightweight command frame on state change (lim_cmd_t)
 *
 *  The calculator sends: vario telemetry + state of both encoders.
 *  The display: renders needles/UI and feeds its menu state machine
 *               with encoder events (rotations + button clicks).
 * ============================================================ */
#pragma once
#include <stdint.h>

#define LIM_SYNC0    0xA5
#define LIM_SYNC1    0x5A
#define LIM_VERSION  8           // v7 : added utc_hour/min/sec/day/month/year2 (RMC time/date,
                                 // flight-log timestamping for IGC comparison)
                                 // v8 : added volume (absolute 0..20, calculator's real sndVol).
                                 // The display used to track its own g_volume via delta of the
                                 // relayed enc2_count -- correct only as long as neither board
                                 // ever restarts independently of the other, since a delta has no
                                 // way to self-correct. Sending the calculator's actual value lets
                                 // the display just mirror it (see lim_check() reject on version
                                 // mismatch -- both sides are always rebuilt together here anyway).
#define LIM_BAUD     115200      // UART link baud rate (reliable across both ESP32s)

// Flags field bits (Calculator -> Display frame)
#define LIM_FLAG_BMP_OK   0x01   // BMP388 read successfully
#define LIM_FLAG_SPD_OK   0x02   // MS4525 present -> TE compensated vario
#define LIM_FLAG_GPS_OK   0x04   // Valid GPS fix -> airspeed = GPS ground speed
#define LIM_FLAG_MAG_OK   0x08   // LIS3MDL present -> mag_x/y/z valid (raw, uncalibrated)

// Cmd field bits (Display -> Calculator frame)
#define LIM_CMD_SINK_SOUND 0x01  // 1 = sink tone active (Full), 0 = silent (Mute)
#define LIM_CMD_CONDOR     0x02  // 1 = honor Condor UDP/serial telemetry, 0 = ignore (real-flight GPS/baro)

#pragma pack(push, 1)
typedef struct {
  uint8_t  sync0;       // 0xA5
  uint8_t  sync1;       // 0x5A
  uint8_t  ver;         // LIM_VERSION
  uint8_t  flags;       // LIM_FLAG_*
  float    pressure;    // Pa (raw absolute static pressure -> display calculates altitude via QNH)
  float    vario;       // m/s (TE compensated if available)
  float    vario_int;   // m/s (integrated over ~20 s)
  float    airspeed;    // m/s
  float    gps_track;   // ground course (track) in degrees 0..360 (NaN if no fix)
  float    gps_alt;     // GPS altitude in meters (NaN if no fix)
  float    gnd_speed;   // GPS ground speed, same unit as airspeed (0 if no fix)
  float    gps_lat;     // latitude, decimal degrees +N/-S (NaN if no fix)
  float    gps_lon;     // longitude, decimal degrees +E/-W (NaN if no fix)
  float    mag_x;       // magnetic field, uT, sensor frame, RAW/uncalibrated (0 if LIS3MDL absent)
  float    mag_y;       // no tilt/hard/soft-iron compensation applied here -> the display (which
  float    mag_z;       // owns the IMU and the AHRS fusion) is where that belongs, not the calculator
  uint8_t  utc_hour;    // UTC time/date from the last valid GPS RMC sentence, for flight-log
  uint8_t  utc_min;     // timestamping (IGC file comparison). utc_hour=0xFF means "no time yet"
  uint8_t  utc_sec;     // (never trust utc_min/sec/day/month/year2 unless utc_hour != 0xFF).
  uint8_t  utc_day;
  uint8_t  utc_month;
  uint8_t  utc_year2;   // 2-digit year (e.g. 26 = 2026)
  int32_t  enc1_count;  // cumulative encoder 1 step count
  int32_t  enc2_count;  // cumulative encoder 2 step count
  uint8_t  enc1_btn;    // encoder 1 button state (1 = pressed)
  uint8_t  enc2_btn;    // encoder 2 button state (1 = pressed)
  uint8_t  volume;      // 0..20, calculator's actual acoustic volume (sndVol) -- authoritative,
                        // the display should mirror this directly rather than re-derive it from
                        // enc2_count deltas (see LIM_VERSION v8 comment above)
  uint16_t crc;         // CRC16-CCITT across all preceding bytes
} lim_packet_t;
#pragma pack(pop)

// CRC16-CCITT (polynomial 0x1021, init 0xFFFF)
static inline uint16_t lim_crc16(const uint8_t* d, uint32_t n) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < n; i++) {
    crc ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

// CALCULATOR side: finalize packet (fill sync/ver/flags/crc)
static inline void lim_finalize(lim_packet_t* p, uint8_t flags) {
  p->sync0 = LIM_SYNC0;
  p->sync1 = LIM_SYNC1;
  p->ver   = LIM_VERSION;
  p->flags = flags;
  p->crc   = lim_crc16((const uint8_t*)p, sizeof(lim_packet_t) - sizeof(uint16_t));
}

// DISPLAY side: validate incoming packet
static inline bool lim_check(const lim_packet_t* p) {
  if (p->sync0 != LIM_SYNC0 || p->sync1 != LIM_SYNC1) return false;
  if (p->ver != LIM_VERSION) return false;
  return p->crc == lim_crc16((const uint8_t*)p, sizeof(lim_packet_t) - sizeof(uint16_t));
}

// ============================================================
//  Command Frame: DISPLAY -> CALCULATOR (3 bytes)
//  Sent only on state change (not continuously).
//  Different sync bytes to avoid confusion with lim_packet_t.
// ============================================================
#define LIM_CMD_SYNC0  0xC3
#define LIM_CMD_SYNC1  0x3C

#pragma pack(push, 1)
typedef struct {
  uint8_t sync0;  // 0xC3
  uint8_t sync1;  // 0x3C
  uint8_t cmd;    // LIM_CMD_* flags
  uint8_t crc8;   // XOR of sync0 ^ sync1 ^ cmd (simple verification)
} lim_cmd_t;
#pragma pack(pop)

// DISPLAY side: prepare and send command frame
static inline void lim_cmd_send(uint8_t cmd, void* serial_ptr) {
  lim_cmd_t c;
  c.sync0 = LIM_CMD_SYNC0;
  c.sync1 = LIM_CMD_SYNC1;
  c.cmd   = cmd;
  c.crc8  = c.sync0 ^ c.sync1 ^ c.cmd;
  (void)serial_ptr; // unused here, macro recommended
}

// Helper macro to transmit via Arduino HardwareSerial:
// LIM_CMD_SEND(Serial1, cmd)
#define LIM_CMD_SEND(serial, cmd_val) do { \
  lim_cmd_t _c; \
  _c.sync0 = LIM_CMD_SYNC0; \
  _c.sync1 = LIM_CMD_SYNC1; \
  _c.cmd   = (cmd_val); \
  _c.crc8  = _c.sync0 ^ _c.sync1 ^ _c.cmd; \
  (serial).write((const uint8_t*)&_c, sizeof(_c)); \
} while(0)

// CALCULATOR side: validate received command frame
static inline bool lim_cmd_check(const lim_cmd_t* c) {
  if (c->sync0 != LIM_CMD_SYNC0 || c->sync1 != LIM_CMD_SYNC1) return false;
  return c->crc8 == (uint8_t)(c->sync0 ^ c->sync1 ^ c->cmd);
}

// ============================================================
//  SOUND CONFIG FRAME: DISPLAY -> CALCULATOR (7 bytes)
//  Sent on change (and at boot) for Sound menu settings.
//  Different sync bytes (0xC4/0x4C) to avoid collision with lim_cmd_t.
// ============================================================
#define LIM_SCFG_SYNC0 0xC4
#define LIM_SCFG_SYNC1 0x4C

#pragma pack(push, 1)
typedef struct {
  uint8_t  sync0;   // 0xC4
  uint8_t  sync1;   // 0x4C
  uint16_t pitch;   // Base tone frequency in Hz (200..1500)
  uint8_t  wave;    // 0=Sine 1=Square 2=Triangle
  uint8_t  spread;  // 0..10, tone frequency variation intensity with vario
  uint8_t  crc8;    // XOR of all preceding bytes
} lim_scfg_t;
#pragma pack(pop)

static inline uint8_t lim_scfg_crc(const lim_scfg_t* s) {
  const uint8_t* p = (const uint8_t*)s;
  uint8_t x = 0;
  for (uint32_t i = 0; i < sizeof(lim_scfg_t) - 1; i++) x ^= p[i];
  return x;
}

// CALCULATOR side: validate received sound config frame
static inline bool lim_scfg_check(const lim_scfg_t* s) {
  if (s->sync0 != LIM_SCFG_SYNC0 || s->sync1 != LIM_SCFG_SYNC1) return false;
  return s->crc8 == lim_scfg_crc(s);
}

// DISPLAY side: transmit sound config frame via Arduino HardwareSerial
// LIM_SCFG_SEND(Serial1, pitch, wave, spread)
#define LIM_SCFG_SEND(serial, pitch_val, wave_val, spread_val) do { \
  lim_scfg_t _s; \
  _s.sync0 = LIM_SCFG_SYNC0; \
  _s.sync1 = LIM_SCFG_SYNC1; \
  _s.pitch = (uint16_t)(pitch_val); \
  _s.wave  = (uint8_t)(wave_val); \
  _s.spread= (uint8_t)(spread_val); \
  _s.crc8  = lim_scfg_crc(&_s); \
  (serial).write((const uint8_t*)&_s, sizeof(_s)); \
} while(0)

// ============================================================
//  MASTER VARIO FRAME: DISPLAY -> CALCULATOR (5 bytes)
//  The display owns the IMU, so it computes the best vario (inertial
//  fusion + total-energy compensation). It streams that master vario back to the
//  calculator so the SPEAKER (wired on the calculator) beeps the EXACT same vario as
//  the needle -- a single master vario drives everything.
//  Sent continuously (~30 Hz). Sync 0xC5/0x5C, distinct from cmd (0xC3) and scfg (0xC4).
// ============================================================
#define LIM_VARIO_SYNC0 0xC5
#define LIM_VARIO_SYNC1 0x5C

#pragma pack(push, 1)
typedef struct {
  uint8_t  sync0;     // 0xC5
  uint8_t  sync1;     // 0x5C
  int16_t  vario_cms; // master vario in cm/s (m/s * 100), signed -> +/-327 m/s, 1 cm/s resolution
  uint8_t  crc8;      // XOR of all preceding bytes
} lim_vario_t;
#pragma pack(pop)

static inline uint8_t lim_vario_crc(const lim_vario_t* v) {
  const uint8_t* p = (const uint8_t*)v;
  uint8_t x = 0;
  for (uint32_t i = 0; i < sizeof(lim_vario_t) - 1; i++) x ^= p[i];
  return x;
}

// CALCULATOR side: validate received master vario frame
static inline bool lim_vario_check(const lim_vario_t* v) {
  if (v->sync0 != LIM_VARIO_SYNC0 || v->sync1 != LIM_VARIO_SYNC1) return false;
  return v->crc8 == lim_vario_crc(v);
}

// DISPLAY side: transmit master vario (m/s) via Arduino HardwareSerial
// LIM_VARIO_SEND(Serial1, vario_ms)
#define LIM_VARIO_SEND(serial, vario_ms) do { \
  lim_vario_t _v; \
  _v.sync0 = LIM_VARIO_SYNC0; \
  _v.sync1 = LIM_VARIO_SYNC1; \
  float _vm = (vario_ms); \
  if (_vm >  320.0f) _vm =  320.0f; \
  if (_vm < -320.0f) _vm = -320.0f; \
  _v.vario_cms = (int16_t)(_vm * 100.0f); \
  _v.crc8  = lim_vario_crc(&_v); \
  (serial).write((const uint8_t*)&_v, sizeof(_v)); \
} while(0)
