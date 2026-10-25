// AIRDWELLER's audio task: the grid, the trigger and strike queues, the scheduling (a hit at frame `at` plays in the
// block that holds it, at its offset), the loops' playback and the played report (AIR's), then the sound: the kit
// (inst_drums.c), the material (inst_material.c) and the bus. inst_ad.h is the contract; inst_eng.h joins the three.
//
// The bus: drums + material -> DRIVE (DWELL's asymmetric cubic clip with first-order ADAA and a DC blocker, wet/dry;
// the clip's out-gain holds a full kick near where it was, so DRIVE squeezes rather than swells) -> TONE (a 12 dB low
// pass, 800 Hz .. 16 kHz) -> ECHO (DWELL's: PSRAM, its time gliding like tape, a dark saturating feedback, equal-power
// mix) -> ROOM (DWELL's four-line hall, 0.3 .. 6 s, equal-power) -> LEVEL -> CUT (a 3 ms linear fade to silence and
// back). Every gain is ramped across the block; filters and delays move at block rate.
//
// Memory: the echo 203 KB in PSRAM (app_alloc), the hall 33 KB in internal RAM (app_alloc_fast), the voices and the
// material about 6 KB static.
#include "inst_eng.h"
#include "spsc.h"
#include <string.h>

// ---------------------------------------------------------------- the contract's variables
loop_t eng_loops[NLOOP];
volatile int   eng_run = 1;
volatile int   eng_role, eng_pad_voice[NPAD] = { DV_HAT, DV_KICK, DV_SNAP, DV_CLANG }, eng_mat_chord, eng_cut, eng_voices_on;
volatile float eng_pad_tune[NPAD], eng_kick_hz = 55.0f, eng_drum_decay = 1.0f, eng_squeeze;
volatile float eng_mat_morph, eng_mat_hz = 220.0f, eng_mat_ring = 2.0f, eng_mat_bright = 0.5f, eng_bend, eng_bow, eng_creak, eng_mat_level = 1.0f, eng_duck;
volatile float eng_drive, eng_tone = 0.6f, eng_echo_s = 0.3f, eng_echo_fb, eng_echo_mix, eng_room, eng_room_size = 0.5f, eng_level = 0.8f;
volatile float eng_out_lvl, eng_mat_lvl, eng_mode_amp[8];

// ---------------------------------------------------------------- the queues
SPSC(trigq, trig_t, 32)
SPSC(playq, played_t, 64)
typedef struct { uint32_t at; int vel, voice; } strike_t;
SPSC(strkq, strike_t, 32)
static trigq_t trigs;
static playq_t played;
static strkq_t strikes;
void eng_trigger(const trig_t *t) { trigq_push(&trigs, t); }
bool eng_played_pop(played_t *p) { return playq_pop(&played, p); }
void eng_strike(uint32_t at, int vel, int voice) { strike_t s = { at, vel, voice }; strkq_push(&strikes, &s); }
#define NPEND 32
typedef struct { uint32_t at; int pad, vel, src, voice; bool strike; } pend_t;   // waiting for its frame
static pend_t pend[NPEND]; static int npend;

static uint32_t frame0;

