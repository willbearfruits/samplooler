// DWELL's audio task: the voices and their clusters, every source, the difference tone, the strum, the hall, the
// take, the room pass's recording and playback. inst_dwell.h is the contract; inst_wave.c is the UI task's half
// (the take's analysis, the built-in WAVE table).
//
// Every block the UI's targets are read once. Each slot glides its pitch (in log2) and its level (linear) at
// eng_glide_s (tau is a third of it: within 5 % when it has passed); a slot near silence takes a new pitch at once,
// so a tone that swells in starts on its pitch. A slot is a cluster of 1 + round((COPIES - 1) DENSE) copies of its
// tone, BEAT Hz apart, each copy with its own phase and its own fade; the copies' gains are normalised so the cluster
// peaks at (1 + DENSE) times a pure voice, whatever their number. The table sources read one of six mip levels
// (1 2 4 8 16 32 harmonics, 1024 points, a guard point, linear interpolation) picked by BRIGHT and by the pitch (no
// harmonic above 14 kHz), two levels crossfaded by the fractional wish so BRIGHT never steps. TANPURA has a string
// per slot and ROOM a loop head per copy; neither clusters' phases. After the slots: the difference tone, the hall
// (a four-line FDN, Householder mixing), LEVEL, REST, the duck round a source change, then the room pass's playback.
// The slots are two layers of NVL (eng_layer crossfades them, equal power, over eng_fade_s); a slot whose layer is
// silent takes its targets at once. Before the hall: the energy's VCA, the interference bursts (eng_zap), the dirt (an
// asymmetric cubic soft clip with first-order antiderivative anti-aliasing, a DC blocker, a tone low pass, wet/dry) and
// the echo (PSRAM, its time gliding like tape, a dark saturating feedback, wet/dry).
// Every gain that reaches the sound is ramped inside the block; frequencies step per block, phase-continuous.
//
// Internal RAM (app_alloc_fast): the bank (39 KB: the current source's six tables, or TANPURA's twelve strings), the
// WAVE tables (8 KB), the hall (33 KB). PSRAM (app_alloc): the three spectra's banks built at init (74 KB), the take
// (128 KB), the room's two buffers (768 KB), the echo (203 KB).
#include "inst_dwell.h"
#include "inst_eng.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- the contract's variables
volatile float eng_slot_hz[NV], eng_slot_lvl[NV];
volatile float eng_glide_s = 20.0f, eng_dense, eng_beat_hz = 0.5f, eng_bright = 0.4f, eng_diff, eng_pluck_s = 0.8f, eng_jawari = 0.5f;
volatile float eng_wheel_hz = 1.2f, eng_breath = 0.4f, eng_body = 0.5f, eng_level = 0.8f, eng_space = 0.25f, eng_tail = 0.5f, eng_strum_s = 2.0f, eng_rest;
volatile int   eng_copies = 6, eng_source;
volatile float eng_slot_hz_now[NV], eng_slot_lvl_now[NV], eng_out_lvl, eng_diff_hz, eng_strum_env, eng_beat_phase;
volatile int   eng_copies_now = 1;
volatile int   eng_take_on, eng_take_frames; volatile float eng_take_level;
volatile float eng_wave_hz; volatile int eng_wave_ready;
volatile int   eng_room_state, eng_room_pass, eng_room_passes, eng_room_pos, eng_room_frames, eng_room_have;
volatile float eng_room_peak, eng_room_base_hz;
volatile int   eng_layer;
volatile float eng_fade_s = 8.0f, eng_vca = 1.0f, eng_drive, eng_tone = 0.6f, eng_dmix = 1.0f, eng_grit;
volatile float eng_echo_s = 0.45f, eng_echo_fb, eng_echo_mix, eng_zap_gate;
volatile float eng_layer_x, eng_zap_env, eng_drive_now;
volatile float eng_slot_hand[NV], eng_slot_bright[NV], eng_slot_sub[NV], eng_hand, eng_ring_s = 0.5f, eng_detune_hz;

int16_t *dw_take;
float   *dw_wtab[2];
volatile int dw_wave_cur, dw_wave_want, dw_wave_busy;

// ---------------------------------------------------------------- sizes and spectra
#define TAB_N      1024
#define NLEV       6                        // 1 2 4 8 16 32 harmonics
#define TAB_F      (TAB_N + 1)
#define BANK_F     (NLEV * TAB_F)           // 6150 floats
#define STR_N      816                      // a string's ring: 32000 / 40 and a little (lower strings play an octave up)
#define BANK_ALLOC (NV * STR_N)             // 9792 floats: the strings (both layers) share the bank with the tables
#define ECHO_N     52000                    // the echo's line: 1.625 s (PSRAM)
#define NZAP       16                       // interference bursts sounding at once
#define ROOM_N     (ROOM_MAX_S * AUDIO_SR)
#define ROOM_XF    640                      // the loop's seam, 20 ms
#define V_GAIN     0.075f                   // one pure voice's peak at level 1
#define HZ_MIN     16.0f
#define STR_A      0.9f                     // the string's loss filter (a one-pole in the loop): its overtones ring for seconds
#define STR_EX     0.05f                    // the pluck's low pass: a soft finger, a dark start
#define STR_KNEE   0.1f                     // the jawari's knee in the loop: what rises over it is squeezed 10 to 1
#define JAW_CAP    0.85f                    // the buzz: what swings past 85 % of the string's envelope
#define JAW_K      35.0f                    // ... through the bridge's formant, this loud at JAWARI 100
#define JAW_Q      5.0f
#define JAW_SWEEP  14.0f                    // the formant climbs from the 2nd harmonic by this many ...
#define JAW_SP     1.5f                     // ... as the string decays to a third of its peak
enum { SP_SINE, SP_HARM, SP_GURDY, NSPEC };
enum { SW_IDLE, SW_DUCK, SW_BUILD, SW_RISE };

static const int hall_len[HALL_N] = { 1559, 1877, 2203, 2617 };

// ---------------------------------------------------------------- state
typedef struct {
    float lg, lv, hz;                  // pitch now (log2 Hz), level now, Hz now
    float hz_t, lg_t;                  // the target seen last and its log2 (log2f once per change)
    float ph[NCOPY], cf[NCOPY], cg[NCOPY];   // a copy's phase (turns), fade, gain last block
    float rpos[NCOPY];                 // ROOM: a head per copy
    float g;                           // the slot's gain last block
    float wish;                        // the mip level wished, smoothed
    float body;                        // the body mix last block
    svf_t f1, f2;                      // REED / SHRUTI formants
    reso_t r1, r2;                     // BOW body
    float bow;                         // the bow's noise, slow
    float hg, bri, sub, subg, psub;    // BOW: the hand's gain, its brightness push, the growl an octave down (gain, phase)
    // TANPURA
    float sd;                          // the string's delay, samples
    float slp, sdx, sdy, senv, spk, sg, srel, sexc_a, sexc_lp;
    int   sw, sexc, sexc_len;
    uint32_t rng;
} slot_t;
static slot_t S[NV];

static float *bank, *spec_bank[NSPEC], *hall_buf[HALL_N];
static int16_t *rbuf[2];
static float sintab[TAB_N];

// smoothed controls
static float dense_s, beat_s, bright_s, body_s, breath_s, level_s = 0.8f, space_s = 0.25f, tail_s = 0.5f, rest_s = 1.0f, mute_s = 1.0f;
static float strum_env, strum_a;
static float glide_last = -1.0f, kp;
static int   ncopy = 1, cur_src = -1, want_src;
static float out_g;                    // LEVEL x REST x the duck, last block
static float bus[AUDIO_BLOCK], wet[AUDIO_BLOCK];
static uint32_t rng = 0x9e3779b9u;

// requests from the UI
static volatile uint32_t strum_seq, hush_seq, take_seq, room_seq;
static volatile float strum_amt;
static volatile int take_want;
static volatile int room_cmd, room_req_passes, room_req_frames; static volatile float room_req_base;
static uint32_t strum_seen, hush_seen, take_seen, room_seen;

// the source switch
static int   sw_state = SW_IDLE, sw_step;
static float sw_g = 1.0f, sw_g_prev = 1.0f;
static int   hush_hold;

// the difference tone
static float diff_hz_s, diff_lvl_s, diff_ph;

