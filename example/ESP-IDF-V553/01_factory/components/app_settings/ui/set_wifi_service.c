#include "set_wifi_service.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "lvgl.h"
#include "set_lv_style.h"
#include "system_manage_service.h"

static const char *TAG = "wifi ui";

#define WIFI_SCAN_MAX_AP        8
#define WIFI_SCAN_TASK_STACK    4096
#define WIFI_PROV_AP_PASSWORD   "waveshare0755"
#define WIFI_QR_SIZE            132

static lv_obj_t *return_screen = NULL;
static lv_group_t *return_group = NULL;
static lv_group_t *wifi_group = NULL;
static lv_obj_t *wifi_con = NULL;
static lv_obj_t *provision_con = NULL;
static lv_obj_t *back_btn = NULL;
static lv_obj_t *provision_btn = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *scan_overlay = NULL;
static lv_obj_t *current_wifi_label = NULL;
static lv_obj_t *wifi_list = NULL;
static lv_obj_t *provision_ssid_label = NULL;
static lv_obj_t *provision_qr = NULL;
static lv_obj_t *provision_qr_fallback = NULL;
static lv_obj_t *success_msgbox = NULL;
static lv_timer_t *provision_timer = NULL;
static lv_timer_t *success_exit_timer = NULL;
static lv_timer_t *wifi_status_timer = NULL;
static lv_timer_t *scan_result_timer = NULL;
static lv_group_t *scan_wait_group = NULL;
static TaskHandle_t scan_task_handle = NULL;
static volatile bool wifi_screen_alive = false;
static volatile bool wifi_scan_running = false;
static uint32_t wifi_screen_generation = 0;
static lv_obj_t *wifi_list_group_items[WIFI_SCAN_MAX_AP] = {0};
static uint16_t wifi_list_group_item_count = 0;
static portMUX_TYPE scan_result_mux = portMUX_INITIALIZER_UNLOCKED;
static bool pending_scan_result_ready = false;

typedef struct {
    uint32_t generation;
    esp_err_t ret;
    uint16_t record_count;
    wifi_ap_record_t records[WIFI_SCAN_MAX_AP];
} wifi_scan_result_t;

static wifi_scan_result_t *pending_scan_result = NULL;

static void deinit_wifi_settings_screen(void);
static void start_wifi_scan(void);
static void show_scan_page(bool restart_sta);
static void show_provision_page(void);
static void wifi_scan_result_timer_cb(lv_timer_t *timer);

static void log_lv_mem(const char *point)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "[LV_MEM] %s used:%u%% frag:%u%% free:%u biggest:%u",
             point,
             (unsigned)mon.used_pct,
             (unsigned)mon.frag_pct,
             (unsigned)mon.free_size,
             (unsigned)mon.free_biggest_size);
}


static const char *authmode_to_text(wifi_auth_mode_t authmode)
{
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 ENT";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";
        default:
            return "SEC";
    }
}

static void sanitize_ssid_text(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    size_t out = 0;
    for (size_t i = 0; i < src_size && out < dst_size - 1; i++) {
        uint8_t ch = src[i];
        if (ch == '\0') {
            break;
        }
        dst[out++] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '?';
    }
    dst[out] = '\0';

    if (out == 0) {
        snprintf(dst, dst_size, "<hidden>");
    }
}

