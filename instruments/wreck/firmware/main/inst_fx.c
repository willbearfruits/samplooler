// FX (inst_fx.h): the drums' insert and the master.
//
// fx_drums(), in place on the drums, in this order:
//   crush    a sample and hold stepping at RATE (32 kHz down to 3 kHz), then a mid-tread quantizer (16 bits down to
//            4). The hold's rate and the step glide sample by sample, so moving them never zippers. No anti-aliasing:
//            the images are the point
//   drive    WARM (a cubic soft clip), TORN (an asymmetric hard clip with a bias) or FOLD (a triangle folder), each
//            anti-aliased by its antiderivative (ADAA: the curve's mean between two samples, worked out so that it
//            keeps its precision however hard it is driven), up to +33 dB in with a make-up after that keeps a hit
//            about as loud, then a DC blocker. At 0 the dry signal (ADAA is a slight low pass). A new type crossfades
//            in over 12 ms
//   filter   a state-variable (TPT) low or high pass with resonance, off at its ends (low pass at 16 kHz, high pass at
//            40 Hz); then DARK, a second low pass that closes towards DRONE
// The axis: the crush is whole at BREAKCORE and fades between HARDCORE and SLUDGE to DOOM CRUSH of itself; the drive
// is at 55 % at BREAKCORE, whole at HARDCORE and goes to DOOM DRIVE of itself at DRONE; DARK closes from SLUDGE on.
// perf.crush and perf.drive (the stick) push them towards the full.
//
// fx_master(), in place on the mix; the send feeds the reverb:
//   the tape    the mix and the send are written to a 2 s loop (int16 pairs, 256 KB of PSRAM) and read back by a
//               head that moves at the tape's speed (a 4-point Hermite read). Wow (0.55 Hz and a slow drift) and
//               flutter (7.3 and 11.9 Hz), growing from SLUDGE to DRONE, are a small extra delay on the head (never
//               ahead of time: at most 7 ms). The transport:
//                 RUN    the speed follows a motor (80 ms). Tipped back it goes down to 1 - 0.75 x DRAG at full tilt;
//                        flat, it runs fast to catch up (up to 1.6x, easing in: a critically damped loop) and then
//                        sits exactly two frames behind the write head, sample for sample: in time
//                 BRAKE  the speed falls to 0 in a straight line over the brake's beats; the level fades with it
//                        below a quarter speed; then STOP: silence, the head waits at the write head
//                 SPIN   the speed rises from where it is along s0 + a u + b u^2 + c u^3 (u: 0..1 over the spin),
//                        reaching 1 with no slope; its area is chosen so the head lands back in time exactly when the
//                        spin ends (from a stop it overshoots to 1.25x halfway)
//   the reverb  at 16 kHz (half the rate: [1 2 1] / 4 in, linear interpolation out; its tail is damped above 8 kHz
//               anyway, and the same memory holds twice the time). A pre-delay (8 to 36 ms), a low cut (90 Hz) and a
//               high cut, four allpass diffusers, an eight-line feedback delay network (Hadamard). Four lines keep
//               their length; four are read by Hermite taps that drift (0.53 to 1.37 Hz, up to 10 frames: at most 9
//               cents) so long tails do not ring metallic, and shorten to half towards BREAKCORE (a smaller room).
//               Each line's gain sets the decay: ROOM's time at BREAKCORE, HALL's at DRONE, between them along the
//               axis; DAMP makes the top decay faster. The input comes down as the time grows (rt^-0.3), so a
//               cathedral is louder than a room but does not pile up. A 1e-18 guard at its input keeps every value
//               out of the subnormal range. Lines, diffusers and pre-delay in internal RAM: 36.3 KB
//   glue        a soft saturation on everything
// Every level and tone is smoothed per block and ramped per sample; states that decay are flushed (no denormals).
#include "wreck.h"
#include <string.h>

// ---------------------------------------------------------------- params
enum { FP_CRUSH, FP_RATE, FP_DRIVE, FP_DTYPE, FP_FILTER, FP_RES, FP_FTYPE, FP_DARK, FP_DCRUSH, FP_DDRIVE,
       FP_ROOM, FP_HALL, FP_REVERB, FP_DAMP, FP_WOW, FP_FLUTTER, FP_DRAG, FP_BRAKE, FP_GLUE, FP_N };
