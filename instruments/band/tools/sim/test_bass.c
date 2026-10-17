// BASS's checks (run by test_inst.c's inst_tests with the whole jam): the pitch of every sound, KNOCK plays the
// tilted note on a knock, HUM follows a voice two octaves down, FOLLOW keeps the grid, LOCK KICK plays on the
// kicks, leaning changes the song's chord, the loop records and replays on the grid, tap REC mutes, muted a hold
// is a new take (REC's click left out), the LINE (ready lines, its notes on the sixteenths and on the chord, two
// bars at 1/8, under other layers, kept in a sound) and its editor (open, move, drag a note, tap, REC, close), no
// clicks, the cost. Screens and GIFs of FOLLOW changing chords and of writing a line.
#include "sim.h"
#include "band.h"
#include "params.h"
#include "oled.h"
#include <math.h>
#include <string.h>
#include <time.h>

bool sim_menu_param(int p);
void sim_menu_close(void);

enum { M_KNOCK, M_HUM, M_FOLLOW, M_LINE };
enum { PT_ROOT, PT_FIFTH, PT_OCT, PT_WALK, PT_ARP, PT_ACID };
enum { S_SUB, S_ACID, S_808, S_FINGER, S_REESE, S_FM, S_N };
static const char *const snd_name[S_N] = { "SUB", "ACID", "808", "FINGER", "REESE", "FM" };

bool bass_editing(void);
int  bass_cursor(void);
int  bass_cursor_pitch(void);
void bass_line_words(int line, uint32_t w[5]);

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
        float ph = song_beat((uint32_t)((ts[i] - 0.005f) * AUDIO_SR)) * 2.0f, fr = ph - floorf(ph);   // MIDI leaves at the tick after the note; beat 0 is the session's
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
        int s = (int)lroundf(song_beat((uint32_t)((ts[i] - 0.005f) * AUDIO_SR)) * 4.0f) % 16;
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

// ---------------------------------------------------------------- muted, a hold is a new take
static void new_take(void)
{
    set("bmod", M_FOLLOW); set("bpat", PT_OCT); set("brat", 1); set("bkik", 0); set("bfil", 0); set("bsnd", S_FINGER);
    host_chord_override(0); sim_tilt(0, 0); SI.mic = NULL;
    sim_run(1.2f);
    if (!song.loop_len) { SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick(); }   // (the song's loop, if the tests before left none)
    uint32_t len0 = song.loop_len;
    float L = (float)song.loop_len / (float)AUDIO_SR;
    SI.rec = true; sim_run(L + 0.1f); SI.rec = false; sim_tick();   // a take of FOLLOW's octaves
    set("bmod", M_KNOCK); sim_run(0.3f);
    int ns[64]; float ts[64];
    float t0 = sim_t; sim_run(L);
    int n = notes_in(t0, sim_t, ts, ns, 64), oct = 0;
    for (int i = 0; i < n; i++) oct += ns[i] == song.chord[0] + 12;
    check(n >= 4 && oct > 0, "a loop of octaves to start with (%d notes, %d octaves up)", n, oct);
    sim_rec(0.1f); sim_run(0.1f);
    check(layer_bass.state() == LOOP_OFF, "tap REC: stopped");
    // held while stopped: the old loop goes; knocks played two notes up the scale make the new one. The first
    // knock comes with REC's own click (20 ms after it goes down) and is not recorded
    sim_tilt(0, 14); sim_run(0.5f);
    int third = scale_note(song.degree + 2, 2);
    nknock = 0; SI.mic = knock_mic;
    float tp = sim_t + 0.005f;
    knock_t[nknock++] = tp + 0.02f; knock_t[nknock++] = tp + 0.45f; knock_t[nknock++] = tp + 0.85f;
    SI.rec = true; sim_run(0.35f);
    bool heard = layer_bass.state() == LOOP_REC;
    sim_run(0.85f); SI.rec = false; sim_tick();
    int played = notes_in(tp, tp + 1.2f, NULL, NULL, 64);
    SI.mic = NULL; nknock = 0; sim_tilt(0, 0); sim_run(0.3f);
    t0 = sim_t; sim_run(L);
    n = notes_in(t0, sim_t, ts, ns, 64);
    int other = 0;
    for (int i = 0; i < n; i++) other += ns[i] != third;
    check(heard && played == 3 && n == 2 && other == 0, "held while stopped: a new take of the knocked notes only (%d of the %d played: REC's click is left out; %d not the third)", n, played, other);
    check(song.loop_len == len0 && layer_bass.state() == LOOP_ON, "the loop keeps its length (%.2f s) and plays", (double)L);
    layer_bass.clear();
}

