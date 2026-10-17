// LEAD's checks (run by test_inst.c's inst_tests with the whole band): every sound in tune and under full scale,
// HUM follows a voice to a note of the scale (and with SNAP 0 to the pitch sung), TILT plays the tilted note on a
// knock, ARP plays the chord's tones on the song's grid and keeps playing when another layer is live, the lean
// widens and thins it, the loop records and replays (and follows the chord when it changes), tap REC mutes, MIDI
// on the lead's channel, no clicks, the cost. Screens and GIFs of ARP, HUM and the loop.
#include "sim.h"
#include "band.h"
#include "params.h"
#include <math.h>
#include <string.h>
#include <time.h>

bool sim_menu_param(int p);
void sim_menu_close(void);

enum { M_HUM, M_TILT, M_ARP };
enum { PT_UP, PT_DOWN, PT_UPDOWN, PT_RANDOM, PT_OCTAVES, PT_RIFF1, PT_RIFF2 };
enum { S_SAW, S_SQUARE, S_SYNC, S_SUPER, S_BELL, S_CHIP, S_WHISTLE, S_BRASS, S_N };
static const char *const snd_name[S_N] = { "SAW", "SQUARE", "SYNC", "SUPERSAW", "FM BELL", "CHIP", "WHISTLE", "BRASS" };

static int tagp(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tagp(t); if (p >= 0) param_set(p, v); }
static void make_live(int l) { for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); sim_run(0.1f); }
static uint8_t in_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1) & 15); }
static uint8_t lead_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1 + 3) & 15); }
static void key_on(int n, int v) { front_midi((uint8_t)(0x90 | in_ch()), (uint8_t)n, (uint8_t)v); }
static void key_off(int n) { front_midi((uint8_t)(0x80 | in_ch()), (uint8_t)n, 0); }
static void clear_all(void) { int p = tagp("xcla"); if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); } }
static float cents(float f, float want) { return f > 0 ? 1200.0f * log2f(f / want) : 9999.0f; }
static float hz(int m) { return 440.0f * powf(2.0f, ((float)m - 69.0f) / 12.0f); }

// the lead's note-ons in the MIDI log between two times
static int notes_in(float t0, float t1, float *ts, int *ns, int max)
{
    int k = 0;
    for (int i = 0; i < midi_n && k < max; i++) {
        const midi_ev_t *e = &midi_log[i];
        if (e->st == (0x90 | lead_ch()) && e->d2 > 0 && e->t >= t0 && e->t < t1) { if (ts) ts[k] = e->t; if (ns) ns[k] = e->d1; k++; }
    }
    return k;
}
static bool off_in(float t0, float t1, int note)
{
    for (int i = 0; i < midi_n; i++) {
        const midi_ev_t *e = &midi_log[i];
        bool off = e->st == (0x80 | lead_ch()) || (e->st == (0x90 | lead_ch()) && e->d2 == 0);
        if (off && e->d1 == note && e->t >= t0 && e->t < t1) return true;
    }
    return false;
}
static bool pc_in_chord(int n) { for (int k = 0; k < song.chord_n; k++) if (((n - song.chord[k]) % 12 + 12) % 12 == 0) return true; return false; }

// a knock: a short burst of noise, loud and dull
static float knock_t[8]; static int nknock;
static float knock_mic(float t, void *u)
{
    (void)u;
    float x = 0.0005f * sim_noise();
    for (int i = 0; i < nknock; i++) { float d = t - knock_t[i]; if (d >= 0 && d < 0.05f) x += 0.6f * sim_noise() * expf(-d / 0.008f); }
    return x;
}
// a sung voice: a few harmonics and a little vibrato
static float hum_hz = 220.0f;
static float hum_mic(float t, void *u)
{
    (void)u;
    float ph = hum_hz * t + 0.6f * sinf(6.2831853f * 5.0f * t) / 5.0f * 0.01f * hum_hz / 6.2831853f;
    float x = 0;
    for (int h = 1; h <= 5; h++) x += sinf(6.2831853f * (float)h * ph) / (float)h;
    return 0.09f * x + 0.002f * sim_noise();
}

void excite_lead(void) { key_on(57, 100); }       // a held note (MIDI): a steady sound for the click check

