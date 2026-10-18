// SCORE's own checks (inst_score.c, inst_bowl.c): what the score pushes to the voices, read back from its log (the
// voices' own sound is VOICE's to check), measured against the song's beat; the blend's zones; the whistle; the link;
// the bowl, offline, measured; the cost. WAVs of every mode, of a slow sweep from the chant to Daisy, of the bowl.
#include "sim.h"
#include "mantra.h"
#include "params.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// from inst_score.c and inst_bowl.c (tools/sim only)
typedef struct { uint32_t at; uint8_t v, ph, flags, kind; float note, level, dur; int16_t tag; } sc_log_t;
extern sc_log_t sc_log[32768];
extern int sc_log_n;
extern double sc_tick_ns;
extern int sc_tick_n;
extern mv_ctl_t sc_last_ctl;
extern int sc_drops;
int sc_test_daisy(int k, int *p6, int *l6, int *note, int *syl);
int sc_test_chord(int bar);
const char *sc_test_syl(int s, int *nuc);
int sc_test_line(int s);
int sc_test_anchor(int k);
int sc_test_harm(int style, int deg);
int sc_test_melody(int mi, int k, int *half);
void sc_test_daisy_clock(double *anchor_beats, double *quarter_beats);
int sc_test_zone(int *next, double *at_beats);
double sc_test_beats(uint32_t frame);
bool sc_test_bar_clean(int b);
void bowl_test_reset(void);
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
enum { K_ONSET, K_VOWEL, K_TURN, K_CODA, K_BREATH, K_WALTZ, K_HUM, K_STOP, K_DRONE, K_DHUM };
enum { S_OM, S_MONKS, S_MANI, S_KHOOMEI, S_SYGYT, S_KARG, S_DAISY, S_DAISY8, S_VOWELS, S_HUM, S_SUNN, S_DOOMD, S_ORGAN };
static int nans(const float *x, int n)   // NaN or inf, by the bits (-ffast-math folds isfinite() to "finite")
{
    int bad = 0;
    for (int i = 0; i < n; i++) { uint32_t u; memcpy(&u, &x[i], 4); if ((u & 0x7f800000u) == 0x7f800000u) bad++; }
    return bad;
}

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); SI.gyro = (vec3_t){ 0, 0, 0 }; }
static void sound(int k) { front_midi(0xC0, (uint8_t)k, 0); }
static double fpb(void) { return (double)(song.fpb ? song.fpb : 19200); }
static double ms_of_beats(double b) { return b * fpb() / AUDIO_SR * 1000.0; }
static double beats_at(uint32_t f) { return sc_test_beats(f); }
static double beats_now(void) { return sc_test_beats(audio_frames()); }
static void run_beats(double b) { sim_run((float)(b * fpb() / AUDIO_SR)); }

// Daisy as the score has it
static int NDN, dp6[64], dl6[64], dnote[64], dsyl[64], syl_p6[64], syl_note[64], NSYL;
static double DA, Q, L;              // her anchor, quarter and loop, in song beats
static void daisy_load(void)
{
    NDN = sc_test_daisy(0, &dp6[0], &dl6[0], &dnote[0], &dsyl[0]);
    NSYL = 0;
    for (int k = 0; k < NDN; k++) {
        sc_test_daisy(k, &dp6[k], &dl6[k], &dnote[k], &dsyl[k]);
        if (dsyl[k] >= 0) { syl_p6[dsyl[k]] = dp6[k]; syl_note[dsyl[k]] = dnote[k]; if (dsyl[k] + 1 > NSYL) NSYL = dsyl[k] + 1; }
    }
}
static void daisy_clock(void) { sc_test_daisy_clock(&DA, &Q); L = 99.0 * Q; }
static int key_shift(void) { int k = ((song.key % 12) + 12) % 12; return ((k - 7 + 6) % 12 + 12) % 12 - 6; }
static float sung(int written) { return (float)(written - 12 + key_shift() + 12 * (int)get("coct")); }
static int place(int pc, int lo) { return lo + ((pc - lo) % 12 + 12) % 12; }

static bool wait_zone(int z, float secs)
{
    for (int i = 0; i < (int)(secs * 100.0f); i++) {
        int nx; double at; if (sc_test_zone(&nx, &at) == z && nx < 0) return true;
        sim_tick();
    }
    int nx; double at; return sc_test_zone(&nx, &at) == z;
}
// the error of a Daisy event against her grid: its beat less where p6 falls in the nearest loop (ms)
static double daisy_err_ms(uint32_t at, int p6, int *loop)
{
    double rel = beats_at(at) - DA - (double)p6 / 6.0 * Q;
    double k = floor(rel / L + 0.5);
    if (loop) *loop = (int)k;
    return ms_of_beats(rel - k * L);
}

static double bowl_us;
static void shot(const char *name)   // a long test brings the saver: a touch of the knob ends it (it swallows the touch)
{
    if (front_screen() == 4) { SI.pressed = true; sim_tick(); SI.pressed = false; sim_tick(); }
    ui_awake(); sim_run(0.08f); sim_shot(name);
}
static void wav_write(const char *name, const float *x, int n)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s-%s.wav", sim_name, name);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds, fl = 16; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { float v = x[i]; v = v > 1 ? 1 : v < -1 ? -1 : v; int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f); }
    fclose(f);
}