// ---------------------------------------------------------------- LINE
static uint32_t lword(int k) { char t[5] = "bln0"; t[3] = (char)('0' + k); int p = tagp(t); return p >= 0 ? (uint32_t)pv[p] : 0; }
static uint32_t lget(int i)                                 // step i of the line in the params: 7 bits
{
    int b = 7 * i, k = b / 24, o = b % 24;
    uint32_t v = lword(k) >> o;
    if (o > 17) v |= lword(k + 1) << (24 - o);
    return v & 0x7Fu;
}
static bool lon(uint32_t c) { return (c & 15u) >= 1 && (c & 15u) <= 13; }
static int  lpitch(uint32_t c) { return (int)(c & 15u) - 4; }
static int  root_of(int d) { int r = scale_note(d, 2); return 36 + ((r % 12) + 12) % 12; }
static int  line_note_of(int d, int s) { return root_of(d) + scale_note(d + s, 2) - scale_note(d, 2); }

// to the next bar line; returns its time
static float to_bar(void)
{
    float b = song_beat(audio_frames()), rem = (4.0f - fmodf(b, 4.0f)) * 60.0f / song.bpm;
    float tb = sim_t + rem;
    sim_run(rem + 0.02f);
    return tb;
}
// the bass's notes from t0 on, each with its sixteenth (0..15 of the bar) and how far off it (in sixteenths)
static int notes_16(float t0, float t1, int *ns, int *st, float *off, int *vel, int max)
{
    int k = 0;
    for (int i = 0; i < midi_n && k < max; i++) {
        const midi_ev_t *e = &midi_log[i];
        if (e->st != (0x90 | bass_ch()) || e->d2 == 0 || e->t < t0 || e->t >= t1) continue;
        float ph = song_beat((uint32_t)((e->t - 0.005f) * AUDIO_SR)) * 4.0f, r = floorf(ph + 0.5f);
        ns[k] = e->d1; st[k] = ((int)r % 16 + 16) % 16; off[k] = ph - r; if (vel) vel[k] = e->d2; k++;
    }
    return k;
}

// WALKING at 1/8: two bars of quarters, up the chord and down again (with a bar's loop the line runs on from
// pass to pass)
static void walking(void)
{
    set("bmod", M_LINE); set("blin", 3); host_chord_override(0);
    check((int)pv[tagp("blst")] == 1, "WALKING is two bars of eighths (LINE STEP 1/8)");
    float tb = to_bar(), bar = 240.0f / song.bpm;
    sim_run(2.0f * bar);
    int ns[64], st[64]; float off[64];
    int n = notes_16(tb - 0.01f, tb + 2.0f * bar - 0.01f, ns, st, off, NULL, 64);
    static const int up[8] = { 0, 2, 4, 5, 7, 5, 4, 2 };
    int ph0 = n ? (ns[0] == line_note_of(0, 0) ? 0 : 4) : 0, okw = 0, onq = 0;
    for (int i = 0; i < n && i < 8; i++) { okw += ns[i] == line_note_of(0, up[(ph0 + i) % 8]); onq += st[i] % 4 == 0; }
    check(n == 8 && okw == 8 && onq == 8, "WALKING %s: %d notes in two bars, on the beats (%d), up the chord and down (%d right)",
          song.loop_len ? "with a bar's loop" : "without a loop", n, onq, okw);
}