static void sounds(void)
{
    set("lmod", M_TILT); set("lvib", 0); set("loct", 0); SI.mic = NULL;
    int bad = 0, loud = 0, quiet = 0;
    float worst = 0;
    char line[200] = "", lvl[200] = "";
    for (int s = 0; s < S_N; s++) {
        set("lsnd", (float)s); sim_run(0.15f);
        key_on(57, 100);                                       // A3: 220 Hz
        float t0 = sim_t;
        sim_run(0.6f);
        float f = m_pitch_at(t0 + 0.15f, t0 + 0.45f, 80.0f, 900.0f);
        float pk = m_peak(t0, t0 + 0.6f), rms = m_rms(t0 + 0.1f, t0 + 0.5f);
        key_off(57); sim_run(0.5f);
        if (rms < 0.01f) quiet++;
        if (pk > 0.9f) loud++;
        char b[40];
        if (s != S_BELL) {                                     // the bell's partials are not harmonic: its pitch is the ear's
            float c = cents(f, 220.0f);
            if (fabsf(c) > fabsf(worst)) worst = c;
            if (fabsf(c) > 15.0f) bad++;
            snprintf(b, sizeof b, " %s %+.0f", snd_name[s], (double)c); strncat(line, b, sizeof line - strlen(line) - 1);
        }
        snprintf(b, sizeof b, " %s %.2f", snd_name[s], (double)pk); strncat(lvl, b, sizeof lvl - strlen(lvl) - 1);
    }
    note("cents off at A3:%s", line);
    note("peaks after the master:%s", lvl);
    check(bad == 0, "every sound plays A3 in tune (worst %+.1f cents)", (double)worst);
    check(quiet == 0 && loud == 0, "each sounds and stays under full scale (%d silent, %d over 0.9)", quiet, loud);
    set("lvib", param(tagp("lvib"))->def); set("lsnd", S_SAW);
}

static void hum_mode(void)
{
    set("lmod", M_HUM); set("lsnd", S_SAW); set("loct", 0); set("lsnp", 100);
    sim_run(0.3f);
    hum_hz = 220.0f; SI.mic = hum_mic;                         // A3
    float t0 = sim_t;
    sim_gif_begin("lead-hum");
    sim_run(1.0f);
    int ns[16]; float ts[16];
    int n = notes_in(t0, sim_t, ts, ns, 16);
    check(n >= 1 && ns[0] == 57 && n <= 2, "HUM: a sung A3 plays A3 (%d note%s, first %d)", n, n == 1 ? "" : "s", n ? ns[0] : -1);
    check(n && ts[0] - t0 < 0.15f, "within %.0f ms of starting to sing", (double)((n ? ts[0] - t0 : 9.0f) * 1000.0f));
    float f = m_pitch_at(sim_t - 0.4f, sim_t - 0.05f, 80.0f, 900.0f);
    check(fabsf(cents(f, 220.0f)) < 15.0f, "and sounds at 220 Hz (%.1f)", (double)f);
    hum_hz = 233.08f;                                          // A#3: not in C major, it stays on A or goes to B
    sim_run(0.6f);
    sim_shot("lead_hum");
    f = m_pitch_at(sim_t - 0.3f, sim_t - 0.02f, 80.0f, 900.0f);
    check(fabsf(cents(f, 220.0f)) < 15.0f || fabsf(cents(f, 246.94f)) < 15.0f, "a sung A#3 sounds as A3 or B3, notes of the scale (%.1f Hz)", (double)f);
    set("lsnp", 0); sim_run(0.5f);                             // SNAP 0: it follows the pitch sung
    f = m_pitch_at(sim_t - 0.3f, sim_t - 0.02f, 80.0f, 900.0f);
    check(fabsf(cents(f, 233.08f)) < 20.0f, "SNAP 0: it sounds at what is sung, 233 Hz (%.1f)", (double)f);
    hum_hz = 392.0f;                                           // G4 is in the lead's range: it stays there
    float t2 = sim_t; sim_run(0.5f);
    sim_gif_end();
    n = notes_in(t2, sim_t, ts, ns, 16);
    check(n >= 1 && ns[n - 1] == 67, "a sung G4 plays G4 (%d)", n ? ns[n - 1] : -1);
    int last = n ? ns[n - 1] : 0;
    SI.mic = NULL; set("lsnp", 100);
    float t3 = sim_t; sim_run(0.4f);
    check(off_in(t3, sim_t, last), "stop singing: the note stops");
}

