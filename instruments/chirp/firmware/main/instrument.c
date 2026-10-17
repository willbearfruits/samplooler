// CHIRP: the Bluetooth devices around you as a dawn chorus. Phones, earbuds, watches, trackers, beacons and
// laptops advertise over Bluetooth LE several times a second; each one becomes a bird on one of three wires
// (BLE's three advertising channels), with a call that fits what it is, a note of its own, and a voice as
// near or far as its signal.
//
//   CHORUS  each advert can be a call; birds sing in short phrases and rest, as birds do
//   CLOCKS  each bird pulses at its own advertising interval (slowed down): a polyrhythm; QUANTIZE puts it on a grid
//   SWARM   every advert is a grain: the crowd's density as a flutter over a bed of air
//   HUNT    follow one bird like a Geiger counter: ticks speed up and rise as you get nearer to it
//
// Controls: turn = sound, tap = menu, hold knob = hold the tilt, REC = looper (core).
//   tilt (bottom edge down)  longer calls (TILT DOES: LENGTH) or faster birds (SPEED)
//   lean left / right        shift the birds up or down the scale
//   stick left / right       spotlight a bird (louder; the one HUNT follows)
//   stick up / down          brighter / darker (STICK Y: TONE) or more / fewer calls (DENSITY)
// MIDI out: a note per call (velocity = nearness), CC 3 bird count, CC 9 calls per second. MIDI in: notes play a call.
//
// Privacy: the radio hands over only a salted hash of each address (a new salt every boot), the signal
// strength, the advertising type and a coarse guess at the kind of device (inst_adv.c). Nothing is kept.
#include "inst_chirp.h"
#include "params.h"
#include "dsp.h"
#include "ui.h"
#include "spsc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    P_MODE, P_SCALE, P_KEY, P_OCT, P_SPREAD, P_VOICES, P_LEN, P_SWEEP, P_BRIGHT,
    P_DENS, P_GAP, P_PHRASE, P_NEAR, P_FAR, P_FORGET, P_GREET, P_MAXB,
    P_RATE, P_QUANT, P_BPM, P_GRID, P_SWING, P_PULSE,
    P_VERB, P_ROOM, P_DIST, P_AIR, P_TONE,
    P_TILT, P_LEAN, P_STICK, P_SPOT, P_NEXT, P_MSPLIT, P_MIN,
    P_N
};
enum { G_BIRDS, G_CROWD, G_CLOCKS, G_SPACE, G_CONTROL };
static const char *const groups[] = { "BIRDS", "CROWD", "CLOCKS", "SPACE", "CONTROL" };

