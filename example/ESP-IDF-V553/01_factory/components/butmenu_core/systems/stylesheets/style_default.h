#pragma once

#include "systems/assets/butmenu_assets.h"
#include "systems/stylesheets/butmenu_stylesheets.h"

#ifdef __cplusplus
extern "C" {
#endif

bm_style_t bm_style_default = {
    .bm_style_container = 
    {
        .con_bg_color = 0,
    },
    .bm_style_app_launcher = 
    {
        .bg_image_src = &butmenu_image_middle_wallpaper_dark_320_320,
        .app_icon_image_src = &butmenu_image_middle_app_launcher_default_112_112,
        .apps_per_page = 4,
        .icons_per_row = 2,
        .icon_size = 70,
        .icon_gap_row = 50,
        .icon_gap_col = 20,
        .icon_label_height = 30,
        .vertical_offset = 0,
        .icon_label_font = &lv_font_montserrat_16,
        .icon_label_font_color = 0xffffff,
        .dot_size      = 10, 
        .dot_space     = 10,  
        .dot_bottom_offset = -12, 
    },
    .bm_style_status_bar = 
    {
        .status_bar_height = 30,
    },
    .style_focus_key = NULL,
};

#ifdef __cplusplus
}
#endif,