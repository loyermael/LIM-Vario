#include "MS4525DO.h"

bool MS4525DO::begin()
{
  // Simple I2C ping probe
  _bus->beginTransmission(_addr);
  return (_bus->endTransmission() == 0);
}

bool MS4525DO::read(float& dp_pa, float& temp_c)
{
  // The MS4525DO sensor transmits 4 bytes:
  //  [0] : status (2 bits) + pressure[13:8]
  //  [1] : pressure[7:0]            -> 14 bits (0..16383)
  //  [2] : temperature[10:3]
  //  [3] : temperature[2:0] + unused  -> 11 bits (0..2047)
  if (_bus->requestFrom((int)_addr, 4) != 4) return false;

  uint8_t b0 = _bus->read();
  uint8_t b1 = _bus->read();
  uint8_t b2 = _bus->read();
  uint8_t b3 = _bus->read();

  uint8_t status = (b0 >> 6) & 0x03;
  if (status != 0) return false;             // 1=command mode, 2=stale data, 3=fault/error

  uint16_t praw = ((uint16_t)(b0 & 0x3F) << 8) | b1;     // 14 bits
  uint16_t traw = ((uint16_t)b2 << 3) | (b3 >> 5);       // 11 bits

  // Transfer Function Type A (10% .. 90% of 16383 span) -> pressure in psi
  const float OUT_MIN = 0.1f * 16383.0f;     // = 1638.3
  const float OUT_MAX = 0.9f * 16383.0f;     // = 14744.7
  float p_psi = (float)(praw - OUT_MIN) * (P_MAX_PSI - P_MIN_PSI)
                / (OUT_MAX - OUT_MIN) + P_MIN_PSI;

  dp_pa  = p_psi * PSI_TO_PA;

  // Temperature formula: 0..2047 -> -50 .. +150 °C
  temp_c = ((float)traw / 2047.0f) * 200.0f - 50.0f;

  return true;
}