// the hall
static int   hall_w[HALL_N];
static float hall_lp[HALL_N], hall_g[HALL_N], hall_t60 = -1.0f, wet_norm = 0.5f, dry_g = 1.0f, wet_g;

// the sources' extras
static float wheel_ph, swell_ph, buzz_env, buzz_ph, air_s;
static svf_t buzz_bp, air_bp;
static int   pluck_wait, pluck_k;
static float wave_xf;

// the layers, the VCA, the dirt, the echo
static float lay_x, lay_g[2] = { 1.0f, 0.0f };
static float det_s;                    // NEAR FAR's detune, Hz, gliding
static svf_t grit_bp[NVL];             // a shake's grit: noise rung through band passes an octave over the tones that sound
static float grit_hz[NVL], grit_lv;
static float vca_s = 1.0f, drive_s, dmix_s = 1.0f, grit_s, tone_k = 1.0f, tone_last = -1.0f;
static float dr_u1, dr_dcx, dr_dcy, dr_lp, dr_G = 2.5f, dr_bias = 1.0f, dr_mix, dr_out = 1.0f;
static float *echo_buf;
static int   echo_w;
static float echo_d = 0.45f * (float)AUDIO_SR, echo_lp, efb_s, emix_s, e_dry = 1.0f, e_wet;
// the interference
typedef struct { uint32_t at; int len; float amp; int kind; } zap_t;
SPSC(zapq, zap_t, 64)
static zapq_t zq;
typedef struct { uint32_t at; int k, len; float amp, ph, inc; int kind; bool on; } zv_t;
static zv_t zv[NZAP];
static slot_t BODY;                    // the reed's formants and the bow's box: the same filter for every voice, so run once on their sum
static float zbuf[AUDIO_BLOCK], zenv[AUDIO_BLOCK], hbuf[AUDIO_BLOCK];

// the room pass
static int   room_rec = -1, room_play = -1, room_loop = -1, room_len, room_len_seen;
static int   rp_pos, rp_frames, rp_pass, rp_passes;
static float rp_peak, rp_gain;
static float room_norm = 1.0f;          // the ROOM loop to full scale, as the tables are

// ---------------------------------------------------------------- small helpers
// 1 - exp(-n / (t * SR)) without libm: the one-pole step for a block of n at a time constant t
static inline float kblock(float t_s, int n)
{
    float x = (float)n / (t_s * (float)AUDIO_SR);
    if (x < 0.15f) return x - 0.5f * x * x + x * x * x * (1.0f / 6.0f);
    return 1.0f - fast_exp2(-1.442695f * x);
}
// 2^x to a few parts in a million (fast_exp2 is 0.03 %: enough for a glide, not for a just ratio held)
static inline float exp2_prec(float x)
{
    if (x < -30.0f) return 0.0f;
    if (x > 30.0f) x = 30.0f;
    int   i = (int)(x + 31.0f) - 31;
    float f = x - (float)i;
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * (0.0013334f + f * (0.0001540f + f * 0.0000153f))))));
    union { float f; int32_t i; } u = { .f = p };
    u.i += i << 23;
    return u.f;
}
static inline float log2_rough(float x)        // for the brightness cap only
{
    union { float f; uint32_t i; } u = { .f = x };
    float e = (float)(int)((u.i >> 23) & 255) - 128.0f;
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
static inline float flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }
static void formants_for(slot_t *s, int src);

// ---------------------------------------------------------------- the banks (init: UI task)
static float harm_amp(int spec, int h)
{
    float a = 1.0f / (float)h;
    if (spec == SP_SINE) a /= sqrtf((float)h);
    if (spec == SP_GURDY && !(h & 1)) a *= 0.7071f;
    return a;
}
static void bank_build(float *b, int spec)
{
    for (int L = 0; L < NLEV; L++) {
        float *t = b + L * TAB_F, pk = 0.0f;
        int H = 1 << L;
        for (int i = 0; i < TAB_N; i++) {
            float s = 0.0f;
            for (int h = 1; h <= H; h++) s += harm_amp(spec, h) * sintab[(h * i) & (TAB_N - 1)];
            t[i] = s;
            if (fabsf(s) > pk) pk = fabsf(s);
        }
        float g = pk > 0.0f ? 1.0f / pk : 1.0f;
        for (int i = 0; i < TAB_N; i++) t[i] *= g;
        t[TAB_N] = t[0];
    }
}
static int spec_of(int src)
{
    switch (src) {
    case SRC_SINE: return SP_SINE;
    case SRC_REED: case SRC_BOW: case SRC_SHRUTI: return SP_HARM;
    case SRC_GURDY: return SP_GURDY;
    default: return -1;
    }
}

void eng_init(void)
{
    for (int i = 0; i < TAB_N; i++) sintab[i] = sin_turns((float)i / (float)TAB_N);
    bank = app_alloc_fast(BANK_ALLOC * sizeof(float));
    for (int s = 0; s < NSPEC; s++) { spec_bank[s] = app_alloc(BANK_F * sizeof(float)); if (spec_bank[s]) bank_build(spec_bank[s], s); }
    for (int k = 0; k < 2; k++) dw_wtab[k] = app_alloc_fast((WAVE_N + 1) * sizeof(float));
    for (int k = 0; k < HALL_N; k++) hall_buf[k] = app_alloc_fast((size_t)hall_len[k] * sizeof(float));
    dw_take = app_alloc(TAKE_N * sizeof(int16_t));
    for (int k = 0; k < 2; k++) rbuf[k] = app_alloc(ROOM_N * sizeof(int16_t));
    echo_buf = app_alloc(ECHO_N * sizeof(float));                     // read and written in step, once a sample: PSRAM is fine
    if (!bank || !dw_wtab[0] || !dw_wtab[1] || !hall_buf[HALL_N - 1]) { app_log("DWELL: out of internal RAM"); bank = NULL; return; }
    if (!dw_take || !rbuf[1]) app_log("DWELL: out of PSRAM (no take or room)");
    if (!echo_buf) app_log("DWELL: out of PSRAM (no echo)");
    dw_wave_default(dw_wtab[0]);
    memcpy(dw_wtab[1], dw_wtab[0], (WAVE_N + 1) * sizeof(float));
    eng_wave_ready = 1;
    for (int v = 0; v < NV; v++) {
        slot_t *s = &S[v];
        s->rng = 0x1234567u + (uint32_t)v * 0x9e3779b9u;
        s->lg = s->lg_t = log2f(65.4f); s->hz = s->hz_t = 65.4f;
        for (int c = 0; c < NCOPY; c++) s->ph[c] = rng_uni(&s->rng);
        s->cf[0] = s->cg[0] = 1.0f;
        formants_for(s, eng_source);
        reso_set(&s->r1, 280.0f, 0.06f); reso_set(&s->r2, 480.0f, 0.06f);
        s->sd = 400.0f; s->srel = 1.1f; s->hg = 1.0f;
    }
    formants_for(&BODY, eng_source);
    for (int k = 0; k < NVL; k++) { grit_hz[k] = 260.0f; svf_set(&grit_bp[k], 260.0f, 14.0f); }
    howl_init();
    reso_set(&BODY.r1, 280.0f, 0.06f); reso_set(&BODY.r2, 480.0f, 0.06f);
    svf_set(&buzz_bp, 1800.0f, 1.2f);
    svf_set(&air_bp, 1500.0f, 0.8f);
    // the first source's tables, at once: no switch at boot
    cur_src = want_src = eng_source;
    int sp = spec_of(cur_src);
    if (sp >= 0 && spec_bank[sp]) memcpy(bank, spec_bank[sp], BANK_F * sizeof(float));
}

