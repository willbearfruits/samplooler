// HOWL as a layer of the jam: tuned feedback whose bands sit on the song's chord. The mic (and, in AIR, the
// speaker and the room) feeds 3..8 band passes tuned to the chord's notes, so only those notes can build up
// into a howl. When the song's chord changes (the progression, the bass), every band slides to the nearest
// note of the new chord: notes the two chords share keep howling, the others glide over GLIDE.
//
// The DSP is HOWL's (../howl), with the same safety: the loop saturates at 1.3x HOWL LEVEL, a
// governor on the log of the level eases the gain down (fast) and up (no faster than SWELL), a per-sample
// brake for a speaker far too loud, and what leaves the layer rises at most 120 dB/s and never passes 1.25x
// HOWL LEVEL. INNER closes the loop inside (works on headphones), AIR through the speaker and the room, BOTH.
// A room loop (SIZE) with a frequency shifter (SHIFT) makes howls that climb or fall.
//
// Live: tip the bottom edge down for gain, lean (or the stick's x) to pick the band in the spotlight, the
// stick's y swells. Not live: the layer keeps the last gain and spotlight; it fades out over 50 ms and then
// costs nothing, unless FREEZE is on: then the mic is cut and the inner loop holds the howl under the other
// layers (it still follows the chord). The host records the dry output into HOWL's track.
#include "jam.h"
#include "ui.h"
#include "oled.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
enum {
    P_MODE, P_GAIN, P_RING, P_LEVEL, P_FREEZE, P_OCT, P_BANDS, P_GLIDE,
    P_ROOM, P_SIZE, P_SHIFT, P_COLOUR, P_HEAT, P_DRIFT, P_SWELL, P_SEND,
    P_N
};
static const char *const pages[2] = { "HOWL", "HOWL ROOM" };
static const char *const mode_l[] = { "INNER", "AIR", "BOTH" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const colour_l[] = { "SOFT", "WARM", "HARD", "FOLD" };
static const param_t params[P_N] = {
    [P_MODE]   = { "MODE", "wmod", 0, PK_ENUM, 0, 0, 2, 1, 0, "", mode_l, "AIR NEEDS THE SPEAKER" },
    [P_GAIN]   = { "GAIN", "wgai", 0, PK_NUM, PF_INT, 0, 100, 2, 50, "%", NULL, "MOVES THE HOWLING EDGE" },
    [P_RING]   = { "RING", "wrng", 0, PK_NUM, PF_LOG, 6, 150, 1.08f, 30, "", NULL, "HOW NARROW EACH NOTE IS" },
    [P_LEVEL]  = { "HOWL LEVEL", "wlvl", 0, PK_NUM, PF_INT, -30, -6, 1, -16, "dB", NULL, "THE LOUDEST A HOWL GETS" },
    [P_FREEZE] = { "FREEZE", "wfrz", 0, PK_ENUM, 0, 0, 1, 1, 0, "", onoff_l, "HOWLS ON UNDER THE OTHERS" },
    [P_OCT]    = { "OCTAVE", "woct", 0, PK_NUM, PF_INT, 3, 6, 1, 5, "", NULL, "WHERE THE HOWLS SIT" },
    [P_BANDS]  = { "BANDS", "wbnd", 0, PK_NUM, PF_INT, 3, 8, 1, 6, "", NULL, "HOW MANY NOTES CAN HOWL" },
    [P_GLIDE]  = { "GLIDE", "wgld", 0, PK_NUM, PF_LOG, 0.02f, 4.0f, 1.15f, 0.6f, "s", NULL, "SLIDE TO A NEW CHORD" },
    [P_ROOM]   = { "ROOM", "wroo", 1, PK_NUM, PF_INT, 0, 100, 5, 10, "%", NULL, "A ROOM INSIDE THE LOOP" },
    [P_SIZE]   = { "SIZE", "wsiz", 1, PK_NUM, PF_LOG, 0.002f, 0.25f, 1.08f, 0.03f, "s", NULL, "THE INNER ROOM'S DELAY" },
    [P_SHIFT]  = { "SHIFT", "wshf", 1, PK_NUM, PF_SIGNED, -20, 20, 0.1f, 0, "Hz", NULL, "HOWLS CLIMB OR FALL" },
    [P_COLOUR] = { "COLOUR", "wcol", 1, PK_ENUM, 0, 0, 3, 1, 0, "", colour_l, "HOW THE LOOP SATURATES" },
    [P_HEAT]   = { "HEAT", "whea", 1, PK_NUM, PF_LOG, 1, 10, 1.1f, 1, "x", NULL, "HOW HARD THE COLOUR BITES" },
    [P_DRIFT]  = { "DRIFT", "wdrf", 1, PK_NUM, PF_INT, 0, 50, 1, 4, "ct", NULL, "SLOW DETUNE: HOWLS BEAT" },
    [P_SWELL]  = { "SWELL", "wswl", 1, PK_NUM, PF_LOG, 0.2f, 10.0f, 1.15f, 1.5f, "s", NULL, "HOW SLOWLY HOWLS GROW" },
    [P_SEND]   = { "SEND", "wsnd", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "INTO THE ECHO AND REVERB" },
};
#define P(i) LP(L_HOWL, (i))

// ---------------------------------------------------------------- shared between the UI and the audio task
#define NB 8
static volatile float band_hz[NB];          // UI -> audio
static volatile float band_w[NB];
static volatile float drive_req = 0.6f;     // the loop gain asked for, before the governor
static volatile float band_lvl[NB];         // audio -> UI
static volatile float howl_lvl;             // the governed level (envelope)
static volatile float out_lvl;              // what left the layer (envelope, after the fade)
static volatile int   sleeping = 1;         // faded out and skipped
static volatile float peak_hold;            // the loudest sample that left the layer since howl_out_peak()

// for tools/sim/test_howl.c
const volatile float *howl_band_hz(void) { return band_hz; }
float howl_level(void) { return howl_lvl; }
int   howl_sleeping(void) { return sleeping; }
float howl_out_peak(void) { float p = peak_hold; peak_hold = 0; return p; }

// ---------------------------------------------------------------- audio side
typedef struct { float x1[4], x2[4], y1[4], y2[4]; } ap4_t;
static const float HA[4] = { 0.6923878f * 0.6923878f, 0.9360654f * 0.9360654f, 0.9882295f * 0.9882295f, 0.9987488f * 0.9987488f };
static const float HB[4] = { 0.4021921f * 0.4021921f, 0.8561711f * 0.8561711f, 0.9722910f * 0.9722910f, 0.9952885f * 0.9952885f };
static inline float ap4(ap4_t *a, const float *c, float x)
{
    for (int k = 0; k < 4; k++) {
        float y = c[k] * (x + a->y2[k]) - a->x2[k];
        a->x2[k] = a->x1[k]; a->x1[k] = x; a->y2[k] = a->y1[k]; a->y1[k] = flush_denormal(y);
        x = y;
    }
    return x;
}

#define ROOM_N 8192                          // 256 ms, 32 KB of PSRAM
static float  *room_buf;
static dline_t room_dl;
static svf_t   bp[NB];
static float   f_s[NB], k_s[NB], w_s[NB], drift_ph[NB];
static float   y_prev, room_out, room_lp;
static dcb_t   dcb;
static float   drive_s = 0.6f, fb_t_s = 0.92f, fb_r_s, mic_s = 1.0f, frz_s, delay_s = 960.0f, shift_s, damp_a = 0.5f;
static float   env, agc_db = -6.0f, g_s = 0.5f, brake = 1.0f, fade;
static float   tgt_s = 0.16f, env_o, allow = 1e-4f, og = 1.0f, heat_s = 1.0f, send_s = 0.3f;
static float   rot_c = 1.0f, rot_s;
static ap4_t   hil_a, hil_b;
static float   hil_a_z;
static int     col_cur, col_prev;
static float   col_x = 1.0f;
static float   frz_drive;
static uint32_t seed = 0x2468ace1u;
static float   warm_off;

// tan and sec for the small angles the bands use (below 0.35 rad: MIDI 103 is 0.31): polynomials, no libm
static inline float tan_small(float x) { float x2 = x * x; return x * (1.0f + x2 * (0.33333334f + x2 * (0.13333334f + x2 * 0.053968254f))); }
static inline float sec_small(float x) { float x2 = x * x; return 1.0f + x2 * (0.5f + x2 * (0.20833333f + x2 * 0.084722224f)); }
// the TPT state variable filter's coefficients (as dsp.h's svf_set) for centre fc and damping k
static inline void svf_coef(float *c, float fc, float k)
{
    float g = tan_small(3.14159265f * clampf(fc, 20.0f, 4500.0f) / (float)AUDIO_SR);
    float a1 = 1.0f / (1.0f + g * (g + k));
    c[0] = a1; c[1] = g * a1; c[2] = g * c[1]; c[3] = k;
}

static inline float colour(int c, float v)
{
    switch (c) {
    case 1: return (soft_tanh(v + 0.35f) - warm_off) * 1.13f;          // WARM: biased, even harmonics
    case 2: return soft_knee(v);                                        // HARD: clean, then a short knee
    case 3: return sin_turns(v * 0.1591549f);                           // FOLD: sin(v), folds past pi/2
    default: return soft_tanh(v);                                       // SOFT
    }
}

static void init(void)
{
    room_buf = app_alloc(ROOM_N * sizeof(float));
    room_dl = (dline_t){ room_buf, ROOM_N - 1, 0 };
    warm_off = soft_tanh(0.35f);
    for (int b = 0; b < NB; b++) { f_s[b] = 523.0f; k_s[b] = 1.0f / 30.0f; svf_set(&bp[b], f_s[b], 30.0f); drift_ph[b] = 0.137f * (float)b; }
}

static void audio(const float *in, float *dry, float *send, int n, bool live)
{
    if (!room_buf) return;
    const float bdt = (float)n / (float)AUDIO_SR;
    bool frozen_t = P(P_FREEZE) >= 1.0f;
    float fade_t = live || frozen_t ? 1.0f : 0.0f;
    if (fade <= 0.0f && fade_t <= 0.0f) { sleeping = 1; out_lvl = 0; howl_lvl = 0; return; }   // faded out: nothing to do
    sleeping = 0;
    // ---- per block: the bank. Frequencies glide in pitch (GLIDE), Q in ratio, coefficients per sample.
    float glide_k = 1.0f - expf(-bdt / fmaxf(P(P_GLIDE), 0.005f));
    float ring = P(P_RING);
    float frz_t = frozen_t ? 1.0f : 0.0f;
    float drift = P(P_DRIFT);
    float w[NB], cf[NB][4], dc_[NB][4];
    bool  mv[NB];
    for (int b = 0; b < NB; b++) {
        float ft = band_hz[b];
        if (ft < 30.0f) ft = 30.0f;
        drift_ph[b] = wrap1(drift_ph[b] + bdt * (0.05f + 0.031f * (float)b));
        float det = drift * (0.6f * sin_turns(drift_ph[b]) + 0.4f * sin_turns(drift_ph[b] * 2.618f + 0.3f));
        ft *= fast_exp2(det * (1.0f / 1200.0f));
        f_s[b] += glide_k * (ft - f_s[b]);
        float kt = 1.0f / (frz_s > 0.5f ? fmaxf(ring, 120.0f) : ring);
        if (fabsf(kt - k_s[b]) > 1e-6f) k_s[b] *= exp2f(0.0133f * log2f(kt / k_s[b]));   // Q glides in ratio, ~150 ms
        // the one-sample loop's phase: tune up by w/2Q and boost by 1/cos(w), as far as the tight loop is used
        float wc = TWO_PI * f_s[b] / (float)AUDIO_SR, tight = fb_t_s * (1.0f / 0.92f);
        float fc = f_s[b] * (1.0f + tight * 0.5f * wc * k_s[b]);
        float nx[4]; svf_coef(nx, fc, k_s[b]);
        cf[b][0] = bp[b].a1; cf[b][1] = bp[b].a2; cf[b][2] = bp[b].a3; cf[b][3] = bp[b].k;
        float m = 0;
        for (int j = 0; j < 4; j++) { dc_[b][j] = (nx[j] - cf[b][j]) / (float)n; float a = fabsf(dc_[b][j]); if (a > m) m = a; }
        mv[b] = m > 4e-7f;                                   // a step of < 3e-5 a block (DRIFT) is not interpolated
        if (!mv[b]) { bp[b].a1 = nx[0]; bp[b].a2 = nx[1]; bp[b].a3 = nx[2]; bp[b].k = nx[3]; }
        if (frz_s < 0.5f) w_s[b] += 0.05f * (band_w[b] - w_s[b]);       // frozen: the spotlight stays
        w[b] = w_s[b] * (1.0f + tight * (sec_small(wc) - 1.0f));
    }
    // ---- per block: the loops
    int mode = (int)P(P_MODE);
    float fb_t_t = mode == 1 ? 0.0f : mode == 2 ? 0.6f : 0.92f;
    float room_t = clampf(P(P_ROOM) * 0.01f, 0.0f, 1.0f);
    float fb_r_t = fb_t_t * room_t;
    fb_t_t *= 1.0f - room_t;
    if (frz_t > 0.5f && fb_t_t + fb_r_t < 0.9f) { fb_t_t = 0.92f * (1.0f - room_t); fb_r_t = 0.92f * room_t; }   // frozen in AIR: the inner loop holds it
    float delay_t = clampf(P(P_SIZE), 0.002f, 0.25f) * (float)AUDIO_SR;
    damp_a += 0.1f * (0.58f - damp_a);                                  // the walls: about 4.5 kHz
    shift_s += 0.05f * (clampf(P(P_SHIFT), -20.0f, 20.0f) - shift_s);
    float wr = TWO_PI * shift_s / (float)AUDIO_SR, rc = cosf(wr), rs = sinf(wr);
    { float m = 1.0f / sqrtf(rot_c * rot_c + rot_s * rot_s); rot_c *= m; rot_s *= m; }
    bool shifting = fabsf(shift_s) > 0.005f;
    float drive_t = drive_req;
    if (frz_t > 0.5f && frz_s < 0.01f) frz_drive = fmaxf(drive_s, 1.25f);
    if (frz_t > 0.5f) drive_t = frz_drive;
    float swell = P(P_SWELL);
    float up = 1.0f - expf(-1.0f / ((float)AUDIO_SR * swell)), down = 1.0f - expf(-1.0f / ((float)AUDIO_SR * 0.05f));
    float mic_t = live ? 1.0f - frz_t : 0.0f;                             // the mic is the live layer's
    float seed_amp = 0.0005f * (1.0f - frz_t);                           // a whisper (-66 dB) to start on
    int col_t = (int)P(P_COLOUR);
    if (col_t != col_cur && col_x >= 1.0f) { col_prev = col_cur; col_cur = col_t; col_x = 0.0f; }
    float heat0 = heat_s, dheat = (P(P_HEAT) - heat_s) / (float)n;
    heat_s = P(P_HEAT);
    float send0 = send_s, dsend = (P(P_SEND) * 0.01f - send_s) / (float)n;
    send_s = P(P_SEND) * 0.01f;
    // the governor: on the log of the level, down fast, up no faster than SWELL allows
    float target_db = P(P_LEVEL), target = exp2f(target_db * 0.16609640f);
    {
        float env_db = env > 1e-6f ? 20.0f * log10f(env) : -120.0f;
        float err = target_db - env_db;
        if (err < 0) agc_db += fmaxf(err * 0.05f, -0.6f);
        else agc_db += fminf(err * 0.02f, bdt * 30.0f / swell);
        agc_db = clampf(agc_db, -60.0f, 0.0f);
    }
    float g1 = exp2f(agc_db * 0.16609640f), g = g_s, dg = (g1 - g_s) / (float)n;
    g_s = g1;
    brake = fminf(1.0f, brake * exp2f(bdt * 15.0f / swell));
    float t0_ = tgt_s;
    tgt_s += 0.15f * (target - tgt_s);
    const float dt_ = (tgt_s - t0_) / (float)n;
    const float rise = 1.000432f;                                        // 120 dB/s at most
    const float fstep = 1.0f / 1600.0f;                                  // in or out over 50 ms

    float blv[NB] = { 0 }, opk = 0;
    for (int i = 0; i < n; i++) {
        g += dg;
        drive_s += (drive_t > drive_s ? up : down) * (drive_t - drive_s);
        fb_t_s += 0.002f * (fb_t_t - fb_t_s);
        fb_r_s += 0.002f * (fb_r_t - fb_r_s);
        mic_s += 0.002f * (mic_t - mic_s);
        frz_s += 0.0005f * (frz_t - frz_s);
        delay_s += 0.0004f * (delay_t - delay_s);
        float x = in[i] * mic_s + seed_amp * rng_bi(&seed) + fb_t_s * y_prev + fb_r_s * room_out;
        float s = 0;
        for (int b = 0; b < NB; b++) {
            if (mv[b]) {
                bp[b].a1 = cf[b][0] += dc_[b][0]; bp[b].a2 = cf[b][1] += dc_[b][1];
                bp[b].a3 = cf[b][2] += dc_[b][2]; bp[b].k = cf[b][3] += dc_[b][3];
            }
            if (w[b] < 1e-4f) continue;
            float v = svf_bp(&bp[b], x, NULL) * w[b];
            s += v;
            float a = fabsf(v); if (a > blv[b]) blv[b] = a;
        }
        float v = s * drive_s * g * brake;
        // the colour saturates at 1.3x HOWL LEVEL: the loop limits its own amplitude there, whatever the gain
        float tg = t0_ + dt_ * (float)i, ts = 1.3f * tg, ceil_ = 1.25f * tg;
        float h = heat0 + dheat * (float)i, u = v * h / ts;
        float y = ts * colour(col_cur, u);
        if (col_x < 1.0f) { y = lerpf(ts * colour(col_prev, u), y, col_x); col_x += 1.0f / 160.0f; }
        y = dcb_run(&dcb, y);
        if (!(fabsf(y) < 4.0f)) y = 0.0f;
        float a = fabsf(y);
        env += (a > env ? 0.05f : 0.0004f) * (a - env);
        if (env > 1.5f * tg) brake *= 0.996f;
        y_prev = flush_denormal(y);
        room_lp = flush_denormal(room_lp + damp_a * (y - room_lp));
        dline_write(&room_dl, room_lp);
        float r = dline_read(&room_dl, delay_s);
        if (shifting) {                                    // Hilbert pair (path A one sample late), turning phasor: + is up
            float ia = ap4(&hil_a, HA, r);
            float qb = ap4(&hil_b, HB, r);
            float i_ = hil_a_z; hil_a_z = ia;
            float c = rot_c, sn = rot_s;
            rot_c = c * rc - sn * rs; rot_s = c * rs + sn * rc;
            r = i_ * c + qb * sn;
        }
        room_out = r;
        // what leaves the layer: rises at most 120 dB/s, never passes 1.25x HOWL LEVEL, fades in and out
        env_o += (a > env_o ? 0.2f : 0.0015f) * (a - env_o);
        if (env_o > allow) allow *= rise; else allow = env_o;
        if (allow < 1e-4f) allow = 1e-4f;
        float og_t = env_o > allow ? allow / env_o : 1.0f;
        og += 0.03f * (og_t - og);
        if (fade < fade_t) { fade += fstep; if (fade > fade_t) fade = fade_t; }
        else if (fade > fade_t) { fade -= fstep; if (fade < fade_t) fade = fade_t; }
        float o = ceil_ * soft_knee(y * og / ceil_) * fade;
        dry[i] += o;
        send[i] += o * (send0 + dsend * (float)i);
        float ao = fabsf(o); if (ao > opk) opk = ao;
    }
    if (brake < 1e-3f) brake = 1e-3f;
    for (int b = 0; b < NB; b++) band_lvl[b] = band_lvl[b] * 0.85f + blv[b] * 0.15f;
    howl_lvl = env;
    out_lvl = opk > out_lvl ? opk : out_lvl * 0.9f;
    if (opk > peak_hold) peak_hold = opk;
}

// ---------------------------------------------------------------- UI side
static int   spot = 2;
static float spot_vis = 2.0f;
static int   nb_now = 6;
static float notes[NB];                     // the bands' notes (MIDI)
static bool  notes_ok;
static int   chord_sig = -1;
static char  chord_name[16];
static int   sounding = -1, last_cc = -1, cc_ms;
static float ratio_s, u_gain = 0.3f, u_thresh;

// The chord's notes as candidates around the octave, then the window of BANDS consecutive candidates that
// moves the bands least: notes the chords share stay put, the others take the nearest step.
static void voice_chord(void)
{
    int pcs[12], npc = 0;
    for (int i = 0; i < song.chord_n && i < 5; i++) {
        int pc = ((song.chord[i] % 12) + 12) % 12;
        bool dup = false;
        for (int k = 0; k < npc; k++) if (pcs[k] == pc) dup = true;
        if (!dup) pcs[npc++] = pc;
    }
    if (!npc) pcs[npc++] = song.key;
    int nb = clampi((int)P(P_BANDS), 3, NB), oct = (int)P(P_OCT);
    int sig = nb * 1000 + oct * 100 + npc;
    for (int k = 0; k < npc; k++) sig = sig * 13 + pcs[k];
    if (sig == chord_sig && notes_ok) return;
    static int last_oct = -1;
    bool fresh = !notes_ok || nb != nb_now || oct != last_oct;      // a new register: from the root up
    chord_sig = sig; last_oct = oct;
    int anchor = 12 * (oct + 1) + pcs[0];
    float L[64]; int nl = 0;
    for (int note = anchor - 12; note <= anchor + 48 && nl < 64; note++)
        for (int k = 0; k < npc; k++) if (((note % 12) + 12) % 12 == pcs[k] && note >= 36 && note <= 103) { L[nl++] = (float)note; break; }
    int best_s = 0; float best = 1e9f;
    for (int s = 0; s + nb <= nl; s++) {
        if (fabsf(L[s] - (float)anchor) > 7.0f) continue;               // keep the register near OCTAVE
        float cost = 0;
        if (fresh) cost = fabsf(L[s] - (float)anchor);
        else for (int b = 0; b < nb; b++) cost += fabsf(L[s + b] - notes[b]);
        if (cost < best) { best = cost; best_s = s; }
    }
    for (int b = 0; b < nb && best_s + b < nl; b++) notes[b] = L[best_s + b];
    nb_now = nb; notes_ok = true;
    // its name: the root and whether the third is major or minor
    static const char *nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int r = pcs[0], q = 0;
    for (int k = 1; k < npc; k++) { int iv = (pcs[k] - r + 12) % 12; if (iv == 4) q = 1; else if (iv == 3 && q != 1) q = 2; }
    bool dim = false;
    for (int k = 1; k < npc; k++) if ((pcs[k] - r + 12) % 12 == 6) dim = true;
    snprintf(chord_name, sizeof chord_name, "%s%s", nm[r], q == 1 ? " MAJ" : q == 2 ? (dim ? " DIM" : " MIN") : "");
}

static void tick(const ctl_t *c, bool live)
{
    voice_chord();
    // live: tip for gain, lean or the stick for the spotlight; not live: keep what they last set
    if (live) {
        float u = 0.5f, want = spot_vis;
        if (c->has_imu) {
            u = clampf((c->lean_y + 15.0f) / 50.0f, 0.0f, 1.0f);
            want = clampf((c->lean_x + 35.0f) / 70.0f, 0.0f, 1.0f) * (float)(nb_now - 1);
        } else want = (float)(nb_now - 1) * 0.5f;
        if (c->has_joy) { u += 0.35f * c->jy; if (fabsf(c->jx) > 0.15f) want = (c->jx * 0.5f + 0.5f) * (float)(nb_now - 1); }
        if (fabsf(want - (float)spot) > 0.6f) spot = (int)lroundf(want);
        u_gain = clampf(u + (P(P_GAIN) - 50.0f) * 0.01f, 0.0f, 1.0f);
        static float ax, ay;
        if (fabsf(c->lean_x - ax) > 4.0f || fabsf(c->lean_y - ay) > 4.0f) { ax = c->lean_x; ay = c->lean_y; ui_awake(); }
    }
    spot = clampi(spot, 0, nb_now - 1);
    spot_vis += 0.25f * ((float)spot - spot_vis);
    for (int b = 0; b < NB; b++) {
        float d = (float)b - (float)spot;
        band_w[b] = b < nb_now ? 0.4f + 0.6f * expf(-d * d / 0.5f) : 0.0f;
        band_hz[b] = midi_hz(b < nb_now ? notes[b] : notes[nb_now - 1]);
    }
    drive_req = 0.3f * expf(2.59f * u_gain);                  // 0.3 .. 4: HOWL's measured range
    int mode = (int)P(P_MODE);
    float fb = mode == 1 ? 0.0f : mode == 2 ? 0.6f : 0.92f;
    u_thresh = fb > 0 ? clampf(logf(1.0f / (fb * 0.3f)) / 2.59f, 0.0f, 1.0f) : -1.0f;

    // how much it howls: the screen, the LED and MIDI (HOWL's channel)
    float target = exp2f(P(P_LEVEL) * 0.16609640f);
    float r = sleeping ? 0.0f : clampf(howl_lvl / target, 0.0f, 1.5f);
    ratio_s += (r > ratio_s ? 0.3f : 0.05f) * (r - ratio_s);
    int best = 0; float bl = 0;
    for (int b = 0; b < nb_now; b++) if (band_lvl[b] > bl) { bl = band_lvl[b]; best = b; }
    int note = (int)lroundf(notes[best]);
    bool audible = !sleeping && (live || P(P_FREEZE) >= 1.0f);
    if (audible && ratio_s > 0.45f && note != sounding) {
        if (sounding >= 0) host_midi_note(L_HOWL, sounding, 0);
        host_midi_note(L_HOWL, note, clampi(40 + (int)(ratio_s * 80.0f), 1, 127));
        sounding = note;
    } else if ((ratio_s < 0.25f || !audible) && sounding >= 0) { host_midi_note(L_HOWL, sounding, 0); sounding = -1; }
    if ((cc_ms += 10) >= 20) {
        cc_ms = 0;
        int cv = audible ? clampi((int)(ratio_s * 127.0f), 0, 127) : 0;
        if (abs(cv - last_cc) >= 2 || (cv != last_cc && (cv == 0 || cv == 127))) { host_midi_cc(L_HOWL, 11, cv); last_cc = cv; }
    }
    if (live) { float l = clampf(ratio_s, 0.0f, 1.0f); led_rgb((uint8_t)(10 + 200 * l), (uint8_t)(4 + 160 * l * l), (uint8_t)(50 + 170 * l)); }
}

static void changed(int p) { if (p < 0 || p == P_BANDS || p == P_OCT) notes_ok = false; }
static void clear(void) { }

// ---------------------------------------------------------------- drawing (rows 16..63; the host has the header)
static uint8_t star_x[24], star_y[24], star_p[24];
static bool stars_ok;
static parts_t parts;
static uint32_t pseed = 0x51ed27u;
static float head_a;
static int  arc_cd;
static float last_lvl[NB];
typedef struct { float x, y, vx, vy, age; } arc_t;
static arc_t arcs[12];
static int narcs;

static void draw_stars(float t)
{
    if (!stars_ok) {
        uint32_t s = 0x9e3779b9u;
        for (int i = 0; i < 24; i++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            star_x[i] = (uint8_t)(s % 128u); star_y[i] = (uint8_t)(17 + (s >> 8) % 26u); star_p[i] = (uint8_t)(s >> 16);
        }
        stars_ok = true;
    }
    for (int i = 0; i < 24; i++) {
        float tw = sin_turns(t * (0.2f + (float)(star_p[i] & 7) * 0.09f) + (float)star_p[i] * 0.01f);
        if (tw < -0.55f) continue;
        int x = star_x[i], y = star_y[i];
        oled_pixel(x, y, true);
        if ((star_p[i] & 0x30) == 0x30 && tw > 0.7f && y > 17) { oled_pixel(x - 1, y, true); oled_pixel(x + 1, y, true); oled_pixel(x, y - 1, true); oled_pixel(x, y + 1, true); }
    }
}

static void fill_poly(const float *px, const float *py, int n, bool on)
{
    float ymin = 1e9f, ymax = -1e9f;
    for (int i = 0; i < n; i++) { if (py[i] < ymin) ymin = py[i]; if (py[i] > ymax) ymax = py[i]; }
    for (int y = (int)ceilf(ymin - 0.5f); y <= (int)floorf(ymax + 0.5f); y++) {
        if (y < 16) continue;
        float yc = (float)y, xs[16]; int k = 0;
        for (int i = 0; i < n && k < 16; i++) {
            float x0 = px[i], y0 = py[i], x1 = px[(i + 1) % n], y1 = py[(i + 1) % n];
            if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) xs[k++] = x0 + (yc - y0) / (y1 - y0) * (x1 - x0);
        }
        for (int i = 1; i < k; i++) for (int j = i; j > 0 && xs[j] < xs[j - 1]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < k; i += 2)
            for (int x = (int)lroundf(xs[i]); x <= (int)lroundf(xs[i + 1]); x++) oled_pixel(x, y, on);
    }
}

