// AIR's sound engine. Audio task only, except where noted. The drums are KNOCK's voices; a pad set to SAMPLE
// plays its own buffer at the tune's rate. The loop replays hits with GRID, STRENGTH and SWING applied at
// playback (never destructive). The bus: DJ filter, drive, punch (a compressor), tape echo, room, then the
// stutter (beat repeat) the stick asks for.
#include "inst_air.h"
#include "inst_kit.h"
#include "dsp.h"
#include <string.h>

// ---------------------------------------------------------------- tables
const char *const vt_label[NVTYPE] = { "SAMPLE", "KICK", "SNARE", "HAT", "OPEN", "TOM", "CLAP", "RIM", "BELL", "SHAKE", "CONGA", "ZAP", "PART 1", "PART 2", "PART 3", "PART 4", "OFF" };
const int vt_gm[NVTYPE] = { 0, 36, 38, 42, 46, 45, 39, 37, 56, 70, 63, 81, 0, 0, 0, 0, 0 };
const int pad_gm[NPAD] = { 42, 36, 38, 46 };
// the tuned drums: each pad a degree, so toms and cowbells on different pads make a phrase
static const int8_t pad_deg[NPAD] = { 0, -5, 7, 3 };

// ---------------------------------------------------------------- shared state
hit_t eng_hits[MAXHITS];
volatile int      eng_nhits;
volatile uint32_t eng_loop_len, eng_loop_start;
volatile int      eng_loop_run = 1, eng_beats_fixed;
volatile float eng_tune, eng_key, eng_mutate, eng_filter, eng_roll_hz, eng_stutter, eng_part_rate = 1.0f;
volatile int   eng_part_bar = -1;
volatile int   eng_roll_pad, eng_roll_vel = 90;
volatile float eng_out_lvl, eng_in_env;
int16_t *eng_pad[NPAD];
volatile int   eng_pad_len[NPAD];
volatile float eng_pad_gain[NPAD] = { 1, 1, 1, 1 };
volatile int   eng_cap_state = CAP_IDLE, eng_cap_pad = -1, eng_cap_len, eng_cap_hold;

SPSC(trigq, trig_t, 32)
SPSC(playq, played_t, 64)
static trigq_t  trigs;
static playq_t  played;
#define NPEND 24
static trig_t pend[NPEND]; static int npend;

void eng_trigger(const trig_t *t) { trigq_push(&trigs, t); }
bool eng_played_pop(played_t *p) { return playq_pop(&played, p); }

static uint32_t seed = 0x9e3779b9u;
static uint32_t frame0;                     // audio_frames() at this block

// ---------------------------------------------------------------- voices
#define NV 12
typedef struct {
    uint8_t type, pad;
    bool on;
    int delay, t;
    float amp, ph, ph2, f0, f1;
    float ae, kae, pe, kpe, xe;              // amp env, pitch env, an extra env
    float carry;                             // the stolen voice's last sample, decaying away
    float last;
    float pos, rate;                         // SAMPLE and PART: where in the buffer, and how fast
    const int16_t *buf; int end;             // PART: the break's pcm and where the part ends; serial: whose break
    uint32_t serial;
    svf_t fa;
} voice_t;
static voice_t vo[NV];

int eng_voices_on(void) { int n = 0; for (int i = 0; i < NV; i++) n += vo[i].on; return n; }

// the break a part voice still reads: KIT does not write that buffer until the voice is done
uint32_t beat_reading(void)
{
    uint32_t curs = kit_break()->serial;
    for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].type >= V_PART1 && vo[i].type <= V_PART4 && vo[i].serial && vo[i].serial != curs) return vo[i].serial;
    return curs;
}

// where part k of the break starts and ends: the whole break in four, or one bar's four beats (HANDS)
static void part_span(const brk_t *b, int k, int *start, int *end)
{
    int bar = eng_part_bar, len = b->len;
    if (bar >= 0 && b->steps >= 32) { int bl = len / (b->steps / 16); int b0 = bar * bl; if (b0 + bl > len) b0 = 0; *start = b0 + k * bl / 4; *end = b0 + (k + 1) * bl / 4; }
    else { *start = k * len / 4; *end = (k + 1) * len / 4; }
}

