/* ============================================================
 *  L!M Vario - PONT USB <-> serie (depannage DevKit)
 *
 *  L'ecran ESP32-S3 fait passe-plat entre :
 *    - son USB natif (CDC) cote PC
 *    - son UART1 (GPIO43=TX, GPIO44=RX) cote DevKit
 *
 *  -> permet de reflasher le DevKit dont la CP2102 est HS,
 *     en parlant directement a son UART0 (GPIO1/GPIO3).
 *
 *  Flash a faire en --no-stub --baud 115200 (debit fixe),
 *  avec boot MANUEL sur le DevKit (BOOT maintenu + tap EN).
 * ============================================================ */
#include <Arduino.h>

#define TGT_TX 43   // ecran TX  -> DevKit RX0 (GPIO3)
#define TGT_RX 44   // ecran RX  <- DevKit TX0 (GPIO1)

static uint8_t buf[512];

void setup()
{
  Serial.begin(115200);                                   // USB CDC <-> PC
  Serial1.begin(115200, SERIAL_8N1, TGT_RX, TGT_TX);      // UART <-> DevKit
}

void loop()
{
  // Suit le debit demande par le PC (si esptool le change)
  static uint32_t cur = 115200;
  uint32_t b = Serial.baudRate();
  if (b != 0 && b != cur) { Serial1.updateBaudRate(b); cur = b; }

  // PC -> DevKit
  int n = Serial.available();
  if (n > 0) {
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    n = Serial.readBytes(buf, n);
    Serial1.write(buf, n);
  }
  // DevKit -> PC
  n = Serial1.available();
  if (n > 0) {
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    n = Serial1.readBytes(buf, n);
    Serial.write(buf, n);
  }
}