// HOWL's wolf: sitting, facing right, (x, g) its rump on the ground; head 0 level .. 1 howling at the sky.
static void wolf(float x, float g, float head, float mouth, float tail, float *mx, float *my)
{
    float px[16], py[16];
    static const float bx[] = { 0, -1, 1, 5, 9, 12, 15, 18, 19, 20, 21 };
    static const float by[] = { 0, -5, -9, -12, -14, -16, -17, -15, -11, -6, 0 };
    for (int i = 0; i < 11; i++) { px[i] = x + bx[i]; py[i] = g + by[i]; }
    fill_poly(px, py, 11, true);
    oled_disc((int)lroundf(x + 6), (int)lroundf(g - 5), 5, true);
    for (int k = 1; k <= 5; k++) oled_pixel((int)lroundf(x + 18), (int)lroundf(g - (float)k), false);
    oled_pixel((int)lroundf(x + 11), (int)lroundf(g - 1), false);
    float tx[] = { x + 1, x - 3, x - 7 + tail, x - 10 + tail, x - 12 + tail, x - 11 + tail, x - 6, x };
    float ty[] = { g - 4, g - 6, g - 6, g - 4, g - 1, g, g, g };
    fill_poly(tx, ty, 8, true);
    float an = head * 0.9f, ca = cosf(an), sa = sinf(an), nx = x + 15, ny = g - 15;
#define ROT(i, lx, ly) (px[i] = nx + (lx) * ca + (ly) * sa, py[i] = ny - (lx) * sa + (ly) * ca)
    static const float hx[] = { -3, -1, 3, 7, 12, 14, 14, 9, 6, 2, -3 };
    static const float hy[] = { 0, -3, -4, -3, -2, -1, 1, 1, 2, 4, 3 };
    for (int i = 0; i < 11; i++) { float ly = hy[i]; if (i == 6 || i == 7) ly += mouth * (i == 6 ? 3.0f : 1.5f); ROT(i, hx[i], ly); }
    fill_poly(px, py, 11, true);
    if (mouth > 0.25f) { ROT(0, 8, 0.5f); ROT(1, 14, -0.3f); ROT(2, 13, 0.6f + 2.4f * mouth); fill_poly(px, py, 3, false); }
    ROT(0, 0, -3); ROT(1, 1, -8); ROT(2, 3, -4); fill_poly(px, py, 3, true);
    ROT(0, 3, -4); ROT(1, 5, -8); ROT(2, 6, -3.5f); fill_poly(px, py, 3, true);
    ROT(0, 5, -1.5f); oled_pixel((int)lroundf(px[0]), (int)lroundf(py[0]), false);
    ROT(0, 14.5f, 0); *mx = px[0]; *my = py[0];
#undef ROT
}

