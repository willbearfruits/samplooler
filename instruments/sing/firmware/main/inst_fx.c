// SING's input stage and FX bus (see sing.h).
//
// fx_input(), in place on the mic, in this order:
//   the speaker guard  the box's speaker sits centimetres from its mic. An adaptive notch follows the strongest
//                      tone in the input; a tone that the notch takes almost all of (a sine, not a voice), whose
//                      pitch does not move (a voice always does), that grows while the speaker is loud, for a
//                      quarter of a second, is feedback: one of three narrow cuts goes onto it (40 ms in) and stays
//                      until the room has been quiet for a while. A tone that rings on through its cut, or a fourth,
//                      brings the whole input down instead, and back up once the ringing stops.
//   the gate           opens 12 dB over the noise it has learnt (between -62 and -38 dBFS), closes 6 dB lower
//                      after 100 ms of hold; opens in 2 ms, closes in about 80 ms, down to -40 dB at GATE 1
//   the compressor     RMS, 3:1 at COMP 1 with a 10 dB knee from -30 dBFS, 5 ms attack, 120 ms release, the
//                      make-up keeping a voice at -18 dBFS where it was
//   the de-esser       4.5..10 kHz taken down (to -12 dB at DEESS 1) only in blocks where that band dominates
// Detection is per 64-frame block, every gain is ramped across the block.
//
// fx_bus(), the send's wet added to the output:
//   the echo           int16 in PSRAM (2 s), its time gliding to ECHO TIME like tape (no clicks, a pitch swoop on big
//                      changes), a tone in its loop (dark tape .. bright digital), soft-clipped, ducked while the send
//                      is loud, and the THROW (the roll): more in, longer repeats, louder back; flat again it rings out
//   the reverb         20 ms pre-delay, four diffusers, an eight-line feedback delay network (Hadamard mix), its lines
//                      gently modulated; the decay set for the mid band from REVERB TIME, the top decaying up to four
//                      times faster with a dark TONE; the echo feeds it as on a vocal chain
//   the shimmer        an octave up (two crossfading taps sweeping a 50 ms window) inside the reverb's loop
#include "sing.h"
#include <string.h>

// ---------------------------------------------------------------- params (UI task -> audio task, seqlocks)
static fx_in_params_t pi_ui, pi = { .gate = 0.5f, .comp = 0.5f, .guard = 1.0f, .deess = 0.3f };
static fx_bus_params_t pb_ui, pb = { .echo = 0, .echo_frames = 12000, .echo_fb = 0.35f, .echo_tone = 0.5f, .echo_duck = 0.5f,
                                     .reverb = 0.2f, .reverb_s = 1.8f, .reverb_tone = 0.5f, .shimmer = 0, .throw_ = 0 };
static volatile uint32_t si_seq, sb_seq;
static uint32_t si_seen, sb_seen;

void fx_set_input(const fx_in_params_t *p)
{
    __atomic_add_fetch(&si_seq, 1, __ATOMIC_ACQ_REL); pi_ui = *p; __atomic_add_fetch(&si_seq, 1, __ATOMIC_ACQ_REL);
}
void fx_set_bus(const fx_bus_params_t *p)
{
    __atomic_add_fetch(&sb_seq, 1, __ATOMIC_ACQ_REL); pb_ui = *p; __atomic_add_fetch(&sb_seq, 1, __ATOMIC_ACQ_REL);
}
static void pull_in(void)
{
    uint32_t s = __atomic_load_n(&si_seq, __ATOMIC_ACQUIRE);
    if ((s & 1u) || s == si_seen) return;
    fx_in_params_t t = pi_ui;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&si_seq, __ATOMIC_RELAXED) == s) { pi = t; si_seen = s; }
}
static void pull_bus(void)
{
    uint32_t s = __atomic_load_n(&sb_seq, __ATOMIC_ACQUIRE);
    if ((s & 1u) || s == sb_seen) return;
    fx_bus_params_t t = pb_ui;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (__atomic_load_n(&sb_seq, __ATOMIC_RELAXED) == s) { pb = t; sb_seen = s; }
}

