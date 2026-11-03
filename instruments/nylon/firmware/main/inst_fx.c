// NYLON's effects, audio task: the room (a four-line FDN, SIFT's, with a shimmer: its output an octave or a fifth
// up, back into it), ECHO (a tape echo in PSRAM: the time glides, it wows, past 100 % it runs away into its own
// saturation), WAH (a state variable band pass on the neck, a fuzz before it), OCTAVE (a two-tap delay line
// shifter: an octave down, an octave up, a fifth) and WOBBLE (a delay line swung by a slow LFO and a little
// flutter: vibrato, or chorus with the dry). Each engine takes its input times `feed` (0 while it is left, its
// tail ringing out) and writes only what it adds.
#include "inst_nylon.h"
#include "dsp.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- the shifter: two taps of a delay line, crossfaded
#define SH_N 4096
#define SH_M (SH_N - 1)
static inline float sh_read(const float *b, int w, float d)
{
    float rp = (float)w - d;
    if (rp < 0.0f) rp += (float)SH_N;
    int i = (int)rp; float f = rp - (float)i;
    float a = b[i & SH_M], c = b[(i + 1) & SH_M];
    return a + f * (c - a);
}
static inline float wrap01(float p) { p -= (float)(int)p; return p < 0.0f ? p + 1.0f : p; }
// one voice at ratio r: the read point slides by (1 - r) a sample through a window of W samples
static inline float sh_tap(const float *b, int w, float *ph, float r, float W)
{
    *ph = wrap01(*ph + (1.0f - r) / W);
    float p1 = *ph, p2 = p1 + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    float s = sin_turns(0.5f * p1), w1 = s * s;                  // sin^2 and cos^2: they add to one
    return sh_read(b, w, 2.0f + p1 * W) * w1 + sh_read(b, w, 2.0f + p2 * W) * (1.0f - w1);
}

// ---------------------------------------------------------------- the room
static const int rv_len[4] = { 1559, 1877, 2203, 2617 }, ap_len[2] = { 223, 557 };
static float *rv_buf[4], *ap_buf[2];
static int rv_w[4], ap_w[2];
static float rv_lp[4], rv_g[4], rv_damp, rsize_s = -1.0f;
static float *shm_buf; static int shm_w; static float shm_ph1, shm_ph2, shm_s, shm_hp, shm_hpx, shm_lp, shm_fb, shm_env;

