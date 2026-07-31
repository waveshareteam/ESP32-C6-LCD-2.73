#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "dirent.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <time.h>
#include <sys/time.h>

#include "bsp/esp32_c6_lcd_2_73.h"
#include "bsp/touch.h"
#include "bsp_err_check.h"

#include "iot_button.h"
#include "button_gpio.h"
#include "esp_lcd_ili9488.h"

static const char *TAG = "esp32_c6_lcd_2_73";

/**
 * @brief I2C handle for BSP usage
 *
 * In IDF v5.4 you can call i2c_master_get_bus_handle(BSP_I2C_NUM, i2c_master_bus_handle_t *ret_handle)
 * from #include "esp_private/i2c_platform.h" to get this handle
 *
 * For IDF 5.2 and 5.3 you must call bsp_i2c_get_handle()
 */
static i2c_master_bus_handle_t i2c_handle = NULL;
static bool i2c_initialized = false;
static bool spi_initialized = false;
static esp_lcd_touch_handle_t tp;   // LCD touch handle
static esp_lcd_panel_handle_t panel_handle = NULL; // LCD panel handle
static esp_lcd_panel_io_handle_t io_handle = NULL;
#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_indev_t *disp_indev = NULL;
static lv_display_t *disp_drv = NULL;
#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static uint8_t brightness;
static sdmmc_card_t *bsp_sdcard = NULL;    // Global uSD card handler
static qmi8658_dev_t *_qmi8658_dev = NULL; 
static pcf85063a_dev_t *_pcf85063a_dev = NULL;
static i2c_master_dev_handle_t _shtc3_handle = NULL;
esp_err_t bsp_i2c_init(void)
{
    /* I2C was initialized before */
    if (i2c_initialized) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    BSP_ERROR_CHECK_RETURN_ERR(i2c_new_master_bus(&i2c_config, &i2c_handle));

    i2c_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(i2c_handle));
    i2c_initialized = false;
    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    bsp_i2c_init();
    return i2c_handle;
}


esp_err_t bsp_spi_init(int32_t max_transfer_sz)
{
    /* SPI was initialized before */
    if (spi_initialized) {
        //ESP_LOGD(TAG,"SPI 已经初始化");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_PCLK,
        .mosi_io_num = BSP_LCD_DATA0,
        .miso_io_num = BSP_SPI_MISO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");
    spi_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_spi_deinit(void)
{
    BSP_ERROR_CHECK_RETURN_ERR(spi_bus_free(BSP_LCD_SPI_NUM));
    i2c_initialized = false;
    return ESP_OK;
}

sdmmc_card_t *bsp_sdcard_get_handle(void)
{
    return bsp_sdcard;
}

void bsp_sdcard_get_sdspi_host(sdmmc_host_t *config)
{
    assert(config);

    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();

    memcpy(config, &host_config, sizeof(sdmmc_host_t));
}


void bsp_sdcard_sdspi_get_slot(const spi_host_device_t spi_host, sdspi_device_config_t *config)
{
    assert(config);
    memset(config, 0, sizeof(sdspi_device_config_t));

    config->gpio_cs   = BSP_SD_SPI_CS;
    config->gpio_cd   = SDSPI_SLOT_NO_CD;
    config->gpio_wp   = SDSPI_SLOT_NO_WP;
    config->gpio_int  = GPIO_NUM_NC;
    config->host_id = spi_host;
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
        config->gpio_wp_polarity = SDSPI_IO_ACTIVE_LOW;
    #endif
}

esp_err_t bsp_sdcard_sdspi_mount(bsp_sdcard_cfg_t *cfg)
{
    sdmmc_host_t sdhost = {0};
    sdspi_device_config_t sdslot = {0};
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    assert(cfg);

    ESP_LOGD(TAG, "Initialize SPI bus");
    bsp_spi_init(0);

    if (!cfg->mount) {
        cfg->mount = &mount_config;
    }

    if (!cfg->host) {
        bsp_sdcard_get_sdspi_host(&sdhost);
        cfg->host = &sdhost;
    }

    if (!cfg->slot.sdspi) {
        bsp_sdcard_sdspi_get_slot(BSP_LCD_SPI_NUM, &sdslot);
        cfg->slot.sdspi = &sdslot;
    }

    #if !CONFIG_FATFS_LONG_FILENAMES
        ESP_LOGW(TAG, "Warning: Long filenames on SD card are disabled in menuconfig!");
    #endif


    return  esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, cfg->host, cfg->slot.sdspi, cfg->mount, &bsp_sdcard);
}

esp_err_t bsp_sdcard_mount(void)
{
    bsp_sdcard_cfg_t cfg = {0};
    return bsp_sdcard_sdspi_mount(&cfg);
}

esp_err_t bsp_sdcard_unmount(void)
{
    esp_err_t ret = ESP_OK;

    ret |= esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, bsp_sdcard);
    bsp_sdcard = NULL;

    if (i2c_initialized) {
        ret |= spi_bus_free(BSP_LCD_SPI_NUM);
        i2c_initialized = false;
    }

    return ret;
}