static inline float db2lin(float db) { return fast_exp2(db * 0.16609640f); }          // 10^(db/20)
static inline float lin2db(float x) { return 6.0206f * log2f(x + 1e-12f); }             // block rate only

// ================================================================ the input stage
// ---- the guard
#define NNOTCH   3
#define LV_HIST  128                     // block levels kept: 256 ms
typedef struct { float f, depth, want; biquad_t q; float rem; int idle; bool on; } notch_t;
static notch_t notch[NNOTCH];
static float anf_a = -1.0f, anf_s1, anf_s2, anf_p = 1e-6f;
static float g_ratio = 1.0f, g_f = 1000.0f, g_dev = 1.0f;
static float lv_hist[LV_HIST], f_hist[LV_HIST];
static int   lv_w, g_cnt, quiet_blocks, g_age, g_grow, g_off;
static float broad_db, broad_g = 1.0f;
static volatile float view_cut;
// ---- the gate
static float gate_env = -90.0f, noise_floor = -70.0f, gate_g = 1.0f;
static bool  gate_open;
static int   gate_hold;
#define NF_SUB  16                       // the noise floor: the lowest level of the last 2 s, in sixteen 125 ms pieces
static float nf_min[NF_SUB], nf_cur = 0;
static int   nf_i, nf_n;
static volatile float view_gate = 1.0f;
// ---- the compressor and the de-esser
static float comp_lv = -60.0f, comp_g = 1.0f;
static svf_t des;
static float des_c;
static float bpbuf[AUDIO_BLOCK];

static void notch_coef(notch_t *k)
{
    biquad_set(&k->q, BQ_PEAK, k->f, 10.0f, k->depth);
}

