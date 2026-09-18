#pragma once

#ifdef NODE_HOST_TEST
#include <stdio.h>
#define NODE_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define NODE_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define NODE_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define NODE_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#else
#include "esp_log.h"
#define NODE_LOGI ESP_LOGI
#define NODE_LOGW ESP_LOGW
#define NODE_LOGE ESP_LOGE
#define NODE_LOGD ESP_LOGD
#endif // ifdef NODE_HOST_TEST
