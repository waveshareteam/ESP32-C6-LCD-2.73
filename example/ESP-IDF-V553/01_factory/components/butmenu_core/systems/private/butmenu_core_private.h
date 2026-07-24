#pragma once

#include "lvgl.h"
#include "systems/stylesheets/butmenu_stylesheets.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
    lv_display_t *disp;
    lv_indev_t *indev;
    lv_group_t  *nav_group;
    lv_obj_t    *bm_sys_scr;
    lv_obj_t    *container;
    lv_obj_t    *app_tileview;
    lv_obj_t    *app_tileview_tiles[BM_MAX_APPS_TITLE];
    lv_obj_t    *page_indicator;
    lv_obj_t    *indicator_items[BM_MAX_APPS_TITLE];
    lv_obj_t    *status_bar;
    lv_obj_t    *time_label;
    lv_obj_t    *icon_container;
    lv_obj_t    *wifi_img;
    lv_obj_t    *battery_img;
}bm_lvgl_obj;

typedef struct 
{
    //lvgl
    bm_style_t *obj_style;
    bm_lvgl_obj obj;

    //core
    bm_app_desc_t *apps[BM_MAX_APPS];
    uint16_t app_count;
    int current_page;
    int app_current_index;
    int total_pages;
}s_butmenu_manager_t;


#ifdef __cplusplus
}
#endif