static void set_scan_button_visible(bool visible)
{
    if (scan_btn == NULL) {
        return;
    }

    if (visible) {
        lv_obj_remove_flag(scan_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(scan_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_current_wifi_label(void)
{
    if (current_wifi_label == NULL) {
        return;
    }

    sys_manage_service_t *service = get_system_manage_service_handle();
    if (service != NULL && service->wifi_connected && service->wifi_ssid[0] != '\0') {
        lv_label_set_text_fmt(current_wifi_label, "Connected: %s", service->wifi_ssid);
    } else {
        lv_label_set_text(current_wifi_label, "Not connected");
    }
}

static void wifi_status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_current_wifi_label();
}

static void delete_wifi_status_timer(void)
{
    if (wifi_status_timer != NULL) {
        lv_timer_delete(wifi_status_timer);
        wifi_status_timer = NULL;
    }
}

static void create_wifi_status_timer(void)
{
    delete_wifi_status_timer();
    wifi_status_timer = lv_timer_create(wifi_status_timer_cb, 1000, NULL);
}

static void delete_scan_result_timer(void)
{
    if (scan_result_timer != NULL) {
        lv_timer_delete(scan_result_timer);
        scan_result_timer = NULL;
    }
}

static void create_scan_result_timer(void)
{
    delete_scan_result_timer();
    scan_result_timer = lv_timer_create(wifi_scan_result_timer_cb, 100, NULL);
}

static void show_scan_loading(void)
{
    if (wifi_con == NULL) {
        return;
    }

    if (scan_wait_group == NULL) {
        scan_wait_group = lv_group_create();
    }
    lv_indev_set_group(bsp_display_get_input_dev(), scan_wait_group);

    if (scan_overlay != NULL) {
        lv_obj_remove_flag(scan_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(scan_overlay);
        return;
    }

    scan_overlay = lv_obj_create(wifi_con);
    lv_obj_remove_style_all(scan_overlay);
    lv_obj_set_size(scan_overlay, 320, 290);
    lv_obj_align(scan_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(scan_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(scan_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scan_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scan_overlay, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t *spinner = lv_spinner_create(scan_overlay);
    lv_obj_set_size(spinner, 56, 56);
    lv_spinner_set_anim_params(spinner, 900, 200);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -18);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x0D6EFD), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);

    lv_obj_t *label = lv_label_create(scan_overlay);
    lv_label_set_text(label, "Scanning...");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 30);
}

static void hide_scan_loading(void)
{
    lv_indev_t *indev = bsp_display_get_input_dev();

    if (wifi_screen_alive && wifi_group != NULL) {
        lv_indev_set_group(indev, wifi_group);
    }

    if (scan_wait_group != NULL) {
        lv_group_delete(scan_wait_group);
        scan_wait_group = NULL;
    }

    if (scan_overlay != NULL) {
        lv_obj_delete(scan_overlay);
        scan_overlay = NULL;
    }
}

static void remove_wifi_list_group_items(void)
{
    for (uint16_t i = 0; i < wifi_list_group_item_count; i++) {
        if (wifi_list_group_items[i] != NULL) {
            lv_group_remove_obj(wifi_list_group_items[i]);
            wifi_list_group_items[i] = NULL;
        }
    }
    wifi_list_group_item_count = 0;
}

static void clear_wifi_list(const char *message)
{
    if (wifi_list == NULL) {
        return;
    }

    remove_wifi_list_group_items();
    lv_obj_clean(wifi_list);
    if (message != NULL) {
        lv_obj_t *item = lv_list_add_text(wifi_list, message);
        lv_obj_set_style_text_color(item, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    }
}

static void add_wifi_record_item(const wifi_ap_record_t *record)
{
    if (wifi_list == NULL || record == NULL) {
        return;
    }

    char ssid[33] = {0};
    sanitize_ssid_text(ssid, sizeof(ssid), record->ssid, sizeof(record->ssid));

    char text[96];
    snprintf(text, sizeof(text), "%s  %d dBm  %s", ssid, record->rssi, authmode_to_text(record->authmode));

    lv_obj_t *item = lv_list_add_button(wifi_list, LV_SYMBOL_WIFI, text);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x1E1E1E), LV_PART_MAIN);
    lv_obj_set_style_text_color(item, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(item, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(item, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_radius(item, 8, LV_PART_MAIN);
    lv_obj_set_style_height(item, 42, LV_PART_MAIN);
    lv_obj_set_style_pad_left(item, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(item, 8, LV_PART_MAIN);
    lv_group_add_obj(wifi_group, item);
    if (wifi_list_group_item_count < WIFI_SCAN_MAX_AP) {
        wifi_list_group_items[wifi_list_group_item_count++] = item;
    }
}

static void publish_scan_result_from_task(wifi_scan_result_t *result)
{
    portENTER_CRITICAL(&scan_result_mux);
    pending_scan_result = result;
    pending_scan_result_ready = true;
    portEXIT_CRITICAL(&scan_result_mux);
}

static wifi_scan_result_t *take_pending_scan_result(void)
{
    wifi_scan_result_t *result = NULL;

    portENTER_CRITICAL(&scan_result_mux);
    if (pending_scan_result_ready) {
        result = pending_scan_result;
        pending_scan_result = NULL;
        pending_scan_result_ready = false;
    }
    portEXIT_CRITICAL(&scan_result_mux);

    return result;
}

static void discard_pending_scan_result(void)
{
    wifi_scan_result_t *result = take_pending_scan_result();
    if (result != NULL) {
        free(result);
    }
}

static void process_wifi_scan_result(wifi_scan_result_t *result)
{
    if (result == NULL) {
        return;
    }

    bool can_update = wifi_screen_alive &&
                      result->generation == wifi_screen_generation &&
                      wifi_list != NULL &&
                      wifi_group != NULL;

    if (can_update) {
        log_lv_mem("before scan result");
        update_current_wifi_label();

        if (result->ret == ESP_OK) {
            if (result->record_count == 0) {
                clear_wifi_list("No hotspot found");
            } else {
                clear_wifi_list(NULL);
                for (int i = 0; i < result->record_count; i++) {
                    add_wifi_record_item(&result->records[i]);
                }
            }
        } else {
            clear_wifi_list("Scan failed");
            ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(result->ret));
        }

        set_scan_button_visible(true);
        hide_scan_loading();
        if (scan_btn != NULL) {
            lv_group_focus_obj(scan_btn);
        }
        log_lv_mem("after scan result");
    }

    wifi_scan_running = false;
    scan_task_handle = NULL;
    free(result);
}

static void wifi_scan_result_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_scan_result_t *result = take_pending_scan_result();
    if (result != NULL) {
        process_wifi_scan_result(result);
    }
}

static void wifi_scan_task(void *arg)
{
    wifi_scan_result_t *result = (wifi_scan_result_t *)arg;
    sys_manage_service_t *service = get_system_manage_service_handle();

    if (result == NULL) {
        vTaskDelete(NULL);
        return;
    }

    result->record_count = WIFI_SCAN_MAX_AP;
    result->ret = ESP_ERR_INVALID_STATE;

    if (service != NULL && service->scan_wifi != NULL) {
        result->ret = service->scan_wifi(result->records, &result->record_count);
    } else {
        result->record_count = 0;
    }

    publish_scan_result_from_task(result);
    vTaskDelete(NULL);
}

static void scan_btn_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        start_wifi_scan();
    }
}

static void provision_btn_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_provision_page();
    }
}

