// SING's vocoder (inst_vocoder.c), input stage and FX bus (inst_fx.c) on the host, with nothing else of the
// instrument: a synthetic singer (glottal pulses through vowel formants, s and t noise, gaps), sines, noise and a
// speaker-to-mic feedback loop, each run through the modules block by block as the audio task would, checked
// with numbers, timed, and written to out-fx/*.wav for listening.
//
//   tools/dsp/run_fx.sh        builds and runs; non-zero on any failure
#include "sing.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SR    AUDIO_SR
#define BLK   AUDIO_BLOCK
#define PI_F  3.14159265f

// ---------------------------------------------------------------- the platform's stand-ins
void *app_alloc(size_t n) { return calloc(1, n); }
void *app_alloc_fast(size_t n) { return calloc(1, n); }
void app_free(void *p) { free(p); }
void app_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); printf("        [log] "); vprintf(fmt, ap); printf("\n"); va_end(ap); }
uint32_t audio_frames(void) { return 0; }

// ---------------------------------------------------------------- checks
static int nrun, nfail;
static void check(bool ok, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("  %s  ", ok ? "ok  " : "FAIL"); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    nrun++; if (!ok) nfail++;
}
static void section(const char *s) { printf("\n== %s\n", s); }

// ---------------------------------------------------------------- buffers, measures, WAV
typedef struct { float *x; int n; } buf_t;
static buf_t mkbuf(float s) { buf_t b; b.n = ((int)(s * SR) / BLK) * BLK; b.x = calloc((size_t)b.n, sizeof(float)); return b; }
static int ix(const buf_t *b, float t) { int i = (int)(t * SR); return i < 0 ? 0 : i > b->n ? b->n : i; }
static float rms(const buf_t *b, float t0, float t1)
{
    int a = ix(b, t0), c = ix(b, t1); double e = 0;
    for (int i = a; i < c; i++) e += (double)b->x[i] * b->x[i];
    return c > a ? (float)sqrt(e / (c - a)) : 0.0f;
}
static float peak(const buf_t *b, float t0, float t1)
{
    int a = ix(b, t0), c = ix(b, t1); float p = 0;
    for (int i = a; i < c; i++) if (fabsf(b->x[i]) > p) p = fabsf(b->x[i]);
    return p;
}
static float db(float x) { return 20.0f * log10f(x + 1e-12f); }
static float goertzel(const buf_t *b, float t0, float t1, float f)
{
    // one bin of a Hann-windowed DFT, summed directly (a long Goertzel cancels itself into noise)
    int a = ix(b, t0), c = ix(b, t1);
    double w = 2.0 * M_PI * f / SR, re = 0, im = 0;
    for (int i = a; i < c; i++) {
        double hann = 0.5 - 0.5 * cos(2.0 * M_PI * (i - a) / (double)(c - a));
        re += b->x[i] * hann * cos(w * i); im += b->x[i] * hann * sin(w * i);
    }
    return (float)((re * re + im * im) / ((double)(c - a) * (c - a)));
}
// energy in a band, by a pair of biquads over the stretch
static float band_rms(const buf_t *b, float t0, float t1, float flo, float fhi)
{
    biquad_t hp = { 0 }, lp = { 0 }; biquad_set(&hp, BQ_HP, flo, 0.707f, 0); biquad_set(&lp, BQ_LP, fhi, 0.707f, 0);
    int a = ix(b, t0 - 0.02f), c = ix(b, t1), a1 = ix(b, t0); double e = 0;
    for (int i = a; i < c; i++) { float y = biquad_run(&lp, biquad_run(&hp, b->x[i])); if (i >= a1) e += (double)y * y; }
    return c > a1 ? (float)sqrt(e / (c - a1)) : 0.0f;
}
// as tools/sim's m_click: the biggest step just after tc against the biggest step around it (> 3 is a click)
static float click(const buf_t *b, float tc)
{
    float sp = 0, cx = 0;
    int a = ix(b, tc), c = ix(b, tc + 0.006f);
    for (int i = a > 0 ? a : 1; i < c; i++) { float d = fabsf(b->x[i] - b->x[i - 1]); if (d > sp) sp = d; }
    int c0 = ix(b, tc - 0.10f), c1 = ix(b, tc - 0.004f), c2 = ix(b, tc + 0.04f), c3 = ix(b, tc + 0.14f);
    for (int i = c0 > 0 ? c0 : 1; i < c1; i++) { float d = fabsf(b->x[i] - b->x[i - 1]); if (d > cx) cx = d; }
    for (int i = c2; i < c3; i++) { float d = fabsf(b->x[i] - b->x[i - 1]); if (d > cx) cx = d; }
    return sp / (cx + 0.004f);
}
static int bad_count(const buf_t *b) { int n = 0; for (int i = 0; i < b->n; i++) { uint32_t u; memcpy(&u, &b->x[i], 4); if ((u & 0x7f800000u) == 0x7f800000u) n++; } return n; }
static int total_bad;
static void wav(const char *name, const buf_t *b)
{
    char fn[256]; snprintf(fn, sizeof fn, "%s.wav", name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    int n = b->n; uint32_t bytes = (uint32_t)n * 2u, v32; uint16_t v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16; fwrite(&v32, 4, 1, f); v16 = 1; fwrite(&v16, 2, 1, f); fwrite(&v16, 2, 1, f);
    v32 = SR; fwrite(&v32, 4, 1, f); v32 = SR * 2; fwrite(&v32, 4, 1, f); v16 = 2; fwrite(&v16, 2, 1, f); v16 = 16; fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (int i = 0; i < n; i++) { float x = b->x[i]; x = x > 1 ? 1 : x < -1 ? -1 : x; int16_t s = (int16_t)(x * 32767.0f); fwrite(&s, 2, 1, f); }
    fclose(f);
}

// ---------------------------------------------------------------- the synthetic singer
// A script of segments: a vowel at a note (with vibrato), an s, a t, or silence. Glottal source: harmonics up
// to 8 kHz falling 12 dB an octave, through three formant band passes for the vowel; s and t: high-passed noise.
enum { SEG_SIL, SEG_A, SEG_I, SEG_U, SEG_S, SEG_T };
typedef struct { int kind; float dur, note, vib; } seg_t;
static uint32_t nseed = 0x1234567u;
static float noise1(void) { return rng_bi(&nseed); }
static const float formants[4][3] = { { 0, 0, 0 }, { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 } };
typedef struct { float t; float ph; biquad_t f[3], ns1, ns2; float amp, vowel_mix[3]; } singer_t;
// renders the script into a buffer at level lv (peak-ish) starting at t0; returns the end time
static float sing_into(buf_t *b, float t0, const seg_t *sc, int nsc, float lv)
{
    float t = t0, ph = 0, amp = 0, f0 = 220;
    biquad_t fb[3] = { 0 }; float fz[3] = { 0 };
    biquad_t hs = { 0 }; biquad_set(&hs, BQ_HP, 4500.0f, 0.707f, 0);
    biquad_t ls = { 0 }; biquad_set(&ls, BQ_LP, 9500.0f, 0.707f, 0);
    for (int s = 0; s < nsc; s++) {
        const seg_t *g = &sc[s];
        int a = ix(b, t), c = ix(b, t + g->dur);
        if (g->kind >= SEG_A && g->kind <= SEG_U) for (int k = 0; k < 3; k++) { biquad_set(&fb[k], BQ_BP, formants[g->kind][k], 8.0f, 0); fz[k] = k == 0 ? 1.0f : k == 1 ? 0.6f : 0.35f; }
        for (int i = a; i < c; i++) {
            float tt = (float)(i - a) / SR, rem = g->dur - tt;
            float y = 0;
            if (g->kind >= SEG_A && g->kind <= SEG_U) {
                float want = fminf(1.0f, fminf(tt / 0.03f, rem / 0.03f));
                amp += (want - amp) * 0.003f;
                f0 = midi_hz(g->note + g->vib * sinf(2.0f * PI_F * 5.5f * tt) + 0.03f * noise1());
                ph += f0 / SR; if (ph >= 1.0f) ph -= 1.0f;
                float src = 0;
                int nh = (int)(8000.0f / f0);
                for (int h = 1; h <= nh; h++) src += sinf(2.0f * PI_F * ph * (float)h) / (float)(h * h);
                for (int k = 0; k < 3; k++) y += biquad_run(&fb[k], src) * fz[k];
                y = y * amp * lv * 2.2f + 0.004f * lv * noise1() * amp;    // a little breath
            } else if (g->kind == SEG_S || g->kind == SEG_T) {
                float env = g->kind == SEG_S ? fminf(1.0f, fminf(tt / 0.02f, rem / 0.03f)) : expf(-tt / 0.006f);
                y = biquad_run(&ls, biquad_run(&hs, noise1())) * env * lv * (g->kind == SEG_S ? 0.9f : 1.6f);
                amp = 0;
            } else amp = 0;
            b->x[i] += y;
        }
        t += g->dur;
    }
    return t;
}
// a phrase: s-a, t-i, s-u, with gaps
static const seg_t phrase[] = {
    { SEG_SIL, 0.25f, 0, 0 }, { SEG_S, 0.14f, 0, 0 }, { SEG_A, 0.6f, 57, 0.25f }, { SEG_T, 0.03f, 0, 0 },
    { SEG_I, 0.5f, 60, 0.3f }, { SEG_SIL, 0.2f, 0, 0 }, { SEG_S, 0.16f, 0, 0 }, { SEG_U, 0.8f, 64, 0.3f }, { SEG_SIL, 0.4f, 0, 0 },
};
#define NPHRASE ((int)(sizeof phrase / sizeof phrase[0]))
static float phrase_start(int k) { float t = 0; for (int i = 0; i < k; i++) t += phrase[i].dur; return t; }

// ---------------------------------------------------------------- running the modules over a buffer
static voc_params_t vp;
static void voc_defaults(void)
{
    memset(&vp, 0, sizeof vp);
    vp.carrier = VOC_CHORD; vp.chord[0] = 60; vp.chord[1] = 64; vp.chord[2] = 67; vp.chord_n = 3; vp.note = 0; vp.octave = 0;
    vp.bands = 16; vp.sharp = 0.5f; vp.release_ms = 60; vp.sibilance = 0.5f; vp.bright = 0.5f; vp.detune = 8;
}
typedef void (*hook_t)(float t);                           // called before each block (parameter changes)
static void run_voc(const buf_t *in, buf_t *out, hook_t h)
{
    for (int i = 0; i < in->n; i += BLK) {
        if (h) h((float)i / SR);
        vocoder_set(&vp);
        vocoder_process(in->x + i, out->x + i, BLK);
    }
}
static fx_in_params_t ip;
static fx_bus_params_t bp;
static void in_defaults(void) { ip = (fx_in_params_t){ .gate = 0, .comp = 0, .guard = 0, .deess = 0 }; }
static void bus_defaults(void)
{
    bp = (fx_bus_params_t){ .echo = 0, .echo_frames = 12000, .echo_fb = 0.4f, .echo_tone = 0.5f, .echo_duck = 0, .reverb = 0,
                            .reverb_s = 1.8f, .reverb_tone = 0.5f, .shimmer = 0, .throw_ = 0 };
}
static void run_in(const buf_t *in, buf_t *out, float out_env, hook_t h)
{
    for (int i = 0; i < in->n; i += BLK) {
        if (h) h((float)i / SR);
        fx_set_input(&ip);
        memcpy(out->x + i, in->x + i, BLK * sizeof(float));
        fx_input(out->x + i, BLK, out_env);
    }
}
// the bus: out = wet only (io starts at zero)
static void run_bus(const buf_t *send, buf_t *out, hook_t h)
{
    for (int i = 0; i < send->n; i += BLK) {
        if (h) h((float)i / SR);
        fx_set_bus(&bp);
        memset(out->x + i, 0, BLK * sizeof(float));
        fx_bus(send->x + i, out->x + i, BLK);
    }
}
// the modules keep state; a fresh start between scenarios: a stretch of silence through them
static void flush_voc(void) { buf_t z = mkbuf(1.0f), o = mkbuf(1.0f); run_voc(&z, &o, NULL); free(z.x); free(o.x); }
static void flush_in(void) { buf_t z = mkbuf(2.0f), o = mkbuf(2.0f); for (int i = 0; i < z.n; i++) z.x[i] = 1e-4f * noise1(); in_defaults(); run_in(&z, &o, 0, NULL); free(z.x); free(o.x); }
static void flush_bus(void) { buf_t z = mkbuf(12.0f), o = mkbuf(12.0f); bus_defaults(); bp.reverb_s = 0.3f; run_bus(&z, &o, NULL); free(z.x); free(o.x); }

static double now_us(void) { struct timespec a; clock_gettime(CLOCK_MONOTONIC, &a); return (double)a.tv_sec * 1e6 + (double)a.tv_nsec * 1e-3; }

// ================================================================ the vocoder
static float sw_t; static int sw_mode;
static void hook_chord(float t) { if (t >= sw_t && sw_mode == 0) { vp.chord[0] = 65; vp.chord[1] = 69; vp.chord[2] = 72; sw_mode = 1; } }
static void hook_note(float t) { vp.note = t < sw_t ? 60.0f : 67.0f; }
static int jp_which; static float jp_a, jp_b;
static void apply_jump(float v)
{
    switch (jp_which) {
    case 0: vp.carrier = (int)v; break;
    case 1: vp.octave = (int)v; break;
    case 2: vp.bands = v; break;
    case 3: vp.sharp = v; break;
    case 4: vp.release_ms = v; break;
    case 5: vp.sibilance = v; break;
    case 6: vp.bright = v; break;
    case 7: vp.detune = v; break;
    case 8: vp.chord_n = (int)v; vp.chord[3] = 71; break;
    case 9: vp.note = v; break;
    }
}
static float jp_t[4]; static int jp_n;
static void hook_jump(float t) { float v = jp_a; for (int k = 0; k < jp_n; k++) if (t >= jp_t[k]) v = (k & 1) ? jp_a : jp_b; apply_jump(v); }

static void test_vocoder(void)
{
    section("vocoder: pitch, envelope, level");
    // a long "a" at 150 Hz (not a chord note) through the chord C4 E4 G4
    buf_t v = mkbuf(3.0f), o = mkbuf(3.0f);
    seg_t aa[] = { { SEG_SIL, 0.2f, 0, 0 }, { SEG_A, 2.6f, 50.3f, 0.2f }, { SEG_SIL, 0.2f, 0, 0 } };
    sing_into(&v, 0, aa, 3, 0.25f);
    voc_defaults(); vp.sibilance = 0; flush_voc();
    run_voc(&v, &o, NULL);
    total_bad += bad_count(&o);
    wav("vocoder_chord_a", &o);
    const float chord_hz[3] = { 261.63f, 329.63f, 392.0f }, off_hz[4] = { 233.08f, 293.66f, 349.23f, 369.99f };
    float on = 0, off = 0;
    for (int k = 0; k < 3; k++) on += goertzel(&o, 0.8f, 2.6f, chord_hz[k]);
    for (int k = 0; k < 4; k++) off += goertzel(&o, 0.8f, 2.6f, off_hz[k]);
    on /= 3.0f; off /= 4.0f;
    float voice150 = goertzel(&o, 0.8f, 2.6f, 150.0f);
    check(10.0f * log10f(on / (off + 1e-20f)) > 15.0f, "CHORD: the chord's notes stand %.1f dB over the notes between them", (double)(10.0f * log10f(on / (off + 1e-20f))));
    check(10.0f * log10f(on / (voice150 + 1e-20f)) > 20.0f, "the voice's own pitch (150 Hz) is %.1f dB under them", (double)(10.0f * log10f(on / (voice150 + 1e-20f))));
    float lv_in = rms(&v, 0.8f, 2.6f), lv_out = rms(&o, 0.8f, 2.6f);
    check(fabsf(db(lv_out / lv_in)) < 4.0f, "its level sits at the voice's (%+.1f dB)", (double)db(lv_out / lv_in));
    free(v.x); free(o.x);

    // the envelope: 0.4 s on, 0.4 s off
    v = mkbuf(3.6f); o = mkbuf(3.6f);
    seg_t gate[] = { { SEG_SIL, 0.2f, 0, 0 }, { SEG_A, 0.4f, 52, 0.2f }, { SEG_SIL, 0.4f, 0, 0 }, { SEG_A, 0.4f, 55, 0.2f }, { SEG_SIL, 0.4f, 0, 0 },
                     { SEG_I, 0.4f, 57, 0.2f }, { SEG_SIL, 0.4f, 0, 0 }, { SEG_U, 0.4f, 52, 0.2f }, { SEG_SIL, 0.6f, 0, 0 } };
    sing_into(&v, 0, gate, 9, 0.25f);
    voc_defaults(); vp.sibilance = 0; flush_voc();
    run_voc(&v, &o, NULL);
    float son = 0, soff = 0;
    for (int k = 0; k < 4; k++) { float t = 0.2f + 0.8f * (float)k; son += rms(&o, t + 0.1f, t + 0.35f); soff += rms(&o, t + 0.4f + 0.15f, t + 0.78f); }
    check(db(son / (soff + 1e-9f)) > 30.0f, "it follows the voice: %.1f dB between sung and silent stretches", (double)db(son / (soff + 1e-9f)));
    // envelope correlation in 10 ms frames
    {
        int fr = SR / 100, nf = v.n / fr; double sxy = 0, sxx = 0, syy = 0, mx = 0, my = 0;
        float *ex = malloc(sizeof(float) * (size_t)nf), *ey = malloc(sizeof(float) * (size_t)nf);
        for (int f = 0; f < nf; f++) { double a = 0, c = 0; for (int i = 0; i < fr; i++) { a += fabs(v.x[f * fr + i]); c += fabs(o.x[f * fr + i]); } ex[f] = (float)a; ey[f] = (float)c; mx += a; my += c; }
        mx /= nf; my /= nf;
        for (int f = 0; f < nf; f++) { sxy += (ex[f] - mx) * (ey[f] - my); sxx += (ex[f] - mx) * (ex[f] - mx); syy += (ey[f] - my) * (ey[f] - my); }
        float r = (float)(sxy / sqrt(sxx * syy + 1e-30));
        check(r > 0.9f, "its envelope follows the voice's (correlation %.3f)", (double)r);
        free(ex); free(ey);
    }
    wav("vocoder_gated", &o);
    free(v.x); free(o.x);

    section("vocoder: bands, sharp, release");
    v = mkbuf(2.0f);
    seg_t ai[] = { { SEG_SIL, 0.1f, 0, 0 }, { SEG_A, 0.8f, 52, 0.2f }, { SEG_I, 0.8f, 55, 0.2f }, { SEG_SIL, 0.3f, 0, 0 } };
    sing_into(&v, 0, ai, 4, 0.25f);
    float ref = rms(&v, 0.3f, 1.6f);
    const float bands[4] = { 8, 12, 16, 20 }, sharps[3] = { 0, 0.5f, 1 };
    float worst = 0;
    char line[160] = ""; int lp = 0;
    for (int bi = 0; bi < 4; bi++) for (int si = 0; si < 3; si++) {
        o = mkbuf(2.0f);
        voc_defaults(); vp.sibilance = 0; vp.bands = bands[bi]; vp.sharp = sharps[si]; flush_voc();
        run_voc(&v, &o, NULL);
        float d = db(rms(&o, 0.3f, 1.6f) / ref);
        if (fabsf(d) > worst) worst = fabsf(d);
        if (si == 1) lp += snprintf(line + lp, sizeof line - (size_t)lp, " %d bands %+.1f dB", (int)bands[bi], (double)d);
        free(o.x);
    }
    check(worst < 4.0f, "8..20 bands, sharp 0..1: level within %.1f dB of the voice's (%s)", (double)worst, line);
    // sharp rings longer, release holds longer
    seg_t stop[] = { { SEG_SIL, 0.1f, 0, 0 }, { SEG_A, 0.8f, 45, 0.0f }, { SEG_SIL, 0.6f, 0, 0 } };
    buf_t vs = mkbuf(1.5f); sing_into(&vs, 0, stop, 3, 0.25f);
    // a hard stop: cut the voice at 0.9 s
    for (int i = ix(&vs, 0.9f); i < vs.n; i++) vs.x[i] = 0;
    float tails[2];
    for (int k = 0; k < 2; k++) {
        o = mkbuf(1.5f); voc_defaults(); vp.sibilance = 0; vp.release_ms = 5; vp.sharp = (float)k; flush_voc();
        run_voc(&vs, &o, NULL);
        tails[k] = rms(&o, 0.91f, 0.95f) / rms(&o, 0.6f, 0.88f);
        free(o.x);
    }
    check(tails[1] > 1.5f * tails[0], "SHARP 1 rings on after the voice stops (%.3f of its level 10..50 ms after, against %.3f at 0)", (double)tails[1], (double)tails[0]);
    float rel[2]; const float rms_[2] = { 5, 300 };
    for (int k = 0; k < 2; k++) {
        o = mkbuf(1.5f); voc_defaults(); vp.sibilance = 0; vp.release_ms = rms_[k]; flush_voc();
        run_voc(&vs, &o, NULL);
        rel[k] = rms(&o, 0.95f, 1.05f) / rms(&o, 0.6f, 0.88f);
        if (k == 1) wav("vocoder_release300", &o);
        free(o.x);
    }
    check(db(rel[1] / (rel[0] + 1e-9f)) > 20.0f, "RELEASE 300 ms holds the band %.1f dB longer than 5 ms (50..150 ms after)", (double)db(rel[1] / (rel[0] + 1e-9f)));
    free(vs.x); free(v.x);

    section("vocoder: sibilance");
    v = mkbuf((float)phrase_start(NPHRASE) + 0.1f);
    sing_into(&v, 0, phrase, NPHRASE, 0.25f);
    float s_t0 = phrase_start(1) + 0.03f, s_t1 = phrase_start(2) - 0.03f;   // the first s
    float a_t0 = phrase_start(2) + 0.15f, a_t1 = phrase_start(3) - 0.1f;    // the a after it
    float hf_s[2], hf_a[2];
    for (int k = 0; k < 2; k++) {
        o = mkbuf((float)v.n / SR); voc_defaults(); vp.sibilance = (float)k; flush_voc();
        run_voc(&v, &o, NULL);
        hf_s[k] = band_rms(&o, s_t0, s_t1, 5000, 12000); hf_a[k] = band_rms(&o, a_t0, a_t1, 5000, 12000);
        if (k == 1) wav("vocoder_phrase", &o);
        total_bad += bad_count(&o);
        free(o.x);
    }
    check(db(hf_s[1] / (hf_s[0] + 1e-9f)) > 10.0f, "SIBILANCE 1 lets the s through: %+.1f dB above 5 kHz in it", (double)db(hf_s[1] / (hf_s[0] + 1e-9f)));
    check(fabsf(db(hf_a[1] / (hf_a[0] + 1e-9f))) < 1.5f, "and leaves the vowels alone: %+.1f dB above 5 kHz in the a", (double)db(hf_a[1] / (hf_a[0] + 1e-9f)));
    wav("voice_phrase", &v);
    free(v.x);

    section("vocoder: note, noise, changes");
    v = mkbuf(2.4f); o = mkbuf(2.4f);
    seg_t a2[] = { { SEG_SIL, 0.1f, 0, 0 }, { SEG_A, 2.2f, 50, 0.2f }, { SEG_SIL, 0.1f, 0, 0 } };
    sing_into(&v, 0, a2, 3, 0.25f);
    voc_defaults(); vp.carrier = VOC_NOTE; vp.sibilance = 0; flush_voc();
    sw_t = 1.2f; run_voc(&v, &o, hook_note);
    float pn = goertzel(&o, 0.4f, 1.1f, 261.63f), pg = goertzel(&o, 1.4f, 2.1f, 392.0f), px = goertzel(&o, 0.4f, 1.1f, 392.0f);
    check(10.0f * log10f(pn / (px + 1e-20f)) > 15.0f && pg > 0.1f * pn, "NOTE: it sings the tuned note (C4, then G4)");
    check(click(&o, 1.2f) < 3.0f, "the note glides to its next without a click (%.2f)", (double)click(&o, 1.2f));
    float lvn = db(rms(&o, 0.4f, 2.0f) / rms(&v, 0.4f, 2.0f));
    check(fabsf(lvn) < 4.0f, "at the voice's level (%+.1f dB)", (double)lvn);
    wav("vocoder_note", &o);
    // note 0: silent
    memset(o.x, 0, sizeof(float) * (size_t)o.n);
    voc_defaults(); vp.carrier = VOC_NOTE; vp.note = 0; vp.sibilance = 0; flush_voc();
    run_voc(&v, &o, NULL);
    check(rms(&o, 0.4f, 2.0f) < 1e-4f, "NOTE with no note: silent (rms %.6f)", (double)rms(&o, 0.4f, 2.0f));
    // noise: not tonal
    voc_defaults(); vp.carrier = VOC_NOISE; vp.sibilance = 0; flush_voc();
    run_voc(&v, &o, NULL);
    {
        int a = ix(&o, 0.6f), n = 4096; double best = 0, e0 = 0;
        for (int i = 0; i < n; i++) e0 += (double)o.x[a + i] * o.x[a + i];
        for (int l = 40; l < 400; l++) { double c = 0; for (int i = 0; i < n; i++) c += (double)o.x[a + i] * o.x[a + i + l]; if (c / e0 > best) best = c / e0; }
        check(best < 0.5, "NOISE: a whisper, no pitch (autocorrelation %.2f)", best);
        float lvz = db(rms(&o, 0.4f, 2.0f) / rms(&v, 0.4f, 2.0f));
        check(fabsf(lvz) < 4.0f, "at the voice's level (%+.1f dB)", (double)lvz);
    }
    wav("vocoder_noise", &o);
    // CHORD + NOTE
    voc_defaults(); vp.carrier = VOC_CHORD_NOTE; vp.note = 55; vp.sibilance = 0; flush_voc();
    run_voc(&v, &o, NULL);
    float pc = goertzel(&o, 0.4f, 2.0f, 261.63f), pnn = goertzel(&o, 0.4f, 2.0f, 196.0f), pof = goertzel(&o, 0.4f, 2.0f, 233.08f);
    check(pc > 10.0f * pof && pnn > 10.0f * pof, "CHORD+NOTE: both the chord and the note sound (C4 %.1f dB, G3 %.1f dB over A#3)",
          (double)(10.0f * log10f(pc / (pof + 1e-20f))), (double)(10.0f * log10f(pnn / (pof + 1e-20f))));
    // a chord change mid-vowel
    voc_defaults(); vp.sibilance = 0; flush_voc(); sw_t = 1.2f; sw_mode = 0;
    run_voc(&v, &o, hook_chord);
    check(click(&o, 1.2f) < 3.0f, "a chord change (C to F) glides, no click (%.2f)", (double)click(&o, 1.2f));
    float pf = goertzel(&o, 1.5f, 2.2f, 349.23f), pe = goertzel(&o, 1.5f, 2.2f, 329.63f);
    check(pf > 10.0f * pe, "and lands on F (F4 %.1f dB over E4)", (double)(10.0f * log10f(pf / (pe + 1e-20f))));
    wav("vocoder_chord_change", &o);
    free(o.x);

    section("vocoder: every control end to end");
    // each control from one end to the other and back while the voice sings
    struct { const char *name; int which; float a, b; } J[] = {
        { "CARRIER chord/noise", 0, VOC_CHORD, VOC_NOISE }, { "CARRIER chord/note", 0, VOC_CHORD, VOC_NOTE },
        { "OCTAVE", 1, -2, 2 }, { "BANDS", 2, 8, 20 }, { "SHARP", 3, 0, 1 }, { "RELEASE", 4, 5, 300 },
        { "SIBILANCE", 5, 0, 1 }, { "BRIGHT", 6, 0, 1 }, { "DETUNE", 7, 0, 30 }, { "CHORD NOTES", 8, 1, 4 }, { "NOTE", 9, 40, 84 },
    };
    int nj = (int)(sizeof J / sizeof J[0]), bad = 0; float worstc = 0; const char *wn = "";
    for (int j = 0; j < nj; j++) {
        o = mkbuf(2.4f);
        voc_defaults(); vp.note = 60; if (J[j].which == 9) vp.carrier = VOC_NOTE;
        flush_voc();
        jp_which = J[j].which; jp_a = J[j].a; jp_b = J[j].b; jp_n = 2; jp_t[0] = 0.8f; jp_t[1] = 1.6f;
        run_voc(&v, &o, hook_jump);
        float c1 = click(&o, 0.8f), c2 = click(&o, 1.6f), c = fmaxf(c1, c2);
        if (c > worstc) { worstc = c; wn = J[j].name; }
        if (c > 3.0f) { bad++; printf("        %s: %.2f\n", J[j].name, (double)c); }
        total_bad += bad_count(&o);
        free(o.x);
    }
    check(bad == 0, "no click when any of %d controls jumps end to end (worst %.2f, %s)", nj, (double)worstc, wn);
    free(v.x);

    section("vocoder: cost");
    v = mkbuf(6.0f); o = mkbuf(6.0f);
    seg_t lng[] = { { SEG_A, 6.0f, 50, 0.2f } };
    sing_into(&v, 0, lng, 1, 0.25f);
    // the best of five passes over 6 s (other work on this machine only ever adds time)
    voc_defaults(); flush_voc();
    vocoder_set(&vp);
    double us16 = 1e9, us20 = 1e9;
    for (int r = 0; r < 5; r++) {
        double t0 = now_us();
        for (int i = 0; i < v.n; i += BLK) vocoder_process(v.x + i, o.x + i, BLK);
        double u = (now_us() - t0) / (v.n / BLK); if (u < us16) us16 = u;
    }
    voc_defaults(); vp.bands = 20; vp.carrier = VOC_CHORD_NOTE; vp.chord_n = 4; vp.chord[3] = 71; vp.note = 55; flush_voc();
    vocoder_set(&vp);
    for (int i = 0; i < v.n; i += BLK) vocoder_process(v.x + i, o.x + i, BLK);   // let the bands settle at 20
    for (int r = 0; r < 5; r++) {
        double t0 = now_us();
        for (int i = 0; i < v.n; i += BLK) vocoder_process(v.x + i, o.x + i, BLK);
        double u = (now_us() - t0) / (v.n / BLK); if (u < us20) us20 = u;
    }
    check(us16 <= 5.0, "%.2f us per block at 16 bands with the chord (budget 5)", us16);
    printf("        %.2f us per block at 20 bands, a four-note chord and the note\n", us20);
    float lv[24]; int nb = vocoder_bands(lv, 24);
    check(nb == 20, "vocoder_bands: %d bands for the screen", nb);
    free(v.x); free(o.x);
}

// ================================================================ the input stage
static void test_input(void)
{
    section("input: gate");
    // a phrase, then 2 s of quiet noise at -70 dBFS
    float plen = phrase_start(NPHRASE);
    buf_t v = mkbuf(plen + 2.5f), o = mkbuf(plen + 2.5f), o0 = mkbuf(plen + 2.5f);
    for (int i = 0; i < v.n; i++) v.x[i] = 0.00032f * noise1() * 1.7f;      // about -70 dBFS rms
    sing_into(&v, 0, phrase, NPHRASE, 0.25f);
    flush_in();
    in_defaults(); run_in(&v, &o0, 0, NULL);
    flush_in();
    in_defaults(); ip.gate = 1; run_in(&v, &o, 0, NULL);
    float quiet_in = rms(&v, plen + 1.0f, plen + 2.4f), quiet_out = rms(&o, plen + 1.0f, plen + 2.4f);
    check(db(quiet_in / (quiet_out + 1e-12f)) > 30.0f, "GATE 1: quiet noise taken down %.1f dB", (double)db(quiet_in / (quiet_out + 1e-12f)));
    float worstv = 0;
    for (int k = 2; k < NPHRASE; k += 2) if (phrase[k].kind != SEG_SIL) {
        float t0 = phrase_start(k) + 0.08f, t1 = phrase_start(k + 1) - 0.05f;
        float d = fabsf(db(rms(&o, t0, t1) / rms(&o0, t0, t1)));
        if (d > worstv) worstv = d;
    }
    check(worstv < 1.0f, "and leaves the singing alone (vowels within %.2f dB)", (double)worstv);
    // a click the gate made: over 3 and over what the singer did there anyway (the t is a click of its own)
    float wc = 0, wsrc = 0;
    for (int k = 1; k <= NPHRASE; k++) {
        float tc = k < NPHRASE ? phrase_start(k) : plen + 0.1f;
        float c = click(&o, tc), c0 = click(&o0, tc), ex = c - fmaxf(3.0f, c0 * 1.2f);
        if (ex > wc - 3.0f) { wc = ex + 3.0f; wsrc = c0; }
    }
    check(wc < 3.0f, "opening and closing make no click of their own (worst %.2f over the singing's own %.2f)", (double)wc, (double)wsrc);
    wav("input_gate", &o);
    free(v.x); free(o.x); free(o0.x);
    // chatter: a note dying away slowly from -20 to -70 dB over 3 s
    v = mkbuf(4.0f); o = mkbuf(4.0f);
    seg_t held[] = { { SEG_A, 3.6f, 52, 0.2f } };
    sing_into(&v, 0, held, 1, 0.3f);
    for (int i = 0; i < v.n; i++) { float t = (float)i / SR; v.x[i] *= powf(10.0f, -50.0f * fminf(t, 3.0f) / 3.0f / 20.0f); v.x[i] += 0.0003f * noise1(); }
    flush_in(); in_defaults(); ip.gate = 1;
    int flips = 0; bool was = true;
    for (int i = 0; i < v.n; i += BLK) {
        fx_set_input(&ip); memcpy(o.x + i, v.x + i, BLK * 4); fx_input(o.x + i, BLK, 0);
        bool open = fx_gate_open() > 0.999f;
        if (i == 0) was = open;
        else if (open != was) { if (!open) flips++; was = open; }   // closings
    }
    check(flips == 1, "a note dying away: the gate closes once (%d time%s, no chatter)", flips, flips == 1 ? "" : "s");
    free(v.x); free(o.x);

    section("input: compressor");
    float lev_in[2] = { -24.0f, -8.0f }, out1[2], out0[2];
    for (int c = 0; c < 2; c++) for (int k = 0; k < 2; k++) {
        v = mkbuf(1.5f); o = mkbuf(1.5f);
        float a = powf(10.0f, lev_in[k] / 20.0f) * 1.41421f;
        for (int i = 0; i < v.n; i++) v.x[i] = a * sinf(2.0f * PI_F * 440.0f * (float)i / SR);
        flush_in(); in_defaults(); ip.comp = (float)(1 - c); run_in(&v, &o, 0, NULL);
        float r = db(rms(&o, 1.0f, 1.45f));
        if (c == 0) out1[k] = r; else out0[k] = r;
        free(v.x); free(o.x);
    }
    float slope1 = (out1[1] - out1[0]) / (lev_in[1] - lev_in[0]), slope0 = (out0[1] - out0[0]) / (lev_in[1] - lev_in[0]);
    check(fabsf(slope1 - 1.0f / 3.0f) < 0.07f, "COMP 1: 16 dB more in is %.1f dB more out (3:1 is 5.3)", (double)(out1[1] - out1[0]));
    check(fabsf(slope0 - 1.0f) < 0.03f, "COMP 0: a straight wire (slope %.2f)", (double)slope0);
    {
        // the make-up keeps a voice at -18 dBFS where it was
        v = mkbuf(1.5f); o = mkbuf(1.5f);
        float a = powf(10.0f, -18.0f / 20.0f) * 1.41421f;
        for (int i = 0; i < v.n; i++) v.x[i] = a * sinf(2.0f * PI_F * 440.0f * (float)i / SR);
        flush_in(); in_defaults(); ip.comp = 1; run_in(&v, &o, 0, NULL);
        float d = db(rms(&o, 1.0f, 1.45f)) + 18.0f;
        check(fabsf(d) < 1.0f, "and its make-up keeps -18 dBFS where it was (%+.2f dB)", (double)d);
        free(v.x); free(o.x);
    }

    section("input: de-esser");
    v = mkbuf(plen + 0.2f);
    sing_into(&v, 0, phrase, NPHRASE, 0.25f);
    buf_t d1 = mkbuf(plen + 0.2f), d0 = mkbuf(plen + 0.2f);
    flush_in(); in_defaults(); run_in(&v, &d0, 0, NULL);
    flush_in(); in_defaults(); ip.deess = 1; run_in(&v, &d1, 0, NULL);
    float st0 = phrase_start(6) + 0.03f, st1 = phrase_start(7) - 0.03f;
    float cut = db(band_rms(&d0, st0, st1, 5000, 9000) / band_rms(&d1, st0, st1, 5000, 9000));
    float a0 = phrase_start(7) + 0.15f, a1 = phrase_start(8) - 0.1f;
    float vow = fabsf(db(rms(&d1, a0, a1) / rms(&d0, a0, a1)));
    check(cut > 6.0f, "DEESS 1: the s is %.1f dB softer in 5..9 kHz", (double)cut);
    check(vow < 0.5f, "the vowels keep their level (%.2f dB)", (double)vow);
    wav("input_deess", &d1);
    free(v.x); free(d1.x); free(d0.x);
}

// ---------------------------------------------------------------- the speaker guard
// The box: what goes out of the speaker comes back into the mic 2.5 ms later through a resonance (the case and
// the room) that peaks at 2.2 kHz with a gain over 1 there: a howl unless something stops it.
typedef struct { float *dl; int w; biquad_t res; float env; } room_t;
#define DL 4096
static float howl_level(const buf_t *b, float t0, float t1) { return band_rms(b, t0, t1, 1900, 2500); }
static void feedback_run(buf_t *mic_src, buf_t *out, float guard, float loop_gain, bool *engaged_at_ok, float *t_engage)
{
    room_t r; memset(&r, 0, sizeof r); r.dl = calloc(DL, sizeof(float));
    biquad_set(&r.res, BQ_BP, 2200.0f, 18.0f, 0);
    flush_in(); in_defaults(); ip.guard = guard; ip.gate = 0; ip.comp = 0;
    *t_engage = -1;
    float in[BLK];
    for (int i = 0; i < mic_src->n; i += BLK) {
        // the mic: the singer (or the room's noise) plus the speaker 2.5 ms ago through the room
        for (int j = 0; j < BLK; j++) {
            float spk = r.dl[(r.w + DL - 80) & (DL - 1)];
            in[j] = mic_src->x[i + j] + biquad_run(&r.res, spk) * loop_gain;
            r.dl[r.w] = 0; r.w = (r.w + 1) & (DL - 1);
        }
        fx_set_input(&ip);
        fx_input(in, BLK, r.env);
        // the speaker plays what the input stage gives it (the rest of SING is a wire here), clipped as an amp would
        float pk = 0;
        for (int j = 0; j < BLK; j++) {
            float s = soft_tanh(in[j] * 1.5f);
            out->x[i + j] = s;
            r.dl[(r.w + DL - BLK + j) & (DL - 1)] = s;
            if (fabsf(s) > pk) pk = fabsf(s);
        }
        r.env = pk > r.env ? pk : r.env * 0.97f;
        if (*t_engage < 0 && fx_guard_cut() > 10.0f) *t_engage = (float)i / SR;
    }
    (void)engaged_at_ok;
    free(r.dl);
}

static void test_guard(void)
{
    section("input: the speaker guard");
    // first a melody (the guard has heard no feedback yet: what it does here is all its own)
    // a sung melody with the speaker loud but no loop: the guard leaves it alone
    seg_t mel[] = {
        { SEG_A, 0.4f, 57, 0.3f }, { SEG_I, 0.4f, 59, 0.3f }, { SEG_A, 0.4f, 60, 0.3f }, { SEG_U, 1.5f, 64, 0.35f },
        { SEG_SIL, 0.1f, 0, 0 }, { SEG_A, 0.4f, 62, 0.3f }, { SEG_I, 0.4f, 60, 0.3f }, { SEG_A, 1.6f, 57, 0.4f },
        { SEG_U, 1.6f, 55, 0.0f }, { SEG_SIL, 0.3f, 0, 0 },
    };
    buf_t m = mkbuf(7.2f), g0 = mkbuf(7.2f), g1 = mkbuf(7.2f);
    sing_into(&m, 0, mel, 10, 0.3f);
    flush_in(); in_defaults(); run_in(&m, &g0, 0.3f, NULL);
    flush_in(); in_defaults(); ip.guard = 1;
    float cut_max = 0;
    for (int i = 0; i < m.n; i += BLK) {
        fx_set_input(&ip); memcpy(g1.x + i, m.x + i, BLK * 4); fx_input(g1.x + i, BLK, 0.3f);
        if (fx_guard_cut() > cut_max) cut_max = fx_guard_cut();
    }
    float worst = 0;
    for (float t = 0; t < 6.8f; t += 0.2f) { float a = rms(&g0, t, t + 0.2f); if (a < 0.003f) continue; float d = fabsf(db(rms(&g1, t, t + 0.2f) / a)); if (d > worst) worst = d; }
    check(cut_max < 0.5f && worst < 1.0f, "a melody (and a held note without vibrato) through it with the speaker loud: no cut (%.1f dB), within %.2f dB",
          (double)cut_max, (double)worst);
    wav("guard_melody", &g1);
    free(m.x); free(g0.x); free(g1.x);
    // the room's noise and a little voice to start it; the loop gain 1.6 at 2.2 kHz
    buf_t src = mkbuf(4.0f), off = mkbuf(4.0f), on = mkbuf(4.0f);
    for (int i = 0; i < src.n; i++) src.x[i] = 0.0005f * noise1();
    seg_t hum[] = { { SEG_SIL, 0.3f, 0, 0 }, { SEG_A, 0.5f, 55, 0.25f } };
    sing_into(&src, 0, hum, 2, 0.1f);
    bool dummy; float te0, te1;
    feedback_run(&src, &off, 0.0f, 1.6f, &dummy, &te0);
    feedback_run(&src, &on, 1.0f, 1.6f, &dummy, &te1);
    // when the howl became audible (over -30 dBFS around 2.2 kHz) without the guard
    float t_aud = -1;
    for (float t = 0; t < 3.9f; t += 0.01f) if (howl_level(&off, t, t + 0.02f) > 0.0316f) { t_aud = t; break; }
    float off_end = howl_level(&off, 3.0f, 3.9f), on_end = howl_level(&on, 3.0f, 3.9f);
    check(t_aud >= 0 && db(off_end) > -30.0f, "no guard: it howls (audible at %.2f s, %.1f dBFS at the end)", (double)t_aud, (double)db(off_end));
    float t_on_aud = -1;
    for (float t = 0; t < 3.9f; t += 0.01f) if (howl_level(&on, t, t + 0.02f) > 0.0316f) { t_on_aud = t; break; }
    float t_quiet = -1;
    if (t_on_aud >= 0) for (float t = t_on_aud; t < 3.9f; t += 0.01f) if (howl_level(&on, t, t + 0.05f) < 0.01f) { t_quiet = t; break; }
    check(te1 >= 0 && t_quiet >= 0 && t_quiet - t_on_aud <= 0.6f,
          "GUARD 1: the cut goes in at %.2f s, the howl is down 30 dB %.2f s after it was heard", (double)te1, (double)(t_quiet - t_on_aud));
    check(db(on_end) < -45.0f, "and stays gone: %.1f dBFS around 2.2 kHz at the end (%.1f without)", (double)db(on_end), (double)db(off_end));
    wav("guard_off", &off); wav("guard_on", &on);
    free(src.x); free(off.x); free(on.x);

}

// ================================================================ the FX bus
static void hook_echo_time(float t) { bp.echo_frames = t < 1.0f ? 12000.0f : 3000.0f; }
static int bj_which; static float bj_a, bj_b;
static void hook_bus_jump(float t)
{
    float v = (t >= 0.9f && t < 1.8f) ? bj_b : bj_a;
    switch (bj_which) {
    case 0: bp.echo = v; break;
    case 1: bp.echo_frames = v; break;
    case 2: bp.echo_fb = v; break;
    case 3: bp.echo_tone = v; break;
    case 4: bp.echo_duck = v; break;
    case 5: bp.reverb = v; break;
    case 6: bp.reverb_s = v; break;
    case 7: bp.reverb_tone = v; break;
    case 8: bp.shimmer = v; break;
    case 9: bp.throw_ = v; break;
    }
}
static int ij_which; static float ij_a, ij_b;
static void hook_in_jump(float t)
{
    float v = (t >= 0.9f && t < 1.8f) ? ij_b : ij_a;
    switch (ij_which) { case 0: ip.gate = v; break; case 1: ip.comp = v; break; case 2: ip.guard = v; break; case 3: ip.deess = v; break; }
}

static float rt60(const buf_t *b, float t0, float t1)
{
    // Schroeder backward integral of the mid band (a band pass at 1 kHz), T20 from -5 to -25 dB
    int a = ix(b, t0), c = ix(b, t1), n = c - a;
    float *y = malloc(sizeof(float) * (size_t)n); double *E = malloc(sizeof(double) * (size_t)n);
    biquad_t q = { 0 }; biquad_set(&q, BQ_BP, 1000.0f, 0.7f, 0);
    for (int i = 0; i < n; i++) y[i] = biquad_run(&q, b->x[a + i]);
    double s = 0; for (int i = n - 1; i >= 0; i--) { s += (double)y[i] * y[i]; E[i] = s; }
    int i5 = -1, i25 = -1;
    for (int i = 0; i < n; i++) { double d = 10.0 * log10(E[i] / E[0] + 1e-30); if (i5 < 0 && d <= -5.0) i5 = i; if (i25 < 0 && d <= -25.0) { i25 = i; break; } }
    free(y); free(E);
    if (i5 < 0 || i25 < 0) return -1;
    return 3.0f * (float)(i25 - i5) / SR;
}

static void test_bus(void)
{
    section("FX: echo");
    buf_t s = mkbuf(3.0f), o = mkbuf(3.0f);
    for (int i = 0; i < 64; i++) s.x[ix(&s, 0.2f) + i] = 0.5f * sinf(PI_F * (float)i / 64.0f) * sinf(2.0f * PI_F * 1000.0f * (float)i / SR);
    flush_bus(); bus_defaults(); bp.echo = 1; bp.echo_fb = 0.5f; bp.echo_tone = 1; bp.echo_frames = 9600;
    run_bus(&s, &o, NULL);
    float r[4]; int pos[4];
    for (int k = 0; k < 4; k++) {
        float tc = 0.2f + (float)(k + 1) * 0.3f; int a = ix(&o, tc - 0.02f), c = ix(&o, tc + 0.04f); float pk = 0; int at = a;
        for (int i = a; i < c; i++) if (fabsf(o.x[i]) > pk) { pk = fabsf(o.x[i]); at = i; }
        pos[k] = at - ix(&s, 0.2f); r[k] = rms(&o, (float)(at - 64) / SR, (float)(at + 64) / SR);
    }
    float rat = (r[1] / r[0] + r[2] / r[1] + r[3] / r[2]) / 3.0f;
    bool spaced = abs(pos[0] - 9600 - 32) < 40 && abs(pos[1] - pos[0] - 9600) < 6 && abs(pos[2] - pos[1] - 9600) < 6;
    check(spaced, "repeats every ECHO TIME (9600 frames): at %d, %d, %d", pos[0], pos[1], pos[2]);
    check(fabsf(rat - 0.5f) < 0.1f, "each %.2f of the last (REPEATS 0.5)", (double)rat);
    wav("echo_hit", &o);
    free(s.x); free(o.x);
    // the time changing under a held note glides
    s = mkbuf(2.0f); o = mkbuf(2.0f);
    seg_t a1[] = { { SEG_A, 2.0f, 55, 0.2f } }; sing_into(&s, 0, a1, 1, 0.25f);
    flush_bus(); bus_defaults(); bp.echo = 0.8f; bp.echo_fb = 0.6f;
    run_bus(&s, &o, hook_echo_time);
    check(click(&o, 1.0f) < 3.0f, "a new ECHO TIME glides like tape (click %.2f)", (double)click(&o, 1.0f));
    wav("echo_time_glide", &o);
    free(s.x); free(o.x);
    // ducking: the echo while you sing against in the gaps
    float plen = phrase_start(NPHRASE);
    s = mkbuf(plen + 1.0f);
    sing_into(&s, 0, phrase, NPHRASE, 0.25f);
    buf_t du0 = mkbuf(plen + 1.0f), du1 = mkbuf(plen + 1.0f);
    flush_bus(); bus_defaults(); bp.echo = 0.8f; bp.echo_fb = 0.5f; bp.echo_frames = 6000; run_bus(&s, &du0, NULL);
    flush_bus(); bus_defaults(); bp.echo = 0.8f; bp.echo_fb = 0.5f; bp.echo_frames = 6000; bp.echo_duck = 1; run_bus(&s, &du1, NULL);
    float t0 = phrase_start(7) + 0.3f, t1 = phrase_start(8) - 0.1f;                 // during the long u
    float tg0 = phrase_start(8) + 0.15f, tg1 = phrase_start(8) + 0.4f;             // the gap after it
    float under = db(rms(&du0, t0, t1) / rms(&du1, t0, t1)), gap = db(rms(&du0, tg0, tg1) / rms(&du1, tg0, tg1));
    check(under > 6.0f && gap < under - 4.0f, "DUCK 1: the echo %.1f dB down while you sing, %.1f dB in the gap after", (double)under, (double)gap);
    wav("echo_duck", &du1);
    free(du0.x); free(du1.x);
    // throw: longer and louder repeats while rolled, the tail rings out after
    buf_t th0 = mkbuf(plen + 3.0f), th1 = mkbuf(plen + 3.0f);
    buf_t s2 = mkbuf(plen + 3.0f); memcpy(s2.x, s.x, sizeof(float) * (size_t)s.n);
    flush_bus(); bus_defaults(); bp.echo = 0.4f; bp.echo_fb = 0.35f; bp.echo_frames = 9600; run_bus(&s2, &th0, NULL);
    flush_bus(); bus_defaults(); bp.echo = 0.4f; bp.echo_fb = 0.35f; bp.echo_frames = 9600; bp.throw_ = 1; run_bus(&s2, &th1, NULL);
    float tail = db(rms(&th1, plen + 0.5f, plen + 2.5f) / (rms(&th0, plen + 0.5f, plen + 2.5f) + 1e-9f));
    check(tail > 6.0f, "THROW 1: the tail after the phrase is %.1f dB louder and longer", (double)tail);
    wav("echo_throw", &th1);
    free(th0.x); free(th1.x); free(s2.x); free(s.x);

    section("FX: reverb and shimmer");
    const float rts[3] = { 0.8f, 2.0f, 5.0f };
    char line[160] = ""; int lp = 0; float worst = 0;
    for (int k = 0; k < 3; k++) {
        float len = rts[k] * 1.6f + 0.5f;
        s = mkbuf(len); o = mkbuf(len);
        s.x[ix(&s, 0.1f)] = 0.8f;
        flush_bus(); bus_defaults(); bp.reverb = 1; bp.reverb_s = rts[k]; bp.reverb_tone = 0.5f;
        run_bus(&s, &o, NULL);
        float rt = rt60(&o, 0.13f, len);
        float err = rt > 0 ? fabsf(rt / rts[k] - 1.0f) : 1.0f;
        if (err > worst) worst = err;
        lp += snprintf(line + lp, sizeof line - (size_t)lp, " %.1f s -> %.2f s", (double)rts[k], (double)rt);
        if (k == 1) wav("reverb_2s", &o);
        total_bad += bad_count(&o);
        free(s.x); free(o.x);
    }
    check(worst < 0.2f, "REVERB TIME within %.0f %% (%s)", (double)(worst * 100.0f), line);
    // its level against the send, on a steady voice
    s = mkbuf(3.0f); o = mkbuf(3.0f);
    seg_t a3[] = { { SEG_A, 3.0f, 55, 0.25f } }; sing_into(&s, 0, a3, 1, 0.25f);
    flush_bus(); bus_defaults(); bp.reverb = 1; run_bus(&s, &o, NULL);
    float rl = db(rms(&o, 1.5f, 2.9f) / rms(&s, 1.5f, 2.9f));
    check(rl > -15.0f && rl < 0.0f, "REVERB 1 sits %.1f dB under a steady voice", (double)-rl);
    free(s.x); free(o.x);
    // shimmer: an A4 in, its octave in the tail
    float sh[2];
    for (int k = 0; k < 2; k++) {
        s = mkbuf(3.0f); o = mkbuf(3.0f);
        for (int i = 0; i < ix(&s, 1.0f); i++) s.x[i] = 0.2f * sinf(2.0f * PI_F * 440.0f * (float)i / SR) * fminf(1.0f, (float)i / 300.0f) * fminf(1.0f, (float)(ix(&s, 1.0f) - i) / 300.0f);
        flush_bus(); bus_defaults(); bp.reverb = 1; bp.reverb_s = 3; bp.shimmer = (float)k;
        run_bus(&s, &o, NULL);
        sh[k] = goertzel(&o, 1.2f, 2.4f, 880.0f) / (goertzel(&o, 1.2f, 2.4f, 440.0f) + 1e-20f);
        if (k == 1) wav("shimmer", &o);
        free(s.x); free(o.x);
    }
    check(10.0f * log10f(sh[1] / (sh[0] + 1e-20f)) > 10.0f, "SHIMMER 1: the octave above is %.1f dB stronger in the tail", (double)(10.0f * log10f(sh[1] / (sh[0] + 1e-20f))));
    // the worst case: everything up, loud, for 12 s: it stays bounded
    s = mkbuf(12.0f); o = mkbuf(12.0f);
    seg_t lng[] = { { SEG_A, 6.0f, 55, 0.3f }, { SEG_SIL, 6.0f, 0, 0 } }; sing_into(&s, 0, lng, 2, 0.8f);
    flush_bus(); bus_defaults(); bp.echo = 1; bp.echo_fb = 0.95f; bp.echo_tone = 1; bp.reverb = 1; bp.reverb_s = 12; bp.reverb_tone = 1; bp.shimmer = 1; bp.throw_ = 1;
    run_bus(&s, &o, NULL);
    float pk = peak(&o, 0, 12.0f);
    int nb = bad_count(&o); total_bad += nb;
    check(pk <= 1.5f && nb == 0, "every control at its top, a loud voice, 12 s: the wet stays under its ceiling of 1.5 (peak %.2f)", (double)pk);
    wav("bus_everything", &o);
    free(s.x); free(o.x);

    section("FX: every control end to end");
    s = mkbuf(2.6f);
    seg_t a4[] = { { SEG_A, 2.6f, 55, 0.25f } }; sing_into(&s, 0, a4, 1, 0.25f);
    struct { const char *name; int which; float a, b; } J[] = {
        { "ECHO", 0, 0, 1 }, { "ECHO TIME", 1, 1000, 60000 }, { "REPEATS", 2, 0, 0.95f }, { "ECHO TONE", 3, 0, 1 }, { "DUCK", 4, 0, 1 },
        { "REVERB", 5, 0, 1 }, { "REVERB TIME", 6, 0.3f, 12 }, { "REVERB TONE", 7, 0, 1 }, { "SHIMMER", 8, 0, 1 }, { "THROW", 9, 0, 1 },
    };
    int nj = (int)(sizeof J / sizeof J[0]), bad = 0; float wc = 0; const char *wn = "";
    for (int j = 0; j < nj; j++) {
        o = mkbuf(2.6f);
        flush_bus(); bus_defaults(); bp.echo = 0.5f; bp.reverb = 0.4f;
        bj_which = J[j].which; bj_a = J[j].a; bj_b = J[j].b;
        // the output with the dry voice in it, as the host mixes it
        for (int i = 0; i < s.n; i += BLK) { hook_bus_jump((float)i / SR); fx_set_bus(&bp); memcpy(o.x + i, s.x + i, BLK * 4); fx_bus(s.x + i, o.x + i, BLK); }
        float c = fmaxf(click(&o, 0.9f), click(&o, 1.8f));
        if (c > wc) { wc = c; wn = J[j].name; }
        if (c > 3.0f) { bad++; printf("        %s: %.2f\n", J[j].name, (double)c); }
        total_bad += bad_count(&o);
        free(o.x);
    }
    check(bad == 0, "no click when any of %d FX controls jumps end to end (worst %.2f, %s)", nj, (double)wc, wn);
    struct { const char *name; int which; } K[] = { { "GATE", 0 }, { "COMP", 1 }, { "GUARD", 2 }, { "DEESS", 3 } };
    bad = 0; wc = 0; wn = "";
    for (int j = 0; j < 4; j++) {
        o = mkbuf(2.6f);
        flush_in(); in_defaults();
        ij_which = K[j].which; ij_a = 0; ij_b = 1;
        run_in(&s, &o, 0.2f, hook_in_jump);
        float c = fmaxf(click(&o, 0.9f), click(&o, 1.8f));
        if (c > wc) { wc = c; wn = K[j].name; }
        if (c > 3.0f) { bad++; printf("        %s: %.2f\n", K[j].name, (double)c); }
        free(o.x);
    }
    check(bad == 0, "no click when any of 4 input controls jumps end to end (worst %.2f, %s)", (double)wc, wn);
    free(s.x);

    section("FX: cost");
    s = mkbuf(6.0f); o = mkbuf(6.0f);
    seg_t a5[] = { { SEG_A, 6.0f, 55, 0.25f } }; sing_into(&s, 0, a5, 1, 0.25f);
    for (int i = 0; i < s.n; i++) s.x[i] += 0.02f * sinf(2.0f * PI_F * 2200.0f * (float)i / SR);   // something for the guard to chase
    flush_in(); flush_bus();
    ip = (fx_in_params_t){ .gate = 1, .comp = 1, .guard = 1, .deess = 1 };
    bus_defaults(); bp.echo = 0.6f; bp.echo_fb = 0.6f; bp.echo_duck = 0.5f; bp.reverb = 0.5f; bp.reverb_s = 3; bp.shimmer = 1; bp.throw_ = 0.5f;
    fx_set_input(&ip); fx_set_bus(&bp);
    float blk[BLK];
    double us = 1e9;
    for (int r = 0; r < 5; r++) {
        double t0 = now_us();
        for (int i = 0; i < s.n; i += BLK) {
            memcpy(blk, s.x + i, sizeof blk);
            fx_input(blk, BLK, 0.3f);
            memcpy(o.x + i, blk, sizeof blk);
            fx_bus(blk, o.x + i, BLK);
        }
        double u = (now_us() - t0) / (s.n / BLK);
        if (u < us) us = u;
    }
    check(us <= 6.0, "input stage + FX bus: %.2f us per block with everything on (budget 6)", us);
    float taps[8]; int nt = fx_echo_taps(taps, 8);
    check(nt == 8 && taps[0] > 0.0f, "fx_echo_taps: %d repeats for the screen (newest %.3f)", nt, (double)taps[0]);
    free(s.x); free(o.x);
}

int main(void)
{
    if (system("true")) { }
    vocoder_init();
    fx_init();
    test_vocoder();
    test_input();
    test_guard();
    test_bus();
    section("health");
    check(total_bad == 0, "no NaN or inf anywhere (%d)", total_bad);
    printf("\n%d checks, %d failed\n", nrun, nfail);
    return nfail ? 1 : 0;
}
