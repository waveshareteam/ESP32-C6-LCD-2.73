#include "weather_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/app_weather_assets.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "include/butmenu.h"
#include "system_manage_service.h"

#define MAX_HTTP_OUTPUT_BUFFER 2048
#define WEATHER_TASK_STACK     4096
#define WEATHER_POLL_MS        5000
#define WEATHER_WAIT_WIFI_MS   1000

typedef struct {
    char *buffer;
    int len;
} http_response_t;

typedef struct {
    bool status;
    char country[32];
    char city[32];
    char lat[16];
    char lon[16];
} weather_location_t;

typedef struct {
    bool status;
    float temperature;
    int humidity;
    float windspeed;
    int winddirection;
    int weathercode;
    int is_day;
    char time[32];
} weather_current_t;

typedef struct {
    uint32_t generation;
    weather_location_t location;
    weather_current_t current;
} weather_update_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *label_temp_obj;
    lv_obj_t *label_humidity_obj;
    lv_obj_t *label_windspeed_obj;
    lv_obj_t *label_winddirection_obj;
    lv_obj_t *label_location;
    lv_obj_t *label_status;
    lv_obj_t *label_condition;
    lv_obj_t *image_weathercode_obj;
} weather_obj_t;

static const char *TAG = "app_weather";
static TaskHandle_t weather_handle = NULL;
static volatile bool weather_service_running = false;
static volatile bool weather_ui_active = false;
static volatile uint32_t weather_generation = 0;
static lv_group_t *weather_group = NULL;

static weather_location_t location = {0};
static weather_current_t current_weather = {0};
static weather_obj_t ui_obj = {0};

static lv_style_t style_card;
static lv_style_t style_value;
static bool styles_inited = false;

static esp_err_t http_get_location(weather_location_t *out);
static esp_err_t https_get_open_meteo(const char *lat, const char *lon, weather_current_t *out);
static esp_err_t parse_weather_location(const char *json_str, weather_location_t *out);
static esp_err_t parse_open_meteo(const char *json_str, weather_current_t *out);
static void weather_task(void *args);

static void format_float_1(char *buf, size_t buf_size, float value)
{
    if (buf == NULL || buf_size == 0) {
        return;
    }

    int scaled = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    int negative = scaled < 0;
    if (negative) {
        scaled = -scaled;
    }

    int whole = scaled / 10;
    int frac = scaled % 10;
    snprintf(buf, buf_size, "%s%d.%d", negative ? "-" : "", whole, frac);
}

static bool weather_wifi_ready(void)
{
    sys_manage_service_t *service = get_system_manage_service_handle();
    return service != NULL && service->wifi_connected;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && resp != NULL) {
        int copy_len = evt->data_len;
        if (resp->len + copy_len >= MAX_HTTP_OUTPUT_BUFFER) {
            copy_len = MAX_HTTP_OUTPUT_BUFFER - resp->len - 1;
        }

        if (copy_len > 0) {
            memcpy(resp->buffer + resp->len, evt->data, copy_len);
            resp->len += copy_len;
            resp->buffer[resp->len] = '\0';
        }
    }

    return ESP_OK;
}

static esp_err_t perform_http_get(const char *url, bool use_crt_bundle, char **out_body)
{
    if (url == NULL || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_body = NULL;
    http_response_t resp = {
        .buffer = (char *)calloc(1, MAX_HTTP_OUTPUT_BUFFER),
        .len = 0,
    };
    if (resp.buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 8000,
    };
    if (use_crt_bundle) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(resp.buffer);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        free(resp.buffer);
        return err;
    }
    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "HTTP status: %d", status_code);
        free(resp.buffer);
        return ESP_FAIL;
    }

    *out_body = resp.buffer;
    return ESP_OK;
}

static esp_err_t http_get_location(weather_location_t *out)
{
    char *body = NULL;
    esp_err_t err = perform_http_get("http://ip-api.com/json/?fields=status,country,city,lat,lon",
                                     false, &body);
    if (err == ESP_OK) {
        err = parse_weather_location(body, out);
    }
    free(body);
    return err;
}