esp_err_t bsp_io_expander_init(void)
{
    esp_err_t ret = ESP_OK;
    bsp_i2c_init();
    IO_EXTENSION_Init(BSP_I2C_NUM);
    return ret;
}


uint16_t bsp_get_io_expander_adc(void)
{
    uint16_t adc_value = 0;
    //custom_io_expander_get_adc(io_expander, &adc_value);
    return adc_value;
}

// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

static void test_draw_bitmap(esp_lcd_panel_handle_t lcd_handle,uint16_t velua)
{
    const int x_start = 0;
    const int y_start = 0;
    const int x_end   = x_start + BSP_LCD_H_RES;
    const int y_end   = y_start + BSP_LCD_V_RES;

    uint16_t *rect_buf = (uint16_t *)heap_caps_malloc(BSP_LCD_H_RES * BSP_LCD_V_RES *2, MALLOC_CAP_SPIRAM);

    for (int i = 0; i < BSP_LCD_H_RES * BSP_LCD_V_RES; i++) {
        rect_buf[i] = velua;
    }

    esp_lcd_panel_draw_bitmap(
        lcd_handle,
        x_start,
        y_start,
        x_end,
        y_end,
        rect_buf 
    );
    
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGD(TAG, "Initialize SPI bus");
    bsp_spi_init(config->max_transfer_sz);

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = CONFIG_BSP_LCD_TRANS_QUEUE_DEPTH,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, ret_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST, // Shared with Touch reset
        .rgb_ele_order = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(*ret_io, &panel_config, BSP_LCD_H_RES * 30, ret_panel));

    esp_lcd_panel_reset(*ret_panel);
    esp_lcd_panel_init(*ret_panel);
    esp_lcd_panel_invert_color(*ret_panel, true);
    esp_lcd_panel_disp_on_off(*ret_panel, true);
    esp_lcd_panel_swap_xy(*ret_panel, false);
    esp_lcd_panel_mirror(*ret_panel,false,false);


    // test_draw_bitmap(*ret_panel,0x001F);
    // vTaskDelay(pdMS_TO_TICKS(2000));

    return ret;

err:
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

esp_err_t bsp_display_draw_bitmap(int x_start, int y_start, int x_end, int y_end,
                                  const void *color_data)
{
    if (panel_handle == NULL || color_data == NULL ||
        x_start < 0 || y_start < 0 ||
        x_end > BSP_LCD_H_RES || y_end > BSP_LCD_V_RES ||
        x_start >= x_end || y_start >= y_end) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start, x_end, y_end, color_data);
}


