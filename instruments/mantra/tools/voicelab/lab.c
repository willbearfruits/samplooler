// tools/voicelab: VOICE on its own, fast: renders scenarios to out/*.wav and prints measurements.
//   ./build.sh && out/lab vowels | words | daisy | whistle | wsweep | wstep | sub | choir | c64 | effect | fxdebug | fric | env | burst | mwords | cost | all
#include "mantra.h"
#include "ana.h"
#include <time.h>

float pv[PARAMS_MAX];
int mod_base[M_N] = { 0, MOD_PARAMS_MAX };
uint32_t audio_frames(void) { return 0; }
extern uint32_t (*mv_clock)(void);
float mv_test_hz(int v); float mv_test_env(int v); void mv_test_formants(int v, float *f); void mv_test_reset(void);

#define SRD 32000.0
static uint32_t clk;
static uint32_t lab_clock(void) { return clk; }
static mv_ctl_t C;
static float *buf; static int bn, bcap;
static void (*tick_fn)(double t);
static mv_fx_t FX; static float (*mic_fn)(double t);
static float *micbuf;
static void run(double sec)
{
    int blocks = (int)(sec * SRD / AUDIO_BLOCK + 0.5);
    for (int b = 0; b < blocks; b++) {
        if ((clk % 320) == 0) { if (tick_fn) tick_fn(clk / SRD); mv_set(&C); }
        float out[AUDIO_BLOCK] = { 0 }, send[AUDIO_BLOCK] = { 0 }, mic[AUDIO_BLOCK];
        mv_process(out, send, AUDIO_BLOCK);
        if (mic_fn) {
            for (int i = 0; i < AUDIO_BLOCK; i++) mic[i] = mic_fn((clk + i) / SRD);
            mv_effect(mic, out, send, AUDIO_BLOCK, &FX);
        }
        if (bn + AUDIO_BLOCK > bcap) { bcap = bcap * 2 + 64000; buf = realloc(buf, bcap * sizeof(float)); micbuf = realloc(micbuf, bcap * sizeof(float)); }
        memcpy(buf + bn, out, sizeof out);
        if (mic_fn) memcpy(micbuf + bn, mic, sizeof mic); else memset(micbuf + bn, 0, sizeof mic);
        bn += AUDIO_BLOCK; clk += AUDIO_BLOCK;
    }
}

static int ptag(const char *t) { for (int i = 0; i < mod_voice.nparams; i++) if (!strcmp(mod_voice.params[i].tag, t)) return i; return -1; }
static void defaults(void) { for (int i = 0; i < mod_voice.nparams; i++) pv[i] = mod_voice.params[i].def; }
static void sound(const char *s)
{
    defaults();
    char b[512]; snprintf(b, sizeof b, "%s", s);
    for (char *tok = strtok(b, " "); tok; tok = strtok(NULL, " ")) {
        char *eq = strchr(tok, '='); if (!eq) continue; *eq = 0;
        int p = ptag(tok); if (p >= 0) pv[p] = (float)atof(eq + 1);
    }
}
static void ctl_default(void)
{
    memset(&C, 0, sizeof C);
    C.jaw = 0.5f; C.vib_rate = 5.5f; C.breath = 0.15f; C.level = 0.8f; C.send = 0.6f;
}
static double hz2midi(double f) { return 69 + 12 * log2(f / 440.0); }
static void push(int v, uint32_t at, int ph, double dur, double note, double lvl, int flags)
{
    mv_ev_t e = { at, (uint8_t)ph, (uint8_t)flags, (float)dur, (float)note, (float)lvl };
    while (mv_queued(v) >= 30) run(0.02);
    if (!mv_push(v, &e)) printf("  queue full\n");
}
// a word: consonants at their spoken length, the vowels share the rest of `dur`
static void say(int v, uint32_t at, const char *phs, double dur, double note, double lvl, int flags)
{
    uint8_t ph[32]; int n = mv_parse(phs, ph, 32);
    double cons = 0; int nv = 0;
    for (int i = 0; i < n; i++) { if (mv_ph_vowel(ph[i])) nv++; else cons += mv_ph_len(ph[i]); }
    double vd = nv ? (dur - cons) / nv : 0; if (vd < 0.06) vd = 0.06;
    for (int i = 0; i < n; i++) {
        double d = mv_ph_vowel(ph[i]) ? vd : mv_ph_len(ph[i]);
        push(v, i == 0 ? at : 0, ph[i], d, note, lvl, i == 0 ? flags : 0);
    }
}
static void reset_all(void) { mv_test_reset(); ctl_default(); defaults(); tick_fn = NULL; mic_fn = NULL; run(0.3); bn = 0; }
static void save(const char *name, int from, int to)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s.wav", name);
    double pk = peakabs(buf + from, to - from);
    wav_write(fn, buf + from, to - from, 1.0);
    printf("  -> %s (%.1f s, peak %.3f, rms %.4f)\n", fn, (to - from) / SRD, pk, rms(buf + from, to - from));
}

