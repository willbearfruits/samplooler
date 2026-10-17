#include "recorder.h"
#include <string.h>
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "rec";

#define REC_MAX_SECONDS 20
#define MAX_SAMPLES (REC_SAMPLE_RATE * REC_MAX_SECONDS)   // 640 000 = 1.25 MB of int16 in PSRAM
#define XF          320                                    // 10 ms seam crossfade
#define MIN_LOOP    (REC_SAMPLE_RATE / 10)
#define SLEW        0.000625f                              // one-pole per sample, tau = 50 ms
#define GUARD       4
#define WIN_N       512                                    // Hann table for the grains
#define NGRAIN      2

static int16_t *buf;
static float    xf_w[XF];
static float    win[WIN_N + 1];

// UI -> audio
static volatile bool  rec_req;
static volatile float speed_target = 1.0f;
static volatile float pitch_target = 1.0f;
static volatile int   mode_req = REC_MODE_TAPE;
static volatile int   grain_len = REC_SAMPLE_RATE / 20;   // 50 ms
static volatile float volume = 1.0f;
// audio -> UI
static volatile int   state = REC_IDLE;
static volatile int   mode_now = REC_MODE_TAPE;
static volatile int   loop_len;
static volatile int   pos_i;
static volatile float speed;
static volatile int   rec_len;

// audio task only
static bool  rec_prev;
static float pos_f;
static float pitch;                                        // slewed
typedef struct { int start; float ph; int len; } grain_t;
static grain_t gr[NGRAIN];

bool recorder_init(void)
{
    buf = heap_caps_malloc((MAX_SAMPLES + GUARD) * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!buf) { ESP_LOGE(TAG, "no PSRAM for the take buffer"); return false; }
    memset(buf, 0, (MAX_SAMPLES + GUARD) * sizeof(int16_t));
    for (int i = 0; i < XF; i++) xf_w[i] = 0.5f - 0.5f * cosf((float)M_PI * (float)i / (float)XF);
    for (int i = 0; i <= WIN_N; i++) win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)WIN_N);
    ESP_LOGI(TAG, "take buffer %d s at %d Hz in PSRAM, %d ms seam crossfade, tape + grain",
             REC_MAX_SECONDS, REC_SAMPLE_RATE, XF * 1000 / REC_SAMPLE_RATE);
    return true;
}

int   recorder_max_seconds(void)  { return REC_MAX_SECONDS; }
void  recorder_set_record(bool h) { rec_req = h; }
void  recorder_set_speed(float s)
{
    if (s > REC_SPEED_LIMIT) s = REC_SPEED_LIMIT;
    if (s < -REC_SPEED_LIMIT) s = -REC_SPEED_LIMIT;
    speed_target = s;
}
void  recorder_set_pitch(float r)
{
    if (r < REC_PITCH_MIN) r = REC_PITCH_MIN;
    if (r > REC_PITCH_MAX) r = REC_PITCH_MAX;
    pitch_target = r;
}
void  recorder_set_mode(int m)     { mode_req = m == REC_MODE_GRAIN ? REC_MODE_GRAIN : REC_MODE_TAPE; }
void  recorder_set_grain_ms(int ms)
{
    if (ms < 20) ms = 20;
    if (ms > 200) ms = 200;
    grain_len = ms * (REC_SAMPLE_RATE / 1000);
}
void  recorder_set_volume(float v) { volume = v < 0 ? 0 : v > 2 ? 2 : v; }
int   recorder_state(void)        { return state; }
int   recorder_mode(void)         { return mode_now; }
float recorder_loop_seconds(void) { return (float)loop_len / (float)REC_SAMPLE_RATE; }
float recorder_position(void)     { int l = loop_len; return l ? (float)pos_i / (float)l : 0.0f; }
float recorder_speed_now(void)    { return speed; }
float recorder_record_fill(void)  { return (float)rec_len / (float)MAX_SAMPLES; }
const int16_t *recorder_take(int *n) { if (state == REC_RECORDING || rec_len <= 0) { *n = 0; return NULL; } *n = rec_len; return buf; }
int16_t *recorder_load_begin(int *max) { if (state == REC_RECORDING) return NULL; state = REC_IDLE; loop_len = 0; *max = MAX_SAMPLES; return buf; }
void recorder_load_end(int n)
{
    if (n <= 0) return;
    rec_len = n;
    int le = n - XF;
    if (le < MIN_LOOP) return;
    for (int g = 0; g < GUARD; g++) buf[n + g] = buf[n - 1];
    loop_len = le; pos_i = 0; pos_f = 0.0f; speed = speed_target; pitch = pitch_target;
    for (int g = 0; g < NGRAIN; g++) { gr[g].start = 0; gr[g].len = grain_len; gr[g].ph = (float)(g * gr[g].len) / NGRAIN; }
    state = REC_PLAYING;
}
int recorder_input_is_usb(void) { return 0; }

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

