// BASS's checks (run by test_inst.c's inst_tests with the whole jam): the pitch of every sound, KNOCK plays the
// tilted note on a knock, HUM follows a voice two octaves down, FOLLOW keeps the grid, LOCK KICK plays on the
// kicks, leaning changes the song's chord, the loop records and replays on the grid, tap REC mutes, no clicks,
// the cost. Screens and a GIF of FOLLOW changing chords.
#include "sim.h"
#include "jam.h"
#include "params.h"
#include <math.h>
#include <string.h>
#include <time.h>

bool sim_menu_param(int p);
void sim_menu_close(void);

enum { M_KNOCK, M_HUM, M_FOLLOW };
enum { PT_ROOT, PT_FIFTH, PT_OCT, PT_WALK, PT_ARP, PT_ACID };
enum { S_SUB, S_ACID, S_808, S_FINGER, S_REESE, S_FM, S_N };
static const char *const snd_name[S_N] = { "SUB", "ACID", "808", "FINGER", "REESE", "FM" };

static int tagp(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tagp(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); sim_run(0.1f); }
static uint8_t in_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1) & 15); }
static uint8_t bass_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1 + 1) & 15); }
static void key_on(int n, int v) { front_midi((uint8_t)(0x90 | in_ch()), (uint8_t)n, (uint8_t)v); }
static void key_off(int n) { front_midi((uint8_t)(0x80 | in_ch()), (uint8_t)n, 0); }

// the bass's note-ons in the MIDI log between two times
static int notes_in(float t0, float t1, float *ts, int *ns, int max)
{
    int k = 0;
    for (int i = 0; i < midi_n && k < max; i++) {
        const midi_ev_t *e = &midi_log[i];
        if (e->st == (0x90 | bass_ch()) && e->d2 > 0 && e->t >= t0 && e->t < t1) { if (ts) ts[k] = e->t; if (ns) ns[k] = e->d1; k++; }
    }
    return k;
}
static bool off_in(float t0, float t1, int note)
{
    for (int i = 0; i < midi_n; i++) {
        const midi_ev_t *e = &midi_log[i];
        bool off = e->st == (0x80 | bass_ch()) || (e->st == (0x90 | bass_ch()) && e->d2 == 0);
        if (off && e->d1 == note && e->t >= t0 && e->t < t1) return true;
    }
    return false;
}

// a knock: a short burst of noise, loud and dull
static float knock_t[8]; static int nknock;
static float knock_mic(float t, void *u)
{
    (void)u;
    float x = 0.0005f * sim_noise();
    for (int i = 0; i < nknock; i++) { float d = t - knock_t[i]; if (d >= 0 && d < 0.05f) x += 0.6f * sim_noise() * expf(-d / 0.008f); }
    return x;
}
// a hummed G3: a buzzy voice (a few harmonics) with a little vibrato
static float hum_hz = 196.0f;
static float hum_mic(float t, void *u)
{
    (void)u;
    float ph = hum_hz * t + 0.6f * sinf(6.2831853f * 5.0f * t) / 5.0f * 0.01f * hum_hz / 6.2831853f;
    float x = 0;
    for (int h = 1; h <= 5; h++) x += sinf(6.2831853f * (float)h * ph) / (float)h;
    return 0.09f * x + 0.002f * sim_noise();
}

void excite_bass(void) { key_on(40, 100); }        // a held note (MIDI): a steady sound for the click check

static void pitch_per_sound(void)
{
    set("bmod", M_KNOCK); SI.mic = NULL;
    float worst = 0; int bad = 0;
    char line[160] = "";
    for (int s = 0; s < S_N; s++) {
        set("bsnd", (float)s); sim_run(0.15f);
        key_on(45, 100);                                       // A2: 110 Hz
        float t0 = sim_t;
        sim_run(0.6f);
        float f = m_pitch_at(t0 + 0.12f, t0 + 0.45f, 40.0f, 400.0f);
        key_off(45); sim_run(0.4f);
        float c = f > 0 ? 1200.0f * log2f(f / 110.0f) : 999.0f;
        if (fabsf(c) > fabsf(worst)) worst = c;
        if (fabsf(c) > 15.0f) bad++;
        char b[32]; snprintf(b, sizeof b, " %s %+.0f", snd_name[s], (double)c); strncat(line, b, sizeof line - strlen(line) - 1);
    }
    note("cents off at A2:%s", line);
    check(bad == 0, "every sound plays A2 in tune (worst %+.1f cents)", (double)worst);
}

