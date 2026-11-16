#include "recorder.h"
#include "diag.h"
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
static volatile float speed_slew = SLEW;                         // recorder_set_slew_ms
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
static float vol_s = 1.0f;                                  // the volume as it glides (audio task only)
// audio -> UI
static volatile int   state = REC_IDLE;
static volatile int   mode_now = REC_MODE_TAPE;
static volatile int   loop_len;
static volatile int   pos_i;
static volatile float speed;
static volatile int   rec_len;
static volatile uint32_t loop_start;                       // the frame the loop began playing at (position 0)
static volatile int   closing_v;                           // REC let go, recording on to the next beat
static volatile float lock_err_v;                          // the head's distance from the grid (frames), for the screen
static volatile int   waiting_v;                           // REC down, no sound yet
static volatile float jog_pub;                             // the jog's extra speed, for the screen

// The jog (the left knob): pushes gather here in 1/256 frames; the audio task turns them into an extra speed that
// decays, so the distance asked for is travelled smoothly (the integral of the decay is the push).
#define JOG_MAX     8.0f
static volatile int32_t jog_req;
static volatile float   jog_k = 1.0f / 1920.0f;           // 1 / the glide in samples: 60 ms
static float jog_v;                                        // audio task only

// How a take starts and ends (recorder_set_take).
#define PRE_MAX     1600                                   // 50 ms of pre-roll at most
#define DUCK_FADE   160                                    // 5 ms
static volatile int   take_sound, take_pre, take_guard, take_tail;
static volatile float take_thresh = 0.016f;
static int16_t pre[PRE_MAX];                               // the last samples while waiting for a sound (internal RAM)
static int   pre_w, pre_n, guard_left, duck_left;
static bool  armed;

// The grid (from the link, set by the UI task each tick under a seqlock): beat 0's frame in this recorder's frames
// (a whole frame and a fraction), the frames per beat, and what the recorder does with it (GRID_* in recorder.h).
// The loop's anchor is the frame where its position is 0 on the grid, kept as whole frames after beat 0 so it moves
// with the grid (the link slews beat 0 by a few frames a second on a board that is not the clock): the head is
// pulled to ((now - anchor) * r) mod len while the speed sits on a musical ratio r. Trims and anchors from the UI
// ride the same seqlock, each with a request number the audio task applies once.
typedef struct { uint32_t fpb, origin; float frac; int mode; int len_req; uint32_t len_v, anchor_v, for_loop; } grid_t;
static volatile uint32_t grid_seq;
static grid_t grid_ui;
static grid_t G;                                           // the audio task's copy
static int    len_done;

// audio task only
static bool  rec_prev, closing;
static int   close_at;                                     // closing: the take closes once rec_len reaches this
static float pos_f;
static float pitch;                                        // slewed
static uint32_t frames;                                    // frames processed so far: audio_frames() at the block's start
static uint32_t rec_start, anchor_off;                     // the take's first frame; the loop's position 0: frames after the grid's beat 0
static int   xf_from_i, xf_left;                           // a jump of the head: the old place still read, samples left
static float xf_from_f;
typedef struct { int start; float ph; int len; } grain_t;
static grain_t gr[NGRAIN];