static esp_err_t https_get_open_meteo(const char *lat, const char *lon, weather_current_t *out)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code,is_day,"
             "wind_speed_10m,wind_direction_10m&timezone=Asia/Shanghai",
             lat, lon);

    char *body = NULL;
    esp_err_t err = perform_http_get(url, true, &body);
    if (err == ESP_OK) {
        err = parse_open_meteo(body, out);
    }
    free(body);
    return err;
}

static const lv_image_dsc_t *weather_icon_for_code(const weather_current_t *weather)
{
    bool is_day = weather->is_day != 0;
    int code = weather->weathercode;

    if (code == 0) {
        return is_day ? &sunny : &clear_night;
    }
    if (code == 1 || code == 2) {
        return is_day ? &cloudy_day : &Cloudy_night;
    }
    if (code == 3 || code == 45 || code == 48) {
        return is_day ? &cloudy : &Cloudy_night;
    }
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        return is_day ? &cloudy : &Nighttime_showers;
    }
    if ((code >= 61 && code <= 65) || (code >= 95 && code <= 99)) {
        return &heavy_rain;
    }

    return is_day ? &cloudy : &Cloudy_night;
}

static const char *weather_condition_for_code(int code)
{
    if (code == 0) return "Sunny";
    if (code <= 2) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code == 45 || code == 48) return "Foggy";
    if (code >= 51 && code <= 57) return "Drizzle";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 95) return "Thunderstorm";
    return "Weather";
}

static const char *wind_direction_text(int degrees)
{
    static const char *directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int index = ((degrees + 22) % 360) / 45;
    return directions[index];
}

static void weather_update_async_cb(void *arg)
{
    weather_update_t *update = (weather_update_t *)arg;
    if (update == NULL) {
        return;
    }

    bool can_update = weather_ui_active &&
                      update->generation == weather_generation &&
                      ui_obj.root != NULL;

    if (can_update) {
        char temp_text[16];
        char windspeed_text[16];

        format_float_1(temp_text, sizeof(temp_text), update->current.temperature);
        format_float_1(windspeed_text, sizeof(windspeed_text), update->current.windspeed);

        lv_label_set_text_fmt(ui_obj.label_location, "%s", update->location.city);
        lv_label_set_text_fmt(ui_obj.label_temp_obj, "%s C", temp_text);
        lv_label_set_text_fmt(ui_obj.label_humidity_obj, "%d%%", update->current.humidity);
        lv_label_set_text_fmt(ui_obj.label_windspeed_obj, "%s km/h", windspeed_text);
        lv_label_set_text_fmt(ui_obj.label_winddirection_obj, "%s  %d deg",
                              wind_direction_text(update->current.winddirection),
                              update->current.winddirection);
        if (strlen(update->current.time) >= 10) {
            lv_label_set_text_fmt(ui_obj.label_status, "%.4s/%.2s/%.2s",
                                  update->current.time,
                                  update->current.time + 5,
                                  update->current.time + 8);
        }
        lv_label_set_text(ui_obj.label_condition,
                          weather_condition_for_code(update->current.weathercode));
        lv_image_set_src(ui_obj.image_weathercode_obj, weather_icon_for_code(&update->current));
    }

    free(update);
}

static void weather_task(void *args)
{
    (void)args;

    while (weather_service_running) {
        if (!weather_ui_active || !weather_wifi_ready()) {
            vTaskDelay(pdMS_TO_TICKS(WEATHER_WAIT_WIFI_MS));
            continue;
        }

        if (!location.status) {
            esp_err_t loc_err = http_get_location(&location);
            if (loc_err != ESP_OK || !location.status) {
                ESP_LOGW(TAG, "Location fetch failed");
                vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_MS));
                continue;
            }
        }

        weather_current_t weather = {0};
        esp_err_t weather_err = https_get_open_meteo(location.lat, location.lon, &weather);
        if (weather_err == ESP_OK && weather.status) {
            current_weather = weather;

            weather_update_t *update = (weather_update_t *)calloc(1, sizeof(weather_update_t));
            if (update != NULL) {
                update->generation = weather_generation;
                update->location = location;
                update->current = current_weather;
                bsp_display_lock(-1);
                lv_async_call(weather_update_async_cb, update);
                bsp_display_unlock();
            }
        } else {
            ESP_LOGW(TAG, "Weather fetch failed");
        }

        vTaskDelay(pdMS_TO_TICKS(WEATHER_POLL_MS));
    }

    weather_handle = NULL;
    vTaskDelete(NULL);
}

