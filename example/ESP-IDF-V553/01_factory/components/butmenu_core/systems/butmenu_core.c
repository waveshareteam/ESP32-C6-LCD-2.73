#include "include/butmenu.h"
#include "private/butmenu_core_private.h"
#include "assets/butmenu_assets.h"
#include "systems/stylesheets/style_default.h"
#include "systems/widgets/indicator/butmenu_indicator.h"
#include "systems/widgets/status_bar/butmenu_status_bar.h"
#include "esp_log.h"

static const char *TAG = "butmenu_core";

static bool s_butmenu_init = false;
static s_butmenu_manager_t *s_bm = NULL;
static lv_style_t style_focus_key;

static void style_focus_init(void)
{
    // 只初始化一次
    static bool inited = false;
    if (inited) return;
    inited = true;

    lv_style_init(&style_focus_key);

    // 聚焦框设置（按键焦点才生效）
    lv_style_set_outline_width(&style_focus_key, 2);    // 框宽度
    lv_style_set_outline_color(&style_focus_key, lv_color_hex(0xFFD700)); // 金黄色
    lv_style_set_outline_pad(&style_focus_key, 3);        // 向外扩3px，保证不裁切
    lv_style_set_outline_opa(&style_focus_key, 255);      // 完全显示
}

butmenu_manager_t *butmenu_init(bm_config_t *bm_config,bm_style_t *bm_style)
{
    if (s_butmenu_init) {
        return NULL;
    }

    if (bm_config->disp == NULL || bm_config->indev == NULL) {
        return NULL;
    }

    s_bm = (s_butmenu_manager_t *)calloc(1, sizeof(s_butmenu_manager_t));
    if (!s_bm) {
        return NULL;
    }
    s_bm->obj.disp = bm_config->disp;
    s_bm->obj.indev = bm_config->indev;
    
    if(bm_style == NULL){
        s_bm->obj_style = &bm_style_default;
    }
    else {
        s_bm->obj_style = bm_style;
    }

    int screen_w = lv_display_get_horizontal_resolution(bm_config->disp);
    int screen_h = lv_display_get_vertical_resolution(bm_config->disp);

    //创建bm界面管理器主页屏幕
    s_bm->obj.bm_sys_scr = lv_obj_create(NULL);
    lv_screen_load(s_bm->obj.bm_sys_scr);

    //create container
    s_bm->obj.container = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_bm->obj.container,screen_w,screen_h);
    lv_obj_set_style_radius(s_bm->obj.container, 0, 0);
    lv_obj_set_style_border_width(s_bm->obj.container, 0, 0);  
    lv_obj_set_style_bg_opa(s_bm->obj.container, LV_OPA_100, 0);  
    lv_obj_set_scrollbar_mode(s_bm->obj.container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_bm->obj.container, lv_color_hex(s_bm->obj_style->bm_style_container.con_bg_color), 0);

    //create bg image
    const lv_image_dsc_t *img_dsc = s_bm->obj_style->bm_style_app_launcher.bg_image_src;
    int img_w = img_dsc->header.w;
    int img_h = img_dsc->header.h;

    float scale_w = (float)screen_w / img_w;
    float scale_h = (float)screen_h / img_h;
    float scale = (scale_w < scale_h) ? scale_w : scale_h;
    uint16_t scale_val = (uint16_t)(scale * 256) + 5;

    lv_obj_t *bg_image = lv_image_create(s_bm->obj.container);
    lv_image_set_src(bg_image, img_dsc);
    lv_image_set_scale(bg_image,scale_val);
    lv_obj_center(bg_image);
    lv_obj_set_scrollbar_mode(s_bm->obj.container, LV_SCROLLBAR_MODE_OFF);

    if(s_bm->obj_style->style_focus_key == NULL){
        style_focus_init();
        s_bm->obj_style->style_focus_key = &style_focus_key;
    }

    status_bar_create((butmenu_manager_t *)s_bm);
    
    s_butmenu_init = true;
    return (butmenu_manager_t *)s_bm;
}


esp_err_t butmenu_register_app(butmenu_manager_t *bm, const bm_app_desc_t *app)
{
    if (!s_butmenu_init || bm == NULL || app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t*)bm;

    if (bm_tmp->app_count >= BM_MAX_APPS) {
        return ESP_ERR_NO_MEM;
    }

    bm_tmp->apps[bm_tmp->app_count] = (bm_app_desc_t *)app;

    bm_tmp->app_count++;

    return ESP_OK;
}