bool recorder_init(void)
{
    buf = heap_caps_malloc((MAX_SAMPLES + GUARD) * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    diag_ram("rec.take", buf, (MAX_SAMPLES + GUARD) * sizeof(int16_t), false);
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
static inline bool finite_f(float x) { uint32_t b; memcpy(&b, &x, 4); return (b & 0x7f800000u) != 0x7f800000u; }   // -ffast-math hides x != x
void  recorder_set_speed(float s)
{
    if (!finite_f(s)) return;                              // a NaN here would send the head anywhere
    if (s > REC_SPEED_LIMIT) s = REC_SPEED_LIMIT;
    if (s < -REC_SPEED_LIMIT) s = -REC_SPEED_LIMIT;
    speed_target = s;
}
void  recorder_set_pitch(float r)
{
    if (!finite_f(r)) return;
    if (r < REC_PITCH_MIN) r = REC_PITCH_MIN;
    if (r > REC_PITCH_MAX) r = REC_PITCH_MAX;
    pitch_target = r;
}
void  recorder_set_grid(uint32_t fpb, uint32_t origin, float frac, int mode)
{
    if (fpb < 3200 || fpb > 96000 || !finite_f(frac)) mode = 0;
    __atomic_add_fetch(&grid_seq, 1, __ATOMIC_ACQ_REL);
    grid_ui.fpb = fpb; grid_ui.origin = origin; grid_ui.frac = frac; grid_ui.mode = mode;
    __atomic_add_fetch(&grid_seq, 1, __ATOMIC_ACQ_REL);
}
void  recorder_set_loop(int len, uint32_t anchor_frame, uint32_t loop)
{
    if (len < MIN_LOOP) return;
    __atomic_add_fetch(&grid_seq, 1, __ATOMIC_ACQ_REL);
    grid_ui.len_v = (uint32_t)len; grid_ui.anchor_v = anchor_frame; grid_ui.for_loop = loop; grid_ui.len_req++;
    __atomic_add_fetch(&grid_seq, 1, __ATOMIC_ACQ_REL);
}
static void grid_pull(void)
{
    uint32_t q = __atomic_load_n(&grid_seq, __ATOMIC_ACQUIRE);
    if (q & 1u) return;
    grid_t t = grid_ui;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&grid_seq, __ATOMIC_RELAXED) == q) G = t;
}
void  recorder_set_mode(int m)     { mode_req = m == REC_MODE_GRAIN ? REC_MODE_GRAIN : REC_MODE_TAPE; }
void  recorder_set_grain_ms(int ms)
{
    if (ms < 20) ms = 20;
    if (ms > 200) ms = 200;
    grain_len = ms * (REC_SAMPLE_RATE / 1000);
}
void  recorder_set_volume(float v) { volume = v < 0 ? 0 : v > 2 ? 2 : v; }
void  recorder_jog(float fr)
{
    if (!finite_f(fr) || state != REC_PLAYING) return;
    const float lim = (float)(REC_SAMPLE_RATE * 4);        // one call never asks for more than 4 s of tape
    if (fr > lim) fr = lim; else if (fr < -lim) fr = -lim;
    __atomic_fetch_add(&jog_req, (int32_t)(fr * 256.0f), __ATOMIC_ACQ_REL);
}
void  recorder_set_jog_ms(float ms) { if (finite_f(ms) && ms >= 1.0f) jog_k = 1.0f / (ms * (float)(REC_SAMPLE_RATE / 1000)); }
float recorder_jog_now(void)       { return jog_pub; }
void  recorder_set_take(bool on_sound, float thresh, int preroll, int guard, int tail)
{
    if (!finite_f(thresh) || thresh < 0.0005f) thresh = 0.0005f;
    take_thresh = thresh > 0.9f ? 0.9f : thresh;
    take_pre = preroll < 0 ? 0 : preroll > PRE_MAX ? PRE_MAX : preroll;
    take_guard = guard < 0 ? 0 : guard > REC_SAMPLE_RATE ? REC_SAMPLE_RATE : guard;
    take_tail = tail < 0 ? 0 : tail > REC_SAMPLE_RATE ? REC_SAMPLE_RATE : tail;
    take_sound = on_sound;
}
bool  recorder_waiting(void)       { return waiting_v != 0; }
int   recorder_state(void)        { return state; }
int   recorder_mode(void)         { return mode_now; }
float recorder_loop_seconds(void) { return (float)loop_len / (float)REC_SAMPLE_RATE; }
int   recorder_loop_frames(void)  { return loop_len; }
float recorder_position(void)     { int l = loop_len; return l ? (float)pos_i / (float)l : 0.0f; }
float recorder_speed_now(void)    { return speed; }
float recorder_record_fill(void)  { return (float)rec_len / (float)MAX_SAMPLES; }
uint32_t recorder_loop_start(void) { return loop_start; }
uint32_t recorder_frames(void)     { return frames; }
bool  recorder_closing(void)       { return closing_v != 0; }
float recorder_lock_error(void)    { return lock_err_v; }
const int16_t *recorder_take(int *n) { if (state == REC_RECORDING || rec_len <= 0) { *n = 0; return NULL; } *n = rec_len; return buf; }
int16_t *recorder_load_begin(int *max) { if (state == REC_RECORDING) return NULL; state = REC_IDLE; loop_len = 0; *max = MAX_SAMPLES; return buf; }
// Playing a loaded take of n samples: its first le frames as the loop (0: all but the seam), on the grid or from
// position 0 now. A loop longer than the take is padded with silence. UI task; the audio task is idle meanwhile, so
// these stores are safe.
static void start_loop(int le, bool on_grid);
void recorder_load_end(int n, int le, bool on_grid)
{
    if (n <= 0) return;
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;
    if (le <= 0) le = n - XF;
    if (le + XF > n) {
        if (le + XF > MAX_SAMPLES) le = n - XF;
        else { memset(buf + n, 0, (size_t)(le + XF - n) * sizeof(int16_t)); n = le + XF; }
    }
    rec_len = n;
    if (le < MIN_LOOP) return;
    for (int g = 0; g < GUARD; g++) buf[n + g] = buf[n - 1];
    rec_start = frames;
    start_loop(le, on_grid);
}
int recorder_take_max(void) { return MAX_SAMPLES; }
#ifndef ESP_PLATFORM
void recorder_debug_grid(uint32_t *origin, uint32_t *fpb, int *mode, uint32_t *anchor_out) { *origin = G.origin; *fpb = G.fpb; *mode = G.mode; *anchor_out = G.origin + anchor_off; }
#endif
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