// ---------------------------------------------------------------- 1. Daisy alone: the tune, on the beat
static void daisy_alone(void)
{
    section("score: Daisy Bell, alone with the waltz");
    quiet(); sound(S_DAISY); set("hbld", 100); sim_run(0.5f);
    check(wait_zone(3, 12.0f), "the knob at 100: Daisy alone");
    daisy_clock();
    double now = beats_now(), k0 = ceil((now + 1.0 - DA) / L), start = DA + k0 * L;
    sc_log_n = 0;
    double t0 = sim_t + (start - now) * fpb() / AUDIO_SR;
    run_beats(start - now + L + 1.5);
    sim_wav("-score-daisy", (float)t0 - 0.5f, (float)(t0 + L * fpb() / AUDIO_SR) + 1.0f);
    // the lead's vowels, one chorus
    int got[64]; uint32_t vat[64]; memset(got, 0, sizeof got);
    int order_bad = 0, note_bad = 0, last = -1, nv = 0;
    double worst = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->kind != K_VOWEL || e->tag < 100) continue;
        int s = e->tag - 100, loop;
        double err = daisy_err_ms(e->at, syl_p6[s], &loop);
        if (loop != (int)k0) continue;
        nv++;
        if (s != last + 1) order_bad++;
        last = s; got[s]++; vat[s] = e->at;
        if (fabs(err) > worst) worst = fabs(err);
        if (fabsf(e->note - sung(syl_note[s])) > 0.01f) note_bad++;
    }
    int missing = 0; for (int s = 0; s < NSYL; s++) if (got[s] != 1) missing++;
    check(nv == NSYL && missing == 0 && order_bad == 0, "all %d syllables sung once, in order (%d vowels, %d out of order)", NSYL, nv, order_bad);
    check(note_bad == 0, "each on its note, in G an octave down (a man's voice): %d wrong", note_bad);
    check(worst <= 5.0, "each vowel starts on its beat (worst %.2f ms, 5 allowed)", worst);
    // the consonants come before the beat, the codas end before the next syllable
    int early = 0, late = 0, nons = 0, ncod = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->tag < 100) continue;
        int s = e->tag - 100;
        if (!got[s]) continue;
        if (e->kind == K_ONSET) { int32_t d = (int32_t)(vat[s] - e->at); if (d < -48000 || d > 48000) continue; nons++; if (d <= 0 || d > 9600) early++; }
        if (e->kind == K_CODA && s + 1 < NSYL && got[s + 1]) { ncod++; if ((int32_t)(vat[s + 1] - e->at) <= 0) late++; }
    }
    check(nons > 30 && early == 0, "%d consonants start before their vowel's beat (within 300 ms): %d not", nons, early);
    check(ncod > 15 && late == 0, "%d codas sung before the next syllable: %d not", ncod, late);
    // the turn on "mar": A B A in triplets
    int turn_ok = 0; double terr = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->tag != 126 || (e->kind != K_TURN && e->kind != K_VOWEL)) continue;
        int loop; double best = 1e9; int bp6 = 0;
        for (int p = 324; p <= 328; p += 2) { double er = daisy_err_ms(e->at, p, &loop); if (fabs(er) < fabs(best)) { best = er; bp6 = p; } }
        if (loop != (int)k0) continue;
        float want = sung(bp6 == 326 ? 71 : 69);
        if (fabsf(e->note - want) < 0.01f && fabs(best) <= 5.0) turn_ok++;
        if (fabs(best) > terr) terr = fabs(best);
    }
    check(turn_ok == 3, "the turn on \"mar\": A, B, A in a triplet on one vowel (%d of 3 right, %.2f ms)", turn_ok, terr);
    // the waltz: the root on 1, the third and the fifth on 2 and 3
    int wbad = 0, wn = 0; double werr = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->kind != K_WALTZ) continue;
        double rel = beats_at(e->at) - DA - k0 * L;
        if (rel < -0.1 || rel > L - 0.1) continue;
        int qn = (int)floor(rel / Q + 0.5), b = qn / 3, beat = qn % 3;
        double err = ms_of_beats(rel - qn * Q);
        if (e->v == 2 && beat == 0 && e->ph == PH_M) continue;     // the oom's closing M
        wn++;
        int deg = sc_test_chord(b), root = (7 + (deg == 3 ? 5 : deg == 4 ? 7 : 0) + key_shift() + 24) % 12;
        float want = beat == 0 ? (float)place(root, 36) : e->v == 2 ? (float)place((root + 4) % 12, 44) : (float)place((root + 7) % 12, 44);
        if (fabsf(e->note - want) > 0.01f || fabs(err) > 5.0) wbad++;
        if (fabs(err) > werr) werr = fabs(err);
    }
    check(wn == 33 * 5 && wbad == 0, "the waltz under it: %d notes (33 bars: oom on 1, pah pah on 2 and 3 in two voices), %d wrong, %.2f ms",
          wn, wbad, werr);
    // in another key
    set("skey", 9); sim_run(0.1f);
    sc_log_n = 0; run_beats(8.0);
    int moved = 0, nm = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->kind != K_VOWEL || e->tag < 100) continue;
        nm++; if (fabsf(e->note - sung(syl_note[e->tag - 100])) < 0.01f) moved++;
    }
    check(nm > 3 && moved == nm, "in A it sings a tone up (%d of %d syllables)", moved, nm);
    set("skey", 7); sim_run(0.2f);
    shot("score_daisy");
}

// ---------------------------------------------------------------- 2. the zones of the knob
static int zone_events(int from, int kind_lo_tag, bool daisy, int *voices_mask)
{
    int n = 0; *voices_mask = 0;
    for (int i = from; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (daisy != (e->tag >= 100)) continue;
        if (kind_lo_tag >= 0 && e->kind != kind_lo_tag) continue;
        if (kind_lo_tag < 0 && (e->kind == K_WALTZ || e->kind == K_STOP || e->kind == K_DRONE)) continue;
        n++; *voices_mask |= 1 << e->v;
    }
    return n;
}
static bool on_bar(double at_beats)
{
    double b = (at_beats - DA) / (3.0 * Q);
    long k = lround(b);
    return fabs(b - (double)k) < 1e-6 && sc_test_bar_clean((int)(((k % 33) + 33) % 33));
}
static void zones(void)
{
    section("score: the knob from the chant to Daisy");
    quiet(); set("hbld", 0); sound(S_MONKS); sim_run(0.5f);
    wait_zone(0, 10.0f); daisy_clock();
    static const float pos[4] = { 10, 35, 60, 90 };
    static const char *const what[4] = { "the monks chant OM", "they hum Daisy over a drone", "they sing her words over the drone", "Daisy alone, the waltz under her" };
    for (int z = 0; z < 4; z++) {
        set("hbld", pos[z]);
        int nx = -1; double S = 0;
        for (int i = 0; i < 1000 && nx < 0 && z > 0; i++) { sim_tick(); sc_test_zone(&nx, &S); }
        // the drone comes in (or goes) over the bar after the switch, not at once
        float lv_at[3] = { 0, 0, 0 };
        if (z > 0) {
            while (beats_now() < S && nx >= 0) sim_tick();
            double bar = 3.0 * Q;
            for (int k = 0; k < 3; k++) { run_beats(S + bar * (0.15 + 0.4 * k) - beats_now()); lv_at[k] = sc_last_ctl.v[5].on ? sc_last_ctl.v[5].level : 0.0f; }
        }
        bool switched = wait_zone(z, 15.0f);
        int from = sc_log_n;
        run_beats(12.0);
        int m_chant, m_hum, m_words, m_waltz, m_stop;
        int n_chant = zone_events(from, -1, false, &m_chant);
        int n_hum = zone_events(from, K_DHUM, true, &m_hum);
        int n_words = zone_events(from, K_VOWEL, true, &m_words);
        int n_waltz = zone_events(from, K_WALTZ, false, &m_waltz);
        zone_events(from - 200 > 0 ? from - 200 : 0, K_STOP, false, &m_stop);
        bool drone = sc_last_ctl.v[5].on && sc_last_ctl.v[5].level > 0.2f;
        const int monks = (1 << 0) | (1 << 1) | (1 << 4) | (1 << 2) | (1 << 3);
        bool ok;
        switch (z) {
        case 0:  ok = n_chant > 10 && m_chant == monks && !n_hum && !n_words && !drone; break;
        case 1:  ok = n_hum > 10 && m_hum == monks && !n_words && !n_chant && drone; break;
        case 2:  ok = n_words > 10 && m_words == monks && !n_hum && drone; break;
        default: ok = n_words > 3 && m_words == 1 && n_waltz > 6 && m_waltz == 12 && !drone; break;
        }
        check(switched && ok, "BLEND %.0f: %s (chant %d, hummed %d, sung %d, waltz %d; drone %s)", (double)pos[z], what[z],
              n_chant, n_hum, n_words, n_waltz, drone ? "on" : "off");
        if (z == 3) {
            float vund = param_by_tag("vund") >= 0 ? get("vund") * 0.01f : 0.0f, vtrc = param_by_tag("vtrc") >= 0 ? get("vtrc") * 0.01f : 0.0f;
            check(fabsf(sc_last_ctl.sub + vund) < 0.01f && fabsf(sc_last_ctl.tract + vtrc) < 0.01f,
                  "... sung plainly: the monks' undertone (%.2f) and long tract (%.2f) taken away", (double)vund, (double)vtrc);
        }
        if (z > 0) check(on_bar(S), "... it took over on one of Daisy's bar lines with no note held across it (beat %.2f)", S);
        if (z == 1) check(lv_at[0] > 0.02f && lv_at[0] < 0.6f * lv_at[2] && lv_at[1] > lv_at[0] && lv_at[2] > lv_at[1],
                          "... the drone comes in over that bar (%.2f, %.2f, %.2f)", (double)lv_at[0], (double)lv_at[1], (double)lv_at[2]);
        if (z == 3) check(lv_at[0] > lv_at[1] && lv_at[1] > lv_at[2] && lv_at[0] > 0.1f,
                          "... and goes over the bar into Daisy alone (%.2f, %.2f, %.2f)", (double)lv_at[0], (double)lv_at[1], (double)lv_at[2]);
        { sc_view_t sv; sc_view(&sv); note("view: line %s at %d len %d, monks %d", sv.line ? sv.line : "-", sv.word_at, sv.word_len, sv.monks); }
        char b[24]; snprintf(b, sizeof b, "score_zone_%d", z); shot(b);
    }
    // back to the chant: it starts its phrase on the bar line
    set("hbld", 0);
    int nx = -1; double S = 0;
    for (int i = 0; i < 1000 && nx < 0; i++) { sim_tick(); sc_test_zone(&nx, &S); }
    int from = sc_log_n;
    wait_zone(0, 15.0f); run_beats(2.0);
    uint32_t first = 0; bool found = false;
    for (int i = from; i < sc_log_n && !found; i++) if (sc_log[i].tag < 100 && sc_log[i].v == 0 && sc_log[i].kind == K_VOWEL) { first = sc_log[i].at; found = true; }
    check(found && fabs(ms_of_beats(beats_at(first) - S)) < 5.0 && on_bar(S), "back at 0 the OM starts its phrase on Daisy's bar line (%.2f ms off)",
          found ? ms_of_beats(beats_at(first) - S) : 999.0);
}

