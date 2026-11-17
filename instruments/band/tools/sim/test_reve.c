// BAND on a rev E (the Vessel): no stick; the left knob is the live layer's fader, a hand held over the sensor is the
// live layer's (each layer puts it on one of its stick moves). No rev E has been built: this is how it is checked.
//
//   the stick board  first, on the board the sim boots as: the left knob and a hand do nothing there, not even with a
//                    sensor wired next to the stick
//   the left knob    BASS turned down under BREATH's track (each one's own tone measured in the mix: only BASS
//                    moves); the picked strip in the mixer; LEAD, KEYS and DRUMS alone, by their level before and
//                    after; the ends, a muted layer, a fast turn, the menu open; no click on any detent
//   the hand         for each of the six layers: away, at 20 cm, 12 cm and 4 cm, away again, measured in what that
//                    layer plays (the roll's hits, how bright, the pitch, the wobble, how soon it howls); a hand that
//                    arrives and leaves in one tick; a layer left with the hand still there
//   the song         a loop of each kind, the grid, a pretend board on the link: every layer's knob and hand, then
//                    the tempo, the key, the loops and the session are what they were
//   the editors      the grid and the bass line with the hand over them and the left knob turned
//   the screen       the header's two shows, the mixer's lines, the two hints
#include "sim.h"
#include "band.h"
#include "link.h"
#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void  front_midi(uint8_t st, uint8_t d1, uint8_t d2);
bool  sim_menu_param(int p);
void  sim_menu_close(void);
int   mixer_cursor(void);
bool  mixer_grabbed(void);
int   host_layer_state(int l);
float host_layer_gain(int l);
float host_hand(void);
bool  host_knob_shown(void);
void  mixer_first_visit(void);
bool  drums_editing(void);
int   drums_cursor(void);
bool  bass_editing(void);
int   bass_cursor(void);
void  excite_breath(void);
void  excite_howl(void);

static const char *const lname[L_N] = { "DRUMS", "BASS", "LEAD", "KEYS", "BREATH", "HOWL" };
static const char *const lvl_tag[L_N] = { "mlvd", "mlvb", "mlvl", "mlvk", "mlvh", "mlvw" };
static const char *const mute_tag[L_N] = { "mmud", "mmub", "mmul", "mmuk", "mmuh", "mmuw" };

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void make_live(int l) { sim_menu_close(); for (int i = 0; i < 14 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1); sim_run(0.1f); }
static void run_action(const char *t) { int p = tag(t); if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); } }
static uint8_t in_ch(void) { return (uint8_t)(((int)pv[CP(CP_MIDI_CH)] - 1) & 15); }
static void key_on(int n, int v) { front_midi((uint8_t)(0x90 | in_ch()), (uint8_t)n, (uint8_t)v); }
static void key_off(int n) { front_midi((uint8_t)(0x80 | in_ch()), (uint8_t)n, 0); }

static sim_in_t board_c;                                           // the board the sim boots as: a rev C with its stick
static void rev_c(void) { SI = board_c; SI.jx = SI.jy = 0; sim_run(0.3f); }
static void rev_e(void) { SI = board_c; sim_rev_e(); sim_run(0.3f); }
static void shot(const char *label) { coach_pause(); sim_run(0.04f); sim_shot(label); }   // no hint over the scene
static float turned[64]; static int nturned;                        // when each detent of the last lturn() landed
static void lturn(int n)                                           // n detents as a hand turns them: one every 30 ms
{
    nturned = 0;
    for (int i = 0; i < (n < 0 ? -n : n); i++) { if (nturned < 64) turned[nturned++] = sim_t; sim_lturn(n < 0 ? -1 : 1); sim_run(0.02f); }
}
static float hand_of(int mm) { return mm > 0 ? (250.0f - (float)mm) / 210.0f : 0.0f; }   // the core's value at that height

// ---------------------------------------------------------------- measures
// the worst jump in the first difference around each tick of a stretch, against the signal's own (over 3: a click)
static float worst_click(float t0, float t1)
{
    float w = 0;
    for (float t = t0; t < t1 + 0.005f; t += 0.01f) { float r = m_click(t, NULL, NULL); if (r > w) w = r; }
    return w;
}
static float knob_click(void) { float w = 0; for (int i = 0; i < nturned; i++) { float r = worst_click(turned[i], turned[i] + 0.03f); if (r > w) w = r; } return w; }
// how bright: the first difference's rms over the signal's
static float bright(float t0, float t1)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    double e = 0, d = 0;
    for (int i = a > 0 ? a : 1; i < b && i < sim_n; i++) { float x = sim_out[i], dx = x - sim_out[i - 1]; e += (double)x * x; d += (double)dx * dx; }
    return e > 0 ? (float)sqrt(d / e) : 0.0f;
}
// one tone's amplitude in the mix, and the strongest tone between two frequencies
static float amp_at(float t0, float t1, float hz) { return sqrtf(m_goertzel(t0, t1, hz)); }
static float peak_hz(float t0, float t1, float lo, float hi, float step)
{
    float best = 0, bf = lo;
    for (float f = lo; f <= hi; f += step) { float p = m_goertzel(t0, t1, f); if (p > best) { best = p; bf = f; } }
    return bf;
}
// the pitch followed in 40 ms windows every 10 ms: its middle (MIDI) and how far it swings either side (cents)
static void pitch_swing(float t0, float t1, float fmin, float fmax, float *mid, float *swing)
{
    float lo = 1e9f, hi = -1e9f;
    for (float t = t0; t + 0.04f <= t1; t += 0.01f) {
        float f = m_pitch_at(t, t + 0.04f, fmin, fmax);
        if (f <= 0) continue;
        float m = hz_to_midi(f);
        if (m < lo) lo = m;
        if (m > hi) hi = m;
    }
    if (mid) *mid = hi >= lo ? 0.5f * (lo + hi) : 0.0f;
    if (swing) *swing = hi >= lo ? 50.0f * (hi - lo) : 0.0f;
}
// how much the level moves: 20 ms levels every 10 ms, (max - min) / (max + min)
static float wobble(float t0, float t1)
{
    float lo = 1e9f, hi = 0;
    for (float t = t0; t + 0.02f <= t1; t += 0.01f) { float r = m_rms(t, t + 0.02f); if (r < lo) lo = r; if (r > hi) hi = r; }
    return hi > 0 ? (hi - lo) / (hi + lo) : 0.0f;
}
static int notes_on(uint8_t st, float t0, float t1)
{
    int n = 0;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].st == st && midi_log[i].d2 > 0 && midi_log[i].t >= t0 && midi_log[i].t < t1) n++;
    return n;
}
#define DRUM_ON 0x99                                               // DRUMS: note-ons on channel 10

