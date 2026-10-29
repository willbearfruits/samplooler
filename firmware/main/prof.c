// See prof.h.
#include "prof.h"
#include <string.h>
#include "esp_attr.h"
#include "sdkconfig.h"
#include "recorder.h"

uint32_t prof_sum[PROF_N], prof_top[PROF_N], prof_last;
static uint32_t frames_acc, blocks_acc;
static uint32_t pub_sum[PROF_N], pub_top[PROF_N], pub_frames, pub_blocks;
static volatile uint32_t pub_seq;                  // odd while the audio task writes

void IRAM_ATTR prof_block(int frames)
{
    frames_acc += (uint32_t)frames;
    blocks_acc++;
    if (frames_acc < REC_SAMPLE_RATE) return;
    __atomic_add_fetch(&pub_seq, 1, __ATOMIC_ACQ_REL);
    for (int s = 0; s < PROF_N; s++) { pub_sum[s] = prof_sum[s]; pub_top[s] = prof_top[s]; prof_sum[s] = 0; prof_top[s] = 0; }
    pub_frames = frames_acc; pub_blocks = blocks_acc;
    __atomic_add_fetch(&pub_seq, 1, __ATOMIC_ACQ_REL);
    frames_acc = 0; blocks_acc = 0;
}

bool prof_get(prof_t *p)
{
    uint32_t sum[PROF_N], top[PROF_N], fr, bl, q0;
    for (int tries = 0; ; tries++) {
        q0 = __atomic_load_n(&pub_seq, __ATOMIC_ACQUIRE);
        if (q0 & 1u) { if (tries > 1000) return false; continue; }
        memcpy(sum, pub_sum, sizeof sum); memcpy(top, pub_top, sizeof top); fr = pub_frames; bl = pub_blocks;
        if (__atomic_load_n(&pub_seq, __ATOMIC_ACQUIRE) == q0) break;
    }
    if (!bl) return false;
    const uint32_t mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    for (int s = 0; s < PROF_N; s++) {
        p->avg_us[s] = (uint32_t)((uint64_t)sum[s] / bl / mhz);
        p->max_us[s] = top[s] / mhz;
    }
    p->blocks = bl;
    p->budget_us = (uint32_t)((uint64_t)fr * 1000000u / bl / REC_SAMPLE_RATE);
    return true;
}

const char *prof_name(int s)
{
    static const char *const N[PROF_N] = { "in", "v.in", "v.voice", "v.voc", "v.bus", "v.mix", "rec", "fx", "out" };
    return s >= 0 && s < PROF_N ? N[s] : "?";
}
