// SING's loop (sing.h): the Samplooler's tape loop, on the song's bars, with the stutter.
//
// The take is written as it comes from the press (position 0 = the press), so nothing waits for a bar line; with
// SYNC its length is whole bars (or a multiple of the session's loop), which keeps it in phase with the session,
// and REC let go early records on to the end of that length. The seam: recording runs 6 ms past the end and that
// tail is blended into the start. Playing, the place in the loop is counted from the press on the session's clock:
// the frames since the press plus how far the link has slewed this board's mapping since then (a few frames a
// second at most), read between samples, so a board that is not the clock stays with the other one's drift.
// The tilt detaches the head (faster, slower, backwards); back near 1 it nudges itself onto the beat (small gaps)
// or crossfades there (large ones). Overdubs are written at the beat's place, never where the tilted head is.
// KEEP PITCH: the tilted head is not read as tape but by two grains of 64 ms, half a grain apart, each read at the
// speed it was sung (backwards when the head goes backwards) from where the head will be at its middle: the loop
// goes faster, slower, stops (a smear of where it stopped) or reverses, at the pitch it was sung, so it stays in key.
// Stopped by a tap, a hold records a new take in its place (the old one is gone, as with CLEAR LOOP).
#include "sing.h"
#include "spsc.h"
#include <string.h>

#define MAXLEN   (LOOP_MAX_S * AUDIO_SR)
#define XF       192                          // 6 ms: seams, ramps
#define RING     16384u                       // 0.51 s of pre-roll, so a hold loses nothing to the tap test
#define RMASK    (RING - 1u)
#define MINLEN   (AUDIO_SR / 5)
#define JUMP     960                          // 30 ms crossfade when the head jumps
#define SLEW     0.000625f                    // the tilt's speed, one-pole per sample (50 ms), as the Samplooler's
#define STT_MAX  32768                        // the stutter's slice: a beat at 58 BPM
#define STT_XF   96
// REC's clicks (the joystick's switch is REC, centimetres from the mic). The press and the release are seen 10-30 ms
// after the switch moves (the debounce, the tick), so the click is just before them and rings on a little.
#define CLICK_POST (AUDIO_SR * 12 / 100)      // 120 ms after a press or a release: not used
#define CLICK_PRE  (AUDIO_SR * 4 / 100)       // 40 ms before a release: cut out too
#define ONSET_PRE  (AUDIO_SR * 15 / 1000)     // a sound's take starts 15 ms before it, to keep its attack
#define ONSET_THR  0.015f                     // what counts as a sound on the voice (after the gate): about -36 dBFS
#define FADE       96                         // 3 ms
#define GRAIN      2048                       // KEEP PITCH: 64 ms grains, two at a time half a grain apart
#define GMIX       (1.0f / 960.0f)            // 30 ms from the tape's read to the grains and back

enum { C_PRESS, C_CONFIRM, C_RELEASE, C_TAP, C_CLEAR };
typedef struct { uint8_t cmd; uint32_t a, b; } lcmd_t;    // C_RELEASE: a = the loop's length (0: none), b = the release's frame
SPSC(lcmdq, lcmd_t, 16)
static lcmdq_t q;

static int16_t *buf, *ring, *stt;
static loop_ctl_t ctl = { .sync = true, .speed = 1, .level = 1, .keep = 1 };
// the audio side
static int      state = LOOPER_EMPTY, layers;
static int      len, wpos;                   // frames; the first take: written so far
static bool     closing, pend;
static uint32_t press_f, take_f;             // the press (pending), the take's position 0
static uint32_t arm_min, duck_until;          // ARM: the first frame a take may start at; recording silenced until
static float    play_g, play_t = 1, dub_g, dub_t;
static float    head, sp = 1;                // the head (frames into the loop, free when tilted) and its speed
static bool     free_head;
static float    xf_from; static int xf_left; // a jump: the old place, samples of crossfade left
static int      last_w = -1;                 // the overdub's last written index
static float    corr_was;
// KEEP PITCH: grain A starts at g_ph = 0, B at GRAIN / 2; A's window is a smooth bump (0 at its ends, 1 in its middle)
// and B's is what A leaves (they add to 1)
static int      g_ph, g_pos[2], g_dir[2] = { 1, 1 }, g_last = 1;
static float    g_mix;
static bool     g_run;
static uint32_t g_rng = 22222u;
// the clock: where the session's beat 0 was in our frames when the loop's length was set; the loop moves with it
// (the link slewing this board onto another's drifting clock), never with the link's raw offset, which also jumps when
// the clock board changes while the beat stays where it is
static uint32_t o_ref; static float f_ref;
static bool     o_set;
static volatile float corr_now;
// for the screen
static volatile int   v_state, v_layers, v_len, v_beats;
static volatile float v_pos, v_speed, v_fill;
static volatile bool  v_closing;
static volatile uint32_t v_take_f;
// the UI side
static uint32_t ui_press;
static int      ui_beats;
static float    stt_amt;