// ---------------------------------------------------------------- the UI's calls
void eng_strum(float strength)
{
    strum_amt = clampf(strength, 0.0f, 1.0f);
    __atomic_add_fetch(&strum_seq, 1, __ATOMIC_RELEASE);
}
void eng_hush(void) { __atomic_add_fetch(&hush_seq, 1, __ATOMIC_RELEASE); }
void eng_take_begin(void) { take_want = 1; __atomic_add_fetch(&take_seq, 1, __ATOMIC_RELEASE); }
void eng_take_end(void)   { take_want = 0; __atomic_add_fetch(&take_seq, 1, __ATOMIC_RELEASE); }
void eng_room_start(int passes, int frames, float base_hz)
{
    room_req_passes = clampi(passes, 1, 30);
    room_req_frames = clampi(frames, AUDIO_SR / 4, ROOM_N);
    room_req_base = base_hz > HZ_MIN ? base_hz : 65.4f;
    room_cmd = 1;
    __atomic_add_fetch(&room_seq, 1, __ATOMIC_RELEASE);
}
bool eng_zap(uint32_t at, int len, float amp, int kind)
{
    zap_t z = { .at = at, .len = len < 4 ? 4 : len > 4000 ? 4000 : len, .amp = clampf(amp, 0.0f, 1.0f), .kind = kind };
    return zapq_push(&zq, &z);
}
void eng_room_stop(void) { room_cmd = 2; __atomic_add_fetch(&room_seq, 1, __ATOMIC_RELEASE); }
const int16_t *eng_room_loop(int *frames)
{
    if (room_loop < 0 || !eng_room_have) { *frames = 0; return NULL; }
    *frames = room_len;
    return rbuf[room_loop];
}

// ---------------------------------------------------------------- per-sample kernels (IRAM)
// n samples of one copy: a table of N points (and a guard) read at inc turns a sample, into acc with a gain ramp
static IRAM_ATTR __attribute__((noinline)) void copy_one(float *restrict acc, const float *restrict ta, float N, float *restrict ph_io,
                                                        float inc, float g0, float g1, int n)
{
    float ph = *ph_io, g = g0, dg = (g1 - g0) / (float)n;
    for (int i = 0; i < n; i++) {
        ph += inc; ph -= (float)(ph >= 1.0f);
        float x = ph * N; int k = (int)x; float fr = x - (float)k;
        float a = ta[k];
        acc[i] += g * (a + (ta[k + 1] - a) * fr);
        g += dg;
    }
    *ph_io = ph;
}
// the same between two tables, xf of the second
static IRAM_ATTR __attribute__((noinline)) void copy_two(float *restrict acc, const float *restrict ta, const float *restrict tb, float xf, float N,
                                                        float *restrict ph_io, float inc, float g0, float g1, int n)
{
    float ph = *ph_io, g = g0, dg = (g1 - g0) / (float)n;
    for (int i = 0; i < n; i++) {
        ph += inc; ph -= (float)(ph >= 1.0f);
        float x = ph * N; int k = (int)x; float fr = x - (float)k;
        float a = ta[k], b = tb[k];
        a += (ta[k + 1] - a) * fr; b += (tb[k + 1] - b) * fr;
        acc[i] += g * (a + (b - a) * xf);
        g += dg;
    }
    *ph_io = ph;
}
// one copy's head on the ROOM loop: int16, linear interpolation, the end crossfaded into the start over xfn
static IRAM_ATTR __attribute__((noinline)) void copy_room(float *restrict acc, const int16_t *restrict loop, int len, int xfn, float *restrict pos_io,
                                                         float rate, float g0, float g1, int n)
{
    float pos = *pos_io, g = g0, dg = (g1 - g0) / (float)n;
    float seam = (float)(len - xfn - 1), ixf = 1.0f / (float)xfn, top = (float)(len - 1);
    const float q = 1.0f / 32768.0f;
    for (int i = 0; i < n; i++) {
        int k = (int)pos; float fr = pos - (float)k;
        float a = (float)loop[k]; a += ((float)loop[k + 1] - a) * fr;
        if (pos >= seam) {
            float p2 = pos - seam; int k2 = (int)p2; float f2 = p2 - (float)k2;
            float b = (float)loop[k2]; b += ((float)loop[k2 + 1] - b) * f2;
            float w = p2 * ixf;
            a += (b - a) * w;
        }
        acc[i] += g * a * q;
        g += dg;
        pos += rate;
        if (pos >= top) pos -= seam;
        if (pos < 0.0f) pos = 0.0f;
    }
    *pos_io = pos;
}
// a TANPURA string: Karplus-Strong with a light low pass (its overtones ring for seconds), the jawari's one-sided knee
// at a threshold on its own envelope, a DC block; a pluck adds a dark noise burst as long as the string. Out of the
// loop, the bridge's buzz: the other side's crests, through a formant that climbs as the string decays. After a pluck
// it starts dark, its overtones come up over half a second (the bloom), then fade with it.
static IRAM_ATTR __attribute__((noinline)) void string_run(float *restrict out, float *restrict ring, slot_t *restrict s,
                                                          float d0, float d1, float rel, float jaw, int n)
{
    int w = s->sw;
    float d = d0, dd = (d1 - d0) / (float)n;
    float lp = s->slp, dx = s->sdx, dy = s->sdy, env = s->senv, pk = s->spk, g = s->sg, exlp = s->sexc_lp;
    uint32_t r = s->rng;
    for (int i = 0; i < n; i++) {
        float rp = (float)w - d; if (rp < 0.0f) rp += (float)STR_N;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= STR_N) i1 -= STR_N;
        float a = ring[i0];
        float y = a + (ring[i1] - a) * fr;
        lp += STR_A * (y - lp);
        float z = lp;
        float az = fabsf(z);
        env += (az > env ? 0.1f : 0.0004f) * (az - env);
        if (env > pk) pk = env;
        float thr = rel * env;
        if (z > thr) z = thr + (z - thr) * STR_KNEE;
        float dc = z - dx + 0.999f * dy; dx = z; dy = dc;
        float x = dc * g;
        if (s->sexc > 0) {                                           // a soft pluck: a finger's smooth push, a little noise for life
            int k = s->sexc_len - s->sexc;
            float wq = 0.25f * (float)s->sexc_len;
            float bump = (float)k < wq ? sin_turns(0.5f * (float)k / wq) : 0.0f;
            exlp += STR_EX * (rng_bi(&r) - exlp);
            x += s->sexc_a * (0.8f * bump + 1.5f * exlp);
            s->sexc--;
        }
        ring[w] = x;
        if (++w >= STR_N) w = 0;
        // the bridge's buzz: what swings past JAW_CAP of the string's envelope, through the climbing formant
        float ay = JAW_CAP * env, res = -y > ay ? -y - ay : 0.0f;          // the side the knee leaves whole
        out[i] = y + jaw * JAW_K * svf_bp(&s->f1, res, NULL);
        d += dd;
    }
    s->f1.ic1 = flush(s->f1.ic1); s->f1.ic2 = flush(s->f1.ic2);
    s->sw = w; s->slp = flush(lp); s->sdx = flush(dx); s->sdy = flush(dy); s->senv = flush(env); s->spk = pk; s->sexc_lp = exlp; s->rng = r;
}
// the hall: four lines, a damping one-pole each, Householder feedback; in -> wet
static IRAM_ATTR __attribute__((noinline)) void hall_run(const float *restrict in, float *restrict w, float send0, float send1, int n)
{
    float *b0 = hall_buf[0], *b1 = hall_buf[1], *b2 = hall_buf[2], *b3 = hall_buf[3];
    int w0 = hall_w[0], w1 = hall_w[1], w2 = hall_w[2], w3 = hall_w[3];
    float l0 = hall_lp[0], l1 = hall_lp[1], l2 = hall_lp[2], l3 = hall_lp[3];
    float g0 = hall_g[0], g1 = hall_g[1], g2 = hall_g[2], g3 = hall_g[3];
    float s = send0, ds = (send1 - send0) / (float)n;
    const float damp = 0.35f;
    for (int i = 0; i < n; i++) {
        float x = in[i] * s;
        l0 += damp * (b0[w0] - l0); l1 += damp * (b1[w1] - l1); l2 += damp * (b2[w2] - l2); l3 += damp * (b3[w3] - l3);
        float h = 0.5f * (l0 + l1 + l2 + l3);
        b0[w0] = x + g0 * (l0 - h); b1[w1] = -x + g1 * (l1 - h); b2[w2] = x + g2 * (l2 - h); b3[w3] = -x + g3 * (l3 - h);
        if (++w0 >= 1559) w0 = 0;
        if (++w1 >= 1877) w1 = 0;
        if (++w2 >= 2203) w2 = 0;
        if (++w3 >= 2617) w3 = 0;
        w[i] = 0.5f * (l0 - l1 + l2 - l3);
        s += ds;
    }
    hall_w[0] = w0; hall_w[1] = w1; hall_w[2] = w2; hall_w[3] = w3;
    hall_lp[0] = flush(l0); hall_lp[1] = flush(l1); hall_lp[2] = flush(l2); hall_lp[3] = flush(l3);
}