void room_run(const float *send, float *io, int n)
{
    if (!rv_buf[0]) return;
    float rs = clampf(E.rsize, 0.0f, 1.0f);
    if (fabsf(rs - rsize_s) > 1e-4f) {                          // decay 0.5 .. 7 s, darker as it grows
        rsize_s = rsize_s < 0.0f ? rs : rsize_s + 0.2f * (rs - rsize_s);
        float rt = 0.5f * exp2f(rsize_s * 3.8f);
        for (int k = 0; k < 4; k++) rv_g[k] = exp2f(-3.0f * 3.3219281f * (float)rv_len[k] / (rt * (float)AUDIO_SR));
        rv_damp = onepole_hz(7000.0f - 3500.0f * rsize_s);
    }
    float sh0 = shm_s;
    shm_s += 0.1f * (clampf(E.shim, 0.0f, 1.0f) - shm_s);
    bool shim = shm_buf && (shm_s > 1e-4f || sh0 > 1e-4f);
    int iv = (int)E.shim_int;
    float lvl = 0;
    for (int i = 0; i < n; i++) {
        float d = send[i];
        if (shim) d += shm_fb;
        for (int k = 0; k < 2; k++) {
            float *ab = ap_buf[k]; int w = ap_w[k];
            float z = ab[w], v = d - 0.6f * z;
            ab[w] = v; d = z + 0.6f * v;
            if (++w >= ap_len[k]) w = 0;
            ap_w[k] = w;
        }
        float r[4];
        for (int k = 0; k < 4; k++) { float y = rv_buf[k][rv_w[k]]; rv_lp[k] += rv_damp * (y - rv_lp[k]); r[k] = rv_lp[k]; }
        float h0 = 0.5f * (r[0] + r[1] + r[2] + r[3]), h1 = 0.5f * (r[0] - r[1] + r[2] - r[3]);
        float h2 = 0.5f * (r[0] + r[1] - r[2] - r[3]), h3 = 0.5f * (r[0] - r[1] - r[2] + r[3]);
        rv_buf[0][rv_w[0]] = flush_denormal(d + rv_g[0] * h0);
        rv_buf[1][rv_w[1]] = flush_denormal(d + rv_g[1] * h1);
        rv_buf[2][rv_w[2]] = flush_denormal(d + rv_g[2] * h2);
        rv_buf[3][rv_w[3]] = flush_denormal(d + rv_g[3] * h3);
        for (int k = 0; k < 4; k++) if (++rv_w[k] >= rv_len[k]) rv_w[k] = 0;
        float out = 0.35f * (r[0] - r[1] + r[2] - r[3]);
        if (shim) {                                              // the room's sound shifted up, high-passed, back in
            shm_buf[shm_w] = out;
            float y = 0;
            if (iv != 1) y += sh_tap(shm_buf, shm_w, &shm_ph1, 2.0f, 1400.0f);
            if (iv >= 1) y += (iv == 2 ? 0.6f : 1.0f) * sh_tap(shm_buf, shm_w, &shm_ph2, 1.4983071f, 1400.0f);
            shm_w = (shm_w + 1) & SH_M;
            float hp = y - shm_hpx + 0.99f * shm_hp; shm_hpx = y; shm_hp = flush_denormal(hp);
            shm_lp += 0.7f * (hp - shm_lp); hp = shm_lp;                 // ~6 kHz: each pass an octave up, not a whistle
            float g = sh0 + (shm_s - sh0) * (float)(i + 1) / (float)n;
            shm_fb = 0.55f * soft_tanh(hp * g * 1.6f);
            float a = fabsf(shm_fb); shm_env += (a > shm_env ? 0.01f : 0.0002f) * (a - shm_env);
        } else shm_fb = 0.0f;
        io[i] += out;
        float a = fabsf(out); if (a > lvl) lvl = a;
    }
    for (int k = 0; k < 4; k++) rv_lp[k] = flush_denormal(rv_lp[k]);
    shm_env = flush_denormal(shm_env); shm_lp = flush_denormal(shm_lp);
    eng_shim_lvl = shm_env;
    (void)lvl;
}

// ---------------------------------------------------------------- ECHO
#define EC_N 48000                                               // 1.5 s, int16 in PSRAM
static int16_t *ec_buf;
static int ec_w, ec_clear = -1;                                  // clear: the next slice to zero, -1 none
static float ec_d = 0.36f * AUDIO_SR, ec_fb_s, ec_lp, ec_hp, ec_hpx, ec_wow_ph, ec_flt_ph, ec_env, ec_tone_s = 0.5f;

void echo_clear_step(void)
{
    if (!ec_buf || ec_clear < 0) return;
    int m = EC_N - ec_clear; if (m > 4096) m = 4096;
    memset(ec_buf + ec_clear, 0, (size_t)m * sizeof(int16_t));
    ec_clear += m;
    if (ec_clear >= EC_N) { ec_clear = -1; ec_env = 0.0f; ec_lp = ec_hp = ec_hpx = 0.0f; }
}
bool echo_quiet(void) { return ec_env < 2e-4f; }
void echo_reset(void) { if (ec_buf) ec_clear = 0; }

