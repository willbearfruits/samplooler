// Host stand-in: the driver's log lines go to tofsim.c, which counts them (and prints them with -v).
#pragma once
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO } esp_log_level_t;
void sim_log(esp_log_level_t level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void esp_log_level_set(const char *tag, esp_log_level_t level);
#define ESP_LOGE(tag, ...) sim_log(ESP_LOG_ERROR, tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) sim_log(ESP_LOG_WARN, tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) sim_log(ESP_LOG_INFO, tag, __VA_ARGS__)