static const int vowels[] = { PH_IY, PH_IH, PH_EH, PH_AE, PH_AA, PH_AO, PH_UH, PH_UW, PH_AH, PH_ER };
static const double tF[][2] = { { 270, 2290 }, { 390, 1990 }, { 530, 1840 }, { 660, 1720 }, { 730, 1090 }, { 570, 840 }, { 440, 1020 }, { 300, 870 }, { 640, 1190 }, { 490, 1350 } };
static void t_vowels(double note)
{
    printf("== vowels at %.1f Hz\n", 440 * pow(2, (note - 69) / 12));
    reset_all(); sound("vbre=50 vvib=25"); 
    int worst = 0; double werr = 0;
    for (int k = 0; k < 10; k++) {
        int s0 = bn;
        push(0, 0, vowels[k], 0.9, note, 0.8, 0);
        run(1.05);
        double F[3]; formants(buf + s0 + 9600, 16000, SRD, 3, 12, 0.04, F);
        double e1 = (F[0] - tF[k][0]) / tF[k][0], e2 = (F[1] - tF[k][1]) / tF[k][1];
        double e = fmax(fabs(e1), fabs(e2));
        if (e > werr) { werr = e; worst = k; }
        printf("  %-3s F1 %4.0f (%4.0f, %+4.0f%%)  F2 %4.0f (%4.0f, %+4.0f%%)  F3 %4.0f   rms %.4f\n", mv_ph_name(vowels[k]), F[0], tF[k][0], e1 * 100, F[1], tF[k][1], e2 * 100, F[2], rms(buf + s0 + 9600, 16000));
    }
    printf("  worst %s %.1f %%\n", mv_ph_name(vowels[worst]), werr * 100);
    save(note < 50 ? "vowels-low" : "vowels", 0, bn);
}
static void t_words(void)
{
    printf("== words\n");
    reset_all(); sound("vtrc=5 vbre=35 vrou=0 vvib=15 vchr=10 vprs=45 vdri=8 vgld=40");
    double n = 50;  // D3
    say(0, 0, "D EY Z IY", 0.7, n + 7, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "D EY Z IY", 0.7, n + 4, 0.8, 0); push(0, 0, PH_SIL, 0.4, 0, 0, 0);
    push(0, 0, PH_OW, 0.9, n - 5, 0.8, 0); push(0, 0, PH_M, 0.6, n - 5, 0.8, 0); push(0, 0, PH_SIL, 0.4, 0, 0, 0);
    // BICYCLE BUILT FOR TWO
    say(0, 0, "B AY", 0.3, n + 7, 0.8, 0); say(0, 0, "S IH", 0.3, n + 4, 0.8, 0); say(0, 0, "K AH L", 0.3, n, 0.8, 0);
    say(0, 0, "B IH L T", 0.6, n + 2, 0.8, 0); say(0, 0, "F AO R", 0.3, n - 5, 0.8, 0); say(0, 0, "T UW", 1.4, n, 0.8, 0);
    push(0, 0, PH_SIL, 0.5, 0, 0, 0);
    // a few more consonants
    say(0, 0, "HH AH L OW", 0.8, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "SH IY S IH NG Z", 1.2, n + 2, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "P AA T AA K AA", 1.2, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "K IY P K UW T", 1.0, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "G OW D B AY", 1.0, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "M AA N IY", 0.9, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "CH ER CH", 0.8, n, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    say(0, 0, "Y EH L OW", 0.8, n + 2, 0.8, 0); push(0, 0, PH_SIL, 0.3, 0, 0, 0);
    run(13.0);
    save("words", 0, bn);
}
// Daisy Bell's chorus (MANTRA_NOTES.md), in G at 2 beats a second (a quarter = 0.5 s / 2: waltz)
typedef struct { double beat, len; const char *note; const char *ph; } syl_t;
static const syl_t daisy[] = {
    { 0, 3, "D5", "D EY" }, { 3, 3, "B4", "Z IY" }, { 6, 3, "G4", "D EY" }, { 9, 3, "D4", "Z IY" },
    { 12, 1, "E4", "G IH V" }, { 13, 1, "F#4", "M IY" }, { 14, 1, "G4", "Y AO R" }, { 15, 2, "E4", "AE N" }, { 17, 1, "G4", "S ER" },
    { 18, 5, "D4", "D UW" }, { 24, 3, "A4", "AY M" }, { 27, 3, "D5", "HH AE F" }, { 30, 3, "B4", "K R EY" }, { 33, 3, "G4", "Z IY" },
    { 36, 1, "E4", "AO L" }, { 37, 1, "F#4", "F AO R" }, { 38, 1, "G4", "DH AH" }, { 39, 2, "A4", "L AH V" }, { 41, 1, "B4", "AH V" },
    { 42, 5, "A4", "Y UW" }, { 47, 1, "B4", "IH T" }, { 48, 1, "C5", "W OW N T" }, { 49, 1, "B4", "B IY" }, { 50, 1, "A4", "AH" },
    { 51, 2, "D5", "S T AY" }, { 53, 1, "B4", "L IH SH" }, { 54, 1, "A4", "M EH" }, { 55, 4, "G4", "R IH JH" }, { 59, 1, "A4", "AY" },
    { 60, 2, "B4", "K AE N T" }, { 62, 1, "G4", "AH" }, { 63, 2, "E4", "F AO R D" }, { 65, 1, "G4", "AH" }, { 66, 1, "E4", "K EH" },
    { 67, 4, "D4", "R IH JH" }, { 71, 1, "D4", "B AH T" }, { 72, 2, "G4", "Y UW L" }, { 74, 1, "B4", "L UH K" }, { 75, 3, "A4", "S W IY T" },
    { 78, 2, "G4", "AH P" }, { 80, 0.5, "B4", "AA N" }, { 80.5, 0.5, "B4", "DH AH" }, { 81, 2, "A4", "S IY T" }, { 83, 0.5, "B4", "AH V" },
    { 83.5, 0.5, "C5", "AH" }, { 84, 1, "D5", "B AY" }, { 85, 1, "B4", "S IH" }, { 86, 1, "G4", "K AH L" }, { 87, 2, "A4", "B IH L T" },
    { 89, 1, "D4", "F AO R" }, { 90, 6, "G4", "T UW" },
};
static double note_of(const char *s)
{
    static const int pc[7] = { 9, 11, 0, 2, 4, 5, 7 };
    int n = pc[s[0] - 'A']; int i = 1;
    if (s[i] == '#') { n++; i++; } else if (s[i] == 'b') { n--; i++; }
    return 12 * (atoi(s + i) + 1) + n;
}
static void t_daisy(const char *snd, const char *name, double transpose)
{
    printf("== daisy (%s)\n", name);
    reset_all(); sound(snd);
    const double beat = 0.36;
    uint32_t t0 = clk + 3200;
    for (size_t k = 0; k < sizeof daisy / sizeof daisy[0]; k++) {
        const syl_t *s = &daisy[k];
        double len = s->len * beat * 0.97;
        uint32_t at = t0 + (uint32_t)(s->beat * beat * SRD);
        uint8_t ph[16]; int n = mv_parse(s->ph, ph, 16);
        // the consonants before the vowel come just before the beat
        int vi = 0; while (vi < n && !mv_ph_vowel(ph[vi])) vi++;
        double pre = 0; for (int i = 0; i < vi; i++) pre += mv_ph_len(ph[i]) * 0.8;
        double post = 0; for (int i = vi + 1; i < n; i++) post += mv_ph_len(ph[i]) * 0.8;
        uint32_t start = at - (uint32_t)(pre * SRD);
        double note = note_of(s->note) + transpose;
        for (int i = 0; i < n; i++) {
            double d = i < vi ? mv_ph_len(ph[i]) * 0.8 : i == vi ? fmax(0.08, len - post) : mv_ph_len(ph[i]) * 0.8;
            push(0, i == 0 ? start : 0, ph[i], d, note, 0.85, i == vi && k ? 0 : 0);
        }
        // the queue holds 32: play as we go
        while (mv_queued(0) > 20) run(0.05);
    }
    run(beat * 8);
    save(name, 0, bn);
}
static double mhz(double n) { return 440 * pow(2, (n - 69) / 12); }

static double ot_now;
static void ot_tick(double t) { (void)t; C.overtone = (float)ot_now; }
static void t_whistle(void)
{
    printf("== whistle on a 120 Hz drone\n");
    reset_all(); sound("vtrc=0 vbre=35 vrou=5 vvib=0 vshp=25 vprs=80 vdri=10 vgld=80");
    double n = hz2midi(120);
    C.v[0] = (mv_free_t){ true, (float)n, 0.8f }; C.jaw = 0.4f;
    tick_fn = ot_tick; ot_now = 0; run(1.0);
    int s_all = 0;
    for (int k = 6; k <= 12; k++) {
        ot_now = k; run(0.25); int s0 = bn; run(0.5);
        double f0 = mv_test_hz(0);
        double pk = pw_peak(buf + s0, 16000, SRD, k * f0, 6, NULL), lo = pw_peak(buf + s0, 16000, SRD, (k - 1) * f0, 6, NULL), hi = pw_peak(buf + s0, 16000, SRD, (k + 1) * f0, 6, NULL);
        double h1 = pw_peak(buf + s0, 16000, SRD, f0, 6, NULL), h2 = pw_peak(buf + s0, 16000, SRD, 2 * f0, 6, NULL), h3 = pw_peak(buf + s0, 16000, SRD, 3 * f0, 6, NULL);
        printf("  k=%2d f0 %.1f: over neighbours %+5.1f / %+5.1f dB; whistle vs h1 %+5.1f h2 %+5.1f h3 %+5.1f dB\n", k, f0, db(pk) - db(lo), db(pk) - db(hi), db(pk) - db(h1), db(pk) - db(h2), db(pk) - db(h3));
    }
    (void)s_all;
    // a melody: 6 8 9 10 9 8 6 then a glide 6..12
    static const double mel[] = { 6, 8, 9, 10, 12, 10, 9, 8, 9, 6, 8, 6 };
    for (int i = 0; i < 12; i++) { ot_now = mel[i]; run(0.45); }
    for (int i = 0; i < 300; i++) { ot_now = 6 + 6 * i / 300.0; run(0.01); }
    ot_now = 12; run(0.6);
    ot_now = 0; run(1.0);
    save("whistle", 0, bn);
    // sygyt
    reset_all(); sound("vtrc=-10 vbre=30 vrou=5 vvib=0 vshp=90 vprs=90 vdri=10 vgld=60");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(140), 0.8f }; C.jaw = 0.4f; tick_fn = ot_tick; ot_now = 0; run(0.8);
    static const double mel2[] = { 10, 12, 13, 12, 10, 9, 10, 12, 16, 15, 13, 12 };
    for (int i = 0; i < 12; i++) { ot_now = mel2[i]; run(0.4); }
    ot_now = 0; run(0.8);
    save("sygyt", 0, bn);
}
static void t_wsweep(void)
{
    printf("== whistle sweep: rms with/without (dB), prominence, vs strongest low harmonic\n");
    for (int si = 0; si < 5; si++) {
        double sh = si * 0.25;
        for (int k = 6; k <= 16; k += 5) {
            reset_all(); sound("vtrc=0 vbre=35 vrou=5 vvib=0 vprs=80 vdri=10 vshp=0");
            C.sharp = (float)sh; C.v[0] = (mv_free_t){ true, (float)hz2midi(si >= 3 ? 140 : 120), 0.8f }; C.jaw = 0.4f;
            tick_fn = ot_tick; ot_now = 0; run(0.6); int a0 = bn; run(0.5);
            ot_now = k; run(0.4); int b0 = bn; run(0.5);
            double f0 = mv_test_hz(0);
            double pk = pw_peak(buf + b0, 16000, SRD, k * f0, 6, NULL), lo = pw_peak(buf + b0, 16000, SRD, (k - 1) * f0, 6, NULL), hi = pw_peak(buf + b0, 16000, SRD, (k + 1) * f0, 6, NULL);
            double hm = 0; for (int h = 1; h <= 4; h++) { double q = pw_peak(buf + b0, 16000, SRD, h * f0, 6, NULL); if (q > hm) hm = q; }
            printf("  sharp %.2f k %2d: rms %+5.1f dB  over neighbours %+5.1f/%+5.1f  over low %+5.1f  peak %.3f\n", sh, k,
                   20 * log10(rms(buf + b0, 16000) / rms(buf + a0, 16000)), db(pk) - db(lo), db(pk) - db(hi), db(pk) - db(hm), peakabs(buf + b0, 16000));
        }
    }
}
static void t_sub(void)
{
    printf("== undertone\n");
    reset_all(); sound("vtrc=35 vbre=40 vrou=35 vvib=0 vund=95 vprs=85 vdri=15 vgld=150");
    double n = hz2midi(100);
    C.v[0] = (mv_free_t){ true, (float)n, 0.8f }; C.jaw = 0.0f;
    pv[ptag("vund")] = 0; run(1.0); int a0 = bn; run(1.0);
    pv[ptag("vund")] = 100; run(0.5); int b0 = bn; run(1.0);
    double f0 = mv_test_hz(0);
    printf("  f0 %.1f: at f0/2 %.1f dB without, %.1f dB with; f0 %.1f / %.1f; 1.5 f0 %.1f / %.1f\n", f0,
           db(pw_peak(buf + a0, 32000, SRD, f0 / 2, 3, NULL)), db(pw_peak(buf + b0, 32000, SRD, f0 / 2, 3, NULL)),
           db(pw_peak(buf + a0, 32000, SRD, f0, 3, NULL)), db(pw_peak(buf + b0, 32000, SRD, f0, 3, NULL)),
           db(pw_peak(buf + a0, 32000, SRD, 1.5 * f0, 3, NULL)), db(pw_peak(buf + b0, 32000, SRD, 1.5 * f0, 3, NULL)));
    // kargyraa: vowels moving slowly OO OH AH
    for (int i = 0; i < 400; i++) { C.jaw = (float)(0.25 - 0.25 * cos(i / 400.0 * 2 * M_PI * 1.5)); run(0.01); }
    C.v[0].on = false; run(0.8);
    // OM with undertone
    sound("vtrc=30 vbre=60 vrou=20 vvib=5 vund=45 vchr=25 vprs=55 vnas=15 vdri=25 vgld=120");
    say(0, 0, "OW M", 3.0, hz2midi(73), 0.8, 0); run(3.3);
    save("undertone", 0, bn);
}
static void t_choir(void)
{
    printf("== choir\n");
    for (int pass = 0; pass < 2; pass++) {
        reset_all(); sound(pass ? "vtrc=25 vbre=60 vrou=25 vvib=10 vund=20 vchr=70 vprs=50 vnas=15 vdri=35 vgld=120" : "vchr=0 vdri=0 vvib=0");
        C.choir = pass ? 0.3f : 0.0f;
        uint32_t at = clk + 6400;
        for (int v = 0; v < 5; v++) { push(v, at, PH_OW, 2.2, hz2midi(98) - (v == 4 ? 12 : 0), 0.7, 0); push(v, 0, PH_M, 1.2, 0, 0.7, 0); }
        double on[5] = { -1, -1, -1, -1, -1 }, hz[5] = { 0 };
        for (int b = 0; b < 300; b++) { run(0.002); for (int v = 0; v < 5; v++) if (on[v] < 0 && mv_test_env(v) > 0.002) on[v] = clk / SRD; }
        run(0.5); for (int v = 0; v < 5; v++) hz[v] = mv_test_hz(v);
        run(3.0);
        double lo = 1e9, hi = -1e9, clo = 1e9, chi = -1e9;
        for (int v = 0; v < 4; v++) { if (on[v] < lo) lo = on[v]; if (on[v] > hi) hi = on[v]; double c = 1200 * log2(hz[v] / mhz(hz2midi(98))); if (c < clo) clo = c; if (c > chi) chi = c; }
        printf("  choir %s: onsets spread %.1f ms, pitch spread %.1f cents  (onsets", pass ? "on" : "off", (hi - lo) * 1000, chi - clo);
        for (int v = 0; v < 5; v++) printf(" %.1f", (on[v] - at / SRD) * 1000);
        printf(" ms)\n");
        save(pass ? "choir" : "choir-off", 0, bn);
    }
}
static void t_c64(void)
{
    printf("== 8-bit\n");
    t_daisy("v8bt=100 vbre=0 vrou=0 vvib=0 vdri=0 vchr=0 vbit=5 vrat=8000 vgld=0", "daisy-8bit", 0);
    int s0 = 32000 * 3, n = 32000 * 6;
    double hi = 0, all = 0;
    for (double f = 100; f < 15000; f += 50) { double p = pw_at(buf + s0, n, SRD, f); all += p; if (f > 5000) hi += p; }
    printf("  above 5 kHz: %.1f dB of the whole\n", db(hi) - db(all));
}
// a synthetic singer for the effect: a pulse train through AH's formants
static double sing_ph, sing_hz = 150; static float sz[4][2];
static float singer(double t)
{
    (void)t;
    sing_ph += sing_hz * (1 + 0.004 * sin(2 * M_PI * 5 * t)) / SRD;
    double x = 0; if (sing_ph >= 1) { sing_ph -= 1; x = 1; }
    static const double F[4] = { 700, 1220, 2600, 3300 }, B[4] = { 80, 90, 120, 200 };
    double y = x;
    for (int k = 0; k < 4; k++) {
        double r = exp(-M_PI * B[k] / SRD), c = 2 * r * cos(2 * M_PI * F[k] / SRD), A = 1 - c + r * r;
        double o = A * y + c * sz[k][0] - r * r * sz[k][1]; sz[k][1] = sz[k][0]; sz[k][0] = (float)o; y = o;
    }
    return (float)(y * 0.05);
}
// a realistic singer: VOICE itself singing AH (rendered first), with a little room noise
static float *sung; static int sung_n, sung_i;
static float sung_mic(double t) { (void)t; static uint32_t r = 7; r ^= r << 13; r ^= r >> 17; r ^= r << 5; float x = sung_n ? sung[sung_i++ % sung_n] : 0; return x * 0.5f + (float)((int32_t)r / 2147483648.0 * 0.0003); }
static void render_singer(double hz, int ph)
{
    reset_all(); sound("vbre=50 vvib=30");
    push(0, 0, ph, 6.0, hz2midi(hz), 0.8, 0); run(6.0);
    sung_n = bn - 32000; free(sung); sung = malloc(sizeof(float) * sung_n); memcpy(sung, buf + 32000, sizeof(float) * sung_n); sung_i = 0;
}
static float noise_mic(double t) { (void)t; static uint32_t r = 1; r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (float)((int32_t)r / 2147483648.0 * 0.05); }
static void t_fxdebug(void)
{
    printf("== effect debug\n");
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 3) render_singer(98, PH_AA);
        reset_all();
        sing_hz = pass == 2 ? 100 : 150;
        mic_fn = pass == 0 ? noise_mic : pass == 3 ? sung_mic : singer; FX = (mv_fx_t){ 1.0f, 1.0f, 0, 0, 0, 0, 0 };
        run(1.5); int s0 = bn; run(1.0);
        double F[3]; formants(buf + s0, 32000, SRD, 3, 12, 0.04, F);
        printf("  %s: through EE F1 %.0f F2 %.0f F3 %.0f  rms in %.4f out %.4f\n", pass == 0 ? "noise" : pass == 1 ? "singer 150" : pass == 2 ? "singer 100" : "VOICE's AH", F[0], F[1], F[2], rms(micbuf + s0, 32000), rms(buf + s0, 32000));
        for (int j = 0; j < 5; j++) { double jw[5] = { 0, 0.25, 0.5, 0.75, 1 }; FX.jaw = (float)jw[j]; run(1.0); s0 = bn; run(0.6); formants(buf + s0, 19200, SRD, 3, 12, 0.04, F); printf("     jaw %.2f: F1 %.0f F2 %.0f\n", jw[j], F[0], F[1]); }
    }
}
static void t_effect(void)
{
    printf("== effect\n");
    reset_all();
    mic_fn = singer; FX = (mv_fx_t){ 1.0f, 1.0f, 0, 0, 0, 0, 0 };
    run(1.5);
    int s0 = bn; run(1.0);
    double F[3]; formants(buf + s0, 32000, SRD, 3, 12, 0.04, F);
    double Fm[3]; formants(micbuf + s0, 32000, SRD, 3, 12, 0.04, Fm);
    printf("  the singer's AH: F1 %.0f F2 %.0f; through EE: F1 %.0f F2 %.0f (IY 270 2290); pitch %.2f (%.2f) rms in %.4f out %.4f\n", Fm[0], Fm[1], F[0], F[1], mv_track(), hz2midi(sing_hz), rms(micbuf + s0, 32000), rms(buf + s0, 32000));
    FX.jaw = 0.0f; run(1.0); s0 = bn; run(1.0);
    formants(buf + s0, 32000, SRD, 3, 12, 0.04, F);
    printf("  through OO: F1 %.0f F2 %.0f (UW 300 870)\n", F[0], F[1]);
    FX.jaw = 0.5f; FX.sub = 1.0f; run(1.5);
    FX.sub = 0; FX.overtone = 8; FX.sharp = 0.5f; run(1.5);
    FX.overtone = 0; FX.amount = 0; run(0.5);
    // the singer's own, for comparison
    char fn[] = "out/effect-singer.wav"; wav_write(fn, micbuf, bn, 1.0);
    save("effect", 0, bn);
}
static double bench(int blocks)
{
    float out[64], send[64];
    double sum = 0;
    for (int b = 0; b < blocks; b++) {
        if (b % 5 == 0) mv_set(&C);
        memset(out, 0, sizeof out); memset(send, 0, sizeof send);
        struct timespec a, c; clock_gettime(CLOCK_MONOTONIC, &a);
        mv_process(out, send, 64);
        clock_gettime(CLOCK_MONOTONIC, &c);
        sum += (c.tv_sec - a.tv_sec) * 1e6 + (c.tv_nsec - a.tv_nsec) * 1e-3;
        clk += 64;
    }
    return sum / blocks;
}
static void t_cost(void)
{
    printf("== cost (us per 64-frame block)\n");
    reset_all(); sound("vbre=0");
    C.breath = 0;
    C.v[0] = (mv_free_t){ true, 48, 0.6f }; run(0.3);
    printf("  one free voice, no breath: %.2f\n", bench(3000));
    for (int v = 0; v < MV_N; v++) C.v[v] = (mv_free_t){ true, (float)(48 + v * 3), 0.6f };
    run(0.3);
    printf("  six free voices, no breath: %.2f\n", bench(3000));
    sound("vbre=50"); C.breath = 0.15f; run(0.3);
    printf("  six free voices, breath: %.2f\n", bench(3000));
    sound("vbre=50 vrou=40 vund=50 vchr=60"); C.overtone = 8; run(0.3);
    printf("  six free voices, undertone, whistle: %.2f\n", bench(3000));
    for (int i = 0; i < 300; i++) { C.jaw = (float)(0.5 + 0.5 * sin(i * 0.05)); run(0.01); }
    double s = 0; for (int i = 0; i < 100; i++) { C.jaw = (float)(0.5 + 0.5 * sin(i * 0.05)); s += bench(5); }
    printf("  ... the jaw moving: %.2f\n", s / 100);
    for (int v = 0; v < MV_N; v++) C.v[v].on = false;
    run(0.5);
    for (int v = 0; v < MV_N; v++) for (int r = 0; r < 4; r++) say(v, 0, "S P IY CH K AE T S", 1.2, 48 + v * 3, 0.6, 0);
    printf("  six voices saying words: %.2f\n", bench(2000));
    C.c64 = 1; for (int v = 0; v < MV_N; v++) for (int r = 0; r < 4; r++) say(v, 0, "D EY Z IY", 1.2, 48 + v * 3, 0.6, 0);
    printf("  ... 8-bit: %.2f\n", bench(2000));
}