IRAM_ATTR void echo_run(const float *in, float *wet, int n, float feed)
{
    if (!ec_buf || ec_clear >= 0) { memset(wet, 0, (size_t)n * sizeof(float)); echo_clear_step(); return; }
    float dt = clampf(E.e_time, 0.02f, 1.4f) * (float)AUDIO_SR;
    float fb0 = ec_fb_s, fb1 = clampf(E.e_fb, 0.0f, 1.15f);
    ec_fb_s = fb1;
    ec_tone_s += 0.1f * (clampf(E.e_tone, 0.0f, 1.0f) - ec_tone_s);
    float lpk = onepole_hz(700.0f * exp2f(ec_tone_s * 3.6f));     // 700 Hz .. 8.5 kHz
    float wow = clampf(E.e_wow, 0.0f, 1.0f);
    const float bdt = (float)n / (float)AUDIO_SR;
    ec_wow_ph = wrap1(ec_wow_ph + bdt * 0.55f);
    ec_flt_ph = wrap1(ec_flt_ph + bdt * 6.3f);
    float mod = wow * (0.0035f * sin_turns(ec_wow_ph) + 0.0006f * sin_turns(ec_flt_ph));
    float lvl = 0;
    for (int i = 0; i < n; i++) {
        ec_d += 0.00025f * (dt - ec_d);                          // the time glides, as a tape's speed would
        float d = ec_d * (1.0f + mod);
        float rp = (float)ec_w - d;
        while (rp < 0.0f) rp += (float)EC_N;
        int ri = (int)rp; float rf = rp - (float)ri;
        int rj = ri + 1 >= EC_N ? 0 : ri + 1;
        float y = ((float)ec_buf[ri] + rf * ((float)ec_buf[rj] - (float)ec_buf[ri])) * (1.0f / 16384.0f);
        float fb = y * (fb0 + (fb1 - fb0) * (float)(i + 1) / (float)n);
        ec_lp += lpk * (fb - ec_lp);
        float hp = ec_lp - ec_hpx + 0.995f * ec_hp; ec_hpx = ec_lp; ec_hp = hp;
        float v = in[i] * feed + hp;
        v = 0.7f * soft_tanh(v * (1.0f / 0.7f));                 // a runaway saturates here
        ec_buf[ec_w] = (int16_t)(v * 16384.0f);
        if (++ec_w >= EC_N) ec_w = 0;
        wet[i] = y;
        float a = fabsf(y); if (a > lvl) lvl = a;
    }
    ec_lp = flush_denormal(ec_lp); ec_hp = flush_denormal(ec_hp);
    ec_env = fmaxf(lvl, ec_env * 0.99f);
    eng_echo_lvl = ec_env;
}

// ---------------------------------------------------------------- WAH
static svf_t wah_f;
static float wah_pos_s = 0.5f, wah_env, wah_fz_s, wah_q_s = 5.0f;
IRAM_ATTR void wah_run(const float *in, float *wet, int n, float feed)
{
    // the envelope opens it (AUTO WAH) on top of the neck
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(in[i]); if (a > pk) pk = a; }
    wah_env += (pk > wah_env ? 0.5f : 0.03f) * (pk - wah_env);
    float pos_t = clampf(E.w_pos + E.w_env * clampf(wah_env * 6.0f, 0.0f, 1.0f), 0.0f, 1.0f);
    wah_pos_s += 0.25f * (pos_t - wah_pos_s);
    wah_q_s += 0.2f * (clampf(E.w_q, 0.7f, 16.0f) - wah_q_s);
    svf_set(&wah_f, 330.0f * exp2f(wah_pos_s * 2.9f), wah_q_s);  // 330 Hz .. 2.5 kHz
    float fz0 = wah_fz_s; wah_fz_s += 0.15f * (clampf(E.w_fuzz, 0.0f, 1.0f) - wah_fz_s);
    float g0 = 1.0f + 60.0f * fz0 * fz0, g1 = 1.0f + 60.0f * wah_fz_s * wah_fz_s;
    float dn0 = 0.3f / soft_tanh(0.3f * g0), dn1 = 0.3f / soft_tanh(0.3f * g1);
    bool fuzz = wah_fz_s > 1e-4f || fz0 > 1e-4f;
    for (int i = 0; i < n; i++) {
        float x = in[i] * feed;
        if (fuzz) {
            float t = (float)(i + 1) / (float)n, g = g0 + (g1 - g0) * t, fz = fz0 + (wah_fz_s - fz0) * t;
            float d = soft_tanh(x * g) * (dn0 + (dn1 - dn0) * t);
            x += (d - x) * fminf(1.0f, fz * 4.0f);
        }
        float lp, bp = svf_bp(&wah_f, x, &lp);
        wet[i] = 1.3f * bp + 0.12f * lp;
    }
    eng_wah_pos = wah_pos_s;
}

