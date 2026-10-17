// KEYS's checks (run by test_inst.c's inst_tests with the whole band): every sound in tune and at a sane level,
// COMP plays the song's chord on the grid and follows the chord, keeps playing when another layer is live, KNOCK
// plays a chord on a knock, the lean moves the voicing, the loop records the rhythm and replays it on the chord
// of the moment, tap REC mutes, no clicks, the cost. Screens and GIFs of the scene.
#include "sim.h"
#include "band.h"
#include "params.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>


enum { M_COMP, M_KNOCK };
enum { PT_WHOLE, PT_HALF, PT_QUARTERS, PT_OFFBEAT, PT_STABS, PT_ARP, PT_GOSPEL, PT_BOSSA };
enum { VC_CLOSE, VC_OPEN, VC_SEVENTHS, VC_SPREAD };
enum { S_PIANO, S_EP, S_ORGAN, S_PAD, S_CLAV, S_VIBES, S_N };
static const char *const snd_name[S_N] = { "PIANO", "E.PIANO", "ORGAN", "PAD", "CLAV", "VIBES" };

static int tagp(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tagp(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); sim_run(0.1f); }
static uint8_t in_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1) & 15); }
static uint8_t keys_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1 + 4) & 15); }
static void key_on(int n, int v) { front_midi((uint8_t)(0x90 | in_ch()), (uint8_t)n, (uint8_t)v); }
static void key_off(int n) { front_midi((uint8_t)(0x80 | in_ch()), (uint8_t)n, 0); }
// CLEAR ALL, run as the menu would, but without walking the menu (its remembered place would change how long the
// next layer's tests take to get there, and so where the song's progression is when they start)
static void clear_all(void)
{
    int p = param_by_tag("xcla"); if (p < 0) p = param_by_tag("lcla");
    if (p >= 0 && INST.action) { INST.action(p); sim_run(0.1f); }
}

// the keys' note-ons in the MIDI log between two times
static int notes_in(float t0, float t1, float *ts, int *ns, int max)
{
    int k = 0;
    for (int i = 0; i < midi_n && k < max; i++) {
        const midi_ev_t *e = &midi_log[i];
        if (e->st == (0x90 | keys_ch()) && e->d2 > 0 && e->t >= t0 && e->t < t1) { if (ts) ts[k] = e->t; if (ns) ns[k] = e->d1; k++; }
    }
    return k;
}
static bool in_pcs(int note, const int *pcs, int n) { for (int i = 0; i < n; i++) if (((note - pcs[i]) % 12 + 12) % 12 == 0) return true; return false; }
static int chord_pcs(int degree, int *pcs) { for (int k = 0; k < 3; k++) pcs[k] = ((scale_note(degree + 2 * k, 4) % 12) + 12) % 12; return 3; }

// a knock: a short burst of noise, loud and dull
static float knock_t[8]; static int nknock;
static float knock_mic(float t, void *u)
{
    (void)u;
    float x = 0.0005f * sim_noise();
    for (int i = 0; i < nknock; i++) { float d = t - knock_t[i]; if (d >= 0 && d < 0.05f) x += 0.6f * sim_noise() * expf(-d / 0.008f); }
    return x;
}

// a held chord (MIDI): steady sound. Not notes 60 and 64: the core takes those as REC and the motion hold.
void excite_keys(void) { key_on(55, 100); key_on(59, 100); key_on(62, 100); }