static void t_fric(void)
{
    printf("== fricatives: band levels (dB, 0 = the whole) and rms against AA\n");
    static const int fr[] = { PH_S, PH_SH, PH_F, PH_TH, PH_HH, PH_Z, PH_V };
    reset_all(); sound("vbre=50");
    push(0, 0, PH_AA, 0.6, 48, 0.8, 0); run(0.7); double aa = rms(buf + bn - 9600, 6400);
    const double edges[] = { 0, 1000, 2000, 3000, 4000, 5000, 6500, 8000, 11000, 16000 };
    for (size_t k = 0; k < sizeof fr / sizeof fr[0]; k++) {
        reset_all(); sound("vbre=50");
        push(0, 0, fr[k], 0.5, 48, 0.8, 0); run(0.55);
        const float *x = buf + 4800; int n = 8192;
        double band[9] = { 0 }, all = 0;
        for (double f = 50; f < 16000; f += 25) { double p = pw_at(x, n, SRD, f); all += p; for (int b = 0; b < 9; b++) if (f >= edges[b] && f < edges[b + 1]) band[b] += p; }
        printf("  %-3s rms %+5.1f dB vs AA |", mv_ph_name(fr[k]), 20 * log10(rms(x, n) / aa));
        for (int b = 0; b < 9; b++) printf(" %4.0f", db(band[b]) - db(all));
        printf("\n");
    }
    printf("        bands:             0-1k 1-2k 2-3k 3-4k 4-5k 5-6.5 6.5-8 8-11 11-16k\n");
}
float mv_test_tract_db(int v, float f);
static void t_env(void)
{
    reset_all(); sound("vbre=50 vvib=0 vdri=0"); push(0, 0, PH_AA, 1.0, hz2midi(100), 0.8, 0); run(0.8);
    printf("  the tract alone (AA):"); for (int f = 500; f <= 9000; f += 500) printf(" %d:%.0f", f, mv_test_tract_db(0, (float)f)); printf("\n");
    printf("== spectral envelope: the strongest harmonic per band, dB under the strongest (AA, IY at 100 Hz)\n");
    printf("        0-1k  1-2k  2-3k  3-4k  4-5k  5-6k  6-7k  7-8k  8-10k\n");
    const int ph[6] = { PH_AA, PH_IY, PH_OW, PH_M, PH_N, PH_NG };
    for (int k = 0; k < 6; k++) {
        reset_all(); sound("vbre=50 vvib=0 vdri=0");
        push(0, 0, ph[k], 1.0, hz2midi(100), 0.8, 0); run(1.0);
        const float *x = buf + 9600; int n = 16384;
        const double e[] = { 50, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 10000 };
        double mx[9] = { 0 }, all = 0;
        for (int h = 1; h * 100.0 < 10000; h++) { double p = pw_at(x, n, SRD, h * 100.0); for (int b = 0; b < 9; b++) if (h * 100.0 >= e[b] && h * 100.0 < e[b + 1] && p > mx[b]) mx[b] = p; if (p > all) all = p; }
        printf("  %-3s ", mv_ph_name(ph[k])); for (int b = 0; b < 9; b++) printf(" %5.0f", db(mx[b]) - db(all)); printf("   rms %.4f\n", rms(x, n));
    }
    printf("  aim  0    -5   -20   -28   -35   -40   -45   -50   -55  (a man's open vowel, roughly)\n");
}
static void t_burst(void)
{
    printf("== bursts: the first 10 ms after the release, then the aspiration (band dB, 0 = the loudest band; rms vs the vowel)\n");
    static const int st[] = { PH_P, PH_T, PH_K, PH_B, PH_D, PH_G };
    const double edges[] = { 50, 1000, 2000, 3000, 4000, 5000, 6500, 8000, 11000 };
    for (int k = 0; k < 6; k++) {
        reset_all(); sound("vbre=50");
        uint32_t at = clk + 640;
        push(0, at, st[k], mv_ph_len(st[k]), 48, 0.8, 0); push(0, 0, PH_AA, 0.4, 48, 0.8, 0);
        run(0.7);
        // find the release: the first sample after the closure louder than the closure's silence
        int r0 = 640 + (int)(0.02 * SRD); while (r0 < bn && fabsf(buf[r0]) < 0.002f) r0++;
        for (int w = 0; w < 2; w++) {
            const float *x = buf + r0 + (w ? 320 : 0); int n = w ? 1280 : 320;
            double band[8] = { 0 }, mx = 0;
            for (double f = 50; f < 11000; f += 25) { double pp = pw_at(x, n, SRD, f); for (int b = 0; b < 8; b++) if (f >= edges[b] && f < edges[b + 1]) band[b] += pp; }
            for (int b = 0; b < 8; b++) if (band[b] > mx) mx = band[b];
            printf("  %-2s %s rms %+5.1f |", mv_ph_name(st[k]), w ? "asp  " : "burst", 20 * log10(rms(x, n) / rms(buf + bn - 6400, 3200)));
            for (int b = 0; b < 8; b++) printf(" %4.0f", db(band[b]) - db(mx));
            printf("\n");
        }
    }
    printf("                   bands:  0-1k 1-2k 2-3k 3-4k 4-5k 5-6.5 6.5-8 8-11k\n");
}
static void t_mwords(void)
{
    reset_all(); sound("vtrc=5 vbre=35 vrou=0 vvib=15 vprs=45 vdri=8");
    const char *w[] = { "HH OW M", "K AH M", "M AA M AA", "HH AH M", "S AH M", "T AY M", "OW M", "AA M", "N OW", "S IH NG" };
    for (int i = 0; i < 10; i++) { say(0, 0, w[i], 0.75, 50, 0.8, 0); push(0, 0, PH_SIL, 0.35, 0, 0, 0); }
    run(12.0); save("mwords", 0, bn);
}
static double clickr(int i0)
{
    double sp = 0, cx = 0;
    for (int i = i0; i < i0 + 192 && i < bn; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > sp) sp = d; }
    for (int i = i0 - 3200; i < i0 - 128; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > cx) cx = d; }
    for (int i = i0 + 1280; i < i0 + 4480 && i < bn; i++) { double d = fabs(buf[i] - buf[i - 1]); if (d > cx) cx = d; }
    return sp / (cx + 0.0005);
}
static void t_wstep(void)
{
    reset_all(); sound("vtrc=0 vbre=35 vrou=5 vvib=0 vshp=25 vprs=80 vdri=10 vgld=80");
    C.v[0] = (mv_free_t){ true, (float)hz2midi(120), 0.8f }; C.jaw = 0.4f; tick_fn = ot_tick;
    const double seq[] = { 6, 12, 6, 9, 16, 8, 6 };
    ot_now = 6; run(1.0);
    for (int i = 1; i < 7; i++) {
        int b0 = bn; ot_now = seq[i]; run(0.6);
        // the biggest first-difference over the signal's own within 40 ms of the step
        double w = 0; for (int j = b0; j < b0 + 1280; j += 32) { double r = clickr(j); if (r > w) w = r; }
        printf("  whistle %g -> %g: worst %.2fx\n", seq[i - 1], seq[i], w);
    }
    save("wstep", 0, bn);
}
int main(int argc, char **argv)
{
    mv_clock = lab_clock;
    defaults(); mv_init(); ctl_default(); mv_set(&C);
    const char *w = argc > 1 ? argv[1] : "all";
    bool all = !strcmp(w, "all");
    if (all || !strcmp(w, "vowels")) { t_vowels(43); t_vowels(52); }
    if (all || !strcmp(w, "words")) t_words();
    if (all || !strcmp(w, "daisy")) t_daisy("vtrc=5 vbre=35 vrou=0 vvib=15 vchr=10 vprs=45 vdri=8 vgld=40", "daisy", -12);
    if (all || !strcmp(w, "whistle")) t_whistle();
    if (!strcmp(w, "wsweep")) t_wsweep();
    if (!strcmp(w, "fric")) t_fric();
    if (!strcmp(w, "env")) t_env();
    if (!strcmp(w, "burst")) t_burst();
    if (!strcmp(w, "wstep")) t_wstep();
    if (!strcmp(w, "mwords")) t_mwords();
    if (!strcmp(w, "fxdebug")) t_fxdebug();
    if (all || !strcmp(w, "sub")) t_sub();
    if (all || !strcmp(w, "choir")) t_choir();
    if (all || !strcmp(w, "c64")) t_c64();
    if (all || !strcmp(w, "effect")) t_effect();
    if (all || !strcmp(w, "cost")) t_cost();
    return 0;
}
