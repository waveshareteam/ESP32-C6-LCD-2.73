#include "app_weather.h"

#include "assets/app_weather_assets.h"
#include "ui/weather_ui.h"

static char *app_name = "Weather";
static bm_app_desc_t app_obj;

static void app_init(void);
static void app_deinit(void);
static void app_run(void);
static void app_close(void);
static void app_resume(void);
static void app_pause(void);

bm_app_desc_t *app_weather_init(bool status_bar_flag)
{
    app_obj.name = app_name;
    app_obj.icon = &img_app_weather;
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
    weather_ui_service_init();
}

static void app_deinit(void)
{
}

static void app_run(void)
{
    weather_ui_screen_init(lv_screen_active());
}

static void app_close(void)
{
    weather_ui_screen_deinit();
}

static void app_resume(void)
{
    weather_ui_resume();
}

static void app_pause(void)
{
    weather_ui_pause();
}