static void line_play(void)
{
    // the default line is ROOT EIGHTHS, packed in the params' defaults
    uint32_t w[5]; bass_line_words(1, w);
    int dflt = 0;
    for (int k = 0; k < 5; k++) { char t[5] = "bln0"; t[3] = (char)('0' + k); int p = tagp(t); if (p >= 0 && (uint32_t)param(p)->def == w[k]) dflt++; }
    check(dflt == 5 && param(tagp("blin"))->def == 1.0f, "the line's defaults are ROOT EIGHTHS (%d of 5 words)", dflt);
    // every ready line loads its steps
    int loads = 0;
    for (int l = 1; l < 8; l++) {
        set("blin", (float)l); bass_line_words(l, w);
        bool ok = true; for (int k = 0; k < 5; k++) ok &= lword(k) == w[k];
        int on = 0; for (int i = 0; i < 16; i++) on += lon(lget(i));
        loads += ok && on >= 4;
    }
    check(loads == 7, "each of the 7 ready lines loads its notes (%d)", loads);

    // OCTAVE POP at 1/16: the root on the beats, the octave (accented) on the eighths between, 8 a bar
    set("bmod", M_LINE); set("blin", 2); set("bsnd", S_FINGER); set("boct", 0);
    host_chord_override(0); sim_tilt(0, 0); SI.mic = NULL;
    sim_run(0.5f);
    check((int)pv[tagp("blst")] == 0, "OCTAVE POP is a bar of sixteenths");
    float tb = to_bar(), bar = 240.0f / song.bpm;
    sim_run(2.0f * bar);
    int ns[64], st[64], vel[64]; float off[64];
    int n = notes_16(tb - 0.01f, tb + 2.0f * bar - 0.01f, ns, st, off, vel, 64), wrong = 0, late = 0, loud = 0;
    int r0 = root_of(0);
    for (int i = 0; i < n; i++) {
        int want = st[i] % 4 == 0 ? r0 : st[i] % 4 == 2 ? r0 + 12 : -1;
        if (ns[i] != want) wrong++;
        if (fabsf(off[i]) > 0.12f) late++;
        if (st[i] % 4 == 2 && vel[i] >= 120) loud++;
    }
    check(n == 16 && wrong == 0 && late == 0, "LINE, OCTAVE POP: %d notes in two bars (16), each on its sixteenth (%d off) and the right note (%d wrong)", n, late, wrong);
    check(loud == 8, "the accented steps play louder (%d of 8)", loud);
    sim_shot("bass_line");

    // it follows the chord: on the V the same line is the V's root and octave
    host_chord_override(4);
    tb = to_bar(); sim_run(bar);
    n = notes_16(tb - 0.01f, tb + bar - 0.01f, ns, st, off, NULL, 64);
    int r5 = root_of(4), on5 = 0;
    for (int i = 0; i < n; i++) on5 += ns[i] == (st[i] % 4 == 0 ? r5 : r5 + 12);
    check(n == 8 && on5 == 8, "on the V chord it plays the V's root and octave (%d of %d, root %d)", on5, n, r5);
    host_chord_override(0);

    walking();

    // under another layer it plays on, once it has played a second here
    set("blin", 1); sim_run(1.2f);
    check(layer_bass.playing && layer_bass.playing(), "the line plays by itself: playing() is true");
    make_live(L_LEAD);
    tb = to_bar(); sim_run(bar);
    n = notes_16(tb - 0.01f, tb + bar - 0.01f, ns, st, off, NULL, 64);
    check(n == 8, "LEAD live: the line plays on (%d notes a bar, 8 written)", n);
    make_live(L_BASS);

    // the line is part of the sound (SAVE AS, flash)
    {
        static uint8_t buf[PARAMS_MAX * 8];
        set("blin", 5); uint32_t fk[5]; for (int k = 0; k < 5; k++) fk[k] = lword(k);
        int nb = params_pack(buf, sizeof buf, true);
        set("blin", 1);
        params_unpack(buf, nb, true);
        bool same = true; for (int k = 0; k < 5; k++) same &= lword(k) == fk[k];
        check(same && (int)pv[tagp("blin")] == 5, "the line is kept with the sound (packed and unpacked)");
        layer_bass.changed(-1);                                 // (what the host does after loading a sound)
    }
}

