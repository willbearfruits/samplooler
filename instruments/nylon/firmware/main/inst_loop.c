// NYLON's looper, made for hands that are on the strings: one button (or a footswitch on the REC pin).
//
//   empty     tap: armed. With the METRONOME on, one bar of count-in and the take starts on the next bar line.
//             Without it, for 0.8 s nothing starts it (the button's own click and its let-go, through the wood);
//             then the first note does (START ON A NOTE), or it starts 25 ms after the let-go (START AT ONCE)
//   armed     tap: not any more
//   recording LENGTH in bars: it closes by itself after that many bars of TEMPO, exactly. FREE: tap where it should
//             close (on the nearest bar line with the METRONOME on). A tap in bars mode closes it early the same way
//   playing   tap: a layer goes on from just after the let-go; hold: stop
//   adding    tap: the layer ends just before the tap; hold: stop
//   stopped   tap: plays from the start; hold on: erase
//   anywhere  hold on past 2 s: erase (instrument.c times the holds)
//
// The button is on the guitar's top, so the mic hears it click. Nothing of that click goes into the loop:
//   - the take starts at a note heard 0.8 s or more after the tap (or 50 ms after it);
//   - the loop is [start - 20 ms, close - 20 ms): the note sounds again exactly a loop later, and only 6 ms of what
//     came after the loop's end (20..14 ms before the close) is crossfaded into its start: the seam is smooth and the
//     click at the close is after it;
//   - a layer starts 25 ms after the let-go of the tap that starts it (the button clicks going down and coming up),
//     and is written 40 ms behind the play head, from what was played 40 ms earlier, so its end can be put 15 ms
//     before the tap that ends it although that click has already been heard.
// With the METRONOME on (instrument.c sets the grid: E.l_grid0, E.l_fpb) a take starts on a bar line after a bar of
// count-in, and every close lands on a bar line.
//
// UNDO takes the newest layer off: while the loop fits in half the buffer, each sample a layer touches first is
// copied to the other half before it is written. TAPE (the neck) plays it at another speed or backwards; a layer
// goes on at 1x (the speed glides there).
#include "inst_nylon.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>

#define RING     32768u                      // 1.02 s of what was played, a power of two
#define RMASK    (RING - 1u)
#define PRE      640                         // 20 ms of run-up before a take's first note
#define XF       192                         // 6 ms: ramps, and the seam's crossfade
#define DEAF     (AUDIO_SR * 8 / 10)         // armed: 0.8 s before a note can start the take
#define AFTER    (AUDIO_SR * 25 / 1000)      // a layer starts 25 ms after its tap's let-go
#define BEFORE   (AUDIO_SR * 15 / 1000)      // and ends 15 ms before its tap
#define DLY      (AUDIO_SR * 40 / 1000)      // a layer is written this far behind the play head
#define MINLEN   (AUDIO_SR / 4)              // a quarter second: shorter is a slip
#define CATCH    2048                        // a layer's start mixed in from the ring: this many samples a block
#define UCOPY    4096                        // UNDO copies back this many a block

SPSC(lcmdq, lcmd_t, 16)
static lcmdq_t q;

static int16_t *buf, *ring, *bak;
static int lmax;                             // samples in buf
static lstate_t st;
static int len, rec_k, rec_pre, take_target;
static uint32_t rec_t0;                      // the frame of buf[0] while recording
static uint32_t deaf_until, start_at, close_at;
static bool start_pending, close_pending;
static float pos, rate_s = 1.0f;
static float play_g, play_t, lvl_s;
static bool erasing, undo_after;
// a layer: written DLY behind the head, from frame dub_ts of what was played to dub_te
static bool dubbing, dub_te_set;
static uint32_t dub_ts, dub_te;
static float dub_g;
static uint32_t cu_f; static int cu_n, cu_p, cu_done;   // a layer's start caught up from the ring
static int dub_from, dub_n;                  // the layer's samples: dub_n of them from dub_from (UNDO)
static bool undo_ok;
static int u_stage, u_copied; static float u_x;
static int layers;
static float bar_len_s;                      // the take's bar, frames (0: free)
static volatile int v_state, v_layers, v_undo, v_bars, v_bar;
static volatile float v_pos, v_len, v_rate = 1.0f;