static void hill(int x0, int x1, int top, int base)
{
    for (int x = x0; x <= x1; x++) {
        float u = ((float)(x - x0) / (float)(x1 - x0)) * 2.0f - 1.0f;
        int h = (int)lroundf((float)top + (float)(base - top) * u * u);
        for (int y = h; y <= base; y++) if (((x + y) & 1) == 0 || y == h) oled_pixel(x, y, true);
    }
}

static void moon(int cx, int cy, int r, float glow)
{
    for (int k = 1; k <= 3; k++) {
        if (glow < 0.25f * (float)k) break;
        int rr = r + 2 * k + (int)(glow * 2.0f);
        for (int a = 0; a < 360; a += 360 / (8 + rr * 2)) {
            float t = (float)a * 0.0174533f;
            int gx = cx + (int)lroundf((float)rr * cosf(t)), gy = cy + (int)lroundf((float)rr * sinf(t));
            if ((((a / 9) + k) & 1) && gy >= 17) oled_pixel(gx, gy, true);
        }
    }
    oled_disc(cx, cy, r, true);
    oled_pixel(cx - r / 3, cy - r / 3, false); oled_pixel(cx + r / 4, cy + r / 3, false);
    oled_pixel(cx + r / 3, cy - r / 4, false); oled_pixel(cx - r / 4 + 1, cy + r / 2, false);
}