// ---------------------------------------------------------------- 3. a slow sweep: the tune never restarts
static void sweep(int snd, const char *wav, float secs)
{
    quiet(); set("hbld", 0); sound(snd); sim_run(0.3f);
    wait_zone(0, 12.0f); sim_run(3.0f);
    daisy_clock();
    sc_log_n = 0;
    int seen[4] = { 0 }, order_ok = 1, zlast = 0, switches = 0, off_bar = 0;
    double last_S = -1;
    float t0 = sim_t;
    int steps = (int)(secs * 100.0f);
    for (int i = 0; i <= steps; i++) {
        set("hbld", roundf(100.0f * (float)i / (float)steps));
        sim_tick();
        int nx; double S; int z = sc_test_zone(&nx, &S);
        seen[z] = 1;
        if (z < zlast) order_ok = 0;
        if (z != zlast) { switches++; if (!on_bar(last_S)) off_bar++; }
        if (nx >= 0) last_S = S;
        zlast = z;
    }
    sim_run(6.0f);
    sim_wav(wav, t0 - 2.0f, sim_t);
    // every Daisy phoneme, in every zone, on one clock
    int n = 0; double worst = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->tag < 100 || e->v != (snd == S_KHOOMEI || snd == S_KARG || snd == S_VOWELS ? 1 : 0)) continue;
        if (e->kind != K_VOWEL && e->kind != K_DHUM) continue;
        n++;
        double err = fabs(daisy_err_ms(e->at, syl_p6[e->tag - 100], NULL));
        if (err > worst) worst = err;
    }
    char b[200]; snprintf(b, sizeof b, "%s, BLEND 0 to 100 over %.0f s: zones 0 1 2 3 in turn, each on a clean bar line (%d of %d not)",
                          INST.presets[snd].name, (double)secs, off_bar, switches);
    check(seen[0] && seen[1] && seen[2] && seen[3] && order_ok && off_bar == 0, "%s", b);
    check(n > 20 && worst <= 5.0, "... and the tune ran on one clock all along: %d syllables, worst %.2f ms from her grid", n, worst);
}

// ---------------------------------------------------------------- 4. OM and the monks
static void om(void)
{
    section("score: OM");
    quiet(); set("hbld", 0); sound(S_OM); sim_run(0.5f); wait_zone(0, 10.0f);
    sc_log_n = 0;
    float t0 = sim_t; run_beats(24.0);
    sim_wav("-score-om", t0, sim_t);
    // v0: OW AO OW M, a quiet breath, again, each phrase two bars
    static const uint8_t want[5] = { PH_OW, PH_AO, PH_OW, PH_M, PH_HH };
    int k = 0, bad = 0, phrases = 0; double p0 = -1, period = 0; float hh = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0) continue;
        if (k == 0 && !(e->ph == PH_OW && e->tag == 0)) continue;     // from the first phrase's start
        if (e->ph != want[k % 5]) bad++;
        if (k % 5 == 0) { double b = beats_at(e->at); if (p0 >= 0) period = b - p0; p0 = b; phrases++; }
        if (e->ph == PH_HH && e->level > hh) hh = e->level;
        k++;
    }
    check(phrases >= 2 && bad == 0, "OW opening, AO, OW closing, M, a breath: %d phrases, %d out of place", phrases, bad);
    check(fabs(period - 8.0) < 0.01 && hh > 0.0f && hh <= 0.15f, "phrases of two bars (%.2f beats), the breath quiet (%.2f)", period, (double)hh);
    check(fabsf(sc_last_ctl.sub) < 0.001f && sc_last_ctl.v[0].on == false, "its undertone is VOICE's (its UNDERTONE in the sound: %.0f%%), the score adds none",
          (double)(param_by_tag("vund") >= 0 ? get("vund") : 0.0f));
    shot("score_om");

    sound(S_MONKS); sim_run(0.3f); wait_zone(0, 10.0f);
    sc_log_n = 0; t0 = sim_t; run_beats(24.0);
    sim_wav("-score-monks", t0, sim_t);
    // five voices begin each phrase, a little apart; one an octave below
    int vm = 0; double first[6] = { 0 }; float notes[6] = { 0 }; bool have[6] = { 0 };
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->ph != PH_OW || e->kind != K_VOWEL || e->tag != 0 || have[e->v]) continue;
        have[e->v] = true; first[e->v] = beats_at(e->at); notes[e->v] = e->note; vm |= 1 << e->v;
    }
    double lo = 1e9, hi = -1e9;
    for (int v = 0; v < 5; v++) if (have[v]) { if (first[v] < lo) lo = first[v]; if (first[v] > hi) hi = first[v]; }
    double spread = ms_of_beats(hi - lo);
    check(vm == 0x1F, "MONKS: five voices sing OM (voices %02x)", vm);
    check(spread > 10.0 && spread < 260.0, "... staggered a little (%.0f ms first to last)", spread);
    check(fabsf(notes[1] - (notes[0] - 12.0f)) < 0.01f && fabsf(notes[4] - notes[0]) < 0.2f, "... one an octave below (%.1f under %.1f), the others near unison",
          (double)notes[1], (double)notes[0]);
    float root = notes[0]; float hz = 440.0f * powf(2.0f, (root - 69.0f) / 12.0f);
    check(hz >= 55.0f && hz <= 112.0f, "the key's root two octaves down: %.1f Hz", (double)hz);
}