static inline int16_t s16(float x) { x *= 32767.0f; return (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x); }
static inline float f32(int16_t v) { return (float)v * (1.0f / 32767.0f); }

void loop_init(void)
{
    if (buf) return;
    buf = app_alloc(((size_t)MAXLEN + XF + 4) * sizeof(int16_t));
    ring = app_alloc(RING * sizeof(int16_t));
    stt = app_alloc(STT_MAX * sizeof(int16_t));
    if (!buf || !ring || !stt) app_log("sing: no memory for the loop");
    lcmdq_clear(&q);
}

void loop_set(const loop_ctl_t *c) { ctl = *c; stt_amt = c->stutter; }

static void push2(int cmd, uint32_t a, uint32_t b) { lcmd_t c = { (uint8_t)cmd, a, b }; lcmdq_push(&q, &c); }
static void push(int cmd, uint32_t a) { push2(cmd, a, 0); }

// ---------------------------------------------------------------- the UI side
void loop_press(void)
{
    ui_press = audio_frames();
    push(C_PRESS, ui_press);
}
void loop_confirm(void)
{
    if (v_state == LOOPER_STOP) {                                  // stopped and held: a new take in its place
        if (v_beats) host_loop_made(0);
        o_set = false;
        toast("NEW TAKE", 900);
    }
    push(C_CONFIRM, 0);
}

// the length a first take gets, from how long REC was held
static uint32_t take_length(uint32_t held)
{
    if (held > MAXLEN - XF) held = MAXLEN - XF;
    if (!ctl.sync) { ui_beats = 0; return held < MINLEN ? MINLEN : held; }
    uint32_t fpb = song.fpb ? song.fpb : 19200, bar = fpb * (uint32_t)song.bar_beats;
    int beats;
    if (song.loop_beats > 0) {                                 // a multiple of the session's loop
        uint32_t unit = fpb * (uint32_t)song.loop_beats;
        int k = (int)((held + unit / 2) / unit), most = (int)((MAXLEN - XF) / unit);
        if (k < 1) k = 1;
        if (k > most) k = most > 0 ? most : 1;
        beats = k * song.loop_beats;
    } else if (host_tempo_heard()) {                           // whole bars at the tempo that is playing
        int bars = (int)((held + bar / 2) / bar), most = (int)((MAXLEN - XF) / bar);
        if (bars < 1) bars = 1;
        if (bars > most) bars = most > 0 ? most : 1;
        beats = bars * song.bar_beats;
    } else {                                                   // nothing in time yet: the take sets the tempo
        static const int opts[] = { 1, 2, 4, 8 };
        int best = 1; float bd = 1e9f;
        for (int i = 0; i < 4; i++) {
            float bpm = (float)(opts[i] * song.bar_beats) * 60.0f * (float)AUDIO_SR / (float)held;
            float d = bpm < 70.0f ? 70.0f - bpm + 50.0f : bpm > 160.0f ? bpm - 160.0f + 50.0f : (bpm > 110.0f ? bpm - 110.0f : 110.0f - bpm);
            if (d < bd) { bd = d; best = opts[i]; }
        }
        beats = best * song.bar_beats;
        fpb = (held + (uint32_t)beats / 2) / (uint32_t)beats;
        host_set_tempo(fpb, v_take_f);                         // beat 0 where the take begins
    }
    ui_beats = beats;
    return fpb * (uint32_t)beats;
}

void loop_release(bool confirmed)
{
    if (!confirmed) { push(C_TAP, 0); return; }
    uint32_t R = audio_frames();
    if (v_state == LOOPER_ARM) { push(C_RELEASE, 0); toast("NOTHING HEARD: NO LOOP", 1200); return; }
    if (v_state == LOOPER_REC) {
        // how long the take is: from its start (the first sound) to just before the release's click
        int32_t held = (int32_t)(R - CLICK_PRE - v_take_f);
        if (held < MINLEN + XF) { push(C_RELEASE, 0); return; }
        uint32_t L = ctl.sync ? take_length((uint32_t)held) : take_length((uint32_t)held - XF);   // free: ends before the click
        o_ref = song.origin; f_ref = song.origin_frac; o_set = true;
        push2(C_RELEASE, L, R);
        if (ui_beats) host_loop_made(ui_beats);
    } else push2(C_RELEASE, 0, R);
}

