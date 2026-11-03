// SIFT: a granular synthesizer and recorder. Whole on one side, in pieces on the other. inst_sift.h says how it
// works; this file is the UI side: the params and sounds, the material (Daisy Bell, the Amen, the starter, a bank,
// what you record), REC (hold to record, tap and it keeps listening), the tilt (DECOMPOSE: the roll pushes it apart
// and back, the pitch moves the head; MAP: the cursor), your voice or the stick as the cursor, the shake, the catch
// (which grains the cursor holds, in what order, tuned to what), the link (tempo and key shared with the flock),
// loading and saving, MIDI and the LED.
#include "core.h"
#include "dsp.h"
#include "link.h"
#include "inst_sift.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "GRAINS", "DECOMPOSE", "DIRT", "ORDER", "CHAOS", "MAP", "TAKE", "SPACE", "SONG" };
enum { G_GRAINS, G_DECOMP, G_DIRT, G_ORDER, G_CHAOS, G_MAP, G_TAKE, G_SPACE, G_SONG };
static const char *const mode_l[] = { "DECOMPOSE", "MAP" };
static const char *const shape_l[] = { "SOFT", "PLUCK", "SWELL", "SLICE", "HARD" };
static const char *const step_l[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
static const int step_div[] = { 1, 2, 3, 4, 6, 8 };
static const char *const pattern_l[] = { "UP", "DOWN", "BOUNCE", "AS PLAYED", "SHUFFLE" };
static const char *const tune_l[] = { "OFF", "ALL NOTES", "SCALE", "PENTATONIC" };
static const char *const cursor_l[] = { "TILT", "VOICE", "STICK" };
static const char *const axis_l[] = { "TIME", "TONE", "BRIGHT", "LOUD", "NOISE" };
static const char *const source_l[] = { "MIC", "USB", "ITSELF", "MIC+ITSELF" };
static const char *const takes_l[] = { "ADD", "NEW" };
static const char *const bank_l[NBANK] = { "DAISY", "AMEN", "STARTER", "SIFT 1", "SIFT 2", "SIFT 3", "SIFT 4",
                                           "SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4", "SLOT 5", "SLOT 6", "SLOT 7", "SLOT 8" };
static const char *const matl_l[] = { "KEEP", "DAISY", "AMEN", "STARTER" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_l[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };   // SING's, as the link numbers them
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
static const char *const off_on[] = { "OFF", "ON" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };
#define SYS (PF_SYS | PF_NOCC)

static const param_t params[P_N] = {
    [P_MODE]    = { "MODE", "mode", G_GRAINS, PK_ENUM, 0, 0, 1, 1, MODE_DECOMP, "", mode_l, "IT FALLS APART, OR A MAP" },
    [P_CHAOS]   = { "CHAOS", "chao", G_GRAINS, PK_NUM, PF_INT, 0, 100, 2, 0, "%", NULL, "0 WHOLE, 100 IN PIECES" },
    [P_SIZE]    = { "SIZE", "size", G_GRAINS, PK_NUM, PF_LOG, 0.005f, 0.8f, 1.1f, 0.09f, "s", NULL, "HOW LONG EACH GRAIN IS" },
    [P_SHAPE]   = { "SHAPE", "shap", G_GRAINS, PK_ENUM, 0, 0, NSHAPE - 1, 1, SH_SOFT, "", shape_l, "IN MAP: EACH GRAIN'S SHAPE" },
    [P_PITCH]   = { "PITCH", "ptch", G_GRAINS, PK_NUM, PF_INT | PF_SIGNED, -24, 24, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_DETUNE]  = { "DRIFT", "drft", G_GRAINS, PK_NUM, PF_INT, 0, 100, 5, 10, "", NULL, "CENTS EACH GRAIN WANDERS" },
    [P_LEVEL]   = { "LEVEL", "levl", G_GRAINS, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD" },
    [P_SPEED]   = { "SPEED", "sped", G_DECOMP, PK_NUM, PF_INT | PF_SIGNED, -100, 200, 5, 100, "%", NULL, "HOW FAST IT MOVES ON" },
    [P_STUTTER] = { "STUTTER", "stut", G_DECOMP, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SYLLABLES CAUGHT, REPEATED" },
    [P_JUMPS]   = { "JUMPS", "jump", G_DECOMP, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW FAR PIECES COME FROM" },
    [P_GAPS]    = { "GAPS", "gaps", G_DECOMP, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HOLES IN IT" },
    [P_BITE]    = { "BITE", "bite", G_DECOMP, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW VIOLENT THE END GETS" },
    [P_DRIVE]   = { "DRIVE", "drve", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DISTORTION" },
    [P_CRUSH]   = { "CRUSH", "crsh", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "FEWER BITS, A LOWER RATE" },
    [P_FEEDBACK]= { "FEEDBACK", "fdbk", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "ITS SOUND BACK INTO ITSELF" },
    [P_FBTIME]  = { "FB TIME", "fbtm", G_DIRT, PK_NUM, PF_LOG, 0.02f, 1.0f, 1.1f, 0.18f, "s", NULL, "HOW LONG THE LOOP IS" },
    [P_ROT]     = { "ROT", "rotw", G_DIRT, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DAMAGE KEPT IN THE SOUND" },
    [P_STEP]    = { "GRID", "grid", G_ORDER, PK_ENUM, 0, 0, 5, 1, 3, "", step_l, "A GRAIN ON EVERY STEP OF IT" },
    [P_STEPS]   = { "NOTES", "nseq", G_ORDER, PK_NUM, PF_INT, 1, 16, 1, 6, "", NULL, "GRAINS IN THE PATTERN" },
    [P_PATTERN] = { "PATTERN", "patt", G_ORDER, PK_ENUM, 0, 0, 4, 1, PAT_UP, "", pattern_l, "THE ORDER THEY PLAY IN" },
    [P_STACK]   = { "STACK", "stak", G_ORDER, PK_NUM, PF_INT, 1, 4, 1, 1, "", NULL, "GRAINS AT ONCE: A CHORD" },
    [P_TUNE]    = { "TUNE TO", "tune", G_ORDER, PK_ENUM, 0, 0, 3, 1, TUNE_SCALE, "", tune_l, "NOTES THE GRAINS LAND ON" },
    [P_DENSITY] = { "DENSITY", "dens", G_CHAOS, PK_NUM, PF_LOG, 2, 200, 1.1f, 40, "/s", NULL, "GRAINS A SECOND IN CHAOS" },
    [P_SPREAD]  = { "SPREAD", "sprd", G_CHAOS, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "HOW FAR THE CURSOR REACHES" },
    [P_SCATTER] = { "SCATTER", "scat", G_CHAOS, PK_NUM, PF_INT, 0, 24, 1, 7, "st", NULL, "PITCHES THROWN ABOUT" },
    [P_REVERSE] = { "REVERSE", "revs", G_CHAOS, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "GRAINS PLAYED BACKWARDS" },
    [P_SHAKE]   = { "SHAKE", "shak", G_CHAOS, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "SHAKE IT: HOW WILD IT GETS" },
    [P_CURSOR]  = { "CURSOR", "curs", G_MAP, PK_ENUM, 0, 0, 2, 1, CUR_TILT, "", cursor_l, "WHAT MOVES THE CURSOR" },
    [P_MAPX]    = { "ACROSS", "mapx", G_MAP, PK_ENUM, 0, 0, ND - 1, 1, D_TONE, "", axis_l, "LEFT TO RIGHT ON THE MAP" },
    [P_MAPY]    = { "UP", "mapy", G_MAP, PK_ENUM, 0, 0, ND - 1, 1, D_NOISE, "", axis_l, "BOTTOM TO TOP ON THE MAP" },
    [P_TRANGE]  = { "TILT RANGE", "trng", G_MAP, PK_NUM, PF_INT | PF_SYS, 10, 60, 5, 30, "", NULL, "DEGREES TO REACH AN EDGE" },
    [P_MATCH]   = { "SING ALONG", "mtch", G_MAP, PK_ENUM, 0, 0, 1, 1, 1, "", off_on, "GRAINS TAKE YOUR NOTE" },
    [P_SOURCE]  = { "RECORD", "srce", G_TAKE, PK_ENUM, PF_SYS, 0, 3, 1, SRC_MIC, "", source_l, "WHAT REC RECORDS" },
    [P_TAKES]   = { "TAKES", "tkmd", G_TAKE, PK_ENUM, PF_SYS, 0, 1, 1, TK_ADD, "", takes_l, "ADD ON, OR START OVER" },
    [P_BANK]    = { "BANK", "bank", G_TAKE, PK_ENUM, SYS, 0, NBANK - 1, 1, BK_DAISY, "", bank_l, "WHAT LOAD AND SAVE USE" },
    [P_LOAD]    = { "LOAD", "load", G_TAKE, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BANK'S SOUND, SIFTED" },
    [P_SAVE]    = { "SAVE", "save", G_TAKE, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "EVERY GRAIN INTO THE BANK" },
    [P_CLEAR]   = { "CLEAR", "clr ", G_TAKE, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "NO GRAINS: RECORD NEW ONES" },
    [P_TONE]    = { "TONE", "tone", G_SPACE, PK_NUM, PF_INT | PF_SIGNED, -100, 100, 5, 0, "%", NULL, "DOWN: DARK  UP: THIN" },
    [P_SPACE]   = { "SPACE", "spce", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "HOW MUCH ROOM ROUND IT" },
    [P_TAIL]    = { "TAIL", "tail", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "A SMALL ROOM TO ENDLESS" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_SONG, PK_NUM, PF_INT | PF_SYS, 50, 200, 1, 100, "", NULL, "SHARED WITH LINKED BOARDS" },
    [P_KEY]     = { "KEY", "skey", G_SONG, PK_ENUM, PF_SYS, 0, 11, 1, 0, "", key_l, "THE GRAINS ARE IN THIS KEY" },
    [P_SCALE]   = { "SCALE", "sscl", G_SONG, PK_ENUM, PF_SYS, 0, NSCALES - 1, 1, 0, "", scale_l, "ITS NOTES" },
    [P_LINK]    = { "LINK", "nlnk", G_SONG, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]   = { "GROUP", "ngrp", G_SONG, PK_NUM, SYS | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]   = { "RANGE", "npow", G_SONG, PK_ENUM, SYS, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]   = { "WHO IS THERE", "nwho", G_SONG, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
    [P_MATL]    = { "MATERIAL", "matl", G_TAKE, PK_ENUM, PF_HIDE | PF_NOCC, 0, 3, 1, MAT_KEEP, "", matl_l, "" },
};

// ---------------------------------------------------------------- sounds
static const preset_t presets[] = {
    // DECOMPOSE: the head through the recording, coming apart
    { "DAISY",        "matl=1 chao=0 spce=20 tail=40" },
    { "DAISY ROT",    "matl=1 chao=25 rotw=35 fdbk=25 fbtm=0.3 bite=80 drve=20 spce=30" },
    { "HAL",          "matl=1 sped=45 ptch=-5 chao=8 drve=10 spce=25 tail=50" },
    { "TONGUES",      "chao=36 stut=95 jump=40 gaps=30 spce=20" },
    { "GLOBALALIA",   "chao=58 jump=100 stut=40 scat=5 sprd=70 spce=25" },
    { "SHRED",        "chao=82 bite=100 drve=50 crsh=25 gaps=60 spce=15" },
    { "DUST TO DUST", "chao=100 dens=200 bite=60 spce=50 tail=70" },
    { "FREEZE FRAME", "sped=0 chao=10 spce=50 tail=70" },
    { "BACKWARDS",    "sped=-100 chao=12 spce=30" },
    { "FEEDBACK",     "chao=30 fdbk=85 fbtm=0.09 drve=35 bite=60 spce=10" },
    { "AMEN",         "matl=2 chao=0 spce=10" },
    { "AMEN SHRED",   "matl=2 chao=60 bite=90 drve=40 stut=80 spce=10" },
    // MAP: the grains by what they sound like
    { "MAP",          "mode=1 chao=30" },
    { "LATTICE",      "mode=1 chao=0 shap=1 size=0.12 grid=3 nseq=8 patt=2 spce=30" },
    { "MUSIC BOX",    "mode=1 chao=4 shap=1 size=0.2 ptch=12 grid=1 nseq=5 spce=55 tail=70 tone=20" },
    { "CHORDS",       "mode=1 chao=8 size=0.35 grid=1 stak=4 nseq=8 drft=15 spce=55 tail=70" },
    { "CHOIR",        "mode=1 chao=22 size=0.45 dens=30 grid=1 stak=3 drft=25 sprd=25 spce=60 tail=80 scat=0 revs=10" },
    { "FREEZE",       "mode=1 chao=15 size=0.7 dens=40 sprd=5 grid=0 drft=15 spce=70 tail=85 scat=0 revs=0" },
    { "TAPE",         "mode=1 mapx=0 mapy=3 chao=20 shap=3 size=0.12 nseq=1 sprd=10 spce=20" },
    { "STUTTER",      "mode=1 chao=0 shap=3 size=0.06 grid=5 nseq=2 patt=3" },
    { "MOSAIC",       "mode=1 curs=1 mtch=1 chao=12 size=0.08 grid=5 nseq=3 sprd=20 spce=30" },
    { "RAIN",         "mode=1 chao=70 shap=1 size=0.03 dens=70 sprd=70 scat=5 spce=60 tail=60 tone=10" },
    { "GLITCH",       "mode=1 chao=60 shap=3 size=0.05 grid=4 dens=50 revs=60 scat=12 sprd=50" },
    { "SWARM",        "mode=1 chao=85 size=0.04 dens=140 sprd=60 scat=12 revs=40" },
    { "DUST",         "mode=1 chao=100 size=0.008 dens=200 sprd=90 scat=24 revs=50 tone=40 spce=40" },
    { "UNDERWATER",   "mode=1 chao=40 size=0.3 ptch=-12 tone=-60 dens=30 spce=80 tail=75" },
    { "STARS",        "mode=1 chao=50 shap=1 size=0.15 ptch=24 dens=15 scat=12 spce=90 tail=90" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { L_LATCH = LEARN_INST, L_SHAKE, L_VOICE, L_SPEED };
static const hint_t hints[] = {
    { "TILT IT RIGHT: IT FALLS APART", LEARN_TILT },
    { "TURN THE KNOB: WHOLE TO PIECES", LEARN_TURN },
    { "TIP IT TOWARDS YOU: TIME SLOWS", L_SPEED },
    { "HOLD REC AND MAKE A SOUND: NEW GRAINS", LEARN_REC },
    { "TAP REC: IT KEEPS LISTENING", L_LATCH },
    { "SHAKE THE BOX TO SCATTER THEM", L_SHAKE },
    { "HOLD THE KNOB: THE TILT STAYS PUT", LEARN_HOLD },
    { "MAP > CURSOR > VOICE: SING TO PLAY", L_VOICE },
    { "PRESS THE KNOB: SOUNDS AND SETTINGS", LEARN_MENU },
};

// ---------------------------------------------------------------- state
sift_ui_t S;
uint32_t  sift_now;
static float cur_x = 0.5f, cur_y = 0.5f;          // the cursor, smoothed
static float shake, push_s;
static int   burst_ms = 1000;
static float voice_note = -1.0f, voice_gate;      // CURSOR VOICE: your note (-1: none) and the cloud's level
static int   midi_held = -1;                      // a MIDI note held: the order side sounds at it
static bool  applying;                            // a param set by the link, not by the player
static bool  link_up, booted, rotted;
static int   rot_unit = -1;                       // ROT: the unit the head was in (it is listened to again once left)
int   sift_peers(void) { return S.peers; }

// the listening queue: units of a take (recording, loading or the starter) waiting to be measured
static struct { bool on; int take, first, n, done; bool closing; } Q;
// REC
static struct { bool active, ignore_release; uint32_t seq; uint32_t t0; } R;
// jobs
static struct { int ms, i, n, bank; int16_t order[NU_MAX]; } J;

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }

float sift_beat_phase(void)
{
    uint32_t o; float f; link_origin(&o, &f);
    uint32_t fpb = link_fpb(); if (!fpb) return 0.0f;
    int32_t m = (int32_t)(sift_now - o) % (int32_t)fpb; if (m < 0) m += (int32_t)fpb;
    float p = ((float)m - f) / (float)fpb;
    return p < 0.0f ? p + 1.0f : p;
}

// ---------------------------------------------------------------- the listening queue
static void q_flush(void)
{
    while (Q.on && Q.done < Q.n) { map_listen((Q.first + Q.done) % corp_units, Q.take); Q.done++; }
    if (Q.on) { map_take_end(Q.take); Q.on = false; }
}
static void q_open(int first, bool closing, int n)
{
    q_flush();
    Q.on = true; Q.take = map_take_begin(); Q.first = first; Q.n = n; Q.done = 0; Q.closing = closing;
}
static void q_tick(void)
{
    if (!Q.on) { S.pending = 0; return; }
    for (int b = 0; b < 4 && Q.done < Q.n; b++) { map_listen((Q.first + Q.done) % corp_units, Q.take); Q.done++; }
    S.pending = Q.n - Q.done;
    if (Q.closing && Q.done >= Q.n) { map_take_end(Q.take); Q.on = false; }
}

static void set_head(uint32_t frames) { if (corp_frames) eng_wr = (frames % (uint32_t)corp_frames + UNIT - 1) / UNIT * UNIT % (uint32_t)corp_frames; }

// after a take: what follows it, where nothing is recorded, is silence (a long grain from its last moments reads on)
static void quiet_after(int u_end)
{
    for (int k = 0; k < 54; k++) {
        int u = (u_end + k) % corp_units;
        if (units[u].state != U_EMPTY) break;
        memset(corp + (size_t)u * UNIT, 0, UNIT * sizeof(int16_t));
    }
}

// ---------------------------------------------------------------- the material
static void starter(void)
{
    if (!corp) return;
    q_flush();
    map_reset();
    int n = seed_render(corp, corp_frames);
    int units_n = (n + UNIT - 1) / UNIT;
    memset(corp + n, 0, (size_t)(corp_frames - n) * sizeof(int16_t));
    set_head((uint32_t)(units_n * UNIT));
    q_open(0, true, units_n);
    S.material = MAT_STARTER; rotted = false;
}

// a built-in: the starter at once; Daisy and the Amen are made a slice a tick (JOB_RENDER)
static void material_load(int bank)
{
    if (!corp || S.job != JOB_NONE) return;
    if (S.rec != REC_IDLE) { eng_rec_stop(); S.rec = REC_IDLE; }
    q_flush();
    eng_hush();
    if (bank == BK_STARTER) { starter(); return; }
    map_reset();
    S.job = JOB_RENDER; S.job_what = bank; S.job_frac = 0.0f; J.ms = 0;
}

static void job_load(int store_bank);
static void load_bank(int bank)
{
    if (bank < NBUILTIN) { material_load(bank); return; }
    int sb = bank - NBUILTIN;
    if (store_has(sb)) job_load(sb);
    else { char b[32]; snprintf(b, sizeof b, "%s IS EMPTY", bank_l[bank]); toast(b, 1200); }
}

int sift_daisy_syl_of(uint32_t pos)
{
    if (S.material != MAT_DAISY) return -1;
    if ((int32_t)pos < daisy_syl_at(0)) return -1;
    int j = 0;
    while (j + 1 < DAISY_NSYL && (int32_t)pos >= daisy_syl_at(j + 1)) j++;
    return (int32_t)pos < daisy_length() ? j : -1;
}

// ---------------------------------------------------------------- REC: hold to record, tap and it keeps listening
static void rec_begin(void)
{
    if (!corp || S.job != JOB_NONE) return;
    bool fresh = pv[P_TAKES] >= 1.0f;
    q_flush();
    uint32_t from = fresh ? 0 : (eng_wr + UNIT - 1) / UNIT * UNIT % (uint32_t)corp_frames;   // a unit boundary (a take still closing is not padded yet)
    if (fresh) map_reset();
    int u0 = (int)(from / UNIT);
    for (int k = 0; k < 3; k++) map_forget((u0 + k) % corp_units);
    R.seq = eng_take_seq + 1;
    R.active = true; R.t0 = sift_now; S.rec_s = 0.0f;
    Q.on = true; Q.take = map_take_begin(); Q.first = u0; Q.n = 0; Q.done = 0; Q.closing = false;
    eng_rec_start(from, fresh, (int)pv[P_SOURCE]);
    S.rec = REC_HOLD;
    S.material = MAT_KEEP;                                              // yours now (on top of what was there)
}
static void rec_end(void) { if (S.rec != REC_IDLE) eng_rec_stop(); S.rec = REC_IDLE; }

static void rec_logic(const ctl_t *c)
{
    if (c->rec_press) {
        if (S.rec == REC_LIVE) { rec_end(); R.ignore_release = true; return; }
        if (S.job != JOB_NONE) { toast("BUSY: WAIT A MOMENT", 900); R.ignore_release = true; return; }
        rec_begin();
    }
    if (c->rec_release) {
        if (R.ignore_release) { R.ignore_release = false; return; }
        if (S.rec == REC_HOLD) {
            if (c->rec_ms < 300) { S.rec = REC_LIVE; coach_done(L_LATCH); toast("LISTENING: TAP REC TO STOP", 1300); }
            else rec_end();
        }
    }
    // the take as the audio task writes it: listen to each finished unit, keep the next ones off the map
    if (R.active && eng_take_seq == R.seq) {
        Q.n = eng_take_units;
        if (eng_rec_state != RS_OFF) {
            int hu = (int)(eng_wr / UNIT);
            for (int k = 0; k < 3; k++) map_forget((hu + k) % corp_units);
            S.rec_s = (float)(sift_now - R.t0) / (float)AUDIO_SR;
        } else {
            Q.closing = true; R.active = false;
            if (S.rec != REC_IDLE) S.rec = REC_IDLE;
            if (eng_rec_full) toast("FULL: 20 S OF GRAINS", 1200);
            q_flush();
            quiet_after((int)(eng_wr / UNIT) % corp_units);
        }
    }
}

// ---------------------------------------------------------------- loading and saving
static void job_load(int store_bank)
{
    if (!corp || S.job != JOB_NONE) return;
    rec_end();
    q_flush();
    eng_hush();
    map_reset();
    S.job = JOB_LOAD; J.ms = 0; J.bank = store_bank; J.n = 0;
}

static int by_time(const void *a, const void *b)
{
    float x = units[*(const int16_t *)a].d[D_TIME], y = units[*(const int16_t *)b].d[D_TIME];
    return x < y ? -1 : x > y;
}

static void job_save(int bank)
{
    if (S.job != JOB_NONE || !corp) return;
    int sb = bank - NBUILTIN;
    if (sb < 0 || sb >= NBANK_OWN) { toast("SAVE GOES TO SIFT 1 TO 4", 1400); return; }
    if (S.rec != REC_IDLE || R.active) { toast("WAIT FOR THE TAKE", 1000); return; }
    q_flush();                                                          // what is still being listened to, at once
    J.n = 0;
    for (int u = 0; u < corp_units; u++) if (units[u].state != U_EMPTY) J.order[J.n++] = (int16_t)u;
    if (!map_count()) { toast("NOTHING TO SAVE", 1000); return; }
    qsort(J.order, (size_t)J.n, sizeof J.order[0], by_time);
    if (!store_save_begin(sb)) { toast("CANNOT SAVE", 1200); return; }
    store_mute(true);
    S.job = JOB_SAVE; J.ms = 0; J.i = 0; J.bank = bank;
}

// a take that is whole in corp from frame 0: its last unit padded, silence after it, the head after it
static int finish_take(int got)
{
    int units_n = (got + UNIT - 1) / UNIT;
    if (units_n > corp_units) units_n = corp_units;
    if (got < corp_frames) memset(corp + got, 0, (size_t)(corp_frames - got) * sizeof(int16_t));
    Q.n = units_n; Q.closing = true;
    set_head((uint32_t)(units_n * UNIT));
    return units_n;
}

static void job_tick(void)
{
    if (S.job == JOB_NONE) return;
    J.ms += 10;
    coach_pause();
    if (S.job == JOB_RENDER) {
        if (J.ms < 30) return;                                          // the grains fade first
        bool daisy = S.job_what == BK_DAISY;
        int total = daisy ? daisy_length() : amen_length(4);
        if (total > corp_frames) total = corp_frames;
        if (J.ms == 30) {
            bool ok = daisy ? daisy_begin(corp, corp_frames) : amen_begin(corp, corp_frames, total / amen_length(1));
            if (!ok) { toast("NO MEMORY FOR IT", 1200); S.job = JOB_NONE; starter(); return; }
            Q.on = true; Q.take = map_take_begin(); Q.first = 0; Q.n = 0; Q.done = 0; Q.closing = false;
        }
        int done;
        bool busy;
#ifdef ESP_PLATFORM
        if (daisy) done = daisy_step(4096); else done = amen_step();   // the board: a couple of milliseconds a tick
#else
        if (daisy) done = daisy_step(16384);                           // the host: quicker, so the tests do not wait
        else { done = 0; for (int k = 0; k < 24 && amen_busy(); k++) done = amen_step(); if (!amen_busy()) done = amen_step(); }
#endif
        busy = daisy ? daisy_busy() : amen_busy();
        S.job_frac = total ? (float)done / (float)total : 1.0f;
        Q.n = done / UNIT;
        if (!busy) {
            finish_take(done);
            static int32_t cuts[4 * KIT_SLICES_MAX];
            int nc = daisy ? daisy_cuts(cuts, 4 * KIT_SLICES_MAX) : amen_cuts(cuts, 4 * KIT_SLICES_MAX);
            map_set_cuts(Q.take, cuts, nc);
            S.material = daisy ? MAT_DAISY : MAT_AMEN; rotted = false;
            S.job = JOB_NONE;
            applying = true; param_set(P_BANK, (float)S.job_what); applying = false;
        }
    } else if (S.job == JOB_LOAD) {
        if (J.ms < 30) return;                                          // the grains fade first
        if (J.ms == 30) {
            if (!store_load_begin(J.bank, corp, corp_frames)) { toast("CANNOT READ IT", 1200); S.job = JOB_NONE; material_load(BK_DAISY); return; }
            Q.on = true; Q.take = map_take_begin(); Q.first = 0; Q.n = 0; Q.done = 0; Q.closing = false;
        }
        int got = store_load_step();
        int total = store_frames(J.bank); if (total > corp_frames) total = corp_frames;
        S.job_frac = total ? (float)got / (float)total : 1.0f;
        Q.n = got / UNIT;
        if (!store_busy()) {
            finish_take(got);
            S.job = JOB_NONE; S.material = MAT_KEEP; rotted = false;
            char b[40]; snprintf(b, sizeof b, "%s: %.1f S", bank_l[J.bank + NBUILTIN], (double)got / AUDIO_SR); toast(b, 1300);
        }
    } else if (S.job == JOB_SAVE) {
        if (!store_silent() && J.ms < 250) return;                      // the mute's fade first
        for (int k = 0; k < 8 && J.i < J.n; k++, J.i++) {
            const unit_t *un = &units[J.order[J.i]];
            if (!store_save_put(corp + (size_t)J.order[J.i] * UNIT, UNIT, un->gain * 1.8f * 32767.0f)) break;
        }
        S.job_frac = J.n ? (float)J.i / (float)J.n : 1.0f;
        if (J.i >= J.n || !store_busy()) {
            bool ok = store_busy() && store_save_end(true);
            store_mute(false);
            S.job = JOB_NONE;
            char b[40]; snprintf(b, sizeof b, ok ? "SAVED IN %s" : "SAVE FAILED", bank_l[J.bank]); toast(b, 1300);
        }
    }
}

// ---------------------------------------------------------------- the tilt, the cursor, the shake
static float dead(float deg, float range)                             // -1..1 past a 4 degree dead zone
{
    float a = fabsf(deg) - 4.0f;
    if (a <= 0.0f) return 0.0f;
    return clampf(copysignf(a / (range - 4.0f), deg), -1.0f, 1.0f);
}

static void cursor_tick(const ctl_t *c)
{
    S.mode = (int)pv[P_MODE];
    int mode = (int)pv[P_CURSOR];
    float tx = cur_x, ty = cur_y;
    bool stick_moves = S.mode == MODE_MAP && mode == CUR_STICK && c->has_joy;
    // the stick: across shortens or stretches the grains, up and down bends them, unless it is the cursor
    S.size_mul = !stick_moves && c->has_joy ? fast_exp2(c->jx * 2.0f) : 1.0f;
    S.bend = !stick_moves && c->has_joy ? c->jy * 12.0f : 0.0f;
    float knob = pv[P_CHAOS] * 0.01f;
    S.voice = false;
    eng_gate = 1.0f;
    if (S.mode == MODE_DECOMP) {
        // the roll pushes it apart (right) or back together (left); the pitch moves the head: tipped towards you it
        // slows, stops half way and runs backwards at the end; away from you it runs up to twice as fast
        float push = 0.0f, spd = pv[P_SPEED] * 0.01f;
        if (c->has_imu) {
            float r = pv[P_TRANGE];
            push = dead(c->lean_x, r);
            float t = dead(c->lean_y, r);
            spd += t > 0.0f ? -2.0f * t : -t;
        }
        if (fabsf(spd) < 0.06f) spd = 0.0f;
        if (fabsf(spd - pv[P_SPEED] * 0.01f) > 0.5f) coach_done(L_SPEED);
        push_s += (push - push_s) * 0.5f;
        S.speed = clampf(spd, -2.0f, 3.0f);
        knob = clampf(knob + push_s, 0.0f, 1.0f);
        // the catch follows the head: grains from wherever sounds like what it is on
        int hu = (int)(eng_head / (float)UNIT);
        if (hu >= 0 && hu < corp_units && units[hu].state == U_OK) { cur_x += (map_ux(hu) - cur_x) * 0.5f; cur_y += (map_uy(hu) - cur_y) * 0.5f; }
        voice_note = -1.0f; voice_gate = 0.0f;
    } else if (mode == CUR_STICK) {
        if (c->has_joy) { tx = clampf(cur_x + c->jx * 0.9f * c->dt, 0.0f, 1.0f); ty = clampf(cur_y + c->jy * 0.9f * c->dt, 0.0f, 1.0f); }
        cur_x = tx; cur_y = ty;
    } else if (mode == CUR_TILT) {
        if (c->has_imu && !c->frozen) {
            float r = 2.0f * pv[P_TRANGE];
            tx = clampf(0.5f + c->lean_x / r, 0.0f, 1.0f);
            ty = clampf(0.5f - c->lean_y / r, 0.0f, 1.0f);
        }
        cur_x += (tx - cur_x) * 0.25f; cur_y += (ty - cur_y) * 0.25f;
    }
    if (S.mode == MODE_MAP && mode == CUR_VOICE) {
        static float vx = 0.5f, vy = 0.5f;
        static UI_RAM float lb[UNIT];
        if ((c->ticks % 3) == 0) {
            uint32_t w = __atomic_load_n(&eng_live_w, __ATOMIC_ACQUIRE);
            for (int i = 0; i < UNIT; i++) lb[i] = eng_live[(w - UNIT + (uint32_t)i) & (LIVE_N - 1)];
            desc_t d;
            map_measure(lb, UNIT, &d);
            float echo = eng_out_lvl * 0.35f;                             // what the speaker puts back into the mic
            bool heard = d.d[D_LOUD] > -48.0f && d.level > echo * 0.5f;
            if (heard && !c->frozen) {
                int ax = map_axis_x(), ay = map_axis_y();
                vx = ax == D_TIME ? 1.0f : map_norm(ax, d.d[ax]);
                vy = ay == D_TIME ? 1.0f : map_norm(ay, d.d[ay]);
                voice_note = d.pitch;
                coach_done(L_VOICE);
            } else if (!heard) voice_note = -1.0f;
            float want = heard ? clampf((d.d[D_LOUD] + 50.0f) / 30.0f, 0.0f, 1.0f) : 0.0f;
            voice_gate = want > voice_gate ? want : voice_gate * 0.8f;
            S.voice = heard;
        } else S.voice = voice_note >= 0.0f || voice_gate > 0.3f;
        cur_x += (vx - cur_x) * 0.35f; cur_y += (vy - cur_y) * 0.35f;
        eng_gate = voice_gate;
    } else if (S.mode == MODE_MAP) { voice_note = -1.0f; voice_gate = 0.0f; }
    S.cx = cur_x; S.cy = cur_y;

    // the shake: a jolt over 0.45 g, or a spin over 350 deg/s, throws it towards chaos; it settles in a second or two
    burst_ms += 10;
    if (c->has_imu && !c->frozen) {
        float am = sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z);
        float kick = fmaxf(0.0f, fabsf(am - 1.0f) - 0.45f) * 2.2f + fmaxf(0.0f, c->gyro_mag - 350.0f) / 900.0f;
        float k = pv[P_SHAKE] * 0.01f;
        shake = fminf(1.0f, shake * 0.993f + kick * k * 0.6f);
        if (kick * k > 0.35f && burst_ms > 150) {
            burst_ms = 0;
            __atomic_add_fetch(&eng_burst_req, (uint32_t)clampi((int)(kick * k * 8.0f), 2, 10), __ATOMIC_RELEASE);
            scene_shake(fminf(1.0f, kick * k));
            coach_done(L_SHAKE);
            ui_awake();
        }
    } else shake *= 0.993f;
    S.shake = shake;
    S.chaos = knob + (1.0f - knob) * shake;
}

// ---------------------------------------------------------------- the catch
static uint16_t scale_mask(void)
{
    int key = (int)pv[P_KEY], sc = clampi((int)pv[P_SCALE], 0, NSCALES - 1), tune = (int)pv[P_TUNE];
    uint16_t m = 0;
    if (tune == TUNE_ALL) return 0x0FFF;
    if (tune == TUNE_PENTA) {
        bool minor = scales[sc][2] == 3;
        static const int maj[] = { 0, 2, 4, 7, 9 }, min[] = { 0, 3, 5, 7, 10 };
        for (int i = 0; i < 5; i++) m |= (uint16_t)(1u << ((key + (minor ? min[i] : maj[i])) % 12));
        return m;
    }
    for (int i = 0; i < 7; i++) m |= (uint16_t)(1u << ((key + scales[sc][i]) % 12));
    return m;
}
static float nearest_note(float n, uint16_t mask)
{
    int c = (int)lroundf(n);
    for (int d = 0; d <= 6; d++) {
        int a = c + d, b = c - d;
        bool ia = mask & (1u << (((a % 12) + 12) % 12)), ib = mask & (1u << (((b % 12) + 12) % 12));
        if (ia && ib) return fabsf((float)a - n) < fabsf((float)b - n) ? (float)a : (float)b;
        if (ia) return (float)a;
        if (ib) return (float)b;
    }
    return (float)c;
}

static int16_t prev_set[16];
static int nprev;

static void catch_tick(void)
{
    static cands_t C;
    C.n = 0; C.nseq = 0;
    S.empty = !corp || map_count() == 0;
    if (S.empty) { S.nseq = 0; eng_cands_publish(&C); return; }
    const float W = (float)(MAP_X1 - MAP_X0), H = (float)(MAP_Y1 - MAP_Y0);
    float ce = S.chaos;
    float ring = 3.0f + pv[P_SPREAD] * 0.01f * 70.0f * (0.2f + 0.8f * ce);
    S.ring = ring;
    // the MAXC nearest, the pattern's last members a little nearer than they are (so it does not flicker)
    float dist[MAXC]; int16_t who[MAXC]; int n = 0;
    for (int u = 0; u < corp_units; u++) {
        if (units[u].state != U_OK) continue;
        float dx = (map_ux(u) - cur_x) * W, dy = (map_uy(u) - cur_y) * H;
        float d2 = dx * dx + dy * dy;
        for (int k = 0; k < nprev; k++) if (prev_set[k] == u) { d2 *= 0.6f; break; }
        if (n == MAXC && d2 >= dist[n - 1]) continue;
        int i = n < MAXC ? n++ : MAXC - 1;
        while (i > 0 && dist[i - 1] > d2) { dist[i] = dist[i - 1]; who[i] = who[i - 1]; i--; }
        dist[i] = d2; who[i] = (int16_t)u;
    }
    // tuning: towards the key, your voice or a MIDI note
    float transpose = pv[P_PITCH] + S.bend;
    uint16_t mask = scale_mask();
    int tune = (int)pv[P_TUNE];
    float follow = midi_held >= 0 ? (float)midi_held : (pv[P_CURSOR] == CUR_VOICE && pv[P_MATCH] >= 1.0f && voice_note >= 0.0f) ? voice_note : -1.0f;
    float wsum = 0;
    for (int i = 0; i < n; i++) {
        cand_t *c = &C.c[i];
        const unit_t *un = &units[who[i]];
        c->unit = who[i];
        c->start = (uint32_t)who[i] * UNIT + un->onset;
        c->gain = un->gain;
        c->pitched = un->pitch >= 0.0f;
        c->semi_free = transpose;
        if (c->pitched && (tune != TUNE_OFF || follow >= 0.0f)) {
            float here = un->pitch + transpose, target;
            if (follow >= 0.0f) {
                target = follow + pv[P_PITCH];
                if (tune != TUNE_OFF) target = nearest_note(target, mask);
                while (target - here > 12.0f) target -= 12.0f;                 // within an octave of where it is
                while (here - target > 12.0f) target += 12.0f;
            } else target = nearest_note(here, mask);
            c->semi_order = target - un->pitch;
        } else c->semi_order = transpose;
        float d = sqrtf(dist[i]);
        float w = d < ring ? 1.0f - d / ring + 0.15f : 0.0f;
        wsum += w;
        c->wcum = wsum;
    }
    if (wsum <= 0.0f) for (int i = 0; i < n; i++) C.c[i].wcum = 1.0f;     // nothing inside the ring: the nearest
    else for (int i = 0; i < n; i++) C.c[i].wcum /= wsum;
    C.n = n;
    C.spread = (float)UNIT * (0.5f + 2.5f * pv[P_SPREAD] * 0.01f);
    // the order side's pattern: the NOTES nearest, sorted
    int k = clampi((int)pv[P_STEPS], 1, 16); if (k > n) k = n;
    uint8_t idx[16];
    for (int i = 0; i < k; i++) idx[i] = (uint8_t)i;
    int pat = (int)pv[P_PATTERN];
    for (int i = 1; i < k; i++) {                                     // insertion sort by the pattern's measure
        uint8_t v = idx[i]; int j = i;
        float kv = pat == PAT_PLAYED ? units[C.c[v].unit].d[D_TIME] : map_tone_of(C.c[v].unit) + (C.c[v].pitched ? C.c[v].semi_order : 0.0f);
        while (j > 0) {
            uint8_t pvv = idx[j - 1];
            float kp = pat == PAT_PLAYED ? units[C.c[pvv].unit].d[D_TIME] : map_tone_of(C.c[pvv].unit) + (C.c[pvv].pitched ? C.c[pvv].semi_order : 0.0f);
            if (kp <= kv) break;
            idx[j] = pvv; j--;
        }
        idx[j] = v;
    }
    if (pat == PAT_SHUFFLE) {                                         // a fixed shuffle of this set: the same set, the same order
        uint32_t h = 2166136261u;
        for (int i = 0; i < k; i++) h = (h ^ (uint32_t)C.c[idx[i]].unit) * 16777619u;
        for (int i = k - 1; i > 0; i--) { int j = (int)(rng_next(&h) % (uint32_t)(i + 1)); uint8_t t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    }
    if (pat == PAT_DOWN) for (int i = 0; i < k; i++) C.seq[i] = idx[k - 1 - i];
    else for (int i = 0; i < k; i++) C.seq[i] = idx[i];
    C.nseq = k;
    if (pat == PAT_BOUNCE && k > 2) { for (int i = k - 2; i >= 1; i--) C.seq[C.nseq++] = idx[i]; }
    nprev = k;
    for (int i = 0; i < k; i++) prev_set[i] = C.c[i].unit;
    S.nseq = C.nseq;
    for (int i = 0; i < C.nseq; i++) S.seq_unit[i] = C.c[C.seq[i]].unit;
    eng_cands_publish(&C);
}

// ROT changes what the head reads: each unit it leaves is listened to again (its dot moves, its level is new)
static void rot_tick(void)
{
    if (S.mode != MODE_DECOMP || pv[P_ROT] <= 0.0f || S.job != JOB_NONE || S.rec != REC_IDLE) { rot_unit = -1; return; }
    rotted = true;
    int hu = (int)(eng_head / (float)UNIT);
    if (hu < 0 || hu >= corp_units) return;
    if (rot_unit >= 0 && rot_unit != hu && units[rot_unit].state != U_EMPTY) map_relisten(rot_unit);
    rot_unit = hu;
}

// ---------------------------------------------------------------- the link: tempo and key, shared
static void harm_here(void)
{
    const link_harm_t *h = link_harm();
    link_set_harm((int)pv[P_KEY], (int)pv[P_SCALE], h->prog, h->hold, h->bars, -1);
}
static void tempo_here(void)
{
    uint32_t fpb = tempo_fpb(pv[P_TEMPO]), old = link_fpb();
    if (fpb == old) return;
    uint32_t o; float f; link_origin(&o, &f);
    float beat = old ? (float)((int32_t)(sift_now - o) % (int32_t)old) / (float)old : 0.0f;   // keep where we are in the beat
    if (beat < 0.0f) beat += 1.0f;
    link_set_time(fpb, sift_now - (uint32_t)(beat * (float)fpb), 4, 0);
}
static void link_start(void)
{
    link_init("SIFT", 1);
    link_boot_time(tempo_fpb(pv[P_TEMPO]), 0, 4);
    link_boot_harm((int)pv[P_KEY], (int)pv[P_SCALE], 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
}
static void link_follow(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    S.peers = link_peers(pr, LINK_PEERS_MAX);
    if (link_time_news()) {
        uint32_t fpb = link_fpb();
        int bpm = fpb ? (int)(60.0f * (float)AUDIO_SR / (float)fpb + 0.5f) : 0;
        if (bpm && bpm != (int)pv[P_TEMPO]) { applying = true; param_set(P_TEMPO, (float)clampi(bpm, 50, 200)); applying = false; }
    }
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        if ((int)pv[P_KEY] != h->key % 12) param_set(P_KEY, (float)(h->key % 12));
        if ((int)pv[P_SCALE] != h->scale && h->scale < NSCALES) param_set(P_SCALE, (float)h->scale);
        applying = false;
    }
    uint32_t fpb = link_fpb();
    S.bpm = fpb ? 60.0f * (float)AUDIO_SR / (float)fpb : 0.0f;
}

// ---------------------------------------------------------------- MIDI and the LED
typedef struct { int note; uint32_t off; } mnote_t;
static mnote_t mnotes[8];
static float led_l, led_c[3];

#ifndef ESP_PLATFORM
spawn_t sift_log[8192];                                                 // the tests read what played
int     sift_log_n;
#endif

static void sounded(const spawn_t *s)
{
#ifndef ESP_PLATFORM
    if (sift_log_n < 8192) sift_log[sift_log_n++] = *s;
#endif
    scene_spawn(s);
    if (s->unit < 0 || s->unit >= corp_units) return;
    const unit_t *un = &units[s->unit];
    float k = clampf(s->amp * 2.0f, 0.2f, 1.0f);
    led_l = fmaxf(led_l, (S.mode == MODE_DECOMP ? 0.1f : 0.25f) + 0.5f * k * (S.mode == MODE_DECOMP ? S.chaos : 1.0f));
    if (S.mode == MODE_DECOMP) {
        if (s->kind != GK_BURST) { int j = sift_daisy_syl_of(s->pos); if (j >= 0) { S.syl_heard = j; S.syl_frame = s->frame; } }
        return;
    }
    if (s->kind == GK_ORDER) {
        S.last_order_unit = s->unit; S.last_order_frame = s->frame;
        if (un->pitch >= 0.0f) {                                      // the order side's notes go out as MIDI notes
            int note = clampi((int)lroundf(un->pitch + s->semi), 0, 127);
            for (int i = 0; i < 8; i++) if (!mnotes[i].off) {
                midi_note(note, clampi((int)(40.0f + 87.0f * k), 1, 127));
                mnotes[i].note = note;
                mnotes[i].off = s->frame + (uint32_t)(pv[P_SIZE] * (float)AUDIO_SR) + 1;
                break;
            }
        }
    }
}

static void midi_tick(void)
{
    for (int i = 0; i < 8; i++) if (mnotes[i].off && (int32_t)(sift_now - mnotes[i].off) >= 0) { midi_note(mnotes[i].note, 0); mnotes[i].off = 0; }
    static int last[3] = { -1, -1, -1 };
    static int every;
    if (++every < 3) return;
    every = 0;
    int v[3] = { (int)(S.cx * 127.0f + 0.5f), (int)(S.cy * 127.0f + 0.5f), (int)(S.chaos * 127.0f + 0.5f) };
    for (int i = 0; i < 3; i++)                                         // a step of 2, or an end: the hand's tremor stays off the wire
        if (abs(v[i] - last[i]) >= 2 || (v[i] != last[i] && (v[i] == 0 || v[i] == 127))) { midi_cc(1 + i, v[i]); last[i] = v[i]; }
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) midi_held = d1;
    else if ((k == 0x80 || (k == 0x90 && d2 == 0)) && midi_held == d1) midi_held = -1;
}

static void led_tick(const ctl_t *c)
{
    // whole / order is a cold blue, pieces / chaos a hot magenta; each grain flashes it; red breathing while recording
    float ce = S.chaos;
    led_c[0] = 40.0f + 215.0f * ce; led_c[1] = 120.0f * (1.0f - ce); led_c[2] = 255.0f - 120.0f * ce;
    led_l *= 0.85f;
    if (S.rec != REC_IDLE) { float l = 0.35f + 0.25f * sinf((float)c->ticks * (S.rec == REC_LIVE ? 0.12f : 0.3f)); led_rgb((uint8_t)(255.0f * l), 0, (uint8_t)(30.0f * l)); return; }
    if (S.voice) { led_rgb(0, (uint8_t)(80.0f + 150.0f * voice_gate), 40); return; }
    float l = fmaxf(led_l, 0.08f);
    led_rgb((uint8_t)(led_c[0] * l), (uint8_t)(led_c[1] * l), (uint8_t)(led_c[2] * l));
}

// ---------------------------------------------------------------- the core's calls
static void controls(void)
{
    eng_mode = S.mode;
    eng_chaos = S.chaos;
    eng_size = pv[P_SIZE];
    eng_shape = (int)pv[P_SHAPE];
    eng_density = pv[P_DENSITY];
    eng_reverse = pv[P_REVERSE] * 0.01f;
    eng_scatter = pv[P_SCATTER];
    eng_detune = pv[P_DETUNE];
    eng_level = pv[P_LEVEL] * 0.01f;
    eng_div = step_div[clampi((int)pv[P_STEP], 0, 5)];
    eng_stack = (int)pv[P_STACK];
    eng_tone = pv[P_TONE] * 0.01f;
    eng_space = pv[P_SPACE] * 0.01f;
    eng_size_mul = S.size_mul;
    eng_pitch = pv[P_PITCH] + S.bend;
    eng_speed = S.mode == MODE_DECOMP ? S.speed : 1.0f;
    eng_stutter = pv[P_STUTTER] / 50.0f;
    eng_jumps = pv[P_JUMPS] / 50.0f;
    eng_gaps = pv[P_GAPS] / 40.0f;
    eng_bite = pv[P_BITE] * 0.01f;
    eng_drive = pv[P_DRIVE] * 0.01f;
    eng_crush = pv[P_CRUSH] * 0.01f;
    eng_fb_amt = pv[P_FEEDBACK] * 0.0095f;
    eng_fb_time = pv[P_FBTIME];
    eng_rot = pv[P_ROT] * 0.01f;
}

static void tail_coefs(void)
{
    static const int len[4] = { 1559, 1877, 2203, 2617 };
    float t60 = 0.3f * exp2f(pv[P_TAIL] * 0.01f * 6.0f);
    for (int k = 0; k < 4; k++) eng_fb[k] = expf(-6.9078f * (float)len[k] / (t60 * (float)AUDIO_SR));
}

static void tick(const ctl_t *c)
{
    sift_now = c->frames;
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    if (!booted) {                                                      // whatever BANK points to: Daisy, the Amen, a bank you saved
        booted = true;
        int b = (int)pv[P_BANK];
        if (b >= NBUILTIN && !store_has(b - NBUILTIN)) b = BK_DAISY;
        load_bank(b);
    }
    job_tick();
    rec_logic(c);
    q_tick();
    map_update();
    cursor_tick(c);
    controls();
    catch_tick();
    rot_tick();
    spawn_t s;
    while (eng_spawn_pop(&s)) sounded(&s);
    midi_tick();
    led_tick(c);
    if (S.axes_shown > 0.0f) S.axes_shown -= c->dt;
    if (S.rec != REC_IDLE || S.voice || shake > 0.1f) ui_awake();
}

static void changed(int p)
{
    if (p < 0 || p == P_MAPX || p == P_MAPY) { map_set_axes((int)pv[P_MAPX], (int)pv[P_MAPY]); if (p >= 0) S.axes_shown = 2.5f; }
    if (p < 0 || p == P_TAIL) tail_coefs();
    // a sound that brings its material (DAISY, AMEN): loaded over another built-in, never over what you recorded
    if ((p < 0 || p == P_MATL) && booted) {
        int m = (int)pv[P_MATL];
        if (m != MAT_KEEP) {
            bool builtin = S.material != MAT_KEEP;
            if (builtin && (m != S.material || rotted)) material_load(m == MAT_DAISY ? BK_DAISY : m == MAT_AMEN ? BK_AMEN : BK_STARTER);
            else if (!builtin && p < 0) toast("YOUR RECORDING STAYS", 1000);
        }
    }
    if (!link_up) return;
    if (p == P_LINK || p == P_GROUP || p == P_RANGE) {
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    }
    if (!applying && (p == P_KEY || p == P_SCALE)) harm_here();
    if (!applying && p == P_TEMPO) tempo_here();
}

static void action(int p)
{
    switch (p) {
    case P_LOAD: load_bank((int)pv[P_BANK]); break;
    case P_SAVE: job_save((int)pv[P_BANK]); break;
    case P_CLEAR:
        if (S.job != JOB_NONE) break;
        rec_end(); q_flush(); eng_hush(); map_reset(); set_head(0);
        S.material = MAT_KEEP;
        toast("CLEARED", 900);
        break;
    case P_PEERS: {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[64];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)pv[P_GROUP]);
        else {
            int n = snprintf(b, sizeof b, "%d: ", np);
            for (int i = 0; i < np && n < (int)sizeof b - 10; i++) n += snprintf(b + n, sizeof b - (size_t)n, "%s%s", i ? " " : "", pr[i].name);
        }
        toast(b, 2000);
        break;
    }
    default: break;
    }
}

static void init(void)
{
    eng_init();
    map_init();
    map_set_axes((int)pv[P_MAPX], (int)pv[P_MAPY]);
    tail_coefs();
    store_init();
    S.last_order_unit = -1; S.syl_heard = -1; S.speed = 1.0f;
    S.material = MAT_STARTER;                                           // nothing yet: the first tick loads the bank
}

#ifdef ESP_PLATFORM
static void audio(const float *in, float *out, int n) { eng_audio(in, out, n); }
#else
#include <time.h>
double sift_audio_ns, sift_audio_max_ns;                               // the tests' clock on the sound
long   sift_audio_blocks;
static void audio(const float *in, float *out, int n)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    eng_audio(in, out, n);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ns = (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
    sift_audio_ns += ns; sift_audio_blocks++;
    if (ns > sift_audio_max_ns) sift_audio_max_ns = ns;
}
#endif
static void draw(const view_t *v) { scene_draw(v); }

const instrument_t INST = {
    .name = "SIFT", .tagline = "WHOLE, THEN IN PIECES",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 9,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false, .knob = "chao",
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = scene_saver,
};
