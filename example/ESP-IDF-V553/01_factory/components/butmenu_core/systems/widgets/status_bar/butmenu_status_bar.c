#include "systems/widgets/status_bar/butmenu_status_bar.h"
#include "systems/private/butmenu_core_private.h"
#include "systems/assets/butmenu_assets.h"


// ==============================
// 顶部状态栏（中间时间 + 右侧 WIFI + 电池）
// 可调参数：状态栏高度
// ==============================
void status_bar_create(butmenu_manager_t *bm)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;

    const int status_bar_height = bm_tmp->obj_style->bm_style_status_bar.status_bar_height; 

    // 创建在顶层
    lv_obj_t *top_layer = lv_layer_top();

    // 状态栏根容器
    bm_tmp->obj.status_bar = lv_obj_create(top_layer);
    lv_obj_remove_style_all(bm_tmp->obj.status_bar);
    lv_obj_set_size(bm_tmp->obj.status_bar, lv_display_get_horizontal_resolution(NULL), status_bar_height);
    lv_obj_align(bm_tmp->obj.status_bar, LV_ALIGN_TOP_MID, 0, 0);

    // 无背景、无边框
    lv_obj_set_style_bg_opa(bm_tmp->obj.status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm_tmp->obj.status_bar, 0, 0);

    // 布局：左右分布
    lv_obj_set_layout(bm_tmp->obj.status_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bm_tmp->obj.status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bm_tmp->obj.status_bar,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_left(bm_tmp->obj.status_bar, 8, 0);
    lv_obj_set_style_pad_right(bm_tmp->obj.status_bar, 8, 0);

    // ==================
    // 中间：时间标签
    // ==================
    bm_tmp->obj.time_label = lv_label_create(bm_tmp->obj.status_bar);
    lv_label_set_text(bm_tmp->obj.time_label, "00:00");
    lv_obj_set_style_text_color(bm_tmp->obj.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(bm_tmp->obj.time_label, &lv_font_montserrat_14, 0);
    lv_obj_center(bm_tmp->obj.time_label);

    // ==================
    // 右侧容器：WIFI + 电池
    // ==================
    bm_tmp->obj.icon_container = lv_obj_create(bm_tmp->obj.status_bar);
    lv_obj_remove_style_all(bm_tmp->obj.icon_container);
    lv_obj_set_size(bm_tmp->obj.icon_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(bm_tmp->obj.icon_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bm_tmp->obj.icon_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bm_tmp->obj.icon_container, 6, 0);
    lv_obj_set_style_bg_opa(bm_tmp->obj.icon_container, LV_OPA_TRANSP, 0);

    // WIFI 图标
    bm_tmp->obj.wifi_img = lv_image_create(bm_tmp->obj.icon_container);
    lv_image_set_src(bm_tmp->obj.wifi_img, &butmenu_image_middle_status_bar_wifi_close_24_24);
    lv_obj_set_size(bm_tmp->obj.wifi_img, 24, 24);

    // 电池图标
    bm_tmp->obj.battery_img = lv_image_create(bm_tmp->obj.icon_container);
    lv_image_set_src(bm_tmp->obj.battery_img, &butmenu_image_middle_status_bar_battery_level4_24_24);
    lv_obj_set_size(bm_tmp->obj.battery_img, 24, 24);
}


void status_bar_set_time(butmenu_manager_t *bm, const int hour,const int min,const int sec)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;
    if (bm_tmp->obj.time_label) {
        lv_label_set_text_fmt(bm_tmp->obj.time_label, "%02d:%02d:%02d",hour,min,sec);
    }
}

void status_bar_set_wifi(butmenu_manager_t *bm, int wifi_level)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;
    const lv_image_dsc_t *img = NULL;

    switch(wifi_level) {
        case 0: img = &butmenu_image_middle_status_bar_wifi_close_24_24; break;
        case 1: img = &butmenu_image_middle_status_bar_wifi_level1_24_24; break;
        case 2: img = &butmenu_image_middle_status_bar_wifi_level2_24_24; break;
        case 3: img = &butmenu_image_middle_status_bar_wifi_level3_24_24; break;
        default: img = &butmenu_image_middle_status_bar_wifi_close_24_24; break;
    }

    if (img && bm_tmp->obj.wifi_img) {
        lv_image_set_src(bm_tmp->obj.wifi_img, img);
    }
}

void status_bar_set_battery(butmenu_manager_t *bm, int battery_level)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;
    const lv_image_dsc_t *img = NULL;

    switch(battery_level) {
        case 1: img = &butmenu_image_middle_status_bar_battery_level1_24_24; break;
        case 2: img = &butmenu_image_middle_status_bar_battery_level2_24_24; break;
        case 3: img = &butmenu_image_middle_status_bar_battery_level3_24_24; break;
        case 4: img = &butmenu_image_middle_status_bar_battery_level4_24_24; break;
        case 99: img = &butmenu_image_middle_status_bar_battery_charge_24_24; break;
        default: img = &butmenu_image_middle_status_bar_battery_level4_24_24; break;
    }

    if (img && bm_tmp->obj.battery_img) {
        lv_image_set_src(bm_tmp->obj.battery_img, img);
    }
}