// ---------------------------------------------------------------- the grid (a seqlock: the UI writes, any task reads)
static grid_t grid_v = { 19200, 0, 0.0f };
static volatile uint32_t grid_seq;
void eng_set_grid(const grid_t *g)
{
    __atomic_store_n(&grid_seq, grid_seq + 1, __ATOMIC_RELEASE);
    grid_v = *g;
    __atomic_store_n(&grid_seq, grid_seq + 1, __ATOMIC_RELEASE);
}
void eng_get_grid(grid_t *g)
{
    uint32_t s0, s1;
    do { s0 = __atomic_load_n(&grid_seq, __ATOMIC_ACQUIRE); *g = grid_v; s1 = __atomic_load_n(&grid_seq, __ATOMIC_ACQUIRE); } while (s0 != s1 || (s0 & 1));
}
int64_t eng_floor_div(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }
int64_t eng_tick_at(const grid_t *g, uint32_t frame)
{
    if (!g->fpb) return 0;
    int64_t rel = (int32_t)(frame - g->origin);
    return eng_floor_div(rel * PPB - (int64_t)(g->frac * (float)PPB), (int64_t)g->fpb);
}
uint32_t eng_frame_of(const grid_t *g, int64_t tick)
{
    int64_t f = eng_floor_div(tick * (int64_t)g->fpb + PPB / 2, PPB);
    return g->origin + (uint32_t)(int32_t)f + (uint32_t)(int32_t)(g->frac + 0.5f);
}
uint16_t eng_hit_tick(const loop_t *l, const hit_t *h)
{
    int t = h->tick;
    if ((t % (PPB / 2)) == PPB / 4) t += (int)(pv[P_SWING] * 0.01f * (float)(PPB / 4));
    int len = l->bars * BAR_TICKS;
    if (len > 0 && t >= len) t -= len;
    return (uint16_t)t;
}

// ---------------------------------------------------------------- a hit or a strike on its frame
// DRUM: the pad's drum. PAINT (and PUMP, from a loop): the pad strikes the material. The other hand's strikes ring the
// material in PAINT and PUMP and are ignored in DRUM. Every pad hit is reported (eng_played_pop).
static void fire(const pend_t *p, int d)
{
    if (p->strike) { if (eng_role != ROLE_DRUM) mat_strike_voice(p->voice, p->vel, d); return; }
    if (eng_role == ROLE_DRUM) kit_hit(p->voice, p->vel, eng_pad_tune[p->pad & 3], d);
    else mat_strike_pad(p->pad, p->vel, d);
    played_t pl = { frame0 + (uint32_t)d, (uint8_t)p->pad, (uint8_t)p->vel, (uint8_t)p->src, (uint8_t)p->voice };
    playq_push(&played, &pl);
}
static void schedule(uint32_t at, int pad, int vel, int src, int voice, bool strike)
{
    pend_t p = { at, pad, vel, src, voice, strike };
    int32_t d = (int32_t)(at - frame0);
    if (at == 0 || d < 0) { fire(&p, 0); return; }
    if (npend < NPEND) pend[npend++] = p;
}

// ---------------------------------------------------------------- the loops' playback (AIR's)
static void loop_block(int n)
{
    if (!eng_run) return;
    grid_t g; eng_get_grid(&g);
    if (!g.fpb) return;
    int64_t t_lo = eng_tick_at(&g, frame0 - 1) + 1, t_hi = eng_tick_at(&g, frame0 + (uint32_t)n - 1);
    if (t_hi < t_lo) return;
    for (int64_t t = t_lo; t <= t_hi; t++) {
        uint32_t at = eng_frame_of(&g, t);
        for (int k = 0; k < NLOOP; k++) {
            loop_t *l = &eng_loops[k];
            int bars = l->bars, nh = l->n;
            if (!bars || !l->on || !nh) continue;
            int len = bars * BAR_TICKS;
            int64_t since = t - l->start;
            int rel = (int)(since - eng_floor_div(since, len) * len);
            int32_t cycle = (int32_t)eng_floor_div(since, len);
            for (int i = 0; i < nh; i++) {
                const hit_t *h = &l->hits[i];
                if (eng_hit_tick(l, h) != rel || h->born == cycle) continue;
                schedule(at, h->pad, h->vel, SRC_LOOP, eng_pad_voice[h->pad], false);
            }
        }
    }
}

