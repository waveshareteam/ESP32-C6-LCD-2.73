#pragma once

#include "esp_log.h"
#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t init_photo_player(void);
void init_photo_ui_screen(lv_obj_t *parent, generic_file_list_t *file_list, const char *message);
void deinit_photo_player(void);

#ifdef __cplusplus
}
#endif
