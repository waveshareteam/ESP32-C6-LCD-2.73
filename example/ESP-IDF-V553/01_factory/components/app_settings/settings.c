#include "settings.h"
#include "ui/settings_ui.h"
#include "ui/set_wifi_service.h"
#include "assets/app_setting_assets.h"

static char *app_name = "settings";
static bm_app_desc_t app_obj;
static lv_group_t *s_app_group;

static void app_init(void);
static void app_deinit(void);
static void app_run(void);
static void app_close(void);
static void app_resume(void);
static void app_pause(void);

bm_app_desc_t *app_setting_init(bool status_bar_flag)
{
    app_obj.name = app_name;
    app_obj.icon = &img_app_settings;
    app_obj.init = app_init;
    app_obj.deinit = app_deinit;
    app_obj.run = app_run;
    app_obj.close = app_close;
    app_obj.resume = app_resume;
    app_obj.pause = app_pause;
    app_obj.status_bar_flag = status_bar_flag;
    
    return &app_obj;
}


static void app_init(void)
{

}

static void app_deinit(void)
{
    
}



static void list_last_item_cb(lv_event_t *e)
{
    lv_obj_t *list = lv_event_get_current_target(e);
    ESP_LOGI("APP", "最后一项被点击！执行回调");

    bm_app_close_notify_handle();
}



static void app_run(void)
{
    settings_ui_screen_init();
}
static void app_close(void)
{
    settings_ui_screen_deinit();
}
static void app_resume(void)
{
    
}
static void app_pause(void)
{
    
}