// the dirt: the cubic soft clip x - x^3/3 (flat at 2/3 past 1) and its antiderivative, for first-order ADAA
static inline float clip3(float x) { return x > 1.0f ? 0.6666667f : x < -1.0f ? -0.6666667f : x - x * x * x * (1.0f / 3.0f); }
static inline float clip3_F(float x)
{
    float a = fabsf(x);
    return a > 1.0f ? 0.6666667f * a - 0.25f : 0.5f * x * x - x * x * x * x * (1.0f / 12.0f);
}
// asymmetric: the negative side through k times the gain (k < 1 clips it later and higher): a fuzz's even harmonics
static inline float clipa(float u, float k) { return u >= 0.0f ? clip3(u) : clip3(k * u) / k; }
static inline float clipa_F(float u, float k) { return u >= 0.0f ? clip3_F(u) : clip3_F(k * u) / (k * k); }
// in place: in-gain (G), the asymmetric clip (k) with first-order ADAA, a DC blocker for what the asymmetry leaves, the
// tone, the clean and the driven mixed; G, k, out-gain and mix ramp over the block from where they were
static IRAM_ATTR __attribute__((noinline)) void drive_run(float *restrict x, int n, float G1, float k1, float o1, float m1)
{
    if (m1 <= 0.0f && dr_mix <= 0.0f) { dr_G = G1; dr_bias = k1; dr_out = o1; return; }   // clean: nothing to do
    float G = dr_G, dG = (G1 - G) / (float)n, k = dr_bias, dk = (k1 - k) / (float)n, o = dr_out, dO = (o1 - o) / (float)n;
    float m = dr_mix, dm = (m1 - m) / (float)n, u1 = dr_u1, dcx = dr_dcx, dcy = dr_dcy, lp = dr_lp, tk = tone_k;
    for (int i = 0; i < n; i++) {
        float clean = x[i];
        float u = clean * G;
        float du = u - u1, y;
        if (fabsf(du) > 1e-4f) y = (clipa_F(u, k) - clipa_F(u1, k)) / du;
        else y = clipa(0.5f * (u + u1), k);
        u1 = u;
        float h = y - dcx + 0.9985f * dcy; dcx = y; dcy = h;         // what the asymmetry leaves: about 8 Hz
        lp += tk * (h - lp);
        x[i] = clean + m * (lp * o - clean);
        G += dG; k += dk; o += dO; m += dm;
    }
    dr_G = G1; dr_bias = k1; dr_out = o1; dr_mix = m1;
    dr_u1 = u1; dr_dcx = flush(dcx); dr_dcy = flush(dcy); dr_lp = flush(lp);
}
// in place: the echo. Its time glides (tape), what comes round is darkened and saturated, then mixed in (equal power)
static IRAM_ATTR __attribute__((noinline)) void echo_run(float *restrict x, int n, float d1, float fb1, float wet1, float dry1)
{
    float d = echo_d, dd = (d1 - d) / (float)n, fb = efb_s, wet = e_wet, dw = (wet1 - wet) / (float)n, dry = e_dry, dy = (dry1 - dry) / (float)n;
    float lp = echo_lp;
    int w = echo_w;
    float *buf = echo_buf;
    for (int i = 0; i < n; i++) {
        float rp = (float)w - d; if (rp < 0.0f) rp += (float)ECHO_N;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= ECHO_N) i1 = 0;
        float a = buf[i0], e = a + (buf[i1] - a) * fr;
        lp += 0.35f * (e - lp);                                      // a little darker each time round
        buf[w] = flush(x[i] + fb * 0.5f * soft_tanh(2.0f * lp));      // and never more than half scale coming round
        if (++w >= ECHO_N) w = 0;
        x[i] = x[i] * dry + e * wet;
        d += dd; wet += dw; dry += dy;
    }
    echo_d = d1; e_wet = wet1; e_dry = dry1; echo_lp = flush(lp); echo_w = w; (void)fb1;
}
// the interference: new bursts from the queue into free voices; then n samples of them into zbuf, their envelope
// into zenv (a Wi-Fi frame is a crackle of noise, a Bluetooth advert a short buzz; 8-sample edges)
static bool zaps_take(void)
{
    zap_t z;
    while (zapq_pop(&zq, &z)) {
        int k = -1;
        for (int i = 0; i < NZAP; i++) if (!zv[i].on) { k = i; break; }
        if (k < 0) continue;                                          // all busy: this one is lost in the others
        static const float buzz[3] = { 1180.0f, 1590.0f, 2030.0f };  // the three advertising channels, as three buzzes
        zv[k] = (zv_t){ .at = z.at, .k = -1, .len = z.len, .amp = z.amp, .kind = z.kind, .on = true,
                        .inc = buzz[(z.at >> 3) % 3] / (float)AUDIO_SR };
    }
    for (int i = 0; i < NZAP; i++) if (zv[i].on) return true;
    return false;
}
static IRAM_ATTR __attribute__((noinline)) float zaps_run(int n, uint32_t frame0, uint32_t *rs)
{
    memset(zbuf, 0, (size_t)n * sizeof(float));
    memset(zenv, 0, (size_t)n * sizeof(float));
    float pk = 0.0f;
    uint32_t r = *rs;
    for (int j = 0; j < NZAP; j++) {
        zv_t *z = &zv[j];
        if (!z->on) continue;
        int i0 = 0;
        if (z->k < 0) {                                              // not started: when its moment comes in this block
            int32_t off = (int32_t)(z->at - frame0);
            if (off >= n) continue;
            if (off < -AUDIO_SR / 10) off = 0;                       // far too late (a stall): now
            i0 = off > 0 ? off : 0;
            z->k = 0;
        }
        for (int i = i0; i < n; i++) {
            int left = z->len - z->k;
            if (left <= 0) { z->on = false; break; }
            float e = z->k < 8 ? (float)z->k * 0.125f : left < 8 ? (float)left * 0.125f : 1.0f;
            float v;
            if (z->kind == ZAP_WIFI) v = 0.25f * rng_bi(&r);
            else { z->ph += z->inc; z->ph -= (float)(z->ph >= 1.0f); v = z->ph < 0.5f ? 0.12f : -0.12f; }
            zbuf[i] += z->amp * e * v;
            zenv[i] += z->amp * e;
            z->k++;
        }
        if (z->k >= z->len) z->on = false;
    }
    for (int i = 0; i < n; i++) { if (zenv[i] > 1.0f) zenv[i] = 1.0f; if (zenv[i] > pk) pk = zenv[i]; }
    *rs = r;
    return pk;
}