static void arcs_step_draw(float dt, float stop_x)
{
    int j = 0;
    for (int i = 0; i < narcs; i++) {
        arc_t a = arcs[i];
        a.age += dt; a.x += a.vx * dt; a.y += a.vy * dt;
        if (a.age > 1.6f || a.x > stop_x || a.y < 12.0f) continue;
        float dir = atan2f(a.vy, a.vx) * 57.29578f, r = 2.0f + a.age * 5.0f;
        for (float an = dir - 50.0f; an <= dir + 50.0f; an += 40.0f / (r + 1.0f)) {
            float t = an * 0.0174533f;
            int px = (int)lroundf(a.x + r * cosf(t)), py = (int)lroundf(a.y + r * sinf(t));
            if (py >= 17 && (a.age < 1.2f || ((px + py) & 1))) oled_pixel(px, py, true);
        }
        arcs[j++] = a;
    }
    narcs = j;
}

static void strings(float t, int x0, int x1, int y0, int yb)
{
    float span = (float)(x1 - x0) / (float)(nb_now > 1 ? nb_now - 1 : 1);
    float target = exp2f(P(P_LEVEL) * 0.16609640f);
    for (int b = 0; b < nb_now; b++) {
        int xb = x0 + (int)lroundf(span * (float)b);
        float lv = sleeping ? 0.0f : clampf(band_lvl[b] / (target * 1.2f), 0.0f, 1.0f);
        float A = 3.2f * sqrtf(lv), ph = sin_turns(t * (1.1f + 0.45f * (float)b));
        int m = 1 + b % 3, px = xb;
        bool hot = lv > 0.55f;
        if (hot) oled_dither(xb - 2, y0 + 2, 5, yb - y0 - 3, 1, true);
        for (int y = y0; y <= yb; y++) {
            float s = sin_turns(0.5f * (float)m * (float)(y - y0) / (float)(yb - y0));
            int x = xb + (int)lroundf(A * s * ph);
            if (y > y0) oled_line(px, y - 1, x, y, true); else oled_pixel(x, y, true);
            if (hot) oled_pixel(x + 1, y, true);
            px = x;
        }
        oled_pixel(xb, y0 - 1, true); oled_pixel(xb, yb + 1, true);
        if (lv > 0.55f && last_lvl[b] <= 0.55f) parts_burst(&parts, (float)xb, (float)y0, 6, 18.0f, 0.5f, PART_SPARK, &pseed);
        last_lvl[b] = lv;
    }
    int xs = x0 + (int)lroundf(span * spot_vis);
    char nm[6]; ui_note_name((int)lroundf(notes[clampi(spot, 0, nb_now - 1)]), nm, sizeof nm);
    int w = oled_ftext_w(F_TINY, nm, 1);
    oled_ftext_ex(F_TINY, clampi(xs - w / 2, x0 - 4, x1 + 4 - w), 16, nm, 1);
}

