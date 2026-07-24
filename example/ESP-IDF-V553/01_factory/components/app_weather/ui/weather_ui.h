#pragma once

#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

void weather_ui_service_init(void);
void weather_ui_screen_init(lv_obj_t *parent);
void weather_ui_screen_deinit(void);
void weather_ui_pause(void);
void weather_ui_resume(void);

#ifdef __cplusplus
}
#endif