// every layer's level setting and its fader in the mix, apart from one: are they what they were?
typedef struct { float lvl[L_N], mute[L_N], gain[L_N]; } mix_t;
static mix_t mix_now(void) { mix_t m; for (int l = 0; l < L_N; l++) { m.lvl[l] = get(lvl_tag[l]); m.mute[l] = get(mute_tag[l]); m.gain[l] = host_layer_gain(l); } return m; }
static int others_moved(const mix_t *a, const mix_t *b, int but)
{
    int n = 0;
    for (int l = 0; l < L_N; l++) if (l != but && (a->lvl[l] != b->lvl[l] || a->mute[l] != b->mute[l] || fabsf(a->gain[l] - b->gain[l]) > 1e-4f)) n++;
    return n;
}

// a plain band: nothing plays by itself, no echo or reverb in what is measured, one chord
static void plain(void)
{
    front_midi(0xC0, 0, 0);                                        // FIRST BAND: every sound setting at its default
    set("stmp", 100); set("skey", 0); set("sscl", 0); set("sprg", 0); set("schd", 0);
    set("fech", 0); set("frev", 0);
    set("dgon", 0); set("bmod", 0); set("lmod", 1); set("kmod", 1);   // the grid off; BASS and KEYS on KNOCK, LEAD on TILT
    set("bfx ", 0); set("lfx ", 0); set("kfx ", 0); set("hsnd", 0); set("wsnd", 0); set("dsnd", 0);
    for (int l = 0; l < L_N; l++) { set(lvl_tag[l], param(tag(lvl_tag[l]))->def); set(mute_tag[l], 0); }
    sim_run(0.3f);
}

// ---------------------------------------------------------------- the board with the stick
static void stick_board(void)
{
    section("band: rev E's controls on the board with the stick");
    rev_c(); plain();
    const ctl_t *c = front_ctl();
    make_live(L_LEAD); set("lvib", 0); set("lenv", 0); set("ltim", 0);   // a note that holds still: one saw, no beating
    key_on(72, 100); sim_run(3.0f);
    mix_t m0 = mix_now();
    float p0 = hz_to_midi(m_pitch_at(sim_t - 0.3f, sim_t, 300, 800)), r0 = m_rms(sim_t - 0.3f, sim_t);
    // the two pins are the stick's: counts on them and a sensor's reading are nothing here
    sim_lturn(3); sim_run(0.1f); sim_lturn(-5); sim_run(0.1f);
    bool no_knob = !c->has_lknob && c->lknob == 0 && !host_knob_shown();
    sim_hand(60, 0.8f);
    bool no_hand = !c->has_hand && c->hand == 0.0f && host_hand() == 0.0f;
    // and a sensor wired to a board that has its stick: the stick keeps the layer, the hand gets nothing
    SI.tof = true; sim_hand(60, 0.8f);
    bool seen = c->has_hand && c->hand > 0.8f && c->has_joy;
    float p1 = hz_to_midi(m_pitch_at(sim_t - 0.3f, sim_t, 300, 800)), r1 = m_rms(sim_t - 0.3f, sim_t);
    mix_t m1 = mix_now();
    shot("reve_stick_board");
    check(no_knob && no_hand && seen && host_hand() == 0.0f && others_moved(&m0, &m1, -1) == 0 && fabsf(p1 - p0) < 0.01f && fabsf(r1 / r0 - 1.0f) < 0.01f,
          "with a stick: left-knob counts and a hand at 6 cm change no level and no sound (LEAD's note %.3f st and %.2f %% from where it was; %d levels moved), even with a sensor wired in",
          (double)(p1 - p0), (double)(100.0f * (r1 / r0 - 1.0f)), others_moved(&m0, &m1, -1));
    // the stick still does what it did: LEAD's bend
    SI.jy = -0.95f; sim_run(0.6f);
    float p2 = hz_to_midi(m_pitch_at(sim_t - 0.3f, sim_t, 300, 800));
    SI.jy = 0; sim_hand(0, 0.5f);
    check(fabsf(p2 - p0 + 2.0f) < 0.05f, "and the stick pulled down still bends LEAD two semitones (%.2f st)", (double)(p2 - p0));
    // DRUMS: a hand over a board with a stick rolls nothing
    key_off(72); make_live(L_DRUMS); sim_run(0.5f);
    float t0 = sim_t; sim_hand(60, 1.2f); sim_hand(0, 0.3f);
    check(notes_on(DRUM_ON, t0, sim_t) == 0, "nor does it roll the drums (%d hits)", notes_on(DRUM_ON, t0, sim_t));
    SI.tof = false;
}