static void tilt_mode(void)
{
    set("lmod", M_TILT); set("lsnd", S_SQUARE); set("loct", 0);
    host_chord_override(0);
    sim_tilt(0, 0); sim_run(0.5f);
    nknock = 0; SI.mic = knock_mic;
    int exp0 = scale_note(song.degree, 4);                     // flat: the chord's root
    knock_t[nknock++] = sim_t + 0.1f;
    float t0 = sim_t; sim_run(0.5f);
    int ns[8]; float ts[8];
    int n = notes_in(t0, sim_t, ts, ns, 8);
    check(n == 1 && ns[0] == exp0, "TILT, flat: a knock plays the chord's root (%d note%s, %d, wanted %d)", n, n == 1 ? "" : "s", n ? ns[0] : -1, exp0);
    float f = m_pitch_at(knock_t[0] + 0.04f, knock_t[0] + 0.25f, 80.0f, 900.0f);
    check(fabsf(cents(f, hz(exp0))) < 25.0f, "and it sounds at that pitch (%.1f Hz, wanted %.1f)", (double)f, (double)hz(exp0));
    sim_tilt(0, 14); sim_run(0.5f);                            // two rungs up the scale
    int exp2 = scale_note(song.degree + 2, 4);
    knock_t[nknock++] = sim_t + 0.1f;
    t0 = sim_t; sim_run(0.5f);
    sim_shot("lead_tilt");
    n = notes_in(t0, sim_t, ts, ns, 8);
    check(n == 1 && ns[0] == exp2, "tipped 14 degrees: two notes up the scale (%d, wanted %d)", n ? ns[0] : -1, exp2);
    float lag = n ? ts[0] - knock_t[1] : 1.0f;
    check(lag >= 0 && lag < 0.012f, "the note goes out within a tick of the knock (%.1f ms)", (double)(lag * 1000.0f));
    SI.mic = NULL; nknock = 0; sim_tilt(0, 0); sim_run(0.5f);
    host_chord_override(-1);
}

static void arp_mode(void)
{
    set("lmod", M_ARP); set("larp", PT_UP); set("lrat", 0); set("lrng", 1); set("lsnd", S_SAW);
    host_chord_override(0);
    sim_tilt(0, 0);
    sim_gif_begin("lead-arp");
    sim_run(1.3f);                                             // a second live: the player joins
    check(layer_lead.playing && layer_lead.playing(), "ARP: after a second live it plays by itself");
    float t0 = sim_t;
    sim_run(2.0f);
    sim_shot("lead_arp");
    float ts[64]; int ns[64];
    int n = notes_in(t0, sim_t, ts, ns, 64);
    float eighths = 2.0f * 2.0f * song.bpm / 60.0f;
    int off_grid = 0, wrong = 0, up = 0;
    for (int i = 0; i < n; i++) {
        float ph = song_beat((uint32_t)((ts[i] - 0.005f) * AUDIO_SR)) * 2.0f, fr = ph - floorf(ph);   // MIDI leaves at the tick after
        if (fr > 0.08f && fr < 0.92f) off_grid++;
        if (!pc_in_chord(ns[i])) wrong++;
        if (i && ns[i] > ns[i - 1]) up++;
    }
    check(fabsf((float)n - eighths) <= 1.5f, "1/8: %d notes in 2 s at %.0f BPM (%.0f expected)", n, (double)song.bpm, (double)eighths);
    check(off_grid == 0 && wrong == 0, "every one on an eighth of the song's beat and a tone of the chord (%d off the grid, %d wrong)", off_grid, wrong);
    check(n > 4 && up >= (n - 1) * 2 / 3, "UP climbs (%d of %d steps up)", up, n - 1);
    // the lean: the bottom edge down widens it, sideways left thins it
    sim_tilt(0, 30); sim_run(0.6f);
    t0 = sim_t; sim_run(3.2f);
    n = notes_in(t0, sim_t, ts, ns, 64);
    int lo = 127, hi = 0; for (int i = 0; i < n; i++) { if (ns[i] < lo) lo = ns[i]; if (ns[i] > hi) hi = ns[i]; }
    check(n && hi - lo >= 24, "tipped 30 degrees: it climbs over %d semitones (two octaves or more)", n ? hi - lo : 0);
    sim_tilt(0, 0); sim_run(0.5f);
    t0 = sim_t; sim_run(2.0f); int n_flat = notes_in(t0, sim_t, NULL, NULL, 64);
    sim_tilt(-30, 0); sim_run(0.3f);
    t0 = sim_t; sim_run(2.0f); int n_thin = notes_in(t0, sim_t, NULL, NULL, 64);
    sim_tilt(30, 0); sim_run(0.3f);
    t0 = sim_t; sim_run(2.0f); int n_fill = notes_in(t0, sim_t, NULL, NULL, 64);
    sim_shot("lead_arp_fill");
    sim_tilt(0, 0); sim_run(0.3f);
    sim_gif_end();
    check(n_thin < n_flat && n_fill > n_flat, "leaning left thins it (%d notes), right fills it (%d), flat %d", n_thin, n_fill, n_flat);
    // another layer live: it keeps playing
    make_live(L_DRUMS);
    t0 = sim_t; sim_run(1.5f);
    n = notes_in(t0, sim_t, NULL, NULL, 64);
    check(n >= 4 && layer_lead.playing(), "DRUMS live: the lead keeps arpeggiating (%d notes in 1.5 s)", n);
    make_live(L_LEAD);
    host_chord_override(-1);
}