static void line_editor(void)
{
    set("bmod", M_FOLLOW); set("blin", 1); set("blst", 0);
    sim_run(0.3f);
    int pe = tagp("bled");
    if (pe >= 0 && sim_menu_param(pe)) { sim_tap(); sim_run(0.1f); }
    check(bass_editing() && front_menu_level() == 0, "EDIT LINE: the menu closes and the editor is up");
    check((int)pv[tagp("bmod")] == M_LINE, "opening it makes the MODE LINE (you hear what you write)");
    sim_shot("bass_line_editor_open");
    int c0 = bass_cursor();
    sim_turn(1); sim_turn(1); sim_turn(1);
    check(bass_cursor() == (c0 + 3) % 16 && host_live() == L_BASS, "turning walks the steps (%d -> %d), the layer stays", c0, bass_cursor());
    while (bass_cursor() != 0) sim_turn(bass_cursor() < 8 ? -1 : 1);
    check(bass_cursor_pitch() == 0, "on a note the cursor is at its pitch (the root: %d)", bass_cursor_pitch());
    // press and turn on the empty step 1: it takes the note two up the scale (the third)
    sim_turn(1);
    SI.pressed = true; sim_run(0.05f);
    sim_turn(1); sim_run(0.02f); sim_turn(1);
    SI.pressed = false; sim_tick(); sim_run(0.05f);
    uint32_t c1 = lget(1);
    check(lon(c1) && lpitch(c1) == 2 && bass_cursor_pitch() == 2 && bass_editing(), "press and turn: the step takes the note two up the scale (%d), the editor stays", lon(c1) ? lpitch(c1) : -99);
    check((int)pv[tagp("blin")] == 0, "written on, the line is MINE");
    float tb = to_bar(), bar = 240.0f / song.bpm;
    sim_run(bar);
    int ns[64], st[64]; float off[64];
    int n = notes_16(tb - 0.01f, tb + bar - 0.01f, ns, st, off, NULL, 64), at1 = -1;
    for (int i = 0; i < n; i++) if (st[i] == 1) at1 = ns[i];
    check(at1 == line_note_of(song.degree, 2), "and it plays on the second sixteenth (%d, the third is %d)", at1, line_note_of(song.degree, 2));
    // tap on an empty step: a note at the cursor's pitch; again: held; again: off
    sim_turn(1); sim_turn(1);                                   // step 3 (the cursor took the root at step 2)
    int pc = bass_cursor_pitch();
    sim_tap(); sim_run(0.05f);
    uint32_t a = lget(3);
    sim_tap(); sim_run(0.05f);
    uint32_t b = lget(3);
    sim_tap(); sim_run(0.05f);
    uint32_t c = lget(3);
    check(lon(a) && lpitch(a) == pc && !(a & 16u) && lon(b) && (b & 16u) && !lon(c), "tap: a note at the cursor's pitch, again: held, again: off");
    // REC tapped: the bass stops, again: it plays
    SI.rec = true; sim_run(0.1f); SI.rec = false; sim_tick();
    tb = to_bar(); sim_run(bar);
    int quiet = notes_16(tb - 0.01f, tb + bar - 0.01f, ns, st, off, NULL, 64);
    SI.rec = true; sim_run(0.1f); SI.rec = false; sim_tick();
    tb = to_bar(); sim_run(bar);
    int again = notes_16(tb - 0.01f, tb + bar - 0.01f, ns, st, off, NULL, 64);
    check(quiet == 0 && again >= 8, "REC tapped in the editor stops the bass (%d notes), again starts it (%d)", quiet, again);
    // the hints fit the bottom line
    int hw = oled_ftext_w(F_TINY, "TURN: STEP  TAP: NOTE, LONG, OFF", 0), hw2 = oled_ftext_w(F_TINY, "PRESS + TURN: NOTE  HOLD: DONE", 0);
    check(hw <= 126 && hw2 <= 126, "the editor's hints fit the screen (%d, %d px)", hw, hw2);
    // REC held 1 s clears the line
    SI.rec = true; sim_run(1.1f); SI.rec = false; sim_tick(); sim_run(0.05f);
    int left = 0; for (int i = 0; i < 16; i++) left += lon(lget(i));
    check(left == 0 && bass_editing(), "REC held 1 s clears the line (%d notes left)", left);
    // hold: the editor goes, and the knob picks layers again
    sim_hold_knob(); sim_run(0.05f);
    check(!bass_editing(), "holding the knob leaves the editor");
    sim_turn(1); sim_run(0.1f);
    check(host_live() == L_BASS + 1, "after it a turn picks the next layer again (live %d)", host_live());
    make_live(L_BASS);
    if (layer_bass.hold) {
        bool used = layer_bass.hold(); sim_run(0.05f);
        check(used && bass_editing(), "layer.hold() opens the editor and says it used the hold");
        sim_hold_knob(); sim_run(0.05f);
        check(!bass_editing(), "and a hold closes it");
    }
    set("blin", 1);
}

