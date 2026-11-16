// Host stand-in: the time is the test's (tofsim.c). It moves with every 10 ms tick and with the bus time of
// every transfer.
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
