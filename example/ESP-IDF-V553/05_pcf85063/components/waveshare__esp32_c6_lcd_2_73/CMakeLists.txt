idf_component_register(
    SRCS "esp32_c6_lcd_2_73.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES fatfs esp_driver_i2c esp_driver_gpio esp_lcd atanisoft__esp_lcd_ili9488 shtc3 io_extension waveshare__pcf85063a waveshare__qmi8658
    PRIV_REQUIRES esp_timer spiffs esp_psram usb
)