// ---------------------------------------------------------------- the bus
#define ECHO_N 52000                  // 1.625 s (PSRAM)
#define HALL_N 4
static const int hall_len[HALL_N] = { 1559, 1877, 2203, 2617 };
static float *echo_buf, *hall_buf[HALL_N];
static int   echo_w, hall_w[HALL_N];
static float echo_d = 0.3f * (float)AUDIO_SR, echo_lp, efb_s, emix_s, e_dry = 1.0f, e_wet;
static float hall_lp[HALL_N], hall_g[HALL_N], hall_t60 = -1.0f, wet_norm = 0.5f, room_s, size_s = 0.5f, dry_g = 1.0f, wet_g, send_g;
static float dr_u1, dr_dcx, dr_dcy, dr_G = 1.0f, dr_bias = 1.0f, dr_mix, dr_out = 1.0f, drive_s;
static svf_t tone_f; static float tone_s = -1.0f, tone_fc = -1.0f;
static float sq_s, level_s, cut_g = 1.0f;

// the hall: four lines, a damping one-pole each, Householder feedback; in -> w (DWELL's)
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
    hall_lp[0] = flushf(l0); hall_lp[1] = flushf(l1); hall_lp[2] = flushf(l2); hall_lp[3] = flushf(l3);
}

// the drive: the cubic soft clip x - x^3/3 (flat at 2/3 past 1) and its antiderivative, for first-order ADAA (DWELL's)
// (branch free: a branch on the sign mispredicts on audio)
static inline float clip3(float x) { float c = fminf(fmaxf(x, -1.0f), 1.0f); return c - c * c * c * (1.0f / 3.0f); }
static inline float clip3_F(float x)
{
    float a = fabsf(x), c = fminf(a, 1.0f), c2 = c * c;
    return 0.5f * c2 - c2 * c2 * (1.0f / 12.0f) + 0.6666667f * (a - c);
}
// asymmetric: the negative side through k times the gain (k < 1 clips it later and higher): even harmonics. ik = 1 / k
static inline float neg_of(float u) { return 0.5f - 0.5f * copysignf(1.0f, u); }          // 1 below 0, else 0
static inline float clipa(float u, float k, float ik) { float s = neg_of(u), kk = 1.0f + (k - 1.0f) * s; return clip3(kk * u) * (1.0f + (ik - 1.0f) * s); }
static inline float clipa_F(float u, float k, float ik) { float s = neg_of(u), kk = 1.0f + (k - 1.0f) * s, ii = 1.0f + (ik - 1.0f) * s; return clip3_F(kk * u) * ii * ii; }
// in place: in-gain G, the asymmetric clip (k) with ADAA, a DC blocker for what the asymmetry leaves, out-gain o,
// clean and driven mixed by m; all four ramp across the block
static IRAM_ATTR __attribute__((noinline)) void drive_run(float *restrict x, int n, float G1, float k1, float o1, float m1)
{
    if (m1 <= 0.0f && dr_mix <= 0.0f) { dr_G = G1; dr_bias = k1; dr_out = o1; return; }   // clean: nothing to do
    float G = dr_G, dG = (G1 - G) / (float)n, k = dr_bias, dk = (k1 - k) / (float)n, o = dr_out, dO = (o1 - o) / (float)n;
    float ik = 1.0f / k, dik = (1.0f / k1 - ik) / (float)n;
    float m = dr_mix, dm = (m1 - m) / (float)n, u1 = dr_u1, dcx = dr_dcx, dcy = dr_dcy;
    float F1 = clipa_F(u1, k, ik);                                   // the antiderivative at the last sample (k moves slowly)
    for (int i = 0; i < n; i++) {
        float clean = x[i];
        float u = clean * G;
        float du = u - u1, Fu = clipa_F(u, k, ik);
        bool far = fabsf(du) > 1e-4f;
        float ya = (Fu - F1) / (far ? du : 1.0f), yb = clipa(0.5f * (u + u1), k, ik);
        float y = far ? ya : yb;
        u1 = u; F1 = Fu; ik += dik;
        float h = y - dcx + 0.9985f * dcy; dcx = y; dcy = h;
        x[i] = clean + m * (h * o - clean);
        G += dG; k += dk; o += dO; m += dm;
    }
    dr_G = G1; dr_bias = k1; dr_out = o1; dr_mix = m1;
    dr_u1 = u1; dr_dcx = flushf(dcx); dr_dcy = flushf(dcy);
}
// in place: the echo. Its time glides (tape), what comes round is darkened and saturated, then mixed in (DWELL's)
static IRAM_ATTR __attribute__((noinline)) void echo_run(float *restrict x, int n, float d1, float wet1, float dry1)
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
        buf[w] = flushf(x[i] + fb * 0.5f * soft_tanh(2.0f * lp));    // and never more than half scale coming round
        if (++w >= ECHO_N) w = 0;
        x[i] = x[i] * dry + e * wet;
        d += dd; wet += dw; dry += dy;
    }
    echo_d = d1; e_wet = wet1; e_dry = dry1; echo_lp = flushf(lp); echo_w = w;
}
static IRAM_ATTR __attribute__((noinline)) void tone_run(float *restrict x, int n)
{
    for (int i = 0; i < n; i++) x[i] = svf_lp(&tone_f, x[i]);
    tone_f.ic1 = flushf(tone_f.ic1); tone_f.ic2 = flushf(tone_f.ic2);
}