static void loop_rec(void)
{
    set("lmod", M_ARP); set("larp", PT_UPDOWN); set("lrat", 1); set("lsnd", S_SAW);
    host_chord_override(0);
    sim_tilt(0, 0);
    bool had_loop = song.loop_len != 0;
    sim_run(1.2f);
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    check(song.loop_len > 0 && layer_lead.state && layer_lead.state() == LOOP_ON,
          "hold REC 2.4 s: a loop (%s, %d beats at %.1f BPM)", had_loop ? "added to the song's" : "the song's first", song.beats, (double)song.bpm);
    set("lmod", M_TILT);                                       // the player stops: what plays now is the loop
    sim_run(0.3f);
    float L = (float)song.loop_len / (float)AUDIO_SR, tA = sim_t;
    sim_gif_begin("lead-loop");
    sim_run(2.0f * L + 0.05f);
    sim_shot("lead_loop");
    float ta[96], tb[96]; int na[96], nb[96];
    int n1 = notes_in(tA, tA + L, ta, na, 96), n2 = notes_in(tA + L, tA + 2.0f * L, tb, nb, 96);
    int same = 0;
    for (int i = 0; i < n1 && i < n2; i++) if (na[i] == nb[i] && fabsf((tb[i] - L) - ta[i]) < 0.012f) same++;
    check(n1 >= 8 && n1 == n2 && same == n1, "it replays: %d notes a pass, the same notes at the same times in the next (%d)", n1, same);
    int off = 0;
    for (int i = 0; i < n1; i++) {
        float ph = song_beat((uint32_t)((ta[i] - 0.005f) * AUDIO_SR)) * 4.0f, fr = ph - floorf(ph);
        if (fr > 0.08f && fr < 0.92f) off++;
    }
    check(off == 0, "every loop note on a sixteenth of the song (%d off)", off);
    // the chord changes: the line moves with it, in key
    host_chord_override(3);                                    // IV
    sim_run(0.3f);
    float tc = sim_t; sim_run(L);
    sim_gif_end();
    int nc = notes_in(tc, sim_t, ta, na, 96), in_iv = 0, moved = 0;
    for (int i = 0; i < nc; i++) { if (pc_in_chord(na[i])) in_iv++; }
    for (int i = 0; i < nc && i < n1; i++) if (na[i] != nb[i]) moved++;
    check(nc == n1 && in_iv == nc && moved > 0, "the chord goes to IV: the line follows it (%d of %d notes on IV's tones, %d moved)", in_iv, nc, moved);
    host_chord_override(0); sim_run(0.3f);
    // tap REC mutes, again plays
    sim_rec(0.1f); sim_run(0.1f);
    float tm = sim_t; sim_run(L);
    check(layer_lead.state() == LOOP_OFF && notes_in(tm, sim_t, NULL, NULL, 96) == 0, "tap REC: muted, nothing plays");
    sim_rec(0.1f); sim_run(0.1f);
    tm = sim_t; sim_run(L);
    check(layer_lead.state() == LOOP_ON && notes_in(tm, sim_t, NULL, NULL, 96) > 0, "tap again: it plays");
    // muted, then REC held: a new take in place of the old (here one with no notes: the loop is empty after it)
    sim_rec(0.1f); sim_run(0.1f);
    SI.rec = true; sim_run(0.8f); SI.rec = false; sim_tick();
    tm = sim_t; sim_run(L);
    check(layer_lead.state() == LOOP_NONE && notes_in(tm, sim_t, NULL, NULL, 96) == 0, "muted, then REC held: a new take replaces the loop (state %d)", layer_lead.state());
    // MIDI: on the lead's channel (the base + 3), nothing on the base channel from the lead
    int other = 0;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].t >= tA && (midi_log[i].st == (0x90 | in_ch())) && midi_log[i].d2 > 0) other++;
    check(notes_in(tA, sim_t, NULL, NULL, 96) > 0 && other == 0, "its notes go out on MIDI channel base + 3 (%d on the base channel)", other);
    layer_lead.clear();
    check(layer_lead.state() == LOOP_NONE, "clear: the loop is gone");
    host_chord_override(-1);
}