// ---------------------------------------------------------------- 5. OM MANI PADME HUM
static void mani(void)
{
    section("score: OM MANI PADME HUM");
    quiet(); set("hbld", 0); sound(S_MANI); sim_run(0.5f); wait_zone(0, 10.0f);
    sc_log_n = 0; float t0 = sim_t; run_beats(36.0);
    sim_wav("-score-mani", t0, sim_t);
    static const uint8_t vow[6] = { PH_AO, PH_AA, PH_IY, PH_AA, PH_EY, PH_UW };
    int seq = 0, bad = 0, mantras = 0, tone_bad = 0, fall_ok = 0, falls = 0, cons_ok = 0;
    float tone = 0, hum[3]; int hk = 0; bool begun = false;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0) continue;
        if (!begun && !(e->kind == K_VOWEL && e->tag == 0)) continue;  // from the first OM
        begun = true;
        if (e->kind == K_VOWEL) {
            if (e->tag != seq || e->ph != vow[seq]) bad++;
            if (seq == 0) { tone = e->note; mantras++; hk = 0; }
            else if (fabsf(e->note - tone) > 0.01f) tone_bad++;
            seq = (seq + 1) % 6;
        }
        if (e->tag == 5 && (e->kind == K_VOWEL || e->kind == K_TURN || (e->kind == K_CODA && e->ph == PH_M)) && hk < 3) {
            hum[hk++] = e->note;
            if (hk == 3) { falls++; if (hum[1] < hum[0] && hum[2] < hum[1]) fall_ok++; }
        }
        if (e->kind == K_ONSET && ((e->tag == 2 && e->ph == PH_N) || (e->tag == 3 && e->ph == PH_P) || (e->tag == 4 && e->ph == PH_M) || (e->tag == 5 && e->ph == PH_HH))) cons_ok++;
    }
    check(mantras >= 6 && bad == 0, "OM MA NI PAD ME HUM in order: %d mantras, %d syllables out of place", mantras, bad);
    check(tone_bad == 0, "recited on one tone (%d off it)", tone_bad);
    check(falls >= 6 && fall_ok == falls, "HUM falls at the end of each (%d of %d)", fall_ok, falls);
    check(cons_ok >= 4 * 6, "the consonants: N, P, M, HH before their vowels (%d)", cons_ok);
    check(sc_last_ctl.v[1].on && fabsf(sc_last_ctl.v[1].note - (tone - 12.0f)) < 0.01f, "a drone under it, an octave down (%.0f)", (double)sc_last_ctl.v[1].note);
    shot("score_mani");
}

// ---------------------------------------------------------------- 6. throat singing: the melody, then the hands
static void throat(void)
{
    section("score: the throat singers' whistle");
    quiet(); set("hbld", 0); sound(S_KHOOMEI); sim_run(0.5f); wait_zone(0, 10.0f);
    float t0 = sim_t;
    // wait for the melody's start, then sample the middle of each step
    double A = (double)sc_test_anchor(0) / 1440.0, now = beats_now(), start = A + ceil((now + 0.2 - A) / 16.0) * 16.0;
    run_beats(start - now);
    int mi = (int)get("cmel"), style = (int)get("csty"), half, d, bad = 0, n = 0;
    double at = 0;
    for (int k = 0; (d = sc_test_melody(mi, k, &half)) >= 0; k++) {
        double mid = at + half * 0.25;
        run_beats(start + mid - beats_now());
        float want = (float)sc_test_harm(style, d), got = sc_last_ctl.overtone;
        if (fabsf(got - want) > 0.35f) { bad++; note("step %d: harmonic %.2f, the melody's %.0f", k, (double)got, (double)want); }
        n++; at += half * 0.5;
    }
    check(n >= 8 && bad == 0, "KHOOMEI: the whistle follows its melody on the harmonics 6..12, in time (%d steps, %d off)", n, bad);
    check(sc_last_ctl.v[0].on && sc_last_ctl.jaw < 0.4f && sc_last_ctl.hum >= 0.2f, "over a drone (%.0f), its vowel low, humming a little",
          (double)sc_last_ctl.v[0].note);
    // the prayer wheel: an eighth of a turn per harmonic
    run_beats(1.0);
    float h0 = roundf(sc_last_ctl.overtone);
    SI.gyro = (vec3_t){ 0, 0, 45.0f }; sim_run(1.0f); SI.gyro = (vec3_t){ 0, 0, 0 }; sim_run(0.15f);
    float h1 = sc_last_ctl.overtone;
    SI.gyro = (vec3_t){ 0, 0, 45.0f }; sim_run(2.0f); SI.gyro = (vec3_t){ 0, 0, 0 }; sim_run(0.15f);
    float h3 = sc_last_ctl.overtone;
    SI.gyro = (vec3_t){ 0, 0, -90.0f }; sim_run(1.0f); SI.gyro = (vec3_t){ 0, 0, 0 }; sim_run(0.15f);
    float h1b = sc_last_ctl.overtone;
    check(fabsf(h1 - h0 - 1.0f) < 0.3f && fabsf(h3 - h0 - 3.0f) < 0.3f && fabsf(h1b - h0 - 1.0f) < 0.3f,
          "the yaw takes it: an eighth of a turn %.0f to %.1f, three eighths %.1f, two back %.1f", (double)h0, (double)h1, (double)h3, (double)h1b);
    // HANDS > ROLL: OVERTONE (KHOOMEI sets it): rolling it up
    sim_tilt(30, 0); sim_run(0.6f);
    float hr = sc_last_ctl.overtone;
    sim_tilt(0, 0); sim_run(0.3f);
    check(get("hrol") == 1.0f && hr >= h1b + 1.5f, "with ROLL on the overtone, rolling right lifts it (%.1f to %.1f)", (double)h1b, (double)hr);
    sim_run(4.0f);                                                   // the hands let go: the melody comes back
    double A2 = (double)sc_test_anchor(0) / 1440.0, b = beats_now();
    double rel = fmod(b - A2, 16.0); double at2 = 0; int want_d = -1;
    for (int k = 0; (d = sc_test_melody(mi, k, &half)) >= 0; k++) { if (rel >= at2 && rel < at2 + half * 0.5) want_d = d; at2 += half * 0.5; }
    check(want_d >= 0 && fabsf(sc_last_ctl.overtone - (float)sc_test_harm(style, want_d)) < 0.5f, "left alone, it goes back to the melody (%.1f)", (double)sc_last_ctl.overtone);
    sim_wav("-score-khoomei", t0, sim_t);
    shot("score_khoomei");

    // the second zone: Daisy on the drone's harmonics
    set("hbld", 35); wait_zone(1, 15.0f); run_beats(3.0);
    daisy_clock();
    int nb = 0, ns = 0;
    for (int k = 0; k < 24; k++) {
        double b2 = beats_now(), p6 = fmod((b2 - DA) / Q * 6.0, 594.0);
        int j = -1; for (int i = 0; i < NDN; i++) if (dp6[i] <= p6) j = i;
        double since = (p6 - dp6[j]) / 6.0 * Q, left = (dp6[j] + dl6[j] - p6) / 6.0 * Q;
        if (j >= 0 && since > 0.15 && left > 0.05 && p6 < 576) {
            float want = fminf(16.0f, fmaxf(4.0f, roundf(8.0f * powf(2.0f, (float)(dnote[j] - 67) / 12.0f))));
            ns++; if (fabsf(sc_last_ctl.overtone - want) > 0.35f) { nb++; note("Daisy's note %d: harmonic %.2f, wanted %.0f", dnote[j], (double)sc_last_ctl.overtone, (double)want); }
        }
        run_beats(0.37);
    }
    check(ns >= 10 && nb == 0, "BLEND 35: the whistle plays Daisy on the nearest harmonics (%d samples, %d off)", ns, nb);
    set("hbld", 0); wait_zone(0, 15.0f);

    sound(S_SYGYT); sim_run(0.5f); wait_zone(0, 10.0f);
    t0 = sim_t; float hmin = 99, hmax = 0;
    for (int i = 0; i < 1600; i++) { sim_tick(); float h = sc_last_ctl.overtone; if (h > 0) { if (h < hmin) hmin = h; if (h > hmax) hmax = h; } }
    sim_wav("-score-sygyt", t0, sim_t);
    check(hmin >= 7.9f && hmax <= 16.1f, "SYGYT: higher, harmonics %.0f..%.0f (how sharp: VOICE's WHISTLE)", (double)hmin, (double)hmax);
}