static void app_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        s_bm->app_current_index = (int)(intptr_t)lv_event_get_user_data(e);
        ESP_LOGI(TAG,"App %d clicked!", s_bm->app_current_index);
        if(s_bm->apps[s_bm->app_current_index]->run != NULL)
        {
            if(s_bm->apps[s_bm->app_current_index]->status_bar_flag)
            {
                lv_obj_add_flag(s_bm->obj.status_bar,LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_add_flag(s_bm->obj.page_indicator,LV_OBJ_FLAG_HIDDEN);
            s_bm->apps[s_bm->app_current_index]->app_scr = lv_obj_create(NULL);
            lv_screen_load(s_bm->apps[s_bm->app_current_index]->app_scr);
            s_bm->apps[s_bm->app_current_index]->run();
        }
    }

    if (code == LV_EVENT_FOCUSED) {
        int idx = (int)(intptr_t)lv_event_get_user_data(e);
        int apps_per_page = s_bm->obj_style->bm_style_app_launcher.apps_per_page;
        int target_page = idx / apps_per_page;

        if (target_page != s_bm->current_page) {
            s_bm->current_page = target_page;
            ESP_LOGI(TAG,"Page changed to %d (app index %d)", s_bm->current_page, idx);

            // 翻页
            if (s_bm->obj.app_tileview_tiles[target_page]) {
                lv_tileview_set_tile(s_bm->obj.app_tileview,s_bm->obj.app_tileview_tiles[target_page],LV_ANIM_OFF);
                page_indicator_update((butmenu_manager_t*)s_bm, target_page);
            }
        }
    }
}

