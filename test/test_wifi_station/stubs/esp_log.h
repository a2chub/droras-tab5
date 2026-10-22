#pragma once
inline void testEspLog(const char*, const char*, ...) {}
#define ESP_LOGI(tag, ...) testEspLog(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) testEspLog(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) testEspLog(tag, __VA_ARGS__)
