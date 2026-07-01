#pragma once
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define I2C_SCL_PIN       7
#define I2C_SDA_PIN       15

// Bus I2C partage entre le loop() principal (core 1, ex: RTC lors d'un decollage
// detecte) et Driver_Loop (core 0, IMU/RTC/batterie en continu) -> mutex obligatoire
// pour eviter un blocage permanent du bus (2 juillet 2026, cf freezes aleatoires).
extern SemaphoreHandle_t g_i2cMutex;

void I2C_Init(void);

bool I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);
bool I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);