void loop_clear(void) { push(C_CLEAR, 0); if (v_beats) host_loop_made(0); }

void loop_tick(void)
{
    // how far beat 0 has moved in this board's frames since the loop's length was set (a few frames a second at
    // most while the link follows another board's crystal; a jump when the session itself moved)
    // (beat 0 earlier in our frames = the beat is later than it was: the loop's place moves on by as much)
    corr_now = ctl.sync && o_set ? (float)(int32_t)(o_ref - song.origin) + (f_ref - song.origin_frac) : 0.0f;
}

int   loop_state(void) { return v_state; }
static float fade_in(uint32_t j) { return j < FADE ? (float)j / (float)FADE : 1.0f; }
float loop_pos(void) { return v_pos; }
float loop_len_s(void) { return (float)(v_state == LOOPER_REC ? (int)(v_fill * (float)MAXLEN) : v_len) / (float)AUDIO_SR; }
int   loop_beats(void) { return v_beats; }
float loop_speed(void) { return v_speed; }
int   loop_layers(void) { return v_layers; }
bool  loop_closing(void) { return v_closing; }

void loop_envelope(uint8_t *cols, int n)
{
    int L = v_state == LOOPER_REC ? wpos : v_len;
    if (L <= 0 || !buf) { memset(cols, 0, (size_t)n); return; }
    int per = L / n; if (per < 1) per = 1;
    for (int c = 0; c < n; c++) {
        int base = c * per, pk = 0;
        for (int k = 0; k < 8; k++) { int i = base + k * per / 8; if (i >= L) break; int v = buf[i]; if (v < 0) v = -v; if (v > pk) pk = v; }
        int e = pk >> 6; cols[c] = (uint8_t)(e > 255 ? 255 : e);
    }
}

// ---------------------------------------------------------------- the audio side
static inline float hermite(float y0, float y1, float y2, float y3, float f)
{
    float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}
static inline float wrapf(float p, float L) { while (p >= L) p -= L; while (p < 0.0f) p += L; return p; }
static inline int imod(int32_t a, int m) { int r = (int)(a % m); return r < 0 ? r + m : r; }
static inline float read_at(float p)
{
    int i = (int)p; float f = p - (float)i;
    int a = i - 1, b = i, c = i + 1, d = i + 2;
    if (a < 0) a += len;
    if (c >= len) c -= len;
    if (d >= len) d -= len;
    return hermite(f32(buf[a]), f32(buf[b]), f32(buf[c]), f32(buf[d]), f);
}

// KEEP PITCH: a grain starts in slot k, centred on where the head will be half a grain from now (a little scatter
// when the head is slow, so a stopped loop smears instead of buzzing at the grain's rate)
static void grain_start(int k, float hd, float s)
{
    int d = s > 0.02f ? 1 : s < -0.02f ? -1 : g_last;
    g_last = d;
    float a = s < 0.0f ? -s : s;
    float spread = a < 1.0f ? 400.0f * (1.0f - a) : 0.0f;
    g_rng = g_rng * 1664525u + 1013904223u;
    float j = spread * ((float)(g_rng >> 8) * (2.0f / 16777216.0f) - 1.0f);
    float p = hd + (s - (float)d) * (float)(GRAIN / 2) + j;
    g_pos[k] = imod((int32_t)(p + (p >= 0.0f ? 0.5f : -0.5f)), len);
    g_dir[k] = d;
}
static void grains_begin(float hd, float s)
{
    g_ph = 0;
    grain_start(0, hd, s);                                         // A starts silent ...
    g_pos[1] = imod((int32_t)(hd + 0.5f), len); g_dir[1] = g_dir[0];   // ... B plays the head as it is
    g_run = true;
}
static inline float grains(float hd, float s)
{
    if (g_ph == 0) grain_start(0, hd, s);
    else if (g_ph == GRAIN / 2) grain_start(1, hd, s);
    float x = (float)g_ph * (1.0f / (float)GRAIN), u = 4.0f * x * (1.0f - x), wa = u * u * (3.0f - 2.0f * u);
    float y = f32(buf[g_pos[0]]) * wa + f32(buf[g_pos[1]]) * (1.0f - wa);
    for (int k = 0; k < 2; k++) { int q = g_pos[k] + g_dir[k]; g_pos[k] = q >= len ? 0 : q < 0 ? len - 1 : q; }
    if (++g_ph >= GRAIN) g_ph = 0;
    return y;
}

