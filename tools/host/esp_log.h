// Host stand-in: the firmware's log lines go to stderr.
#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { fprintf(stderr, "%s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGE ESP_LOGI
