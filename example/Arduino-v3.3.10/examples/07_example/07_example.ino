#include <Arduino.h>
#include <Wire.h>
#define LV_CONF_INCLUDE_SIMPLE
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_lcd_ili9488.h"
#include "i2c_bsp.h"
#include "qmi8658c.h"
#include "rtc_bsp.h"
#include "sd_card_bsp.h"

static constexpr int LCD_H_RES = 320;
static constexpr int LCD_V_RES = 320;
static constexpr int LCD_DRAW_BUF_HEIGHT = 40;
static constexpr gpio_num_t LCD_SPI_SCLK = GPIO_NUM_0;
static constexpr gpio_num_t LCD_SPI_MOSI = GPIO_NUM_1;
static constexpr gpio_num_t LCD_SPI_MISO = GPIO_NUM_18;
static constexpr gpio_num_t LCD_SPI_DC = GPIO_NUM_19;
static constexpr gpio_num_t LCD_SPI_CS = GPIO_NUM_8;
static constexpr gpio_num_t SD_SPI_CS = GPIO_NUM_10;
static constexpr gpio_num_t BOOT_BUTTON = GPIO_NUM_9;
static constexpr spi_host_device_t LCD_SPI_HOST = SPI2_HOST;

static constexpr uint8_t IO_EXT_ADDR = 0x24;
static constexpr uint8_t IO_EXT_MODE_REG = 0x02;
static constexpr uint8_t IO_EXT_OUT_REG = 0x03;
static constexpr uint8_t IO_EXT_PWM_REG = 0x05;
static constexpr uint8_t IO_EXT_LCD_RST = 0;

static constexpr uint8_t RTC_ADDR = 0x51;
static constexpr uint8_t SHTC3_ADDR = 0x70;
static constexpr uint16_t SHTC3_CMD_WAKE = 0x3517;
static constexpr uint16_t SHTC3_CMD_SLEEP = 0xB098;
static constexpr uint16_t SHTC3_CMD_SOFT_RESET = 0x805D;
static constexpr uint16_t SHTC3_CMD_READ_ID = 0xEFC8;
static constexpr uint16_t SHTC3_CMD_MEASURE_T_RH = 0x7866;

static uint16_t io_ext_state = 0xFFFF;
static esp_lcd_panel_io_handle_t lcd_io = nullptr;
static esp_lcd_panel_handle_t lcd_panel = nullptr;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *draw_buf_1 = nullptr;
static lv_color_t *draw_buf_2 = nullptr;
static lv_disp_drv_t disp_drv;

static lv_obj_t *page_title_label = nullptr;
static lv_obj_t *page_status_label = nullptr;
static lv_obj_t *line_labels[5] = {nullptr};
static lv_obj_t *page_hint_label = nullptr;

static bool rtc_ok = false;
static bool shtc3_ok = false;
static bool qmi_ok = false;
static bool sd_ok = false;
static float last_temp = 0.0f;
static float last_humi = 0.0f;
static float last_acc[3] = {0};
static float last_gyro[3] = {0};
static uint32_t last_sensor_ms = 0;
static uint32_t last_screen_ms = 0;
static uint32_t last_button_ms = 0;
static uint8_t current_page = 0;
static bool last_button_level = true;
static constexpr uint8_t PAGE_COUNT = 5;

