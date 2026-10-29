// Where the audio block's time goes. The audio task reads the cycle counter at each stage's end (prof_lap) and
// adds the cycles since the last lap to that stage; once a second (prof_block) the sums and the longest blocks are
// published through a sequence count, and prof_get turns them into microseconds for the status line.
// A lap is one counter read, two adds and a compare: nothing allocates, locks or logs. On the host it is nothing.
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { PROF_IN,      // the input: the mic's slot, the high-pass, the gain (or the USB stream)
       PROF_VIN,     // the voice's input stage: gate, compressor, de-esser, the speaker guard
       PROF_VOICE,   // the voice engine: the pitch tracker, the tuned lead, harmonies, the choir
       PROF_VOC,     // the vocoder
       PROF_BUS,     // the voice's echo, reverb and shimmer
       PROF_VMIX,    // the voice's mix, its limiter and the fade to OFF
       PROF_REC,     // the recorder (TAPE or GRAIN)
       PROF_FX,      // Monitor and the roll effects (delay, stutter)
       PROF_OUT,     // the NaN guard, the soft clip, the DAC words and the USB stream
       PROF_N };

typedef struct {
    uint32_t avg_us[PROF_N], max_us[PROF_N];   // per block, over the last second
    uint32_t blocks, budget_us;                // blocks in that second, and the time one block may take
} prof_t;

#ifdef ESP_PLATFORM
#include "esp_cpu.h"
extern uint32_t prof_sum[PROF_N], prof_top[PROF_N], prof_last;
static inline void prof_start(void) { prof_last = esp_cpu_get_cycle_count(); }
static inline void prof_lap(int s)
{
    uint32_t now = esp_cpu_get_cycle_count(), d = now - prof_last;
    prof_last = now;
    prof_sum[s] += d;
    if (d > prof_top[s]) prof_top[s] = d;
}
void prof_block(int frames);           // the audio task, after each block
bool prof_get(prof_t *p);              // false until the first second is in
const char *prof_name(int s);
#else
static inline void prof_start(void) {}
static inline void prof_lap(int s) { (void)s; }
#endif