static void knock_mode(void)
{
    set("bmod", M_KNOCK); set("bsnd", S_FINGER); set("boct", 0);
    sim_tilt(0, 0); sim_run(0.5f);
    nknock = 0; SI.mic = knock_mic;
    int exp0 = scale_note(song.degree, 2);                     // flat: the chord's root
    knock_t[nknock++] = sim_t + 0.1f;
    float t0 = sim_t; sim_run(0.5f);
    int ns[8]; float ts[8];
    int n = notes_in(t0, sim_t, ts, ns, 8);
    check(n == 1 && ns[0] == exp0, "KNOCK, flat: a knock plays the chord's root (%d note%s, %d, wanted %d)", n, n == 1 ? "" : "s", n ? ns[0] : -1, exp0);
    float f = m_pitch_at(knock_t[0] + 0.03f, knock_t[0] + 0.25f, 30.0f, 400.0f);
    float want = 440.0f * powf(2.0f, ((float)exp0 - 69.0f) / 12.0f);
    check(f > 0 && fabsf(1200.0f * log2f(f / want)) < 25.0f, "and it sounds at that pitch (%.1f Hz, wanted %.1f)", (double)f, (double)want);
    sim_shot("bass_knock");
    sim_tilt(0, 14); sim_run(0.5f);                            // two rungs up the scale
    int exp2 = scale_note(song.degree + 2, 2);
    knock_t[nknock++] = sim_t + 0.1f;
    t0 = sim_t; sim_run(0.5f);
    n = notes_in(t0, sim_t, ts, ns, 8);
    check(n == 1 && ns[0] == exp2, "tipped 14 degrees: two notes up the scale (%d, wanted %d)", n ? ns[0] : -1, exp2);
    float lag = n ? ts[0] - knock_t[1] : 1.0f;
    check(lag >= 0 && lag < 0.012f, "the note goes out within a tick of the knock (%.1f ms)", (double)(lag * 1000.0f));
    SI.mic = NULL; nknock = 0; sim_tilt(0, 0); sim_run(0.5f);
}

static void hum_mode(void)
{
    set("bmod", M_HUM); set("bsnd", S_SUB); set("boct", 0);
    sim_run(0.3f);
    hum_hz = 196.0f; SI.mic = hum_mic;                         // G3
    float t0 = sim_t;
    sim_gif_begin("bass-hum");
    sim_run(1.2f);
    sim_shot("bass_hum");
    sim_gif_end();
    int ns[16]; float ts[16];
    int n = notes_in(t0, sim_t, ts, ns, 16);
    int want = 43;                                             // G2, folded down into the bass's range
    check(n >= 1 && ns[0] == want && n <= 2, "HUM: a sung G3 plays G2 (%d note%s, first %d)", n, n == 1 ? "" : "s", n ? ns[0] : -1);
    float lag = n ? ts[0] - t0 : 9.0f;
    check(lag < 0.15f, "within %.0f ms of starting to sing", (double)(lag * 1000.0f));
    float f = m_pitch_at(sim_t - 0.5f, sim_t - 0.1f, 40.0f, 400.0f);
    check(f > 0 && fabsf(1200.0f * log2f(f / 97.999f)) < 15.0f, "and sounds at 98 Hz (%.1f)", (double)f);
    hum_hz = 233.08f;                                           // up a minor third to A#3: not in C major, snaps to A or B
    float t1 = sim_t; sim_run(0.6f);
    n = notes_in(t1, sim_t, ts, ns, 16);
    check(n >= 1 && (ns[n - 1] == 45 || ns[n - 1] == 47), "a sung A#3 lands on a note of the scale (%d)", n ? ns[n - 1] : -1);
    SI.mic = NULL;
    float t2 = sim_t; sim_run(0.4f);
    check(off_in(t2, sim_t, n ? ns[n - 1] : 0), "stop singing: the note stops");
}