static void delete_success_msgbox(void)
{
    if (success_msgbox != NULL) {
        lv_msgbox_close(success_msgbox);
        success_msgbox = NULL;
    }
}

static void delete_provision_timers(void)
{
    if (provision_timer != NULL) {
        lv_timer_delete(provision_timer);
        provision_timer = NULL;
    }
    if (success_exit_timer != NULL) {
        lv_timer_delete(success_exit_timer);
        success_exit_timer = NULL;
    }
}

static void success_exit_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (success_exit_timer != NULL) {
        lv_timer_delete(success_exit_timer);
        success_exit_timer = NULL;
    }

    delete_success_msgbox();
    show_scan_page(false);
}

static void show_success_msgbox(void)
{
    if (success_msgbox != NULL) {
        return;
    }

    success_msgbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(success_msgbox, "WiFi");
    lv_msgbox_add_text(success_msgbox, "Connected");
    lv_obj_set_size(success_msgbox, 180, 110);
    lv_obj_center(success_msgbox);

    success_exit_timer = lv_timer_create(success_exit_timer_cb, 1500, NULL);
    lv_timer_set_repeat_count(success_exit_timer, 1);
}

static void provision_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (wifi_screen_alive && wifi_manager_is_connected()) {
        if (provision_timer != NULL) {
            lv_timer_delete(provision_timer);
            provision_timer = NULL;
        }
        show_success_msgbox();
    }
}

static void provision_exit_btn_click_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        show_scan_page(true);
    }
}

static lv_obj_t *create_round_icon_button(lv_obj_t *parent, const char *symbol, lv_coord_t x_ofs,
                                          lv_color_t bg_color, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 30, 30);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, x_ofs, 4);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(wifi_group, btn);
    lv_obj_set_style_bg_color(btn, bg_color, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(label);
    return btn;
}

static void show_scan_page(bool restart_sta)
{
    delete_provision_timers();
    delete_success_msgbox();

    sys_manage_service_t *service = get_system_manage_service_handle();
    if (service != NULL && service->stop_wifi_provisioning != NULL) {
        service->stop_wifi_provisioning(restart_sta);
    }

    if (provision_con != NULL) {
        lv_obj_add_flag(provision_con, LV_OBJ_FLAG_HIDDEN);
    }
    if (wifi_con != NULL) {
        lv_obj_remove_flag(wifi_con, LV_OBJ_FLAG_HIDDEN);
    }
    update_current_wifi_label();
}

