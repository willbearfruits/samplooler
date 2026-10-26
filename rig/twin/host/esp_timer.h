// The twin's clock (twin.c): simulated time, so a fast run and a real-time run draw the same frames.
#pragma once
#include <stdint.h>
extern int64_t twin_now_us;
static inline int64_t esp_timer_get_time(void) { return twin_now_us; }
