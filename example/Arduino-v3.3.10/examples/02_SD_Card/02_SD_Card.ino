#include <Arduino.h>
#include <string.h>
#include "sd_card_bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TEST_FILE = "/sd_card/sd_rw_test.txt";
static char testData[128];
static uint8_t readBuffer[256];

static void runSdReadWriteTest()
{
  float cardSizeGb = sd_cadr_get_value();
  if (cardSizeGb <= 0.0f) {
    Serial.println("[SD] Mount failed or card not found.");
    return;
  }

  snprintf(testData, sizeof(testData),
           "ESP32-C6-LCD-2.73 SD read/write test\r\nmillis=%lu\r\n",
           millis());

  Serial.printf("[SD] Card mounted, size: %.2f GB\r\n", cardSizeGb);
  Serial.printf("[SD] Writing test file: %s\r\n", TEST_FILE);

  esp_err_t err = s_example_write_file(TEST_FILE, testData);
  if (err != ESP_OK) {
    Serial.printf("[SD] Write failed: 0x%X\r\n", err);
    return;
  }

  memset(readBuffer, 0, sizeof(readBuffer));
  uint32_t readLen = 0;
  err = s_example_read_file(TEST_FILE, readBuffer, &readLen);
  if (err != ESP_OK) {
    Serial.printf("[SD] Read failed: 0x%X\r\n", err);
    return;
  }

  size_t writeLen = strlen(testData);
  bool pass = (readLen == writeLen) && (memcmp(readBuffer, testData, writeLen) == 0);

  Serial.printf("[SD] Write length: %u bytes\r\n", static_cast<unsigned>(writeLen));
  Serial.printf("[SD] Read length : %lu bytes\r\n", readLen);
  Serial.printf("[SD] Read data:\r\n%s", reinterpret_cast<char *>(readBuffer));
  Serial.printf("[SD] Read/write test: %s\r\n", pass ? "PASS" : "FAIL");
}

void setup()
{
  Serial.begin(115200);
  delay(3000);
  SD_card_Init();
  runSdReadWriteTest();
}

void loop()
{
}