int eng_part_kind(int part)
{
    const brk_t *b = kit_break();
    if (!b || b->len <= 64 || b->nslices == 0) return -1;
    int s0, s1; part_span(b, part, &s0, &s1);
    int best = -1; int32_t bd = 0x7fffffff;
    for (int j = 0; j < b->nslices; j++) { int32_t d = b->slice[j] - s0; if (d < 0) d = -d; if (d < bd && b->slice[j] < s1) { bd = d; best = b->kind[j]; } }
    return best;
}

static void voice_start(int type, int pad, float amp, int delay)
{
    // a free voice, else the quietest; a voice in use hands its last sample over as a fading carry
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NV; i++) {
        if (!vo[i].on) { k = i; break; }
        float l = vo[i].type == V_SAMPLE || (vo[i].type >= V_PART1 && vo[i].type <= V_PART4) ? 0.5f * vo[i].amp : vo[i].ae * vo[i].amp;
        if (l < q) { q = l; k = i; }
    }
    voice_t *v = &vo[k];
    float carry = v->on ? v->last + v->carry : 0.0f;
    if (type == V_HAT) for (int i = 0; i < NV; i++) if (vo[i].on && vo[i].type == V_OHAT) vo[i].kae = 0.992f;   // closed hat chokes the open one
    memset(v, 0, sizeof *v);
    v->carry = carry;
    v->type = (uint8_t)type; v->pad = (uint8_t)pad; v->on = true; v->delay = delay; v->amp = amp; v->ae = 1.0f; v->pe = 1.0f; v->xe = 1.0f;
    bool tuned = type == V_TOM || type == V_CONGA || type == V_ZAP || type == V_COWBELL;
    float tn = exp2f((eng_tune + (tuned ? eng_key + (float)pad_deg[pad] : 0.0f)) / 12.0f);
    float dc = pv[P_DECAY], pk = pv[P_PUNCH] * 0.01f;
    switch (type) {
    case V_SAMPLE:  v->rate = exp2f(eng_tune / 12.0f); v->pos = 0; break;
    case V_PART1: case V_PART2: case V_PART3: case V_PART4: {
        const brk_t *b = kit_break();
        int s0, s1; part_span(b, type - V_PART1, &s0, &s1);
        v->buf = b->pcm; v->pos = (float)s0; v->end = s1; v->serial = b->serial;
        v->rate = eng_part_rate * exp2f(eng_tune / 12.0f);
        if (b->len <= 64) v->on = false;
        break;
    }
    case V_KICK:    v->f0 = 46.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.30f * dc); v->f1 = 2.0f + 3.0f * pk; break;
    case V_SNARE:   v->f0 = 185.0f * tn; svf_set(&v->fa, 1600.0f * tn, 0.7f); v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.15f * dc); break;
    case V_HAT:     svf_set(&v->fa, 7200.0f * tn, 0.8f); v->kae = 1.0f - onepole_s(0.04f * dc); break;
    case V_OHAT:    svf_set(&v->fa, 6500.0f * tn, 0.9f); v->kae = 1.0f - onepole_s(0.32f * dc); break;
    case V_TOM:     v->f0 = 105.0f * tn; v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.38f * dc); break;
    case V_CLAP:    svf_set(&v->fa, 1100.0f * tn, 1.5f); v->kpe = 1.0f - onepole_s(0.004f); v->kae = 1.0f - onepole_s(0.14f * dc); break;
    case V_RIM:     v->f0 = 520.0f * tn; svf_set(&v->fa, 1800.0f * tn, 2.0f); v->kae = 1.0f - onepole_s(0.028f * dc); v->kpe = 1.0f - onepole_s(0.006f); break;
    case V_COWBELL: v->f0 = 540.0f * tn; v->f1 = 800.0f * tn; svf_set(&v->fa, 2400.0f * tn, 1.2f); v->kpe = 1.0f - onepole_s(0.02f); v->kae = 1.0f - onepole_s(0.22f * dc); break;
    case V_SHAKER:  svf_set(&v->fa, 5500.0f * tn, 1.2f); v->kae = 1.0f - onepole_s(0.07f * dc); v->xe = 0.0f; break;
    case V_CONGA:   v->f0 = 330.0f * tn; v->kpe = 1.0f - onepole_s(0.008f); v->kae = 1.0f - onepole_s(0.2f * dc); break;
    case V_ZAP:     v->f0 = 85.0f * tn; v->f1 = 2600.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.17f * dc); break;
    default: v->on = false; break;
    }
}

