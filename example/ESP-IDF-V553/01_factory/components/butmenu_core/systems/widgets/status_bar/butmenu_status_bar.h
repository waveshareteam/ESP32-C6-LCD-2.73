#pragma once

#include "include/butmenu.h"


#ifdef __cplusplus
extern "C" {
#endif

void status_bar_create(butmenu_manager_t *bm);
void status_bar_set_battery(butmenu_manager_t *bm, int battery_level);
void status_bar_set_wifi(butmenu_manager_t *bm, int wifi_level);
void status_bar_set_time(butmenu_manager_t *bm, const int hour,const int min,const int sec);

#ifdef __cplusplus
}
#endif
