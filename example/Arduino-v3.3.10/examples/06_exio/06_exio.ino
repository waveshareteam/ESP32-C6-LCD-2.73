#include <Arduino.h>
#include <Wire.h>

#include "io_extension.h"

// ESP32-C6-LCD-2.73 BSP: SCL = GPIO2, SDA = GPIO20
static constexpr uint8_t I2C_SDA = 20;
static constexpr uint8_t I2C_SCL = 2;
static constexpr uint32_t I2C_FREQ = 400000;

static constexpr uint8_t FIRST_OUTPUT_PIN = IO_EXTENSION_IO_4;
static constexpr uint8_t LAST_OUTPUT_PIN = IO_EXTENSION_IO_14;

static bool outputLevel = false;

static bool i2cDevicePresent(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

static void setOutputRange(uint8_t firstPin, uint8_t lastPin, bool level)
{
  for (uint8_t pin = firstPin; pin <= lastPin; pin++) {
    IO_EXTENSION_Output(pin, level);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("IO extension test");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);

  if (!i2cDevicePresent(IO_EXTENSION_ADDR)) {
    Serial.printf("IO extension not found at 0x%02X. Check SDA/SCL and power.\r\n", IO_EXTENSION_ADDR);
    return;
  }

  if (!IO_EXTENSION_Init(Wire)) {
    Serial.println("IO extension init failed.");
    return;
  }

  // Same mode mask as the IDF 07_exio example: IO4~IO14 are used as outputs.
  IO_EXTENSION_IO_Mode(0xFFF7);
  setOutputRange(FIRST_OUTPUT_PIN, LAST_OUTPUT_PIN, outputLevel);

  Serial.println("IO4~IO14 will toggle every second.");
}

void loop()
{
  outputLevel = !outputLevel;
  setOutputRange(FIRST_OUTPUT_PIN, LAST_OUTPUT_PIN, outputLevel);

  Serial.printf("IO4~IO14 output: %s\r\n", outputLevel ? "HIGH" : "LOW");
  delay(1000);
}