static inline float voice_run(voice_t *v)
{
    if (v->delay > 0) { v->delay--; float c = v->carry; v->carry *= 0.93f; return c; }
    float y = 0, pk = pv[P_PUNCH] * 0.01f;
    const float isr = 1.0f / (float)AUDIO_SR;
    switch (v->type) {
    case V_SAMPLE: {                                 // the pad's buffer, linear interpolation, a short fade at the end
        int len = eng_pad_len[v->pad];
        const int16_t *b = eng_pad[v->pad];
        int i = (int)v->pos;
        if (!b || i + 1 >= len) { v->on = false; break; }
        float fr = v->pos - (float)i;
        float s0 = (float)b[i], s1 = (float)b[i + 1];
        float w = len - i < 64 ? (float)(len - i) / 64.0f : 1.0f;
        y = (s0 + (s1 - s0) * fr) * (1.0f / 32768.0f) * w * eng_pad_gain[v->pad];
        v->pos += v->rate;
        break;
    }
    case V_PART1: case V_PART2: case V_PART3: case V_PART4: {     // a part of the printed break, faded in and out
        int i = (int)v->pos;
        if (!v->buf || i + 1 >= v->end) { v->on = false; break; }
        float fr = v->pos - (float)i;
        float s0 = (float)v->buf[i], s1 = (float)v->buf[i + 1];
        float w = v->end - i < 64 ? (float)(v->end - i) / 64.0f : v->t < 32 ? (float)v->t / 32.0f : 1.0f;
        y = (s0 + (s1 - s0) * fr) * (1.0f / 32768.0f) * w * 1.6f;
        v->pos += v->rate;
        break;
    }
    case V_KICK:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + v->f1 * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->t < 48) y += (0.2f + 0.4f * pk) * rng_bi(&seed) * (1.0f - (float)v->t / 48.0f);
        break;
    case V_SNARE:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + 0.4f * v->pe) * isr);
        y = 0.45f * sin_turns(v->ph) * v->pe + 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case V_HAT: case V_OHAT:
        y = 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case V_TOM: case V_CONGA:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + (v->type == V_TOM ? 0.45f : 0.25f) * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->type == V_CONGA && v->t < 40) y += 0.25f * rng_bi(&seed);
        break;
    case V_CLAP: {                                   // three claps 11 ms apart, then the tail
        if (v->t == 352 || v->t == 704) v->pe = 1.0f;
        float e = v->t > 704 && v->ae > v->pe ? v->ae : v->pe;
        y = 1.5f * svf_bp(&v->fa, rng_bi(&seed), NULL) * e;
        break;
    }
    case V_RIM:
        v->ph = wrap1(v->ph + v->f0 * isr);
        y = 0.6f * sin_turns(v->ph) * v->ae + 1.2f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->pe;
        break;
    case V_COWBELL: {
        float d0 = v->f0 * isr, d1 = v->f1 * isr;
        v->ph = wrap1(v->ph + d0); v->ph2 = wrap1(v->ph2 + d1);
        float s = 0.5f * (square_blep(v->ph, d0) + square_blep(v->ph2, d1));
        y = 0.8f * svf_bp(&v->fa, s, NULL) * (0.4f * v->pe + 0.6f) * v->ae;
        break;
    }
    case V_SHAKER:
        v->xe += (1.0f - v->xe) * 0.03f;             // a 1 ms swell: grains, not a click
        y = 1.1f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->ae * v->xe;
        break;
    case V_ZAP:
        v->ph = wrap1(v->ph + (v->f0 + v->f1 * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        break;
    default: v->on = false; break;
    }
    bool held = v->type == V_SAMPLE || (v->type >= V_PART1 && v->type <= V_PART4);
    if (!held) { v->ae *= v->kae; v->pe *= v->kpe; }
    v->t++;
    y *= v->amp;
    v->last = y;
    float c = v->carry; v->carry = flush_denormal(v->carry * 0.93f);
    if (!held && v->ae < 1e-4f && v->t > 64) { v->on = false; return y + c; }
    return y + c;
}