static const char *const pages[] = { "FX", "FX 2" };
static const char *const dtype_l[] = { "WARM", "TORN", "FOLD" };
static const char *const ftype_l[] = { "LOW PASS", "HIGH PASS" };
static const param_t params[FP_N] = {
    [FP_CRUSH]   = { "CRUSH", "fcru", 0, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "FEWER BITS: 16 DOWN TO 4" },
    [FP_RATE]    = { "RATE", "frat", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "LOWER RATE, DOWN TO 3 KHZ" },
    [FP_DRIVE]   = { "DRIVE", "fdrv", 0, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "DISTORTION AT HARDCORE" },
    [FP_DTYPE]   = { "DRIVE TYPE", "fdty", 0, PK_ENUM, 0, 0, 2, 1, 0, "", dtype_l, "WARM, TORN OR FOLDED" },
    [FP_FILTER]  = { "FILTER", "ffil", 0, PK_NUM, PF_LOG, 40, 16000, 1.06f, 16000, "Hz", NULL, "THE CUTOFF; AT THE END: OFF" },
    [FP_RES]     = { "RESONANCE", "fres", 0, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "A PEAK AT THE CUTOFF" },
    [FP_FTYPE]   = { "FILTER TYPE", "fftp", 0, PK_ENUM, 0, 0, 1, 1, 0, "", ftype_l, "LOW PASS OR HIGH PASS" },
    [FP_DARK]    = { "DARK", "fdrk", 0, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "DARKER DRUMS AT DRONE" },
    [FP_DCRUSH]  = { "DOOM CRUSH", "fdcr", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE CRUSH LEFT AT DRONE" },
    [FP_DDRIVE]  = { "DOOM DRIVE", "fddr", 0, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "THE DRIVE LEFT AT DRONE" },
    [FP_ROOM]    = { "ROOM", "froo", 1, PK_NUM, PF_LOG, 0.2f, 4, 1.06f, 0.8f, "s", NULL, "ROOM DECAY AT BREAKCORE" },
    [FP_HALL]    = { "HALL", "fhal", 1, PK_NUM, PF_LOG, 2, 15, 1.06f, 10, "s", NULL, "CATHEDRAL DECAY AT DRONE" },
    [FP_REVERB]  = { "REVERB", "frev", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW LOUD THE ROOM IS" },
    [FP_DAMP]    = { "DAMP", "fdmp", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "A DARKER, SOFTER TAIL" },
    [FP_WOW]     = { "WOW", "fwow", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SLOW TAPE WOBBLE AT DRONE" },
    [FP_FLUTTER] = { "FLUTTER", "fflu", 1, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "FAST TAPE WOBBLE AT DRONE" },
    [FP_DRAG]    = { "DRAG", "fdrg", 1, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "TAPE SPEED TIPPED BACK" },
    [FP_BRAKE]   = { "BRAKE", "fbrk", 1, PK_NUM, PF_INT, 25, 200, 5, 100, "%", NULL, "HOW LONG A STOP TAKES" },
    [FP_GLUE]    = { "GLUE", "fglu", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "SOFT SATURATION ON ALL" },
};
// each sound's FX: the defaults are WRECK's
static const char *const sounds[WR_NSOUNDS] = {
    "",                                                                                         // WRECK
    "fcru=10 frat=15 fdrv=20 fdrk=35 fddr=10 froo=0.6 fhal=9 frev=50 fdmp=30 fwow=25 fflu=20 fglu=20",   // SNARES
    "fcru=30 frat=65 fdrv=25 fdrk=15 fddr=10 froo=0.5 fhal=7 frev=45 fdmp=15 fwow=15 fflu=25 fglu=20",   // DRILL
    "fcru=10 frat=10 fdrv=85 fdty=1 fdrk=30 fddr=85 froo=0.5 fhal=6 frev=35 fdmp=60 fwow=20 fflu=30 fglu=60",   // GABBA
    "fcru=65 frat=55 fdrv=75 fdty=1 fdrk=60 fdcr=20 fddr=60 froo=0.4 fhal=7 frev=35 fdmp=60 fwow=35 fglu=50",   // GRIND
    "fcru=25 frat=30 fdrv=40 fdrk=40 fddr=25 froo=1 fhal=12 frev=60 fdmp=45 fwow=25 fflu=20",            // BAROQUE
    "fcru=15 frat=20 fdrv=55 fdrk=65 fddr=50 froo=0.9 fhal=14 frev=75 fdmp=60 fwow=80 fflu=45 fglu=40",  // WIZARD
    "fcru=0 frat=0 fdrv=15 fdrk=45 fddr=15 froo=0.3 fhal=4 frev=45 fdmp=25 fwow=40 fflu=30 fglu=20",     // EARTH
    "fcru=70 frat=70 fdrv=70 fdty=2 fdrk=85 fdcr=10 fddr=40 froo=1.2 fhal=15 frev=80 fdmp=70 fwow=55 fflu=40 fglu=45",   // DRONE
    "fcru=20 frat=25 fdrv=50 fdty=1 fdrk=75 fddr=45 froo=0.5 fhal=15 frev=70 fdmp=75 fwow=30 fflu=20 fglu=45",   // THUNDER
};
#define P(i) MP(M_FX, (i))

// ---------------------------------------------------------------- sizes and constants
#define TAPE_N     65536u                  // frames of tape: 2.05 s, mix and send as int16: 256 KB of PSRAM
#define TAPE_M     (TAPE_N - 1u)
#define TAPE_REST  2                       // the head's place at rest, frames behind the write head (the read's reach)
#define TAPE_Q     16384.0f                // int16 per unit: +-2 fits
#define TAPE_WALL  ((float)TAPE_N - 6144.0f)   // the furthest behind the tape may fall
#define DRAG_MAX   0.75f                   // DRAG 100 %, full tilt: a quarter speed
#define CATCH_MAX  0.6f                    // catching up: at most 1.6x
#define CATCH_K    3.0f                    // ... and 3x the lag (in seconds) faster when near: with the motor, critical
#define MOTOR_K    0.0247f                 // the motor: 80 ms per 64-frame block
#define WOW_HZ     0.55f
#define WOW_MAX    0.012f                  // WOW 100 % at DRONE: +-1.2 % of pitch (plus the drift)
#define FLUT_HZ1   7.3f
#define FLUT_HZ2   11.9f
#define FLUT_MAX   0.004f                  // FLUTTER 100 %: +-0.4 %
#define RSR        16000                   // the reverb runs at half the rate: its tail is damped above 8 kHz anyway
#define NL         8
#define LINE_N     1024u                   // every line's ring (16 kHz frames; one write index for all), +3 mirrored
#define LINE_M     (LINE_N - 1u)
#define NDIF       4
#define PRE_N      640                     // pre-delay ring at 16 kHz: up to 40 ms
#define GUARD      1e-18f                  // added to the network's input: nothing in it can ever become subnormal
#define REV_CAL    1.75f                   // REVERB 50 %: the room comes back at the send's level at BREAKCORE (test_fx.c)

#define NFIX       4                       // lines 0..3 keep their length; 4..7 drift and take the room's size
static const int   line_len[NL] = { 557, 613, 683, 751, 809, 877, 941, 997 };   // 16 kHz frames (at full size), primes
static const float lfo_hz[NL] = { 0, 0, 0, 0, 0.53f, 0.79f, 1.07f, 1.37f };
static const float inj_sign[NL] = { 1, -1, 1, -1, 1, -1, 1, -1 };
static const float out_sign[NL] = { 1, 1, -1, -1, 1, 1, -1, -1 };
static const int   dif_len[NDIF] = { 61, 83, 127, 163 };
static const float dif_g[NDIF] = { 0.65f, 0.65f, 0.55f, 0.55f };

enum { T_RUN, T_BRAKE, T_STOP, T_SPIN };
enum { REQ_BRAKE = 1, REQ_SPIN = 2 };

struct fx_engine {
    bool     d_ready;                      // fx_drums has run: its smoothers have their first values
    uint32_t rng;
    // ---- drums: crush
    float sh_ph, sh_v;                     // the sample and hold
    float c_bits, c_inc, c_q, c_iq;        // bits and the hold's step (smoothed), the quantizer's step and its inverse
    // drive
    float d_amt;                           // the amount (smoothed)
    float d_w[3];                          // the types' weights
    float a_x1;                            // ADAA: the last input
    float dc_x1, dc_y1;
    // filter and DARK
    float f_l2, f_k, f_h, f_mix, f_a[3], f_ic1, f_ic2, f_done[2];   // f_done: the cutoff and k the coefficients are for
    float k_l2, k_mix, k_a[3], k_ic1, k_ic2, k_done;
    // ---- the tape
    int16_t *tape;
    uint32_t tw;                           // frames written (the index is tw & TAPE_M)
    uint32_t tpos;                         // the head, 16.16 frames: the 65536-frame tape wraps with the 32 bits
    float    spd;
    int      mode;
    int32_t  m_t, m_T;                     // the brake or spin: frames done, frames long
    float    m_s0, m_a, m_b, m_c;
    uint32_t m_seq;                        // the brake's request
    float    dw;                           // the wow and flutter's delay at the block's end
    float    wow_d, flut_d, wow_ph, wow_r, wow_rt, fl_ph1, fl_ph2;
    int32_t  wow_cnt;
    // ---- the reverb
    float  *line[NL];
    uint32_t lw;                           // the lines' write index
    float   lz[NL], md[NL], lph[NL], g[NL], hf[NL];
    float  *dif[NDIF];
    int     dw_[NDIF];
    float  *pre;
    int     pw;
    float   dec_x1, y_prev;                // the 2:1 decimator's last frame, the network's last output
    float   pd, rt, size, hk, k_inlp, in_hp, in_lp, rt_in, wet, glue;
    float   c_room, c_hall, c_lr, c_lh, c_rt, c_damp;   // what the block-rate libm was last worked out for
    bool    nomod;
    // ---- requests: the UI task writes them (a seqlock), the audio task takes them at a block's start
    volatile uint32_t req_seq;
    volatile int      req_kind;
    volatile float    req_beats;
    volatile uint32_t stop_seq;            // the request whose brake reached 0
    uint32_t req_seen;
    // ---- for the screen
    volatile float v_speed, v_crush, v_drive, v_room;
};
typedef struct fx_engine fxe_t;
static fxe_t live;
static fxe_t *E = &live;

static void engine_reset(fxe_t *e)
{
    e->d_ready = false; e->rt = 0.0f; e->rng = 0x2545F491u;
    e->spd = 1.0f; e->mode = T_RUN; e->tw = 0; e->tpos = (uint32_t)(-TAPE_REST) << 16;
    e->stop_seq = 0xFFFFFFFFu;             // never a request's number: not stopped
    for (int k = 0; k < NL; k++) e->lph[k] = (float)k * 0.137f;
    e->wow_ph = 0.3f;
}

static bool engine_alloc(fxe_t *e)
{
    // the tape: two streams through a long buffer, fine in PSRAM. The reverb's lines are read all over every sample:
    // internal RAM (in PSRAM their cache slots collide: SING's reverb took the whole 2 ms block that way)
    e->tape = app_alloc((size_t)TAPE_N * 2u * sizeof(int16_t));
    bool ok = true;
    for (int k = 0; k < NL; k++) { e->line[k] = app_alloc_fast((LINE_N + 3u) * sizeof(float)); ok = ok && e->line[k]; }
    for (int j = 0; j < NDIF; j++) { e->dif[j] = app_alloc_fast((size_t)dif_len[j] * sizeof(float)); ok = ok && e->dif[j]; }
    e->pre = app_alloc_fast(PRE_N * sizeof(float));
    ok = ok && e->pre;
    if (!ok) {
        for (int k = 0; k < NL; k++) { app_free(e->line[k]); e->line[k] = NULL; }
        for (int j = 0; j < NDIF; j++) { app_free(e->dif[j]); e->dif[j] = NULL; }
        app_free(e->pre); e->pre = NULL;
    }
    return e->tape && ok;
}

// ---------------------------------------------------------------- small pieces
static inline float lerp_t(float a, float b, float t) { return a + (b - a) * t; }
static inline float tape_lag(const struct fx_engine *e)   // frames behind time (0: in time)
{
    return (float)((e->tw << 16) - e->tpos) * (1.0f / 65536.0f) - (float)TAPE_REST;
}
static inline void tape_rest(struct fx_engine *e) { e->tpos = (e->tw - TAPE_REST) << 16; }
static inline float hermite(float p0, float p1, float p2, float p3, float f)   // between p1 (f 0) and p2 (f 1)
{
    float c1 = 0.5f * (p2 - p0), c2 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3, c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
    return ((c3 * f + c2) * f + c1) * f + p1;
}

// The drive's curves, each with unit slope at 0, and their antiderivative means between two inputs (ADAA, first
// order): D(x0, x1) = (F(x1) - F(x0)) / (x1 - x0), worked out per region so that nearby inputs lose no precision.
// WARM: x - x^3/3 inside +-1, +-2/3 outside
static inline float warm_f(float x) { return x > 1.0f ? 0.6666667f : x < -1.0f ? -0.6666667f : x - x * x * x * 0.33333333f; }
static inline float warm_F(float x) { float a = fabsf(x); return a > 1.0f ? 0.6666667f * a - 0.25f : 0.5f * x * x - x * x * x * x * 0.083333333f; }
static inline float warm_D(float x0, float x1)
{
    float a0 = fabsf(x0), a1 = fabsf(x1);
    if (a0 <= 1.0f && a1 <= 1.0f) { float s = x0 + x1; return 0.5f * s - s * (x0 * x0 + x1 * x1) * 0.083333333f; }
    if (x0 > 1.0f && x1 > 1.0f) return 0.6666667f;
    if (x0 < -1.0f && x1 < -1.0f) return -0.6666667f;
    float d = x1 - x0;
    return fabsf(d) > 1e-5f ? (warm_F(x1) - warm_F(x0)) / d : warm_f(0.5f * (x0 + x1));
}
// TORN: clip(x + b) between LO and HI, minus b (the DC blocker takes what the asymmetry leaves)
#define TORN_B  0.18f
#define TORN_LO -0.62f
#define TORN_HI 1.0f
static inline float clip_C(float u) { return u < TORN_LO ? TORN_LO * u - 0.5f * TORN_LO * TORN_LO : u > TORN_HI ? TORN_HI * u - 0.5f * TORN_HI * TORN_HI : 0.5f * u * u; }
static inline float torn_D(float x0, float x1)
{
    float u0 = x0 + TORN_B, u1 = x1 + TORN_B, c;
    bool in0 = u0 >= TORN_LO && u0 <= TORN_HI, in1 = u1 >= TORN_LO && u1 <= TORN_HI;
    if (in0 && in1) c = 0.5f * (u0 + u1);
    else if (u0 > TORN_HI && u1 > TORN_HI) c = TORN_HI;
    else if (u0 < TORN_LO && u1 < TORN_LO) c = TORN_LO;
    else {
        float d = u1 - u0;
        c = fabsf(d) > 1e-5f ? (clip_C(u1) - clip_C(u0)) / d : clampf(0.5f * (u0 + u1), TORN_LO, TORN_HI);
    }
    return c - TORN_B;
}
// FOLD: a triangle folder: x inside +-1, folded back beyond (period 4). Its antiderivative is a chain of parabolas between
// -1/2 and 0, bounded however hard it is driven, so the difference keeps its precision
static inline float fold_F(float x)          // p = (x + 1) mod 4: p^2/2 - p below 2, 3p - p^2/2 - 4 above; as one parabola in |p - 2|
{
    float u = (x + 1.0f) * 0.25f;
    int k = (int)u; k -= (float)k > u;
    float w = fabsf((u - (float)k) * 4.0f - 2.0f), c = 1.0f - w;                // w = |p - 2|, 0..2
    float sg = (float)(((u - (float)k) < 0.5f) * 2 - 1);                      // + below p = 2, - above (no branch)
    return sg * (0.5f * c * c - 0.5f);
}
static inline float fold_f(float x)
{
    float u = (x + 1.0f) * 0.25f;
    int k = (int)u; if ((float)k > u) k--;
    float p = (u - (float)k) * 4.0f;
    return p < 2.0f ? p - 1.0f : 3.0f - p;
}
static inline float fold_D(float x0, float x1)
{
    if (x0 >= -1.0f && x0 <= 1.0f && x1 >= -1.0f && x1 <= 1.0f) return 0.5f * (x0 + x1);
    float d = x1 - x0;
    return fabsf(d) > 1e-4f ? (fold_F(x1) - fold_F(x0)) / d : fold_f(0.5f * (x0 + x1));
}

// ---------------------------------------------------------------- the drums
void fx_drums(float *io, int n)
{
    fxe_t *e = E;
    if (n <= 0) return;
    float ax = perf.axis, inv = 1.0f / (float)n;
    // ---- the crush: whole at BREAKCORE, fading by SLUDGE to DOOM CRUSH of itself; the stick pushes it
    float keep_c = lerp_t(1.0f, P(FP_DCRUSH) * 0.01f, smoothstep(0.15f, 0.6f, ax));
    float pc = clampf(perf.crush, 0.0f, 1.0f);
    float bits_a = 1.0f - (1.0f - P(FP_CRUSH) * 0.01f * keep_c) * (1.0f - pc);
    float rate_a = 1.0f - (1.0f - P(FP_RATE) * 0.01f * keep_c) * (1.0f - 0.9f * pc);
    bits_a = clampf(bits_a, 0.0f, 1.0f); rate_a = clampf(rate_a, 0.0f, 1.0f);
    float bits_t = 16.0f - 12.0f * (bits_a > 0.0f ? fast_exp2(0.5849625f * log2f(bits_a)) : 0.0f);   // 50 %: 8 bits
    float inc_t = fast_exp2(-3.4150375f * rate_a);                                                 // 100 %: 3 kHz
    // ---- the drive: 55 % at BREAKCORE, whole at HARDCORE, DOOM DRIVE of itself at DRONE
    float prof = ax < 0.375f ? lerp_t(0.55f, 1.0f, smoothstep(0.05f, 0.375f, ax))
                             : lerp_t(1.0f, P(FP_DDRIVE) * 0.01f, smoothstep(0.375f, 0.85f, ax));
    float drv_t = 1.0f - (1.0f - clampf(P(FP_DRIVE) * 0.01f * prof, 0.0f, 1.0f)) * (1.0f - clampf(perf.drive, 0.0f, 1.0f));
    int dtype = clampi((int)(P(FP_DTYPE) + 0.5f), 0, 2);
    // ---- the filter, and DARK
    float res = clampf(P(FP_RES) * 0.01f, 0.0f, 1.0f);
    float k_t = 1.0f / (0.707f * fast_exp2(4.0857f * res));                // Q 0.707 .. 12
    float l2_t = log2f(clampf(P(FP_FILTER), 40.0f, 16000.0f)), h_t = P(FP_FTYPE) >= 0.5f ? 1.0f : 0.0f;
    float kl2_t = 13.965784f - 4.5f * clampf(P(FP_DARK) * 0.01f, 0.0f, 1.0f) * smoothstep(0.45f, 1.0f, ax);   // 16 kHz down
    if (!e->d_ready) {
        e->c_bits = bits_t; e->c_inc = inc_t; e->d_amt = drv_t; e->f_l2 = l2_t; e->f_k = k_t; e->f_h = h_t; e->k_l2 = kl2_t;
        for (int t = 0; t < 3; t++) e->d_w[t] = t == dtype ? 1.0f : 0.0f;
    }
    // block-end values (smoothed), the per-sample ramps run from the last block's
    float bits1 = e->c_bits + (bits_t - e->c_bits) * 0.12f, inc1 = e->c_inc + (inc_t - e->c_inc) * 0.12f;
    float q0 = e->c_q, iq0 = e->c_iq, q1 = fast_exp2(1.0f - bits1), iq1 = fast_exp2(bits1 - 1.0f), inc0 = e->c_inc;
    if (!e->d_ready) { q0 = q1; iq0 = iq1; inc0 = inc1; }
    bool crush_on = bits1 < 15.9f || e->c_bits < 15.9f || inc1 < 0.9995f || inc0 < 0.9995f;
    e->c_bits = bits1; e->c_inc = inc1; e->c_q = q1; e->c_iq = iq1;

    float amt0 = e->d_amt, amt1 = amt0 + (drv_t - amt0) * 0.1f;
    e->d_amt = amt1;
    float g0 = fast_exp2(5.5f * amt0), g1 = fast_exp2(5.5f * amt1);            // up to 45x (+33 dB)
    // the make-up: 1 / the gain while the curve is linear, levelling off as it saturates (a -12 dBFS hit stays about as loud)
    float mk0 = (1.0f + 0.38f * g0) / (1.38f * g0), mk1 = (1.0f + 0.38f * g1) / (1.38f * g1);
    float m0 = smoothstep(0.0f, 0.06f, amt0), m1 = smoothstep(0.0f, 0.06f, amt1);
    float w0[3], w1[3];
    for (int t = 0; t < 3; t++) {
        float tg = t == dtype ? 1.0f : 0.0f;
        w0[t] = e->d_w[t];
        w1[t] = w0[t] + clampf(tg - w0[t], -0.17f, 0.17f);                    // 12 ms across
        e->d_w[t] = w1[t];
    }
    bool drive_on = m0 > 0.0f || m1 > 0.0f;
    int one = -1;                                                              // one curve, not crossfading: that one
    for (int t = 0; t < 3; t++) if (w0[t] == 1.0f && w1[t] == 1.0f) one = t;

    float k0 = e->f_k;
    e->f_l2 += (l2_t - e->f_l2) * 0.15f; e->f_k += (k_t - e->f_k) * 0.15f;
    if (fabsf(l2_t - e->f_l2) < 1e-4f) e->f_l2 = l2_t;                        // arrived: the coefficients stay as they are
    if (fabsf(k_t - e->f_k) < 1e-5f) e->f_k = k_t;
    float k1 = e->f_k;
    float h0 = e->f_h; e->f_h += clampf(h_t - e->f_h, -0.17f, 0.17f);
    float fc = fast_exp2(e->f_l2);
    // the filter is off at its ends: a low pass at the top, a high pass at the bottom
    float mix_t = (1.0f - e->f_h) * (1.0f - smoothstep(11000.0f, 15500.0f, fc)) + e->f_h * smoothstep(40.5f, 52.0f, fc);
    float fm0 = e->d_ready ? e->f_mix : mix_t;
    e->f_mix = mix_t;
    bool filt_on = fm0 > 0.0f || mix_t > 0.0f;
    if (!filt_on) { e->f_ic1 = e->f_ic2 = 0.0f; }
    else if (!e->d_ready || e->f_l2 != e->f_done[0] || k1 != e->f_done[1]) {    // the coefficients, once a block while it moves
        float g = tanf(3.14159265f * clampf(fc, 10.0f, 0.45f * (float)AUDIO_SR) / (float)AUDIO_SR);
        e->f_a[0] = 1.0f / (1.0f + g * (g + k1)); e->f_a[1] = g * e->f_a[0]; e->f_a[2] = g * e->f_a[1];
        e->f_done[0] = e->f_l2; e->f_done[1] = k1;
    }
    // a resonant peak is not let get much louder than the rest
    float cmp0 = 1.0f / (1.0f + 0.04f * (1.0f / k0 - 0.707f)), cmp1 = 1.0f / (1.0f + 0.04f * (1.0f / k1 - 0.707f));

    e->k_l2 += (kl2_t - e->k_l2) * 0.05f;
    if (fabsf(kl2_t - e->k_l2) < 1e-4f) e->k_l2 = kl2_t;
    float kc = fast_exp2(e->k_l2);
    float km_t = 1.0f - smoothstep(11000.0f, 15500.0f, kc);
    float km0 = e->d_ready ? e->k_mix : km_t;
    e->k_mix = km_t;
    bool dark_on = km0 > 0.0f || km_t > 0.0f;
    if (dark_on && (!e->d_ready || e->k_l2 != e->k_done)) {
        float g = tanf(3.14159265f * clampf(kc, 10.0f, 0.45f * (float)AUDIO_SR) / (float)AUDIO_SR);
        e->k_a[0] = 1.0f / (1.0f + g * (g + 1.5384615f)); e->k_a[1] = g * e->k_a[0]; e->k_a[2] = g * e->k_a[1];   // Q 0.65
        e->k_done = e->k_l2;
    }
    if (!dark_on) { e->k_ic1 = e->k_ic2 = 0.0f; }
    e->d_ready = true;

    e->v_crush = bits_a > rate_a ? bits_a : rate_a;
    e->v_drive = amt1;

    // the filters' coefficients are set once a block (their cutoffs glide, so each step is small)
    const float fa1 = e->f_a[0], fa2 = e->f_a[1], fa3 = e->f_a[2], ka1 = e->k_a[0], ka2 = e->k_a[1], ka3 = e->k_a[2];
    float ph = e->sh_ph, hv = e->sh_v, ax1 = e->a_x1, dx1 = e->dc_x1, dy1 = e->dc_y1, fF0 = fold_F(e->a_x1);
    float ic1 = e->f_ic1, ic2 = e->f_ic2, jc1 = e->k_ic1, jc2 = e->k_ic2, xc = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * inv, x = io[i];
        if (crush_on) {
            ph += inc0 + (inc1 - inc0) * t;
            if (ph >= 1.0f) { ph -= 1.0f; hv = x; }
            float v = hv * (iq0 + (iq1 - iq0) * t);
            int r = (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
            x = (float)r * (q0 + (q1 - q0) * t);
        }
        xc = x;
        if (drive_on) {
            float xd = x * (g0 + (g1 - g0) * t), d;
            if (one == 2) {                                                    // the folder, its antiderivative carried over
                float F1 = fold_F(xd), dd = xd - ax1;
                d = fabsf(dd) > 1e-4f ? (F1 - fF0) / dd : fold_f(0.5f * (ax1 + xd));   // (no branch on the region: noise
                                                                                     // would mispredict it every other frame)
                fF0 = F1;
            } else if (one >= 0) d = one == 0 ? warm_D(ax1, xd) : torn_D(ax1, xd);
            else {
                float wa = w0[0] + (w1[0] - w0[0]) * t, wb = w0[1] + (w1[1] - w0[1]) * t, wc = w0[2] + (w1[2] - w0[2]) * t;
                d = 0.0f;
                if (wa > 0.0f) d += wa * warm_D(ax1, xd);
                if (wb > 0.0f) d += wb * torn_D(ax1, xd);
                if (wc > 0.0f) d += wc * fold_D(ax1, xd);
            }
            ax1 = xd;
            float yd = d * (mk0 + (mk1 - mk0) * t);
            float o = yd - dx1 + 0.995f * dy1; dx1 = yd; dy1 = o;
            x += (m0 + (m1 - m0) * t) * (o - x);
        }
        if (filt_on) {
            float v3 = x - ic2, v1 = fa1 * ic1 + fa2 * v3, v2 = ic2 + fa2 * ic1 + fa3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            float hp = x - k1 * v1 - v2, h = h0 + (e->f_h - h0) * t;
            float yf = (v2 + h * (hp - v2)) * (cmp0 + (cmp1 - cmp0) * t);
            x += (fm0 + (mix_t - fm0) * t) * (yf - x);
        }
        if (dark_on) {
            float v3 = x - jc2, v1 = ka1 * jc1 + ka2 * v3, v2 = jc2 + ka2 * jc1 + ka3 * v3;
            jc1 = 2.0f * v1 - jc1; jc2 = 2.0f * v2 - jc2;
            x += (km0 + (km_t - km0) * t) * (v2 - x);
        }
        io[i] = x;
    }
    if (!drive_on) { ax1 = xc * g1; dx1 = dy1 = 0.0f; }
    e->sh_ph = ph; e->sh_v = hv; e->a_x1 = ax1;
    e->dc_x1 = flush_denormal(dx1); e->dc_y1 = flush_denormal(dy1);
    e->f_ic1 = flush_denormal(ic1); e->f_ic2 = flush_denormal(ic2); e->k_ic1 = flush_denormal(jc1); e->k_ic2 = flush_denormal(jc2);
}

// ---------------------------------------------------------------- the tape's transport
static void take_request(fxe_t *e)
{
    uint32_t s1 = __atomic_load_n(&e->req_seq, __ATOMIC_ACQUIRE);
    if ((s1 & 1u) || s1 == e->req_seen) return;
    int kind = e->req_kind; float beats = e->req_beats;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&e->req_seq, __ATOMIC_RELAXED) != s1) return;
    e->req_seen = s1;
    float fpb = (float)(song.fpb ? song.fpb : 19200u);
    if (kind == REQ_BRAKE) {
        float fr = clampf(beats, 0.0f, 16.0f) * clampf(P(FP_BRAKE), 25.0f, 200.0f) * 0.01f * fpb;
        int32_t T = ((int32_t)fr + AUDIO_BLOCK - 1) / AUDIO_BLOCK * AUDIO_BLOCK;
        if (T < 2 * AUDIO_BLOCK) T = 2 * AUDIO_BLOCK;                          // at least 4 ms: never a cut
        e->m_seq = s1;
        if (e->mode == T_STOP) { e->stop_seq = s1; return; }                   // already still
        e->mode = T_BRAKE; e->m_T = T; e->m_t = 0; e->m_s0 = e->spd;
    } else if (kind == REQ_SPIN) {
        float fr = clampf(beats, 0.0f, 16.0f) * fpb;
        int32_t T = ((int32_t)fr + AUDIO_BLOCK - 1) / AUDIO_BLOCK * AUDIO_BLOCK;
        if (T < 2 * AUDIO_BLOCK) T = 2 * AUDIO_BLOCK;
        float s0 = e->spd, lagE = 0.0f;
        if (e->mode == T_STOP || s0 < 0.001f) { tape_rest(e); s0 = 0.0f; }
        else {
            float lag = tape_lag(e);
            lagE = clampf(lag / (float)T, 0.0f, 0.5f);                         // more than that: the motor catches up after
        }
        // s(u) = s0 + a u + b u^2 + c u^3: s(1) = 1, s'(1) = 0, and its mean over the spin 1 + lag / T
        float A = 1.0f - s0, I = 1.0f + lagE - s0;
        e->m_c = 12.0f * I - 8.0f * A; e->m_b = -A - 2.0f * e->m_c; e->m_a = 2.0f * A + e->m_c;
        e->m_s0 = s0; e->m_T = T; e->m_t = 0; e->mode = T_SPIN; e->spd = s0;
    }
}

static void request(int kind, float beats)
{
    fxe_t *e = E;
    __atomic_add_fetch(&e->req_seq, 1u, __ATOMIC_ACQ_REL);                    // odd: being written
    e->req_kind = kind; e->req_beats = beats;
    __atomic_add_fetch(&e->req_seq, 1u, __ATOMIC_ACQ_REL);
}
void fx_brake(float beats) { request(REQ_BRAKE, beats); }
void fx_spin(float beats) { request(REQ_SPIN, beats); }
bool fx_stopped(void) { return __atomic_load_n(&E->req_seq, __ATOMIC_ACQUIRE) == E->stop_seq; }

void fx_view(fx_view_t *v)
{
    v->speed = E->v_speed; v->crush = E->v_crush; v->drive = E->v_drive; v->room = E->v_room;
}

// ---------------------------------------------------------------- the master: the tape, the reverb, the glue
void fx_master(float *io, const float *send, int n)
{
    fxe_t *e = E;
    if (n <= 0 || !e->tape) return;
    float inv = 1.0f / (float)n, ax = perf.axis;
    bool first = e->rt <= 0.0f;                                                // the first block: smoothers start at their targets
    take_request(e);

    // ---- the transport: the speed at this block's end (RUN), or the brake's and spin's curves (per sample)
    int mode = e->mode;
    float s0 = e->spd, s1 = s0;
    if (mode == T_RUN) {
        float lag = tape_lag(e);
        float drag = clampf(perf.drag, 0.0f, 1.0f), hold = smoothstep(0.0f, 0.1f, drag);
        float lag_s = lag * (1.0f / (float)AUDIO_SR);
        float catchup = CATCH_K * lag_s / (1.0f + CATCH_K * fabsf(lag_s) * (1.0f / CATCH_MAX));
        float target = (1.0f - hold) * (1.0f + catchup) + hold * (1.0f - DRAG_MAX * clampf(P(FP_DRAG) * 0.01f, 0.0f, 1.0f) * drag);
        float wall = clampf((lag - (TAPE_WALL - 16000.0f)) * (1.0f / 16000.0f), 0.0f, 1.0f);   // the tape's end: no slower
        if (wall > 0.0f && target < 1.02f) target += (1.02f - target) * wall;
        s1 = s0 + (target - s0) * MOTOR_K;
        if (hold == 0.0f && fabsf(lag) < 0.05f && fabsf(s1 - 1.0f) < 1e-4f && fabsf(target - 1.0f) < 1e-4f) {
            s0 = s1 = 1.0f; tape_rest(e);                                      // in time: sample for sample
        }
    } else if (mode == T_STOP) {
        tape_rest(e); s0 = s1 = 0.0f;
    }
    // ---- wow and flutter: a small delay on the head, growing from SLUDGE to DRONE
    float wa = smoothstep(0.25f, 1.0f, ax);
    float wow_t = clampf(P(FP_WOW) * 0.01f, 0.0f, 1.0f) * wa * WOW_MAX * (float)AUDIO_SR / (TWO_PI * WOW_HZ);
    float flu_t = clampf(P(FP_FLUTTER) * 0.01f, 0.0f, 1.0f) * wa * FLUT_MAX * (float)AUDIO_SR / (TWO_PI * FLUT_HZ1);
    if (first) { e->wow_d = wow_t; e->flut_d = flu_t; }
    e->wow_d += (wow_t - e->wow_d) * 0.0013f;                                  // 1.5 s: never a pitch dip of its own
    e->flut_d += (flu_t - e->flut_d) * 0.0067f;
    e->wow_ph = wrap1(e->wow_ph + WOW_HZ * (float)n / (float)AUDIO_SR);
    e->fl_ph1 = wrap1(e->fl_ph1 + FLUT_HZ1 * (float)n / (float)AUDIO_SR);
    e->fl_ph2 = wrap1(e->fl_ph2 + FLUT_HZ2 * (float)n / (float)AUDIO_SR);
    if ((e->wow_cnt -= n) <= 0) { e->wow_cnt = 12000 + (int32_t)(rng_next(&e->rng) & 8191u); e->wow_rt = rng_bi(&e->rng); }
    e->wow_r += (e->wow_rt - e->wow_r) * 0.004f;
    float mw = 0.65f * sin_turns(e->wow_ph) + 0.35f * e->wow_r, mf = 0.7f * sin_turns(e->fl_ph1) + 0.3f * sin_turns(e->fl_ph2);
    float dw0 = e->dw, dw1 = e->wow_d * (1.0f + mw) + e->flut_d * (1.0f + mf);
    if (first) dw0 = dw1;
    e->dw = dw1;

    // ---- the reverb's settings for this block
    bool rev_on = e->line[0] != NULL;
    float cr = smoothstep(0.08f, 0.95f, ax);
    float room = clampf(P(FP_ROOM), 0.1f, 20.0f), hall = clampf(P(FP_HALL), 0.1f, 20.0f);
    if (first || room != e->c_room) { e->c_room = room; e->c_lr = log2f(room); }
    if (first || hall != e->c_hall) { e->c_hall = hall; e->c_lh = log2f(hall); }
    float rt_t = fast_exp2(e->c_lr + (e->c_lh - e->c_lr) * cr);
    float size_t_ = 0.5f + 0.5f * cr, pd_t = 128.0f + 440.0f * cr;    // half size at BREAKCORE; 8 .. 35.5 ms of pre-delay
    float damp = clampf(P(FP_DAMP) * 0.01f, 0.0f, 1.0f);
    float wet_t = clampf(P(FP_REVERB) * 0.01f, 0.0f, 1.0f) * (0.6f + 0.6f * smoothstep(0.3f, 1.0f, ax)) * REV_CAL;
    float glue_t = clampf(P(FP_GLUE) * 0.01f, 0.0f, 1.0f);
    if (first) { e->rt = rt_t; e->size = size_t_; e->pd = pd_t; e->wet = wet_t; e->glue = glue_t; }
    e->rt += (rt_t - e->rt) * 0.03f;
    if (fabsf(rt_t - e->rt) < 1e-5f * rt_t) e->rt = rt_t;
    e->size += (size_t_ - e->size) * 0.0013f;
    e->pd += (pd_t - e->pd) * 0.002f;
    float wet0 = e->wet; e->wet += (wet_t - e->wet) * 0.05f;
    float gl0 = e->glue; e->glue += (glue_t - e->glue) * 0.05f;
    float rtin0 = e->rt_in;
    if (first || e->rt != e->c_rt) {                                           // the level for the time, and the screen's room
        e->c_rt = e->rt;
        float l = log2f(e->rt);
        e->rt_in = fast_exp2(-0.3f * l);
        e->v_room = clampf((l + 2.3219281f) * 0.16055f, 0.0f, 1.0f);         // 0.2 s .. 15 s
        if (first) rtin0 = e->rt_in;
    }
    float ratio = 1.0f - 0.85f * damp;                                         // the top's decay against the middle's
    if (first || damp != e->c_damp) {
        e->c_damp = damp;
        e->hk = 1.0f - fast_exp2(-TWO_PI * 5000.0f * fast_exp2(-1.6f * damp) / (float)RSR * 1.4426950f);   // the shelf: 5 .. 1.6 kHz
        e->k_inlp = 1.0f - fast_exp2(-TWO_PI * 7000.0f * fast_exp2(-1.2f * damp) / (float)RSR * 1.4426950f);
    }
    float depth = e->nomod ? 0.0f : (3.0f + 7.0f * cr) * e->size;            // 16 kHz frames
    float md0[NL], md1[NL];
    float kdec = -9.9657843f / (e->rt * (float)RSR);                          // log2 of 10^-3 per frame of time
    for (int k = 0; k < NL; k++) {
        e->lph[k] = wrap1(e->lph[k] + lfo_hz[k] * (float)n / (float)AUDIO_SR);
        md1[k] = k < NFIX ? (float)line_len[k] : (float)line_len[k] * e->size + depth * sin_turns(e->lph[k]);
        md0[k] = first ? md1[k] : e->md[k];
        e->md[k] = md1[k];
        e->g[k] = fast_exp2(kdec * md1[k]);
        e->hf[k] = fast_exp2(kdec * md1[k] * (1.0f / ratio - 1.0f));
    }

    // ---- pass 1: the tape. Record, then play from the head (minus the wow's delay), then move the head
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    float mixb[AUDIO_BLOCK], sndb[AUDIO_BLOCK];
    {
        int16_t *restrict tape = e->tape;
        uint32_t tw = e->tw, tpos = e->tpos;
        const float bs0 = e->m_s0, ba = e->m_a, bb = e->m_b, bc = e->m_c;
        const float uT = (mode == T_BRAKE || mode == T_SPIN) ? 1.0f / (float)e->m_T : 0.0f, ut0 = ((float)e->m_t + 0.5f) * uT;
        // the speed per frame in 16.16 (RUN: a ramp across the block; BRAKE and SPIN: their curves at each frame's middle)
        int32_t sqv[AUDIO_BLOCK];
        if (mode == T_RUN) {
            int32_t sq = (int32_t)(s0 * 65536.0f + 0.5f), dsq = ((int32_t)(s1 * 65536.0f + 0.5f) - sq) / n;
            for (int i = 0; i < n; i++) { sq += dsq; sqv[i] = sq; }
        } else if (mode == T_BRAKE) {
            for (int i = 0; i < n; i++) { float u = ut0 + (float)i * uT; float s = bs0 * (1.0f - u); sqv[i] = (int32_t)((s > 0.0f ? s : 0.0f) * 65536.0f + 0.5f); }
        } else if (mode == T_SPIN) {
            for (int i = 0; i < n; i++) { float u = ut0 + (float)i * uT; float s = bs0 + u * (ba + u * (bb + u * bc)); sqv[i] = (int32_t)((s > 0.0f ? s : 0.0f) * 65536.0f + 0.5f); }
        } else for (int i = 0; i < n; i++) sqv[i] = 0;
        int32_t wq = (int32_t)(dw0 * 65536.0f), dwq = ((int32_t)(dw1 * 65536.0f) - wq) / n;   // the wow's delay, 16.16
        for (int i = 0; i < n; i++) {
            float s = (float)sqv[i] * (1.0f / 65536.0f);
            wq += dwq;
            float wm = clampf(io[i] * TAPE_Q, -32767.0f, 32767.0f), ws = clampf(send[i] * TAPE_Q, -32767.0f, 32767.0f);
            uint32_t wi = tw & TAPE_M;
            int16_t qm = (int16_t)(wm >= 0.0f ? wm + 0.5f : wm - 0.5f), qs = (int16_t)(ws >= 0.0f ? ws + 0.5f : ws - 0.5f);
            tape[2u * wi] = qm; tape[2u * wi + 1u] = qs;
            float g = s * 4.0f; g = g > 1.0f ? 1.0f : g;                       // the level falls with the speed below 1/4
            float mo = 0.0f, so = 0.0f;
            if (g > 0.0f) {
                uint32_t rpos = tpos - (uint32_t)wq, b = ((rpos >> 16) - 1u) & TAPE_M;   // four frames from one before the head
                const int16_t *p0 = tape + 2u * b, *p1 = p0 + 2, *p2 = p0 + 4, *p3 = p0 + 6;
                if (b > TAPE_M - 3u) { p1 = tape + 2u * ((b + 1u) & TAPE_M); p2 = tape + 2u * ((b + 2u) & TAPE_M); p3 = tape + 2u * ((b + 3u) & TAPE_M); }
                float fr = (float)(rpos & 0xFFFFu) * (1.0f / 65536.0f), f2 = fr * fr, gq = g * (1.0f / TAPE_Q);
                float c0 = fr * (-0.5f + fr * (1.0f - 0.5f * fr)) * gq, c1 = (1.0f + f2 * (-2.5f + 1.5f * fr)) * gq;
                float c2 = fr * (0.5f + fr * (2.0f - 1.5f * fr)) * gq, c3 = f2 * (-0.5f + 0.5f * fr) * gq;
                mo = c0 * (float)p0[0] + c1 * (float)p1[0] + c2 * (float)p2[0] + c3 * (float)p3[0];
                so = c0 * (float)p0[1] + c1 * (float)p1[1] + c2 * (float)p2[1] + c3 * (float)p3[1];
            }
            mixb[i] = mo; sndb[i] = so;
            tw++;
            tpos += (uint32_t)sqv[i];
            uint32_t lagq = (tw << 16) - tpos;
            if (lagq < ((uint32_t)TAPE_REST << 16) || lagq > 0xFFF00000u) tpos = (tw - TAPE_REST) << 16;   // never ahead of time
            else if (lagq > ((TAPE_N - 2048u) << 16)) tpos = (tw - (TAPE_N - 2048u)) << 16;                // never off the tape
        }
        e->tw = tw; e->tpos = tpos;
    }
    // ---- pass 2: the reverb on the tape's send, at 16 kHz: [1 2 1] / 4 and every other frame in, linear
    // interpolation out (half a frame late)
    float wetb[AUDIO_BLOCK];
    if (rev_on) {
        float *restrict pre = e->pre;
        int pw = e->pw, h = n / 2;
        float ihp = e->in_hp, ilp = e->in_lp, kil = e->k_inlp, hk = e->hk, rin0 = rtin0, rin1 = e->rt_in;
        int pdi = (int)(clampf(e->pd, 1.0f, (float)PRE_N - 2.0f) + 0.5f);       // whole frames: it glides slowly, in the input only
        float w0 = wet0, w1 = e->wet, x1p = e->dec_x1, yp = e->y_prev, ih = 1.0f / (float)h;
        float *restrict d0 = e->dif[0], *restrict d1 = e->dif[1], *restrict d2 = e->dif[2], *restrict d3 = e->dif[3];
        int j0 = e->dw_[0], j1 = e->dw_[1], j2 = e->dw_[2], j3 = e->dw_[3];
        float *ln[NL], lz[NL], gg[NL], hf[NL];
        uint32_t lw = e->lw, rp[NL], st[NL];
        for (int k = 0; k < NL; k++) {
            ln[k] = e->line[k]; lz[k] = e->lz[k]; gg[k] = e->g[k]; hf[k] = e->g[k] * e->hf[k];   // the top's gain, all in
            // a drifting line's head, in 1024 frames of 32 bits (22 of fraction: it wraps by itself), one frame before the
            // read point (the Hermite's first frame), from the delay at the block's start; it steps by 1 - the delay's
            // change per frame. A fixed line reads one whole frame
            int D0 = (int)md0[k]; float f0 = md0[k] - (float)D0;
            rp[k] = ((lw - (uint32_t)D0 - 1u) << 22) - (uint32_t)(f0 * 4194304.0f);
            st[k] = (uint32_t)(int32_t)((1.0f - (md1[k] - md0[k]) * ih) * 4194304.0f);
        }
        for (int m = 0; m < h; m++) {
            float t = (float)(m + 1) * ih;
            float xa = sndb[2 * m], xb = sndb[2 * m + 1];
            float r = 0.25f * (x1p + xb) + 0.5f * xa;
            x1p = xb;
            // pre-delay, a low cut, a high cut, the level for the time
            pre[pw] = r;
            int rd = pw - pdi; if (rd < 0) rd += PRE_N;
            r = pre[rd];
            if (++pw == PRE_N) pw = 0;
            ihp += 0.0347f * (r - ihp); r -= ihp;                              // 90 Hz
            ilp += kil * (r - ilp);
            r = ilp * (rin0 + (rin1 - rin0) * t) + GUARD;
            // four diffusers (Schroeder allpasses)
#define DIFF(B, J, LEN, G) { float v = B[J], w = r + (G) * v; B[J] = w; r = v - (G) * w; if (++J == (LEN)) J = 0; }
            DIFF(d0, j0, dif_len[0], dif_g[0]) DIFF(d1, j1, dif_len[1], dif_g[1]) DIFF(d2, j2, dif_len[2], dif_g[2]) DIFF(d3, j3, dif_len[3], dif_g[3])
#undef DIFF
            // the network: every line read by a drifting Hermite tap, damped, mixed (Hadamard), written back
            float y[NL], u[NL];
            _Pragma("GCC unroll 8")
            for (int k = 0; k < NL; k++) {
                float v;
                if (k < NFIX) v = ln[k][(lw - (uint32_t)line_len[k]) & LINE_M];
                else {
                    uint32_t pos = rp[k];
                    rp[k] = pos + st[k];
                    const float *q = ln[k] + (pos >> 22);                      // q[1] and q[2] around the read point
                    float f = (float)(pos & 0x3FFFFFu) * 2.3841858e-7f;
                    v = hermite(q[0], q[1], q[2], q[3], f);
                }
                y[k] = v;
                lz[k] += hk * (v - lz[k]);
                u[k] = gg[k] * lz[k] + hf[k] * (v - lz[k]);                    // the middle decays by g, the top by g hf
            }
            {
                float a0 = u[0] + u[1], a1 = u[0] - u[1], a2 = u[2] + u[3], a3 = u[2] - u[3];
                float a4 = u[4] + u[5], a5 = u[4] - u[5], a6 = u[6] + u[7], a7 = u[6] - u[7];
                float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
                u[0] = b0 + b4; u[1] = b1 + b5; u[2] = b2 + b6; u[3] = b3 + b7; u[4] = b0 - b4; u[5] = b1 - b5; u[6] = b2 - b6; u[7] = b3 - b7;
            }
            float rev = 0.0f;
            uint32_t wi = lw & LINE_M;
            _Pragma("GCC unroll 8")
            for (int k = 0; k < NL; k++) {
                float w = u[k] * 0.35355339f + inj_sign[k] * r;
                ln[k][wi] = w;
                rev += out_sign[k] * y[k];
            }
            if (wi < 3u) for (int k = 0; k < NL; k++) ln[k][wi + LINE_N] = ln[k][wi];   // the mirror: reads never wrap
            lw++;
            float ob = rev * (w0 + (w1 - w0) * t), ab = fabsf(ob);
            if (ab > 0.75f) { float v = (ab - 0.75f) * 1.3333333f, c = 0.75f + 0.75f * v / (1.0f + v); ob = ob < 0.0f ? -c : c; }   // a soft ceiling
            wetb[2 * m] = 0.5f * (yp + ob); wetb[2 * m + 1] = ob;
            yp = ob;
        }
        e->pw = pw; e->lw = lw; e->in_hp = flush_denormal(ihp); e->in_lp = flush_denormal(ilp); e->dec_x1 = x1p; e->y_prev = yp;
        e->dw_[0] = j0; e->dw_[1] = j1; e->dw_[2] = j2; e->dw_[3] = j3;
        for (int k = 0; k < NL; k++) e->lz[k] = flush_denormal(lz[k]);
        if (n & 1) wetb[n - 1] = yp;                                           // (the host's blocks are 64: never)
    } else for (int i = 0; i < n; i++) wetb[i] = 0.0f;
    // ---- pass 3: the mix, the wet, the glue
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) * inv, x = mixb[i] + wetb[i], gl = gl0 + (e->glue - gl0) * t;
        if (gl > 0.0f) { float k = 1.0f + 1.5f * gl; x += gl * (warm_f(x * k) / k - x); }   // a cubic: unity below, rounding the peaks
        io[i] = x;
    }
    // ---- the transport's state for the next block
    if (mode == T_RUN || mode == T_STOP) e->spd = s1;
    else {
        e->m_t += n;
        float u = (float)e->m_t / (float)e->m_T;
        if (mode == T_BRAKE) {
            e->spd = e->m_s0 * (1.0f - u);
            if (e->m_t >= e->m_T) { e->spd = 0.0f; e->mode = T_STOP; e->stop_seq = e->m_seq; }
        } else {
            e->spd = e->m_s0 + u * (e->m_a + u * (e->m_b + u * e->m_c));
            if (e->m_t >= e->m_T) {
                e->spd = 1.0f; e->mode = T_RUN;
                if (fabsf(tape_lag(e)) < 0.5f) tape_rest(e);                    // landed: exactly in time
            }
        }
    }
    e->v_speed = e->spd;
}

