#include "photo_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "include/butmenu.h"

static const char *TAG = "photo";

#define PHOTO_VIEW_W 320
#define PHOTO_VIEW_H 320
#define PHOTO_BTN_W 56
#define PHOTO_BTN_H 26
#define PHOTO_DECODE_MAX_BYTES (64 * 1024)
#define PHOTO_DECODE_ALIGN 8

static generic_file_list_t *s_file_list = NULL;
static int32_t s_current_idx = 0;
static lv_group_t *s_photo_group = NULL;
static jpeg_dec_handle_t s_dec_handle = NULL;
static uint8_t *s_jpeg_out_buf = NULL;
static size_t s_jpeg_out_size = 0;
static lv_image_dsc_t s_img_dsc;
static lv_obj_t *s_image = NULL;
static lv_obj_t *s_title = NULL;

static void show_message(lv_obj_t *parent, const char *message);
static void show_photo_screen(lv_obj_t *parent, generic_file_list_t *file_list);
static void update_photo_display(int32_t index);

static esp_err_t open_decoder(uint16_t width, uint16_t height)
{
    if (s_dec_handle != NULL) {
        jpeg_dec_close(s_dec_handle);
        s_dec_handle = NULL;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    config.scale.width = width;
    config.scale.height = height;

    if (jpeg_dec_open(&config, &s_dec_handle) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static uint16_t align_down_u16(uint32_t value)
{
    return (uint16_t)(value & ~(PHOTO_DECODE_ALIGN - 1U));
}

static void get_decode_size(uint16_t source_width, uint16_t source_height,
                            uint16_t *decode_width, uint16_t *decode_height)
{
    uint32_t width = source_width;
    uint32_t height = source_height;
    const uint32_t max_pixels = PHOTO_DECODE_MAX_BYTES / sizeof(uint16_t);

    while (width * height > max_pixels) {
        width = (width * 7U) / 8U;
        height = (height * 7U) / 8U;
    }

    width = align_down_u16(width);
    height = align_down_u16(height);

    uint32_t min_width = (source_width + 7U) / 8U;
    uint32_t min_height = (source_height + 7U) / 8U;
    min_width = (min_width + 7U) & ~7U;
    min_height = (min_height + 7U) & ~7U;

    if (width < min_width) {
        width = min_width;
    }
    if (height < min_height) {
        height = min_height;
    }

    *decode_width = (uint16_t)width;
    *decode_height = (uint16_t)height;
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

static void free_output_buffer(void)
{
    if (s_jpeg_out_buf != NULL) {
        jpeg_free_align(s_jpeg_out_buf);
        s_jpeg_out_buf = NULL;
        s_jpeg_out_size = 0;
    }
}

static const char *photo_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return (base != NULL) ? base + 1 : path;
}

static void set_title_for_index(int32_t index)
{
    if (s_title == NULL || s_file_list == NULL || s_file_list->count <= 0) {
        return;
    }

    lv_label_set_text_fmt(s_title, "%ld/%d  %s",
                          (long)index + 1,
                          s_file_list->count,
                          photo_basename(s_file_list->list[index]));
}

static void fit_image_to_view(lv_obj_t *img, uint16_t width, uint16_t height)
{
    if (width == 0 || height == 0) {
        return;
    }

    uint32_t scale_w = (PHOTO_VIEW_W * 256U) / width;
    uint32_t scale_h = (PHOTO_VIEW_H * 256U) / height;
    uint32_t scale = (scale_w < scale_h) ? scale_w : scale_h;

    if (scale == 0) {
        scale = 1;
    }
    if (scale > 512) {
        scale = 512;
    }

    lv_image_set_scale(img, (uint16_t)scale);
    lv_obj_center(img);
}

static void update_photo_display(int32_t index)
{
    if (s_file_list == NULL || s_file_list->count <= 0 || s_image == NULL || s_dec_handle == NULL) {
        return;
    }
    if (index < 0 || index >= s_file_list->count) {
        return;
    }

    lv_image_set_src(s_image, NULL);
    free_output_buffer();

    if (open_decoder(0, 0) != ESP_OK) {
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

    uint16_t decode_width = 0;
    uint16_t decode_height = 0;
    get_decode_size(header_info.width, header_info.height, &decode_width, &decode_height);

    if (decode_width != header_info.width || decode_height != header_info.height) {
        ESP_LOGI(TAG, "Scaling JPEG from %ux%u to %ux%u",
                 header_info.width, header_info.height, decode_width, decode_height);

        if (open_decoder(decode_width, decode_height) != ESP_OK) {
            jpeg_free_align(jpeg_buf);
            return;
        }

        memset(&io, 0, sizeof(io));
        io.inbuf = jpeg_buf;
        io.inbuf_len = (int)jpeg_size;
        memset(&header_info, 0, sizeof(header_info));

        jpeg_ret = jpeg_dec_parse_header(s_dec_handle, &io, &header_info);
        if (jpeg_ret != JPEG_ERR_OK) {
            ESP_LOGE(TAG, "Failed to parse scaled jpeg: %s", s_file_list->list[index]);
            jpeg_free_align(jpeg_buf);
            return;
        }
    }

    int outbuf_len = 0;
    jpeg_ret = jpeg_dec_get_outbuf_len(s_dec_handle, &outbuf_len);
    if (jpeg_ret != JPEG_ERR_OK || outbuf_len <= 0) {
        ESP_LOGE(TAG, "Invalid jpeg output size: %d", outbuf_len);
        jpeg_free_align(jpeg_buf);
        return;
    }

    s_jpeg_out_buf = (uint8_t *)jpeg_calloc_align((size_t)outbuf_len, 16);
    if (s_jpeg_out_buf == NULL) {
        ESP_LOGE(TAG, "No memory for jpeg output: %d bytes", outbuf_len);
        jpeg_free_align(jpeg_buf);
        return;
    }
    s_jpeg_out_size = (size_t)outbuf_len;
    io.outbuf = s_jpeg_out_buf;

    jpeg_ret = jpeg_dec_process(s_dec_handle, &io);
    jpeg_free_align(jpeg_buf);

    if (jpeg_ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to decode jpeg: %s", s_file_list->list[index]);
        free_output_buffer();
        return;
    }

    s_current_idx = index;
    memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    s_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.w = header_info.width;
    s_img_dsc.header.h = header_info.height;
    s_img_dsc.header.stride = header_info.width * 2;
    s_img_dsc.data_size = s_jpeg_out_size;
    s_img_dsc.data = s_jpeg_out_buf;

    lv_image_set_src(s_image, &s_img_dsc);
    fit_image_to_view(s_image, header_info.width, header_info.height);
    set_title_for_index(index);
}

static void prev_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_file_list == NULL || s_file_list->count <= 0) {
        return;
    }

    int32_t next = s_current_idx - 1;
    if (next < 0) {
        next = s_file_list->count - 1;
    }
    update_photo_display(next);
}

static void next_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || s_file_list == NULL || s_file_list->count <= 0) {
        return;
    }

    int32_t next = s_current_idx + 1;
    if (next >= s_file_list->count) {
        next = 0;
    }
    update_photo_display(next);
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

    s_title = lv_label_create(parent);
    lv_obj_set_width(s_title, 300);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_color(s_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 6);

    s_image = lv_image_create(parent);
    lv_obj_center(s_image);
    lv_obj_remove_flag(s_image, LV_OBJ_FLAG_SCROLLABLE);

    create_control_button(parent, LV_SYMBOL_LEFT, LV_ALIGN_BOTTOM_LEFT, 8, -8, prev_btn_event_cb);
    create_control_button(parent, "Back", LV_ALIGN_BOTTOM_MID, 0, -8, back_btn_event_cb);
    create_control_button(parent, LV_SYMBOL_RIGHT, LV_ALIGN_BOTTOM_RIGHT, -8, -8, next_btn_event_cb);

    update_photo_display(0);
}

esp_err_t init_photo_player(void)
{
    if (s_dec_handle != NULL) {
        return ESP_OK;
    }

    return open_decoder(0, 0);
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

    free_output_buffer();

    if (s_dec_handle != NULL) {
        jpeg_dec_close(s_dec_handle);
        s_dec_handle = NULL;
    }

    memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    s_file_list = NULL;
    s_current_idx = 0;
    s_image = NULL;
    s_title = NULL;

    ESP_LOGI(TAG, "Photo player deinitialized");
}