void weather_ui_service_init(void)
{
    if (weather_handle != NULL) {
        return;
    }

    weather_service_running = true;
    BaseType_t ok = xTaskCreate(weather_task, "weather_task", WEATHER_TASK_STACK, NULL, 4, &weather_handle);
    if (ok != pdPASS) {
        weather_service_running = false;
        weather_handle = NULL;
        ESP_LOGE(TAG, "Failed to create weather task");
    }
}

static void init_styles(void)
{
    if (styles_inited) {
        return;
    }

    lv_style_init(&style_card);
    lv_style_set_size(&style_card, 142, 70);
    lv_style_set_radius(&style_card, 15);
    lv_style_set_bg_color(&style_card, lv_color_hex(0xA9D1F5));
    lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
    lv_style_set_border_width(&style_card, 0);
    lv_style_set_pad_all(&style_card, 10);

    lv_style_init(&style_value);
    lv_style_set_text_font(&style_value, &lv_font_montserrat_16);
    lv_style_set_text_color(&style_value, lv_color_hex(0x243545));

    styles_inited = true;
}

static lv_obj_t *create_weather_card(lv_obj_t *parent, const char *title,
                                     lv_coord_t x, lv_coord_t y, lv_obj_t **value_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &style_card, LV_PART_MAIN);
    lv_obj_set_pos(card, x, y);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x168FE3), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

    *value_label = lv_label_create(card);
    lv_obj_add_style(*value_label, &style_value, LV_PART_MAIN);
    lv_obj_align(*value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    return card;
}

static void back_btn_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        bm_app_close_notify_handle();
    }
}