// ---------------------------------------------------------------- the left knob
// a layer alone, a steady sound: its level in the mix before and after the turn
static void knob_alone(int l, int detents, float secs, const char *label)
{
    make_live(l); sim_run(0.3f);
    mix_t m0 = mix_now();
    float before = get(lvl_tag[l]);
    sim_run(secs);
    float ta = sim_t - secs;
    lturn(detents);
    float show_t = sim_t;
    bool shown = host_knob_shown();
    if (label) shot(label);
    sim_run(secs - (sim_t - ta - secs));                           // (the same stretch of a bar, a bar later, for the grid)
    float tb = sim_t;
    sim_run(secs);
    float after = get(lvl_tag[l]), ra = m_rms(ta, ta + secs), rb = m_rms(tb, tb + secs);
    mix_t m1 = mix_now();
    float want = after / before, click = knob_click();
    if (sim_t - show_t < 1.3f) sim_run(1.3f - (sim_t - show_t));   // the show lasts 1.2 s after the last detent
    check(after == before + 5.0f * (float)detents && fabsf(rb / ra / want - 1.0f) < 0.05f && fabsf(m1.gain[l] - after * 0.01f) < 0.002f
          && others_moved(&m0, &m1, l) == 0 && click < 3.0f && shown && !host_knob_shown(),
          "%s live, the left knob %+d: its level %.0f -> %.0f, x%.3f in the mix (x%.3f wanted), %d other layers moved, no click (%.1fx the signal's own), shown in the header and gone again",
          lname[l], detents, (double)before, (double)after, (double)(rb / ra), (double)want, others_moved(&m0, &m1, l), (double)click);
}

static float breath_hz;                                            // BREATH's track: its strongest tone
static void left_knob(void)
{
    section("band: rev E, the left knob");
    rev_e(); plain();
    run_action("xcla"); sim_run(0.3f);
    const ctl_t *c = front_ctl();
    check(c->has_lknob && c->has_hand && !c->has_joy, "a rev E: the left knob and the sensor, no stick");

    // ---- two layers in the mix: BREATH's track loops, BASS holds a note; the knob on BASS
    make_live(L_BREATH); excite_breath(); sim_run(0.5f);
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();     // (REC is the left knob's push on this board)
    SI.mic = NULL; SI.mic_u = NULL;
    float L = (float)song.loop_len / AUDIO_SR;
    make_live(L_BASS); set("bsnd", 0);                             // SUB: it holds (FINGER is a pluck)
    sim_run(3.0f * L - 0.1f - 0.4f);                               // the take closes at the loop's end; then the track alone
    float th = sim_t - L;
    breath_hz = m_pitch_at(th, th + 0.5f, 300, 2500);
    breath_hz = peak_hz(th, th + L, breath_hz - 6.0f, breath_hz + 6.0f, 0.05f);
    float h_alone = amp_at(th, th + L, breath_hz);
    key_on(40, 100); sim_run(L);                                   // (a bar for the note to settle; the windows are whole loops)
    mix_t m0 = mix_now();
    float ta = sim_t; sim_run(L);
    lturn(-4);
    shot("reve_knob_bass");
    sim_run(L - (sim_t - ta - L));
    float tb = sim_t; sim_run(L);
    mix_t m1 = mix_now();
    float bass_hz = peak_hz(ta, ta + L, 35.0f, 180.0f, 0.1f);
    float ba = amp_at(ta, ta + L, bass_hz), bb = amp_at(tb, tb + L, bass_hz), ha = amp_at(ta, ta + L, breath_hz), hb = amp_at(tb, tb + L, breath_hz);
    float click = knob_click();
    check(L > 2.39f && L < 2.41f && h_alone > 0.002f && fabsf(ha / h_alone - 1.0f) < 0.02f,
          "BREATH's track loops (%.2f s, its tone at %.1f Hz) under BASS's held note (%.1f Hz)", (double)L, (double)breath_hz, (double)bass_hz);
    check(m0.lvl[L_BASS] == 80.0f && m1.lvl[L_BASS] == 60.0f && fabsf(bb / ba - 0.75f) < 0.03f && fabsf(hb / ha - 1.0f) < 0.01f && others_moved(&m0, &m1, L_BASS) == 0,
          "BASS live, the left knob four detents down: BASS 80 -> 60, its tone x%.3f in the mix (x0.750 wanted), BREATH's x%.4f, %d other levels moved",
          (double)(bb / ba), (double)(hb / ha), others_moved(&m0, &m1, L_BASS));
    check(click < 3.0f, "no click on any of the four detents (the worst jump %.1fx the signal's own)", (double)click);
    lturn(4); sim_run(0.2f);
    key_off(40); set("bsnd", param(tag("bsnd"))->def);

    // ---- the mixer: the right knob (held) picks the strip, the left knob sets its level, held or not
    make_live(L_N); sim_run(0.6f);
    sim_hold_knob(); sim_run(0.2f);
    bool grabbed = mixer_grabbed();
    for (int i = 0; i < 8 && mixer_cursor() != L_BREATH; i++) { sim_turn(mixer_cursor() < L_BREATH ? 1 : -1); sim_run(0.05f); }
    int cur = mixer_cursor();
    sim_run(2.0f * L - fmodf(sim_t - ta, L));                       // the bass has died away; on the same place in the loop
    m0 = mix_now();
    float ma = sim_t; sim_run(L);
    lturn(-4);
    shot("reve_mixer_knob");
    sim_run(L - (sim_t - ma - L));
    float mb = sim_t; sim_run(L);
    m1 = mix_now();
    float xa = amp_at(ma, ma + L, breath_hz), xb = amp_at(mb, mb + L, breath_hz);
    click = knob_click();
    check(host_live() == L_N && grabbed && cur == L_BREATH && m0.lvl[L_BREATH] == 80.0f && m1.lvl[L_BREATH] == 60.0f && fabsf(xb / xa - 0.75f) < 0.02f
          && others_moved(&m0, &m1, L_BREATH) == 0 && click < 3.0f,
          "the mixer: the right knob held and turned picks BREATH, the left knob four down: BREATH 80 -> 60, its track x%.3f (x0.750 wanted), %d others moved, no click (%.1fx)",
          (double)(xb / xa), others_moved(&m0, &m1, L_BREATH), (double)click);
    SI.pressed = true; sim_run(0.05f); SI.enc = 1; sim_tick(); sim_run(0.05f); SI.pressed = false; sim_tick(); sim_run(0.1f);
    float pt = get("mlvh");
    sim_hold_knob(); sim_run(0.2f);
    bool freed = !mixer_grabbed();
    lturn(3); sim_run(0.1f);
    float fr = get("mlvh");
    sim_rec(0.1f); sim_run(0.1f);                                   // the left knob's push: the strip's mute
    float mu = get("mmuh");
    lturn(1); sim_run(0.1f);
    check(pt == 65.0f && freed && fr == 80.0f && mu == 1.0f && get("mmuh") == 0.0f && get("mlvh") == 85.0f && mixer_cursor() == L_BREATH,
          "press and turn still sets it (65), the left knob with the right one free too (80); its push mutes the strip, a turn up brings it back (85)");
    lturn(-1);
    shot("reve_mixer");
    sim_turn(-1); sim_run(0.5f);
    check(host_live() == L_HOWL && get("mlvh") == 80.0f, "the right knob, free, leaves the mixer as before (HOWL live)");
    run_action("xcla"); sim_run(0.5f);

    // ---- one layer at a time, by its level in the mix
    make_live(L_LEAD); set("lvib", 0); set("lenv", 0); set("ltim", 0); key_on(72, 100); sim_run(3.0f);   // a note that holds still
    knob_alone(L_LEAD, 4, 0.6f, "reve_knob_lead");
    key_off(72);
    make_live(L_KEYS); set("ksnd", 2); set("kmot", 0); key_on(62, 100); sim_run(1.5f);   // (not 60: over MIDI that note is REC)
    knob_alone(L_KEYS, -6, 0.6f, NULL);
    key_off(62); sim_run(0.5f);
    make_live(L_DRUMS); set("dgon", 1); sim_run(2.4f - fmodf(sim_t, 2.4f) + 2.4f);
    knob_alone(L_DRUMS, -8, 2.4f, NULL);                           // the grid's bar, bar against bar
    set("dgon", 0);

    // ---- the ends, a fast turn, a muted layer
    make_live(L_LEAD); key_on(72, 100); sim_run(0.3f);
    set("mlvl", 70);
    sim_lturn(3); sim_run(0.1f);
    float fast = get("mlvl");
    lturn(12); float top = get("mlvl");
    lturn(-25); sim_run(0.3f); float bottom = get("mlvl"), silent = m_rms(sim_t - 0.2f, sim_t);
    shot("reve_knob_zero");
    lturn(1); float one = get("mlvl");
    set("mmul", 1); sim_run(0.2f);
    lturn(-1); float still = get("mmul");
    shot("reve_knob_muted");
    lturn(1); sim_run(0.3f);
    check(fast == 85.0f && top == 100.0f && bottom == 0.0f && silent < 0.0005f && one == 5.0f && still == 1.0f && get("mmul") == 0.0f && get("mlvl") == 5.0f,
          "three detents in one tick: +15; it stops at 100 and at 0 (silent, rms %.5f); a muted layer stays muted turned down, turned up it is on again", (double)silent);
    // the menu open: the right knob is the menu's, the left one is still the fader
    set("mlvl", 70);
    sim_tap(); sim_run(0.1f);
    bool menu = front_menu_level() > 0;
    lturn(2); sim_run(0.1f);
    float in_menu = get("mlvl");
    shot("reve_knob_menu");
    sim_menu_close(); sim_run(0.2f);
    check(menu && in_menu == 80.0f && front_menu_level() == 0 && host_live() == L_LEAD, "with the menu open the left knob still sets LEAD's level (70 -> %.0f)", (double)in_menu);
    key_off(72); set("mlvl", 70);
    check((meta.learned >> LEARN_LKNOB) & 1u, "the coach knows the left knob has been turned");
}