static inline int16_t s16(float x)
{
    x *= 32767.0f;
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32767.0f) x = -32767.0f;
    return (int16_t)x;
}
static inline float f32(int16_t v) { return (float)v * (1.0f / 32767.0f); }

bool loop_init(void)
{
    if (buf) return true;
    static const int secs[] = { 22, 18, 14, 10, 8 };
    for (int k = 0; k < 5 && !buf; k++) {
        buf = app_alloc((size_t)secs[k] * AUDIO_SR * sizeof(int16_t));
        if (buf) lmax = secs[k] * AUDIO_SR;
    }
    ring = app_alloc(RING * sizeof(int16_t));
    if (!buf || !ring) { app_free(buf); app_free(ring); buf = ring = NULL; app_log("nylon: no memory for the loop"); return false; }
    app_log("nylon: loop up to %d s, UNDO up to %d s", (lmax - PRE) / AUDIO_SR, lmax / 2 / AUDIO_SR);
    lcmdq_clear(&q);
    return true;
}

void loop_cmd(int cmd, uint32_t at) { lcmd_t c = { (uint8_t)cmd, at }; lcmdq_push(&q, &c); }
lstate_t loop_state(void) { return (lstate_t)v_state; }
float loop_pos(void) { return v_pos; }
float loop_len_s(void) { return v_len; }
float loop_max_s(void) { return (float)(lmax - PRE - XF) / (float)AUDIO_SR; }
int   loop_layers(void) { return v_layers; }
bool  loop_can_undo(void) { return v_undo != 0; }
bool  loop_undo_fits(void) { return v_state >= L_PLAY ? v_len * (float)AUDIO_SR <= (float)(lmax / 2) : true; }
float loop_rate_now(void) { return v_rate; }
int   loop_bar(int *of) { if (of) *of = v_bars; return v_bar; }

// a sample of the loop as it plays: during UNDO the touched part comes from the copy
static inline float smp(int i)
{
    float y = f32(buf[i]);
    if (u_stage) {
        int d = i - dub_from; if (d < 0) d += len;
        if (d < dub_n) {
            float b = f32(bak[i]);
            if (u_stage == 1) y += u_x * (b - y);
            else if (d >= u_copied) y = b;
        }
    }
    return y;
}
static inline int wrapi(int p, int n) { while (p < 0) p += n; while (p >= n) p -= n; return p; }

// the take starts: buf[0] is frame t0, its first note (or beat) pre frames in; what was played since is in the ring
static void start_take(uint32_t t0, int pre, uint32_t fnow)
{
    uint32_t k = fnow - t0;
    if ((int32_t)k < 0) k = 0;
    if (k > RING - 1) k = RING - 1;
    for (uint32_t j = 0; j < k; j++) buf[j] = ring[(t0 + j) & RMASK];
    rec_t0 = fnow - k; rec_k = (int)k; rec_pre = pre;
    st = L_REC; len = 0; layers = 0; undo_ok = false; u_stage = 0; cu_n = cu_done = 0;
    play_g = play_t = 0; dubbing = false; start_pending = close_pending = false;
    // bars: the loop's length is known now; one that would not fit is halved until it does
    float fpb = E.l_fpb;
    bar_len_s = fpb > 1.0f ? fpb * (float)clampi(E.l_beats, 1, 16) : 0.0f;
    take_target = 0;
    if (E.l_bars > 0 && bar_len_s > 0.0f) {
        int bars = E.l_bars;
        while (bars > 1 && (float)bars * bar_len_s > (float)(lmax - pre - XF)) bars /= 2;
        take_target = (int)((float)bars * bar_len_s + 0.5f);
        if (take_target > lmax - pre - XF) take_target = 0;
        if (take_target) { close_pending = true; close_at = rec_t0 + (uint32_t)pre + (uint32_t)take_target; }
    }
}

