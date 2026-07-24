#pragma once

#include "include/butmenu.h"


#ifdef __cplusplus
extern "C" {
#endif

void page_indicator_update(butmenu_manager_t *bm, int current_page_idx);
void page_indicator_create(butmenu_manager_t *bm);

#ifdef __cplusplus
}
#endif
