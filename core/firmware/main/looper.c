#include "looper.h"
#include "core.h"
#include "spsc.h"
#include <string.h>

#define RING     16384u                      // 0.51 s of pre-roll, a power of two
#define RMASK    (RING - 1u)
#define XF       192                         // 6 ms: seams, ramps
#define MAXLEN   (LOOP_MAX_S * AUDIO_SR)
#define MINLEN   (AUDIO_SR / 10)             // shorter than 0.1 s is a slip, not a loop

enum { C_PRESS, C_CONFIRM, C_RELEASE, C_TAP, C_CLEAR };
SPSC(cmdq, uint8_t, 16)
static cmdq_t q;

static int16_t *buf, *ring;
static uint32_t lf;                          // frames seen by the looper
static int      len, pos, rec_pos, tail_i = XF;
static loop_state_t state;
static float    play_g, play_t, dub_g, dub_t, lvl_s = 0.8f;
static bool     pending, pend_dub, clearing;
static uint32_t press_frame;
// for the screen
static volatile int   v_state;
static volatile float v_pos, v_len, v_fill;
static volatile int   v_layers;

static inline int16_t s16(float x)
{
    x *= 32767.0f;
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32767.0f) x = -32767.0f;
    return (int16_t)x;
}
static inline float f32(int16_t v) { return (float)v * (1.0f / 32767.0f); }

bool looper_init(void)
{
    if (buf) return true;
    buf = app_alloc(((size_t)MAXLEN + XF) * sizeof(int16_t));
    ring = app_alloc(RING * sizeof(int16_t));
    if (!buf || !ring) { app_free(buf); app_free(ring); buf = ring = NULL; app_log("looper: no memory"); return false; }
    cmdq_clear(&q);
    return true;
}
bool looper_ready(void) { return buf != NULL; }

static void push(uint8_t c) { cmdq_push(&q, &c); }
void looper_press(void) { push(C_PRESS); }
void looper_confirm(void) { push(C_CONFIRM); }
void looper_release(bool confirmed) { push(confirmed ? C_RELEASE : C_TAP); }
void looper_clear(void) { push(C_CLEAR); }

loop_state_t looper_state(void) { return (loop_state_t)v_state; }
float looper_pos(void) { return v_pos; }
float looper_len_s(void) { return v_len; }
float looper_fill(void) { return v_fill; }
int   looper_layers(void) { return v_layers; }

static void close_recording(void)
{
    if (rec_pos < MINLEN) { state = LOOP_EMPTY; len = 0; return; }
    len = rec_pos; pos = 0; tail_i = 0;          // the next XF samples become the tail
    play_g = 0; play_t = 1;                      // the first pass fades in from nothing
    state = LOOP_PLAYING; v_layers = 1;
}

static void command(uint8_t c)
{
    switch (c) {
    case C_PRESS:
        pending = true; press_frame = lf;
        pend_dub = state == LOOP_PLAYING || state == LOOP_DUBBING;
        break;
    case C_CONFIRM: {
        if (!pending) break;
        pending = false;
        uint32_t k = lf - press_frame;
        if (k > RING - 1) k = RING - 1;
        if (!pend_dub) {                         // a new loop: what was played since the press is its start
            for (uint32_t j = 0; j < k; j++) buf[j] = ring[(press_frame + j) & RMASK];
            rec_pos = (int)k; len = 0; state = LOOP_RECORDING; play_g = play_t = 0; tail_i = XF;
        } else if (len > 0) {                    // adding: mix the part since the press in where it was played
            for (uint32_t j = 0; j < k; j++) {
                int at = pos - (int)(k - j); while (at < 0) at += len;
                float r = j < XF ? (float)j / (float)XF : 1.0f;
                buf[at] = s16(f32(buf[at]) + r * f32(ring[(press_frame + j) & RMASK]));
            }
            state = LOOP_DUBBING; dub_g = dub_t = 1;
        }
        break;
    }
    case C_RELEASE:
        if (state == LOOP_RECORDING) close_recording();
        else if (state == LOOP_DUBBING) { dub_t = 0; v_layers++; }
        break;
    case C_TAP:
        pending = false;
        if (state == LOOP_PLAYING || state == LOOP_DUBBING) { play_t = 0; dub_t = 0; }
        else if (state == LOOP_STOPPED) { state = LOOP_PLAYING; pos = 0; play_g = 0; play_t = 1; }
        break;
    case C_CLEAR:
        pending = false;
        if (state == LOOP_RECORDING || state == LOOP_EMPTY) { state = LOOP_EMPTY; len = 0; }
        else { play_t = 0; dub_t = 0; clearing = true; }
        break;
    }
}

void looper_process(float *io, int n, float level, float keep)
{
    if (!buf) return;
    uint8_t c;
    while (cmdq_pop(&q, &c)) command(c);
    float l0 = lvl_s, l1 = level, dl = (l1 - l0) / (float)n;
    lvl_s = l1;
    const float ramp = 1.0f / (float)XF;
    for (int i = 0; i < n; i++) {
        float x = io[i];
        ring[lf & RMASK] = s16(x);
        lf++;
        if (state == LOOP_RECORDING) {
            buf[rec_pos++] = s16(x);
            if (rec_pos >= MAXLEN) close_recording();
            continue;
        }
        if (state == LOOP_EMPTY || len <= 0) continue;
        if (tail_i < XF) {                       // the tail after the release, then blended into the start
            buf[len + tail_i++] = s16(x);
            if (tail_i == XF)
                for (int j = 0; j < XF; j++) { float r = (float)j * ramp; buf[j] = s16(f32(buf[j]) * r + f32(buf[len + j]) * (1.0f - r)); }
        }
        float y = f32(buf[pos]);
        if (state != LOOP_STOPPED) {
            if (dub_g > 0.0f || keep < 0.999f) buf[pos] = s16(y * keep + x * dub_g);
            io[i] = x + y * (l0 + dl * (float)i) * play_g;
            if (++pos >= len) pos = 0;
        }
        if (play_g < play_t) { play_g += ramp; if (play_g > play_t) play_g = play_t; }
        else if (play_g > play_t) { play_g -= ramp; if (play_g < play_t) play_g = play_t; }
        if (dub_g < dub_t) { dub_g += ramp; if (dub_g > dub_t) dub_g = dub_t; }
        else if (dub_g > dub_t) { dub_g -= ramp; if (dub_g < dub_t) dub_g = dub_t; }
        if (state == LOOP_DUBBING && dub_t == 0.0f && dub_g == 0.0f) state = LOOP_PLAYING;
        if (play_g == 0.0f && play_t == 0.0f && state != LOOP_STOPPED) {
            if (clearing) { state = LOOP_EMPTY; len = 0; clearing = false; v_layers = 0; break; }
            state = LOOP_STOPPED;
        }
    }
    v_state = state;
    v_pos = len > 0 ? (float)pos / (float)len : 0.0f;
    v_len = (float)(state == LOOP_RECORDING ? rec_pos : len) / (float)AUDIO_SR;
    v_fill = state == LOOP_RECORDING ? (float)rec_pos / (float)MAXLEN : 0.0f;
}