// ---------------------------------------------------------------- the bus
static svf_t f_lp, f_hp;
static float flt_s;
static smooth_t drive_s, vlev_s, room_s, echo_s, cap_mute_s;
static float comp_env, comp_g = 1.0f, comp_s;
#define ECHO_N 32768
static float *echo_buf; static uint32_t echo_w;
static float echo_lp, echo_t_s = 0.25f, wow_ph;
#define CN 4
#define CLEN 1024
static float comb[CN][CLEN], comb_lp[CN];
static uint32_t comb_w;
static const float comb_len[CN] = { 810.0f, 862.0f, 927.0f, 984.0f };
#define AP_LEN 512
static float ap[2][AP_LEN]; static uint32_t ap_w;
static const int ap_len[2] = { 403, 320 };
static float rsize_s = 0.5f;
#define STUT_N 32768
static float *stut_buf; static uint32_t stut_w;
static bool  stut_on; static uint32_t stut_start; static int stut_len, stut_pos; static float stut_x;
static float roll_ph; static bool roll_was;
// crush: a sample held at a lower rate and fewer bits; grain: little pieces of the last half second, overlapping
static float crush_hold, crush_acc; static smooth_t crush_s;
#define NGRAIN 4
typedef struct { int len, pos; uint32_t start; float rate; bool on; } grain_t;
static grain_t gr[NGRAIN]; static smooth_t grain_s; static int grain_wait;

// ---------------------------------------------------------------- capture
#define HIST 256                                     // the mic's last 8 ms, for the take's pre-roll
static float hist[HIST]; static uint32_t hist_w;
static float env_f, env_s, floor_e = 0.01f, a_f, r_f, a_s, r_s;
static int   cap_max, cap_quiet; static float cap_peak;
static volatile int cap_want, cap_want_pad, cap_want_max;     // UI -> audio (a plain handshake: want is read per block)
static int cap_serial;

void eng_capture(int pad, float max_s)
{
    if (pad < 0 || pad >= NPAD) return;
    eng_pad_len[pad] = 0;                            // dropped now: a voice on it stops
    cap_want_pad = pad; cap_want_max = (int)(max_s * (float)AUDIO_SR);
    if (cap_want_max > PAD_MAX) cap_want_max = PAD_MAX;
    if (cap_want_max < AUDIO_SR / 10) cap_want_max = AUDIO_SR / 10;
    __atomic_store_n(&cap_want, 1, __ATOMIC_RELEASE);
}
void eng_capture_stop(void)   { __atomic_store_n(&cap_want, 2, __ATOMIC_RELEASE); }
void eng_capture_cancel(void) { __atomic_store_n(&cap_want, 3, __ATOMIC_RELEASE); }

static void cap_finish(void)
{
    int n = eng_cap_len;
    int16_t *b = eng_pad[eng_cap_pad];
    if (b) for (int i = 0; i < 64 && i < n; i++) b[n - 1 - i] = (int16_t)((float)b[n - 1 - i] * (float)i / 64.0f);   // a 2 ms fade out
    eng_cap_state = n >= AUDIO_SR / 20 ? CAP_DONE : CAP_IDLE;
    if (eng_cap_state == CAP_IDLE) eng_cap_pad = -1;
}