// writing a line from nothing, for the GIF
static void line_gif(void)
{
    set("blin", 1); set("bsnd", S_FINGER); set("stmp", 96);
    sim_run(1.0f);
    sim_gif_begin("bass-line");
    sim_run(0.4f);
    if (layer_bass.hold) layer_bass.hold();
    sim_run(0.8f);
    SI.rec = true; sim_run(1.1f); SI.rec = false; sim_tick(); sim_run(0.3f);   // a clean sheet
    while (bass_cursor() != 0) { sim_turn(bass_cursor() < 8 ? -1 : 1); sim_run(0.02f); }
    static const struct { int step, pitch, taps; } w[] = {
        { 0, 0, 2 }, { 3, 0, 1 }, { 6, 4, 1 }, { 7, 7, 1 }, { 8, 0, 2 }, { 11, 2, 1 }, { 12, 4, 1 }, { 14, 5, 1 }, { 15, 6, 1 },
    };
    for (unsigned k = 0; k < sizeof w / sizeof w[0]; k++) {
        while (bass_cursor() != w[k].step) { sim_turn(1); sim_run(0.04f); }
        int d = w[k].pitch - bass_cursor_pitch();
        if (d) {                                                         // drag it there
            SI.pressed = true; sim_run(0.04f);
            for (int i = 0; i < (d > 0 ? d : -d); i++) { sim_turn(d > 0 ? 1 : -1); sim_run(0.06f); }
            SI.pressed = false; sim_tick(); sim_run(0.1f);
            for (int t = 1; t < w[k].taps; t++) { sim_tap(); sim_run(0.12f); }
        } else for (int t = 0; t < w[k].taps; t++) { sim_tap(); sim_run(0.12f); }
        sim_run(0.1f);
    }
    sim_shot("bass_line_written");
    sim_run(2.5f);
    sim_hold_knob();
    sim_run(1.5f);
    sim_gif_end();
    sim_shot("bass_line_lane");
    int on = 0; for (int i = 0; i < 16; i++) on += lon(lget(i));
    check(on == 9, "the GIF's line was written: %d notes", on);
    set("blin", 7); sim_run(0.3f);                                   // ACID SLIDE: accents and slides in the roll
    if (layer_bass.hold) layer_bass.hold();
    sim_run(1.6f); sim_shot("bass_line_acid");
    sim_hold_knob(); sim_run(0.2f);
    set("stmp", 100); set("blin", 1);
    sim_run(0.3f);
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
    static const int snds[] = { S_ACID, S_REESE, S_FINGER, S_FM, S_ACID };
    for (int k = 0; k < 5; k++) {
        set("bsnd", (float)snds[k]); set("bmod", k == 4 ? M_LINE : M_KNOCK); set("blin", 7);
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
    int cl = tagp("xcla");                                      // no loop left by the tests before: the free grid
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
    line_play();                                                // (no loop yet)
    loop_rec();
    new_take();
    walking();                                                  // (a bar's loop: the line's two bars run on over it)
    line_editor();                                              // (the song has a bar's loop now)
    line_gif();
    clicks();
    cost();
    // leave it as it was: another layer live, defaults, the chord back with the song, no loop
    make_live(L_DRUMS);
    for (int i = 0; i < layer_bass.nparams; i++) param_set(layer_base[L_BASS] + i, layer_bass.params[i].def);
    host_chord_override(-1);
    if (prg >= 0) param_set(prg, prg_keep);
    int p = tagp("xcla");
    if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); }
    layer_bass.clear();
    sim_run(0.3f);
}