// ---------------------------------------------------------------- the hand
#define FAR 200                                                    // mm: 20 cm, 12 cm, 4 cm
#define MID 120
#define LOW 40
// the hand comes from nowhere to 4 cm and leaves again, each in one reading: the worst jump around either
static float hand_snap(void)
{
    sim_hand(0, 0.5f);
    float t0 = sim_t; sim_hand(LOW, 0.5f);
    float t1 = sim_t; sim_hand(0, 0.7f);
    sim_run(0.2f);
    float a = worst_click(t0, t0 + 0.3f), b = worst_click(t1, t1 + 0.5f);
    return a > b ? a : b;
}

static void hand_drums(void)
{
    make_live(L_DRUMS); set("dgon", 0); set("dkno", 1); sim_run(1.0f);
    float beat = 60.0f / song.bpm;
    float t0 = sim_t; sim_run(1.0f);
    int away = notes_on(DRUM_ON, t0, sim_t);
    // a hand that comes down to knock and goes: over the sensor for 0.15 s
    t0 = sim_t; sim_hand(60, 0.15f); sim_hand(0, 0.8f);
    int pass = notes_on(DRUM_ON, t0, sim_t);
    // held: eighths, sixteenths, thirty-seconds as it comes down
    static const int mm[3] = { FAR, 150, 60 }, div[3] = { 2, 4, 8 };
    int n[3]; float want[3]; bool ok = true;
    float first = 0;
    for (int k = 0; k < 3; k++) {
        float ts = sim_t;
        sim_hand(mm[k], 0.6f);
        if (k == 0) for (int i = 0; i < midi_n; i++) if (midi_log[i].st == DRUM_ON && midi_log[i].d2 > 0 && midi_log[i].t >= ts) { first = midi_log[i].t - ts; break; }
        float ta = sim_t; sim_hand(mm[k], 4.0f * beat);
        if (k == 1) shot("reve_hand_drums");
        n[k] = notes_on(DRUM_ON, ta, ta + 4.0f * beat); want[k] = 4.0f * (float)div[k];
        if (abs(n[k] - (int)want[k]) > 1) ok = false;
    }
    float tl = sim_t; sim_hand(0, 1.0f);
    int late = notes_on(DRUM_ON, tl + 0.1f, sim_t);
    float cut = worst_click(tl, tl + 0.3f);
    check(away == 0 && ok, "DRUMS: no hand, no roll (%d hits); a hand at 20 cm rolls eighths, at 15 cm sixteenths, at 6 cm thirty-seconds (%d, %d, %d hits in a bar; %.0f, %.0f, %.0f wanted)",
          away, n[0], n[1], n[2], (double)want[0], (double)want[1], (double)want[2]);
    check(pass == 0 && first > 0.25f && first < 0.45f, "a hand that passes for 0.15 s (a knock) rolls nothing (%d hits); held, the roll starts after %.2f s", pass, (double)first);
    check(late == 0 && cut < 3.0f && host_layer_state(L_DRUMS) == LOOP_NONE, "the hand gone: the roll stops within 0.1 s (%d hits after), the last hit rings out (%.1fx), nothing was recorded", late, (double)cut);
    // KNOCKS off (only the grid plays): nobody knocks, so the roll starts at once
    set("dkno", 0); sim_run(0.3f);
    t0 = sim_t; sim_hand(60, 0.2f);
    int quick = notes_on(DRUM_ON, t0, sim_t);
    sim_hand(0, 0.6f); set("dkno", 1);
    check(quick >= 1, "with KNOCKS off the roll does not wait (%d hits in the first 0.2 s)", quick);
}

