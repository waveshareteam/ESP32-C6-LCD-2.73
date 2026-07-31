#include "photo_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "bsp/display.h"
#include "include/butmenu.h"

static const char *TAG = "photo";

#define PHOTO_BTN_W 56
#define PHOTO_BTN_H 26

static generic_file_list_t *s_file_list = NULL;
static int32_t s_current_idx = 0;
static lv_group_t *s_photo_group = NULL;
static jpeg_dec_handle_t s_dec_handle = NULL;
static lv_obj_t *s_key_catcher = NULL;

static void show_message(lv_obj_t *parent, const char *message);
static void show_photo_screen(lv_obj_t *parent, generic_file_list_t *file_list);
static void update_photo_display(int32_t index);

static esp_err_t open_decoder(void)
{
    if (s_dec_handle != NULL) {
        jpeg_dec_close(s_dec_handle);
        s_dec_handle = NULL;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    /* The ILI9488 driver accepts byte-swapped RGB565 before converting it to
     * the panel's RGB666 wire format, so request big-endian bytes here. */
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE;
    config.block_enable = true;

    if (jpeg_dec_open(&config, &s_dec_handle) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static uint8_t *read_file_to_buffer(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);
    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }

    rewind(file);

    size_t aligned_size = ((size_t)file_size + 15) & ~((size_t)15);
    uint8_t *buf = (uint8_t *)jpeg_calloc_align(aligned_size, 16);
    if (buf == NULL) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(buf, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        jpeg_free_align(buf);
        return NULL;
    }

    *size = (size_t)file_size;
    return buf;
}

static void update_photo_display(int32_t index)
{
    if (s_file_list == NULL || s_file_list->count <= 0 || s_dec_handle == NULL) {
        return;
    }
    if (index < 0 || index >= s_file_list->count) {
        return;
    }

    if (open_decoder() != ESP_OK) {
        return;
    }

    size_t jpeg_size = 0;
    uint8_t *jpeg_buf = read_file_to_buffer(s_file_list->list[index], &jpeg_size);
    if (jpeg_buf == NULL) {
        ESP_LOGE(TAG, "Failed to read image: %s", s_file_list->list[index]);
        return;
    }

    jpeg_dec_io_t io = {
        .inbuf = jpeg_buf,
        .inbuf_len = (int)jpeg_size,
        .outbuf = NULL,
    };
    jpeg_dec_header_info_t header_info = {0};

    jpeg_error_t jpeg_ret = jpeg_dec_parse_header(s_dec_handle, &io, &header_info);
    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to parse jpeg header: %s", s_file_list->list[index]);
        jpeg_free_align(jpeg_buf);
        return;
    }

    if ((header_info.width & 7U) != 0 || (header_info.height & 7U) != 0 ||
        header_info.width > BSP_LCD_H_RES || header_info.height > BSP_LCD_V_RES) {
        ESP_LOGE(TAG, "Block mode requires <=320x320 and dimensions divisible by 8: %ux%u",
                 header_info.width, header_info.height);
        jpeg_free_align(jpeg_buf);
        return;
    }

    int outbuf_len = 0;
    jpeg_ret = jpeg_dec_get_outbuf_len(s_dec_handle, &outbuf_len);
    if (jpeg_ret != JPEG_ERR_OK || outbuf_len <= 0) {
        ESP_LOGE(TAG, "Invalid jpeg output size: %d", outbuf_len);
        jpeg_free_align(jpeg_buf);
        return;
    }

    uint8_t *block_buf = (uint8_t *)jpeg_calloc_align((size_t)outbuf_len, 16);
    if (block_buf == NULL) {
        ESP_LOGE(TAG, "No memory for jpeg block: %d bytes", outbuf_len);
        jpeg_free_align(jpeg_buf);
        return;
    }
    io.outbuf = block_buf;

    int process_count = 0;
    jpeg_ret = jpeg_dec_get_process_count(s_dec_handle, &process_count);
    if (jpeg_ret != JPEG_ERR_OK || process_count <= 0) {
        ESP_LOGE(TAG, "Invalid jpeg block count: %d", process_count);
        jpeg_free_align(block_buf);
        jpeg_free_align(jpeg_buf);
        return;
    }

    /* Paint the LVGL background first. JPEG blocks are then written directly
     * to the panel, so no full RGB565 image has to remain in RAM. */
    lv_refr_now(NULL);
    int x = (BSP_LCD_H_RES - header_info.width) / 2;
    int y = (BSP_LCD_V_RES - header_info.height) / 2;
    int drawn_lines = 0;

    for (int block = 0; block < process_count; block++) {
        jpeg_ret = jpeg_dec_process(s_dec_handle, &io);
        if (jpeg_ret != JPEG_ERR_OK || io.out_size <= 0) {
            ESP_LOGE(TAG, "JPEG block %d decode failed", block);
            break;
        }

        int lines = io.out_size / (header_info.width * sizeof(uint16_t));
        if (lines <= 0 || drawn_lines + lines > header_info.height ||
            bsp_display_draw_bitmap(x, y + drawn_lines,
                                    x + header_info.width, y + drawn_lines + lines,
                                    block_buf) != ESP_OK) {
            ESP_LOGE(TAG, "JPEG block %d display failed", block);
            jpeg_ret = JPEG_ERR_FAIL;
            break;
        }
        drawn_lines += lines;
    }

    jpeg_free_align(block_buf);
    jpeg_free_align(jpeg_buf);
    if (jpeg_ret != JPEG_ERR_OK || drawn_lines != header_info.height) {
        return;
    }

    s_current_idx = index;
    ESP_LOGI(TAG, "Displayed JPEG at native resolution: %ux%u, block buffer %d bytes",
             header_info.width, header_info.height, outbuf_len);
}