// a first take begins: from frame S to frame e (exclusive) out of the ring, faded in
static void begin_take(uint32_t S, uint32_t e)
{
    uint32_t k = e - S; if (k > RING - 1) k = RING - 1;
    for (uint32_t j = 0; j < k; j++) buf[j] = s16(f32(ring[(S + j) & RMASK]) * fade_in(j));
    wpos = (int)k; take_f = S; v_take_f = S; closing = false; duck_until = S; state = LOOPER_REC; layers = 0;
}

// the first sound at or after arm_min among frames [from, to) of the ring: its frame, or to when there is none
static uint32_t find_sound(uint32_t from, uint32_t to)
{
    if ((int32_t)(from - arm_min) < 0) from = arm_min;
    for (uint32_t f = from; (int32_t)(f - to) < 0; f++) {
        if (!ctl.from_sound) return f;
        float x = f32(ring[f & RMASK]); if (x < 0) x = -x;
        if (x > ONSET_THR) return f;
    }
    return to;
}

static void command(const lcmd_t *c, uint32_t f0)
{
    switch (c->cmd) {
    case C_PRESS: pend = true; press_f = c->a; break;
    case C_CONFIRM: {
        if (!pend || !buf) break;
        pend = false;
        if (state == LOOPER_STOP) { state = LOOPER_EMPTY; len = 0; wpos = 0; closing = false; layers = 0; play_g = 0; g_run = false; g_mix = 0; }
        if (state == LOOPER_EMPTY) {                              // the first take: armed, from the first sound past the click
            arm_min = press_f + CLICK_POST;
            state = LOOPER_ARM;
            uint32_t from = f0 - (RING - 1u);
            uint32_t o = find_sound((int32_t)(from - press_f) > 0 ? from : press_f, f0);
            if (o != f0) {
                uint32_t S = ctl.from_sound ? o - ONSET_PRE : o;
                if ((int32_t)(S - arm_min) < 0) S = arm_min;
                begin_take(S, f0);
            }
        } else if (len) {                                          // adding: what was sung since the press (past its click)
            float corr = corr_now;
            uint32_t s0 = press_f + CLICK_POST;
            int32_t k = (int32_t)(f0 - s0);
            if (k > (int32_t)RING - 1) { s0 = f0 - (RING - 1u); k = (int32_t)RING - 1; }
            int base = imod((int32_t)(s0 - take_f), len);
            for (int32_t j = 0; j < k; j++) {
                int at = (int)wrapf((float)((base + (int)j) % len) + corr + 0.5f, (float)len);
                if (at >= len) at -= len;
                buf[at] = s16(f32(buf[at]) * ctl.keep + fade_in((uint32_t)j) * f32(ring[(s0 + (uint32_t)j) & RMASK]));
            }
            state = LOOPER_DUB; dub_t = 1; dub_g = k > 0 ? 1.0f : 0.0f; last_w = -1; layers++;
            duck_until = s0;                                       // nothing added before then
        }
        break;
    }
    case C_RELEASE: {
        uint32_t R = c->b, cut = R - CLICK_PRE;
        if (state == LOOPER_ARM) { state = LOOPER_EMPTY; wpos = 0; break; }
        if (state == LOOPER_REC) {
            if (!c->a) { state = LOOPER_EMPTY; wpos = 0; break; }
            len = (int)c->a; closing = true;                       // on to the end, then the seam
            // the release's click: what was written from just before it is silenced (faded), and so is what comes until
            // it has rung out (a free loop ends before it: its length stops short of the cut)
            int32_t p0 = (int32_t)(cut - take_f);
            if (p0 < 0) p0 = 0;
            for (int32_t p = p0; p < wpos; p++) { float w = 1.0f - fade_in((uint32_t)(p - p0)); buf[p] = s16(f32(buf[p]) * w); }
            duck_until = R + CLICK_POST;
        } else if (state == LOOPER_DUB) {
            // take back what was added from just before the release's click (it is still in the ring), then stop adding
            float corr = corr_now;
            int32_t k = (int32_t)(f0 - cut);
            if (k > 0 && k < (int32_t)RING && (int32_t)(cut - duck_until) >= 0) {
                int base = imod((int32_t)(cut - take_f), len);
                for (int32_t j = 0; j < k; j++) {
                    int at = (int)wrapf((float)((base + (int)j) % len) + corr + 0.5f, (float)len);
                    if (at >= len) at -= len;
                    buf[at] = s16(f32(buf[at]) - fade_in((uint32_t)j) * f32(ring[(cut + (uint32_t)j) & RMASK]));
                }
            }
            dub_t = 0; dub_g = 0; last_w = -1; state = LOOPER_PLAY;
        }
        break;
    }
    case C_TAP:
        pend = false;
        if (state == LOOPER_PLAY || state == LOOPER_DUB) { state = LOOPER_STOP; play_t = 0; dub_t = 0; }
        else if (state == LOOPER_STOP) { state = LOOPER_PLAY; play_t = 1; }
        break;
    case C_CLEAR: state = LOOPER_EMPTY; len = 0; wpos = 0; closing = false; pend = false; layers = 0; play_g = 0; g_run = false; g_mix = 0; break;
    }
}