static void hand_bass(void)
{
    make_live(L_BASS); set("bsnd", 1); set("benv", 0); sim_run(0.3f);   // ACID: a saw through the filter
    key_on(40, 100); sim_run(1.2f);
    float b[5];
    static const int mm[5] = { 0, FAR, MID, LOW, 0 };
    for (int k = 0; k < 5; k++) { sim_hand(mm[k], k == 4 ? 2.5f : 1.0f); b[k] = bright(sim_t - 0.4f, sim_t); if (k == 3) shot("reve_hand_bass"); }
    float click = hand_snap();
    check(b[1] > 1.1f * b[0] && b[2] > 1.2f * b[1] && b[3] > 1.2f * b[2] && fabsf(b[4] / b[0] - 1.0f) < 0.03f,
          "BASS: the filter opens as the hand comes down: brightness %.3f with no hand, %.3f at 20 cm, %.3f at 12 cm, %.3f at 4 cm, %.3f with it gone",
          (double)b[0], (double)b[1], (double)b[2], (double)b[3], (double)b[4]);
    check(click < 3.0f, "BASS: a hand arriving at 4 cm and leaving in one reading: no click (%.1fx the signal's own)", (double)click);
    // the layer left with the hand still over the sensor: BASS lets go of it (its note still sounds under LEAD)
    sim_hand(LOW, 0.8f);
    float open = bright(sim_t - 0.3f, sim_t);
    make_live(L_LEAD); sim_hand(LOW, 0.9f);
    float left = bright(sim_t - 0.3f, sim_t);
    sim_hand(0, 0.5f);
    make_live(L_BASS);
    check(open > 1.5f * b[0] && fabsf(left / b[0] - 1.0f) < 0.03f, "BASS left for LEAD with the hand still at 4 cm: its filter closes again (%.3f, from %.3f open)", (double)left, (double)open);
    key_off(40); set("bsnd", param(tag("bsnd"))->def); set("benv", param(tag("benv"))->def);
}

static void hand_lead(void)
{
    make_live(L_LEAD); set("lvib", 0); sim_run(0.3f);
    key_on(72, 100); sim_run(0.8f);
    float p[5];
    static const int mm[5] = { 0, FAR, MID, LOW, 0 };
    for (int k = 0; k < 5; k++) { sim_hand(mm[k], k == 4 ? 2.5f : 0.9f); p[k] = hz_to_midi(m_pitch_at(sim_t - 0.3f, sim_t, 300, 800)); if (k == 2) shot("reve_hand_lead"); }
    float worst = 0;
    for (int k = 1; k < 4; k++) { float e = fabsf(p[k] - p[0] + 2.0f * hand_of(mm[k])); if (e > worst) worst = e; }
    float click = hand_snap();
    check(worst < 0.05f && fabsf(p[4] - p[0]) < 0.02f,
          "LEAD: the note bends down as the hand comes down: %.2f st at 20 cm, %.2f at 12 cm, %.2f at 4 cm (%.2f, %.2f, %.2f wanted), %.3f with it gone",
          (double)(p[1] - p[0]), (double)(p[2] - p[0]), (double)(p[3] - p[0]), (double)(-2.0f * hand_of(FAR)), (double)(-2.0f * hand_of(MID)), -2.0, (double)(p[4] - p[0]));
    check(click < 3.0f, "LEAD: a hand arriving at 4 cm and leaving in one reading: no click (%.1fx the signal's own)", (double)click);
    key_off(72);
}

static void hand_keys(void)
{
    make_live(L_KEYS); set("ksnd", 2); set("kmot", 0); sim_run(0.3f);   // ORGAN, its rotor still
    key_on(62, 100); sim_run(1.5f);
    float w[5];
    static const int mm[5] = { 0, FAR, MID, LOW, 0 };
    for (int k = 0; k < 5; k++) { sim_hand(mm[k], k == 4 ? 4.0f : 3.5f); w[k] = wobble(sim_t - 2.0f, sim_t); if (k == 3) shot("reve_hand_keys"); }
    float click = hand_snap();
    check(w[0] < 0.03f && w[1] > w[0] + 0.004f && w[2] > 2.0f * w[1] && w[3] > 2.0f * w[2] && fabsf(w[4] - w[0]) < 0.004f,
          "KEYS: the organ's rotor turns up as the hand comes down: the level moves %.3f with no hand, %.3f at 20 cm, %.3f at 12 cm, %.3f at 4 cm, %.3f with it gone",
          (double)w[0], (double)w[1], (double)w[2], (double)w[3], (double)w[4]);
    check(click < 3.0f, "KEYS: a hand arriving at 4 cm and leaving in one reading: no click (%.1fx the signal's own)", (double)click);
    key_off(62); set("ksnd", param(tag("ksnd"))->def); set("kmot", param(tag("kmot"))->def);
}

