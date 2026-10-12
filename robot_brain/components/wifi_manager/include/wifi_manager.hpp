#pragma once
#include "esp_err.h"

esp_err_t wifi_manager_init();
bool wifi_manager_is_sta_connected();
const char *wifi_manager_get_ap_ip();