// The grid lock: while the speed sits on (or within 2 % of) a musical ratio r of 1x, the head belongs at
// ((now - anchor) * r) mod len. The small error that the link's slew and the speed's own slew leave is pulled out
// by bending the speed up to 3 % (an error of 15 ms takes a second); a large one (the head was scrubbed away)
// is left alone, or, with GRID_JUMP, crossfaded straight to the beat in 10 ms. Nothing here is per sample.
#define LOCK_WIN  640                                      // 20 ms: nudge within, jump beyond (GRID_JUMP) or leave it
#define LOCK_MAX  0.03f
static const float ratios[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
static int ratio_of(float a)                               // index of the ratio |a| sits on, -1 for none
{
    for (unsigned k = 0; k < sizeof ratios / sizeof ratios[0]; k++) if (fabsf(a - ratios[k]) <= 0.02f * ratios[k]) return (int)k;
    return -1;
}
// the loop's place on the grid at this frame, at ratio index k with sign (the loop plays 8x..1/4x: both periods
// divide 8 loops, so the count since the anchor is kept below that and never wraps)
static int grid_pos(uint32_t now, int le, int k, bool back)
{
    uint32_t g8 = (now - (G.origin + anchor_off)) % (uint32_t)(8 * le);
    uint32_t num = k >= 2 ? 1u << (k - 2) : 1u, den = k < 2 ? 1u << (2 - k) : 1u;     // 1/4 1/2 1 2 4 8
    uint32_t e = (uint32_t)(((uint64_t)g8 * num / den) % (uint32_t)le);
    if (back) e = e ? (uint32_t)le - e : 0;
    return (int)e;
}
// the take has just closed at this frame (the block's first): the loop plays from a position that keeps the beats
// where they were sung (anchor: the grid beat nearest the take's start), or from 0 with the anchor here
static void start_loop(int le, bool on_grid)
{
    loop_len = le; speed = speed_target; pitch = pitch_target; xf_left = 0;
    for (int g = 0; g < NGRAIN; g++) { gr[g].start = 0; gr[g].len = grain_len; gr[g].ph = (float)(g * gr[g].len) / NGRAIN; }
    anchor_off = frames - G.origin;
    pos_i = 0; pos_f = 0.0f;
    if (on_grid && G.fpb) {
        int32_t d = (int32_t)(rec_start - G.origin);                  // the take's start against beat 0 (signed, wraps)
        int32_t b = (d >= 0 ? d + (int32_t)G.fpb / 2 : d - (int32_t)G.fpb / 2) / (int32_t)G.fpb;
        anchor_off = (uint32_t)(b * (int32_t)G.fpb);
        pos_i = grid_pos(frames, le, 2, false);
    }
    for (int g = 0; g < NGRAIN; g++) gr[g].start = pos_i;
    loop_start = frames;
    state = REC_PLAYING;
}

void IRAM_ATTR recorder_process(const float *in, float *out, int n)
{
    grid_pull();
    bool r = rec_req;
    if (r && !rec_prev) {
        rec_len = 0; rec_start = frames; closing = false; closing_v = 0; loop_len = 0; xf_left = 0;
        guard_left = take_guard; armed = take_sound != 0; pre_w = pre_n = 0; duck_left = 0;
        waiting_v = armed;
        state = REC_RECORDING;
    } else if (!r && rec_prev && state == REC_RECORDING) {
        // the release's click was recorded just before the release was seen: the take ends before it
        const int full = rec_len, tail = take_tail;
        int L = armed ? 0 : full > tail ? full - tail : 0;      // no sound ever came: nothing
        armed = false; waiting_v = 0;
        if (L < MIN_LOOP + XF) {                               // a tap, not a take: the old loop is gone
            rec_len = 0; loop_len = 0;
            state = REC_IDLE;
        } else if ((G.mode & GRID_SNAP_END) && G.fpb) {
            // whole beats of the grid: the nearest, at least one; too short for that, it records on to the beat
            int fpb = (int)G.fpb, nb = (L + fpb / 2) / fpb;
            if (nb < 1) nb = 1;
            int want = nb * fpb;
            if (want + XF > MAX_SAMPLES) { nb = (MAX_SAMPLES - XF) / fpb; want = nb * fpb; }
            if (nb >= 1 && want + XF <= L) { rec_len = L; start_loop(want, true); }
            else if (nb >= 1) {
                // it records on, so the click stays inside the take: faded out where it begins, silent while it rings
                closing = true; closing_v = 1; close_at = want + XF;
                if (tail > 0) {
                    int f0 = L > DUCK_FADE ? L - DUCK_FADE : 0;
                    for (int q = f0; q < L; q++) buf[q] = (int16_t)((int)buf[q] * (L - q) / (L - f0));
                    for (int q = L; q < full; q++) buf[q] = 0;
                    duck_left = tail + DUCK_FADE;
                }
            }
            else if (L - XF >= MIN_LOOP) { rec_len = L; start_loop(L - XF, true); }
            else { rec_len = 0; loop_len = 0; state = REC_IDLE; }
        } else { rec_len = L; start_loop(L - XF, false); }
        if (state != REC_RECORDING) { int e = rec_len; for (int g = 0; g < GUARD; g++) buf[e + g] = buf[e > 0 ? e - 1 : 0]; }
    }
    rec_prev = r;

    if (state == REC_RECORDING) {
        int len = rec_len;
        const float thr = take_thresh;
        for (int i = 0; i < n; i++) {
            float x = in[i];
            out[i] = x;
            if (guard_left > 0) { guard_left--; continue; }     // the press's click: not a sample of it is kept
            float s = x * 32767.0f;
            if (s > 32767.0f) s = 32767.0f; else if (s < -32768.0f) s = -32768.0f;
            if (armed) {                                        // waiting for the first sound, the last moments kept by
                if (x < thr && x > -thr) {
                    pre[pre_w] = (int16_t)s;
                    if (++pre_w >= PRE_MAX) pre_w = 0;
                    if (pre_n < PRE_MAX) pre_n++;
                    continue;
                }
                int k = take_pre < pre_n ? take_pre : pre_n;    // here it is: its attack goes in first
                for (int j = 0; j < k; j++) { int q = pre_w - k + j; if (q < 0) q += PRE_MAX; buf[len++] = pre[q]; }
                armed = false; waiting_v = 0;
                rec_start = frames + (uint32_t)i - (uint32_t)k;
            } else if (len == 0) rec_start = frames + (uint32_t)i;
            if (duck_left > 0) {                                // recording on to the beat: the release's click rings out unheard
                if (duck_left <= DUCK_FADE) s *= 1.0f - (float)duck_left * (1.0f / DUCK_FADE); else s = 0.0f;
                duck_left--;
            }
            if (len < MAX_SAMPLES) buf[len++] = (int16_t)s;
        }
        rec_len = len;
        frames += (uint32_t)n;
        if (closing && (len >= close_at || len >= MAX_SAMPLES)) {   // the beat is reached (or the tape is full)
            closing = false; closing_v = 0;
            for (int g = 0; g < GUARD; g++) buf[len + g] = buf[len - 1];
            int le = len >= close_at ? close_at - XF : len - XF;
            if (le >= MIN_LOOP) start_loop(le, true); else { loop_len = 0; state = REC_IDLE; }
        }
        return;
    }

    if (state != REC_PLAYING) {
        memset(out, 0, n * sizeof(float));
        frames += (uint32_t)n;
        vol_s = volume;
        jog_v = 0.0f; jog_pub = 0.0f; jog_req = 0;
        return;
    }

    // a trim or a new anchor from the UI task (once per request, and only for the loop it was meant for: a request
    // made for a loop that a new take has since replaced is dropped)
    if (G.len_req != len_done) {
        len_done = G.len_req;
        if (G.for_loop == loop_start) {
            if ((int)G.len_v >= MIN_LOOP && (int)G.len_v <= rec_len - XF) {
                loop_len = (int)G.len_v;
                if (pos_i >= loop_len) pos_i -= loop_len;
                for (int g = 0; g < NGRAIN; g++) if (gr[g].start >= loop_len) gr[g].start = 0;
            }
            anchor_off = G.anchor_v - G.origin;
        }
    }

    const int   le  = loop_len;
    const int   len = rec_len;
    const float st  = speed_target;
    const float pt  = pitch_target;
    // the volume glides (about 50 ms to settle 63 %), so the encoder's 5 % steps sound like a fader, not a staircase
    const float vt = volume;
    float vk = 1.0f - (float)n * (1.0f / 1600.0f); if (vk < 0.0f) vk = 0.0f;
    float v1 = vt + (vol_s - vt) * vk;
    if (fabsf(v1 - vt) < 1e-4f) v1 = vt;
    const float vol = vol_s, dvol = (v1 - vol_s) / (float)n;
    vol_s = v1;
    const int   mode = mode_req;
    mode_now = mode;
    int   ip = pos_i;
    float fp = pos_f;
    float sp = speed;
    float pc = pitch;

    // the jog: what the knob pushed since the last block becomes speed that dies away (its integral is the push)
    const float jk = jog_k;
    float jv = jog_v;
    {
        int32_t jq = __atomic_exchange_n(&jog_req, 0, __ATOMIC_ACQ_REL);
        if (jq) jv += (float)jq * (1.0f / 256.0f) * jk;
        if (jv > JOG_MAX) jv = JOG_MAX; else if (jv < -JOG_MAX) jv = -JOG_MAX;
        if (jv < 1e-5f && jv > -1e-5f) jv = 0.0f;                 // no crawling into denormals
    }
    const bool jogging = jv > 0.01f || jv < -0.01f;

    // the grid lock, decided once per block: the loop is whole beats of the grid and the speed sits on a ratio
    // (not while the knob moves the tape: the hand wins, and the beat takes the head back afterwards)
    float corr = 0.0f;
    int k = !jogging && (G.mode & GRID_LOCK) && G.fpb && (uint32_t)le % G.fpb == 0 ? ratio_of(fabsf(st)) : -1;
    if (k >= 0 && fabsf(sp - st) < 0.01f * fabsf(st)) {
        float rt = st < 0 ? -ratios[k] : ratios[k];
        int e = grid_pos(frames, le, k, st < 0) - ip;
        if (e > le / 2) e -= le; else if (e < -le / 2) e += le;
        float err = (float)e - fp - rt * G.frac;                      // beat 0's fraction of a frame, at this ratio
        lock_err_v = err;
        if (fabsf(err) <= (float)LOCK_WIN) {
            corr = err * (1.0f / 16000.0f) + (rt - sp);           // towards the place, and onto the ratio exactly
            if (corr > LOCK_MAX) corr = LOCK_MAX; else if (corr < -LOCK_MAX) corr = -LOCK_MAX;
        } else if (G.mode & GRID_JUMP) {
            xf_from_i = ip; xf_from_f = fp; xf_left = mode == REC_MODE_TAPE ? XF : 0;
            ip += e; if (ip >= le) ip -= le; else if (ip < 0) ip += le;
            for (int g = 0; g < NGRAIN; g++) gr[g].start = ip;
        }
    } else lock_err_v = 0.0f;

    const float sk = speed_slew;                                   // tilt.slew (the tuning table), read once a block
    for (int i = 0; i < n; i++) {
        sp += sk * (st - sp);
        float y;
        if (mode == REC_MODE_TAPE) {
            y = read_loop(ip, fp, le, len);
            if (xf_left > 0) {                                      // a jump: the old place fades out under the new
                float w = xf_w[XF - xf_left];
                y = y * w + read_loop(xf_from_i, xf_from_f, le, len) * (1.0f - w);
                xf_left--;
                xf_from_f += sp + corr + jv;
                if (xf_from_f >= 1.0f)      { int s = (int)xf_from_f; xf_from_i += s; xf_from_f -= (float)s; }
                else if (xf_from_f < 0.0f)  { int s = (int)(-xf_from_f) + 1; xf_from_i -= s; xf_from_f += (float)s; }
                if (xf_from_i >= le) xf_from_i -= le; else if (xf_from_i < 0) xf_from_i += le;
            }
        } else {
            pc += SLEW * (pt - pc);
            y = 0.0f;
            for (int g = 0; g < NGRAIN; g++) {
                grain_t *G_ = &gr[g];
                if (G_->ph >= (float)G_->len) { G_->start = ip; G_->ph -= (float)G_->len; G_->len = grain_len; }
                int   gi = (int)G_->ph;
                float gf = G_->ph - (float)gi;
                int   ri = G_->start + gi; if (ri >= le) ri -= le;
                float w = win[(int)(G_->ph * (float)WIN_N / (float)G_->len)];
                y += read_loop(ri, gf, le, len) * w;
                G_->ph += pc;
            }
        }
        out[i] = y * (vol + dvol * (float)i);

        jv -= jv * jk;
        fp += sp + corr + jv;
        if (fp >= 1.0f)      { int s = (int)fp; ip += s; fp -= (float)s; }
        else if (fp < 0.0f)  { int s = (int)(-fp) + 1; ip -= s; fp += (float)s; }
        if (ip >= le) ip -= le; else if (ip < 0) ip += le;
    }

    pos_i = ip; pos_f = fp; speed = sp; pitch = pc;
    jog_v = jv; jog_pub = jv;
    frames += (uint32_t)n;
}

void recorder_set_slew_ms(float ms) { if (ms >= 1.0f) speed_slew = 1.0f / (ms * (float)(REC_SAMPLE_RATE / 1000)); }   // 50 ms: SLEW