static button_handle_t prev_btn_handle = NULL, next_btn_handle = NULL, enter_btn_handle = NULL;
void bsp_iot_button_init(void)
{
    const button_gpio_config_t bsp_button_config[] = {
        {
            .gpio_num = GPIO_NUM_4,
            .active_level = 0,
        },
        {
            .gpio_num = GPIO_NUM_5,
            .active_level = 0,
        },
        {
            .gpio_num = GPIO_NUM_9,
            .active_level = 0,
        },
    };

    const button_config_t btn_cfg = {0};
    /* 创建按键设备 */
    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[0], &prev_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[1], &next_btn_handle);
    iot_button_new_gpio_device(&btn_cfg, &bsp_button_config[2], &enter_btn_handle);
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)
static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);

    bsp_display_config_t disp_config = {0};
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new(&disp_config, &panel_handle, &io_handle));

    ESP_LOGD(TAG, "Add LCD screen");
    esp_lv_adapter_display_config_t disp_cfg = {
        .panel = panel_handle,
        .panel_io = io_handle,
        .profile = {
            .interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
            .rotation = cfg->rotation,
            .hor_res = BSP_LCD_H_RES,
            .ver_res = BSP_LCD_V_RES,
            .buffer_height = 30,
            .use_psram = false,
            .enable_ppa_accel = false,
            .require_double_buffer = false,
        },
        .tear_avoid_mode = cfg->tear_avoid_mode,
    };

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    if (!disp)
    {
        return NULL;
    }

    return disp;
}


static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *cfg, lv_display_t *disp)
{
    assert(cfg != NULL);
    bsp_iot_button_init();

    const esp_lv_adapter_nav_buttons_config_t nav_btns_cfg = {
        .disp         = disp,
        .button_prev  = prev_btn_handle,
        .button_next  = next_btn_handle,
        .button_enter = enter_btn_handle,
    };

    return esp_lv_adapter_register_navigation_buttons(&nav_btns_cfg);
}

#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)


esp_err_t bsp_display_brightness_init(void)
{
    ESP_LOGW(TAG, "This board doesn't support to change brightness of LCD");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    } else if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    int flipped_brightness = brightness_percent;

    brightness = (uint8_t)flipped_brightness /100 * 255;

    ESP_LOGI(TAG, "Setting flipped LCD backlight: %d%% (original: %d%%)", flipped_brightness, brightness_percent);

    IO_EXTENSION_Pwm_Output((uint8_t)flipped_brightness);
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    if (disp_drv == NULL)
    {
        ESP_LOGE(TAG, "disp_drv is not initialized");
        return -1;
    }

    return brightness * 100 / 255;
}

esp_err_t bsp_display_backlight_off(void)
{
    ESP_LOGI(TAG, "Backlight off");
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    ESP_LOGI(TAG, "Backlight on");
    return bsp_display_brightness_set(100);
}

#if (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0}};
    return bsp_display_start_with_config(&cfg);
}

lv_display_t *bsp_display_start_with_config(bsp_display_cfg_t *cfg)
{
    lv_display_t *disp;

    assert(cfg != NULL);
    BSP_ERROR_CHECK_RETURN_NULL(esp_lv_adapter_init(&cfg->lv_adapter_cfg));

    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(cfg, disp), NULL);
    lv_indev_set_mode(disp_indev, LV_INDEV_MODE_TIMER);
    
    ESP_ERROR_CHECK(esp_lv_adapter_start());

    disp_drv = disp;
    return disp_drv;
}

lv_display_t *bsp_display_get_disp_dev(void)
{
    return disp_drv;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

esp_err_t bsp_display_lock(int32_t timeout_ms)
{
    return esp_lv_adapter_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    esp_lv_adapter_unlock();
}

#endif // (BSP_CONFIG_NO_GRAPHIC_LIB == 0)

esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    BSP_ERROR_CHECK_RETURN_ERR(ret_val);

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}

/**
 * @brief A universal function to retrieve a list of files with specific extensions in a designated directory
 * 
 * @param dir_path path (example "/sdcard")
 * @param extension target extension (example ".jpg", ".avi", ".png")
 * @param out 
 * @return esp_err_t 
 */