// ---------------------------------------------------------------- the module
static void init(void)
{
    engine_reset(&live);
    if (!engine_alloc(&live)) app_log("fx: no memory for the tape or the reverb (tape %s, reverb %s)", live.tape ? "ok" : "none", live.line[0] ? "ok" : "none");
}
static void changed(int p) { (void)p; }   // everything is read and smoothed per block

const module_t mod_fx = { .name = "FX", .letter = 'f', .pages = pages, .npages = 2, .params = params, .nparams = FP_N,
                          .sounds = sounds, .init = init, .changed = changed };

#ifndef ESP_PLATFORM
fx_engine_t *fx_engine_new(void)
{
    fxe_t *e = app_alloc(sizeof *e);
    if (!e) return NULL;
    engine_reset(e);
    engine_alloc(e);
    return e;
}
void fx_engine_free(fx_engine_t *e)
{
    if (!e) return;
    app_free(e->tape);
    for (int k = 0; k < NL; k++) app_free(e->line[k]);
    for (int j = 0; j < NDIF; j++) app_free(e->dif[j]);
    app_free(e->pre);
    if (E == e) E = &live;
    app_free(e);
}
void fx_engine_use(fx_engine_t *e) { E = e ? e : &live; }
float fx_tape_lag(void) { return tape_lag(E); }
static int sub_n(const float *x, int n)
{
    int c = 0;
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &x[i], 4); if ((u & 0x7F800000u) == 0 && (u & 0x007FFFFFu)) c++; }
    return c;
}
int fx_subnormals(void)
{
    fxe_t *e = E;
    int c = 0;
    for (int k = 0; k < NL; k++) if (e->line[k]) c += sub_n(e->line[k], (int)LINE_N + 3);
    for (int j = 0; j < NDIF; j++) if (e->dif[j]) c += sub_n(e->dif[j], dif_len[j]);
    if (e->pre) c += sub_n(e->pre, PRE_N);
    c += sub_n(e->lz, NL) + sub_n(&e->in_hp, 1) + sub_n(&e->in_lp, 1);
    return c;
}
void fx_test_nomod(bool off) { E->nomod = off; }
#endif