static void hand_breath(void)
{
    make_live(L_BREATH); set("hvib", 0); excite_breath(); sim_run(1.5f);
    float f0 = m_pitch_at(sim_t - 0.4f, sim_t, 300, 2500);
    float s[5], mid[5];
    static const int mm[5] = { 0, FAR, MID, LOW, 0 };
    for (int k = 0; k < 5; k++) { sim_hand(mm[k], k == 4 ? 3.0f : 1.6f); pitch_swing(sim_t - 1.0f, sim_t, f0 * 0.85f, f0 * 1.18f, &mid[k], &s[k]); if (k == 3) shot("reve_hand_breath"); }
    float click = hand_snap();
    bool ok = s[0] < 4.0f && s[4] < 4.0f;
    for (int k = 1; k < 4; k++) if (fabsf(s[k] / (50.0f * hand_of(mm[k])) - 1.0f) > 0.3f) ok = false;
    check(ok && s[1] < s[2] && s[2] < s[3],
          "BREATH: the vibrato deepens as the hand comes down: the pitch swings %.1f cents with no hand, %.1f at 20 cm, %.1f at 12 cm, %.1f at 4 cm (%.0f, %.0f, %.0f wanted), %.1f with it gone",
          (double)s[0], (double)s[1], (double)s[2], (double)s[3], (double)(50.0f * hand_of(FAR)), (double)(50.0f * hand_of(MID)), 50.0, (double)s[4]);
    check(click < 3.0f, "BREATH: a hand arriving at 4 cm and leaving in one reading: no click (%.1fx the signal's own)", (double)click);
    SI.mic = NULL; SI.mic_u = NULL; set("hvib", param(tag("hvib"))->def);
    sim_run(0.5f);
}

// HOWL: the hand is gain. Tipped 9 degrees it sits under the howling edge; with a hand there, how soon it howls
// (a 50 ms level over 0.02; 9: not within 6 s) and how loud it is 2 to 3 s later
static float howl_after(int mm, float *t_howl)
{
    sim_hand(0, 2.5f);
    float t0 = sim_t;
    sim_hand(mm, 6.0f);
    *t_howl = 9.0f;
    for (float t = t0; t + 0.05f <= sim_t; t += 0.01f) if (m_rms(t, t + 0.05f) > 0.02f) { *t_howl = t - t0; break; }
    return m_rms(t0 + 2.0f, t0 + 3.0f);
}
static void hand_howl(void)
{
    make_live(L_HOWL); excite_howl();                              // (its own mic: the room's noise, no joystick)
    sim_tilt(0, 9); sim_run(3.0f);
    float t[4], r[4];
    static const int mm[4] = { 0, FAR, MID, LOW };
    for (int k = 0; k < 4; k++) { r[k] = howl_after(mm[k], &t[k]); if (k == 3) shot("reve_hand_howl"); }
    sim_hand(0, 3.0f);
    float gone = m_rms(sim_t - 0.5f, sim_t);
    float t0 = sim_t; sim_hand(LOW, 1.5f); float t1 = sim_t; sim_hand(0, 2.0f);
    sim_run(0.2f);
    float click = fmaxf(worst_click(t0, t0 + 1.5f), worst_click(t1, t1 + 1.0f));
    check(r[0] < 0.005f && t[0] > 6.0f && t[1] > t[2] && t[2] < 6.0f && t[3] < t[2] && r[2] > 0.02f && r[3] > 0.02f && gone < 0.005f,
          "HOWL: tipped 9 degrees and no hand, it is quiet (rms %.4f); a hand at 20 cm: %s; at 12 cm it howls after %.2f s, at 4 cm after %.2f s (rms %.3f, %.3f); gone, it dies away (%.4f)",
          (double)r[0], t[1] > 6.0f ? "not within 6 s" : "later still", (double)t[2], (double)t[3], (double)r[2], (double)r[3], (double)gone);
    check(click < 3.0f, "HOWL: the hand at 4 cm and away again: no click (%.1fx the signal's own)", (double)click);
    SI.mic = NULL; SI.mic_u = NULL; sim_tilt(0, 0);
    sim_run(1.0f);
}

static void hand(void)
{
    section("band: rev E, the hand");
    rev_e(); plain();
    run_action("xcla"); sim_run(0.3f);
    hand_drums();
    hand_bass();
    hand_lead();
    hand_keys();
    hand_breath();
    hand_howl();
    check((meta.learned >> LEARN_HAND) & 1u, "the coach knows a hand has been over the sensor");
    // the mixer: no layer is live there, the hand is nobody's
    make_live(L_N); sim_run(0.5f);
    mix_t m0 = mix_now();
    sim_hand(LOW, 1.0f);
    float hv = host_hand();
    mix_t m1 = mix_now();
    sim_hand(0, 0.5f);
    check(hv == 0.0f && others_moved(&m0, &m1, -1) == 0, "in the mixer the hand does nothing (%d levels moved)", others_moved(&m0, &m1, -1));
    make_live(L_DRUMS);
}