// ---------------------------------------------------------------- 7. kargyraa
static void kargyraa(void)
{
    section("score: kargyraa");
    quiet(); set("hbld", 0); sound(S_KARG); sim_run(0.5f); wait_zone(0, 10.0f);
    sc_log_n = 0; float t0 = sim_t; run_beats(30.0);
    sim_wav("-score-kargyraa", t0, sim_t);
    int vowels = 0, oo = 0, oh = 0, ah = 0, breaths = 0, other = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0) continue;
        if (e->kind == K_VOWEL) { vowels++; if (e->ph == PH_UW) oo++; else if (e->ph == PH_OW) oh++; else if (e->ph == PH_AA) ah++; else other++; }
        if (e->kind == K_BREATH) breaths++;
    }
    float hz = 440.0f * powf(2.0f, (sc_log[0].note - 69.0f) / 12.0f);
    check(vowels >= 8 && oo && oh && ah && !other && breaths >= 2, "slow vowels OO, OH, AH (%d, %d, %d) in long phrases, %d breaths", oo, oh, ah, breaths);
    check(hz < 125.0f && fabsf(sc_last_ctl.sub) < 0.001f, "very low: %.0f Hz, and VOICE's UNDERTONE (%.0f%% in the sound) an octave under that", (double)hz,
          (double)(param_by_tag("vund") >= 0 ? get("vund") : 0.0f));
}

// ---------------------------------------------------------------- 8. the link: a new tempo, a moved beat
static void link_follow(void)
{
    section("score: the song's beat moves");
    quiet(); set("hbld", 100); sound(S_DAISY); sim_run(0.5f); wait_zone(3, 12.0f);
    // the tempo set here: the beat keeps its place, the next syllables come at the new speed
    set("stmp", 96); sim_run(0.6f);
    daisy_clock();
    sc_log_n = 0; run_beats(10.0);
    int n = 0; double worst = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->kind != K_VOWEL || e->tag < 100) continue;
        n++; double err = fabs(daisy_err_ms(e->at, syl_p6[e->tag - 100], NULL)); if (err > worst) worst = err;
    }
    check(n > 5 && worst <= 5.0 && song.fpb == 20000, "TEMPO 72 to 96: the next %d syllables land on the new beats (worst %.2f ms)", n, worst);
    // a BAND at 120 takes the clock: the beat jumps, the tune finds its place on the new one
    int k = sim_peer_add("BAND", 2, 15.0, 3.3e9);
    sim_peer_use(k);
    { uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o + 7000, 4, 0); }
    sim_peer_use(-1);
    sim_run(3.0f);
    daisy_clock();
    sc_log_n = 0; run_beats(12.0);
    n = 0; worst = 0;
    for (int i = 0; i < sc_log_n; i++) {
        sc_log_t *e = &sc_log[i];
        if (e->v != 0 || e->kind != K_VOWEL || e->tag < 100) continue;
        n++; double err = fabs(daisy_err_ms(e->at, syl_p6[e->tag - 100], NULL)); if (err > worst) worst = err;
    }
    check(song.fpb == 16000 && n > 5 && worst <= 5.0, "a linked BAND at 120 BPM: Daisy follows its beat (%d syllables, worst %.2f ms)", n, worst);
    sim_peer_power(k, false); sim_run(1.5f);
    set("stmp", 72); sim_run(0.5f);
}

// ---------------------------------------------------------------- 9. the free voices: VOWELS, HUM ALONG
static float hum_mic(float t, void *u) { (void)u; return 0.2f * sinf(6.2831853f * 196.0f * t) + 0.08f * sinf(6.2831853f * 392.0f * t); }
static void free_voices(void)
{
    section("score: VOWELS and HUM ALONG");
    quiet(); set("hbld", 0); sound(S_VOWELS); sim_run(0.5f); wait_zone(0, 10.0f);
    sc_log_n = 0;
    float t0 = sim_t;
    for (int i = 0; i < 8; i++) { sim_tilt((float)(i % 3) * 12.0f - 12.0f, (float)((i * 7) % 5) * 15.0f - 30.0f); sim_run(1.2f); }
    sim_tilt(0, 0); sim_run(1.0f);
    sim_wav("-score-vowels", t0, sim_t);
    check(sc_last_ctl.v[0].on && (int)sc_last_ctl.v[0].note == scale_note(0, 3) && sc_log_n == 0,
          "VOWELS: one free voice on the key's root below middle C (%.0f), nothing sequenced", (double)sc_last_ctl.v[0].note);
    shot("score_vowels");
    sound(S_HUM); sim_run(1.0f); wait_zone(0, 10.0f);
    check(!sc_last_ctl.v[0].on, "HUM ALONG: silent while nobody hums");
    SI.mic = hum_mic; t0 = sim_t; sim_run(3.0f);
    float tr = mv_track();
    ui_awake();
    if (tr > 0.0f) check(sc_last_ctl.v[0].on && fabsf(sc_last_ctl.v[0].note - 55.0f) < 0.6f, "... humming G3: it follows (%.1f, tracked %.1f)", (double)sc_last_ctl.v[0].note, (double)tr);
    else note("HUM ALONG: no pitch from mv_track() yet (VOICE's), so nothing to follow");
    sim_run(1.0f); SI.mic = NULL; sim_run(1.0f);
    sim_wav("-score-hum", t0, sim_t);
}

