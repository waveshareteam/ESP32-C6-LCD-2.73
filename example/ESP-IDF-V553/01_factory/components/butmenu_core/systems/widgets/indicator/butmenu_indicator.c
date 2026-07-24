#include "systems/widgets/indicator/butmenu_indicator.h"

#include "systems/private/butmenu_core_private.h"

// ==============================
// 底部居中圆点分页指示器（无背景，纯圆点）
// 可调参数：圆点大小、圆点间距、底部距离
// ==============================
void page_indicator_create(butmenu_manager_t *bm)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;

    const int dot_size      = bm_tmp->obj_style->bm_style_app_launcher.dot_size; 
    const int dot_space     = bm_tmp->obj_style->bm_style_app_launcher.dot_space; 
    const int bottom_offset = bm_tmp->obj_style->bm_style_app_launcher.dot_bottom_offset;

    lv_obj_t *top_layer = lv_layer_top();

    // 指示器容器：完全透明、无背景
    bm_tmp->obj.page_indicator = lv_obj_create(top_layer);
    lv_obj_remove_style_all(bm_tmp->obj.page_indicator);
    lv_obj_set_size(bm_tmp->obj.page_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(bm_tmp->obj.page_indicator, LV_ALIGN_BOTTOM_MID, 0, bottom_offset);

    // 完全透明，无背景
    lv_obj_set_style_bg_opa(bm_tmp->obj.page_indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm_tmp->obj.page_indicator, 0, 0);
    lv_obj_set_style_pad_column(bm_tmp->obj.page_indicator, dot_space, 0);

    // 水平布局
    lv_obj_set_layout(bm_tmp->obj.page_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bm_tmp->obj.page_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bm_tmp->obj.page_indicator,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    // 创建圆点
    for (int i = 0; i < bm_tmp->total_pages; i++) {
        lv_obj_t *dot = lv_obj_create(bm_tmp->obj.page_indicator);
        bm_tmp->obj.indicator_items[i] = dot;

        lv_obj_set_size(dot, dot_size, dot_size);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);

        // 默认灰色
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x666666), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(dot, 255, LV_STATE_DEFAULT);

        // 选中白色
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(dot, 255, LV_STATE_CHECKED);
    }
}

// ==============================
// 更新指示器当前页
// ==============================
void page_indicator_update(butmenu_manager_t *bm, int current_page_idx)
{
    s_butmenu_manager_t *bm_tmp = (s_butmenu_manager_t *)bm;

    for (int i = 0; i < bm_tmp->total_pages; i++) {
        if (bm_tmp->obj.indicator_items[i] == NULL) continue;

        if (i == current_page_idx) {
            lv_obj_add_state(bm_tmp->obj.indicator_items[i], LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(bm_tmp->obj.indicator_items[i], LV_STATE_CHECKED);
        }
    }
}