static void cap_block(const float *in, int n)
{
    int w = __atomic_exchange_n(&cap_want, 0, __ATOMIC_ACQ_REL);
    if (w == 1) {
        eng_cap_pad = cap_want_pad; cap_max = cap_want_max; eng_cap_len = 0; cap_peak = 0; cap_quiet = 0;
        eng_cap_state = eng_pad[eng_cap_pad] ? CAP_ARMED : CAP_IDLE;
        cap_serial++;
    } else if (w == 2) {
        if (eng_cap_state == CAP_REC) cap_finish();
        else if (eng_cap_state == CAP_ARMED) { eng_cap_state = CAP_IDLE; eng_cap_pad = -1; }
    } else if (w == 3) {
        if (eng_cap_state != CAP_IDLE) { eng_cap_state = CAP_IDLE; eng_cap_pad = -1; }
    }
    if (eng_cap_state != CAP_ARMED && eng_cap_state != CAP_REC) return;
    int16_t *b = eng_pad[eng_cap_pad];
    for (int i = 0; i < n; i++) {
        float x = in[i];
        if (eng_cap_state == CAP_ARMED) {
            // a sound: the fast envelope over the room's floor, well over the slow one
            if (env_f > 0.012f + 4.0f * floor_e && env_f > 2.5f * env_s) {
                for (int k = 0; k < PAD_PRE; k++) { float h = hist[(hist_w - PAD_PRE + k) & (HIST - 1)]; b[k] = (int16_t)clampf(h * 32767.0f, -32767.0f, 32767.0f); }
                eng_cap_len = PAD_PRE; eng_cap_state = CAP_REC; cap_peak = env_f; cap_quiet = 0;
            }
        }
        if (eng_cap_state == CAP_REC) {
            int len = eng_cap_len;
            b[len] = (int16_t)clampf(x * 32767.0f, -32767.0f, 32767.0f);
            eng_cap_len = ++len;
            if (env_f > cap_peak) cap_peak = env_f;
            // over when the sound has died (40 dB under its peak, or into the room's floor, for 150 ms), or at the limit
            if (env_f < fmaxf(cap_peak * 0.01f, 0.004f + 2.0f * floor_e)) cap_quiet++; else cap_quiet = 0;
            if (len >= cap_max || (!eng_cap_hold && cap_quiet > AUDIO_SR * 15 / 100 && len > AUDIO_SR / 10)) { cap_finish(); return; }
        }
    }
}

void eng_init(void)
{
    for (int p = 0; p < NPAD; p++) eng_pad[p] = app_alloc((size_t)PAD_MAX * sizeof(int16_t));
    echo_buf = app_alloc(ECHO_N * sizeof(float));
    stut_buf = app_alloc(STUT_N * sizeof(float));
    a_f = onepole_s(0.0005f); r_f = onepole_s(0.015f); a_s = onepole_s(0.030f); r_s = onepole_s(0.200f);
    smooth_init(&drive_s, 0, 0.02f); smooth_init(&vlev_s, 0.8f, 0.02f);
    smooth_init(&room_s, 0, 0.03f); smooth_init(&echo_s, 0, 0.03f); smooth_init(&cap_mute_s, 1.0f, 0.01f);
    smooth_init(&crush_s, 0, 0.02f); smooth_init(&grain_s, 0, 0.03f);
    if (!eng_pad[0] || !eng_pad[NPAD - 1] || !echo_buf || !stut_buf) app_log("air: short of memory (pads %p, echo %p, stutter %p)", (void *)eng_pad[0], (void *)echo_buf, (void *)stut_buf);
}

uint32_t eng_pass(uint32_t frame) { uint32_t l = eng_loop_len; return l ? (frame - eng_loop_start) / l : 0; }

int eng_beats(void)
{
    if (eng_beats_fixed > 0) return eng_beats_fixed;
    int b = (int)pv[P_BEATS];
    uint32_t len = eng_loop_len;
    if (b > 0 || !len) return b > 0 ? b : 4;
    static const int pref[] = { 4, 2, 8, 3, 6, 1, 12, 16 };
    float sec = (float)len / (float)AUDIO_SR, best = 1e9f; int bb = 4;
    for (int i = 0; i < 8; i++) {
        float bpm = (float)pref[i] * 60.0f / sec;
        if (bpm >= 75.0f && bpm <= 165.0f) return pref[i];
        float d = fabsf(bpm - 110.0f);
        if (d < best) { best = d; bb = pref[i]; }
    }
    return bb;
}
float eng_bpm(void) { uint32_t len = eng_loop_len; return len ? (float)eng_beats() * 60.0f * (float)AUDIO_SR / (float)len : 0.0f; }

uint32_t eng_qoff(const hit_t *h)
{
    static const int div[] = { 0, 1, 2, 3, 4, 6, 8 };       // GRID: OFF, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32
    uint32_t len = eng_loop_len;
    int d = div[(int)pv[P_GRID]];
    if (!len || !d) return h->off;
    int steps = eng_beats() * d;
    float g = (float)len / (float)steps, off = (float)h->off;
    float k = (float)(int)(off / g + 0.5f);
    float target = k * g;
    if (((int)k & 1) && d >= 2) target += pv[P_SWING] * 0.01f * g * 0.5f;
    float q = off + (target - off) * pv[P_SNAP] * 0.01f;
    while (q >= (float)len) q -= (float)len;
    if (q < 0) q += (float)len;
    return (uint32_t)q;
}

