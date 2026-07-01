/* ============================================================
 *  MS4525DO - DIFFERENTIAL pressure sensor (I2C)
 *  Provides dynamic pressure -> airspeed calculation.
 *
 *  IMPORTANT: The calibration formula depends on the exact part number.
 *  Typical pitot tube reference: MS4525DO-DS5AI001DP
 *    - "001D" = +/- 1 psi, differential range
 *    - "A"    = Transfer function Type A (10% .. 90% output span)
 *  If using a different model variant, adjust P_MIN_PSI / P_MAX_PSI below.
 * ============================================================ */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class MS4525DO {
public:
  // Default I2C address = 0x28
  explicit MS4525DO(uint8_t addr = 0x28, TwoWire* bus = &Wire)
    : _addr(addr), _bus(bus) {}

  bool begin();

  // Reads the sensor. Returns false if read is invalid (status != 0).
  //  dp_pa   : Differential pressure in Pascals (signed)
  //  temp_c  : Temperature in degrees Celsius
  bool read(float& dp_pa, float& temp_c);

  // Airspeed (m/s) calculated from dynamic pressure in Pa and
  // air density (1.225 kg/m^3 default at sea level standard atmosphere).
  static float airspeed_ms(float dp_pa, float rho = 1.225f) {
    if (dp_pa <= 0.0f) return 0.0f;          // Pitot: zero or negative pressure = 0 airspeed
    return sqrtf(2.0f * dp_pa / rho);
  }

private:
  uint8_t   _addr;
  TwoWire*  _bus;

  // Sensor span range (adjust according to part number, see header description):
  static constexpr float P_MIN_PSI = -1.0f;  // 001D = +/- 1 psi
  static constexpr float P_MAX_PSI =  1.0f;
  static constexpr float PSI_TO_PA = 6894.757f;
};
