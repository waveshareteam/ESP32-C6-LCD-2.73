#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include "sdmmc_cmd.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lv_demos.h"



void app_main(void)
{
    ESP_ERROR_CHECK(bsp_io_expander_init());

    const uint8_t first_pin = IO_EXTENSION_IO_4;
    const uint8_t last_pin = IO_EXTENSION_IO_14;
    bool level = false;

    IO_EXTENSION_IO_Mode(0xFFF7);

    while (1) {
        for (uint8_t pin = first_pin; pin <= last_pin; pin++) {
            IO_EXTENSION_Output(pin, level);
        }

        level = !level;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
