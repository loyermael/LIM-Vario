#include "I2C_Driver.h"

SemaphoreHandle_t g_i2cMutex = nullptr;

void I2C_Init(void) {
  if (!g_i2cMutex) g_i2cMutex = xSemaphoreCreateMutex();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // Timeout bus I2C (2 juillet 2026) : sans ca, un device qui etire l'horloge
  // indefiniment (clock stretching) ou un bus bloque fait attendre Wire.endTransmission()/
  // requestFrom() POUR TOUJOURS -> gel total, meme le watchdog logiciel ne s'en sort pas
  // (jamais vu un seul panic watchdog sur ~18 gels ce soir -> coherent avec un vrai
  // blocage bas niveau, pas une boucle applicative). 50 ms est tres large pour de l'I2C
  // normal (qui prend quelques dizaines de microsecondes a low ms).
  Wire.setTimeOut(50);
}

// 寄存器地址为 8 位的
bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  bool fail = false;
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  if ( Wire.endTransmission(true)){
    printf("The I2C transmission fails. - I2C Read\r\n");
    fail = true;
  } else {
    Wire.requestFrom(Driver_addr, Length);
    for (int i = 0; i < Length; i++) {
      *Reg_data++ = Wire.read();
    }
  }
  if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
  return fail ? -1 : 0;
}
bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
  if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
  Wire.beginTransmission(Driver_addr);
  Wire.write(Reg_addr);
  for (int i = 0; i < Length; i++) {
    Wire.write(*Reg_data++);
  }
  bool fail = (Wire.endTransmission(true) != 0);
  if (fail) printf("The I2C transmission fails. - I2C Write\r\n");
  if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
  return fail ? -1 : 0;
}