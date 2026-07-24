#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "esp_lv_adapter.h"
#include "include/butmenu.h"
#include <time.h>
#include "sdmmc_cmd.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "settings.h"
#include "app_sensor.h"
#include "app_photo.h"
#include "app_weather.h"

#include "system_manage_service.h"

butmenu_manager_t *bm = NULL;
bm_app_desc_t app[7] ;
pcf85063a_dev_t *pcf85063a_dev = NULL;
static sys_manage_service_t *sms;

static void status_timer_cb(lv_timer_t *timer)
{
    static int last_wifi_level = -1;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    bm_set_time_label(bm,  timeinfo.tm_hour, timeinfo.tm_min,timeinfo.tm_sec);

    int wifi_level = (sms != NULL && sms->wifi_connected) ? 3 : 0;
    if (wifi_level != last_wifi_level) {
        bm_set_wifi_level_icon(bm, wifi_level);
        last_wifi_level = wifi_level;
    }
}

void app_main(void)
{
    esp_err_t ret;
    esp_log_level_set("wifi", ESP_LOG_ERROR);

        sms = system_manage_service_init();
    
    // NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        sdmmc_card_print_info(stdout, bsp_sdcard_get_handle());
    } else {
        ESP_LOGW("main", "TF card mount failed: %s", esp_err_to_name(ret));
    }

    bsp_io_expander_init();
    IO_EXTENSION_Output(IO_EXTENSION_IO_0, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    IO_EXTENSION_Output(IO_EXTENSION_IO_0, 1);

    pcf85063a_dev = bsp_pcf85063a_drv_init();
    pcf85063a_datetime_t Now_time;
    pcf85063a_get_time_date(pcf85063a_dev, &Now_time);
    sync_system_time_from_rtc(&Now_time);

    lv_display_t *disp = bsp_display_start();
    lv_indev_t *but_indev = bsp_display_get_input_dev();
    bsp_display_backlight_on();
    
    esp_lv_adapter_lock(-1);

    bm_config_t bm_config = {
        .disp = disp,
        .indev = but_indev
    };
    bm = butmenu_init(&bm_config,NULL);


    bm_app_desc_t *app_settings = app_setting_init(false);
    butmenu_register_app(bm,app_settings);

    bm_app_desc_t *app_sensor = app_sensor_init(true);
    butmenu_register_app(bm,app_sensor);

    bm_app_desc_t *app_photo = app_photo_init(true);
    butmenu_register_app(bm,app_photo);

    bm_app_desc_t *app_weather = app_weather_init(true);
    butmenu_register_app(bm,app_weather);

    lv_timer_create(status_timer_cb, 1000, NULL);

    butmenu_start(bm);

    esp_lv_adapter_unlock();

}