static void trigger(int pad, int vel, int src, int delay)
{
    if (pad < 0 || pad >= NPAD) return;
    float a = (float)vel / 127.0f;
    if (src == SRC_LOOP) a *= pv[P_LLEVEL] * 0.01f;
    int vt = (int)pv[P_FRONT + pad];
    if (vt == V_SAMPLE) { if (eng_pad_len[pad] > 0 && eng_cap_state == CAP_IDLE) voice_start(V_SAMPLE, pad, 0.25f + 0.75f * a * a, delay); }
    else if (vt >= V_PART1 && vt <= V_PART4) voice_start(vt, pad, 0.25f + 0.75f * a * a, delay);
    else if (vt != V_OFF) voice_start(vt, pad, a * a * 0.42f + 0.02f * a, delay);
    played_t p = { frame0 + (uint32_t)delay, (uint8_t)pad, (uint8_t)vel, (uint8_t)src };
    playq_push(&played, &p);
}

static void loop_block(int n)
{
    uint32_t len = eng_loop_len;
    if (!len || !eng_loop_run) return;
    uint32_t rel = frame0 - eng_loop_start, ph = rel % len;
    int nh = eng_nhits;
    float mu = eng_mutate;
    for (int i = 0; i < nh; i++) {
        hit_t *h = &eng_hits[i];
        if (h->dead) continue;
        uint32_t q = eng_qoff(h), d = (q + len - ph) % len;
        if (d >= (uint32_t)n) continue;
        uint32_t pass = (rel + d) / len;
        if ((uint16_t)pass == h->born) continue;                       // it was played in this pass: it already sounded
        int pad = h->pad, vel = h->vel;
        if (mu > 0.001f) {
            float r = rng_uni(&seed);
            if (r < mu * 0.35f) continue;                     // dropped
            if (r < mu * 0.7f) pad = (int)(rng_next(&seed) % NPAD);
            vel = clampi((int)((float)vel * (1.0f + mu * 0.5f * rng_bi(&seed))), 10, 127);
        }
        trigger(pad, vel, SRC_LOOP, (int)d);
    }
}

static void roll_block(int n)
{
    float hz = eng_roll_hz;
    if (hz <= 0.0f) { roll_was = false; return; }
    if (!roll_was) { roll_ph = 1.0f; roll_was = true; }
    float step = hz / (float)AUDIO_SR;
    for (int i = 0; i < n; i++) {
        if (roll_ph >= 1.0f) { roll_ph -= 1.0f; trigger(eng_roll_pad, eng_roll_vel, SRC_ROLL, i); }
        roll_ph += step;
    }
}