typedef struct { const char *name; int n; int8_t st[12]; } scale_t;
static const scale_t scales[] = {
    { "MAJ PENTA", 5, { 0, 2, 4, 7, 9 } },        { "MIN PENTA", 5, { 0, 3, 5, 7, 10 } },
    { "MAJOR", 7, { 0, 2, 4, 5, 7, 9, 11 } },     { "MINOR", 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { "DORIAN", 7, { 0, 2, 3, 5, 7, 9, 10 } },    { "LYDIAN", 7, { 0, 2, 4, 6, 7, 9, 11 } },
    { "HIRAJOSHI", 5, { 0, 2, 3, 7, 8 } },        { "PELOG", 5, { 0, 1, 3, 7, 8 } },
    { "WHOLE TONE", 6, { 0, 2, 4, 6, 8, 10 } },   { "BLUES", 6, { 0, 3, 5, 6, 7, 10 } },
    { "CHROMATIC", 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
};
#define N_SCALES ((int)(sizeof scales / sizeof scales[0]))
static const char *const scale_l[N_SCALES] = { "MAJ PENTA", "MIN PENTA", "MAJOR", "MINOR", "DORIAN", "LYDIAN", "HIRAJOSHI", "PELOG", "WHOLE TONE", "BLUES", "CHROMATIC" };
static const char *const mode_l[] = { "CHORUS", "CLOCKS", "SWARM", "HUNT" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const voice_l[] = { "BY KIND", "CHIRP", "WHISTLE", "TRILL", "WARBLE", "PIP", "COO", "BELL", "DROP", "MIXED" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const grid_l[] = { "1/32", "1/16", "1/8", "1/4" };
static const char *const tilt_l[] = { "LENGTH", "SPEED", "OFF" };
static const char *const stick_l[] = { "TONE", "DENSITY", "OFF" };
static const char *const split_l[] = { "ONE CHANNEL", "BY KIND" };

static const param_t params[P_N] = {
    [P_MODE]   = { "MODE", "mode", G_BIRDS, PK_ENUM, 0, 0, 3, 1, 0, "", mode_l, "HOW THE CROWD SOUNDS" },
    [P_SCALE]  = { "SCALE", "scal", G_BIRDS, PK_ENUM, 0, 0, N_SCALES - 1, 1, 0, "", scale_l, "THE NOTES THE BIRDS SING" },
    [P_KEY]    = { "KEY", "key ", G_BIRDS, PK_ENUM, 0, 0, 11, 1, 2, "", key_l, "THE SCALE'S HOME NOTE" },
    [P_OCT]    = { "OCTAVE", "octv", G_BIRDS, PK_NUM, PF_INT, 3, 6, 1, 5, "", NULL, "THE LOWEST REGISTER" },
    [P_SPREAD] = { "SPREAD", "sprd", G_BIRDS, PK_NUM, PF_INT, 1, 4, 1, 2, "oct", NULL, "HOW MANY OCTAVES" },
    [P_VOICES] = { "VOICES", "spec", G_BIRDS, PK_ENUM, 0, 0, 9, 1, 0, "", voice_l, "BY KIND: EACH ITS OWN" },
    [P_LEN]    = { "CALL LENGTH", "clen", G_BIRDS, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "LONGER OR SHORTER CALLS" },
    [P_SWEEP]  = { "SWEEP", "swep", G_BIRDS, PK_NUM, PF_INT, 0, 200, 10, 100, "%", NULL, "HOW FAR THE CALLS GLIDE" },
    [P_BRIGHT] = { "BRIGHT", "brit", G_BIRDS, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "THIN AND PURE, OR REEDY" },
    [P_DENS]   = { "CALLS/SEC", "dens", G_CROWD, PK_NUM, PF_LOG, 1, 64, 1.12f, 12, "", NULL, "MOST CALLS A SECOND" },
    [P_GAP]    = { "REST", "cgap", G_CROWD, PK_NUM, PF_LOG, 0.05f, 5.0f, 1.12f, 0.35f, "s", NULL, "PAUSE BETWEEN PHRASES" },
    [P_PHRASE] = { "PHRASE", "phrs", G_CROWD, PK_NUM, PF_INT, 1, 8, 1, 3, "", NULL, "MOST CALLS IN A ROW" },
    [P_NEAR]   = { "NEAR AT", "near", G_CROWD, PK_NUM, PF_INT, -70, -20, 1, -45, "dBm", NULL, "THIS STRONG IS RIGHT HERE" },
    [P_FAR]    = { "FAR AT", "far ", G_CROWD, PK_NUM, PF_INT, -110, -60, 1, -92, "dBm", NULL, "THIS WEAK IS FARTHEST" },
    [P_FORGET] = { "FORGET", "frgt", G_CROWD, PK_NUM, PF_LOG, 2, 60, 1.12f, 10, "s", NULL, "SILENT THIS LONG: GONE" },
    [P_GREET]  = { "HELLO/BYE", "grt ", G_CROWD, PK_NUM, PF_INT, 0, 100, 10, 60, "%", NULL, "ARRIVALS AND GOODBYES" },
    [P_MAXB]   = { "MAX BIRDS", "maxb", G_CROWD, PK_NUM, PF_INT, 4, MAXB, 1, 18, "", NULL, "ONLY THE NEAREST SING" },
    [P_RATE]   = { "SLOW DOWN", "rate", G_CLOCKS, PK_NUM, PF_LOG, 1, 32, 1.12f, 4, "x", NULL, "SLOWS EACH BIRD'S PULSE" },
    [P_QUANT]  = { "QUANTIZE", "quan", G_CLOCKS, PK_ENUM, 0, 0, 1, 1, 0, "", off_on, "ON: SNAP TO THE TEMPO" },
    [P_BPM]    = { "TEMPO", "bpm ", G_CLOCKS, PK_NUM, PF_INT, 40, 220, 1, 96, "BPM", NULL, "FOR QUANTIZE" },
    [P_GRID]   = { "GRID", "grid", G_CLOCKS, PK_ENUM, 0, 0, 3, 1, 1, "", grid_l, "THE SHORTEST STEP" },
    [P_SWING]  = { "SWING", "swng", G_CLOCKS, PK_NUM, PF_INT, 0, 60, 5, 0, "%", NULL, "LATE OFFBEATS" },
    [P_PULSE]  = { "PULSE LEN", "puls", G_CLOCKS, PK_NUM, PF_INT, 10, 100, 5, 45, "%", NULL, "PULSE LENGTH VS A CALL" },
    [P_VERB]   = { "REVERB", "verb", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW MUCH OF THE ROOM" },
    [P_ROOM]   = { "ROOM", "room", G_SPACE, PK_NUM, PF_LOG, 0.3f, 10.0f, 1.12f, 2.2f, "s", NULL, "HOW LONG THE ROOM RINGS" },
    [P_DIST]   = { "DISTANCE", "dist", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "FAR: QUIETER, DULLER" },
    [P_AIR]    = { "AIR", "air ", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "WIND FROM THE CROWD" },
    [P_TONE]   = { "TONE", "tone", G_SPACE, PK_NUM, PF_LOG, 600, 16000, 1.12f, 9000, "Hz", NULL, "A FILTER ON EVERYTHING" },
    [P_TILT]   = { "TILT DOES", "tilt", G_CONTROL, PK_ENUM, 0, 0, 2, 1, 0, "", tilt_l, "BOTTOM EDGE DOWN: MORE" },
    [P_LEAN]   = { "LEAN SHIFT", "lean", G_CONTROL, PK_NUM, PF_INT, 0, 7, 1, 4, "", NULL, "SCALE STEPS PER LEAN" },
    [P_STICK]  = { "STICK Y", "stck", G_CONTROL, PK_ENUM, 0, 0, 2, 1, 0, "", stick_l, "STICK UP AND DOWN" },
    [P_SPOT]   = { "SPOTLIGHT", "spot", G_CONTROL, PK_NUM, PF_INT, 0, 12, 1, 6, "dB", NULL, "THE CHOSEN BIRD, LOUDER" },
    [P_NEXT]   = { "NEXT BIRD", "next", G_CONTROL, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "SPOTLIGHT THE NEXT ONE" },
    [P_MSPLIT] = { "MIDI SPLIT", "mspl", G_CONTROL, PK_ENUM, 0, 0, 1, 1, 0, "", split_l, "BY KIND: CH PER KIND" },
    [P_MIN]    = { "MIDI KEYS", "mdin", G_CONTROL, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "ON: NOTES IN PLAY CALLS" },
};

static const preset_t presets[] = {
    { "DAWN CHORUS",   "" },
    { "CLOCK SHOP",    "mode=1 rate=4 spec=5 clen=0.5 verb=15 room=0.8 puls=35 brit=55" },
    { "DRUM CIRCLE",   "mode=1 quan=1 bpm=100 grid=1 swng=20 rate=2 spec=8 scal=1 key=9 octv=4 clen=0.6 verb=20 room=1" },
    { "GAMELAN",       "mode=1 quan=1 bpm=72 grid=2 rate=3 scal=7 key=0 octv=4 spec=7 clen=1.6 verb=45 room=3.5 puls=70" },
    { "NIGHT FOREST",  "scal=1 key=9 octv=4 clen=2.2 dens=4 cgap=1.6 verb=70 room=6 dist=90 air=45 brit=10 tone=3500" },
    { "AVIARY",        "spec=3 dens=40 cgap=0.12 phrs=5 octv=6 clen=0.7 verb=25" },
    { "STARLINGS",     "mode=2 dens=48 air=55 verb=40 octv=6 sprd=3 room=3" },
    { "PEBBLE RAIN",   "mode=2 spec=8 dens=30 verb=20 octv=5 air=10" },
    { "PIGEON SQUARE", "spec=6 octv=4 dens=6 cgap=1.2 clen=1.3 verb=30 phrs=2" },
    { "MORSE",         "spec=5 cgap=0.08 phrs=6 dens=24 verb=10 octv=6 scal=2" },
    { "WATER GARDEN",  "spec=8 dens=10 verb=55 room=3 octv=5 scal=0 key=7" },
    { "SLOW MOTION",   "clen=4 swep=200 dens=3 cgap=2.5 verb=60 room=8 octv=4 scal=6 key=4 brit=15" },
    { "WARBLERS",      "spec=4 scal=5 key=0 dens=14 phrs=4 brit=50" },
    { "GEIGER",        "mode=3 verb=10 room=0.8 air=0 spot=0" },
};
#define N_PRESETS ((int)(sizeof presets / sizeof presets[0]))

#define LEARN_LISTEN (LEARN_INST + 0)
static const hint_t hints[] = {
    { "EACH BIRD IS A DEVICE NEAR YOU", LEARN_LISTEN },
    { "TURN THE KNOB FOR ANOTHER SONG", LEARN_TURN },
    { "TIP THE BOTTOM EDGE DOWN: LONGER CALLS", LEARN_TILT },
    { "STICK LEFT/RIGHT: PICK A BIRD", LEARN_JOY },
    { "HOLD REC TO LOOP THE CHORUS", LEARN_REC },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
    { "HOLD THE KNOB TO FREEZE THE TILT", LEARN_HOLD },
};

// ---------------------------------------------------------------- shared with the scenes
bird_t birds[MAXB];
int    spot = -1, mode, bird_count;
float  calls_per_s, adverts_per_s, hunt_trend;
bool   radio_failed;

// ---------------------------------------------------------------- the radio
SPSC(advq, ble_adv_t, 256)
static advq_t advq;
static void on_adv(const ble_adv_t *a) { advq_push(&advq, a); }   // the radio's task

static uint32_t rs = 0x9e3779b9u;
uint32_t chirp_rand(void) { return rng_next(&rs); }
static float frand(void) { return (float)(chirp_rand() >> 8) * (1.0f / 16777216.0f); }

// per bird, UI side only
typedef struct { float last_adv, rest_until, last_call; int phrase_left; uint32_t next_pulse; bool pulse_set; } bird_ui_t;
static bird_ui_t bu[MAXB];
static float now_s, tokens, len_mul = 1.0f, speed_mul = 1.0f, stick_dens = 1.0f, calls_acc, adv_acc, stat_t;
static int   lean_steps, greet_wait, last_mode = -1, last_cc_count = -1, last_cc_rate = -1;
static int   greet_q[8], ngreet;
static bool  flick_armed = true, ready;
static float hunt_near, hunt_tick_t, rssi_prev, listen_t;
static uint32_t frames_now;

// MIDI note-offs waiting
typedef struct { float t; uint8_t ch, note; } noff_t;
static noff_t noff[32];
static int nnoff;

static int degree_midi(int deg)
{
    const scale_t *s = &scales[(int)pv[P_SCALE]];
    int oct = deg >= 0 ? deg / s->n : -((-deg + s->n - 1) / s->n);
    int step = deg - oct * s->n;
    return 12 * ((int)pv[P_OCT] + 1) + (int)pv[P_KEY] + 12 * oct + s->st[step];
}

static int bird_midi(const bird_t *b, int extra)
{
    const scale_t *s = &scales[(int)pv[P_SCALE]];
    int span = s->n * (int)pv[P_SPREAD];
    int deg = (int)((b->who >> 8) % (uint32_t)span) + lean_steps + extra;
    if (b->cat == CAT_TRACKER || b->cat == CAT_WATCH) deg += s->n;            // small things sing high
    if (b->cat == CAT_SPEAKER || b->cat == CAT_LAPTOP) deg -= s->n;           // big things low
    int m = degree_midi(deg);
    while (m > 108) m -= 12;
    while (m < 36) m += 12;
    return m;
}

static uint8_t species_for(uint32_t who, uint8_t cat)
{
    static const uint8_t by_kind[CAT_N] = { SP_CHIRP, SP_WHISTLE, SP_TRILL, SP_PIP, SP_BELL, SP_WARBLE, SP_COO, SP_CHIRP };
    int v = (int)pv[P_VOICES];
    if (v == 0) { if (cat == CAT_OTHER) { static const uint8_t o[3] = { SP_CHIRP, SP_WARBLE, SP_WHISTLE }; return o[(who >> 3) % 3]; } return by_kind[cat]; }
    if (v == 9) return (uint8_t)((who >> 5) % SP_N);
    return (uint8_t)(v - 1);
}

static float near_of(float rssi)
{
    float n = pv[P_NEAR], f = pv[P_FAR];
    if (n <= f + 1) n = f + 1;
    return clampf((rssi - f) / (n - f), 0.0f, 1.0f);
}

// ---------------------------------------------------------------- calls
static float led_l[3];
static const uint8_t cat_rgb[CAT_N][3] = {
    { 255, 200, 40 }, { 40, 220, 255 }, { 60, 255, 90 }, { 255, 60, 200 }, { 60, 90, 255 }, { 230, 230, 230 }, { 255, 110, 30 }, { 150, 150, 150 },
};

static void midi_call(int cat, int note, float near, float len_s)
{
    int ch = (int)pv[CP(CP_MIDI_CH)];
    if (pv[P_MSPLIT] >= 1.0f && cat >= 0) ch = ((ch - 1 + cat) & 15) + 1;
    if (nnoff >= 32) { midi_note_ch(noff[0].ch, noff[0].note, 0); memmove(noff, noff + 1, sizeof noff[0] * 31); nnoff--; }
    midi_note_ch(ch, note, 20 + (int)(near * 107.0f));
    noff[nnoff++] = (noff_t){ now_s + fmaxf(len_s, 0.03f), (uint8_t)ch, (uint8_t)note };
}

static void flash_led(int cat, float near)
{
    float k = 0.3f + 0.7f * near;
    for (int j = 0; j < 3; j++) { float v = (float)cat_rgb[cat][j] * k; if (v > led_l[j]) led_l[j] = v; }
}

static void call(int i, int kind, uint32_t at, float lenf, int extra)
{
    bird_t *b = &birds[i];
    int note = bird_midi(b, extra);
    trig_t t = { .at = at, .hz = midi_hz((float)note), .near = b->near, .len = len_mul * pv[P_LEN] * lenf,
                 .species = b->species, .kind = (uint8_t)kind, .spot = (uint8_t)(i == spot ? pv[P_SPOT] : 0) };
    synth_trigger(&t);
    b->call_t = 0;
    scene_note(i, b->near);
    midi_call(b->cat, note, b->near, 0.2f * t.len);
    calls_acc += 1.0f;
    flash_led(b->cat, b->near);
    bu[i].last_call = now_s;
}

// ---------------------------------------------------------------- birds
static int alive_count(void) { int n = 0; for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving) n++; return n; }

static bool slot_used(int w, int s)
{
    for (int i = 0; i < MAXB; i++) if (birds[i].alive && birds[i].wire == w && birds[i].slot == s) return true;
    return false;
}

static void depart(int i, bool sing)
{
    bird_t *b = &birds[i];
    if (!b->alive || b->leaving) return;
    b->leaving = true; b->leave = 0;
    if (sing && pv[P_GREET] > 0 && ready) {
        trig_t t = { .at = frames_now, .hz = midi_hz((float)bird_midi(b, 0)), .near = b->near * pv[P_GREET] * 0.01f, .len = 1,
                     .species = b->species, .kind = TR_BYE };
        synth_trigger(&t);
    }
}

static int new_bird(const ble_adv_t *a)
{
    float nr = near_of((float)a->rssi);
    if (alive_count() >= (int)pv[P_MAXB]) {                 // full: the farthest makes room for a nearer one
        int f = -1; float fn = 2.0f;
        for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].near < fn) { fn = birds[i].near; f = i; }
        if (f < 0 || fn >= nr - 0.05f) return -1;
        depart(f, false);
    }
    int i = -1;
    for (int k = 0; k < MAXB; k++) if (!birds[k].alive) { i = k; break; }
    if (i < 0) return -1;
    int start = (int)(a->who % 3) * 8 + (int)((a->who >> 4) % 8), w = 0, s = 0;
    for (int k = 0; k < MAXB; k++) {
        int idx = (start + k) % MAXB;
        w = idx / 8; s = idx % 8;
        if (!slot_used(w, s)) break;
    }
    bird_t *b = &birds[i];
    memset(b, 0, sizeof *b);
    b->who = a->who; b->alive = true; b->cat = a->cat < CAT_N ? a->cat : CAT_OTHER;
    b->species = species_for(b->who, b->cat);
    b->wire = (uint8_t)w; b->slot = (uint8_t)s;
    b->rssi = (float)a->rssi; b->near = nr; b->born = now_s;
    b->midi = bird_midi(b, 0);
    bu[i] = (bird_ui_t){ .last_adv = now_s, .rest_until = now_s + 0.4f + frand(), .phrase_left = 1 + (int)(chirp_rand() % (uint32_t)pv[P_PHRASE]) };
    if (ngreet < 8) greet_q[ngreet++] = i;
    return i;
}

static int nearest(void)
{
    int k = -1; float best = -1;
    for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].near > best) { best = birds[i].near; k = i; }
    return k;
}