static void close_take(uint32_t at, uint32_t fnow)
{
    int L = (int)(at - rec_t0) - rec_pre;
    if (L > rec_k - XF) L = rec_k - XF;
    if (L < MINLEN) { st = L_EMPTY; len = 0; close_pending = false; return; }
    // 6 ms of what came after the loop's end (still before the close's click) is crossfaded into its start
    for (int j = 0; j < XF; j++) { float r = (float)j / (float)XF; buf[j] = s16(f32(buf[j]) * r + f32(buf[L + j]) * (1.0f - r)); }
    len = L;
    int p = wrapi(rec_pre + (int)(fnow - at), len);
    pos = (float)p; rate_s = 1.0f;
    play_g = 0.0f; play_t = 1.0f;
    st = L_PLAY; layers = 1; undo_ok = false; close_pending = false;
    bak = len <= lmax / 2 ? buf + lmax / 2 : NULL;
}

static void layer_done(void) { layers++; undo_ok = bak != NULL && dub_n > 0; dubbing = false; }

static void command(const lcmd_t *c, uint32_t f0)
{
    switch (c->cmd) {
    case LC_ARM: if (st == L_EMPTY) { st = L_ARMED; deaf_until = c->at + DEAF; start_pending = false; } break;
    case LC_DISARM:                                              // the tap's own click may have started a take just now: not that either
        if (st == L_ARMED || (st == L_REC && rec_k - rec_pre < AUDIO_SR / 5)) { st = L_EMPTY; len = 0; start_pending = close_pending = false; }
        break;
    case LC_REC_AT: if (st == L_EMPTY || st == L_ARMED) { st = L_ARMED; start_pending = true; start_at = c->at; } break;
    case LC_CLOSE_AT:
        if (st == L_REC) {
            uint32_t tgt = c->at;
            if (E.l_snap && bar_len_s > 0.0f) {                    // on the bar line nearest the tap
                float line0 = (float)(rec_t0 + (uint32_t)rec_pre);
                float nb = ((float)c->at - line0) / bar_len_s;
                int b = (int)(nb + 0.5f); if (b < 1) b = 1;
                tgt = (uint32_t)(line0 + (float)b * bar_len_s + 0.5f);
            }
            close_pending = true; close_at = tgt;
            if ((int32_t)(f0 - tgt) >= 0) close_take(tgt, f0);
        }
        break;
    case LC_DUB_AT: {
        if (st != L_PLAY || u_stage || len <= DLY + XF) break;
        dub_ts = c->at + AFTER; dub_te_set = false;
        int ip = (int)pos;
        pos = (float)ip; rate_s = 1.0f;
        uint32_t fd = f0 - DLY;                                  // what the writer writes now
        int B = (int32_t)(fd - dub_ts) > 0 ? (int)(fd - dub_ts) : 0;
        if (B > (int)RING - 1) B = (int)RING - 1;
        if (B > len - DLY - XF) B = len - DLY - XF > 0 ? len - DLY - XF : 0;
        int p_ts = wrapi(ip - DLY - B, len);                     // where frame dub_ts plays in the loop
        cu_f = fd - (uint32_t)B; cu_n = B; cu_done = 0; cu_p = p_ts;
        dub_from = p_ts; dub_n = B;
        dub_g = B >= XF ? 1.0f : (float)B / (float)XF;           // a catch-up ramps itself in; the writer goes on from there
        undo_ok = false;
        dubbing = true; st = L_DUB;
        break;
    }
    case LC_DUB_OFF: if (st == L_DUB && !dub_te_set) { dub_te = c->at - BEFORE; dub_te_set = true; } break;
    case LC_STOP:
        if (st == L_PLAY || st == L_DUB) {
            play_t = 0.0f;
            if (dubbing && !dub_te_set) { dub_te = f0 - DLY; dub_te_set = true; }
        }
        break;
    case LC_PLAY: if (st == L_STOP && len > 0) { st = L_PLAY; pos = 0.0f; play_g = 0.0f; play_t = 1.0f; } break;
    case LC_ERASE:
        if (st == L_REC || st == L_ARMED || st == L_STOP) { st = L_EMPTY; len = 0; layers = 0; undo_ok = false; u_stage = 0; dubbing = false; start_pending = close_pending = false; }
        else if (st == L_PLAY || st == L_DUB) { erasing = true; play_t = 0.0f; if (dubbing && !dub_te_set) { dub_te = f0 - DLY; dub_te_set = true; } }
        break;
    case LC_UNDO:
        if (st == L_DUB) { if (!dub_te_set) { dub_te = f0 - DLY; dub_te_set = true; } undo_after = true; break; }
        if ((st == L_PLAY || st == L_STOP) && undo_ok && !u_stage && cu_done >= cu_n) { u_stage = 1; u_x = 0.0f; }
        break;
    }
}

