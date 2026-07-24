#pragma once

#include "lvgl.h"
#include "esp_err.h"
#include "systems/stylesheets/butmenu_stylesheets.h"

#define BM_MAX_APPS 32
#define BM_MAX_APPS_TITLE 10 //最大页面数量

#ifdef __cplusplus
extern "C" {
#endif

typedef struct 
{
    lv_display_t *disp;
    lv_indev_t *indev;
}bm_config_t;

typedef struct {
    const char *name;
    const lv_image_dsc_t *icon;
    void (*init)(void);
    void (*deinit)(void);
    void (*run)(void);
    void (*close)(void);
    void (*resume)(void);
    void (*pause)(void);
    lv_obj_t *app_scr;
    bool status_bar_flag;
} bm_app_desc_t;

typedef struct s_butmenu_manager_t butmenu_manager_t;

butmenu_manager_t *butmenu_init(bm_config_t *bm_config,bm_style_t *bm_style);
esp_err_t butmenu_register_app(butmenu_manager_t *bm, const bm_app_desc_t *app);
esp_err_t butmenu_start(butmenu_manager_t *bm);
esp_err_t butmenu_stop(butmenu_manager_t *bm);

// app ctrl
esp_err_t bm_app_close_notify_handle(void);


void bm_set_battery_level_icon(butmenu_manager_t *bm, int battery_level);
void bm_set_wifi_level_icon(butmenu_manager_t *bm, int wifi_level);
void bm_set_time_label(butmenu_manager_t *bm, const int hour,const int min,const int sec);



#ifdef __cplusplus
}
#endif
