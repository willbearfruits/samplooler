#include "fx.h"
#include "recorder.h"
#if !REC_FIXED_POINT
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "fx";

// ---- delay ----
#define DLY_MS      380
#define DLY_N       (REC_SAMPLE_RATE * DLY_MS / 1000)
#define DLY_LP      0.18f       // one-pole in the feedback path: the tail darkens like tape (about 1 kHz)
#define FB_MIN      0.30f       // feedback at flat: the tail dies within a second
#define FB_MAX      0.92f       // feedback at full tilt: nearly self-oscillating
static float *dly;
static int    dly_w;
static float  dly_lp;
static volatile float dly_send;
static float  send_s;           // slewed
static volatile int dly_active;

// ---- stutter ----
#define STT_MAX_MS  260
#define STT_MIN_MS  12
#define STT_N       (REC_SAMPLE_RATE * STT_MAX_MS / 1000)
#define STT_XF      96          // 3 ms crossfade at the loop point
static float *stt;
static int    stt_fill;         // samples captured so far
static int    stt_len;          // loop length in use
static float  stt_pos;
static volatile float stt_amount;
static bool   stt_on;
static float  stt_mix;          // slewed dry/wet so entry and exit do not click

void fx_init(void)
{
    dly = heap_caps_calloc(DLY_N, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    stt = heap_caps_calloc(STT_N, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!dly) dly = heap_caps_calloc(DLY_N, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!stt) stt = heap_caps_calloc(STT_N, sizeof(float), MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "delay %d ms, stutter %d..%d ms%s", DLY_MS, STT_MIN_MS, STT_MAX_MS, dly && stt ? "" : " (NO MEMORY, effects off)");
}

void fx_set_delay(float a)   { dly_send = a < 0 ? 0 : a > 1 ? 1 : a; }
void fx_set_stutter(float a) { stt_amount = a < 0 ? 0 : a > 1 ? 1 : a; }
int  fx_delay_active(void)   { return dly_active; }
int  fx_stutter_active(void) { return stt_on; }

void IRAM_ATTR fx_process(float *buf, int n)
{
    if (!dly || !stt) return;

    // ---- stutter first, so the delay hears the stutter ----
    const float amt = stt_amount;
    if (amt > 0.0f && !stt_on) { stt_on = true; stt_fill = 0; stt_pos = 0; stt_len = STT_N; }
    if (amt <= 0.0f && stt_on && stt_mix < 0.01f) stt_on = false;
    if (stt_on) {
        // loop length shrinks with the angle, geometric so the sweep feels even
        float k = amt; if (k > 1.0f) k = 1.0f;
        int target = (int)((float)STT_N * (1.0f - k) + (float)(REC_SAMPLE_RATE * STT_MIN_MS / 1000) * k);
        float tgt_mix = amt > 0.0f ? 1.0f : 0.0f;
        for (int i = 0; i < n; i++) {
            float x = buf[i];
            if (stt_fill < STT_N) stt[stt_fill++] = x;           // keep grabbing until the slice is full
            int avail = stt_fill;
            int len = target < avail ? target : avail;
            if (len < STT_XF * 2) len = avail < STT_XF * 2 ? avail : STT_XF * 2;
            stt_len = len;
            float y = x;
            if (len > 0) {
                int p = (int)stt_pos;
                if (p >= len) { p = 0; stt_pos = 0; }
                y = stt[p];
                if (p < STT_XF && len > STT_XF * 2) {           // blend the loop seam with the slice tail
                    float w = (float)p / (float)STT_XF;
                    y = y * w + stt[len - STT_XF + p] * (1.0f - w);
                }
                stt_pos += 1.0f;
            }
            stt_mix += 0.002f * (tgt_mix - stt_mix);
            buf[i] = x + (y - x) * stt_mix;
        }
    }

    // ---- delay ----
    const float send_t = dly_send;
    bool any = false;
    for (int i = 0; i < n; i++) {
        send_s += 0.001f * (send_t - send_s);
        float fb = FB_MIN + (FB_MAX - FB_MIN) * send_s;
        float d = dly[dly_w];
        dly_lp += DLY_LP * (d - dly_lp);
        float w = buf[i] * send_s + dly_lp * fb;
        if (w > 1.5f) w = 1.5f; else if (w < -1.5f) w = -1.5f;   // keep a runaway tail sane
        dly[dly_w] = w;
        if (++dly_w >= DLY_N) dly_w = 0;
        buf[i] += d;
        if (d > 0.002f || d < -0.002f) any = true;
    }
    dly_active = any;
}
#endif
