#include "fx.h"
#include "recorder.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "diag.h"
#include <math.h>

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

// ---- filter ----
// A state-variable filter in its trapezoidal form (Zavalishin's): stable while its cutoff moves, which is all it
// does here. The UI task turns the position into g = tan(pi fc / fs) (libm, its side), the audio task glides g and
// the wet share per sample. Low-pass or high-pass is only swapped while the wet share is at zero.
#define FLT_OPEN    0.02f       // |position| below this: open
static volatile float flt_g_t = 2.4f, flt_k_t = 1.0f, flt_wet_t;
static volatile int   flt_hp_t;
static volatile float flt_pos_v;
static float flt_g = 2.4f, flt_wet, flt_ic1, flt_ic2;
static int   flt_hp;

void fx_set_filter(float pos, float res)
{
    uint32_t b; memcpy(&b, &pos, 4);
    if ((b & 0x7f800000u) == 0x7f800000u) return;             // NaN or inf: as it was
    if (pos > 1.0f) pos = 1.0f; else if (pos < -1.0f) pos = -1.0f;
    if (!(res >= 0.0f)) res = 0.0f; else if (res > 0.9f) res = 0.9f;
    flt_pos_v = pos;
    float a = fabsf(pos);
    if (a < FLT_OPEN) { flt_wet_t = 0.0f; return; }
    float t = (a - FLT_OPEN) / (1.0f - FLT_OPEN);
    // the wet share is full within the first tenth of the travel, the cutoff sweeps over all of it
    float fc = pos < 0 ? 12000.0f * powf(150.0f / 12000.0f, t) : 30.0f * powf(5000.0f / 30.0f, t);
    flt_g_t = tanf((float)M_PI * fc / (float)REC_SAMPLE_RATE);
    flt_k_t = 2.0f - 2.0f * res;
    flt_hp_t = pos > 0;
    flt_wet_t = t < 0.1f ? t * 10.0f : 1.0f;
}
float fx_filter_pos(void) { return flt_pos_v; }

void fx_init(void)
{
    // Both are streams: the delay reads and writes one place that moves a sample at a time, the stutter writes one and
    // reads two. In PSRAM the cache fetches eight samples at once, so a stream costs a miss every eighth sample. The
    // internal RAM is kept for the voice's reverb lines (vox_init, after this), which are read all over every sample:
    // with these two in it, the lines had to go to PSRAM and the voice's bus took 1.4 ms of every 2 ms block.
    dly = heap_caps_calloc(DLY_N, sizeof(float), MALLOC_CAP_SPIRAM);
    stt = heap_caps_calloc(STT_N, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!dly) dly = heap_caps_calloc(DLY_N, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!stt) stt = heap_caps_calloc(STT_N, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    diag_ram("fx.dly", dly, DLY_N * sizeof(float), false);
    diag_ram("fx.stt", stt, STT_N * sizeof(float), false);
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

    // ---- filter ----
    float wet_t = flt_wet_t;
    const int hp_t = flt_hp_t;
    if (hp_t != flt_hp) { if (flt_wet < 0.002f) flt_hp = hp_t; else wet_t = 0.0f; }   // the other kind: through open first
    if (wet_t <= 0.0f && flt_wet < 0.0005f) { flt_wet = 0.0f; flt_ic1 = flt_ic2 = 0.0f; return; }   // open: nothing runs
    const float g_t = flt_g_t, kq = flt_k_t;
    const bool hp = flt_hp != 0;
    float g = flt_g, wet = flt_wet, ic1 = flt_ic1, ic2 = flt_ic2;
    for (int i = 0; i < n; i++) {
        g += 0.002f * (g_t - g);                                  // 16 ms: the hand's 25 ms readings never step
        wet += 0.004f * (wet_t - wet);
        float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
        float x = buf[i], v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        float y = hp ? x - kq * v1 - v2 : v2;
        buf[i] = x + (y - x) * wet;
    }
    {   // never left ringing on a bad number (the exponent's bits: -ffast-math hides a NaN from a compare)
        uint32_t b1, b2; memcpy(&b1, &ic1, 4); memcpy(&b2, &ic2, 4);
        if ((b1 & 0x7f800000u) >= 0x42000000u || (b2 & 0x7f800000u) >= 0x42000000u) ic1 = ic2 = 0.0f;   // 32 or more, inf, NaN
    }
    flt_g = g; flt_wet = wet; flt_ic1 = ic1; flt_ic2 = ic2;
}