static void bus_clear(void)
{
    if (echo_buf) memset(echo_buf, 0, ECHO_N * sizeof(float));
    for (int k = 0; k < HALL_N; k++) { if (hall_buf[k]) memset(hall_buf[k], 0, (size_t)hall_len[k] * sizeof(float)); hall_lp[k] = 0.0f; }
    echo_lp = dr_u1 = dr_dcx = dr_dcy = 0.0f;
    tone_f.ic1 = tone_f.ic2 = 0.0f;
}

void eng_init(void)
{
    kit_init();
    mat_init();
    echo_buf = app_alloc(ECHO_N * sizeof(float));                    // read and written in step, once a sample: PSRAM is fine
    for (int k = 0; k < HALL_N; k++) hall_buf[k] = app_alloc_fast((size_t)hall_len[k] * sizeof(float));
    if (!echo_buf) app_log("AIRDWELLER: out of PSRAM (no echo)");
    if (!hall_buf[0] || !hall_buf[1] || !hall_buf[2] || !hall_buf[3]) {
        app_log("AIRDWELLER: out of internal RAM (no room)");
        for (int k = 0; k < HALL_N; k++) { app_free(hall_buf[k]); hall_buf[k] = NULL; }
    }
    svf_set(&tone_f, 8000.0f, 0.7071f);                              // (k = 1 / Q stays; svf_fc moves the cutoff)
    level_s = eng_level;
}