void loop_process(const float *src, float *io, int n, float onset_lvl, bool onset_now, uint32_t onset_frame)
{
    uint32_t f0 = audio_frames();
    if (!buf) { if (io != src) memcpy(io, src, (size_t)n * sizeof(float)); return; }
    lcmd_t c;
    while (lcmdq_pop(&q, &c)) command(&c, f0);
    // armed: the first note over TRIGGER after the deaf time starts the take (with the METRONOME the UI sends a start
    // frame instead: a bar of count-in)
    if (st == L_ARMED && !start_pending && onset_now && onset_lvl >= E.l_trig && (int32_t)(onset_frame - deaf_until) >= 0)
        start_take(onset_frame - PRE, PRE, f0);
    // a layer's start, mixed in where it was played
    if (cu_done < cu_n && len > 0) {
        int m = cu_n - cu_done; if (m > CATCH) m = CATCH;
        for (int j = 0; j < m; j++) {
            int k = cu_done + j, p = wrapi(cu_p + k, len);
            if (bak) bak[p] = buf[p];
            float r = k < XF ? (float)k / (float)XF : 1.0f;
            buf[p] = s16(f32(buf[p]) + r * f32(ring[(cu_f + (uint32_t)k) & RMASK]));
        }
        cu_done += m;
    }
    // UNDO: crossfade to the copy over 30 ms, then copy it back a slice a block
    if (u_stage == 1) {
        u_x += (float)n / (0.03f * (float)AUDIO_SR);
        if (u_x >= 1.0f) { u_x = 1.0f; u_stage = 2; u_copied = 0; }
    } else if (u_stage == 2) {
        int m = dub_n - u_copied; if (m > UCOPY) m = UCOPY;
        for (int j = 0; j < m; j++) { int p = wrapi(dub_from + u_copied + j, len); buf[p] = bak[p]; }
        u_copied += m;
        if (u_copied >= dub_n) { u_stage = 0; undo_ok = false; dub_n = 0; cu_n = cu_done = 0; if (layers > 1) layers--; }
    }

    float keep = clampf(E.l_keep, 0.0f, 1.0f);
    float l0 = lvl_s, l1 = clampf(E.l_level, 0.0f, 2.0f), dl = (l1 - l0) / (float)n;
    lvl_s = l1;
    float rate_t = dubbing || cu_done < cu_n ? 1.0f : clampf(E.l_rate, -2.0f, 2.0f);
    const float ramp = 1.0f / (float)XF;
    for (int i = 0; i < n; i++) {
        uint32_t f = f0 + (uint32_t)i;
        float x = src[i];
        ring[f & RMASK] = s16(x);
        io[i] = x;
        if (st == L_ARMED) {
            if (start_pending && (int32_t)(f + 1u - (start_at - PRE)) >= 0) start_take(start_at - PRE, PRE, f + 1u);
            continue;
        }
        if (st == L_REC) {
            buf[rec_k++] = s16(x);
            if (close_pending && (int32_t)(f + 1u - close_at) >= 0) close_take(close_at, f + 1u);
            else if (rec_k >= lmax) close_take(f + 1u, f + 1u);
            continue;
        }
        if (len <= 0 || st == L_EMPTY || st == L_STOP) continue;
        // the head's speed: exactly 1 unless the neck plays the tape
        if (rate_s != rate_t) {
            rate_s += 0.0006f * (rate_t - rate_s);
            if (fabsf(rate_s - rate_t) < 2e-4f) rate_s = rate_t;
            if (rate_s == 1.0f) pos = (float)(int)pos;
        }
        int ip = (int)pos;
        float y;
        if (rate_s == 1.0f) {
            y = smp(ip);
            if (keep < 0.999f) buf[ip] = s16(y * keep);
            if (dubbing) {                                       // the layer: what was played DLY ago, where it was played
                uint32_t fd = f - DLY;
                float tgt = (int32_t)(fd - dub_ts) >= 0 && (!dub_te_set || (int32_t)(fd + XF - dub_te) < 0) ? 1.0f : 0.0f;
                if (dub_g < tgt) { dub_g += ramp; if (dub_g > tgt) dub_g = tgt; }
                else if (dub_g > tgt) { dub_g -= ramp; if (dub_g < tgt) dub_g = tgt; }
                if (dub_g > 0.0f) {
                    int p = wrapi(ip - DLY, len);
                    if (bak && dub_n < len) {
                        if (dub_n == 0 && cu_n == 0) dub_from = p;
                        int d = wrapi(p - dub_from, len);
                        if (d == dub_n) { bak[p] = buf[p]; dub_n++; }
                    }
                    buf[p] = s16(f32(buf[p]) + f32(ring[fd & RMASK]) * dub_g);
                } else if (dub_te_set && (int32_t)(fd - dub_te) >= 0) {
                    st = L_PLAY; layer_done();
                    if (undo_after) { undo_after = false; if (undo_ok && cu_done >= cu_n) { u_stage = 1; u_x = 0.0f; } }
                }
            }
        } else {
            int j = ip + 1 >= len ? 0 : ip + 1;
            float a = smp(ip), b = smp(j);
            y = a + (pos - (float)ip) * (b - a);
        }
        io[i] = x + y * (l0 + dl * (float)i) * play_g;
        pos += rate_s;
        if (pos >= (float)len) pos -= (float)len;
        else if (pos < 0.0f) pos += (float)len;
        if (play_g < play_t) { play_g += ramp; if (play_g > play_t) play_g = play_t; }
        else if (play_g > play_t) { play_g -= ramp; if (play_g < play_t) play_g = play_t; }
        if (play_g == 0.0f && play_t == 0.0f && !(dubbing && dub_g > 0.0f)) {
            if (dubbing) { st = L_PLAY; layer_done(); }
            if (erasing) { st = L_EMPTY; len = 0; layers = 0; erasing = false; undo_ok = false; u_stage = 0; break; }
            st = L_STOP;
        }
    }
    v_state = st;
    v_layers = layers;
    v_undo = undo_ok && !u_stage;
    v_pos = len > 0 ? pos / (float)len : 0.0f;
    v_len = (float)(st == L_REC ? (rec_k > rec_pre ? rec_k - rec_pre : 0) : len) / (float)AUDIO_SR;
    v_rate = rate_s;
    if (st == L_REC && bar_len_s > 0.0f) {
        v_bar = rec_k > rec_pre ? 1 + (int)((float)(rec_k - rec_pre) / bar_len_s) : 1;
        v_bars = take_target ? (int)((float)take_target / bar_len_s + 0.5f) : 0;
    } else if (len > 0 && bar_len_s > 0.0f) {
        v_bars = (int)((float)len / bar_len_s + 0.5f);
        v_bar = 1 + (int)(pos / bar_len_s);
    } else { v_bar = v_bars = 0; }
}