void eng_audio(const float *in, float *out, int n)
{
    frame0 = audio_frames();
    // the DJ filter, per block
    float fl = clampf(eng_filter, -1.0f, 1.0f);
    flt_s += 0.2f * (fl - flt_s);
    svf_set(&f_lp, 18000.0f * exp2f(fminf(flt_s, 0.0f) * 7.0f), 0.9f);
    svf_set(&f_hp, 18.0f * exp2f(fmaxf(flt_s, 0.0f) * 9.0f), 0.9f);
    comp_s += 0.12f * (pv[P_COMP] * 0.01f - comp_s);
    float comp_amt = comp_s;
    float etime = pv[P_ETIME], efb = pv[P_EFB] * 0.01f;
    float rsz = 0.45f + 0.55f * pv[P_RSIZE] * 0.01f, rfb = 0.7f + 0.25f * pv[P_RSIZE] * 0.01f;

    // the mic: envelopes (for the capture) and the ring the pre-roll comes from
    for (int i = 0; i < n; i++) {
        float x = in[i];
        hist[hist_w++ & (HIST - 1)] = x;
        follow(&env_f, x, a_f, r_f);
        follow(&env_s, x, a_s, r_s);
        if (env_s < floor_e) floor_e += 0.01f * (env_s - floor_e); else floor_e += 0.000005f * (env_s - floor_e);
    }
    eng_in_env = env_f;
    cap_block(in, n);
    bool listening = eng_cap_state == CAP_ARMED || eng_cap_state == CAP_REC;

    // what the UI asked for: now, or on the grid line it named
    trig_t t;
    while (trigq_pop(&trigs, &t)) {
        if (!t.at || (int32_t)(t.at - frame0) <= 0) trigger(t.pad, t.vel, t.src, 0);
        else if (npend < NPEND) pend[npend++] = t;
    }
    for (int i = 0; i < npend; ) {
        int32_t d = (int32_t)(pend[i].at - frame0);
        if (d < n) { trigger(pend[i].pad, pend[i].vel, pend[i].src, d < 0 ? 0 : d); pend[i] = pend[--npend]; }
        else i++;
    }
    loop_block(n);
    roll_block(n);

    float pk = 0;
    float gscale = 1.0f, gstep = 0.0f;
    {                                                         // the compressor's gain, worked out per block
        float thr = 0.25f, ratio = 1.0f + 5.0f * comp_amt;
        float want = comp_env > thr ? powf(thr / comp_env, 1.0f - 1.0f / ratio) : 1.0f;
        gscale = comp_g; gstep = (want - comp_g) / (float)n; comp_g = want;
    }
    float makeup = 1.0f + 0.9f * comp_amt;
    for (int i = 0; i < n; i++) {
        // ---- voices
        float y = 0;
        for (int v = 0; v < NV; v++) if (vo[v].on) y += voice_run(&vo[v]);
        // ---- the bus
        y *= smooth_to(&vlev_s, pv[P_LEVEL] * 0.01f);
        y = svf_hp(&f_hp, svf_lp(&f_lp, y));
        float dr = smooth_to(&drive_s, pv[P_DRIVE] * 0.01f);
        if (dr > 0.002f) { float k = 1.0f + 7.0f * dr; y = (1.0f - dr) * y + dr * soft_tanh(y * k) * (1.0f + 2.0f * dr) / k; }
        float cr = smooth_to(&crush_s, pv[P_CRUSH] * 0.01f);
        if (cr > 0.002f) {                                                   // the rate falls to 2 kHz, the bits to 4
            crush_acc += 1.0f;
            float step = 1.0f + 15.0f * cr * cr;
            if (crush_acc >= step) { crush_acc -= step; float q = exp2f(15.0f - 11.0f * cr); crush_hold = (float)(int)(y * q + (y >= 0 ? 0.5f : -0.5f)) / q; }   // down to 4 bits
            y = y * (1.0f - cr) + crush_hold * cr;
        }
        float a = fabsf(y);
        comp_env += (a > comp_env ? 0.006f : 0.0004f) * (a - comp_env);
        y *= gscale * makeup; gscale += gstep;
        // tape echo: a send, repeats through a darkening, saturating loop, a little wow
        float es = smooth_to(&echo_s, pv[P_ECHO] * 0.01f);
        if (echo_buf) {
            echo_t_s += 0.0004f * (etime - echo_t_s);
            wow_ph = wrap1(wow_ph + 0.6f / (float)AUDIO_SR);
            float dly = echo_t_s * (float)AUDIO_SR + 6.0f * sin_turns(wow_ph);
            if (dly > (float)ECHO_N - 4.0f) dly = (float)ECHO_N - 4.0f;
            int di = (int)dly; float fr = dly - (float)di;
            float e0 = echo_buf[(echo_w - (uint32_t)di) & (ECHO_N - 1)], e1 = echo_buf[(echo_w - (uint32_t)di - 1) & (ECHO_N - 1)];
            float ret = e0 + (e1 - e0) * fr;
            echo_lp += 0.45f * (ret - echo_lp);
            echo_buf[echo_w & (ECHO_N - 1)] = flush_denormal(soft_tanh(y * es + echo_lp * efb));
            echo_w++;
            y += echo_lp * 0.9f;
        }
        // room: four damped combs, two all-passes
        float rm = smooth_to(&room_s, pv[P_ROOM] * 0.01f);
        rsize_s += 0.0005f * (rsz - rsize_s);
        float rin = y * 0.25f, rout = 0;
        for (int c = 0; c < CN; c++) {
            float dl = comb_len[c] * rsize_s;
            int di = (int)dl; float fr = dl - (float)di;
            float c0 = comb[c][(comb_w - (uint32_t)di) & (CLEN - 1)], c1 = comb[c][(comb_w - (uint32_t)di - 1) & (CLEN - 1)];
            float o = c0 + (c1 - c0) * fr;
            comb_lp[c] += 0.4f * (o - comb_lp[c]);
            comb[c][comb_w & (CLEN - 1)] = flush_denormal(rin + comb_lp[c] * rfb);
            rout += o;
        }
        comb_w++;
        for (int k = 0; k < 2; k++) {
            float d = ap[k][(ap_w - (uint32_t)ap_len[k]) & (AP_LEN - 1)];
            float v = rout + 0.5f * d;
            ap[k][ap_w & (AP_LEN - 1)] = flush_denormal(v);
            rout = d - 0.5f * v;
        }
        ap_w++;
        y += rm * 0.35f * rout;
        // grain: four grains at a time from the last half second of the bus, each a little piece at its own speed
        float ga = smooth_to(&grain_s, pv[P_GRAIN] * 0.01f);
        if (stut_buf && (ga > 0.002f || gr[0].on || gr[1].on || gr[2].on || gr[3].on)) {
            float gy = 0;
            for (int g = 0; g < NGRAIN; g++) {
                grain_t *q = &gr[g];
                if (!q->on) continue;
                float ph = (float)q->pos / (float)q->len, w = 0.5f - 0.5f * sin_turns(ph + 0.25f);   // a raised cosine
                float rp = (float)q->start + (float)q->pos * q->rate;
                int ri = (int)rp; float rf = rp - (float)ri;
                float r0 = stut_buf[(uint32_t)ri & (STUT_N - 1)], r1 = stut_buf[((uint32_t)ri + 1) & (STUT_N - 1)];
                gy += (r0 + (r1 - r0) * rf) * w;
                if (++q->pos >= q->len) q->on = false;
            }
            if (ga > 0.002f && --grain_wait <= 0) {                      // the next grain: somewhere in the last half second
                for (int g = 0; g < NGRAIN; g++) if (!gr[g].on) {
                    grain_t *q = &gr[g];
                    q->len = (int)(pv[P_GSIZE] * 0.001f * (float)AUDIO_SR * (0.7f + 0.6f * rng_uni(&seed)));
                    if (q->len < 128) q->len = 128;
                    uint32_t back = 256u + (uint32_t)(rng_uni(&seed) * (float)(AUDIO_SR / 2));
                    q->start = stut_w - back - (uint32_t)q->len;
                    float r = rng_uni(&seed);
                    q->rate = r < 0.15f * ga ? 0.5f : r < 0.3f * ga ? 2.0f : r < 0.45f * ga ? 1.5f : 1.0f;
                    q->pos = 0; q->on = true;
                    break;
                }
                grain_wait = (int)((float)pv[P_GSIZE] * 0.001f * (float)AUDIO_SR * 0.35f);
            }
            y = y * (1.0f - 0.6f * ga) + gy * ga * 0.9f;
        }
        // stutter: the last slice of the bus, repeated while the stick is held down
        if (stut_buf) {
            if (!stut_on) { stut_buf[stut_w & (STUT_N - 1)] = y; stut_w++; }   // held still while it repeats: the ring
            float want = eng_stutter;                                            // would overwrite the slice otherwise
            if (want > 0.0f && !stut_on) { stut_on = true; stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; stut_start = stut_w - (uint32_t)stut_len; stut_pos = 0; }
            if (stut_on) {
                float r = stut_buf[(stut_start + (uint32_t)stut_pos) & (STUT_N - 1)];
                float w = stut_pos < 48 ? (float)stut_pos / 48.0f : stut_len - stut_pos < 48 ? (float)(stut_len - stut_pos) / 48.0f : 1.0f;
                if (++stut_pos >= stut_len) { stut_pos = 0; if (want > 0.0f) { stut_len = (int)(want * (float)AUDIO_SR); if (stut_len > STUT_N - 64) stut_len = STUT_N - 64; } }
                float tgt = want > 0.0f ? 1.0f : 0.0f;
                stut_x += (tgt - stut_x) * 0.01f;
                y = y * (1.0f - stut_x) + r * w * stut_x;
                if (want <= 0.0f && stut_x < 0.001f) { stut_on = false; stut_x = 0; }
            }
        }
        // quiet while a pad listens, so the mic hears only the room
        y *= smooth_to(&cap_mute_s, listening ? 0.0f : 1.0f);
        out[i] = y;
        float ay = fabsf(y); if (ay > pk) pk = ay;
    }
    eng_out_lvl = pk;
}