static void draw(const view_t *v)
{
    float dt = 1.0f / 25.0f, r = clampf(ratio_s, 0.0f, 1.2f);
    draw_stars(v->t);
    moon(113, 27, 6 + (int)lroundf(2.0f * clampf(r, 0, 1)), r);
    strings(v->t, 48, 96, 23, 41);
    hill(-8, 46, 42, 46);
    float want = smoothstep(0.25f, 0.8f, r);
    head_a += (want > head_a ? 0.25f : 0.08f) * (want - head_a);
    float mx, my;
    wolf(9, 43, head_a, smoothstep(0.35f, 0.9f, r), 1.5f * sin_turns(v->t * 0.3f), &mx, &my);
    if (r > 0.5f && --arc_cd <= 0 && narcs < 12) {          // the howl travels to the moon
        float dx = 113.0f - mx, dy = 27.0f - my, d = sqrtf(dx * dx + dy * dy) + 1e-3f, sp = 34.0f + 14.0f * r;
        arcs[narcs++] = (arc_t){ mx + 1.0f, my, dx / d * sp, dy / d * sp, 0 };
        arc_cd = r > 0.9f ? 5 : 8;
    }
    arcs_step_draw(dt, 100.0f);
    parts_step(&parts, dt, 0.0f, 0.3f);
    parts_draw(&parts);
    if (v->cover == 0) {                                     // mode, the chord, the gain gauge; the bands' notes
        oled_hline_pat(44, 46, 84, 0x11, true);
        bool frz = P(P_FREEZE) >= 1.0f;
        int x = oled_pill(F_SMALL, 1, 48, frz ? "FROZEN" : mode_l[(int)P(P_MODE)], frz) + 4;
        ui_text_fit(F_SMALL, x, 50, 88 - x, chord_name);
        int gx = 92, gw = 34;
        oled_rframe(gx, 49, gw, 7, 2);
        oled_rect(gx + 2, 51, (int)lroundf(u_gain * (float)(gw - 4)), 3, true);
        if (u_thresh >= 0) { int tx = gx + 2 + (int)lroundf(u_thresh * (float)(gw - 4)); oled_rect(tx, 48, 1, 9, true); oled_pixel(tx, 51, false); oled_pixel(tx, 53, false); }
        char b[48] = ""; int k = 0;
        for (int i = 0; i < nb_now && k < 40; i++) { char nm[6]; ui_note_name((int)lroundf(notes[i]), nm, sizeof nm); k += snprintf(b + k, sizeof b - (size_t)k, "%s%s", i ? " " : "", nm); }
        ui_text_fit(F_TINY, 2, 58, 124, b);
    }
}

static void icon(int x, int y, bool lit)
{
    (void)lit;                                               // a wolf's head howling at the moon
    static const char *const rows[9] = {
        "........###.",
        ".......#####",
        "...#....###.",
        "..##.##.....",
        "..#####.....",
        ".#####......",
        "######......",
        "#######.....",
        "########....",
    };
    ui_sprite(x, y, rows, 9, false, true);
}

const layer_t layer_howl = {
    .name = "HOWL", .letter = 'w', .pages = pages, .npages = 2, .params = params, .nparams = P_N,
    .events = false, .init = init, .audio = audio, .tick = tick, .draw = draw, .icon = icon,
    .changed = changed, .clear = clear,
};