void weather_ui_screen_init(lv_obj_t *parent)
{
    if (parent == NULL) {
        return;
    }

    init_styles();
    memset(&ui_obj, 0, sizeof(ui_obj));
    weather_generation++;
    weather_ui_active = true;

    if (weather_group != NULL) {
        lv_group_delete(weather_group);
    }
    weather_group = lv_group_create();
    lv_indev_set_group(bsp_display_get_input_dev(), weather_group);

    ui_obj.root = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(parent, lv_color_hex(0xD9EEFC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 38, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(back_btn, back_btn_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(weather_group, back_btn);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xA9D1F5), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0x168FE3), LV_PART_MAIN);
    lv_obj_center(back_label);

    ui_obj.label_location = lv_label_create(parent);
    lv_label_set_text(ui_obj.label_location, location.status ? location.city : "Shenzhen");
    lv_obj_set_style_text_color(ui_obj.label_location, lv_color_hex(0x243545), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_obj.label_location, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(ui_obj.label_location, LV_ALIGN_TOP_RIGHT, -18, 14);

    ui_obj.label_status = lv_label_create(parent);
    lv_label_set_text(ui_obj.label_status, weather_wifi_ready() ? "Updating..." : "Waiting for WiFi");
    lv_obj_set_style_text_color(ui_obj.label_status, lv_color_hex(0x4E6273), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_obj.label_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(ui_obj.label_status, LV_ALIGN_TOP_RIGHT, -24, 64);

    ui_obj.label_condition = lv_label_create(parent);
    lv_label_set_text(ui_obj.label_condition, "Weather");
    lv_obj_set_style_text_color(ui_obj.label_condition, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_text_font(ui_obj.label_condition, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(ui_obj.label_condition, LV_ALIGN_TOP_RIGHT, -18, 88);

    ui_obj.image_weathercode_obj = lv_image_create(parent);
    lv_image_set_src(ui_obj.image_weathercode_obj, &cloudy);
    lv_image_set_scale(ui_obj.image_weathercode_obj, 118);
    lv_obj_align(ui_obj.image_weathercode_obj, LV_ALIGN_TOP_LEFT, 8, 48);

    create_weather_card(parent, "TEMP", 12, 176, &ui_obj.label_temp_obj);
    lv_label_set_text(ui_obj.label_temp_obj, "--.- C");

    create_weather_card(parent, "HUMIDITY", 166, 176, &ui_obj.label_humidity_obj);
    lv_label_set_text(ui_obj.label_humidity_obj, "--%");

    create_weather_card(parent, "WIND", 12, 258, &ui_obj.label_winddirection_obj);
    lv_label_set_text(ui_obj.label_winddirection_obj, "--  --- deg");

    create_weather_card(parent, "SPEED", 166, 258, &ui_obj.label_windspeed_obj);
    lv_label_set_text(ui_obj.label_windspeed_obj, "--.- km/h");
}

void weather_ui_screen_deinit(void)
{
    weather_ui_active = false;
    weather_generation++;
    if (weather_group != NULL) {
        lv_group_remove_all_objs(weather_group);
        lv_group_delete(weather_group);
        weather_group = NULL;
    }
    memset(&ui_obj, 0, sizeof(ui_obj));
}

void weather_ui_pause(void)
{
    weather_ui_active = false;
    weather_generation++;
}

void weather_ui_resume(void)
{
    weather_ui_active = true;
    weather_generation++;
}

static esp_err_t parse_weather_location(const char *json_str, weather_location_t *out)
{
    if (json_str == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(status) && status->valuestring != NULL) {
        out->status = strcmp(status->valuestring, "success") == 0;
    }

    cJSON *country = cJSON_GetObjectItemCaseSensitive(root, "country");
    if (cJSON_IsString(country) && country->valuestring != NULL) {
        strlcpy(out->country, country->valuestring, sizeof(out->country));
    }

    cJSON *city = cJSON_GetObjectItemCaseSensitive(root, "city");
    if (cJSON_IsString(city) && city->valuestring != NULL) {
        strlcpy(out->city, city->valuestring, sizeof(out->city));
    }

    cJSON *lat = cJSON_GetObjectItemCaseSensitive(root, "lat");
    if (cJSON_IsNumber(lat)) {
        snprintf(out->lat, sizeof(out->lat), "%.4f", lat->valuedouble);
    }

    cJSON *lon = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (cJSON_IsNumber(lon)) {
        snprintf(out->lon, sizeof(out->lon), "%.4f", lon->valuedouble);
    }

    cJSON_Delete(root);
    return out->status ? ESP_OK : ESP_FAIL;
}

static esp_err_t parse_open_meteo(const char *json_str, weather_current_t *out)
{
    if (json_str == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *cw = cJSON_GetObjectItemCaseSensitive(root, "current");
    if (cw == NULL) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *time = cJSON_GetObjectItemCaseSensitive(cw, "time");
    if (cJSON_IsString(time) && time->valuestring != NULL) {
        strlcpy(out->time, time->valuestring, sizeof(out->time));
    }

    cJSON *temperature = cJSON_GetObjectItemCaseSensitive(cw, "temperature_2m");
    cJSON *humidity = cJSON_GetObjectItemCaseSensitive(cw, "relative_humidity_2m");
    cJSON *windspeed = cJSON_GetObjectItemCaseSensitive(cw, "wind_speed_10m");
    cJSON *winddirection = cJSON_GetObjectItemCaseSensitive(cw, "wind_direction_10m");
    cJSON *is_day = cJSON_GetObjectItemCaseSensitive(cw, "is_day");
    cJSON *weathercode = cJSON_GetObjectItemCaseSensitive(cw, "weather_code");

    if (cJSON_IsNumber(temperature)) {
        out->temperature = (float)temperature->valuedouble;
    }
    if (cJSON_IsNumber(humidity)) {
        out->humidity = humidity->valueint;
    }
    if (cJSON_IsNumber(windspeed)) {
        out->windspeed = (float)windspeed->valuedouble;
    }
    if (cJSON_IsNumber(winddirection)) {
        out->winddirection = winddirection->valueint;
    }
    if (cJSON_IsNumber(is_day)) {
        out->is_day = is_day->valueint;
    }
    if (cJSON_IsNumber(weathercode)) {
        out->weathercode = weathercode->valueint;
    }

    out->status = cJSON_IsNumber(temperature) &&
                  cJSON_IsNumber(humidity) &&
                  cJSON_IsNumber(windspeed) &&
                  cJSON_IsNumber(winddirection) &&
                  cJSON_IsNumber(is_day) &&
                  cJSON_IsNumber(weathercode);

    cJSON_Delete(root);
    return out->status ? ESP_OK : ESP_FAIL;
}