static void photo_key_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY ||
        s_file_list == NULL || s_file_list->count <= 0) {
        return;
    }

    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_LEFT) {
        int32_t next = s_current_idx - 1;
        update_photo_display(next < 0 ? s_file_list->count - 1 : next);
    } else if (key == LV_KEY_RIGHT) {
        int32_t next = s_current_idx + 1;
        update_photo_display(next >= s_file_list->count ? 0 : next);
    }
}

static void back_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_photo_group != NULL) {
        lv_group_delete(s_photo_group);
        s_photo_group = NULL;
    }
    bm_app_close_notify_handle();
}

static void photo_exit_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        bm_app_close_notify_handle();
    }
}

static lv_obj_t *create_control_button(lv_obj_t *parent, const char *text, lv_align_t align,
                                       int32_t x, int32_t y, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, PHOTO_BTN_W, PHOTO_BTN_H);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3A3A), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_center(label);

    lv_group_add_obj(s_photo_group, btn);
    return btn;
}

static void setup_photo_group(void)
{
    if (s_photo_group != NULL) {
        lv_group_delete(s_photo_group);
    }
    s_photo_group = lv_group_create();
    lv_indev_set_group(bsp_display_get_input_dev(), s_photo_group);
}

static void show_message(lv_obj_t *parent, const char *message)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    setup_photo_group();

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, message);
    lv_obj_set_width(label, 280);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -18);

    create_control_button(parent, "Back", LV_ALIGN_BOTTOM_MID, 0, -8, back_btn_event_cb);
}

static void show_photo_screen(lv_obj_t *parent, generic_file_list_t *file_list)
{
    s_file_list = file_list;
    s_current_idx = 0;

    lv_obj_set_style_bg_color(parent, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    setup_photo_group();

    /* One invisible focused object receives all three physical navigation
     * keys, leaving every LCD pixel available for the photograph. */
    s_key_catcher = lv_obj_create(parent);
    lv_obj_set_size(s_key_catcher, 1, 1);
    lv_obj_set_pos(s_key_catcher, 0, 0);
    lv_obj_set_style_bg_opa(s_key_catcher, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_key_catcher, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_width(s_key_catcher, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_key_catcher, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_key_catcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_key_catcher, photo_key_event_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(s_key_catcher, photo_exit_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(s_photo_group, s_key_catcher);
    lv_group_focus_obj(s_key_catcher);
    lv_group_set_editing(s_photo_group, true);

    update_photo_display(0);
}

esp_err_t init_photo_player(void)
{
    if (s_dec_handle != NULL) {
        return ESP_OK;
    }

    return open_decoder();
}

void init_photo_ui_screen(lv_obj_t *parent, generic_file_list_t *file_list, const char *message)
{
    if (parent == NULL) {
        return;
    }

    if (message != NULL) {
        show_message(parent, message);
        return;
    }

    if (file_list == NULL || file_list->count <= 0) {
        show_message(parent, "No JPG images found");
        return;
    }

    show_photo_screen(parent, file_list);
}

void deinit_photo_player(void)
{
    if (s_photo_group != NULL) {
        lv_group_remove_all_objs(s_photo_group);
        lv_group_delete(s_photo_group);
        s_photo_group = NULL;
    }

    if (s_dec_handle != NULL) {
        jpeg_dec_close(s_dec_handle);
        s_dec_handle = NULL;
    }

    s_file_list = NULL;
    s_current_idx = 0;
    s_key_catcher = NULL;

    ESP_LOGI(TAG, "Photo player deinitialized");
}