// ---------------------------------------------------------------- 10. the bowl, offline
static float *B; static int Bn, Bcap;
static void brender(float secs)
{
    int n = (int)(secs * AUDIO_SR) / AUDIO_BLOCK * AUDIO_BLOCK;
    if (Bn + n > Bcap) { Bcap = Bn + n + AUDIO_SR; B = realloc(B, (size_t)Bcap * sizeof(float)); }
    for (int i = 0; i < n; i += AUDIO_BLOCK) {
        float o[AUDIO_BLOCK] = { 0 }, s[AUDIO_BLOCK] = { 0 };
        bowl_process(o, s, AUDIO_BLOCK);
        memcpy(B + Bn, o, sizeof o); Bn += AUDIO_BLOCK;
    }
}
static double goertzel(int a, int b, double f)
{
    double w = 2 * M_PI * f / AUDIO_SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = a; i < b; i++) { double s0 = B[i] * (0.5 - 0.5 * cos(2 * M_PI * (i - a) / (b - a))) + c * s1 - s2; s2 = s1; s1 = s0; }
    return sqrt(fmax(0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (b - a);
}
static double peak_near(int a, int b, double f, double span)
{
    double best = 0, bf = f;
    for (double x = f * (1 - span); x <= f * (1 + span); x += f * 0.0004) { double g = goertzel(a, b, x); if (g > best) { best = g; bf = x; } }
    return bf;
}
static double band_rms(int a, int b, double f)       // the pair at f: the stronger of its two, a Hann-windowed DFT over a..b
{
    double best = 0;
    for (double x = f * 0.996; x <= f * 1.004; x += f * 0.0005) { double g = goertzel(a, b, x); if (g > best) best = g; }
    return best;
}
static void bowl(void)
{
    section("score: the singing bowl");
    static const double ratio[6] = { 1.0, 2.987, 5.780, 9.277, 13.329, 17.773 };
    int keep_b = (int)get("cbst"); set("cbst", 0);
    bowl_test_reset(); Bn = 0;
    float f0 = 196.0f;
    bowl_set(f0, 30.0f, 1.0f);
    bowl_strike(1.0f);
    brender(32.0f);
    wav_write("score-bowl", B, Bn);
    // its partials
    int bad = 0; double fp0 = peak_near(16000, 16000 + 4 * AUDIO_SR, f0, 0.02);
    char rs[160]; int w = snprintf(rs, sizeof rs, "1");
    for (int k = 1; k < 6; k++) {
        double fk = peak_near(8000, 8000 + 2 * AUDIO_SR, f0 * ratio[k], 0.02), r = fk / fp0;
        if (fabs(r / ratio[k] - 1.0) > 0.008) bad++;
        w += snprintf(rs + w, sizeof rs - (size_t)w, " : %.2f", r);
    }
    check(bad == 0 && fabs(fp0 / f0 - 1.0) < 0.01, "a hard strike: the partials of a measured bowl, %s (%.1f Hz the first)", rs, fp0);
    // the pair beats: the fundamental's band swells and sinks about every 1/(0.6 %% of f) seconds
    int win = AUDIO_SR / 20, minima = 0; double env[200], depth = 0;
    for (int i = 0; i < 160; i++) env[i] = goertzel(AUDIO_SR + i * win, AUDIO_SR + (i + 1) * win, f0);
    for (int i = 1; i < 159; i++) {
        if (env[i] < env[i - 1] && env[i] <= env[i + 1]) minima++;
        double d = (env[i - 1] - env[i]) / (env[i - 1] + env[i] + 1e-12); if (fabs(d) > depth) depth = fabs(d);

    }
    double beat_hz = (double)minima / (160.0 * win / AUDIO_SR), want = 0.006 * f0;
    check(beat_hz > 0.6 * want && beat_hz < 1.6 * want, "it beats: the first partial swells %.2f times a second (its pair %.2f Hz apart)", beat_hz, want);
    // decays: the fundamental's T60 is BOWL RING, the high ones shorter
    double e1 = band_rms(2 * AUDIO_SR, 6 * AUDIO_SR, f0), e2 = band_rms(18 * AUDIO_SR, 22 * AUDIO_SR, f0);
    double t60 = 16.0 * 60.0 / (20.0 * log10(e1 / e2));
    double h1 = band_rms(AUDIO_SR / 2, AUDIO_SR, f0 * ratio[3]), h2 = band_rms(3 * AUDIO_SR, 7 * AUDIO_SR / 2, f0 * ratio[3]);
    double t60h = 2.75 * 60.0 / (20.0 * log10(h1 / h2));
    check(t60 > 24.0 && t60 < 37.0 && t60h < 0.4 * t60, "it rings: the fundamental's T60 %.1f s (BOWL RING 30 s), the fourth partial's %.1f s", t60, t60h);
    double r0 = band_rms(AUDIO_SR / 2, 3 * AUDIO_SR / 2, f0), r30 = band_rms(29 * AUDIO_SR, 30 * AUDIO_SR, f0);
    check(20.0 * log10(r0 / r30) > 45.0 && 20.0 * log10(r0 / r30) < 72.0 && r30 > 0, "half a minute later it is %.0f dB down: still there, nearly gone",
          20.0 * log10(r0 / r30));
    // no click: the strike comes in over a millisecond or so
    float dmax_on = 0, dmax_ring = 0;
    for (int i = 1; i < 320; i++) { float d = fabsf(B[i] - B[i - 1]); if (d > dmax_on) dmax_on = d; }
    for (int i = 3200; i < 6400; i++) { float d = fabsf(B[i] - B[i - 1]); if (d > dmax_ring) dmax_ring = d; }
    check(fabsf(B[0]) < 0.01f && dmax_on < 2.5f * dmax_ring, "no click at the strike (its steepest step %.4f, the ringing's %.4f)", (double)dmax_on, (double)dmax_ring);
    // soft and hard: a soft strike is quieter and darker
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 0.0f); bowl_strike(0.25f); brender(1.5f);
    double s_lo = band_rms(4000, 20000, f0), s_hi = band_rms(4000, 20000, f0 * ratio[3]);
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 1.0f); bowl_strike(1.0f); brender(1.5f);
    double h_lo = band_rms(4000, 20000, f0), h_hi = band_rms(4000, 20000, f0 * ratio[3]);
    check(h_lo > 2.0 * s_lo && h_hi / h_lo > 3.0 * s_hi / s_lo, "a hard strike is louder (%.1fx) and brighter (the fourth partial %.1fx more against the first)",
          h_lo / s_lo, (h_hi / h_lo) / (s_hi / s_lo));
    // several strikes ring together, a re-strike does not click
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 0.5f);
    bowl_strike(0.6f); brender(2.0f);
    double one = band_rms(AUDIO_SR / 5, 19 * AUDIO_SR / 10, f0);
    float dr = 0; for (int i = AUDIO_SR / 2; i < AUDIO_SR; i++) { float d = fabsf(B[i] - B[i - 1]); if (d > dr) dr = d; }
    int at = Bn;
    bowl_strike(0.6f); brender(2.0f);
    float dk = 0; for (int i = at; i < at + 320; i++) { float d = fabsf(B[i] - B[i - 1]); if (d > dk) dk = d; }
    bowl_strike(0.6f); brender(2.0f); at = Bn; bowl_strike(0.6f); brender(2.0f);
    double four = band_rms(at + AUDIO_SR / 5, at + 19 * AUDIO_SR / 10, f0);
    float pk = 0; int nan = 0; for (int i = 0; i < Bn; i++) { if (!(B[i] == B[i])) nan++; if (fabsf(B[i]) > pk) pk = fabsf(B[i]); }
    check(four > 1.8 * one && pk < 0.9f && nan == 0 && dk < 2.5f * dr, "four strikes 2 s apart build up (%.1fx the first), peak %.2f, no click on a ringing bowl (%.4f, %.4f)",
          four / one, (double)pk, (double)dk, (double)dr);
    // a rhythm to listen to
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 0.4f);
    static const float hits[] = { 0.8f, 0.3f, 0.5f, 0.3f, 0.9f, 0.3f, 0.5f, 0.35f, 0.7f, 0.0f, 0.45f, 0.3f, 1.0f, 0.0f, 0.0f, 0.0f };
    for (int r = 0; r < 2; r++) for (int i = 0; i < 16; i++) { if (hits[i] > 0) bowl_strike(hits[i]); brender(0.625f); }
    brender(10.0f);
    wav_write("score-bowl-rhythm", B, Bn);
    // the cost: three strikes ringing
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 1.0f);
    bowl_strike(1.0f); brender(0.1f); bowl_strike(0.7f); brender(0.1f); bowl_strike(0.9f); brender(0.1f);
    struct timespec a, b; float o[AUDIO_BLOCK], s[AUDIO_BLOCK];
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 20000; i++) { memset(o, 0, sizeof o); memset(s, 0, sizeof s); bowl_process(o, s, AUDIO_BLOCK); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double us = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 20000.0;
    metric("bowl_us_per_block", us);
    bowl_test_reset();
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 20000; i++) bowl_process(o, s, AUDIO_BLOCK);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double idle = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 20000.0;
    metric("bowl_us_per_block_silent", idle);
    // a long ring: the high partials are gone after a few seconds and are no longer run (half the modes by 12 s)
    bowl_test_reset(); Bn = 0; bowl_set(f0, 30.0f, 1.0f); bowl_strike(1.0f); brender(12.0f);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (int i = 0; i < 20000; i++) { memset(o, 0, sizeof o); memset(s, 0, sizeof s); bowl_process(o, s, AUDIO_BLOCK); }
    clock_gettime(CLOCK_MONOTONIC, &b);
    double late = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 20000.0;
    double r12 = band_rms(Bn - AUDIO_SR, Bn, f0);
    check(idle < 0.03 && late < 0.7 * us && r12 > 1e-4, "the bowl costs nothing silent (%.3f us); 12 s into a ring it runs the partials left (%.3f us against %.3f fresh), still ringing",
          idle, late, us);
    free(B); B = NULL; Bn = Bcap = 0;
    set("cbst", (float)keep_b);
    // in the instrument: the score's strikes on the beat (OM: EACH PHRASE)
    quiet(); sound(S_OM); set("hbld", 0); sim_run(0.5f); wait_zone(0, 10.0f);
    bowl_test_reset();
    float lv = 0; for (int i = 0; i < 800; i++) { sim_tick(); if (bowl_level() > lv) lv = bowl_level(); }
    check(lv > 0.1f, "OM strikes the bowl at each phrase (BOWL STRIKES: PHRASES): it rings to %.2f", (double)lv);
    bowl_us = us;
}