static int hunt_target(void) { return spot >= 0 && birds[spot].alive && !birds[spot].leaving ? spot : nearest(); }

static void step_spot(int d)
{
    // in the order they sit, left to right
    int order[MAXB], n = 0;
    for (int s = 0; s < 8; s++) for (int w = 0; w < 3; w++)
        for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].slot == s && birds[i].wire == w) order[n++] = i;
    if (!n) { spot = -1; return; }
    int at = -1;
    for (int k = 0; k < n; k++) if (order[k] == spot) at = k;
    at = at < 0 ? (d > 0 ? 0 : n - 1) : (at + d + n) % n;
    spot = order[at];
    char t[40]; snprintf(t, sizeof t, "%s  %d dBm", cat_name[birds[spot].cat], (int)lroundf(birds[spot].rssi));
    toast(t, 900);
}

static void heard(const ble_adv_t *a)
{
    adv_acc += 1.0f;
    int i = -1;
    for (int k = 0; k < MAXB; k++) if (birds[k].alive && birds[k].who == a->who) { i = k; break; }
    if (i < 0) { i = new_bird(a); if (i < 0) return; }
    bird_t *b = &birds[i];
    bird_ui_t *u = &bu[i];
    if (b->leaving) {                                           // it spoke again before it was gone:
        if (alive_count() >= (int)pv[P_MAXB]) return;            // back on its wire only if there is room
        b->leaving = false; b->leave = 0;
    }
    float dt = now_s - u->last_adv;
    if (b->adverts > 0 && dt > 0.015f) {                        // one advert is sent on three channels: count the event once
        float est = b->interval, smp = dt;
        if (est > 0) { int k = (int)(dt / est + 0.5f); if (k >= 2 && fabsf(dt / (float)k - est) < 0.3f * est) smp = dt / (float)k; }
        b->interval = clampf(est > 0 ? est + 0.2f * (smp - est) : smp, 0.02f, 10.0f);
    }
    if (dt > 0.015f || !b->adverts) u->last_adv = now_s;
    b->adverts++; b->seen = 0;
    b->rssi = b->adverts == 1 ? (float)a->rssi : b->rssi + 0.25f * ((float)a->rssi - b->rssi);
    b->near = near_of(b->rssi);
    if (b->arrive < 1.0f) return;                               // still flying in
    switch (mode) {
    case MODE_CHORUS:
        if (now_s >= u->rest_until && tokens >= 1.0f) {
            tokens -= 1.0f;
            call(i, TR_CALL, frames_now + (chirp_rand() % 160u), 1.0f, 0);
            float gap = pv[P_GAP] / speed_mul;
            if (--u->phrase_left > 0) u->rest_until = now_s + gap * (0.25f + 0.2f * frand());
            else { u->rest_until = now_s + gap * (1.5f + 2.5f * frand()); u->phrase_left = 1 + (int)(chirp_rand() % (uint32_t)pv[P_PHRASE]); }
        }
        break;
    case MODE_SWARM:
        if (tokens >= 0.25f) {
            tokens -= 0.25f;
            const scale_t *s = &scales[(int)pv[P_SCALE]];
            int deg = (int)(chirp_rand() % (uint32_t)(s->n * ((int)pv[P_SPREAD] + 1))) + s->n + lean_steps;
            trig_t t = { .at = frames_now + chirp_rand() % 320u, .hz = midi_hz((float)degree_midi(deg)), .near = b->near,
                         .len = len_mul * pv[P_LEN], .kind = TR_GRAIN };
            synth_trigger(&t);
            b->call_t = 0; calls_acc += 1.0f;
            flash_led(b->cat, b->near * 0.5f);
        }
        break;
    case MODE_HUNT:
        if (i == hunt_target() && now_s >= u->rest_until) { call(i, TR_CALL, frames_now, 0.6f, 0); u->rest_until = now_s + 0.7f; }
        break;
    default: break;
    }
}