// ---------------------------------------------------------------- the source switch (under the duck)
// the reed's two formants; the shruti box's lower and more nasal
static void formants_for(slot_t *s, int src)
{
    if (src == SRC_SHRUTI) { svf_set(&s->f1, 650.0f, 3.0f); svf_set(&s->f2, 1900.0f, 5.0f); }
    else { svf_set(&s->f1, 1500.0f, 4.0f); svf_set(&s->f2, 3000.0f, 4.0f); }
}
static void slot_reset_filters(slot_t *s)
{
    s->f1.ic1 = s->f1.ic2 = s->f2.ic1 = s->f2.ic2 = 0.0f;
    s->r1.y1 = s->r1.y2 = s->r2.y1 = s->r2.y2 = 0.0f;
    s->slp = s->sdx = s->sdy = s->senv = s->spk = 0.0f; s->sw = 0; s->sexc = 0; s->srel = 1.1f;
    s->bow = 0.0f;
}
// one step of the build per block; true when done
static bool switch_build(int step)
{
    int sp = spec_of(want_src);
    if (step == 0) {
        for (int v = 0; v < NV; v++) slot_reset_filters(&S[v]);
        slot_reset_filters(&BODY); formants_for(&BODY, want_src);
        if (want_src == SRC_TANPURA) memset(bank, 0, BANK_ALLOC * sizeof(float));
        if (want_src == SRC_ROOM) room_len_seen = -1;                  // the heads spread out
        pluck_wait = 0; pluck_k = 0; buzz_env = 0.0f;
    }
    if (sp >= 0 && spec_bank[sp]) {
        if (step < NLEV) { memcpy(bank + step * TAB_F, spec_bank[sp] + step * TAB_F, TAB_F * sizeof(float)); return step == NLEV - 1; }
        return true;
    }
    return true;
}
static void switch_tick(void)
{
    if (sw_state == SW_IDLE) {
        if (eng_source != cur_src || hush_seq != hush_seen) { sw_state = SW_DUCK; hush_hold = 0; }
    } else if ((sw_state == SW_BUILD || sw_state == SW_RISE) && eng_source != cur_src && eng_source != want_src) sw_state = SW_DUCK;
    sw_g_prev = sw_g;
    switch (sw_state) {
    case SW_DUCK:
        sw_g -= 0.2f;
        if (sw_g <= 0.0f) {
            sw_g = 0.0f;
            if (eng_source != cur_src) { want_src = eng_source; sw_state = SW_BUILD; sw_step = 0; }
            else if (++hush_hold >= 15) { hush_seen = hush_seq; sw_state = SW_RISE; }
        }
        break;
    case SW_BUILD:
        if (switch_build(sw_step++)) { cur_src = want_src; hush_seen = hush_seq; sw_state = SW_RISE; }
        break;
    case SW_RISE:
        sw_g += 0.2f;
        if (sw_g >= 1.0f) { sw_g = 1.0f; sw_state = SW_IDLE; }
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- the take and the room pass (recording side)
static void take_run(const float *in, int n)
{
    if (take_seq != take_seen) {
        take_seen = take_seq;
        if (take_want && dw_take) { eng_take_frames = 0; eng_take_on = 1; }
        else eng_take_on = 0;
    }
    float pk = 0.0f;
    for (int i = 0; i < n; i++) { float a = fabsf(in[i]); if (a > pk) pk = a; }
    eng_take_level = pk > eng_take_level ? pk : eng_take_level * 0.97f;
    if (!eng_take_on) return;
    int f = eng_take_frames, m = n;
    if (f + m > TAKE_N) m = TAKE_N - f;
    for (int i = 0; i < m; i++) { float v = in[i] * 32767.0f; dw_take[f + i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v); }
    f += m;
    eng_take_frames = f;
    if (f >= TAKE_N) eng_take_on = 0;
}

static void room_pass_end(void)
{
    eng_room_peak = rp_peak;
    room_play = room_rec;
    room_rec = room_rec ^ 1;
    rp_pass++;
    if (rp_pass >= rp_passes) {
        room_loop = room_play; room_len = rp_frames; eng_room_have = 1;
        room_norm = 1.0f / (rp_peak > 0.003f ? rp_peak : 0.003f);
        eng_room_pass = rp_pass; eng_room_state = ROOM_DONE;
        return;
    }
    rp_gain = 0.5f / (rp_peak > 0.0001f ? rp_peak : 0.0001f);
    if (rp_gain > 60.0f) rp_gain = 60.0f;
    rp_pos = 0; rp_peak = 0.0f;
    eng_room_pos = 0; eng_room_pass = rp_pass;
}
static void room_requests(void)
{
    if (room_seq == room_seen) return;
    room_seen = room_seq;
    if (room_cmd == 1 && rbuf[1]) {
        rp_passes = room_req_passes; rp_frames = room_req_frames; rp_pass = 0; rp_pos = 0; rp_peak = 0.0f;
        room_rec = room_loop == 0 ? 1 : 0; room_play = -1;
        eng_room_base_hz = room_req_base; eng_room_passes = rp_passes; eng_room_frames = rp_frames;
        eng_room_pos = 0; eng_room_pass = 0; eng_room_state = ROOM_PASSING;
    } else if (room_cmd == 2 && eng_room_state == ROOM_PASSING) {
        if (rp_pass >= 1 && room_play >= 0) {
            room_loop = room_play; room_len = rp_frames; eng_room_have = 1;
            room_norm = 1.0f / (eng_room_peak > 0.003f ? eng_room_peak : 0.003f);
        }
        eng_room_state = ROOM_IDLE;
    }
}
// records the mic; from pass 1 on plays the last recording (scaled to -6 dBFS) into play[]
static void room_run(const float *in, float *play, int n)
{
    if (eng_room_state != ROOM_PASSING) return;
    int16_t *rec = rbuf[room_rec];
    const int16_t *pl = rp_pass >= 1 ? rbuf[room_play] : NULL;
    const float q = rp_gain * (1.0f / 32768.0f);
    for (int i = 0; i < n; i++) {
        float x = in[i], a = fabsf(x);
        if (a > rp_peak) rp_peak = a;
        float v = x * 32767.0f;
        rec[rp_pos] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
        if (pl) play[i] += (float)pl[rp_pos] * q;
        if (++rp_pos >= rp_frames) { room_pass_end(); if (eng_room_state != ROOM_PASSING) return; rec = rbuf[room_rec]; pl = rbuf[room_play]; }
    }
    eng_room_pos = rp_pos;
}

// ---------------------------------------------------------------- a slot's block
// its copies' frequencies and gains, then the source's sound into buf[]; returns false when there is nothing to hear
static bool slot_render(slot_t *s, float *buf, int n, int src, float strum_mul)
{
    int nc = (src == SRC_TANPURA) ? 1 : ncopy;
    // the copies' fades, and their normalisation: the cluster peaks at (1 + dense) x a pure voice
    float sum = 0.0f;
    for (int c = 0; c < NCOPY; c++) {
        float t = c < nc ? 1.0f : 0.0f;
        s->cf[c] += 0.0392f * (t - s->cf[c]);                        // 50 ms
        if (s->cf[c] < 1e-3f && t == 0.0f) s->cf[c] = 0.0f;
        sum += s->cf[c];
    }
    float norm = sum > 1e-6f ? (1.0f + dense_s) / sum : 0.0f;
    bool any = false;
    for (int c = 0; c < NCOPY; c++) if (s->cf[c] > 0.0f || s->cg[c] > 0.0f) any = true;
    if (!any || s->lv < 1e-4f) { for (int c = 0; c < NCOPY; c++) s->cg[c] = 0.0f; return false; }
    memset(buf, 0, (size_t)n * sizeof(float));
    float hz = s->hz + det_s; if (hz < HZ_MIN) hz = HZ_MIN;              // NEAR FAR's detune on every voice
    float half = 0.5f * (float)(nc - 1);
    float wobble = 1.0f;
    if (src == SRC_GURDY) wobble = 1.0f + 0.003f * sin_turns(wheel_ph);
    if (src == SRC_TANPURA) {
        float f = hz; while (f < 40.0f) f *= 2.0f; if (f > 2000.0f) f = 2000.0f;
        float d = (float)AUDIO_SR / f - (1.0f - STR_A) / STR_A;         // the loop's low pass delays the rest
        float d0 = s->sd; s->sd = d;
        // the decay: 6 s at the fundamental; the jawari's knee, relative to the string's own envelope, hardens as it decays
        s->sg = fast_exp2(-9.966f * d / (6.0f * (float)AUDIO_SR));
        float hard = eng_jawari * (0.3f + 0.7f * (1.0f - (s->spk > 1e-6f ? s->senv / s->spk : 1.0f)));
        float rel = 1.1f - 0.9f * clampf(hard, 0.0f, 1.0f);
        s->srel += 0.2f * (rel - s->srel);
        // the jawari's formant: from the 2nd harmonic at the pluck up to the 16th as the string decays to a third
        float sweep = s->spk > 1e-6f ? clampf(JAW_SP * (1.0f - s->senv / s->spk), 0.0f, 1.0f) : 0.0f;
        svf_set(&s->f1, f * (2.0f + JAW_SWEEP * sweep), JAW_Q);
        string_run(buf, bank + (s - S) * STR_N, s, d0, d, s->srel, eng_jawari, n);
        float g = 11.0f * norm;                                       // the soft pluck is quiet: up to the other sources
        for (int i = 0; i < n; i++) buf[i] *= g;
        s->cg[0] = norm;
        return true;
    }
    if (src == SRC_ROOM) {
        if (room_loop < 0 || !eng_room_have || room_len < 4 * ROOM_XF) return false;
        int xfn = ROOM_XF;
        if (room_len != room_len_seen) { room_len_seen = room_len; for (int c = 0; c < NCOPY; c++) s->rpos[c] = (float)room_len * ((float)c + 0.37f * (float)(s - S)) * (1.0f / (float)NCOPY) * 0.9f; }
        float base = eng_room_base_hz > HZ_MIN ? eng_room_base_hz : hz;
        for (int c = 0; c < NCOPY; c++) {
            float g1 = s->cf[c] * norm * room_norm;
            if (g1 <= 0.0f && s->cg[c] <= 0.0f) continue;
            float f = hz * wobble + beat_s * ((float)c - half);
            float rate = f / base; if (rate < 0.05f) rate = 0.05f; if (rate > 8.0f) rate = 8.0f;
            if (s->rpos[c] >= (float)(room_len - 1)) s->rpos[c] = 0.0f;
            copy_room(buf, rbuf[room_loop], room_len, xfn, &s->rpos[c], rate, s->cg[c], g1, n);
            s->cg[c] = g1;
        }
        return true;
    }
    // the table sources: the mip level by the wish and by the pitch
    const float *ta, *tb; float N, xf;
    if (src == SRC_WAVE) {
        ta = dw_wtab[dw_wave_cur]; tb = dw_wtab[dw_wave_want]; xf = wave_xf; N = (float)WAVE_N;
    } else {
        float wish = src == SRC_SINE ? bright_s * dense_s * 5.0f : 2.0f + bright_s * 3.0f;
        wish += 1.5f * strum_a;
        if (src == SRC_BOW) wish += 0.4f * s->bow;
        wish += 3.5f * s->bri;                                         // BOW: pressure and the bow's place
        if (wish < 0.0f) wish = 0.0f;
        float fmax = hz + beat_s * half; if (fmax < HZ_MIN) fmax = HZ_MIN;
        float cap = log2_rough(14000.0f / fmax);
        if (cap < 0.0f) cap = 0.0f;
        if (wish > cap) wish = cap;
        if (wish > (float)(NLEV - 1)) wish = (float)(NLEV - 1);
        s->wish += 0.1f * (wish - s->wish);
        int l0 = (int)s->wish; if (l0 > NLEV - 1) l0 = NLEV - 1;
        xf = s->wish - (float)l0;
        int l1 = l0 + 1 < NLEV ? l0 + 1 : l0;
        ta = bank + l0 * TAB_F; tb = bank + l1 * TAB_F; N = (float)TAB_N;
    }
    for (int c = 0; c < NCOPY; c++) {
        float g1 = s->cf[c] * norm;
        if (g1 <= 0.0f && s->cg[c] <= 0.0f) continue;
        float f = hz * wobble + beat_s * ((float)c - half);
        float inc = f * (1.0f / (float)AUDIO_SR); if (inc < 0.0f) inc = 0.0f; if (inc > 0.45f) inc = 0.45f;
        if (xf < 0.003f) copy_one(buf, ta, N, &s->ph[c], inc, s->cg[c], g1, n);
        else if (xf > 0.997f) copy_one(buf, tb, N, &s->ph[c], inc, s->cg[c], g1, n);
        else copy_two(buf, ta, tb, xf, N, &s->ph[c], inc, s->cg[c], g1, n);
        s->cg[c] = g1;
    }
    // BOW: pressed too hard, the string growls an octave down
    float gs1 = s->sub * norm * 0.8f;
    if (gs1 > 1e-5f || s->subg > 1e-5f) { copy_one(buf, ta, N, &s->psub, 0.5f * hz * (1.0f / (float)AUDIO_SR), s->subg, gs1, n); s->subg = gs1; }
    (void)strum_mul;
    return true;
}
// the slot's body after its copies: formants, the bow's box; in place
static IRAM_ATTR __attribute__((noinline)) void slot_body(slot_t *s, float *buf, int n, int src, float b1)
{
    float b = s->body, db = (b1 - b) / (float)n;
    s->body = b1;
    if (src == SRC_REED || src == SRC_SHRUTI) {
        for (int i = 0; i < n; i++) {
            float x = buf[i];
            float p = svf_bp(&s->f1, x, NULL) * 1.2f + svf_bp(&s->f2, x, NULL) * 0.8f;
            buf[i] = x + b * p;
            b += db;
        }
        s->f1.ic1 = flush(s->f1.ic1); s->f1.ic2 = flush(s->f1.ic2); s->f2.ic1 = flush(s->f2.ic1); s->f2.ic2 = flush(s->f2.ic2);
    } else if (src == SRC_BOW || src == SRC_GURDY) {
        const float bg = 0.012f;                                        // the resonators' gain, about 1.5 at the peak
        for (int i = 0; i < n; i++) {
            float x = buf[i];
            reso_strike(&s->r1, x); reso_strike(&s->r2, x);
            float y = reso_run(&s->r1) + 0.8f * reso_run(&s->r2);
            buf[i] = x * (1.0f - 0.3f * b) + b * bg * y;
            b += db;
        }
        s->r1.y1 = flush(s->r1.y1); s->r1.y2 = flush(s->r1.y2); s->r2.y1 = flush(s->r2.y1); s->r2.y2 = flush(s->r2.y2);
    }
}

// ---------------------------------------------------------------- the block
void eng_audio(const float *in, float *out, int n)
{
    if (!bank || n > AUDIO_BLOCK) { memset(out, 0, (size_t)n * sizeof(float)); return; }
    memset(out, 0, (size_t)n * sizeof(float));
    // the UI's requests
    if (strum_seq != strum_seen) {
        strum_seen = strum_seq;
        float a = strum_amt;
        if (a > strum_env) strum_env = a;
        if (cur_src == SRC_TANPURA) pluck_wait = (int)(clampf(eng_pluck_s, 0.1f, 8.0f) * (float)AUDIO_SR);   // the cycle starts again after a strum
        if (cur_src == SRC_TANPURA && sw_state == SW_IDLE)
            for (int v = 0; v < NV; v++) if (S[v].lv > 0.02f || eng_slot_lvl[v] > 0.0f) { S[v].sexc = S[v].sexc_len = (int)S[v].sd; S[v].sexc_a = 0.25f * (0.5f + a); S[v].spk = 0.0f; }
        if (cur_src == SRC_GURDY) buzz_env = a > buzz_env ? a : buzz_env;
    }
    switch_tick();
    take_run(in, n);
    room_requests();
    room_run(in, out, n);                                             // the pass's playback goes straight out
    // smoothed controls (block rate; what reaches a sample is ramped inside the kernels)
    dense_s += 0.0392f * (clampf(eng_dense, 0.0f, 1.0f) - dense_s);
    beat_s += 0.02f * (clampf(eng_beat_hz, 0.0f, 16.0f) - beat_s);
    bright_s += 0.0392f * (clampf(eng_bright, 0.0f, 1.0f) - bright_s);
    body_s += 0.0392f * (clampf(eng_body, 0.0f, 1.0f) - body_s);
    breath_s += 0.0392f * (clampf(eng_breath, 0.0f, 1.0f) - breath_s);
    level_s += 0.0645f * (clampf(eng_level, 0.0f, 1.0f) - level_s);
    space_s += 0.0392f * (clampf(eng_space, 0.0f, 1.0f) - space_s);
    tail_s += 0.0392f * (clampf(eng_tail, 0.0f, 1.0f) - tail_s);
    rest_s += kblock(0.6f, n) * ((eng_rest > 0.5f ? 0.0f : 1.0f) - rest_s);
    int src = cur_src;
    bool passing_mute = eng_room_state == ROOM_PASSING && rp_pass >= 1;
    mute_s += 0.0392f * ((passing_mute ? 0.0f : 1.0f) - mute_s);
    int copies = clampi(eng_copies, 1, NCOPY);
    ncopy = 1 + (int)((float)(copies - 1) * dense_s + 0.5f);
    // the layers' crossfade: linear in time over FADE, equal power
    {
        float fs = eng_fade_s; if (fs < 0.2f) fs = 0.2f;
        float lt = eng_layer ? 1.0f : 0.0f, step = (float)n / (fs * (float)AUDIO_SR);
        if (lay_x < lt) { lay_x += step; if (lay_x > lt) lay_x = lt; }
        else if (lay_x > lt) { lay_x -= step; if (lay_x < lt) lay_x = lt; }
        xfade_gains(lay_x, &lay_g[0], &lay_g[1]);
        eng_layer_x = lay_x;
    }
    // while both layers sound, the copies are shared out: 40 at most (48 with one layer: 6 tones of 8)
    {
        int active = 0;
        for (int v = 0; v < NV; v++) if ((S[v].lv > 1e-4f || eng_slot_lvl[v] > 0.0f) && lay_g[v / NVL] > 0.001f) active++;
        if (active > 6 && ncopy * active > 40) { ncopy = 40 / active; if (ncopy < 1) ncopy = 1; }
    }
    eng_copies_now = src == SRC_TANPURA ? 1 : ncopy;
    // the strum: instant up, down over STRUM; applied through a 5 ms one-pole
    float ss = eng_strum_s; if (ss < 0.05f) ss = 0.05f;
    strum_env *= 1.0f - kblock(ss / 3.0f, n);
    if (strum_env < 1e-4f) strum_env = 0.0f;
    strum_a += 0.33f * (strum_env - strum_a);
    eng_strum_env = strum_env;
    float strum_mul = 1.0f + 1.5f * strum_a;
    // BOW: the hand's gain follows fast and rings down over RING; NEAR FAR's detune glides over about 0.2 s
    float hand = clampf(eng_hand, 0.0f, 1.0f);
    float ka = kblock(0.01f, n), kr = kblock(fmaxf(eng_ring_s, 0.02f) / 3.0f, n);
    det_s += 0.01f * (clampf(eng_detune_hz, -20.0f, 20.0f) - det_s);
    // the glide
    float gl = eng_glide_s; if (gl < 0.05f) gl = 0.05f;
    if (gl != glide_last) { glide_last = gl; kp = kblock(gl / 3.0f, n); }
    // the sources' clocks
    wheel_ph += clampf(eng_wheel_hz, 0.05f, 10.0f) * (float)n / (float)AUDIO_SR;
    if (wheel_ph >= 1.0f) { wheel_ph -= 1.0f; if (src == SRC_GURDY && dense_s > 0.5f) { float a = (dense_s - 0.5f) * 1.6f; if (a > buzz_env) buzz_env = a; } }
    swell_ph = wrap1(swell_ph + 0.45f * (float)n / (float)AUDIO_SR);
    eng_beat_phase = wrap1(eng_beat_phase + beat_s * (float)n / (float)AUDIO_SR);
    if (dw_wave_want != dw_wave_cur) {
        dw_wave_busy = 1;
        wave_xf += 0.04f;
        if (wave_xf >= 1.0f) { dw_wave_cur = dw_wave_want; wave_xf = 0.0f; dw_wave_busy = 0; }
    }
    // TANPURA: the strings plucked in turn
    if (src == SRC_TANPURA && sw_state == SW_IDLE) {
        pluck_wait -= n;
        if (pluck_wait <= 0) {
            float ps = eng_pluck_s; if (ps < 0.1f) ps = 0.1f;
            pluck_wait += (int)(ps * (float)AUDIO_SR);
            for (int k = 0; k < NV; k++) {
                slot_t *s = &S[pluck_k]; pluck_k = (pluck_k + 1) % NV;
                if (s->lv > 0.02f || eng_slot_lvl[pluck_k ? pluck_k - 1 : NV - 1] > 0.0f) { s->sexc = s->sexc_len = (int)s->sd; s->sexc_a = 0.25f; s->spk = 0.0f; break; }
            }
        }
    }
    // the slots
    memset(bus, 0, (size_t)n * sizeof(float));
    float lv_sum = 0.0f;
    for (int v = 0; v < NV; v++) {
        slot_t *s = &S[v];
        float hz_t = eng_slot_hz[v], lvl_t = clampf(eng_slot_lvl[v], 0.0f, 1.0f);
        if (!(hz_t > HZ_MIN)) lvl_t = 0.0f;
        else if (hz_t != s->hz_t) { s->hz_t = hz_t; s->lg_t = log2f(hz_t); }
        bool hidden = lay_g[v / NVL] < 0.01f;                          // its layer is all but silent: no one hears it arrive
        if (s->lv < 0.02f || hidden) s->lg = s->lg_t;                    // near silence: the new pitch at once
        else s->lg += kp * (s->lg_t - s->lg);
        if (hidden) s->lv = lvl_t; else s->lv += kp * (lvl_t - s->lv);
        if (s->lv < 1e-5f && lvl_t == 0.0f) s->lv = 0.0f;
        s->hz = fabsf(s->lg - s->lg_t) < 1e-6f ? s->hz_t : exp2_prec(s->lg);
        eng_slot_hz_now[v] = s->lv > 0.001f ? s->hz : 0.0f;
        eng_slot_lvl_now[v] = s->lv;
        lv_sum += s->lv;
        float ht = (1.0f - hand) + hand * clampf(eng_slot_hand[v], 0.0f, 1.6f);
        s->hg += (ht > s->hg ? ka : kr) * (ht - s->hg);
        s->bri += 0.2f * (hand * clampf(eng_slot_bright[v], -1.0f, 1.5f) - s->bri);
        s->sub += 0.1f * (hand * clampf(eng_slot_sub[v], 0.0f, 1.0f) - s->sub);
        float g1 = s->lv * V_GAIN * strum_mul * mute_s * lay_g[v / NVL] * s->hg;
        if (src == SRC_GURDY) g1 *= 1.0f - 0.1f * (0.5f + 0.5f * sin_turns(wheel_ph));
        if (src == SRC_SHRUTI) g1 *= 1.0f - 0.35f * breath_s * (0.5f + 0.5f * sin_turns(swell_ph));
        if (src == SRC_BOW) { s->bow += 0.04f * (rng_bi(&s->rng) - s->bow); g1 *= 1.0f + 0.06f * s->bow; }
        float g0 = s->g; s->g = g1;
        if (g1 < 1e-6f && g0 < 1e-6f) { if (s->lv == 0.0f) for (int c = 0; c < NCOPY; c++) s->cg[c] = 0.0f; continue; }
        float sb[AUDIO_BLOCK];
        if (!slot_render(s, sb, n, src, strum_mul)) continue;
        float g = g0, dg = (g1 - g0) / (float)n;
        for (int i = 0; i < n; i++) { bus[i] += g * sb[i]; g += dg; }
    }
    if (src == SRC_REED || src == SRC_SHRUTI || src == SRC_BOW || src == SRC_GURDY) slot_body(&BODY, bus, n, src, body_s);
    // the sources' extras on the bus: the shruti box's air, the gurdy's trompette
    if (src == SRC_SHRUTI) {
        float a1 = 0.3f * breath_s * V_GAIN * (lv_sum > 1.0f ? 1.0f : lv_sum) * mute_s, a = air_s, da = (a1 - a) / (float)n;
        for (int i = 0; i < n; i++) { bus[i] += a * svf_bp(&air_bp, rng_bi(&rng), NULL); a += da; }
        air_s = a1;
    } else air_s = 0.0f;
    if (src == SRC_GURDY && buzz_env > 1e-4f) {
        float low = 0.0f;
        for (int v = 0; v < NV; v++) if (S[v].lv > 0.02f && (low == 0.0f || S[v].hz < low)) low = S[v].hz;
        float inc = (low > 0.0f ? low : 65.0f) / (float)AUDIO_SR, e = buzz_env;
        for (int i = 0; i < n; i++) {
            buzz_ph = wrap1(buzz_ph + inc);
            float sq = buzz_ph < 0.5f ? 1.0f : -1.0f;
            bus[i] += 2.5f * V_GAIN * mute_s * e * svf_bp(&buzz_bp, sq * (0.6f + 0.4f * rng_bi(&rng)), NULL);
            e *= 0.99875f;
        }
        buzz_env = e;
    }
    // the difference tone of the two lowest sounding slots
    {
        float fa = 0.0f, fb = 0.0f, la = 0.0f, lb = 0.0f;
        for (int v = 0; v < NV; v++) {
            slot_t *s = &S[v];
            if (s->lv < 0.02f) continue;
            if (fa == 0.0f || s->hz < fa) { fb = fa; lb = la; fa = s->hz; la = s->lv; }
            else if (fb == 0.0f || s->hz < fb) { fb = s->hz; lb = s->lv; }
        }
        float fd = fb > 0.0f ? fabsf(fa - fb) : 0.0f;
        float want = fd >= 20.0f ? clampf(eng_diff, 0.0f, 1.0f) * 1.2f * V_GAIN * (la < lb ? la : lb) * mute_s : 0.0f;
        if (fd >= 20.0f) { if (diff_lvl_s < 1e-5f) diff_hz_s = fd; else diff_hz_s += 0.04f * (fd - diff_hz_s); }
        float l0 = diff_lvl_s, dl = (want - l0) * 0.0392f, inc = diff_hz_s / (float)AUDIO_SR;
        diff_lvl_s = l0 + dl;
        if (diff_lvl_s > 1e-6f || l0 > 1e-6f) {
            float g = l0, dg = dl / (float)n;
            for (int i = 0; i < n; i++) { diff_ph = wrap1(diff_ph + inc); bus[i] += g * sin_turns(diff_ph); g += dg; }
        } else diff_lvl_s = 0.0f;
        eng_diff_hz = diff_lvl_s > 1e-4f ? diff_hz_s : 0.0f;
    }
    // the energy's VCA (30 ms), then the interference: its bursts on the bus, the drone chopped under them
    {
        float v0 = vca_s;
        vca_s += kblock(0.03f, n) * (clampf(eng_vca, 0.0f, 1.5f) - vca_s);
        float dv = (vca_s - v0) / (float)n, g = v0, gate = clampf(eng_zap_gate, 0.0f, 1.0f);
        if (zaps_take()) {
            float zpk = zaps_run(n, audio_frames(), &rng);
            eng_zap_env = zpk > eng_zap_env ? zpk : eng_zap_env * 0.97f;
            for (int i = 0; i < n; i++) { bus[i] = bus[i] * g * (1.0f - gate * zenv[i]) + zbuf[i]; g += dv; }
        } else {
            eng_zap_env *= 0.97f;
            for (int i = 0; i < n; i++) { bus[i] *= g; g += dv; }
        }
    }
    // a shake's grit: noise rung through band passes an octave over the tones that sound, so even the grit is in tune
    grit_s += 0.1f * (clampf(eng_grit, 0.0f, 1.0f) - grit_s);
    if (grit_s > 0.001f || grit_lv > 1e-5f) {
        int L = eng_layer & 1, nb = 0;
        float hzs[NVL];
        for (int k = 0; k < NVL; k++) { const slot_t *t = &S[L * NVL + k]; if (t->lv > 0.05f) hzs[nb++] = 2.0f * (t->hz + det_s); }
        for (int k = 0; k < nb; k++) if (fabsf(hzs[k] - grit_hz[k]) > 0.5f) { grit_hz[k] = hzs[k]; svf_set(&grit_bp[k], hzs[k], 14.0f); }
        float a0 = grit_lv, a1 = grit_s * 2.0f * (nb ? 1.0f / sqrtf((float)nb) : 0.0f), da = (a1 - a0) / (float)n;
        for (int i = 0; i < n; i++) {
            float x = rng_bi(&rng), y = 0.0f;
            for (int k = 0; k < nb; k++) y += svf_bp(&grit_bp[k], x, NULL);
            bus[i] += a0 * y;
            a0 += da;
        }
        grit_lv = a1;
        for (int k = 0; k < NVL; k++) { grit_bp[k].ic1 = flush(grit_bp[k].ic1); grit_bp[k].ic2 = flush(grit_bp[k].ic2); }
    }
    // the dirt: up to +32 dB into the clip, the negative side clipping up to twice as late; the clip's ceiling holds the level, so a
    // crescendo into the dirt grows by about 10 dB, not 30
    {
        drive_s += 0.0392f * (clampf(eng_drive, 0.0f, 1.0f) - drive_s);
        dmix_s += 0.0392f * (clampf(eng_dmix, 0.0f, 1.0f) - dmix_s);
        float tn = clampf(eng_tone, 0.0f, 1.0f);
        if (fabsf(tn - tone_last) > 0.002f) { tone_last = tn; tone_k = onepole_hz(800.0f * fast_exp2(tn * 4.32f)); }
        float pre = 1.0f + 40.0f * drive_s * drive_s, G = 2.5f * pre;
        float o = 0.4f * (1.0f + 0.25f * drive_s);                   // unity while it is clean; flat out, peaks near 0.5
        float m = dmix_s * smoothstep(0.0f, 0.12f, drive_s + 0.5f * grit_s);
        drive_run(bus, n, G, 1.0f - 0.5f * drive_s, o, m);
        eng_drive_now = drive_s;
    }
    // the echo
    if (echo_buf) {
        float dt = clampf(eng_echo_s, 0.02f, 1.6f) * (float)AUDIO_SR;
        float d1 = echo_d + kblock(0.25f, n) * (dt - echo_d);
        efb_s += 0.0392f * (clampf(eng_echo_fb, 0.0f, 0.97f) - efb_s);
        emix_s += 0.0392f * (clampf(eng_echo_mix, 0.0f, 1.0f) - emix_s);
        float dry1, wet1; xfade_gains(emix_s, &dry1, &wet1);
        echo_run(bus, n, d1, efb_s, wet1, dry1);
    }
    // the hall
    float t60 = 0.4f * fast_exp2(tail_s * 5.6439f);                   // 0.4 s .. 20 s
    if (fabsf(t60 - hall_t60) > 0.01f * t60) {
        hall_t60 = t60;
        for (int k = 0; k < HALL_N; k++) hall_g[k] = fast_exp2(-9.966f * (float)hall_len[k] / (t60 * (float)AUDIO_SR));
        wet_norm = 0.6f / sqrtf(1.0f + 0.6f * t60);
    }
    float dg0 = dry_g, wg0 = wet_g;
    xfade_gains(space_s, &dry_g, &wet_g);
    hall_run(bus, wet, wg0 * wet_norm, wet_g * wet_norm, n);
    // the room's feedback: the mic through band passes on the tones that sound (each as loud as its tone and its bow)
    {
        float fhz[NVL], fwt[NVL]; int nb = 0, L = eng_layer & 1;
        for (int k = 0; k < NVL; k++) { const slot_t *t = &S[L * NVL + k]; if (t->lv > 0.05f && t->hz > HZ_MIN) { fhz[nb] = t->hz + det_s; fwt[nb] = t->lv * t->hg; nb++; } }
        howl_run(in, bus, hbuf, n, fhz, fwt, nb);
    }
    // out: dry + wet, LEVEL x REST x the duck, then the room pass's playback already in out[]
    float og1 = level_s * rest_s * sw_g, og0 = out_g; out_g = og1;
    {
        float d = dg0, dd = (dry_g - dg0) / (float)n, g = og0, dgg = (og1 - og0) / (float)n;
        float swp = sw_g_prev, dsw = (sw_g - sw_g_prev) / (float)n;
        (void)swp; (void)dsw;
        for (int i = 0; i < n; i++) { out[i] += (d * bus[i] + wet[i] + hbuf[i]) * g; d += dd; g += dgg; }
    }
    float pk = 0.0f;
    for (int i = 0; i < n; i++) { float a = fabsf(out[i]); if (a > pk) pk = a; }
    if (!(pk == pk) || pk > 1e6f) {                                   // a NaN or a blow-up: silence, and start the hall over
        memset(out, 0, (size_t)n * sizeof(float));
        for (int k = 0; k < HALL_N; k++) { memset(hall_buf[k], 0, (size_t)hall_len[k] * sizeof(float)); hall_lp[k] = 0.0f; }
        for (int v = 0; v < NV; v++) slot_reset_filters(&S[v]);
        if (echo_buf) memset(echo_buf, 0, ECHO_N * sizeof(float));
        echo_lp = dr_u1 = dr_dcx = dr_dcy = dr_lp = 0.0f;
        pk = 0.0f;
    }
    eng_out_lvl = pk > eng_out_lvl ? pk : eng_out_lvl * 0.98f;
}
