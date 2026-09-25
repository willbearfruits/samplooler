// Fixed-point recorder for the ESP32-C3: no FPU, no PSRAM, one core.
// Samples are Q15 (int16). The read head is an integer plus a Q16 fraction,
// the rate is Q16, the seam weights are Q15. The only 64-bit work is the
// three Hermite products, which RV32IM does with mul + mulh.
#include "recorder.h"
#if REC_FIXED_POINT
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "rec";

#define XF          (REC_SAMPLE_RATE / 100)      // 10 ms seam crossfade
#define MIN_LOOP    (REC_SAMPLE_RATE / 10)       // 100 ms
#define GUARD       4
#define WANT_SEC    20                            // ceiling; RAM decides the real figure
#define RAM_MARGIN  (48 * 1024)                   // left for stacks, I2C, OLED, logging
#define Q16         65536
#define SLEW_SHIFT  10                            // tau = 1024 samples = 64 ms at 16 kHz

static int16_t *buf;
static int      max_samples;
static int16_t  xf_w[XF];                         // Q15, 0 at loop start rising to 32767 at XF

static volatile bool    rec_req;
static volatile int32_t rate_target = Q16;        // Q16
static volatile int     state = REC_IDLE;
static volatile int     loop_len;
static volatile int     pos_i;
static volatile int32_t rate;                     // Q16, slewed
static volatile int     rec_len;

static bool     rec_prev;
static uint32_t pos_f;                            // Q16 fraction, 0 <= pos_f < Q16

bool recorder_init(void)
{
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int sec = (int)((largest > RAM_MARGIN ? largest - RAM_MARGIN : 0) / (sizeof(int16_t) * REC_SAMPLE_RATE));
    if (sec > WANT_SEC) sec = WANT_SEC;
    if (sec < 1) { ESP_LOGE(TAG, "only %u bytes free, no room for a take", (unsigned)largest); return false; }
    max_samples = sec * REC_SAMPLE_RATE;
    buf = heap_caps_malloc((max_samples + GUARD) * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) { ESP_LOGE(TAG, "allocation of %d s failed", sec); return false; }
    memset(buf, 0, (max_samples + GUARD) * sizeof(int16_t));
    // Raised cosine without libm: recurrence for cos(k*step) in float, once at boot.
    // (Software float is fine here: XF evaluations, not per sample.)
    {
        const float step = 3.14159265f / (float)XF;
        float c = 1.0f, s = 0.0f, cs = 1.0f - step * step * 0.5f, sn = step;   // cos/sin of step, small-angle
        for (int i = 0; i < XF; i++) {
            float w = 0.5f - 0.5f * c;
            xf_w[i] = (int16_t)(w * 32767.0f);
            float c2 = c * cs - s * sn; s = s * cs + c * sn; c = c2;
        }
    }
    ESP_LOGI(TAG, "take buffer %d s at %d Hz in internal RAM (%u bytes were free), %d ms seam",
             sec, REC_SAMPLE_RATE, (unsigned)largest, XF * 1000 / REC_SAMPLE_RATE);
    return true;
}

int   recorder_max_seconds(void)  { return max_samples / REC_SAMPLE_RATE; }
void  recorder_set_record(bool h) { rec_req = h; }
void  recorder_set_speed(float t)
{
    if (t > REC_SPEED_LIMIT) t = REC_SPEED_LIMIT;
    if (t < -REC_SPEED_LIMIT) t = -REC_SPEED_LIMIT;
    rate_target = (int32_t)(t * (float)Q16);      // one software float multiply at 50 Hz
}
static volatile int32_t vol_q15 = 32767;
void  recorder_set_pitch(float r)   { (void)r; }                 // TAPE only on the C3
void  recorder_set_mode(int m)      { (void)m; }
void  recorder_set_grain_ms(int ms) { (void)ms; }
void  recorder_set_volume(float v)  { v = v < 0 ? 0 : v > 2 ? 2 : v; vol_q15 = (int32_t)(v * 16384.0f); }   // Q14 so 2.0 fits
int   recorder_mode(void)           { return REC_MODE_TAPE; }
int   recorder_state(void)        { return state; }
float recorder_loop_seconds(void) { return (float)loop_len / (float)REC_SAMPLE_RATE; }
float recorder_position(void)     { int l = loop_len; return l ? (float)pos_i / (float)l : 0.0f; }
float recorder_speed_now(void)    { return (float)rate / (float)Q16; }
float recorder_record_fill(void)  { return max_samples ? (float)rec_len / (float)max_samples : 0.0f; }
const int16_t *recorder_take(int *n) { if (state == REC_RECORDING || rec_len <= 0) { *n = 0; return NULL; } *n = rec_len; return buf; }
int16_t *recorder_load_begin(int *max) { if (state == REC_RECORDING) return NULL; state = REC_IDLE; loop_len = 0; *max = max_samples; return buf; }
void recorder_load_end(int n)
{
    if (n <= 0) return;
    rec_len = n;
    int le = n - XF;
    if (le < MIN_LOOP) return;
    for (int g = 0; g < GUARD; g++) buf[n + g] = buf[n - 1];
    loop_len = le; pos_i = 0; pos_f = 0; rate = rate_target;
    state = REC_PLAYING;
}
int recorder_input_is_usb(void) { return 0; }