static inline float hermite(const float *y, float f)
{
    float c1 = 0.5f * (y[2] - y[0]);
    float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
    float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
    return ((c3 * f + c2) * f + c1) * f + y[1];
}

static inline float IRAM_ATTR read_at(int i, float f, int len)
{
    const float k = 1.0f / 32768.0f;
    float y[4];
    if (i >= 1 && i + 2 < len) {
        const int16_t *p = buf + i - 1;
        y[0] = p[0] * k; y[1] = p[1] * k; y[2] = p[2] * k; y[3] = p[3] * k;
    } else {
        for (int j = 0; j < 4; j++) {
            int q = i - 1 + j;
            if (q < 0) q = 0; else if (q >= len) q = len - 1;
            y[j] = buf[q] * k;
        }
    }
    return hermite(y, f);
}

// A loop-relative read: ip in [0, le), with the seam blended into the tail.
static inline float IRAM_ATTR read_loop(int ip, float fp, int le, int len)
{
    float y = read_at(ip, fp, len);
    if (ip < XF) { float w = xf_w[ip]; y = y * w + read_at(ip + le, fp, len) * (1.0f - w); }
    return y;
}

void IRAM_ATTR recorder_process(const float *in, float *out, int n)
{
    bool r = rec_req;
    if (r && !rec_prev) {
        rec_len = 0;
        state = REC_RECORDING;
    } else if (!r && rec_prev) {
        int le = rec_len - XF;
        if (le >= MIN_LOOP) {
            for (int g = 0; g < GUARD; g++) buf[rec_len + g] = buf[rec_len - 1];
            loop_len = le; pos_i = 0; pos_f = 0.0f; speed = speed_target; pitch = pitch_target;
            for (int g = 0; g < NGRAIN; g++) { gr[g].start = 0; gr[g].len = grain_len; gr[g].ph = (float)(g * gr[g].len) / NGRAIN; }
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
            float x = in[i];
            out[i] = x;
            if (len < MAX_SAMPLES) {
                float s = x * 32767.0f;
                if (s > 32767.0f) s = 32767.0f; else if (s < -32768.0f) s = -32768.0f;
                buf[len++] = (int16_t)s;
            }
        }
        rec_len = len;
        return;
    }

    if (state != REC_PLAYING) {
        memset(out, 0, n * sizeof(float));
        return;
    }

    const int   le  = loop_len;
    const int   len = rec_len;
    const float st  = speed_target;
    const float pt  = pitch_target;
    const float vol = volume;
    const int   mode = mode_req;
    mode_now = mode;
    int   ip = pos_i;
    float fp = pos_f;
    float sp = speed;
    float pc = pitch;

    for (int i = 0; i < n; i++) {
        sp += SLEW * (st - sp);
        float y;
        if (mode == REC_MODE_TAPE) {
            y = read_loop(ip, fp, le, len);
        } else {
            pc += SLEW * (pt - pc);
            y = 0.0f;
            for (int g = 0; g < NGRAIN; g++) {
                grain_t *G = &gr[g];
                if (G->ph >= (float)G->len) { G->start = ip; G->ph -= (float)G->len; G->len = grain_len; }
                int   gi = (int)G->ph;
                float gf = G->ph - (float)gi;
                int   ri = G->start + gi; if (ri >= le) ri -= le;
                float w = win[(int)(G->ph * (float)WIN_N / (float)G->len)];
                y += read_loop(ri, gf, le, len) * w;
                G->ph += pc;
            }
        }
        out[i] = y * vol;

        fp += sp;
        if (fp >= 1.0f)      { int s = (int)fp; ip += s; fp -= (float)s; }
        else if (fp < 0.0f)  { int s = (int)(-fp) + 1; ip -= s; fp += (float)s; }
        if (ip >= le) ip -= le; else if (ip < 0) ip += le;
    }

    pos_i = ip; pos_f = fp; speed = sp; pitch = pc;
}