static void show_provision_page(void)
{
    sys_manage_service_t *service = get_system_manage_service_handle();
    if (service == NULL || service->start_wifi_provisioning == NULL) {
        clear_wifi_list("Provision unavailable");
        return;
    }

    esp_err_t ret = service->start_wifi_provisioning();
    if (ret != ESP_OK) {
        clear_wifi_list("Provision failed");
        ESP_LOGW(TAG, "start provisioning failed: %s", esp_err_to_name(ret));
        return;
    }

    char ap_ssid[33] = {0};
    if (service->get_wifi_provisioning_ssid != NULL) {
        service->get_wifi_provisioning_ssid(ap_ssid, sizeof(ap_ssid));
    }

    char qr_payload[96] = {0};
    snprintf(qr_payload, sizeof(qr_payload), "WIFI:T:WPA;P:%s;S:%s;H:false;", WIFI_PROV_AP_PASSWORD, ap_ssid);

    lv_obj_remove_flag(provision_con, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_con, LV_OBJ_FLAG_HIDDEN);

    if (provision_ssid_label != NULL) {
        lv_label_set_text(provision_ssid_label, ap_ssid);
    }

#if LV_USE_QRCODE
    if (provision_qr != NULL) {
        lv_qrcode_update(provision_qr, qr_payload, strlen(qr_payload));
    }
#else
    if (provision_qr_fallback != NULL) {
        lv_label_set_text(provision_qr_fallback, qr_payload);
    }
#endif

    delete_provision_timers();
    provision_timer = lv_timer_create(provision_timer_cb, 500, NULL);
}

static void start_wifi_scan(void)
{
    if (wifi_scan_running) {
        return;
    }

    wifi_scan_result_t *result = (wifi_scan_result_t *)calloc(1, sizeof(wifi_scan_result_t));
    if (result == NULL) {
        clear_wifi_list("Unable to start scan");
        ESP_LOGE(TAG, "No memory for Wi-Fi scan result");
        return;
    }

    result->generation = wifi_screen_generation;
    wifi_scan_running = true;
    set_scan_button_visible(false);
    clear_wifi_list("Scanning nearby hotspots...");
    show_scan_loading();

    BaseType_t ok = xTaskCreate(wifi_scan_task, "wifi_scan_ui", WIFI_SCAN_TASK_STACK,
                                result, 5, &scan_task_handle);
    if (ok != pdPASS) {
        free(result);
        wifi_scan_running = false;
        scan_task_handle = NULL;
        set_scan_button_visible(true);
        hide_scan_loading();
        clear_wifi_list("Unable to start scan");
        ESP_LOGE(TAG, "Failed to create Wi-Fi scan task");
    }
}


static void back_btn_click_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (wifi_scan_running) {
            return;
        }

        deinit_wifi_settings_screen();
        log_lv_mem("before wifi return");
        
        printf("退出WIFI组 return_group\r\n");
        if (return_group) {
            lv_indev_set_group(bsp_display_get_input_dev(), return_group);
        }

        printf("加载屏幕 return_group\r\n");
        lv_screen_load(return_screen);
        log_lv_mem("after wifi return");

    }
}