// ---------------------------------------------------------------- OCTAVE
static float *oc_buf; static int oc_w; static float oc_ph[3], oc_sub_lp, oc_g[3];
IRAM_ATTR void oct_run(const float *in, float *wet, int n, float feed)
{
    if (!oc_buf) { memset(wet, 0, (size_t)n * sizeof(float)); return; }
    float t[3] = { clampf(E.o_sub, 0.0f, 1.0f), clampf(E.o_up, 0.0f, 1.0f), clampf(E.o_fifth, 0.0f, 1.0f) };
    float g0[3], dg[3];
    for (int k = 0; k < 3; k++) { g0[k] = oc_g[k]; dg[k] = (t[k] - oc_g[k]) / (float)n; oc_g[k] = t[k]; }
    const float lpk = 0.25f;                                      // the octave down, smoothed (~1.5 kHz)
    for (int i = 0; i < n; i++) {
        oc_buf[oc_w] = in[i] * feed;
        float fi = (float)i;
        float y = 0;
        float gs = g0[0] + dg[0] * fi, gu = g0[1] + dg[1] * fi, gf = g0[2] + dg[2] * fi;
        if (gs > 1e-4f) { float s = sh_tap(oc_buf, oc_w, &oc_ph[0], 0.5f, 2400.0f); oc_sub_lp += lpk * (s - oc_sub_lp); y += 1.3f * gs * oc_sub_lp; }
        if (gu > 1e-4f) y += gu * sh_tap(oc_buf, oc_w, &oc_ph[1], 2.0f, 1800.0f);
        if (gf > 1e-4f) y += gf * sh_tap(oc_buf, oc_w, &oc_ph[2], 1.4983071f, 1500.0f);
        oc_w = (oc_w + 1) & SH_M;
        wet[i] = y;
    }
    oc_sub_lp = flush_denormal(oc_sub_lp);
}

// ---------------------------------------------------------------- WOBBLE
#define WB_N 2048
static float *wb_buf; static int wb_w; static float wb_ph, wb_ph2, wb_dep_s, wb_rate_s = 0.8f, wb_drift, wb_drift_t; static uint32_t wb_seed = 0x77aa11u;
IRAM_ATTR void wob_run(const float *in, float *wet, int n, float feed)
{
    if (!wb_buf) { memset(wet, 0, (size_t)n * sizeof(float)); return; }
    float d0 = wb_dep_s; wb_dep_s += 0.1f * (clampf(E.m_depth, 0.0f, 2.0f) - wb_dep_s);
    wb_rate_s += 0.1f * (clampf(E.m_rate, 0.05f, 12.0f) - wb_rate_s);
    if (rng_uni(&wb_seed) < 0.01f) wb_drift_t = rng_bi(&wb_seed);   // a slow wander, as a worn tape's
    wb_drift += 0.002f * (wb_drift_t - wb_drift);
    const float isr = 1.0f / (float)AUDIO_SR;
    for (int i = 0; i < n; i++) {
        wb_buf[wb_w] = in[i] * feed;
        wb_ph = wrap1(wb_ph + wb_rate_s * isr);
        wb_ph2 = wrap1(wb_ph2 + 5.7f * wb_rate_s * isr);
        float dep = d0 + (wb_dep_s - d0) * (float)(i + 1) / (float)n;
        float lfo = 0.75f * sin_turns(wb_ph) + 0.12f * sin_turns(wb_ph2) + 0.13f * wb_drift;
        float d = 160.0f + dep * 230.0f * (1.0f + lfo);           // 5 ms, up to +- 7 ms at 100 %
        float rp = (float)wb_w - d;
        if (rp < 0.0f) rp += (float)WB_N;
        int ri = (int)rp; float rf = rp - (float)ri;
        float a = wb_buf[ri & (WB_N - 1)], b = wb_buf[(ri + 1) & (WB_N - 1)];
        wet[i] = a + rf * (b - a);
        wb_w = (wb_w + 1) & (WB_N - 1);
    }
}

void fx_init(void)
{
    for (int k = 0; k < 4; k++) rv_buf[k] = app_alloc_fast((size_t)rv_len[k] * sizeof(float));
    for (int k = 0; k < 2; k++) ap_buf[k] = app_alloc_fast((size_t)ap_len[k] * sizeof(float));
    for (int k = 0; k < 4; k++) if (!rv_buf[k]) rv_buf[0] = NULL;
    for (int k = 0; k < 2; k++) if (!ap_buf[k]) rv_buf[0] = NULL;
    shm_buf = app_alloc_fast(SH_N * sizeof(float));
    oc_buf = app_alloc_fast(SH_N * sizeof(float));
    wb_buf = app_alloc_fast(WB_N * sizeof(float));
    ec_buf = app_alloc(EC_N * sizeof(int16_t));
    if (!rv_buf[0] || !shm_buf || !oc_buf || !wb_buf || !ec_buf) app_log("nylon: an effect has no memory");
    rv_damp = onepole_hz(6000.0f);
    svf_set(&wah_f, 800.0f, 5.0f);
}
