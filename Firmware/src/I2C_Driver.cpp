#include "I2C_Driver.h"

SemaphoreHandle_t g_i2cMutex = nullptr;

void I2C_Init(void) {
  if (!g_i2cMutex) g_i2cMutex = xSemaphoreCreateMutex();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // I2C bus timeout (2 July 2026): without it, a device that stretches the clock
  // indefinitely (clock stretching) or a stuck bus makes Wire.endTransmission()/
  // requestFrom() wait FOREVER -> total freeze, even the software watchdog cannot recover
  // (never saw a single watchdog panic across ~18 freezes that night -> consistent with a
  // real low-level lockup, not an application loop). 50 ms is very generous for normal I2C
  // (which takes a few tens of microseconds to low ms).
  Wire.setTimeOut(50);
}

// Register address is 8-bit
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