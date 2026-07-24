#pragma once

#include "esp_log.h"
#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

void sensor_layout_init(qmi8658_dev_t *qmi8658_dev_tmp,i2c_master_dev_handle_t shtc3_dev_tmp);
void sensor_layout_deinit(void);

#ifdef __cplusplus
}
#endif
