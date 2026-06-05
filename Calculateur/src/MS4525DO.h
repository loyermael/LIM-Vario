/* ============================================================
 *  MS4525DO - capteur de pression DIFFERENTIELLE (I2C)
 *  Donne la pression dynamique -> vitesse air.
 *
 *  ATTENTION : la formule depend de la REFERENCE exacte du capteur.
 *  Reference typique pour un pitot : MS4525DO-DS5AI001DP
 *    - "001D" = +/- 1 psi, plage differentielle
 *    - "A"    = transfert type A (sortie 10% .. 90%)
 *  Si ton modele est different, ajuste P_MIN_PSI / P_MAX_PSI ci-dessous.
 * ============================================================ */
#pragma once
#include <Arduino.h>
#include <Wire.h>

class MS4525DO {
public:
  // addr I2C par defaut = 0x28
  explicit MS4525DO(uint8_t addr = 0x28, TwoWire* bus = &Wire)
    : _addr(addr), _bus(bus) {}

  bool begin();

  // Lit le capteur. Renvoie false si lecture invalide (status != 0).
  //  dp_pa   : pression differentielle en Pascals (signee)
  //  temp_c  : temperature en degres C
  bool read(float& dp_pa, float& temp_c);

  // Vitesse air (m/s) a partir d'une pression dynamique en Pa et de la
  // densite de l'air (1.225 par defaut au niveau de la mer).
  static float airspeed_ms(float dp_pa, float rho = 1.225f) {
    if (dp_pa <= 0.0f) return 0.0f;          // pitot : depression = 0
    return sqrtf(2.0f * dp_pa / rho);
  }

private:
  uint8_t   _addr;
  TwoWire*  _bus;

  // Plage du capteur (a adapter selon la reference, voir en-tete) :
  static constexpr float P_MIN_PSI = -1.0f;  // 001D = +/- 1 psi
  static constexpr float P_MAX_PSI =  1.0f;
  static constexpr float PSI_TO_PA = 6894.757f;
};