void init_wifi_settings_screen(lv_obj_t *parent, lv_obj_t *ret_scr, lv_group_t *ret_group)
{

    return_screen = ret_scr;
    return_group = ret_group;

    if (wifi_group != NULL) {
        lv_group_delete(wifi_group);
    }
    wifi_group = lv_group_create();
    wifi_screen_alive = true;
    wifi_screen_generation++;
    

    // 屏幕背景
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    // ===================== 主容器 =====================
    wifi_con = lv_obj_create(parent);
    lv_obj_set_size(wifi_con, 320, 290);
    lv_obj_align(wifi_con, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(wifi_con, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_con, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_con, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_con, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(wifi_con, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(wifi_con, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(wifi_con, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(wifi_con, 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(wifi_con, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(wifi_con, LV_OBJ_FLAG_SCROLLABLE);

    // ===================== 返回按钮 =====================
    back_btn = lv_btn_create(wifi_con);
    lv_obj_set_size(back_btn, 50, 25);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_add_event_cb(back_btn, back_btn_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(wifi_group, back_btn);

    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(back_label);

    // ===================== 标题 =====================
    lv_obj_t *title_label = lv_label_create(wifi_con);
    lv_label_set_text(title_label, "WIFI");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_opa(title_label, LV_OPA_100, 0);  // 不透明

    current_wifi_label = lv_label_create(wifi_con);
    lv_obj_set_width(current_wifi_label, 190);
    lv_label_set_long_mode(current_wifi_label, LV_LABEL_LONG_DOT);
    lv_obj_align(current_wifi_label, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_text_align(current_wifi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(current_wifi_label, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(current_wifi_label, &lv_font_montserrat_14, LV_PART_MAIN);
    update_current_wifi_label();
    create_wifi_status_timer();
    create_scan_result_timer();

    provision_btn = create_round_icon_button(wifi_con, LV_SYMBOL_SETTINGS, -48,
                                             lv_color_hex(0x2A2A2A), provision_btn_click_event_cb);
    scan_btn = create_round_icon_button(wifi_con, LV_SYMBOL_REFRESH, -10,
                                        lv_color_hex(0x0D6EFD), scan_btn_click_event_cb);

    // ===================== 热点列表 =====================
    wifi_list = lv_list_create(wifi_con);
    lv_obj_set_size(wifi_list, 292, 190);
    lv_obj_align(wifi_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wifi_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(wifi_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(wifi_list, 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(wifi_list, LV_DIR_VER);
    clear_wifi_list("No scan data");

    provision_con = lv_obj_create(parent);
    lv_obj_set_size(provision_con, 320, 290);
    lv_obj_align(provision_con, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(provision_con, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(provision_con, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(provision_con, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(provision_con, 16, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(provision_con, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(provision_con, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(provision_con, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *exit_btn = lv_btn_create(provision_con);
    lv_obj_set_size(exit_btn, 50, 25);
    lv_obj_align(exit_btn, LV_ALIGN_TOP_LEFT, 10, 5);
    lv_obj_add_event_cb(exit_btn, provision_exit_btn_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(wifi_group, exit_btn);
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_radius(exit_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(exit_btn, 0, LV_PART_MAIN);

    lv_obj_t *exit_label = lv_label_create(exit_btn);
    lv_label_set_text(exit_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(exit_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(exit_label);

    lv_obj_t *provision_title = lv_label_create(provision_con);
    lv_label_set_text(provision_title, "AP Config");
    lv_obj_align(provision_title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(provision_title, lv_color_white(), LV_PART_MAIN);

    provision_ssid_label = lv_label_create(provision_con);
    lv_label_set_text(provision_ssid_label, "");
    lv_obj_set_width(provision_ssid_label, 260);
    lv_obj_set_style_text_align(provision_ssid_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(provision_ssid_label, lv_color_hex(0xD8D8D8), LV_PART_MAIN);
    lv_obj_align(provision_ssid_label, LV_ALIGN_TOP_MID, 0, 42);

#if LV_USE_QRCODE
    provision_qr = lv_qrcode_create(provision_con);
    lv_qrcode_set_size(provision_qr, WIFI_QR_SIZE);
    lv_qrcode_set_dark_color(provision_qr, lv_color_hex(0x071013));
    lv_qrcode_set_light_color(provision_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_pad_all(provision_qr, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(provision_qr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(provision_qr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(provision_qr, 6, LV_PART_MAIN);
    lv_obj_align(provision_qr, LV_ALIGN_CENTER, 0, 20);
#else
    provision_qr_fallback = lv_label_create(provision_con);
    lv_label_set_text(provision_qr_fallback, "");
    lv_obj_set_width(provision_qr_fallback, 280);
    lv_obj_align(provision_qr_fallback, LV_ALIGN_CENTER, 0, 10);
#endif

    lv_obj_t *hint = lv_label_create(provision_con);
    lv_label_set_text(hint, "Scan QR and open WiFi page");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB8B8B8), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    
}

void open_wifi_page(void)
{
    if (wifi_group != NULL) {
        printf("设置WIFI组lv_indev_set_group\r\n");
        lv_indev_set_group(bsp_display_get_input_dev(),wifi_group);

    }
    update_current_wifi_label();
    set_scan_button_visible(!wifi_scan_running);
}

static void deinit_wifi_settings_screen(void)
{
    wifi_screen_generation++;
    hide_scan_loading();
    set_scan_button_visible(!wifi_scan_running);
}

void close_wifi_settings_screen(void)
{
    wifi_screen_alive = false;
    wifi_screen_generation++;
    delete_wifi_status_timer();
    delete_scan_result_timer();
    discard_pending_scan_result();
    show_scan_page(false);
    deinit_wifi_settings_screen();
    if (wifi_group != NULL) {
        lv_group_remove_all_objs(wifi_group);
        lv_group_delete(wifi_group);
        wifi_group = NULL;
    }

    wifi_con = NULL;
    provision_con = NULL;
    back_btn = NULL;
    provision_btn = NULL;
    scan_btn = NULL;
    scan_overlay = NULL;
    current_wifi_label = NULL;
    wifi_list = NULL;
    provision_ssid_label = NULL;
    provision_qr = NULL;
    provision_qr_fallback = NULL;
    success_msgbox = NULL;
    return_screen = NULL;
    return_group = NULL;
}