// ---------------------------------------------------------------- the song stays
#define NSNAP 64
static void snap(int k, int32_t *v)
{
    int n = 0;
    memset(v, 0, NSNAP * sizeof v[0]);
    v[n++] = (int32_t)song.fpb; v[n++] = (int32_t)song.origin; v[n++] = (int32_t)song.loop_len; v[n++] = (int32_t)song.loop_start; v[n++] = song.beats;
    v[n++] = song.key; v[n++] = (int32_t)get("stmp"); v[n++] = (int32_t)get("skey"); v[n++] = (int32_t)get("sscl"); v[n++] = (int32_t)get("sprg");
    v[n++] = (int32_t)get("schd"); v[n++] = (int32_t)get("sbar");
    const link_time_t *t = link_time(); const link_harm_t *h = link_harm();
    v[n++] = (int32_t)t->ver; v[n++] = (int32_t)t->fpb; v[n++] = (int32_t)t->origin; v[n++] = t->beats; v[n++] = t->bar;
    v[n++] = (int32_t)h->ver; v[n++] = h->key; v[n++] = h->scale; v[n++] = h->prog; v[n++] = h->hold; v[n++] = h->bars; v[n++] = h->live;
    for (int l = 0; l < L_N; l++) v[n++] = host_layer_state(l);
    v[n++] = (int32_t)get("dgr0"); v[n++] = (int32_t)get("dgr1"); v[n++] = (int32_t)get("dgr2"); v[n++] = (int32_t)get("dgr3"); v[n++] = (int32_t)get("dgon");
    for (int l = 0; l < L_N; l++) { v[n++] = (int32_t)get(lvl_tag[l]); v[n++] = (int32_t)get(mute_tag[l]); }
    sim_peer_use(k);                                               // what the other board has of the session
    v[n++] = (int32_t)link_time()->ver; v[n++] = (int32_t)link_time()->fpb; v[n++] = link_time()->beats; v[n++] = (int32_t)link_harm()->ver; v[n++] = link_harm()->key;
    sim_peer_use(-1);
}
static void song_stays(void)
{
    section("band: rev E, the song stays");
    rev_e(); plain();
    run_action("xcla"); sim_run(0.3f);
    set("sprg", 1); set("skey", 2);                                // I V vi IV in D
    int k = sim_peer_add("SING", 1, -25.0, 4.0e9);                 // a board on the link, its crystal 25 ppm slow
    sim_run(2.0f);
    // a loop of each kind: BREATH's audio track, LEAD's notes, the drums' grid
    make_live(L_BREATH); excite_breath(); sim_run(0.5f);
    SI.rec = true; sim_run(2.4f); SI.rec = false; sim_tick();
    SI.mic = NULL; SI.mic_u = NULL;
    float L = (float)song.loop_len / AUDIO_SR;
    make_live(L_LEAD); sim_run(L);
    SI.rec = true; sim_run(0.4f); key_on(74, 100); sim_run(0.5f); key_off(74); sim_run(0.4f); SI.rec = false; sim_tick();
    make_live(L_DRUMS); set("dgon", 1);
    sim_run(6.0f * L);
    uint8_t lead_st = (uint8_t)(0x90 | ((in_ch() + 3) & 15));
    int32_t a[NSNAP], b[NSNAP];
    snap(k, a);
    float ta = sim_t - 4.0f * L;                                    // four passes of the loop, before and after
    int lead_a = notes_on(lead_st, ta, ta + 4.0f * L), drums_a = notes_on(DRUM_ON, ta, ta + 4.0f * L);
    float gap_a = sim_peer_gap_ms(k);
    // every layer: the left knob down and back, the hand down to 4 cm and away; then the mixer
    for (int l = 0; l <= L_N; l++) {
        make_live(l);
        lturn(-3); sim_run(0.2f); lturn(3);
        for (int mm = 240; mm >= 40; mm -= 20) sim_hand(mm, 0.05f);
        sim_hand(40, 0.4f);
        sim_hand(0, 0.5f);
    }
    make_live(L_DRUMS);
    float settle = 2.0f * L - fmodf(sim_t - ta, L);
    sim_run(settle + 4.0f * L);
    snap(k, b);
    float tb = sim_t - 4.0f * L;
    int lead_b = notes_on(lead_st, tb, tb + 4.0f * L), drums_b = notes_on(DRUM_ON, tb, tb + 4.0f * L);
    float gap_b = sim_peer_gap_ms(k);
    int diff = 0, first = -1;
    for (int i = 0; i < NSNAP; i++) if (a[i] != b[i]) { diff++; if (first < 0) first = i; }
    if (diff) note("first difference at %d: %d -> %d", first, (int)a[first], (int)b[first]);
    check(a[2] > 0 && a[15] == a[4] && diff == 0,
          "every layer's left knob and hand, and the mixer's: the tempo (%.1f BPM), the key, the chords, the loop (%d beats), each layer's loop, the grid and the link's session (here and on the other board) are what they were (%d of %d values differ)",
          (double)song.bpm, song.beats, diff, NSNAP);
    // BREATH's track by its tone, pass by pass (the drums' noise falls on it too: a pass differs from the next by
    // up to 1 %), the mean of four
    float hz = peak_hz(ta, ta + 0.5f, 300.0f, 2500.0f, 1.0f);
    hz = peak_hz(ta, ta + L, hz - 2.0f, hz + 2.0f, 0.05f);
    float ha = 0, hb = 0;
    for (int q = 0; q < 4; q++) { ha += 0.25f * amp_at(ta + (float)q * L, ta + (float)(q + 1) * L, hz); hb += 0.25f * amp_at(tb + (float)q * L, tb + (float)(q + 1) * L, hz); }
    check(lead_a >= 4 && lead_b == lead_a && drums_b == drums_a && drums_a > 16 && ha > 0.002f && fabsf(hb / ha - 1.0f) < 0.02f,
          "the loops play what they played: LEAD's %d notes in four passes (%d before), the grid's %d hits (%d before), BREATH's track x%.4f (its tone at %.1f Hz)",
          lead_b, lead_a, drums_b, drums_a, (double)(hb / ha), (double)hz);
    check(fabsf(gap_a) < 1.0f && fabsf(gap_b) < 1.0f, "the other board is still on the beat (%.3f ms apart; %.3f before)", (double)gap_b, (double)gap_a);
    sim_peer_power(k, false);
    set("dgon", 0); set("sprg", 0); set("skey", 0);
    run_action("xcla"); sim_run(2.0f);
}