static void guard_block(float *io, int n, float out_env)
{
    float guard = clampf(pi.guard, 0.0f, 1.0f);
    // the cuts, each ramped per block towards what it wants
    for (int k = 0; k < NNOTCH; k++) {
        notch_t *c = &notch[k];
        if (!c->on) continue;
        float want = c->want * guard;
        float d = c->depth;
        if (want < d) d = fmaxf(want, d - 0.75f);                  // in: -30 dB in 40 ms
        else d = fminf(want, d + 0.004f);                           // out: about 15 s from -30 dB
        if (d != c->depth) { c->depth = d; notch_coef(c); }
        if (c->depth > -0.05f && c->want > -0.05f) c->on = false;
    }
    // the input through the cuts, then the adaptive notch looking for the next tone
    float ex = 0, ee = 0;
    const float rho = 0.98f, r2 = rho * rho, a = anf_a;
    float s1 = anf_s1, s2 = anf_s2, aa = a, p = anf_p;
    float mu = 0.002f / (p + 1e-4f);                                   // normalised by the filter's power (per block)
    float b0 = broad_g, b1 = db2lin(broad_db * guard), db = (b1 - b0) / (float)n;
    broad_g = b1;
    for (int i = 0; i < n; i++) {
        float x = io[i];
        for (int k = 0; k < NNOTCH; k++) {
            if (!notch[k].on) continue;
            float y = biquad_run(&notch[k].q, x);
            float r = x - y; notch[k].rem += r * r;
            x = y;
        }
        x *= b0 + db * (float)i;
        io[i] = x;
        ex += x * x;
        float s0 = x - rho * aa * s1 - r2 * s2;
        float e = s0 + aa * s1 + s2;
        ee += e * e;
        aa -= clampf(mu * e * s1, -0.02f, 0.02f);                       // a bounded step: a sudden loud start cannot throw it
        if (aa > 1.99f) aa = 1.99f;
        if (aa < -1.99f) aa = -1.99f;
        p += 0.002f * (s1 * s1 - p);
        s2 = s1; s1 = s0;
    }
    anf_a = aa; anf_s1 = flush_denormal(s1); anf_s2 = flush_denormal(s2); anf_p = p;
    for (int k = 0; k < NNOTCH; k++) { notch[k].q.z1 = flush_denormal(notch[k].q.z1); notch[k].q.z2 = flush_denormal(notch[k].q.z2); }

    // is it feedback? a pure tone (the notch takes most of it), steady in pitch, growing while the speaker is loud
    float lv = 10.0f * log10f(ex / (float)n + 1e-14f);
    g_ratio += 0.06f * (ee / (ex + 1e-12f) - g_ratio);
    g_f = acosf(clampf(-aa * 0.5f, -1.0f, 1.0f)) * (float)AUDIO_SR / TWO_PI;
    // steadiness: the pitch now against 100 ms ago (a voice's vibrato and slides move it by more than 0.4 %)
    float f_ago = f_hist[(lv_w + LV_HIST - 50) % LV_HIST];
    g_dev += 0.08f * (fabsf(g_f - f_ago) / (g_f + 1.0f) - g_dev);
    float rec = lv_hist[(lv_w + LV_HIST - 20) % LV_HIST];               // 40 ms ago
    lv_hist[lv_w] = lv; f_hist[lv_w] = g_f; lv_w = (lv_w + 1) % LV_HIST;
    bool loud = out_env > 0.03f;
    bool tonal = g_ratio < 0.1f && lv > -55.0f && g_dev < 0.004f && g_f > 80.0f;
    // A tone's life: how long it has been there, and for how much of it it was rising. Feedback climbs for a good
    // while before it screams; a sung note (even a pure, straight one) arrives in its attack and then holds.
    if (tonal) { g_age++; if (lv - rec > 0.6f) g_grow++; g_off = 0; }
    else if (++g_off > 10) g_age = g_grow = 0;
    bool climbing = g_age >= 100 && g_grow >= 50;                      // 200 ms a tone, 100 ms of it rising
    bool screaming = g_ratio < 0.02f && lv > -30.0f && g_age >= 150 && g_grow >= 15;
    if (guard > 0.0f && loud && (climbing || screaming)) g_cnt = 125; else g_cnt = 0;
    quiet_blocks = out_env < 0.01f ? quiet_blocks + 1 : 0;
    for (int k = 0; k < NNOTCH; k++) {
        notch_t *c = &notch[k];
        if (!c->on) continue;
        c->rem = 0;
        if (quiet_blocks > 1500) c->want = 0;                           // 3 s of a quiet room: let go (slowly)
    }
    if (g_cnt >= 125) {                                                 // 250 ms of it
        g_cnt = 0; g_age = g_grow = 0;
        float f = g_f;
        int hit = -1, free_k = -1;
        for (int k = 0; k < NNOTCH; k++) {
            if (notch[k].on && fabsf(notch[k].f - f) < 0.04f * f) hit = k;
            if (!notch[k].on && free_k < 0) free_k = k;
        }
        if (hit >= 0 && notch[hit].depth < -20.0f) broad_db = fmaxf(broad_db - 4.0f, -18.0f);   // it rings through its cut
        else if (hit >= 0) notch[hit].want = -30.0f;
        else if (free_k >= 0) {
            notch_t *c = &notch[free_k];
            memset(&c->q, 0, sizeof c->q);
            c->f = f; c->depth = 0; c->want = -30.0f; c->on = true; c->rem = 0;
            notch_coef(c);
        } else broad_db = fmaxf(broad_db - 4.0f, -18.0f);           // a fourth tone: the whole input comes down
        quiet_blocks = 0;
    }
    if (broad_db < 0.0f && !(tonal && loud)) broad_db = fminf(0.0f, broad_db + 0.01f);   // back up, 5 dB a second
    float cut = -broad_db * guard;
    for (int k = 0; k < NNOTCH; k++) if (notch[k].on && -notch[k].depth > cut) cut = -notch[k].depth;
    view_cut = cut;
}