// the layer alone, driven directly: a held chord, the peak of what it adds to dry over a while
static float layer_peak(int snd, const int *notes, int nn, int vel, float secs)
{
    set("ksnd", (float)snd);
    uint8_t ch = in_ch();
    for (int i = 0; i < nn; i++) layer_keys.midi_in((uint8_t)(0x90 | ch), (uint8_t)notes[i], (uint8_t)vel);
    float in[AUDIO_BLOCK] = { 0 }, d[AUDIO_BLOCK], s[AUDIO_BLOCK], pk = 0;
    int nb = (int)(secs * (float)AUDIO_SR / (float)AUDIO_BLOCK);
    for (int b = 0; b < nb; b++) {
        memset(d, 0, sizeof d); memset(s, 0, sizeof s);
        layer_keys.audio(in, d, s, AUDIO_BLOCK, true);
        for (int i = 0; i < AUDIO_BLOCK; i++) { float a = fabsf(d[i]); if (a > pk) pk = a; }
    }
    layer_keys.midi_in((uint8_t)(0xB0 | ch), 123, 0);
    for (int b = 0; b < 400; b++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
    return pk;
}

static void levels(void)
{
    set("kmod", M_KNOCK); set("kped", 0); set("kdrv", 0); set("kmot", 30); set("kton", 60);
    sim_run(0.2f);
    static const int ch4[4] = { 60, 64, 67, 72 };
    char line[200] = ""; int bad = 0; float lo = 9, hi = 0;
    for (int s = 0; s < S_N; s++) {
        float p = layer_peak(s, ch4, 4, 100, 0.6f);
        if (p < lo) lo = p;
        if (p > hi) hi = p;
        if (p < 0.08f || p > 0.5f) bad++;
        char b[24]; snprintf(b, sizeof b, " %s %.2f", snd_name[s], (double)p); strncat(line, b, sizeof line - strlen(line) - 1);
    }
    note("peak of a four-note chord at velocity 100:%s", line);
    check(bad == 0, "every sound's four-note chord peaks between 0.08 and 0.5 (around -12 dBFS; %.2f .. %.2f)", (double)lo, (double)hi);
    // eight notes ringing over eight more: the most it can get to, still under full scale before the master
    static const int big[8] = { 48, 52, 55, 60, 64, 67, 72, 76 };
    float worst = 0;
    for (int s = 0; s < S_N; s++) { float p = layer_peak(s, big, 8, 127, 0.5f); if (p > worst) worst = p; }
    check(worst < 1.0f, "eight notes at full velocity stay under full scale (peak %.2f)", (double)worst);
}

// the layer alone again: one note held, its first 0.4 s into buf (whatever else the band plays stays out of it)
#define PBUF (AUDIO_SR * 4 / 10)
static float pbuf[PBUF];
static void render_note(int snd, int note)
{
    set("ksnd", (float)snd);
    uint8_t ch = in_ch();
    float in[AUDIO_BLOCK] = { 0 }, d[AUDIO_BLOCK], s[AUDIO_BLOCK];
    layer_keys.midi_in((uint8_t)(0x90 | ch), (uint8_t)note, 100);
    for (int b = 0; b + AUDIO_BLOCK <= PBUF; b += AUDIO_BLOCK) {
        memset(d, 0, sizeof d); memset(s, 0, sizeof s);
        layer_keys.audio(in, d, s, AUDIO_BLOCK, true);
        memcpy(pbuf + b, d, sizeof d);
    }
    layer_keys.midi_in((uint8_t)(0xB0 | ch), 123, 0);
    for (int b = 0; b < 400; b++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
}
static float goertzel(const float *x, int n, float f)
{
    float w = 6.2831853f * f / (float)AUDIO_SR, c = 2.0f * cosf(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { float s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void pitch_per_sound(void)
{
    set("kmod", M_KNOCK); SI.mic = NULL;
    int bad = 0; char line[240] = "";
    for (int s = 0; s < S_N; s++) {
        render_note(s, 57);                                    // A3: 220 Hz
        const float *x = pbuf + AUDIO_SR / 20; int n = PBUF - AUDIO_SR / 20;
        // by its pitch; the organ's 16' and the pad's sub sound an octave below (as on the real ones), so for them
        // the note must be the strongest of its semitone neighbourhood
        char b[32];
        if (s == S_ORGAN || s == S_PAD) {
            float on = goertzel(x, n, 220.0f), lo_ = goertzel(x, n, 207.65f), hi_ = goertzel(x, n, 233.08f);
            float r = on / (fmaxf(lo_, hi_) + 1e-12f);
            if (r < 8.0f) bad++;
            snprintf(b, sizeof b, " %s %.0fx", snd_name[s], (double)r);
        } else {
            float f = m_pitch(x, n, 100.0f, 500.0f);
            float c = f > 0 ? 1200.0f * log2f(f / 220.0f) : 999.0f;
            if (fabsf(c) > 15.0f) bad++;
            snprintf(b, sizeof b, " %s %+.0f", snd_name[s], (double)c);
        }
        strncat(line, b, sizeof line - strlen(line) - 1);
    }
    note("A3 (cents off; organ and pad: 220 Hz against a semitone either side):%s", line);
    check(bad == 0, "every sound plays A3 in tune");
}

static void comp_grid(void)
{
    set("kmod", M_COMP); set("kpat", PT_QUARTERS); set("kswg", 0); set("khum", 0); set("kvoi", VC_CLOSE); set("ksnd", S_EP);
    host_chord_override(0);
    sim_run(1.3f);
    float t0 = sim_t;
    sim_gif_begin("keys-comp");
    sim_run(2.0f);
    sim_shot("keys_comp");
    sim_gif_end();
    float ts[128]; int ns[128];
    int n = notes_in(t0, sim_t, ts, ns, 128);
    int pcs[3]; chord_pcs(0, pcs);
    int off_grid = 0, wrong = 0, chords = 0;
    for (int i = 0; i < n; i++) {
        float ph = song_beat((uint32_t)((ts[i] - 0.005f) * AUDIO_SR)), fr = ph - floorf(ph);   // MIDI leaves at the tick after
        if (fr > 0.1f && fr < 0.9f) off_grid++;
        if (!in_pcs(ns[i], pcs, 3)) wrong++;
        if (i == 0 || ts[i] - ts[i - 1] > 0.02f) chords++;
    }
    float quarters = 2.0f * song.bpm / 60.0f;
    check(n >= 3 * (int)quarters - 3 && fabsf((float)chords - quarters) <= 1.5f, "COMP, QUARTERS: %d chords (%d notes) in 2 s at %.0f BPM (%.0f expected)", chords, n, (double)song.bpm, (double)quarters);
    check(off_grid == 0 && wrong == 0, "every note on a beat and a tone of the chord (%d off the grid, %d wrong)", off_grid, wrong);
    // a chord change: the next chords are the V's
    host_chord_override(4);
    sim_run(0.7f);
    float t1 = sim_t; sim_run(1.2f);
    n = notes_in(t1, sim_t, ts, ns, 128);
    chord_pcs(4, pcs);
    wrong = 0; for (int i = 0; i < n; i++) if (!in_pcs(ns[i], pcs, 3)) wrong++;
    check(n >= 3 && wrong == 0, "the chord goes to V: it plays the V's tones (%d notes, %d wrong)", n, wrong);
    // voice leading: from I to V the hands move little (no note jumps more than a fifth)
    host_chord_override(0); sim_run(0.6f);
    float t2 = sim_t; sim_run(0.6f);
    int a[16]; int na = notes_in(t2, sim_t, NULL, a, 16);
    host_chord_override(4); sim_run(0.6f);
    float t3 = sim_t; sim_run(0.6f);
    int b[16]; int nb2 = notes_in(t3, sim_t, NULL, b, 16);
    int lo_a = 127, hi_a = 0, lo_b = 127, hi_b = 0;
    for (int i = 0; i < na; i++) { if (a[i] < lo_a) lo_a = a[i]; if (a[i] > hi_a) hi_a = a[i]; }
    for (int i = 0; i < nb2; i++) { if (b[i] < lo_b) lo_b = b[i]; if (b[i] > hi_b) hi_b = b[i]; }
    check(na && nb2 && abs(lo_a - lo_b) <= 7 && abs(hi_a - hi_b) <= 7, "voice-led: I (%d..%d) to V (%d..%d), no part moves more than a fifth", lo_a, hi_a, lo_b, hi_b);
    host_chord_override(0);
    // the timing of each pattern: one bar of each, the hits where the pattern says
    static const int pats[] = { PT_WHOLE, PT_HALF, PT_OFFBEAT, PT_STABS, PT_ARP, PT_GOSPEL, PT_BOSSA };
    static const char *const pn[] = { "WHOLE", "HALF", "OFFBEAT", "STABS", "ARPEGGIO", "GOSPEL", "BOSSA" };
    char line[240] = "";
    int empty = 0;
    for (int k = 0; k < 7; k++) {
        set("kpat", (float)pats[k]); sim_run(0.3f);
        float tb = sim_t; sim_run(4.0f * 60.0f / song.bpm);
        int c = notes_in(tb, sim_t, NULL, NULL, 128);
        if (!c) empty++;
        char bb[24]; snprintf(bb, sizeof bb, " %s %d", pn[k], c); strncat(line, bb, sizeof line - strlen(line) - 1);
    }
    note("notes in a bar:%s", line);
    check(empty == 0, "every pattern plays");
    set("kpat", PT_QUARTERS);
}

static void keeps_playing(void)
{
    // a second live in COMP, then DRUMS live: the keys player plays on; tap REC on KEYS mutes it
    make_live(L_KEYS); set("kmod", M_COMP); sim_run(1.3f);
    make_live(L_DRUMS); sim_run(0.3f);
    float t0 = sim_t; sim_run(1.5f);
    int n = notes_in(t0, sim_t, NULL, NULL, 128);
    check(n > 0 && layer_keys.playing && layer_keys.playing(), "DRUMS live: the keys play on by themselves (%d notes)", n);
    make_live(L_KEYS);
    sim_rec(0.1f); sim_run(0.3f);
    float tm = sim_t; sim_run(1.5f);
    int m = notes_in(tm, sim_t, NULL, NULL, 128);
    check(m == 0 && !layer_keys.playing(), "tap REC: muted, nothing plays (%d notes)", m);
    sim_rec(0.1f); sim_run(1.2f);
    tm = sim_t; sim_run(1.0f);
    check(notes_in(tm, sim_t, NULL, NULL, 128) > 0, "tap again: it plays");
}

static void knock_mode(void)
{
    set("kmod", M_KNOCK); set("ksnd", S_PIANO); set("kvoi", VC_CLOSE); set("khum", 0);
    sim_tilt(0, 0); sim_run(1.0f);                             // COMP's last notes ring out
    nknock = 0; SI.mic = knock_mic;
    knock_t[nknock++] = sim_t + 0.1f;
    float t0 = sim_t; sim_run(0.5f);
    float ts[16]; int ns[16];
    int n = notes_in(t0, sim_t, ts, ns, 16);
    int pcs[3]; chord_pcs(song.degree, pcs);
    int wrong = 0, lo1 = 127; for (int i = 0; i < n; i++) { if (!in_pcs(ns[i], pcs, 3)) wrong++; if (ns[i] < lo1) lo1 = ns[i]; }
    check(n == 3 && wrong == 0, "KNOCK: a knock plays the chord (%d notes, %d not in it)", n, wrong);
    float lag = n ? ts[0] - knock_t[0] : 1.0f;
    check(lag >= 0 && lag < 0.015f, "within a tick of the knock (%.1f ms)", (double)(lag * 1000.0f));
    sim_shot("keys_knock");
    // leaning towards the bottom edge: the voicing goes up an inversion or two
    sim_tilt(0, 20); sim_run(0.4f);
    knock_t[nknock++] = sim_t + 0.1f;
    t0 = sim_t; sim_run(0.5f);
    n = notes_in(t0, sim_t, ts, ns, 16);
    int lo2 = 127; for (int i = 0; i < n; i++) if (ns[i] < lo2) lo2 = ns[i];
    check(n == 3 && lo2 > lo1, "tipped 20 degrees: the same chord, higher (lowest note %d, was %d)", lo2, lo1);
    sim_shot("keys_knock_up");
    SI.mic = NULL; nknock = 0; sim_tilt(0, 0); sim_run(0.6f);
}

static void loop_rec(void)
{
    set("kmod", M_COMP); set("kpat", PT_STABS); set("khum", 0); set("kswg", 0); set("ksnd", S_EP);
    host_chord_override(0);
    bool had_loop = song.loop_len != 0;
    sim_run(1.2f);
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    check(song.loop_len > 0 && layer_keys.state && layer_keys.state() == LOOP_ON,
          "hold REC 2.4 s: a loop (%s, %d beats at %.1f BPM)", had_loop ? "added to the song's" : "the song's first", song.beats, (double)song.bpm);
    set("kmod", M_KNOCK);                                      // COMP stops: what plays now is the loop
    sim_run(0.3f);
    float L = (float)song.loop_len / (float)AUDIO_SR, tA = sim_t;
    sim_gif_begin("keys-loop");
    sim_run(2.0f * L + 0.05f);
    sim_shot("keys_loop");
    sim_gif_end();
    float ta[96], tb[96]; int na[96], nb[96];
    int n1 = notes_in(tA, tA + L, ta, na, 96), n2 = notes_in(tA + L, tA + 2.0f * L, tb, nb, 96);
    int same = 0;
    for (int i = 0; i < n1 && i < n2; i++) if (na[i] == nb[i] && fabsf((tb[i] - L) - ta[i]) < 0.012f) same++;
    check(n1 >= 3 && n1 == n2 && same == n1, "it replays: %d notes a pass, the same at the same times in the next (%d)", n1, same);
    // the rhythm keeps, the chord is the song's: on the V, the loop plays the V
    host_chord_override(4); sim_run(0.3f);
    float tv = sim_t; sim_run(L);
    int nv[96]; int k = notes_in(tv, sim_t, NULL, nv, 96);
    int pcs[3]; chord_pcs(4, pcs);
    int wrong = 0; for (int i = 0; i < k; i++) if (!in_pcs(nv[i], pcs, 3)) wrong++;
    check(k == n1 && wrong == 0, "the chord moves to V: the loop plays the V's tones in the same rhythm (%d notes, %d wrong)", k, wrong);
    host_chord_override(0);
    make_live(L_KEYS);
    sim_rec(0.1f); sim_run(0.1f);
    float tm = sim_t; sim_run(L);
    check(layer_keys.state() == LOOP_OFF && notes_in(tm, sim_t, NULL, NULL, 64) == 0, "tap REC: the loop muted, nothing plays");
    sim_rec(0.1f); sim_run(0.1f);
    tm = sim_t; sim_run(L);
    check(layer_keys.state() == LOOP_ON && notes_in(tm, sim_t, NULL, NULL, 64) > 0, "tap again: it plays");
    // muted, then REC held: a new take in place of the old (one with no chords: the loop is empty after it)
    sim_rec(0.1f); sim_run(0.1f);
    SI.rec = true; sim_run(0.8f); SI.rec = false; sim_tick();
    tm = sim_t; sim_run(L);
    check(layer_keys.state() == LOOP_NONE && notes_in(tm, sim_t, NULL, NULL, 64) == 0, "muted, then REC held: a new take replaces the loop (state %d)", layer_keys.state());
    layer_keys.clear();
    check(layer_keys.state() == LOOP_NONE, "clear: the loop is gone");
}

static void midi_channel(void)
{
    // everything the keys send goes out on the base channel + 4, nothing of theirs on another
    int base = (int)pv[CP(CP_MIDI_CH)];
    set("kmod", M_COMP); set("kpat", PT_QUARTERS);
    sim_run(1.3f);
    int i0 = midi_n; float t0 = sim_t; sim_run(1.0f);
    int on = notes_in(t0, sim_t, NULL, NULL, 64), stray = 0;
    for (int i = i0; i < midi_n; i++) {
        uint8_t k = midi_log[i].st & 0xF0, c = midi_log[i].st & 15;
        if ((k == 0x90 || k == 0x80) && c != keys_ch() && c != 9 && c != (uint8_t)((base - 1) & 15) && c != (uint8_t)(base & 15) && c != (uint8_t)((base + 1) & 15) && c != (uint8_t)((base + 2) & 15)) stray++;
    }
    check(on > 0 && stray == 0, "MIDI: the keys' notes on channel %d (%d notes, %d elsewhere)", keys_ch() + 1, on, stray);
}

static void sounds_gallery(void)
{
    set("kmod", M_COMP); set("kpat", PT_GOSPEL); set("kvoi", VC_SEVENTHS); set("khum", 30);
    int prg = param_by_tag("sprg"); float keep = prg >= 0 ? pv[prg] : 0;
    if (prg >= 0) param_set(prg, 1);                            // I V vi IV
    host_chord_override(-1);
    sim_gif_begin("keys-sounds");
    for (int s = 0; s < S_N; s++) {
        set("ksnd", (float)s);
        if (s == S_ORGAN) set("kmot", 80);
        sim_run(1.6f);
        char b[32]; snprintf(b, sizeof b, "keys_%s", snd_name[s]); for (char *q = b; *q; q++) if (*q == '.') *q = '_';
        sim_shot(b);
        if (s == S_ORGAN) set("kmot", 30);
    }
    sim_gif_end();
    if (prg >= 0) param_set(prg, keep);
    host_chord_override(0);
    check(m_bad() == 0 && m_peak(sim_t - 9.0f, sim_t) < 0.95f, "all six sounds through a progression: no NaN, under full scale (peak %.2f)", (double)m_peak(sim_t - 9.0f, sim_t));
}

static void clicks(void)
{
    set("kmod", M_KNOCK); SI.mic = NULL;
    static const char *const tags[] = { "kton", "krel", "kmot", "kdrv", "kfx ", "ksnd", "koct", "kvoi", "kped", "kswg", "khum", "ktch", "kpat", NULL };
    float worst = 0; const char *wt = "";
    static const int snds[] = { S_EP, S_ORGAN, S_PAD };
    for (int si = 0; si < 3; si++) {
        set("ksnd", (float)snds[si]);
        excite_keys(); sim_run(0.5f);
        for (int k = 0; tags[k]; k++) {
            int p = tagp(tags[k]); if (p < 0) continue;
            const param_t *d = param(p);
            float keep = pv[p];
            for (int dir = 0; dir < 2; dir++) {
                param_set(p, dir ? d->min : d->max); sim_run(0.3f);
                param_set(p, dir ? d->max : d->min);
                float tc = sim_t; sim_run(0.15f);
                float r = m_click(tc, NULL, NULL);
                if (r > worst) { worst = r; wt = d->name; }
            }
            param_set(p, keep); sim_run(0.15f);
        }
        set("ksnd", (float)snds[si]);
        key_off(55); key_off(59); key_off(62); sim_run(0.5f);
    }
    check(worst <= 3.0f, "no click when a param jumps end to end under a held chord (worst %.1fx, %s)", (double)worst, wt);
}

static void cost(void)
{
    float in[AUDIO_BLOCK] = { 0 }, d[AUDIO_BLOCK], s[AUDIO_BLOCK];
    double worst = 0; int ws = 0;
    uint8_t ch = in_ch();
    for (int k = 0; k < S_N; k++) {
        set("ksnd", (float)k); set("kmod", M_KNOCK); set("kped", 1);
        // a full chord ringing over the one before: eight voices
        static const int a[4] = { 48, 52, 55, 59 }, b[4] = { 60, 64, 67, 71 };
        for (int i = 0; i < 4; i++) layer_keys.midi_in((uint8_t)(0x90 | ch), (uint8_t)a[i], 110);
        for (int i = 0; i < 20; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
        for (int i = 0; i < 4; i++) layer_keys.midi_in((uint8_t)(0x90 | ch), (uint8_t)b[i], 110);
        for (int i = 0; i < 4; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
        double us = 1e9;                                       // the best of five runs: the code's cost, not the machine's load
        for (int r = 0; r < 5; r++) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < 400; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double u = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3) / 400.0;
            if (u < us) us = u;
        }
        note("%s: %.2f us per block, eight voices", snd_name[k], us);
        if (us > worst) { worst = us; ws = k; }
        layer_keys.midi_in((uint8_t)(0xB0 | ch), 123, 0);
        set("kped", 0);
        for (int i = 0; i < 300; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
    }
    {   // with nothing sounding: the chain alone
        double us = 1e9;
        for (int r = 0; r < 5; r++) {
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (int i = 0; i < 400; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_keys.audio(in, d, s, AUDIO_BLOCK, true); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double u = ((double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-3) / 400.0;
            if (u < us) us = u;
        }
        note("nothing sounding: %.2f us per block", us);
    }
    metric("keys_us_per_block", worst);
    check(worst <= 5.0, "the keys take %.2f us per 64-frame block at their busiest (%s, eight voices; budget 5)", worst, snd_name[ws]);
}

void test_layer_keys(void)
{
    float phase0 = fmodf(song_beat(audio_frames()), 16.0f);    // where the song's four bars were when this began
    midi_n = 0;                                                 // a long run fills the log: start it again
    int prg = tagp("sprg"); float prg_keep = prg >= 0 ? pv[prg] : 0;
    if (prg >= 0) param_set(prg, 0);                            // the song holds its chord: the tests move it
    clear_all();
    make_live(L_KEYS);
    check(host_live() == L_KEYS, "the knob makes KEYS the live layer");
    sim_run(0.3f);
    sim_shot("keys_live");
    levels();
    pitch_per_sound();
    comp_grid();
    keeps_playing();
    knock_mode();
    loop_rec();
    midi_channel();
    sounds_gallery();
    clicks();
    cost();
    // leave it as it was: another layer live, defaults, the chord back with the song, no loop, the player off
    make_live(L_DRUMS);
    for (int i = 0; i < layer_keys.nparams; i++) param_set(layer_base[L_KEYS] + i, layer_keys.params[i].def);
    host_chord_override(-1);
    if (prg >= 0) param_set(prg, prg_keep);
    clear_all();
    layer_keys.clear();
    sim_tilt(0, 0); SI.mic = NULL;
    sim_run(0.5f);
    // end on the same beat of the four bars as at the start, so the layers tested next meet the progression where
    // they would have without these tests
    for (int i = 0; i < 2000; i++) {
        float d = fmodf(song_beat(audio_frames()), 16.0f) - phase0;
        if (fabsf(d) < 0.02f) break;
        sim_tick();
    }
}
