// NYLON's sound, audio task: the chain every 2 ms.
//
//   mic -> LOW CUT -> HIGH CUT -> HOWL GUARD's notches -> the ear (onsets, the tracker's ring at 16 kHz, the guard's
//       ring at 32 kHz, the cloud's ring)
//       -> the sound's engine (and the one before it, ringing out with nothing fed in) -> DRY + EFFECT
//       -> the drums -> the room (ROOM, with its shimmer) -> the looper (records all of that, plays under it)
//
// A new sound with another MODE: the new engine's input fades in over 10 ms, the old one's out, and the old one
// keeps running until its tail is quiet (an echo's repeats, the cloud's last grains), at most 6 s.
#include "inst_nylon.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

volatile eng_ctl_t E;
volatile float eng_in_lvl, eng_out_lvl, eng_wet_lvl, eng_env;
volatile float eng_bloom_lvl, eng_bloom_age, eng_echo_lvl, eng_wah_pos, eng_shim_lvl, eng_synth_lvl;
volatile int   eng_bloom_caps, eng_frozen, eng_drum_last, eng_beat = -1;
volatile uint32_t eng_drum_seq, eng_bloom_cancel;
float eng_ear[EAR_N];
volatile uint32_t eng_ear_w;
float eng_hf[HF_N];
volatile uint32_t eng_hf_w;

SPSC(onsetq, onset_t, 16)
static onsetq_t onsets;
bool eng_onset_pop(onset_t *o) { return onsetq_pop(&onsets, o); }

static svf_t lc, hc;
static float lc_s = 90.0f, hc_s = 9000.0f;
static biquad_t nf[4];                          // HOWL GUARD's notches
static float n_hz[4], n_g[4];                   // the frequency each is set for, its fade 0..1
static float env_f, env_m, env_s, env_hf, x1;
static int   ons_hold, ons_rep = -1;            // refractory; samples until the onset is reported (its peak by then)
static float ons_pk; static uint32_t ons_frame;
static float dry_s = 1.0f, wet_s, wet_tail = 0.0f, space_s;
static int   m_cur = -1, m_tail = -1;
static float feed_cur, feed_tail, tail_t, tail_quiet;
static float lvl_in, lvl_out, lvl_wet;
static float clk_amp, clk_ph, clk_dph; static int clk_i;   // the metronome's tick

void eng_init(void)
{
    fx_init();
    grain_init();
    drum_init();
    loop_init();                                 // last: the biggest, it takes what is left (16 s, or 12, or 8)
    svf_set(&lc, lc_s, 0.7f);
    svf_set(&hc, hc_s, 0.7f);
    E.mode = M_CLEAN; E.dry = 1.0f; E.l_level = 0.85f; E.l_rate = 1.0f; E.l_keep = 1.0f; E.l_trig = 0.01f;
    E.rsize = 0.5f; E.lowcut = 90.0f; E.hicut = 9000.0f;
}

static void reset_mode(int m)
{
    switch (m) {
    case M_ECHO: echo_reset(); break;
    case M_BLOOM: bloom_clear(); break;
    default: break;
    }
}
void eng_reset_mode(int m) { (void)m; }

static IRAM_ATTR void run_mode(int m, const float *in, float *wet, int n, float feed)
{
    switch (m) {
    case M_CLOUD:  cloud_run(wet, n, feed); break;
    case M_BLOOM:  bloom_run(in, wet, n, feed); break;
    case M_ECHO:   echo_run(in, wet, n, feed); break;
    case M_WAH:    wah_run(in, wet, n, feed); break;
    case M_OCTAVE: oct_run(in, wet, n, feed); break;
    case M_WOBBLE: wob_run(in, wet, n, feed); break;
    case M_VOICE:  synth_run(wet, n, feed); break;
    default: memset(wet, 0, (size_t)n * sizeof(float)); break;    // CLEAN, SHIMMER (the room), GOLPE (the drums)
    }
}