void eng_audio(const float *in, float *out, int n)
{
    (void)in;
    memset(out, 0, (size_t)n * sizeof(float));
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    frame0 = audio_frames();
    trig_t t;
    while (trigq_pop(&trigs, &t)) schedule(t.at, t.pad, t.vel, t.src, eng_pad_voice[t.pad & 3], false);
    strike_t s;
    while (strkq_pop(&strikes, &s)) schedule(s.at, -1, s.vel, SRC_REMOTE, s.voice, true);
    loop_block(n);
    for (int i = 0; i < npend;) {
        int32_t d = (int32_t)(pend[i].at - frame0);
        if (d < n) { fire(&pend[i], d < 0 ? 0 : d); pend[i] = pend[--npend]; } else i++;
    }

    // the sources
    float bus[AUDIO_BLOCK], wet[AUDIO_BLOCK];
    for (int i = 0; i < n; i++) bus[i] = 0.0f;
    sq_s += kblock(0.03f, n) * (clampf(eng_squeeze, 0.0f, 1.0f) - sq_s);
    eng_voices_on = kit_run(bus, n, sq_s);
    mat_run(bus, n, eng_role != ROLE_DRUM);

    // DRIVE: 1 .. 29x into the clip; the out-gain keeps a full kick (0.5) near 0.5 however hard it is pushed
    drive_s += kblock(0.03f, n) * (clampf(eng_drive, 0.0f, 1.0f) - drive_s);
    {
        float d = drive_s, G = 1.0f + 3.0f * d + 25.0f * d * d * d;
        float o = 0.5f / clip3(0.5f * G), m = smoothstep(0.0f, 0.12f, d);
        drive_run(bus, n, G, 1.0f - 0.5f * d, o, m);
    }
    // TONE: 800 Hz .. 16 kHz, 12 dB an octave
    {
        float tn = clampf(eng_tone, 0.0f, 1.0f);
        if (tone_s < 0.0f) tone_s = tn;
        tone_s += kblock(0.03f, n) * (tn - tone_s);
        float fc = 800.0f * fast_exp2(4.321928f * tone_s);
        if (fabsf(fc - tone_fc) > 0.004f * fc) { tone_fc = fc; svf_fc(&tone_f, fc); }
        tone_run(bus, n);
    }
    // ECHO
    if (echo_buf) {
        float dt = clampf(eng_echo_s, 0.02f, 1.6f) * (float)AUDIO_SR;
        float d1 = echo_d + kblock(0.25f, n) * (dt - echo_d);
        efb_s += kblock(0.03f, n) * (clampf(eng_echo_fb, 0.0f, 0.95f) - efb_s);
        emix_s += kblock(0.03f, n) * (clampf(eng_echo_mix, 0.0f, 1.0f) - emix_s);
        float dry1, wet1; xfade_gains(emix_s, &dry1, &wet1);
        echo_run(bus, n, d1, wet1, dry1);
    }
    // ROOM: 0.3 s .. 6 s
    float dg0 = dry_g;
    room_s += kblock(0.03f, n) * (clampf(eng_room, 0.0f, 1.0f) - room_s);
    size_s += kblock(0.05f, n) * (clampf(eng_room_size, 0.0f, 1.0f) - size_s);
    if (hall_buf[0]) {
        float t60 = 0.3f * fast_exp2(4.321928f * size_s);
        if (fabsf(t60 - hall_t60) > 0.01f * t60) {
            hall_t60 = t60;
            for (int k = 0; k < HALL_N; k++) hall_g[k] = fast_exp2(-9.966f * (float)hall_len[k] / (t60 * (float)AUDIO_SR));
            wet_norm = 0.6f * rsqrt_f(1.0f + 0.6f * t60);
        }
        float s0 = send_g;
        xfade_gains(room_s, &dry_g, &wet_g);
        send_g = wet_g * wet_norm;
        hall_run(bus, wet, s0, send_g, n);
    } else { dry_g = 1.0f; for (int i = 0; i < n; i++) wet[i] = 0.0f; }
    // LEVEL, CUT
    float l0 = level_s;
    level_s += kblock(0.03f, n) * (clampf(eng_level, 0.0f, 1.0f) - level_s);
    float ct = eng_cut ? 0.0f : 1.0f, cg = cut_g;
    const float cstep = 1.0f / (0.003f * (float)AUDIO_SR);
    float dd = (dry_g - dg0) / (float)n, dr = dg0, l = l0, dl = (level_s - l0) / (float)n, pk = 0.0f;
    for (int i = 0; i < n; i++) {
        if (cg < ct) { cg += cstep; if (cg > ct) cg = ct; } else if (cg > ct) { cg -= cstep; if (cg < ct) cg = ct; }
        float o = (dr * bus[i] + wet[i]) * l * cg;
        out[i] = o;
        float a = fabsf(o); if (a > pk) pk = a;
        dr += dd; l += dl;
    }
    cut_g = cg;
    if (!(pk == pk) || pk > 1e4f) {                                   // a NaN or a blow-up: silence, and start over
        memset(out, 0, (size_t)n * sizeof(float));
        kit_silence(); mat_silence(); bus_clear();
        pk = 0.0f;
    }
    eng_out_lvl = pk > eng_out_lvl ? pk : eng_out_lvl * 0.98f;
}
