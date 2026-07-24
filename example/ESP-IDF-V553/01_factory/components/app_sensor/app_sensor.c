#include "app_sensor.h"
#include "ui/compass_ui.h"

#include "assets/app_assets.h"
#include "system_manage_service.h"

static char *app_name = "Sensor Test";
static bm_app_desc_t app_obj;
static lv_group_t *s_app_group;

static void app_init(void);
static void app_deinit(void);
static void app_run(void);
static void app_close(void);
static void app_resume(void);
static void app_pause(void);

static qmi8658_dev_t *qmi8658_dev = NULL;
static i2c_master_dev_handle_t shtc3_dev = NULL;
static sys_manage_service_t *sms;

bm_app_desc_t *app_sensor_init(bool status_bar_flag)
{
    app_obj.name = app_name;
    app_obj.icon = &img_app_compass;
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
    sms = get_system_manage_service_handle();
    qmi8658_dev = sms->qmi8658_dev;
    shtc3_dev = sms->shtc3_dev;

}

static void app_deinit(void)
{
    
}

static void app_run(void)
{
    sensor_layout_init(qmi8658_dev,shtc3_dev);
}
static void app_close(void)
{
    sensor_layout_deinit();
}
static void app_resume(void)
{
    
}
static void app_pause(void)
{
    
}