void loop_run(const float *in, float *out, int n)
{
    uint32_t f0 = audio_frames();
    lcmd_t c;
    while (lcmdq_pop(&q, &c)) command(&c, f0);
    if (!buf) { memset(out, 0, (size_t)n * sizeof(float)); return; }
    for (int i = 0; i < n; i++) ring[(f0 + (uint32_t)i) & RMASK] = s16(in[i]);

    if (state == LOOPER_ARM) {                                     // waiting for a sound (or for the press's click to pass)
        memset(out, 0, (size_t)n * sizeof(float));
        uint32_t o = find_sound(f0, f0 + (uint32_t)n);
        if (o != f0 + (uint32_t)n) {
            uint32_t S = ctl.from_sound ? o - ONSET_PRE : o;
            if ((int32_t)(S - arm_min) < 0) S = arm_min;
            begin_take(S, f0 + (uint32_t)n);
        }
        v_state = state; v_fill = (float)wpos / (float)MAXLEN;
        return;
    }
    if (state == LOOPER_REC) {
        for (int i = 0; i < n; i++) {
            out[i] = 0.0f;
            uint32_t fr = f0 + (uint32_t)i;
            float dk = 1.0f;                                       // silenced while the release's click rings, faded back in
            if (closing) { int32_t d = (int32_t)(fr - duck_until); dk = d < 0 ? 0.0f : fade_in((uint32_t)d); }
            if (wpos < MAXLEN + XF) buf[wpos++] = s16(in[i] * dk);
            if (closing && wpos >= len + XF) {                     // the end and the seam: the tail blended into the start
                for (int j = 0; j < XF; j++) { float r = (float)j / (float)XF; buf[j] = s16(f32(buf[j]) * r + f32(buf[len + j]) * (1.0f - r)); }
                state = LOOPER_PLAY; closing = false; layers = 1; play_g = 1.0f; play_t = 1.0f;   // the tail took the seam's fade
                free_head = false; xf_left = 0; sp = 1.0f; corr_was = corr_now;
                head = wrapf((float)imod((int32_t)(f0 + (uint32_t)i + 1u - take_f), len) + corr_now, (float)len);
            }
        }
        v_fill = (float)wpos / (float)MAXLEN;
    } else if (state == LOOPER_EMPTY || !len) {
        memset(out, 0, (size_t)n * sizeof(float));
    } else {
        const float L = (float)len, level = ctl.level, keep = ctl.keep;
        float corr = corr_now;
        if (corr - corr_was > 64.0f || corr - corr_was < -64.0f) { xf_from = head; xf_left = JUMP; }   // the clock jumped: crossfade
        corr_was = corr;
        float want = ctl.speed;
        bool tilted = want > 1.02f || want < 0.98f;
        int base = imod((int32_t)(f0 - take_f), len);                  // frames since the take began, in the loop
        for (int i = 0; i < n; i++) {
            int bi = base + i; if (bi >= len) bi -= len;
            float beat_pos = wrapf((float)bi + corr, L);                 // where the beat is in the loop
            sp += SLEW * (want - sp);
            if (tilted) free_head = true;
            if (free_head) {
                head = wrapf(head + sp, L);
                if (!tilted && sp > 0.98f && sp < 1.02f) {             // back near 1: onto the beat again
                    float e = beat_pos - head;
                    if (e > L * 0.5f) e -= L;
                    if (e < -L * 0.5f) e += L;
                    if (e > -0.5f && e < 0.5f) { free_head = false; head = beat_pos; }
                    else if (e > 1600.0f || e < -1600.0f) { xf_from = head; xf_left = JUMP; free_head = false; head = beat_pos; }
                    else head = wrapf(head + clampf(e * 0.0005f, -0.03f, 0.03f), L);   // a nudge, under a quarter tone
                }
            } else head = beat_pos;
            float y = 0.0f;
            float gt = ctl.keep_pitch && free_head ? 1.0f : 0.0f;  // KEEP PITCH: grains while the head is its own
            if (gt > 0.0f && !g_run) grains_begin(head, sp);
            if (g_mix < 1.0f) {
                y = read_at(head);
                if (xf_left > 0) {                                     // from the old place to the new
                    xf_from = wrapf(xf_from + sp, L);
                    float w = (float)xf_left / (float)JUMP;
                    y = y * (1.0f - w) + read_at(xf_from) * w;
                    xf_left--;
                }
            }
            if (g_run) {
                float gy = grains(head, sp);
                y += (gy - y) * g_mix;
                g_mix += gt > g_mix ? GMIX : -GMIX;
                if (g_mix >= 1.0f) g_mix = 1.0f;
                if (g_mix <= 0.0f) { g_mix = 0.0f; if (gt == 0.0f) g_run = false; }
            }
            play_g += (play_t - play_g) * (1.0f / (float)XF) * 4.0f;
            if (play_g < 1e-4f && play_t == 0.0f) play_g = 0.0f;
            out[i] = y * play_g * level;
            // adding: at the beat's place (rounded), every index once (not before the press's click has passed)
            if ((dub_g > 0.0f || dub_t > 0.0f) && (int32_t)(f0 + (uint32_t)i - duck_until) >= 0) {
                int w = (int)(beat_pos + 0.5f); if (w >= len) w -= len;
                float x = in[i] * dub_g;
                if (last_w >= 0 && w != last_w && w != (last_w + 1) % len) {   // the clock slewed past an index: fill it
                    int m = (last_w + 1) % len;
                    buf[m] = s16(f32(buf[m]) * keep + x);
                }
                if (w != last_w) buf[w] = s16(f32(buf[w]) * keep + x);
                last_w = w;
                if (dub_g < dub_t) { dub_g += 1.0f / (float)XF; if (dub_g > dub_t) dub_g = dub_t; }
                else if (dub_g > dub_t) { dub_g -= 1.0f / (float)XF; if (dub_g <= 0.0f) { dub_g = 0.0f; if (state == LOOPER_DUB) state = LOOPER_PLAY; last_w = -1; } }
            }
        }
        v_pos = head / L;
    }
    v_state = state; v_layers = layers; v_len = len; v_speed = sp; v_closing = closing;
    v_beats = ctl.sync && len && song.fpb ? (int)((uint32_t)len / song.fpb) : 0;
}

