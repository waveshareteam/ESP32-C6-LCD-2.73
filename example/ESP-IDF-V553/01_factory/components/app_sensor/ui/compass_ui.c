#include "compass_ui.h"
#include "include/butmenu.h"
static const char *TAG = "qmi8658";
static qmi8658_dev_t *qmi8658_dev = NULL;
static i2c_master_dev_handle_t shtc3_dev = NULL;
static lv_timer_t * auto_step_timer = NULL;
static // 全局标签数组（8个数据项）
lv_obj_t *labels_qmi8658_data[8];  // 0:AccelX, 1:AccelY, 2:AccelZ, 3:Temp, // 4:GyroX, 5:GyroY, 6:GyroZ, 7:Time
lv_obj_t *labels_shtc3_data[2];    // 0:temp, 1:hum

static lv_timer_t *timer = NULL;
static void example1_increase_lvgl_tick(lv_timer_t * t);
static lv_obj_t *back_btn = NULL;
static lv_group_t *sensor_group = NULL;
lv_obj_t *con;
static char buf[10][16];

static void back_btn_click_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {

        lv_group_delete(sensor_group);
        bm_app_close_notify_handle();

    }
}

// 工具函数：创建带宽度、自动滚动的传感器标签
static lv_obj_t *create_sensor_label(lv_obj_t *parent, const char *name, const char *init_val, lv_color_t color)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_text_fmt(lab, "%s\n%s", name, init_val);
    lv_obj_set_style_text_color(lab, color, 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lab, 110);          // 固定宽度，防止挤在一起
    lv_label_set_long_mode(lab, LV_LABEL_LONG_SCROLL); // 超长自动滚动
    return lab;
}

