#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
    uint32_t con_bg_color;
}bm_style_container_t;

typedef struct 
{
    uint32_t status_bar_height; 
}bm_style_status_bar_t;


typedef struct 
{
    void *bg_image_src;
    void *app_icon_image_src;
    uint32_t apps_per_page;
    uint32_t icons_per_row;
    uint32_t icon_size; 
    uint32_t icon_gap_col;
    uint32_t icon_gap_row;
    uint32_t icon_label_height;
    uint32_t vertical_offset;
    lv_font_t *icon_label_font;
    uint32_t dot_size;
    uint32_t dot_space;
    uint32_t dot_bottom_offset;
    uint32_t icon_label_font_color;
}bm_style_app_launcher_t;

typedef struct 
{
    bm_style_container_t bm_style_container;
    bm_style_status_bar_t bm_style_status_bar;
    bm_style_app_launcher_t bm_style_app_launcher;
    lv_style_t  *style_focus_key;
}bm_style_t;

#ifdef __cplusplus
}
#endif
