#include <Arduino.h>
#include <Wire.h>
#include "esp_err.h"
#include "i2c_bsp.h"

#define I2C_MASTER_SCL_IO 2
#define I2C_MASTER_SDA_IO 20

void I2C_master_Init(void)
{
  Wire.begin(I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
  Wire.setClock(400 * 1000);
}

uint8_t I2C_writr_buff(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(buf, len);
  return Wire.endTransmission() == 0 ? ESP_OK : ESP_FAIL;
}

uint8_t I2C_read_buff(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return ESP_FAIL;
  }

  if (Wire.requestFrom(addr, len) != len) {
    return ESP_FAIL;
  }

  for (uint8_t i = 0; i < len; i++) {
    buf[i] = Wire.read();
  }
  return ESP_OK;
}

uint8_t I2C_master_write_read_device(uint8_t addr, uint8_t *writeBuf, uint8_t writeLen, uint8_t *readBuf, uint8_t readLen)
{
  Wire.beginTransmission(addr);
  Wire.write(writeBuf, writeLen);
  if (Wire.endTransmission(false) != 0) {
    return ESP_FAIL;
  }

  if (Wire.requestFrom(addr, readLen) != readLen) {
    return ESP_FAIL;
  }

  for (uint8_t i = 0; i < readLen; i++) {
    readBuf[i] = Wire.read();
  }
  return ESP_OK;
}