static void clicks(void)
{
    set("lmod", M_TILT); SI.mic = NULL;
    static const int snds[] = { S_SAW, S_CHIP, S_SYNC };
    static const char *const tags[] = { "lflt", "lres", "lenv", "latk", "ldec", "lgld", "lvib", "ldrv", "lfx ", "lsnd", "ltim", "loct", "lmod", NULL };
    float worst = 0; const char *wt = ""; int ws = 0;
    for (int k = 0; k < 3; k++) {
        set("lsnd", (float)snds[k]);
        key_on(57, 100); sim_run(0.6f);
        for (int j = 0; tags[j]; j++) {
            int p = tagp(tags[j]); if (p < 0) continue;
            if (!strcmp(tags[j], "lsnd") && k) continue;
            const param_t *d = param(p);
            float keep = pv[p];
            for (int dir = 0; dir < 2; dir++) {
                param_set(p, dir ? d->min : d->max); sim_run(0.35f);
                param_set(p, dir ? d->max : d->min);
                float tc = sim_t; sim_run(0.2f);
                float r = m_click(tc, NULL, NULL);
                if (r > worst) { worst = r; wt = d->name; ws = snds[k]; }
            }
            param_set(p, keep); sim_run(0.2f);
            if (!strcmp(tags[j], "lmod")) set("lmod", M_TILT);
        }
        key_off(57); sim_run(0.3f);
    }
    set("lsnd", S_SAW);
    check(worst <= 3.0f, "no click when a param jumps end to end under a held note (worst %.1fx, %s on %s)", (double)worst, wt, snd_name[ws]);
}

static void cost(void)
{
    float in[AUDIO_BLOCK] = { 0 }, d[AUDIO_BLOCK], s[AUDIO_BLOCK];
    double worst = 0; int wsnd = 0;
    static const int snds[] = { S_SUPER, S_SYNC, S_BRASS, S_SAW, S_WHISTLE };
    set("lmod", M_TILT);
    for (int k = 0; k < 5; k++) {
        set("lsnd", (float)snds[k]); set("lvib", 60);
        key_on(57, 120); sim_run(0.5f);
        double us = 1e9;                                       // the best of five runs: the code's cost, not the machine's load
        for (int r = 0; r < 5; r++) {
            struct timespec a, b;
            clock_gettime(CLOCK_MONOTONIC, &a);
            for (int i = 0; i < 1000; i++) { memset(d, 0, sizeof d); memset(s, 0, sizeof s); layer_lead.audio(in, d, s, AUDIO_BLOCK, true); }
            clock_gettime(CLOCK_MONOTONIC, &b);
            double u = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3) / 1000.0;
            if (u < us) us = u;
        }
        if (us > worst) { worst = us; wsnd = snds[k]; }
        key_off(57); sim_run(0.3f);
    }
    set("lvib", param(tagp("lvib"))->def); set("lsnd", S_SAW);
    metric("lead_us_per_block", worst);
    check(worst <= 3.0, "the lead takes %.2f us per 64-frame block at its busiest (%s; budget 3)", worst, snd_name[wsnd]);
}

void test_layer_lead(void)
{
    midi_n = 0;                                                 // a long run fills the log: start it again
    bool joy = SI.joy, imu = SI.imu; mic_fn mic = SI.mic; void *mu = SI.mic_u;
    int prg = tagp("sprg"); float prg_keep = prg >= 0 ? pv[prg] : 0;
    if (prg >= 0) param_set(prg, 0);                            // the song holds its chord: only the tests move it
    clear_all();
    make_live(L_LEAD);
    check(host_live() == L_LEAD, "the knob makes LEAD the live layer");
    sim_run(0.3f);
    sim_shot("lead_arrive");
    sounds();
    hum_mode();
    tilt_mode();
    arp_mode();
    clear_all();
    loop_rec();
    clicks();
    cost();
    // leave it as it was: another layer live, defaults, the chord back with the song, no loop
    make_live(L_DRUMS);
    for (int i = 0; i < layer_lead.nparams; i++) param_set(layer_base[L_LEAD] + i, layer_lead.params[i].def);
    host_chord_override(-1);
    if (prg >= 0) param_set(prg, prg_keep);
    clear_all();
    layer_lead.clear();
    SI.joy = joy; SI.imu = imu; SI.mic = mic; SI.mic_u = mu; sim_tilt(0, 0);
    sim_run(0.3f);
}