esp_err_t get_file_list_by_ext(const char *dir_path, const char *extension, generic_file_list_t *out)
{
    if (!dir_path || !extension || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    out->list  = NULL;
    out->count = 0;

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return ESP_FAIL;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) { 
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, extension) == 0) {
                count++;
            }
        }
    }

    if (count == 0) {
        closedir(dir);
        return ESP_ERR_NOT_FOUND;
    }

    char **list = (char **)malloc(sizeof(char *) * count);
    if (!list) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    rewinddir(dir);
    int idx = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcasecmp(ext, extension) == 0) {

                size_t len = strlen(dir_path) + strlen(entry->d_name) + 2;
                char *full_path = (char *)malloc(len);
                if (!full_path) {
 
                    for (int i = 0; i < idx; i++) free(list[i]);
                    free(list);
                    closedir(dir);
                    return ESP_ERR_NO_MEM;
                }
                snprintf(full_path, len, "%s/%s", dir_path, entry->d_name);
                list[idx++] = full_path;
            }
        }
    }

    closedir(dir);
    out->list  = list;
    out->count = idx;
    return ESP_OK;
}



qmi8658_dev_t *bsp_qmi8658_drv_init(void)
{
    if(_qmi8658_dev != NULL)
    {
        return _qmi8658_dev;
    }
    bsp_i2c_init();

    _qmi8658_dev = (qmi8658_dev_t *)calloc(sizeof(qmi8658_dev_t),1);
    ESP_ERROR_CHECK(qmi8658_init(_qmi8658_dev, i2c_handle, QMI8658_ADDRESS_HIGH));

    qmi8658_set_accel_range(_qmi8658_dev, QMI8658_ACCEL_RANGE_8G);
    qmi8658_set_accel_odr(_qmi8658_dev, QMI8658_ACCEL_ODR_500HZ);
    qmi8658_set_accel_unit_mps2(_qmi8658_dev, true);
    qmi8658_write_register(_qmi8658_dev, QMI8658_CTRL5, 0x03);

    return _qmi8658_dev;
}


pcf85063a_dev_t *bsp_pcf85063a_drv_init(void)
{
    if(_pcf85063a_dev != NULL)
    {
        return _pcf85063a_dev;
    }
    bsp_i2c_init();
    _pcf85063a_dev = (pcf85063a_dev_t *)calloc(sizeof(pcf85063a_dev_t),1);


    ESP_ERROR_CHECK(pcf85063a_init(_pcf85063a_dev, i2c_handle, PCF85063A_ADDRESS));

    return _pcf85063a_dev;
}


i2c_master_dev_handle_t bsp_shtc3_drv_init(void)
{
    if(_shtc3_handle != NULL)
    {
        return _shtc3_handle;
    }
    bsp_i2c_init();

    _shtc3_handle = shtc3_device_create(i2c_handle, SHTC3_I2C_ADDR, CONFIG_SHTC3_I2C_CLK_SPEED_HZ);

    uint8_t sensor_id[2];
    esp_err_t err = shtc3_get_id(_shtc3_handle, sensor_id);
    ESP_LOGI(TAG, "Sensor ID: 0x%02x%02x", sensor_id[0], sensor_id[1]);

    return _shtc3_handle;
}

float bat_adc_read(void)
{
    float vbat_v = 0.0f;
    uint16_t bat_adc;
    bat_adc = IO_EXTENSION_Adc_Input();
    vbat_v = ((float)bat_adc * 3.3f / 4095.0f) * 3;
    return vbat_v;
}

void sync_system_time_from_rtc(pcf85063a_datetime_t *rtc_time)
{
    struct tm t_st = {0};

    // 将 RTC 结构体转换为 struct tm
    t_st.tm_year  = rtc_time->year - 1900;  // tm_year 是从1900年起的偏移
    t_st.tm_mon   = rtc_time->month - 1;    // tm_mon 范围 0-11
    t_st.tm_mday  = rtc_time->day;
    t_st.tm_hour  = rtc_time->hour;
    t_st.tm_min   = rtc_time->min;
    t_st.tm_sec   = rtc_time->sec;
    t_st.tm_wday  = rtc_time->dotw;         // 0=Sunday
    t_st.tm_isdst = -1;

    // 设置 TZ 为 UTC，确保 mktime() 按 UTC 解析（避免时区影响）
    setenv("TZ", "GMT0", 1);
    tzset();

    time_t t = mktime(&t_st);

    struct timeval now = {
        .tv_sec  = t,
        .tv_usec = 0
    };
    settimeofday(&now, NULL);

    // 恢复你需要的本地时区，例如 CST-8
    setenv("TZ", "CST-8", 1);
    tzset();
}