static void follow_grid(void)
{
    set("bmod", M_FOLLOW); set("bpat", PT_ROOT); set("brat", 1); set("bkik", 0); set("bfil", 0); set("bsnd", S_FINGER);
    host_chord_override(0);
    sim_run(1.3f);                                             // a second live: the bass player joins
    float t0 = sim_t;
    sim_run(2.0f);
    float ts[64]; int ns[64];
    int n = notes_in(t0, sim_t, ts, ns, 64);
    float eighths = 2.0f * song.bpm / 30.0f;
    int off_grid = 0, wrong = 0;
    for (int i = 0; i < n; i++) {
        float ph = (ts[i] - 0.005f) * song.bpm / 30.0f, fr = ph - floorf(ph);   // MIDI leaves at the tick after the note
        if (fr > 0.08f && fr < 0.92f) off_grid++;
        if (ns[i] != song.chord[0]) wrong++;
    }
    check(fabsf((float)n - eighths) <= 1.5f, "FOLLOW, ROOT, 1/8: %d notes in 2 s at %.0f BPM (%.0f expected)", n, (double)song.bpm, (double)eighths);
    check(off_grid == 0 && wrong == 0, "every one on an eighth and on the chord's root (%d off the grid, %d wrong)", off_grid, wrong);

    // LOCK KICK: with the drums' kicks on sixteenths 0, 6 and 10 it plays there
    set("bkik", 1); set("brat", 2);
    const uint64_t K = (1u << 0) | (1u << 6) | (1u << 10);
    for (int i = 0; i < 60; i++) { song.kicks = K; sim_tick(); }
    t0 = sim_t;
    for (int i = 0; i < 240; i++) { song.kicks = K; sim_tick(); }
    n = notes_in(t0, sim_t, ts, ns, 64);
    int on_kick = 0;
    for (int i = 0; i < n; i++) {
        int s = (int)lroundf((ts[i] - 0.005f) * song.bpm / 15.0f) % 16;
        if (s == 0 || s == 6 || s == 10) on_kick++;
    }
    float bars = 2.4f * song.bpm / 240.0f;
    check(n > 0 && on_kick == n && (float)n >= 2.5f * bars, "LOCK KICK: %d notes, all on the kicks (%d), about 3 a bar", n, on_kick);
    song.kicks = 0;
    set("bkik", 0); set("brat", 1);
}

static void lean_chords(void)
{
    set("bpat", PT_ROOT); set("blch", 1);
    sim_tilt(0, 0); sim_run(0.4f);
    sim_gif_begin("bass-follow");
    sim_tilt(12, 0); sim_run(0.5f);
    int d1 = song.degree;
    float t0 = sim_t; sim_run(0.8f);
    int ns[32]; int n = notes_in(t0, sim_t, NULL, ns, 32);
    check(d1 == 4, "leaning right: the song goes to the V chord (degree %d)", d1);
    check(n > 0 && ns[n - 1] == song.chord[0], "and the bass plays its root (%d, root %d)", n ? ns[n - 1] : -1, song.chord[0]);
    sim_shot("bass_follow_v");
    sim_tilt(-12, 0); sim_run(0.7f);
    check(song.degree == 3, "leaning left: the IV (degree %d)", song.degree);
    sim_tilt(-24, 0); sim_run(0.7f);
    check(song.degree == 1, "further left: the ii (degree %d)", song.degree);
    set("bpat", PT_ACID); set("bsnd", S_ACID);
    sim_tilt(0, 0); sim_run(1.6f);
    check(song.degree == 0, "flat again: the I (degree %d)", song.degree);
    sim_shot("bass_acid");
    sim_gif_end();
    set("blch", 0); sim_run(0.1f);                             // LEAN CHORD off hands the chord back to the song
    int d0 = song.degree;
    sim_tilt(12, 0); sim_run(0.5f);
    check(song.degree == d0, "LEAN CHORD off: leaning leaves the chord alone (degree %d, was %d)", song.degree, d0);
    sim_tilt(0, 0); set("blch", 1); sim_run(0.3f);
}

static void loop_rec(void)
{
    set("bmod", M_FOLLOW); set("bpat", PT_OCT); set("brat", 1); set("bkik", 0); set("bsnd", S_FINGER);
    host_chord_override(0);
    bool had_loop = song.loop_len != 0;
    sim_run(1.2f);
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    check(song.loop_len > 0 && layer_bass.state && layer_bass.state() == LOOP_ON,
          "hold REC 2.4 s: a loop (%s, %d beats at %.1f BPM)", had_loop ? "added to the song's" : "the song's first", song.beats, (double)song.bpm);
    set("bmod", M_KNOCK);                                      // the bass player stops: what plays now is the loop
    sim_run(0.3f);
    float L = (float)song.loop_len / (float)AUDIO_SR, tA = sim_t;
    sim_gif_begin("bass-loop");
    sim_run(2.0f * L + 0.05f);
    sim_shot("bass_loop");
    sim_gif_end();
    float ta[64], tb[64]; int na[64], nb[64];
    int n1 = notes_in(tA, tA + L, ta, na, 64), n2 = notes_in(tA + L, tA + 2.0f * L, tb, nb, 64);
    int same = 0;
    for (int i = 0; i < n1 && i < n2; i++) if (na[i] == nb[i] && fabsf((tb[i] - L) - ta[i]) < 0.012f) same++;
    check(n1 >= 6 && n1 == n2 && same == n1, "it replays: %d notes a pass, the same notes at the same times in the next (%d)", n1, same);
    int off = 0;
    float start = (float)song.loop_start / (float)AUDIO_SR;
    for (int i = 0; i < n1; i++) {
        float ph = fmodf(ta[i] - 0.005f - start, L) / L * (float)(song.beats * 4), fr = ph - floorf(ph);
        if (fr > 0.08f && fr < 0.92f) off++;
    }
    check(off == 0, "every loop note on a sixteenth of the song's loop (%d off)", off);
    sim_rec(0.1f); sim_run(0.1f);
    float tm = sim_t; sim_run(L);
    check(layer_bass.state() == LOOP_OFF && notes_in(tm, sim_t, NULL, NULL, 64) == 0, "tap REC: muted, nothing plays");
    sim_rec(0.1f); sim_run(0.1f);
    tm = sim_t; sim_run(L);
    check(layer_bass.state() == LOOP_ON && notes_in(tm, sim_t, NULL, NULL, 64) > 0, "tap again: it plays");
    layer_bass.clear();
    check(layer_bass.state() == LOOP_NONE, "clear: the loop is gone");
}