// 主初始化函数
void sensor_layout_init(qmi8658_dev_t *qmi8658_dev_tmp,i2c_master_dev_handle_t shtc3_dev_tmp)
{
    if(qmi8658_dev_tmp==NULL || shtc3_dev_tmp==NULL){
        return;
    }

    qmi8658_dev = qmi8658_dev_tmp;
    shtc3_dev = shtc3_dev_tmp;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    sensor_group = lv_group_create();
    lv_indev_set_group(bsp_display_get_input_dev(), sensor_group);

    // 主容器
    con = lv_obj_create(screen);
    lv_obj_set_size(con, 320, 320);
    lv_obj_align(con, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(con, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(con, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(con, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(con, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(con, 4, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(con, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(con, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(con, 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(con, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(con, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(con, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(con, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_SPACE_AROUND);
    lv_obj_set_style_pad_all(con, 10, 0);
    lv_obj_set_style_pad_row(con, 10, 0);

    // ===================== 容器1：QMI8658 =====================
    lv_obj_t *cont_qmi = lv_obj_create(con);
    lv_obj_set_size(cont_qmi, 300, 175);
    lv_obj_set_style_bg_color(cont_qmi, lv_color_hex(0x262626), 0);
    lv_obj_set_style_radius(cont_qmi, 12, 0);
    lv_obj_set_style_border_width(cont_qmi, 0, 0);
    lv_obj_set_style_pad_all(cont_qmi, 10, 0);
    //lv_obj_set_flex_flow(cont_qmi, LV_FLEX_FLOW_COLUMN); // 垂直布局
    lv_obj_set_scrollbar_mode(cont_qmi, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(cont_qmi, LV_OBJ_FLAG_SCROLLABLE);

    // QMI 标题（顶部居中）
    lv_obj_t *title_qmi = lv_label_create(cont_qmi);
    lv_label_set_text(title_qmi, "QMI8658 (Accel/Gyro)");
    lv_obj_set_style_text_color(title_qmi, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_qmi, &lv_font_montserrat_14, 0);
    lv_obj_align(title_qmi, LV_ALIGN_TOP_MID, 0, 0);

    // QMI 数据区域（两行，每行3+3个，整齐排列）
    lv_obj_t *qmi_data_cont = lv_obj_create(cont_qmi);
    lv_obj_set_size(qmi_data_cont, 280, 130);
    lv_obj_align(qmi_data_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(qmi_data_cont, 0, 0);
    lv_obj_set_style_border_width(qmi_data_cont, 0, 0);
    lv_obj_set_flex_flow(qmi_data_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(qmi_data_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_AROUND);
    lv_obj_align_to(qmi_data_cont, title_qmi, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_scrollbar_mode(qmi_data_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(qmi_data_cont, LV_OBJ_FLAG_SCROLLABLE);

    // 创建标签（严格按你的索引）
    labels_qmi8658_data[0] = create_sensor_label(qmi_data_cont, "Accel X:", "--.- m/s²", lv_color_hex(0x00FFFF));
    labels_qmi8658_data[4] = create_sensor_label(qmi_data_cont, "Gyro X:",  "--.- rad/s", lv_color_hex(0xFFAA00));

    labels_qmi8658_data[1] = create_sensor_label(qmi_data_cont, "Accel Y:", "--.- m/s²", lv_color_hex(0x00FFFF));
    labels_qmi8658_data[5] = create_sensor_label(qmi_data_cont, "Gyro Y:",  "--.- rad/s", lv_color_hex(0xFFAA00));

    labels_qmi8658_data[2] = create_sensor_label(qmi_data_cont, "Accel Z:", "--.- m/s²", lv_color_hex(0x00FFFF));
    labels_qmi8658_data[6] = create_sensor_label(qmi_data_cont, "Gyro Z:",  "--.- rad/s", lv_color_hex(0xFFAA00));

    labels_qmi8658_data[3] = create_sensor_label(qmi_data_cont, "Temp:",    "--.- °C",    lv_color_hex(0x00FF00));
    labels_qmi8658_data[7] = create_sensor_label(qmi_data_cont, "Time:",    "---- ms",    lv_color_hex(0xBB88FF));

    // ===================== 容器2：SHTC3 =====================
    lv_obj_t *cont_shtc = lv_obj_create(con);
    lv_obj_set_size(cont_shtc, 300, 80);
    lv_obj_set_style_bg_color(cont_shtc, lv_color_hex(0x262626), 0);
    lv_obj_set_style_radius(cont_shtc, 12, 0);
    lv_obj_set_style_border_width(cont_shtc, 0, 0);
    lv_obj_set_style_pad_all(cont_shtc, 10, 0);
    //lv_obj_set_flex_flow(cont_shtc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(cont_shtc, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(cont_shtc, LV_OBJ_FLAG_SCROLLABLE);

    // SHTC3 标题（顶部居中）
    lv_obj_t *title_shtc = lv_label_create(cont_shtc);
    lv_label_set_text(title_shtc, "SHTC3 (Temp/Humi)");
    lv_obj_set_style_text_color(title_shtc, lv_color_white(), 0);
    lv_obj_set_style_text_font(title_shtc, &lv_font_montserrat_14, 0);
    lv_obj_align(title_shtc, LV_ALIGN_TOP_MID, 0, 0); // 顶部居中

    // SHTC3 数据区域
    lv_obj_t *shtc_data_cont = lv_obj_create(cont_shtc);
    lv_obj_set_size(shtc_data_cont, 280, 45);
    lv_obj_set_style_bg_opa(shtc_data_cont, 0, 0);
    lv_obj_set_style_border_width(shtc_data_cont, 0, 0);
    lv_obj_set_flex_flow(shtc_data_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shtc_data_cont, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align_to(shtc_data_cont, title_shtc, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_scrollbar_mode(shtc_data_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(shtc_data_cont, LV_OBJ_FLAG_SCROLLABLE);

    // SHTC3 标签
    labels_shtc3_data[0] = create_sensor_label(shtc_data_cont, "Temp:", "--.- °C",    lv_color_hex(0x00FF00));
    labels_shtc3_data[1] = create_sensor_label(shtc_data_cont, "Humi:", "--.- %% RH", lv_color_hex(0xBB88FF));

    // ===================== 返回按钮 =====================
    back_btn = lv_btn_create(con);
    lv_obj_set_size(back_btn, 50, 20);
    lv_obj_add_event_cb(back_btn, back_btn_click_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(sensor_group, back_btn);

    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);

    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_center(back_label);

    timer = lv_timer_create(example1_increase_lvgl_tick, 300, NULL);
}



static void example1_increase_lvgl_tick(lv_timer_t * t)
{
    qmi8658_data_t data;
    bool ready;
    float shtc_temp, shtc_humi;
    shtc3_register_rw_t reg = SHTC3_REG_T_CSD_NM;

    // 读取 QMI8658
    esp_err_t ret = qmi8658_is_data_ready(qmi8658_dev, &ready);
    if (ret == ESP_OK && ready) {
        ret = qmi8658_read_sensor_data(qmi8658_dev, &data);
        if (ret == ESP_OK) {
            sprintf(buf[0], "%.2f", data.accelX);
            sprintf(buf[1], "%.2f", data.accelY);
            sprintf(buf[2], "%.2f", data.accelZ);
            sprintf(buf[3], "%.2f", data.temperature);
            sprintf(buf[4], "%.2f", data.gyroX);
            sprintf(buf[5], "%.2f", data.gyroY);
            sprintf(buf[6], "%.2f", data.gyroZ);

            lv_label_set_text_fmt(labels_qmi8658_data[0], "Accel X:\n %s", buf[0]);
            lv_label_set_text_fmt(labels_qmi8658_data[1], "Accel Y:\n %s", buf[1]);
            lv_label_set_text_fmt(labels_qmi8658_data[2], "Accel Z:\n %s", buf[2]);
            lv_label_set_text_fmt(labels_qmi8658_data[3], "Temp:\n %s °C", buf[3]);
            lv_label_set_text_fmt(labels_qmi8658_data[4], "Gyro X:\n %s", buf[4]);
            lv_label_set_text_fmt(labels_qmi8658_data[5], "Gyro Y:\n %s", buf[5]);
            lv_label_set_text_fmt(labels_qmi8658_data[6], "Gyro Z:\n %s", buf[6]);
            lv_label_set_text_fmt(labels_qmi8658_data[7], "Time:\n %lu ms", data.timestamp);
        }
    }

    // 读取 SHTC3
    shtc3_get_th(shtc3_dev, reg, &shtc_temp, &shtc_humi);
    sprintf(buf[8], "%.2f", shtc_temp);
    sprintf(buf[9], "%.2f", shtc_humi);

    lv_label_set_text_fmt(labels_shtc3_data[0], "Temp:\n %s °C", buf[8]);
    lv_label_set_text_fmt(labels_shtc3_data[1], "Humi:\n %s %% RH", buf[9]);

    //ESP_LOGI("TAG", "SHTC3 → %.2f°C, %.2f%%RH", shtc_temp, shtc_humi);
}


void sensor_layout_deinit(void)
{
    lv_timer_delete(timer);
}