static void gate_comp_block(float *io, int n)
{
    // levels of this block
    float pk = 0, ms = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(io[i]); if (a > pk) pk = a; ms += io[i] * io[i]; }
    ms /= (float)n;
    // ---- the gate
    float pdb = 20.0f * log10f(pk + 1e-9f);
    gate_env = pdb > gate_env ? pdb : gate_env - 0.3f;                  // 150 dB a second down: bridges a pitch period
    // the noise floor: the lowest the level has been in the last 2 s (it follows the room up and down; a voice
    // has gaps, and a long loud note cannot raise the gate's threshold above -38 dBFS)
    if (nf_n == 0 || gate_env < nf_cur) nf_cur = gate_env;
    if (++nf_n >= 62) {
        nf_min[nf_i] = nf_cur; nf_i = (nf_i + 1) % NF_SUB; nf_n = 0;
        float m = nf_cur;
        for (int k = 0; k < NF_SUB; k++) if (nf_min[k] < m) m = nf_min[k];
        noise_floor = clampf(m, -90.0f, -35.0f);
    }
    float t_open = clampf(noise_floor + 13.0f, -62.0f, -38.0f), t_close = t_open - 6.0f;
    if (gate_env > t_open) { gate_open = true; gate_hold = 50; }
    else if (gate_env < t_close) { if (gate_hold > 0) gate_hold--; else gate_open = false; }
    else if (gate_open) gate_hold = 50;
    float gfloor = db2lin(-40.0f * clampf(pi.gate, 0.0f, 1.0f));
    float g0 = gate_g, g1 = gate_open ? 1.0f : gate_g + (gfloor - gate_g) * 0.025f;
    if (!gate_open && g1 < gfloor) g1 = gfloor;
    if (gate_open && g1 > 1.0f) g1 = 1.0f;
    gate_g = g1;
    view_gate = gate_open ? 1.0f : (g1 - gfloor) / (1.0f - gfloor + 1e-6f);
    // ---- the compressor (on the gated level)
    float gm = 0.5f * (g0 + g1);
    float ldb = 10.0f * log10f(ms * gm * gm + 1e-14f);
    comp_lv += (ldb > comp_lv ? 0.33f : 0.0165f) * (ldb - comp_lv);
    float cmp = clampf(pi.comp, 0.0f, 1.0f), R = 1.0f + 2.0f * cmp, T = -30.0f, W = 10.0f;
    float slope = 1.0f / R - 1.0f;
    #define GR(L) ((L) - T <= -W * 0.5f ? 0.0f : (L) - T < W * 0.5f ? slope * ((L) - T + W * 0.5f) * ((L) - T + W * 0.5f) / (2.0f * W) : slope * ((L) - T))
    float gr = GR(comp_lv), makeup = -GR(-18.0f);
    #undef GR
    float c0 = comp_g, c1 = db2lin(gr + makeup);
    comp_g = c1;
    float inv = 1.0f / (float)n;
    for (int i = 0; i < n; i++) {
        float t = (float)i * inv;
        io[i] *= (g0 + (g1 - g0) * t) * (c0 + (c1 - c0) * t);
    }
    // ---- the de-esser: the 4.5..10 kHz band, taken down where it dominates
    float eb = 0, et = 0;
    for (int i = 0; i < n; i++) { float b = svf_bp(&des, io[i], NULL); bpbuf[i] = b; eb += b * b; et += io[i] * io[i]; }
    des.ic1 = flush_denormal(des.ic1); des.ic2 = flush_denormal(des.ic2);
    float ratio = eb / (et + 1e-12f);
    float want = clampf(pi.deess, 0.0f, 1.0f) * clampf((ratio - 0.3f) / 0.3f, 0.0f, 1.0f) * 0.75f;
    if (eb / (float)n < 1e-7f) want = 0;
    float d0 = des_c;
    des_c += (want - des_c) * (want > des_c ? 0.5f : 0.05f);
    for (int i = 0; i < n; i++) io[i] -= bpbuf[i] * (d0 + (des_c - d0) * (float)i * inv);
}