static void clicks(void)
{
    set("bmod", M_KNOCK); set("bsnd", S_ACID); SI.mic = NULL;
    key_on(40, 100); sim_run(0.6f);
    static const char *const tags[] = { "bcut", "bres", "benv", "bdec", "bgld", "bdrv", "bfx ", "bsnd", "boct", "bfil", "brif", NULL };
    float worst = 0; const char *wt = "";
    for (int k = 0; tags[k]; k++) {
        int p = tagp(tags[k]); if (p < 0) continue;
        const param_t *d = param(p);
        float keep = pv[p];
        for (int dir = 0; dir < 2; dir++) {
            param_set(p, dir ? d->min : d->max); sim_run(0.35f);
            param_set(p, dir ? d->max : d->min);
            float tc = sim_t; sim_run(0.2f);
            float r = m_click(tc, NULL, NULL);
            if (r > worst) { worst = r; wt = d->name; }
        }
        param_set(p, keep); sim_run(0.2f);
    }
    key_off(40); sim_run(0.3f);
    check(worst <= 3.0f, "no click when a sound param jumps end to end under a held note (worst %.1fx, %s)", (double)worst, wt);
}

static void cost(void)
{
    float in[AUDIO_BLOCK] = { 0 }, d[AUDIO_BLOCK], s[AUDIO_BLOCK];
    double worst = 0;
    static const int snds[] = { S_ACID, S_REESE, S_FINGER, S_FM };
    for (int k = 0; k < 4; k++) {
        set("bsnd", (float)snds[k]); set("bmod", M_KNOCK);
        key_on(40, 120); sim_run(0.3f);
        double us = 1e9;                                       // the best of five runs: the code's cost, not the machine's load
        for (int r = 0; r < 5; r++) {
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            for (int i = 0; i < 1000; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_bass.audio(in, d, s, AUDIO_BLOCK, true); }
            clock_gettime(CLOCK_MONOTONIC, &b);
            double u = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 1000.0;
            if (u < us) us = u;
        }
        if (us > worst) worst = us;
        key_off(40); sim_run(0.2f);
    }
    metric("bass_us_per_block", worst);
    check(worst <= 3.0, "the bass takes %.2f us per 64-frame block at its busiest (budget 3)", worst);
}

void test_layer_bass(void)
{
    midi_n = 0;                                                 // a long run fills the log: start it again
    int prg = tagp("sprg"); float prg_keep = prg >= 0 ? pv[prg] : 0;
    if (prg >= 0) param_set(prg, 0);                            // the song holds its chord: only the bass moves it
    int cl = tagp("lcla");                                      // no loop left by the tests before: the free grid
    if (cl >= 0 && sim_menu_param(cl)) { sim_tap(); sim_run(0.1f); sim_menu_close(); }
    make_live(L_BASS);
    check(host_live() == L_BASS, "the knob makes BASS the live layer");
    sim_run(0.3f);
    sim_shot("bass_follow");
    pitch_per_sound();
    knock_mode();
    hum_mode();
    follow_grid();
    lean_chords();
    loop_rec();
    clicks();
    cost();
    // leave it as it was: another layer live, defaults, the chord back with the song, no loop
    make_live(L_DRUMS);
    for (int i = 0; i < layer_bass.nparams; i++) param_set(layer_base[L_BASS] + i, layer_bass.params[i].def);
    host_chord_override(-1);
    if (prg >= 0) param_set(prg, prg_keep);
    int p = tagp("lcla");
    if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); }
    layer_bass.clear();
    sim_run(0.3f);
}