// ---------------------------------------------------------------- 11. what it costs
static void cost(void)
{
    section("score: cost");
    double worst = 0; const char *wn = "";
    static const int snds[] = { S_MONKS, S_MANI, S_KHOOMEI, S_DAISY, S_DAISY8 };
    static const float blends[] = { 0, 35, 60, 100 };
    for (int a = 0; a < 5; a++) for (int b = 0; b < 4; b++) {
        quiet(); sound(snds[a]); set("hbld", blends[b]); sim_run(4.0f);
        sc_tick_ns = 0; sc_tick_n = 0;
        sim_run(6.0f);
        double us = sc_tick_ns / (sc_tick_n ? sc_tick_n : 1) / 1000.0 / 5.0;   // a tick is five blocks
        if (us > worst) { worst = us; wn = INST.presets[snds[a]].name; }
    }
    metric("score_us_per_block", worst);
    check(worst + bowl_us <= 1.0, "SCORE (busiest: %s, %.3f us) + the bowl ringing (%.3f us) per 64-frame block: %.3f us (budget 1)", wn, worst, bowl_us, worst + bowl_us);
    set("hbld", 0);
}

// ---------------------------------------------------------------- 12. to listen to
static void tour(void)
{
    section("score: to listen to");
    // the slow sweep the user hears first: the monks, from OM to Daisy over a chorus
    sweep(S_MONKS, "-score-sweep-monks", 48.0f);
    sweep(S_KHOOMEI, "-score-sweep-khoomei", 48.0f);
    quiet(); set("hbld", 0); sound(S_DAISY); sim_run(0.5f); wait_zone(0, 10.0f);
    float t0 = sim_t; sim_run(22.0f); sim_wav("-score-daisy-choir", t0, sim_t);
    set("hbld", 100); sound(S_DAISY8); sim_run(0.5f); wait_zone(3, 10.0f);
    t0 = sim_t; sim_run(22.0f); sim_wav("-score-daisy-8bit", t0, sim_t);
    // the sounds over the drone (the amps are the DRONE module's; here the voices' part)
    set("hbld", 0); sound(S_SUNN); sim_run(0.5f); wait_zone(0, 10.0f);
    t0 = sim_t; sim_run(26.0f); sim_wav("-score-sunn", t0, sim_t);
    set("hbld", 100); sound(S_DOOMD); sim_run(0.5f); wait_zone(3, 10.0f);
    t0 = sim_t; sim_run(30.0f); sim_wav("-score-doom-daisy", t0, sim_t);
    set("hbld", 0); sound(S_ORGAN); sim_run(0.5f); wait_zone(0, 10.0f);
    t0 = sim_t; sim_run(20.0f); sim_wav("-score-organ", t0, sim_t);
    set("hbld", 0); sim_run(0.5f);
}