// 4-point Hermite, integer. y[] are Q15, f is Q16. Coefficients carry a factor 2.
static inline int32_t hermite_q(const int32_t *y, uint32_t f)
{
    int32_t c1 = y[2] - y[0];
    int32_t c2 = 2 * y[0] - 5 * y[1] + 4 * y[2] - y[3];
    int32_t c3 = y[3] - y[0] + 3 * (y[1] - y[2]);
    int32_t r = (int32_t)(((int64_t)c3 * f) >> 16) + c2;
    r = (int32_t)(((int64_t)r * f) >> 16) + c1;
    r = (int32_t)(((int64_t)r * f) >> 16);
    return y[1] + (r >> 1);
}

static inline int32_t IRAM_ATTR read_at(int i, uint32_t f, int len)
{
    int32_t y[4];
    if (i >= 1 && i + 2 < len) {
        const int16_t *p = buf + i - 1;
        y[0] = p[0]; y[1] = p[1]; y[2] = p[2]; y[3] = p[3];
    } else {
        for (int j = 0; j < 4; j++) {
            int q = i - 1 + j;
            if (q < 0) q = 0; else if (q >= len) q = len - 1;
            y[j] = buf[q];
        }
    }
    return hermite_q(y, f);
}

void IRAM_ATTR recorder_process(const int16_t *in, int16_t *out, int n)
{
    bool r = rec_req;
    if (r && !rec_prev) {
        rec_len = 0;
        state = REC_RECORDING;
    } else if (!r && rec_prev) {
        int le = rec_len - XF;
        if (le >= MIN_LOOP) {
            for (int g = 0; g < GUARD; g++) buf[rec_len + g] = buf[rec_len - 1];
            loop_len = le; pos_i = 0; pos_f = 0; rate = rate_target;
            state = REC_PLAYING;
        } else {
            loop_len = 0;
            state = REC_IDLE;
        }
    }
    rec_prev = r;

    if (state == REC_RECORDING) {
        int len = rec_len;
        for (int i = 0; i < n; i++) {
            out[i] = in[i];
            if (len < max_samples) buf[len++] = in[i];
        }
        rec_len = len;
        return;
    }

    if (state != REC_PLAYING) {
        memset(out, 0, n * sizeof(int16_t));
        return;
    }

    const int     le  = loop_len;
    const int     len = rec_len;
    const int32_t tgt = rate_target;
    int      ip = pos_i;
    uint32_t fp = pos_f;
    int32_t  rt = rate;

    for (int i = 0; i < n; i++) {
        int32_t d = tgt - rt;                                       // slew, converging exactly
        rt += d > 0 ? (d + ((1 << SLEW_SHIFT) - 1)) >> SLEW_SHIFT : -((-d + ((1 << SLEW_SHIFT) - 1)) >> SLEW_SHIFT);

        int32_t y = read_at(ip, fp, len);
        if (ip < XF) {
            int32_t w = xf_w[ip];
            y = (y * w + read_at(ip + le, fp, len) * (32767 - w)) >> 15;
        }
        y = (y * vol_q15) >> 14;
        if (y > 32767) y = 32767; else if (y < -32768) y = -32768;
        out[i] = (int16_t)y;

        int32_t acc = (int32_t)fp + rt;                             // |rt| <= 2*Q16, so at most two wraps
        while (acc >= (int32_t)Q16) { ip++; acc -= Q16; }
        while (acc < 0)             { ip--; acc += Q16; }
        fp = (uint32_t)acc;
        if (ip >= le) ip -= le; else if (ip < 0) ip += le;
    }

    pos_i = ip; pos_f = fp; rate = rt;
}

void recorder_envelope(uint8_t *cols, int ncols)
{
    int n = state == REC_RECORDING ? rec_len : state == REC_PLAYING ? loop_len : 0;
    if (n <= 0 || !buf) { memset(cols, 0, ncols); return; }
    int per = n / ncols; if (per < 1) per = 1;
    int clusters = per > 32 ? 8 : 1, cl = per / clusters, span = per > 32 ? 4 : per;
    for (int c = 0; c < ncols; c++) {
        int base = c * per, pk = 0;
        for (int k = 0; k < clusters; k++) {
            int s0 = base + k * cl;
            for (int j = 0; j < span && s0 + j < n; j++) { int v = buf[s0 + j]; if (v < 0) v = -v; if (v > pk) pk = v; }
        }
        cols[c] = (uint8_t)(pk >> 7);
    }
}
#endif