static bool i2c_probe(uint8_t addr)
{
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool io_ext_write(uint8_t reg, const uint8_t *data, size_t len)
{
  Wire.beginTransmission(IO_EXT_ADDR);
  Wire.write(reg);
  Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

static bool io_ext_write_u16(uint8_t reg, uint16_t value)
{
  uint8_t data[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  return io_ext_write(reg, data, sizeof(data));
}

static bool io_ext_init()
{
  if (!io_ext_write_u16(IO_EXT_MODE_REG, 0xFFF7)) {
    return false;
  }
  io_ext_state = 0xFFFF;
  return io_ext_write_u16(IO_EXT_OUT_REG, io_ext_state);
}

static void io_ext_output(uint8_t pin, bool value)
{
  if (value) {
    io_ext_state |= (1U << pin);
  } else {
    io_ext_state &= ~(1U << pin);
  }
  io_ext_write_u16(IO_EXT_OUT_REG, io_ext_state);
}

static void io_ext_pwm(uint8_t percent)
{
  if (percent > 97) {
    percent = 97;
  }
  uint8_t pwm = static_cast<uint8_t>(percent * 255 / 100);
  io_ext_write(IO_EXT_PWM_REG, &pwm, 1);
}

static void reset_panel_by_io_expander()
{
  io_ext_output(IO_EXT_LCD_RST, false);
  delay(20);
  io_ext_output(IO_EXT_LCD_RST, true);
  delay(150);
}

static void init_lcd_panel()
{
  pinMode(static_cast<uint8_t>(SD_SPI_CS), OUTPUT);
  digitalWrite(static_cast<uint8_t>(SD_SPI_CS), HIGH);

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = LCD_SPI_SCLK;
  buscfg.mosi_io_num = LCD_SPI_MOSI;
  buscfg.miso_io_num = LCD_SPI_MISO;
  buscfg.quadwp_io_num = GPIO_NUM_NC;
  buscfg.quadhd_io_num = GPIO_NUM_NC;
  buscfg.max_transfer_sz = LCD_H_RES * LCD_DRAW_BUF_HEIGHT * 3;

  esp_err_t ret = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("spi_bus_initialize failed: 0x%x\n", ret);
  }

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = LCD_SPI_DC;
  io_config.cs_gpio_num = LCD_SPI_CS;
  io_config.pclk_hz = 60 * 1000 * 1000;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 10;

  ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &lcd_io);
  if (ret != ESP_OK) {
    Serial.printf("esp_lcd_new_panel_io_spi failed: 0x%x\n", ret);
  }

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = GPIO_NUM_NC;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_config.bits_per_pixel = 18;
  panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;

  ret = esp_lcd_new_panel_ili9488(lcd_io, &panel_config, LCD_H_RES * LCD_DRAW_BUF_HEIGHT, &lcd_panel);
  if (ret != ESP_OK) {
    Serial.printf("esp_lcd_new_panel_ili9488 failed: 0x%x\n", ret);
  }

  esp_lcd_panel_reset(lcd_panel);
  esp_lcd_panel_init(lcd_panel);
  esp_lcd_panel_invert_color(lcd_panel, true);
  esp_lcd_panel_swap_xy(lcd_panel, false);
  esp_lcd_panel_mirror(lcd_panel, false, false);
  esp_lcd_panel_disp_on_off(lcd_panel, true);
}

static void display_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
  esp_lcd_panel_draw_bitmap(lcd_panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
  lv_disp_flush_ready(drv);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
  if (label != nullptr) {
    lv_label_set_text(label, text);
  }
}

static lv_obj_t *make_page_line(lv_obj_t *parent, int y)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, 280);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(label, lv_color_hex(0x1D3557), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, y);
  return label;
}

static void create_dashboard_ui()
{
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x1D3557), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);

  page_title_label = lv_label_create(scr);
  lv_obj_set_width(page_title_label, 300);
  lv_label_set_long_mode(page_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(page_title_label, lv_color_white(), 0);
  lv_obj_set_style_text_align(page_title_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(page_title_label, LV_ALIGN_TOP_MID, 0, 16);

  page_status_label = lv_label_create(scr);
  lv_obj_set_width(page_status_label, 300);
  lv_label_set_long_mode(page_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(page_status_label, lv_color_hex(0xA8DADC), 0);
  lv_obj_set_style_text_align(page_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(page_status_label, LV_ALIGN_TOP_MID, 0, 44);

  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, 292, 220);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 76);
  lv_obj_set_style_radius(panel, 8, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0xF1FAEE), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 10, 0);

  for (uint8_t i = 0; i < 5; i++) {
    line_labels[i] = make_page_line(panel, i * 34);
  }

  page_hint_label = lv_label_create(scr);
  lv_obj_set_width(page_hint_label, 300);
  lv_label_set_long_mode(page_hint_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_color(page_hint_label, lv_color_hex(0xA8DADC), 0);
  lv_obj_set_style_text_align(page_hint_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(page_hint_label, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void init_lvgl()
{
  lv_init();

  const uint32_t buf_pixels = LCD_H_RES * LCD_DRAW_BUF_HEIGHT;
  draw_buf_1 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  draw_buf_2 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!draw_buf_1 || !draw_buf_2) {
    draw_buf_1 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT));
    draw_buf_2 = static_cast<lv_color_t *>(heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_8BIT));
  }

  if (!draw_buf_1 || !draw_buf_2) {
    Serial.println("LVGL draw buffer allocation failed");
    while (true) {
      delay(1000);
    }
  }

  lv_disp_draw_buf_init(&draw_buf, draw_buf_1, draw_buf_2, buf_pixels);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_H_RES;
  disp_drv.ver_res = LCD_V_RES;
  disp_drv.flush_cb = display_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  create_dashboard_ui();
}

static uint8_t shtc3_crc(const uint8_t *data, size_t len)
{
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
    }
  }
  return crc;
}