void fx_input(float *io, int n, float out_env)
{
    pull_in();
    guard_block(io, n, out_env);
    gate_comp_block(io, n);
}

float fx_gate_open(void) { return view_gate; }
float fx_guard_cut(void) { return view_cut; }

// ================================================================ the FX bus
#define ECHO_N   65536u                  // 2.05 s, int16, a power of two
#define PRE_N    1024u
#define PRE_D    640                     // 20 ms
#define NL       8
#define LINE_N   2048u                   // each line's ring (the longest line is 1963 + 6 of modulation): 64 KB of internal RAM
#define SHIM_N   2048u
#define SHIM_W   1600.0f                 // the shifter's window, 50 ms
static const int line_len[NL] = { 955, 1097, 1235, 1378, 1521, 1658, 1810, 1963 };
static const float lfo_hz[NL] = { 0.31f, 0.43f, 0.57f, 0.71f, 0.37f, 0.53f, 0.67f, 0.83f };
static const int dif_len[4] = { 163, 247, 320, 403 };
static int16_t *echo;
static float *pre, *lines, *dif[4], *shim;
static uint32_t ew, pw, lw, sw;
static uint32_t dw[4];
static float ed = 12000.0f, e_lp, e_hp, duck_env, duck_g = 1.0f, xf_from;
static int   xf_n;
#define XF_LEN 960                       // 30 ms
static inline float echo_tap(float d)
{
    int di = (int)d; float fr = d - (float)di;
    float a0 = (float)echo[(ew - (uint32_t)di) & (ECHO_N - 1u)], a1 = (float)echo[(ew - (uint32_t)di - 1u) & (ECHO_N - 1u)];
    return (a0 + (a1 - a0) * fr) * (1.0f / 16384.0f);
}
static float lz[NL], lfo_ph[NL], g_line[NL], hf_line[NL], mod_d[NL];
static float rt_s = -1, tone_s = -1, rt_done = -1, tone_done = -1;
static float sh_ph, sh_hp, sh_lp;
// smoothed bus controls (per block, ramped per sample)
static float s_echo, s_fb = 0.35f, s_tone = 0.5f, s_rev, s_shim, s_throw, s_duck = 0.5f;
// the echo's repeats for the screen
static float tap_pk, tap_cnt;
static volatile float view_taps[8];
static int tap_w;

void fx_init(void)
{
    svf_set(&des, 6800.0f, 0.7f);
    for (int k = 0; k < NF_SUB; k++) nf_min[k] = -70.0f;
    if (echo) return;
    // what the reverb reads and writes every sample, in many places at once, is in internal RAM (in PSRAM the lines'
    // cache slots collide and nearly every access misses: 2 ms of every 2 ms block on the board); the echo, two
    // streams through a long buffer, is fine in PSRAM
    size_t fl = PRE_N + SHIM_N + 4 * 512;
    echo = app_alloc(ECHO_N * sizeof(int16_t));
    lines = app_alloc_fast((size_t)NL * LINE_N * sizeof(float));
    float *m = app_alloc_fast(fl * sizeof(float));
    if (!echo || !m || !lines) { app_free(echo); app_free(m); app_free(lines); echo = NULL; app_log("sing fx: no memory for the echo and reverb"); return; }
    pre = m; m += PRE_N;
    shim = m; m += SHIM_N;
    for (int k = 0; k < 4; k++) { dif[k] = m; m += 512; }
    for (int k = 0; k < NL; k++) lfo_ph[k] = (float)k * 0.137f;
}