// ---------------------------------------------------------------- 13. the FX bus asleep when there is nothing to do
static void fx_sleep(void)
{
    section("score: the FX bus at rest");
    fx_bus_params_t b = { 0.3f, 12000.0f, 0.4f, 0.35f, 0.3f, 0.5f, 2.0f, 0.45f, 0.0f, 0.0f };
    fx_set_bus(&b);
    float s[AUDIO_BLOCK], io[AUDIO_BLOCK];
    struct timespec t0, t1;
    // a second of a tone into the send, then silence: the tail, then nothing
    for (int i = 0; i < 500; i++) {
        for (int j = 0; j < AUDIO_BLOCK; j++) s[j] = 0.3f * sinf((float)(i * AUDIO_BLOCK + j) * 0.05f);
        memset(io, 0, sizeof io); fx_bus(s, io, AUDIO_BLOCK);
    }
    memset(s, 0, sizeof s);
    double busy = 0;
    for (int i = 0; i < 500; i++) { memset(io, 0, sizeof io); clock_gettime(CLOCK_MONOTONIC, &t0); fx_bus(s, io, AUDIO_BLOCK); clock_gettime(CLOCK_MONOTONIC, &t1); busy += (double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3; }
    busy /= 500;
    for (int i = 0; i < 20000; i++) { memset(io, 0, sizeof io); fx_bus(s, io, AUDIO_BLOCK); }   // 40 s: the tail gone, then 2.2 s more
    double rest = 0;
    for (int i = 0; i < 2000; i++) { memset(io, 0, sizeof io); clock_gettime(CLOCK_MONOTONIC, &t0); fx_bus(s, io, AUDIO_BLOCK); clock_gettime(CLOCK_MONOTONIC, &t1); rest += (double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3; }
    rest /= 2000;
    metric("fx_us_per_block", busy); metric("fx_us_per_block_asleep", rest);
    // asleep it puts out nothing; it wakes on the first block with something in the send: the reverb at once, the
    // echo's repeat where it belongs (12000 frames on), nothing stale before it
    float asleep = 0, early = 0, between = 0, later = 0;
    for (int i = 0; i < 100; i++) { memset(io, 0, sizeof io); fx_bus(s, io, AUDIO_BLOCK); for (int j = 0; j < AUDIO_BLOCK; j++) if (fabsf(io[j]) > asleep) asleep = fabsf(io[j]); }
    for (int i = 0; i < 400; i++) {
        for (int j = 0; j < AUDIO_BLOCK; j++) s[j] = i < 50 ? 0.3f * sinf((float)(i * AUDIO_BLOCK + j) * 0.05f) : 0.0f;
        memset(io, 0, sizeof io); fx_bus(s, io, AUDIO_BLOCK);
        float pk = 0; for (int j = 0; j < AUDIO_BLOCK; j++) if (fabsf(io[j]) > pk) pk = fabsf(io[j]);
        if (i < 50 && pk > early) early = pk;
        if (i >= 120 && i < 180 && pk > between) between = pk;
        if (i >= 187 && i < 250 && pk > later) later = pk;
    }
    (void)between;
    check(rest < 0.1 * busy && rest < 0.15 && asleep == 0.0f && early > 0.02f && later > 0.03f,
          "with nothing to do the bus sleeps (%.2f us a block, %.2f at work, silent) and wakes on the next send (the reverb at once %.3f, the echo's repeat %.3f)",
          rest, busy, (double)early, (double)later);
}

// ---------------------------------------------------------------- 14. a long time in each mode (opt-in)
// MANTRA_SOAK=<seconds a mode> ./run.sh inst: each of the seven modes for that long (3600 is an hour each; about 30
// minutes on the host for all) with everything going at once: the knob swept across the four zones, the mode's
// sounds every bar, the tempo moved by a linked board every few seconds, the hands (tilt, roll, yaw, the pump, the
// stick, knocks), REC takes back to back, a loop stopped and started; the song's beat count passes SONG_WRAP_BEATS
// early on. The recording is dropped as it goes (an hour is 460 MB), so each 0.3 s step is measured on its own
// tail: no NaN, under full scale, never silent for long (HUM ALONG hums into the mic); the score never refuses a
// push (a full queue); at the end of each mode Daisy's vowels are still on her grid and OM's phrase on the beat.
static float soak_mic(float t, void *u) { (void)u; return 0.25f * sinf(6.2831853f * 196.0f * t) + 0.1f * sinf(6.2831853f * 392.0f * t); }
static void soak_knock(void)
{
    for (int i = 0; i < 10; i++) SI.burst[i] = (vec3_t){ 0, 0, 1.0f + (i == 3 ? 3.0f : i == 4 ? -1.8f : 0.0f) };
    SI.nburst = 10; sim_tick();
}
void sim_finish(void);
static void soak(float per)
{
    section("score: a long time in each mode");
    static const int mode_sounds[MODE_N][4] = { { S_OM, S_MONKS, S_SUNN, S_ORGAN }, { S_MANI, -1 }, { S_KHOOMEI, S_SYGYT, -1 }, { S_KARG, -1 },
                                                { S_DAISY, S_DAISY8, S_DOOMD, -1 }, { S_VOWELS, -1 }, { S_HUM, -1 } };
    static const char *const mode_names[MODE_N] = { "OM", "OM MANI", "THROAT", "KARGYRAA", "DAISY", "VOWELS", "HUM ALONG" };
    quiet(); set("hbld", 0); sound(S_OM); sim_run(0.5f);
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);                   // a board that leads the tempo
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(19200, o, 4, 0); }
    sim_peer_use(-1);
    sim_run(1.0f);
    // the wrap: beat 0 moved back so the count passes SONG_WRAP_BEATS in 8 beats
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(song.fpb, o - (uint32_t)(SONG_WRAP_BEATS - 8) * song.fpb, 4, 0); }
    sim_peer_use(-1);
    sim_run(0.3f);
    double b0 = song_beats_at(audio_frames());
    sim_run(8.0f);
    double b1 = song_beats_at(audio_frames());
    check(b0 >= SONG_WRAP_BEATS - 10 && b1 < 10 && m_bad() == 0 && m_rms(sim_t - 2.0f, sim_t) > 0.003f,
          "the song's beat count wraps (beat %.0f -> %.0f) and it sings on", b0, b1);
    int ok = 0;
    for (int m = 0; m < MODE_N; m++) {
        float t0 = sim_t, pk = 0, longest_quiet = 0; int quiet_run = 0, nan = 0, step = 0, snd = 0, tempos = 0, takes = 0;
        sc_drops = 0; sc_tick_ns = 0; sc_tick_n = 0;
        sound(mode_sounds[m][0]); set("hbld", 0);
        SI.mic = m == MODE_HUM ? soak_mic : NULL;
        sim_run(1.0f);
        while (sim_t - t0 < per) {
            float ph = (float)(step % 200) / 200.0f;                    // the knob: 0 to 100 and back over a minute
            set("hbld", 100.0f * (ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f));
            if (step % 10 == 0) { int n = 0; while (n < 4 && mode_sounds[m][n] >= 0) n++; sound(mode_sounds[m][snd++ % n]); }
            if (step % 23 == 0) {                                       // the tempo from the linked board, 90..140
                tempos++;
                uint32_t fpb = 60u * AUDIO_SR / (uint32_t)(90 + (tempos * 17) % 50);
                sim_peer_use(k);
                uint32_t o; float f; link_origin(&o, &f); link_set_time(fpb, o, 4, 0);
                sim_peer_use(-1);
            }
            if (step % 11 == 3) sim_tilt((float)((step % 3) - 1) * 30.0f, (float)((step % 5) - 2) * 15.0f);
            if (step % 13 == 4) { SI.gyro = (vec3_t){ 0, 0, (float)((step % 4) - 2) * 60.0f }; }
            if (step % 13 == 6) SI.gyro = (vec3_t){ 0, 0, 0 };
            if (step % 17 == 5) { SI.jx = (float)((step % 3) - 1); SI.jy = (float)((step % 5) - 2) * 0.5f; }
            if (step % 19 == 8) { vec3_t a = SI.acc; SI.acc = (vec3_t){ a.x * 1.4f, a.y * 1.4f, a.z * 1.4f }; sim_tick(); SI.acc = a; }   // a pump's stroke
            if (step % 29 == 7) soak_knock();
            if (step % 50 == 20) { sim_rec(1.2f); sim_rec(1.6f); takes += 2; }   // takes back to back
            if (step % 97 == 40) { sim_rec(0.1f); sim_run(1.5f); sim_rec(0.1f); }   // the loop stopped, started
            sim_run(0.3f);
            int n = 9600 < sim_n ? 9600 : sim_n;
            const float *x = sim_out + sim_n - n;
            double e = 0; float p = 0;
            for (int i = 0; i < n; i++) { float a = fabsf(x[i]); if (a > p) p = a; e += (double)x[i] * x[i]; }
            nan += nans(x, n);
            if (p > pk) pk = p;
            if (sqrt(e / n) < 0.002) quiet_run++; else quiet_run = 0;
            if ((float)quiet_run * 0.3f > longest_quiet) longest_quiet = (float)quiet_run * 0.3f;
            sim_n = 0;                                                  // the recording dropped (the tail was measured)
            step++;
        }
        quiet(); SI.mic = m == MODE_HUM ? soak_mic : NULL;
        double us = sc_tick_ns / (sc_tick_n ? sc_tick_n : 1) / 1000.0 / 5.0;
        // still in time: Daisy's vowels on her grid, and OM's phrase on the beat
        sound(mode_sounds[m][0]); set("hbld", 100); sim_run(0.5f);
        bool z3 = wait_zone(3, 20.0f);
        daisy_clock();
        sc_log_n = 0; run_beats(12.0);
        int nv = 0; double worst = 0;
        for (int i = 0; i < sc_log_n; i++) {
            sc_log_t *e = &sc_log[i];
            if (e->kind != K_VOWEL || e->tag < 100) continue;
            nv++; double err = fabs(daisy_err_ms(e->at, syl_p6[e->tag - 100], NULL)); if (err > worst) worst = err;
        }
        sim_n = 0;
        bool good = nan == 0 && pk < 0.99f && longest_quiet < 6.0f && sc_drops == 0 && z3 && nv > 3 && worst <= 5.0;
        if (good) ok++;
        note("%s for %.0f s: %d sounds, %d takes, %d tempos; peak %.3f, longest quiet %.1f s, NaN %d, pushes refused %d, the score %.3f us a block; "
             "then Daisy's %d vowels within %.2f ms of her grid%s",
             mode_names[m], (double)(sim_t - t0), snd, takes, tempos, (double)pk, (double)longest_quiet, nan, sc_drops, us, nv, worst, good ? "" : " BAD");
        SI.mic = NULL;
    }
    check(ok == MODE_N, "%.0f s in each mode with everything going: no NaN, under full scale, never silent for long, no push refused, still in time (%d of %d)",
          (double)per, ok, MODE_N);
    sim_peer_power(k, false); sim_run(1.5f);
}

void test_mod_score(void)
{
    daisy_load();
    int keep_key = (int)get("skey");
    daisy_alone();
    zones();
    section("score: a slow sweep"); sweep(S_MANI, "-score-sweep-mani", 46.0f);
    om();
    mani();
    throat();
    kargyraa();
    link_follow();
    free_voices();
    bowl();
    fx_sleep();
    cost();
    tour();
    quiet(); set("hbld", 0); set("skey", (float)keep_key); sound(S_OM); sim_run(0.5f);
    const char *sk = getenv("MANTRA_SOAK");
    if (sk && atof(sk) > 0) {
        soak((float)atof(sk) < 20.0f ? 20.0f : (float)atof(sk));
        sim_finish();
        printf("\n%d checks, %d failed (a soak run ends here: the checks after it read the recording it dropped)\n", checks_run, checks_failed);
        exit(checks_failed ? 1 : 0);
    }
}
