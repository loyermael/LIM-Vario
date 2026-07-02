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
#define LIM_VERSION  4           // v4 : added gps_alt + gnd_speed (separate baro/GPS alt and air/ground speed)
#define LIM_BAUD     115200      // UART link baud rate (reliable across both ESP32s)

// Flags field bits (Calculator -> Display frame)
#define LIM_FLAG_BMP_OK   0x01   // BMP388 read successfully
#define LIM_FLAG_SPD_OK   0x02   // MS4525 present -> TE compensated vario
#define LIM_FLAG_GPS_OK   0x04   // Valid GPS fix -> airspeed = GPS ground speed

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
  int32_t  enc1_count;  // cumulative encoder 1 step count
  int32_t  enc2_count;  // cumulative encoder 2 step count
  uint8_t  enc1_btn;    // encoder 1 button state (1 = pressed)
  uint8_t  enc2_btn;    // encoder 2 button state (1 = pressed)
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