esp_err_t butmenu_start(butmenu_manager_t *bm)
{
    if (!bm || !s_butmenu_init || bm != s_bm) {
        return ESP_FAIL;
    }

    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;
    const bm_style_t *style = bm_tmp->obj_style;

    bm_tmp->obj.nav_group = lv_group_create();
    lv_indev_set_group(bm_tmp->obj.indev, bm_tmp->obj.nav_group);

    int screen_w = lv_display_get_horizontal_resolution(bm_tmp->obj.disp);
    int screen_h = lv_display_get_vertical_resolution(bm_tmp->obj.disp);

    // 配置
    int icons_per_row = style->bm_style_app_launcher.icons_per_row;
    int apps_per_page = style->bm_style_app_launcher.apps_per_page;
    int icon_size     = style->bm_style_app_launcher.icon_size;
    int gap_row       = style->bm_style_app_launcher.icon_gap_row; 
    int gap_col       = style->bm_style_app_launcher.icon_gap_col;  
    int label_margin  = 5;
    int focus_space   = 6;  // 聚焦左右留白（不挤压布局）

    // cell 宽度 = 按钮宽度 + 左右聚焦空间
    int cell_w = icon_size + focus_space * 2;
    int cell_h = icon_size + style->bm_style_app_launcher.icon_label_height + label_margin;

    // 行数
    int rows = (apps_per_page + icons_per_row - 1) / icons_per_row;

    // 内容宽高
    int content_w = icons_per_row * cell_w + (icons_per_row - 1) * gap_row;
    int content_h = rows * cell_h + (rows - 1) * gap_col;

    // 越界检查
    if (content_w <= 0 || content_h <= 0 || content_w > screen_w || content_h > screen_h) {
        ESP_LOGE(TAG, "布局越界 screen(%d,%d) content(%d,%d)", screen_w, screen_h, content_w, content_h);
        return ESP_FAIL;
    }

    s_bm->total_pages = (bm_tmp->app_count + apps_per_page - 1) / apps_per_page;

    // tileview
    s_bm->obj.app_tileview = lv_tileview_create(s_bm->obj.container);
    lv_obj_set_size(s_bm->obj.app_tileview, screen_w, screen_h);
    lv_obj_center(s_bm->obj.app_tileview);
    lv_obj_remove_flag(s_bm->obj.app_tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_bm->obj.app_tileview, LV_OPA_0, 0);
    lv_obj_set_style_pad_all(s_bm->obj.app_tileview, 0, 0);
    lv_obj_align(s_bm->obj.app_tileview,LV_ALIGN_CENTER,0,s_bm->obj_style->bm_style_app_launcher.vertical_offset);
    
    // ======================
    // 创建页面
    // ======================
    for (int page = 0; page < s_bm->total_pages; page++)
    {
        int start = page * apps_per_page;
        int end   = start + apps_per_page;
        if (end > bm_tmp->app_count) end = bm_tmp->app_count;

        lv_obj_t *tile = lv_tileview_add_tile(s_bm->obj.app_tileview, page, 0, LV_DIR_HOR);
        lv_obj_set_size(tile, screen_w, screen_h);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(tile, LV_OPA_0, 0);

        s_bm->obj.app_tileview_tiles[page] = tile;

        // ======================
        // 内容容器：强制绝对居中，彻底不偏
        // ======================
        lv_obj_t *content = lv_obj_create(tile);
        lv_obj_set_size(content, content_w, content_h);
        lv_obj_set_align(content, LV_ALIGN_CENTER);  // 绝对居中
        lv_obj_set_style_pad_all(content, 0, 0);
        lv_obj_set_style_border_width(content, 0, 0);
        lv_obj_set_style_bg_opa(content, LV_OPA_0, 0);
        lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

        // 布局
        lv_obj_set_layout(content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(content,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_START
        );
        lv_obj_set_style_pad_column(content, gap_row, 0);
        lv_obj_set_style_pad_row(content, gap_col, 0);

        // ======================
        // 图标
        // ======================
        for (int i = start; i < end; i++)
        {
            lv_obj_t *cell = lv_obj_create(content);
            lv_obj_set_size(cell, cell_w, cell_h);
            lv_obj_set_style_pad_all(cell, 0, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(cell, LV_OPA_0, 0);

            // 垂直布局
            lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(cell,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER
            );
            lv_obj_set_style_pad_row(cell, label_margin, 0);

            // 按钮
            lv_obj_t *btn = lv_btn_create(cell);
            lv_obj_set_size(btn, icon_size, icon_size);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

            // 按钮内添加图片
            lv_obj_t *img = lv_image_create(btn);
            if(s_bm->apps[i]->icon == NULL){
                lv_image_set_src(img, &butmenu_image_middle_app_launcher_default_112_112);
            }
            else {
                lv_image_set_src(img, s_bm->apps[i]->icon);
            }
   

            // 图片在按钮内居中
            lv_obj_center(img);
            // 按钮样式：透明、无边框、无阴影、不可滚动
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);

            // 图片缩放：原图112x112，缩放到ICON_SIZE
            uint32_t zoom = (uint32_t)((s_bm->obj_style->bm_style_app_launcher.icon_size * 256) / 112);
            lv_image_set_scale(img, zoom);

            // 文字
            lv_obj_t *label = lv_label_create(cell);
            lv_label_set_text_fmt(label, "%s", s_bm->apps[i]->name);
            lv_obj_set_width(label, icon_size);
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(label, style->bm_style_app_launcher.icon_label_font, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(style->bm_style_app_launcher.icon_label_font_color), 0);

            // 事件
            s_bm->app_current_index = i;
            lv_obj_add_event_cb(btn, app_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, app_btn_event_cb, LV_EVENT_FOCUSED, (void *)(intptr_t)i);

            // 焦点组
            lv_group_add_obj(bm_tmp->obj.nav_group, btn);

            // 添加聚焦框样式
            if(s_bm->obj_style->style_focus_key!=NULL)
            {
                lv_obj_add_style(btn, s_bm->obj_style->style_focus_key, LV_STATE_FOCUS_KEY);
            }
            
        }
    }

    page_indicator_create(bm);
    page_indicator_update(bm, 0);

    for (size_t i = 0; i < s_bm->app_count; i++)
    {
        if(s_bm->apps[i]->init != NULL)
        {
            s_bm->apps[i]->init();
        }
    }
    
    ESP_LOGI(TAG, "APP菜单启动成功：%d 页 | 自动布局：%d x %d", s_bm->total_pages, content_w, content_h);
    return ESP_OK;
}

esp_err_t butmenu_stop(butmenu_manager_t *bm)
{
    if (!s_butmenu_init || !bm || bm != s_bm) {
        return ESP_FAIL;
    }

    free(s_bm);
    s_bm = NULL;

    s_butmenu_init = false;
    return ESP_OK;
}

esp_err_t bm_app_close_notify_handle(void)
{
    //先运行用户回调
    if(s_bm->apps[s_bm->app_current_index]->close){
        s_bm->apps[s_bm->app_current_index]->close();
    }

    //删除APP创建的屏幕
    if (s_bm->apps[s_bm->app_current_index]->app_scr != NULL) {
        lv_obj_delete(s_bm->apps[s_bm->app_current_index]->app_scr);
        s_bm->apps[s_bm->app_current_index]->app_scr = NULL;
    }

    //恢复系统状态栏和指示器
    lv_obj_remove_flag(s_bm->obj.status_bar,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_bm->obj.page_indicator,LV_OBJ_FLAG_HIDDEN);

    //加载主屏幕
    lv_screen_load(s_bm->obj.bm_sys_scr);

    //设置按键组
    lv_indev_set_group(s_bm->obj.indev, s_bm->obj.nav_group);
    return ESP_OK;
}


void bm_set_battery_level_icon(butmenu_manager_t *bm, int battery_level)
{
    status_bar_set_battery(bm,battery_level);
}
void bm_set_wifi_level_icon(butmenu_manager_t *bm, int wifi_level)
{
    status_bar_set_wifi(bm,wifi_level);
}
void bm_set_time_label(butmenu_manager_t *bm, const int hour,const int min,const int sec)
{
    status_bar_set_time(bm,hour,min,sec);
}