static bool shtc3_write_command(uint16_t cmd)
{
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool shtc3_wake()
{
  if (!shtc3_write_command(SHTC3_CMD_WAKE)) {
    return false;
  }
  delayMicroseconds(240);
  return true;
}

static void shtc3_sleep()
{
  shtc3_write_command(SHTC3_CMD_SLEEP);
}

static bool shtc3_read_word(uint16_t *value)
{
  uint8_t buf[3] = {0};
  if (Wire.requestFrom(SHTC3_ADDR, static_cast<uint8_t>(3)) != 3) {
    return false;
  }
  for (uint8_t i = 0; i < sizeof(buf); i++) {
    buf[i] = Wire.read();
  }
  if (shtc3_crc(buf, 2) != buf[2]) {
    return false;
  }
  *value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
  return true;
}

static bool shtc3_init()
{
  if (!shtc3_wake()) {
    return false;
  }
  shtc3_write_command(SHTC3_CMD_SOFT_RESET);
  delay(2);

  uint16_t id = 0;
  bool ok = shtc3_wake() && shtc3_write_command(SHTC3_CMD_READ_ID) && shtc3_read_word(&id);
  shtc3_sleep();
  if (ok) {
    Serial.printf("SHTC3 ID: 0x%04X\n", id);
  }
  return ok;
}

static bool shtc3_read(float *temperature, float *humidity)
{
  uint8_t buf[6] = {0};
  if (!shtc3_wake()) {
    return false;
  }
  if (!shtc3_write_command(SHTC3_CMD_MEASURE_T_RH)) {
    shtc3_sleep();
    return false;
  }
  delay(20);

  if (Wire.requestFrom(SHTC3_ADDR, static_cast<uint8_t>(6)) != 6) {
    shtc3_sleep();
    return false;
  }
  for (uint8_t i = 0; i < sizeof(buf); i++) {
    buf[i] = Wire.read();
  }
  shtc3_sleep();

  if (shtc3_crc(&buf[0], 2) != buf[2] || shtc3_crc(&buf[3], 2) != buf[5]) {
    return false;
  }

  uint16_t raw_temp = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
  uint16_t raw_humi = (static_cast<uint16_t>(buf[3]) << 8) | buf[4];
  *temperature = -45.0f + 175.0f * raw_temp / 65535.0f;
  *humidity = 100.0f * raw_humi / 65535.0f;
  return true;
}

static void init_peripherals()
{
  rtc_ok = i2c_probe(RTC_ADDR);
  if (rtc_ok) {
    rtc_init();
  }

  shtc3_ok = shtc3_init();
  qmi_ok = qmi8658_init() == 1;

  SD_card_Init();
  sd_ok = sd_cadr_get_value() > 0.0f;

  Serial.printf("RTC:%s SHTC3:%s QMI8658:%s SD:%s\n",
                rtc_ok ? "OK" : "NO",
                shtc3_ok ? "OK" : "NO",
                qmi_ok ? "OK" : "NO",
                sd_ok ? "OK" : "NO");
}

static void refresh_sensors()
{
  if (shtc3_ok) {
    shtc3_ok = shtc3_read(&last_temp, &last_humi);
  }
  if (qmi_ok) {
    qmi8658_read_xyz(last_acc, last_gyro);
  }

  if (rtc_ok) {
    RtcDateTime_t now = i2c_rtc_get();
    Serial.printf("RTC %04u-%02u-%02u %02u:%02u:%02u | ",
                  now.year, now.month, now.day, now.hour, now.minute, now.second);
  }
  Serial.printf("SHTC3 %.2fC %.2f%% | ACC %.2f %.2f %.2f | GYRO %.3f %.3f %.3f | SD %.2fGB\n",
                last_temp, last_humi,
                last_acc[0], last_acc[1], last_acc[2],
                last_gyro[0], last_gyro[1], last_gyro[2],
                sd_cadr_get_value());
}

static void clear_page_lines()
{
  for (uint8_t i = 0; i < 5; i++) {
    set_label_text(line_labels[i], "");
  }
}

static void refresh_screen()
{
  char buf[96];
  clear_page_lines();

  snprintf(buf, sizeof(buf), "Page %u/%u", current_page + 1, PAGE_COUNT);
  set_label_text(page_status_label, buf);
  set_label_text(page_hint_label, "Press BOOT to switch page");

  switch (current_page) {
    case 0: {
      set_label_text(page_title_label, "System Status");
      snprintf(buf, sizeof(buf), "RTC: %s", rtc_ok ? "OK" : "NO");
      set_label_text(line_labels[0], buf);
      snprintf(buf, sizeof(buf), "SHTC3: %s", shtc3_ok ? "OK" : "NO");
      set_label_text(line_labels[1], buf);
      snprintf(buf, sizeof(buf), "QMI8658: %s", qmi_ok ? "OK" : "NO");
      set_label_text(line_labels[2], buf);
      snprintf(buf, sizeof(buf), "SD: %s", sd_ok ? "OK" : "NO");
      set_label_text(line_labels[3], buf);
      snprintf(buf, sizeof(buf), "Uptime: %.1f s", millis() / 1000.0f);
      set_label_text(line_labels[4], buf);
      break;
    }
    case 1: {
      set_label_text(page_title_label, "RTC");
      if (rtc_ok) {
        RtcDateTime_t now = i2c_rtc_get();
        snprintf(buf, sizeof(buf), "Date: %04u-%02u-%02u", now.year, now.month, now.day);
        set_label_text(line_labels[0], buf);
        snprintf(buf, sizeof(buf), "Time: %02u:%02u:%02u", now.hour, now.minute, now.second);
        set_label_text(line_labels[1], buf);
        snprintf(buf, sizeof(buf), "Week: %u", now.week);
        set_label_text(line_labels[2], buf);
      } else {
        set_label_text(line_labels[0], "RTC not found");
      }
      break;
    }
    case 2: {
      set_label_text(page_title_label, "SHTC3");
      if (shtc3_ok) {
        snprintf(buf, sizeof(buf), "Temperature: %.2f C", last_temp);
        set_label_text(line_labels[0], buf);
        snprintf(buf, sizeof(buf), "Humidity: %.2f %%", last_humi);
        set_label_text(line_labels[1], buf);
      } else {
        set_label_text(line_labels[0], "SHTC3 not found");
      }
      break;
    }
    case 3: {
      set_label_text(page_title_label, "QMI8658 IMU");
      if (qmi_ok) {
        snprintf(buf, sizeof(buf), "ACC X: %.2f", last_acc[0]);
        set_label_text(line_labels[0], buf);
        snprintf(buf, sizeof(buf), "ACC Y: %.2f", last_acc[1]);
        set_label_text(line_labels[1], buf);
        snprintf(buf, sizeof(buf), "ACC Z: %.2f", last_acc[2]);
        set_label_text(line_labels[2], buf);
        snprintf(buf, sizeof(buf), "GYRO: %.3f %.3f %.3f", last_gyro[0], last_gyro[1], last_gyro[2]);
        set_label_text(line_labels[3], buf);
      } else {
        set_label_text(line_labels[0], "QMI8658 not found");
      }
      break;
    }
    case 4: {
      set_label_text(page_title_label, "SD Card");
      if (sd_ok) {
        snprintf(buf, sizeof(buf), "Mount: OK");
        set_label_text(line_labels[0], buf);
        snprintf(buf, sizeof(buf), "Capacity: %.2f GB", sd_cadr_get_value());
        set_label_text(line_labels[1], buf);
        set_label_text(line_labels[2], "SPI: CLK0 MOSI1 MISO18 CS10");
      } else {
        set_label_text(line_labels[0], "SD not mounted");
      }
      break;
    }
  }
}

static void handle_button()
{
  bool level = digitalRead(static_cast<uint8_t>(BOOT_BUTTON));
  uint32_t now = millis();
  if (last_button_level && !level && now - last_button_ms > 250) {
    current_page = (current_page + 1) % PAGE_COUNT;
    last_button_ms = now;
    refresh_screen();
    Serial.printf("Display page: %u/%u\n", current_page + 1, PAGE_COUNT);
  }
  last_button_level = level;
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("ESP32-C6-LCD-2.73 integrated Arduino demo");

  pinMode(static_cast<uint8_t>(BOOT_BUTTON), INPUT_PULLUP);
  last_button_level = digitalRead(static_cast<uint8_t>(BOOT_BUTTON));
  I2C_master_Init();

  if (!io_ext_init()) {
    Serial.println("IO expander init failed");
  }
  reset_panel_by_io_expander();
  init_lcd_panel();
  io_ext_pwm(100);
  init_lvgl();
  init_peripherals();
  refresh_sensors();
  refresh_screen();
}

void loop()
{
  lv_tick_inc(5);
  lv_timer_handler();
  handle_button();

  uint32_t now = millis();
  if (now - last_sensor_ms >= 1000) {
    last_sensor_ms = now;
    refresh_sensors();
  }
  if (now - last_screen_ms >= 500) {
    last_screen_ms = now;
    refresh_screen();
  }

  delay(5);
}