// ---------------------------------------------------------------- the editors
static void editors(void)
{
    section("band: rev E, the grid and the line");
    rev_e(); plain();
    // the grid: the right knob is its; the hand moves nothing, the left knob is still the drums' fader
    make_live(L_DRUMS); sim_run(0.3f);
    sim_hold_knob(); sim_run(0.3f);
    bool open = drums_editing();
    int c0 = drums_cursor();
    float rows0[4] = { get("dgr0"), get("dgr1"), get("dgr2"), get("dgr3") };
    sim_hand(60, 1.2f);
    bool same = drums_cursor() == c0 && get("dgr0") == rows0[0] && get("dgr1") == rows0[1] && get("dgr2") == rows0[2] && get("dgr3") == rows0[3];
    float hv = host_hand();
    sim_hand(0, 0.4f);
    float l0 = get("mlvd");
    lturn(-2); sim_run(0.1f);
    shot("reve_grid_knob");
    float l1 = get("mlvd");
    bool still = drums_editing() && drums_cursor() == c0;
    sim_turn(1); sim_run(0.1f);
    int c1 = drums_cursor();
    sim_tap(); sim_run(0.1f);
    bool drew = get("dgr0") != rows0[0] || get("dgr1") != rows0[1] || get("dgr2") != rows0[2] || get("dgr3") != rows0[3];
    sim_tap(); sim_run(0.1f); sim_tap(); sim_run(0.1f);            // on, accent, off: as it was
    sim_hold_knob(); sim_run(0.3f);
    check(open && same && hv > 0.8f && l1 == l0 - 10.0f && still && c1 == (c0 + 1) % 32 && drew && !drums_editing(),
          "the grid: a hand at 6 cm moves no cursor and no step; the left knob is the drums' level there (%.0f -> %.0f); the right knob still moves, sets and closes",
          (double)l0, (double)l1);
    lturn(2); set("dgon", 0); set("dgbt", 1); set("dgr0", rows0[0]); set("dgr1", rows0[1]); set("dgr2", rows0[2]); set("dgr3", rows0[3]);
    // the bass line
    make_live(L_BASS); sim_run(0.3f);
    sim_hold_knob(); sim_run(0.3f);
    open = bass_editing();
    c0 = bass_cursor();
    sim_hand(60, 1.2f);
    same = bass_cursor() == c0;
    sim_hand(0, 0.4f);
    l0 = get("mlvb");
    lturn(-2); sim_run(0.1f);
    shot("reve_line_knob");
    l1 = get("mlvb");
    still = bass_editing() && bass_cursor() == c0;
    sim_turn(1); sim_run(0.1f);
    c1 = bass_cursor();
    sim_hold_knob(); sim_run(0.3f);
    check(open && same && l1 == l0 - 10.0f && still && c1 == (c0 + 1) % 16 && !bass_editing(),
          "the bass line: the same (the hand moves nothing, the left knob BASS %.0f -> %.0f, the right knob moves and closes)", (double)l0, (double)l1);
    lturn(2);
    plain();
}

// ---------------------------------------------------------------- the screen
static void show_hint(int learn, const char *label)                // only that hint left to learn: it comes up when idle
{
    uint32_t keep = meta.learned;
    coach_pause();
    param_set(CP(CP_HINTS), 0); param_set(CP(CP_HINTS), 1);        // (HINTS switched on: the coach starts over)
    meta.learned = ~(1u << learn);
    sim_run(4.0f);
    sim_shot(label);
    meta.learned = keep;
    coach_pause(); sim_run(0.3f);
}
static int lit(int x0, int y0, int w, int h) { int n = 0; for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) n += oled_get(x, y) ? 1 : 0; return n; }
static void screens(void)
{
    section("band: rev E, the screen");
    rev_e(); plain();
    make_live(L_KEYS); set("ksnd", 2); sim_run(0.6f);
    coach_pause(); sim_run(0.08f);
    int bar = lit(14, 2, 64, 12);                                  // the five other layers' icons
    float t0 = sim_t;
    sim_gif_begin("rev-e");
    key_on(62, 100); key_on(65, 100); key_on(69, 100); sim_run(1.0f);
    lturn(-6); sim_run(0.1f);
    coach_pause(); sim_run(0.08f);
    int knob = lit(14, 2, 64, 12), spill_k = lit(78, 0, 2, 14);
    sim_run(1.6f);
    lturn(6); sim_run(1.6f);
    for (int mm = 240; mm >= 40; mm -= 8) sim_hand(mm, 0.08f);
    coach_pause(); sim_run(0.08f);
    int handpx = lit(14, 2, 64, 12), spill_h = lit(78, 0, 2, 14);
    sim_run(1.0f);
    for (int mm = 40; mm <= 240; mm += 20) sim_hand(mm, 0.08f);
    sim_hand(0, 1.0f);
    coach_pause(); sim_run(0.08f);
    int back = lit(14, 2, 64, 12);
    key_off(62); key_off(65); key_off(69);
    make_live(L_DRUMS); set("dkno", 0); sim_run(0.5f);
    for (int mm = 240; mm >= 60; mm -= 12) sim_hand(mm, 0.12f);
    sim_hand(60, 0.8f); sim_hand(0, 1.0f);
    set("dkno", 1);
    sim_gif_end();
    sim_wav("-rev-e", t0, sim_t);
    check(knob != bar && handpx != bar && handpx != knob && back == bar && spill_k == 0 && spill_h == 0,
          "the header: the layer bar gives way to the knob's level, then to the hand's show, and comes back when the hand has gone; neither runs into the chord's name");
    // the mixer's first lines on this board: the left knob's among them
    mixer_first_visit();
    make_live(L_N); sim_run(0.3f);
    shot("reve_mixer_lines");
    sim_hold_knob(); sim_run(1.9f);
    shot("reve_mixer_lines_held");
    sim_run(5.0f);
    sim_hold_knob(); sim_run(0.2f);
    int learned_hint = 0;
    for (int i = 0; i < INST.nhints; i++) if (INST.hints[i].learn == LEARN_LKNOB || INST.hints[i].learn == LEARN_HAND) learned_hint++;
    check(learned_hint == 2, "a hint for the left knob and one for the hand (%d)", learned_hint);
    make_live(L_BASS); sim_run(0.5f);
    show_hint(LEARN_LKNOB, "reve_hint_knob");
    show_hint(LEARN_HAND, "reve_hint_hand");
    check(m_bad() == 0, "the knob and the hand over a chord and a roll (out/band-rev-e.wav, band-rev-e.gif): no NaN");
    make_live(L_DRUMS);
}

void test_rev_e(void)
{
    SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0);
    board_c = SI;
    sim_menu_close();
    make_live(L_DRUMS);
    stick_board();
    left_knob();
    hand();
    song_stays();
    editors();
    screens();
    SI = board_c;
    plain();
    set("fech", param(tag("fech"))->def); set("frev", param(tag("frev"))->def);
    run_action("xcla");
    make_live(L_DRUMS);
    sim_run(2.0f);
}