// line gains from the reverb time (mid band) and the extra decay of the top (TONE); libm, only when they move
static void reverb_design(void)
{
    if (fabsf(rt_s - rt_done) < 0.002f * rt_s && fabsf(tone_s - tone_done) < 0.002f) return;
    rt_done = rt_s; tone_done = tone_s;
    float ratio = 0.25f + 0.75f * tone_s;                               // how long the top lasts against the middle
    for (int k = 0; k < NL; k++) {
        float L = (float)line_len[k];
        g_line[k] = powf(10.0f, -3.0f * L / (rt_s * (float)AUDIO_SR));
        hf_line[k] = powf(10.0f, -3.0f * L * (1.0f / ratio - 1.0f) / (rt_s * (float)AUDIO_SR));
    }
}

#define MODULATED(k) ((k) == 0 || (k) == 3 || (k) == 5)
// one of the four allpass diffusers (Schroeder, g = 0.5)
#define DIFFUSE(k) { float *bd = dif[k]; float dd = bd[(dw[k] - (uint32_t)dif_len[k]) & 511u]; float vv = r + 0.5f * dd; \
                     bd[dw[k] & 511u] = vv; r = dd - 0.5f * vv; dw[k]++; }

void fx_bus(const float *send, float *io, int n)
{
    pull_bus();
    if (!echo) return;
    const fx_bus_params_t *P = &pb;
    float inv = 1.0f / (float)n;
    // the controls: ramps across the block
    float e0 = s_echo, f0 = s_fb, t0 = s_tone, r0 = s_rev, h0 = s_shim, w0 = s_throw;
    const float sm = 0.08f;                                             // about 25 ms
    s_echo += (clampf(P->echo, 0.0f, 1.0f) - s_echo) * sm;
    s_fb += (clampf(P->echo_fb, 0.0f, 0.95f) - s_fb) * sm;
    s_tone += (clampf(P->echo_tone, 0.0f, 1.0f) - s_tone) * sm;
    s_rev += (clampf(P->reverb, 0.0f, 1.0f) - s_rev) * sm;
    s_shim += (clampf(P->shimmer, 0.0f, 1.0f) - s_shim) * sm;
    s_throw += (clampf(P->throw_, 0.0f, 1.0f) - s_throw) * 0.04f;       // about 50 ms
    s_duck += (clampf(P->echo_duck, 0.0f, 1.0f) - s_duck) * sm;
    float want_d = clampf(P->echo_frames, 64.0f, (float)ECHO_N - 8.0f);
    float rt_t = clampf(P->reverb_s, 0.3f, 12.0f), tn_t = clampf(P->reverb_tone, 0.0f, 1.0f);
    if (rt_s < 0) { rt_s = rt_t; tone_s = tn_t; ed = want_d; }
    rt_s += (rt_t - rt_s) * 0.05f; tone_s += (tn_t - tone_s) * 0.05f;
    reverb_design();
    // a long reverb gathers more of a held note: its input comes down as its time goes up, so it sits at one level
    float rv_in = sqrtf(1.8f / rt_s);
    // the echo's tone: a low pass from 1.2 kHz (tape) to 14 kHz; a low cut in its loop at 100 Hz
    float lpk0 = 1.0f - expf(-TWO_PI * 1200.0f * exp2f(3.5f * t0) / (float)AUDIO_SR);
    float lpk1 = 1.0f - expf(-TWO_PI * 1200.0f * exp2f(3.5f * s_tone) / (float)AUDIO_SR);
    const float hpk = 0.0194f;                                          // 100 Hz
    // ducking: the send's level this block
    float sp = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(send[i]); if (a > sp) sp = a; }
    duck_env += (sp > duck_env ? 0.5f : 0.02f) * (sp - duck_env);       // falls in about 100 ms: the echo swells in a gap
    float dg0 = duck_g;
    duck_g = 1.0f - s_duck * 0.9f * smoothstep(0.02f, 0.15f, duck_env);
    // the reverb lines' modulation for this block: each line's delay glides to its next value
    float md0[NL], md1[NL];
    for (int k = 0; k < NL; k++) {
        lfo_ph[k] += lfo_hz[k] * (float)n / (float)AUDIO_SR;
        if (lfo_ph[k] >= 1.0f) lfo_ph[k] -= 1.0f;
        md0[k] = mod_d[k];
        md1[k] = (float)line_len[k] + 6.0f * sin_turns(lfo_ph[k]);
        if (md0[k] == 0.0f) md0[k] = md1[k];
        mod_d[k] = md1[k];
    }
    const float hf_k = 0.45f;                                           // the top's shelf: a one-pole near 3 kHz
    float pk_e = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i * inv;
        float thr = w0 + (s_throw - w0) * t;
        float fb = (f0 + (s_fb - f0) * t);
        fb += (0.92f - fb) * thr;
        float x = send[i];
        // ---- the echo: a small change of time glides (the tape speeds up or slows down, at most a quarter); a big
        // one fades across to the new place in 30 ms
        float e;
        if (xf_n > 0) {
            float ea = echo_tap(xf_from), eb = echo_tap(ed);
            float k = (float)xf_n * (1.0f / (float)XF_LEN);
            e = ea * k + eb * (1.0f - k);
            if (--xf_n == 0) xf_from = ed;
        } else {
            float dd = want_d - ed;
            if (dd > 0.25f * ed || dd < -0.2f * ed) { xf_from = ed; ed = want_d; xf_n = XF_LEN; e = echo_tap(xf_from); }
            else { ed += clampf(0.0005f * dd, -0.25f, 0.25f); e = echo_tap(ed); }
        }
        e_lp += (lpk0 + (lpk1 - lpk0) * t) * (e - e_lp);
        e_hp += hpk * (e_lp - e_hp);
        float eo = e_lp - e_hp;
        float wv = soft_tanh(x * (1.0f + 1.5f * thr) + eo * fb) * 16384.0f;
        echo[ew & (ECHO_N - 1u)] = (int16_t)(wv > 32767.0f ? 32767.0f : wv < -32767.0f ? -32767.0f : wv);
        ew++;
        float eg = (e0 + (s_echo - e0) * t);
        eg += (1.0f - eg) * 0.6f * thr;
        float echo_out = eo * eg * (dg0 + (duck_g - dg0) * t);
        float ae = fabsf(echo_out); if (ae > pk_e) pk_e = ae;
        // ---- the reverb: pre-delay, diffusion, the network
        pre[pw & (PRE_N - 1u)] = x * rv_in + 0.35f * eo * eg;
        float r = pre[(pw - PRE_D) & (PRE_N - 1u)];
        pw++;
        DIFFUSE(0) DIFFUSE(1) DIFFUSE(2) DIFFUSE(3)
        uint32_t w = lw & (LINE_N - 1u);
        float y[NL], u[NL];
        #pragma GCC unroll 8
        for (int k = 0; k < NL; k++) {
            float v;
            if (MODULATED(k)) {                                         // three lines drift a few samples: no metallic ring
                float d = md0[k] + (md1[k] - md0[k]) * t;
                int di = (int)d; float fr = d - (float)di;
                const float *bk = lines + (size_t)k * LINE_N;
                float a0 = bk[(lw - (uint32_t)di) & (LINE_N - 1u)], a1 = bk[(lw - (uint32_t)di - 1u) & (LINE_N - 1u)];
                v = a0 + (a1 - a0) * fr;
            } else v = lines[(size_t)k * LINE_N + ((lw - (uint32_t)line_len[k]) & (LINE_N - 1u))];
            y[k] = v;
            lz[k] += hf_k * (v - lz[k]);                                // the top decays faster: a high shelf down
            u[k] = (lz[k] + hf_line[k] * (v - lz[k])) * g_line[k];
        }
        // Hadamard 8 (three butterfly stages)
        {
            float a0 = u[0] + u[1], a1 = u[0] - u[1], a2 = u[2] + u[3], a3 = u[2] - u[3];
            float a4 = u[4] + u[5], a5 = u[4] - u[5], a6 = u[6] + u[7], a7 = u[6] - u[7];
            float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3, b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
            u[0] = b0 + b4; u[1] = b1 + b5; u[2] = b2 + b6; u[3] = b3 + b7; u[4] = b0 - b4; u[5] = b1 - b5; u[6] = b2 - b6; u[7] = b3 - b7;
        }
        // the shimmer: an octave up from the reverb's output, back into its input (two taps sweeping the window,
        // crossfaded by triangles that always add up to one)
        shim[sw & (SHIM_N - 1u)] = y[0] - y[3] + y[5];
        float sh = 0;
        float shv = h0 + (s_shim - h0) * t;
        if (shv > 0.0005f) {
            float p1 = sh_ph, p2 = sh_ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
            float w1 = 1.0f - fabsf(2.0f * p1 - 1.0f);
            float d1 = SHIM_W * (1.0f - p1) + 4.0f, d2 = SHIM_W * (1.0f - p2) + 4.0f;
            int i1 = (int)d1, i2 = (int)d2; float q1 = d1 - (float)i1, q2 = d2 - (float)i2;
            float v1 = shim[(sw - (uint32_t)i1) & (SHIM_N - 1u)], v1b = shim[(sw - (uint32_t)i1 - 1u) & (SHIM_N - 1u)];
            float v2 = shim[(sw - (uint32_t)i2) & (SHIM_N - 1u)], v2b = shim[(sw - (uint32_t)i2 - 1u) & (SHIM_N - 1u)];
            float o = (v1 + (v1b - v1) * q1) * w1 + (v2 + (v2b - v2) * q2) * (1.0f - w1);
            sh_lp += 0.6f * (o - sh_lp);                                // below about 7 kHz
            sh_hp += 0.06f * (sh_lp - sh_hp);                           // above about 300 Hz
            sh = (sh_lp - sh_hp) * shv * 0.55f;
        }
        sw++;
        sh_ph += 1.0f / SHIM_W; if (sh_ph >= 1.0f) sh_ph -= 1.0f;
        float inj = soft_tanh(r * 0.5f + sh);
        #pragma GCC unroll 8
        for (int k = 0; k < NL; k++) lines[(size_t)k * LINE_N + w] = flush_denormal(u[k] * 0.35355339f + ((k & 1) ? -inj : inj));
        lw++;
        float rev = (y[0] - y[1] + y[2] - y[3] + y[4] - y[5] + y[6] - y[7]) * 0.16f;
        float wet = echo_out + rev * (r0 + (s_rev - r0) * t);
        float aw = fabsf(wet);
        if (aw > 0.75f) {                                               // a soft ceiling for the wet: straight to 0.75, then bending to 1.5
            float u = (aw - 0.75f) * 1.3333f;
            float c = 0.75f + 0.75f * u / (1.0f + u);
            wet = wet < 0 ? -c : c;
        }
        io[i] += wet;
    }
    for (int k = 0; k < NL; k++) lz[k] = flush_denormal(lz[k]);
    e_lp = flush_denormal(e_lp); e_hp = flush_denormal(e_hp); sh_lp = flush_denormal(sh_lp); sh_hp = flush_denormal(sh_hp);
    // the echo's repeats for the screen: the loudest of each pass
    if (pk_e > tap_pk) tap_pk = pk_e;
    tap_cnt += (float)n;
    if (tap_cnt >= ed) {
        tap_cnt -= ed;
        tap_w = (tap_w + 7) & 7;
        view_taps[tap_w] = tap_pk;
        tap_pk = 0;
    }
}

int fx_echo_taps(float *lv, int max)
{
    int n = max < 8 ? max : 8;
    for (int k = 0; k < n; k++) lv[k] = view_taps[(tap_w + k) & 7];
    return n;
}