// ---------------------------------------------------------------- the clocks
static const int mults[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
static void clocks(void)
{
    const uint32_t ahead = 640;                                  // scheduled 20 ms ahead, played on the sample
    bool quant = pv[P_QUANT] >= 1.0f;
    static const int per_beat[4] = { 8, 4, 2, 1 };
    float unit = (float)AUDIO_SR * 60.0f / pv[P_BPM] / (float)per_beat[(int)pv[P_GRID]];
    for (int i = 0; i < MAXB; i++) {
        bird_t *b = &birds[i];
        bird_ui_t *u = &bu[i];
        if (!b->alive || b->leaving || b->interval <= 0 || b->arrive < 1.0f) { u->pulse_set = false; continue; }
        float per = b->interval * pv[P_RATE] / speed_mul * (float)AUDIO_SR;
        if (per < 1600.0f) per = 1600.0f;                        // 50 ms at the fastest
        int m = 1;
        if (quant) {
            float best = 1e9f;
            for (int k = 0; k < (int)(sizeof mults / sizeof mults[0]); k++) { float d = fabsf(logf((float)mults[k] * unit / per)); if (d < best) { best = d; m = mults[k]; } }
            per = (float)m * unit;
        }
        if (!u->pulse_set || (int32_t)(u->next_pulse - frames_now) < -(int32_t)ahead || (int32_t)(u->next_pulse - frames_now) > (int32_t)(per * 2.0f)) {
            if (quant) {
                uint32_t off = (b->who >> 12) % (uint32_t)m;
                float k = ceilf(((float)frames_now + (float)ahead - (float)off * unit) / per);
                u->next_pulse = (uint32_t)(k * per + (float)off * unit);
            } else u->next_pulse = frames_now + ahead + (uint32_t)(frand() * per);
            u->pulse_set = true;
        }
        if ((int32_t)(u->next_pulse - frames_now) < (int32_t)ahead) {
            uint32_t at = u->next_pulse;
            if (quant && ((int)lroundf((float)at / unit) & 1)) at += (uint32_t)(pv[P_SWING] * 0.01f * unit * 0.5f);
            call(i, TR_PULSE, at, pv[P_PULSE] * 0.01f, 0);
            u->next_pulse += (uint32_t)per;
        }
        b->phase = clampf(1.0f - (float)(int32_t)(u->next_pulse - frames_now) / per, 0.0f, 1.0f);
    }
}

// ---------------------------------------------------------------- the UI side
static void apply(void)
{
    S.sweep = pv[P_SWEEP] * 0.01f; S.bright = pv[P_BRIGHT] * 0.01f; S.reverb = pv[P_VERB] * 0.01f;
    S.room = pv[P_ROOM]; S.dist = pv[P_DIST] * 0.01f; S.air = pv[P_AIR] * 0.01f;
}

static float stick_tone = 1.0f;
static void changed(int p)
{
    apply();                                                     // plain floats: safe before init()
    if (p == P_VOICES || p < 0) for (int i = 0; i < MAXB; i++) if (birds[i].alive) birds[i].species = species_for(birds[i].who, birds[i].cat);
    if (p == P_MODE || p < 0) for (int i = 0; i < MAXB; i++) bu[i].pulse_set = false;
}

static void action(int p) { if (p == P_NEXT) step_spot(1); }

static void init(void)
{
    synth_init();
    apply();
    radio_failed = !ble_start(on_adv);
    if (radio_failed) app_log("Bluetooth would not start");
    ready = true;
}

static void tick(const ctl_t *c)
{
    float dt = c->dt;
    now_s += dt; frames_now = c->frames;
    ble_tick();
    mode = (int)pv[P_MODE];
    if (mode != last_mode) { last_mode = mode; hunt_trend = 0; hunt_tick_t = 0.08f; for (int i = 0; i < MAXB; i++) bu[i].pulse_set = false; }

    // motion
    float ly = c->has_imu ? clampf(c->lean_y / 30.0f, -1.0f, 1.0f) : 0.0f;
    int tilt = (int)pv[P_TILT];
    len_mul = tilt == 0 ? exp2f(ly * 1.2f) : 1.0f;
    speed_mul = tilt == 1 ? exp2f(ly * 1.5f) : 1.0f;
    if (c->has_imu && pv[P_LEAN] > 0) {
        float want = clampf(c->lean_x / 35.0f, -1.0f, 1.0f) * pv[P_LEAN];
        if (fabsf(want - (float)lean_steps) > 0.7f) lean_steps = (int)lroundf(want);
    } else lean_steps = 0;
    // the stick: left / right picks a bird, up / down per STICK Y
    stick_dens = 1.0f; stick_tone = 1.0f;
    if (c->has_joy) {
        if (flick_armed && fabsf(c->jx) > 0.7f) { step_spot(c->jx > 0 ? 1 : -1); flick_armed = false; }
        else if (fabsf(c->jx) < 0.3f) flick_armed = true;
        if (pv[P_STICK] == 0) stick_tone = exp2f(c->jy * 2.5f);
        else if (pv[P_STICK] == 1) stick_dens = exp2f(c->jy * 2.0f);
    }
    S.tone = clampf(pv[P_TONE] * stick_tone, 300.0f, 18000.0f);

    float dens = pv[P_DENS] * stick_dens;
    tokens += dens * dt;
    if (tokens > 2.0f + dens * 0.2f) tokens = 2.0f + dens * 0.2f;

    ble_adv_t a;
    int k = 0;
    while (k++ < 128 && advq_pop(&advq, &a)) heard(&a);

    // the birds: arrive, age, fly off
    int alive = 0;
    for (int i = 0; i < MAXB; i++) {
        bird_t *b = &birds[i];
        if (!b->alive) continue;
        b->seen += dt; b->call_t += dt;
        if (b->arrive < 1.0f) { b->arrive += dt / 0.9f; if (b->arrive > 1.0f) b->arrive = 1.0f; }
        b->near = near_of(b->rssi);
        b->midi = bird_midi(b, 0);
        if (!b->leaving && b->seen > pv[P_FORGET]) depart(i, true);
        if (b->leaving) { b->leave += dt / 1.2f; if (b->leave >= 1.0f) { b->alive = false; if (spot == i) spot = -1; continue; } }
        else alive++;
    }
    bird_count = alive;
    // MAX BIRDS lowered: the farthest leave, one a tick
    if (alive > (int)pv[P_MAXB]) {
        int f = -1; float fn = 2.0f;
        for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].near < fn) { fn = birds[i].near; f = i; }
        if (f >= 0) depart(f, false);
    }
    // one hello at a time, so a crowd arriving at once comes in as a flock, not a burst
    if (greet_wait > 0) greet_wait--;
    else if (ngreet > 0) {
        int i = greet_q[0];
        memmove(greet_q, greet_q + 1, sizeof greet_q[0] * (size_t)(ngreet - 1)); ngreet--;
        if (birds[i].alive && !birds[i].leaving && pv[P_GREET] > 0) {
            bird_t *b = &birds[i];
            trig_t t = { .at = frames_now, .hz = midi_hz((float)bird_midi(b, 0)), .near = b->near * pv[P_GREET] * 0.01f, .len = 1,
                         .species = b->species, .kind = TR_HELLO };
            synth_trigger(&t);
            scene_note(i, b->near);
            flash_led(b->cat, b->near);
            greet_wait = 30;
        }
    }

    if (mode == MODE_CLOCKS) clocks();
    if (mode == MODE_HUNT) {
        int tg = hunt_target();
        static int last_tg = -1, hist_w;
        static float hist[100], slow;                             // the last second of a slow RSSI, one a tick
        if (tg >= 0) {
            float r = birds[tg].rssi;
            if (tg != last_tg || rssi_prev == 0) { slow = r; for (int k = 0; k < 100; k++) hist[k] = r; last_tg = tg; }
            rssi_prev = r;
            slow += dt * (r - slow) / 1.5f;                       // 1.5 s: a step is not a walk
            hunt_trend = slow - hist[hist_w];                     // dB over the last second
            hist[hist_w] = slow; hist_w = (hist_w + 1) % 100;
            hunt_near += 0.05f * (birds[tg].near - hunt_near);
            hunt_tick_t -= dt;
            if (hunt_tick_t <= 0) {
                float rate = 0.7f + 16.0f * hunt_near * hunt_near;
                hunt_tick_t += 1.0f / rate;
                if (hunt_tick_t < 0) hunt_tick_t = 0;
                trig_t t = { .at = frames_now, .hz = midi_hz((float)degree_midi((int)lroundf(hunt_near * 10.0f) + lean_steps)),
                             .near = 0.4f + 0.6f * hunt_near, .len = 1, .kind = TR_GEIGER };
                synth_trigger(&t);
                birds[tg].call_t = 0.05f;
                flash_led(birds[tg].cat, 0.2f + 0.5f * hunt_near);
            }
        } else { hunt_near *= 0.98f; rssi_prev = 0; hunt_trend = 0; }
    }
    S.swarm_air = clampf(adverts_per_s / 150.0f, 0.0f, 1.0f);

    // numbers twice a second: the screen, MIDI CC 3 (birds) and 9 (calls a second)
    stat_t += dt;
    if (stat_t >= 0.5f) {
        calls_per_s += 0.5f * (calls_acc / stat_t - calls_per_s);
        adverts_per_s += 0.5f * (adv_acc / stat_t - adverts_per_s);
        calls_acc = adv_acc = 0; stat_t = 0;
        int cc = bird_count * 5 > 127 ? 127 : bird_count * 5, cr = calls_per_s > 127 ? 127 : (int)calls_per_s;
        if (cc != last_cc_count) { midi_cc(3, cc); last_cc_count = cc; }
        if (cr != last_cc_rate) { midi_cc(9, cr); last_cc_rate = cr; }
    }
    // MIDI note-offs that are due
    for (int i = 0; i < nnoff; ) {
        if (noff[i].t <= now_s) { midi_note_ch(noff[i].ch, noff[i].note, 0); noff[i] = noff[--nnoff]; } else i++;
    }
    // the LED: a flash in the colour of the kind that called, a faint glow for the crowd
    for (int j = 0; j < 3; j++) led_l[j] *= 0.88f;
    float glow = (float)bird_count * 1.2f;
    led_rgb((uint8_t)fminf(255.0f, led_l[0]), (uint8_t)fminf(255.0f, led_l[1] + glow), (uint8_t)fminf(255.0f, led_l[2] + glow * 0.5f));
    if (bird_count > 0 && (listen_t += dt) > 45.0f) coach_done(LEARN_LISTEN);
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0 || pv[P_MIN] < 1.0f) return;
    int v = (int)pv[P_VOICES];
    trig_t t = { .at = frames_now, .hz = midi_hz((float)d1), .near = (float)d2 / 127.0f, .len = len_mul * pv[P_LEN],
                 .species = (uint8_t)(v == 0 || v == 9 ? SP_CHIRP : v - 1), .kind = TR_CALL };
    synth_trigger(&t);
    flash_led(CAT_OTHER, t.near);
}

static void audio(const float *in, float *out, int n) { synth_audio(in, out, n); }
static void draw(const view_t *v) { scene_draw(v); }
static bool splash(int f) { return scene_splash(f); }
static void saver(uint32_t f) { scene_saver(f); }

const instrument_t INST = {
    .name = "CHIRP", .tagline = "THE BLUETOOTH AROUND YOU",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 5,
    .presets = presets, .npresets = N_PRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true, .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};