IRAM_ATTR void eng_audio(const float *in, float *out, int n)
{
    const uint32_t f0 = audio_frames();
    const float bdt = (float)n / (float)AUDIO_SR;
    float x[AUDIO_BLOCK], a_in[AUDIO_BLOCK], a_tail[AUDIO_BLOCK], wc[AUDIO_BLOCK], wt[AUDIO_BLOCK], send[AUDIO_BLOCK];

    // ---- LOW CUT, HIGH CUT, the notches, the onsets, the ear
    float lcut = clampf(E.lowcut, 30.0f, 600.0f), hcut = clampf(E.hicut, 800.0f, 14000.0f);
    if (fabsf(lcut - lc_s) > 0.05f) { lc_s += 0.2f * (lcut - lc_s); svf_set(&lc, lc_s, 0.7f); }
    if (fabsf(hcut - hc_s) > 1.0f) { hc_s *= exp2f(0.2f * log2f(hcut / hc_s)); svf_set(&hc, hc_s, 0.7f); }
    // a notch fades in over 50 ms; one moved to a new frequency fades out first, then in where it is now
    float ng0[4], dng[4]; int nn = 0;
    for (int k = 0; k < 4; k++) {
        float hz = E.notch_on[k] ? E.notch_hz[k] : 0.0f;
        bool same = hz > 0.0f && fabsf(hz - n_hz[k]) < 0.5f;
        float tgt = same ? 1.0f : 0.0f;
        if (!same && n_g[k] <= 0.0f && hz > 0.0f) {               // faded out (or never in): set it here
            biquad_set(&nf[k], BQ_NOTCH, hz, 10.0f, 0.0f); nf[k].z1 = nf[k].z2 = 0.0f;
            n_hz[k] = hz; tgt = 1.0f;
        }
        ng0[k] = n_g[k];
        n_g[k] = clampf(n_g[k] + (tgt > n_g[k] ? 1.0f : -1.0f) * bdt / 0.05f, 0.0f, 1.0f);
        if (tgt == n_g[k] && tgt == 0.0f && ng0[k] == 0.0f) { dng[k] = 0.0f; continue; }
        dng[k] = (n_g[k] - ng0[k]) / (float)n;
        nn++;
    }
    bool onset_now = false; uint32_t onset_frame = 0; float onset_lvl = 0;
    float pk_in = 0;
    uint32_t ew = eng_ear_w, hw = eng_hf_w;
    for (int i = 0; i < n; i++) {
        float v = svf_lp(&hc, svf_hp(&lc, in[i]));
        if (nn) for (int k = 0; k < 4; k++) {
            float g = ng0[k] + dng[k] * (float)(i + 1);
            if (g <= 0.0f && ng0[k] <= 0.0f) continue;
            float y = biquad_run(&nf[k], v);
            v += g * (y - v);
        }
        eng_hf[hw++ & (HF_N - 1)] = v;
        x[i] = v;
        float a = fabsf(v);
        if (a > pk_in) pk_in = a;
        env_f += (a > env_f ? 0.3f : 0.0015f) * (a - env_f);
        env_m += (a > env_m ? 0.003f : 0.0015f) * (a - env_m);   // ~10 ms up: a note's attack outruns it, its decay does not
        env_s += 0.0002f * (a - env_s);
        float hf = fabsf(v - x1); x1 = v;
        env_hf += (hf > env_hf ? 0.3f : 0.0015f) * (hf - env_hf);
        // an onset is reported 5 ms on, with its peak by then (at the very start an attack is still rising: judged
        // there, a note can look too quiet for TRIGGER)
        if (ons_rep > 0) {
            if (env_f > ons_pk) ons_pk = env_f;
            if (--ons_rep == 0) {
                onset_t o = { ons_frame, ons_pk, clampf(env_hf / (env_f + 1e-9f), 0.0f, 2.0f) };
                onsetq_push(&onsets, &o); ons_rep = -1;
                if (!onset_now) { onset_now = true; onset_frame = ons_frame; onset_lvl = ons_pk; }
            }
        }
        if (ons_hold > 0) ons_hold--;
        else if (env_f > 2.0f * env_s + 0.002f && env_f > 1.5f * env_m && env_f > 0.004f) {
            ons_hold = AUDIO_SR * 60 / 1000;
            ons_frame = f0 + (uint32_t)i; ons_pk = env_f; ons_rep = AUDIO_SR * 5 / 1000;
        }
        if (i & 1) { eng_ear[ew & (EAR_N - 1)] = 0.5f * (x[i] + x[i - 1]); ew++; }
    }
    __atomic_store_n(&eng_ear_w, ew, __ATOMIC_RELEASE);
    __atomic_store_n(&eng_hf_w, hw, __ATOMIC_RELEASE);
    env_f = flush_denormal(env_f); env_m = flush_denormal(env_m); env_s = flush_denormal(env_s); env_hf = flush_denormal(env_hf);
    eng_env = env_f;
    ring_write(x, n, m_cur == M_CLOUD && E.c_frozen);
    eng_frozen = m_cur == M_CLOUD && E.c_frozen;

    // ---- the engines: this sound's, and the last one's tail
    int want = clampi(E.mode, 0, NMODE - 1);
    if (want != m_cur) {
        if (want == m_tail) { int t = m_tail; m_tail = m_cur; m_cur = t; float f = feed_tail; feed_tail = feed_cur; feed_cur = f; wet_tail = wet_s; }
        else {
            if (m_tail >= 0) reset_mode(m_tail);
            m_tail = m_cur; feed_tail = feed_cur; wet_tail = wet_s;
            m_cur = want; feed_cur = 0.0f;
            reset_mode(m_cur);
        }
        tail_t = 0.0f; tail_quiet = 0.0f;
    }
    if (onset_now && m_cur == M_BLOOM) bloom_onset(onset_frame, onset_lvl);
    float fc0 = feed_cur, ft0 = feed_tail;
    feed_cur = fminf(1.0f, feed_cur + bdt / 0.01f);
    feed_tail = fmaxf(0.0f, feed_tail - bdt / 0.01f);
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / (float)n;
        a_in[i] = x[i] * (fc0 + (feed_cur - fc0) * t);
        a_tail[i] = x[i] * (ft0 + (feed_tail - ft0) * t);
    }
    run_mode(m_cur, a_in, wc, n, feed_cur);
    bool tail = m_tail >= 0 && m_tail != m_cur;
    if (tail) {
        run_mode(m_tail, a_tail, wt, n, feed_tail);
        float pk = 0;
        for (int i = 0; i < n; i++) { float a = fabsf(wt[i]); if (a > pk) pk = a; }
        tail_t += bdt;
        tail_quiet = pk < 1e-4f ? tail_quiet + bdt : 0.0f;
        if ((tail_quiet > 0.3f && feed_tail <= 0.0f) || tail_t > 6.0f) {   // over: its state goes, a fade if it was not quiet
            if (pk >= 1e-4f) for (int i = 0; i < n; i++) wt[i] *= 1.0f - (float)(i + 1) / (float)n;
            reset_mode(m_tail); m_tail = -1;
        }
    } else memset(wt, 0, sizeof(float) * (size_t)n);
    if (m_cur == M_ECHO || m_tail == M_ECHO) { /* the echo clears itself while muted */ }
    else echo_clear_step();

    // ---- the mix: DRY, EFFECT, the drums, the room
    float d0 = dry_s, w0 = wet_s, s0 = space_s;
    dry_s += 0.25f * (clampf(E.dry, 0.0f, 1.5f) - dry_s);
    wet_s += 0.25f * (clampf(E.wet, 0.0f, 1.5f) - wet_s);
    space_s += 0.25f * (clampf(E.space, 0.0f, 1.0f) - space_s);
    float pk_wet = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / (float)n;
        float w = wc[i] * (w0 + (wet_s - w0) * t) + (tail ? wt[i] * wet_tail : 0.0f);
        float a = fabsf(w); if (a > pk_wet) pk_wet = a;
        x[i] = x[i] * (d0 + (dry_s - d0) * t) + w;
    }
    drum_run(x, n);
    for (int i = 0; i < n; i++) send[i] = x[i] * (s0 + (space_s - s0) * (float)(i + 1) / (float)n);
    room_run(send, x, n);

    // ---- the looper: what came out so far is recorded, the loop plays under it
    loop_process(x, out, n, onset_lvl, onset_now, onset_frame);

    // ---- the metronome: a tick on each beat of the grid, the bar's first higher; after the looper, so never in the loop
    float fpb = E.l_fpb;
    if (fpb > 100.0f) {
        int32_t rel = (int32_t)(f0 - E.l_grid0);
        if (rel + n <= 0) eng_beat = -1;                         // the grid has not begun
        for (int i = 0; i < n; i++) {                            // a beat line inside this block?
            int32_t r = rel + i;
            if (r < 0) continue;
            int bi = (int)((float)r / fpb);
            if (bi != eng_beat) {
                eng_beat = bi;
                if (E.m_click) {
                    bool one = bi % clampi(E.l_beats, 1, 16) == 0;
                    clk_amp = one ? 0.28f : 0.17f; clk_ph = 0.0f; clk_dph = (one ? 2100.0f : 1500.0f) / (float)AUDIO_SR; clk_i = i;
                }
            }
        }
    }
    if (clk_amp > 1e-4f) {
        for (int i = clk_i; i < n; i++) {
            clk_ph = wrap1(clk_ph + clk_dph);
            out[i] += clk_amp * sin_turns(clk_ph);
            clk_amp *= 0.9965f;                                  // ~9 ms
        }
        clk_i = 0;
        if (clk_amp <= 1e-4f) clk_amp = 0.0f;
    }

    float pk_out = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[i]); if (a > pk_out) pk_out = a; }
    lvl_in = fmaxf(pk_in, lvl_in * 0.97f); lvl_out = fmaxf(pk_out, lvl_out * 0.97f); lvl_wet = fmaxf(pk_wet, lvl_wet * 0.97f);
    eng_in_lvl = lvl_in; eng_out_lvl = lvl_out; eng_wet_lvl = lvl_wet;
}