// ---------------------------------------------------------------- the stutter
static bool  st_on;
static int   st_fill, st_pos;
static float st_mix;
void loop_stutter(float *io, int n)
{
    if (!stt) return;
    float a = stt_amt;
    if (a > 0.02f && !st_on) { st_on = true; st_fill = 0; st_pos = 0; }
    if (!st_on) return;
    // the slice: a beat, a half, a quarter, an eighth of it as the box leans further
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int div = a < 0.3f ? 1 : a < 0.55f ? 2 : a < 0.8f ? 4 : 8;
    int want = (int)(fpb / (uint32_t)div);
    if (want > STT_MAX) want = STT_MAX;
    if (want < STT_XF * 3) want = STT_XF * 3;
    float tgt = a > 0.02f ? 1.0f : 0.0f;
    for (int i = 0; i < n; i++) {
        float x = io[i];
        if (st_fill < want && st_fill < STT_MAX) stt[st_fill++] = s16(x);   // keep grabbing until the slice is whole
        int L = want < st_fill ? want : st_fill;
        float y = x;
        if (L > STT_XF * 2) {
            if (st_pos >= L) st_pos = 0;
            y = f32(stt[st_pos]);
            if (st_pos < STT_XF) { float w = (float)st_pos / (float)STT_XF; y = y * w + f32(stt[L - STT_XF + st_pos]) * (1.0f - w); }
            st_pos++;
        }
        st_mix += 0.003f * (tgt - st_mix);
        io[i] = x + (y - x) * st_mix;
    }
    if (tgt == 0.0f && st_mix < 0.001f) st_on = false;
}
