// NYLON: for a classical guitar, the board stuck on its top. The mic hears the strings; the neck is the pedal.
//
// Every sound is one of ten modes, and under all of them the same loop keeps playing:
//   CLEAN    the guitar, and the loop the neck rides (its level, or its speed and direction like the Samplooler)
//   CLOUD    grains from the last three seconds; a slap on the top freezes it, another lets it go
//   BLOOM    the last note held by grains, swelling in as the string dies; with a speaker on the top, the strings
//            themselves keep ringing; HARMONIC brings the octave and twelfth in as it ages
//   ECHO     a tape echo; lift the neck and the repeats run away
//   WAH      the neck is the pedal (a fuzz before it if you like)
//   SHIMMER  a room that rises in octaves
//   OCTAVE   an octave under, an octave over, a fifth
//   WOBBLE   a warped tape's vibrato, or a chorus with the dry
//   GOLPE    slaps on the top and the side are drums
//   VOICE    a synth that follows the note you play (and sends it out as MIDI)
// REC is the looper's (inst_loop.c): tap to arm (the take starts with your first note), tap on the one to close,
// tap to add a layer, tap again to stop adding; hold to stop, hold on to erase. A footswitch on the REC pin, or a
// sustain pedal over MIDI, does the same. Hold the knob where you sit: that is HOME, where the neck is zero.
//
// This file is the UI side: params, sounds, hints, REC, what the neck does in each mode, slaps, VOICE's notes,
// MIDI, the LED. The sound is inst_engine.c and its parts; the senses inst_ear.c; the screen inst_scene.c.
#include "core.h"
#include "dsp.h"
#include "params.h"
#include "inst_nylon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- params
static const char *const groups[] = { "PLAY", "LOOP", "NECK", "CLOUD", "BLOOM", "FX", "DRUMS", "VOICE" };
enum { G_PLAY, G_LOOP, G_NECK, G_CLOUD, G_BLOOM, G_FX, G_DRUMS, G_VOICE };
static const char *const mode_l[NMODE] = { "CLEAN", "CLOUD", "BLOOM", "ECHO", "WAH", "SHIMMER", "OCTAVE", "WOBBLE", "GOLPE", "VOICE" };
static const char *const freeze_l[] = { "THE STICK", "A SLAP", "NECK UP", "OFF" };
static const char *const bars_l[] = { "FREE", "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
static const int bars_of[] = { 0, 1, 2, 4, 8 };
static const char *const metro_l[] = { "OFF", "LIGHT", "COUNT-IN", "SOUND" };
static const char *const catch_l[] = { "EVERY NOTE", "LOUD NOTES", "THE FIRST" };
static const char *const sync_l[] = { "OFF", "LOOP" };
static const char *const shint_l[] = { "OCTAVE", "FIFTH", "BOTH" };
static const char *const vtype_l[NVT] = { "BASS", "BOW", "ORGAN", "PIPE" };
static const char *const lneck_l[] = { "OFF", "LEVEL", "TAPE" };
static const char *const start_l[] = { "ON A NOTE", "AT ONCE" };
static const char *const lean_l[] = { "OFF", "TOP TO ME", "TOP AWAY" };
static const char *const slap_l[] = { "AUTO", "DRUMS", "OFF" };
static const char *const off_on[] = { "OFF", "ON" };
#define SYS (PF_SYS | PF_NOCC)
#define KEPT (PF_SYS | PF_HIDE | PF_NOCC)

static const param_t params[P_N] = {
    [P_MODE]    = { "MODE", "mode", G_PLAY, PK_ENUM, 0, 0, NMODE - 1, 1, M_CLEAN, "", mode_l, "THE EFFECT ON THE GUITAR" },
    [P_DRY]     = { "DRY", "dry ", G_PLAY, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "THE GUITAR AS IT IS" },
    [P_WET]     = { "EFFECT", "wet ", G_PLAY, PK_NUM, PF_INT, 0, 150, 5, 70, "%", NULL, "HOW LOUD THE EFFECT IS" },
    [P_SPACE]   = { "ROOM", "spce", G_PLAY, PK_NUM, PF_INT, 0, 100, 5, 12, "%", NULL, "A ROOM AROUND IT ALL" },
    [P_RSIZE]   = { "ROOM SIZE", "rsiz", G_PLAY, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "A SMALL ROOM TO A HALL" },
    [P_LOWCUT]  = { "LOW CUT", "lcut", G_PLAY, PK_NUM, PF_LOG | PF_INT | PF_SYS, 40, 400, 1.08f, 90, "Hz", NULL, "LESS BOOM FROM THE BODY" },
    [P_HICUT]   = { "HIGH CUT", "hcut", G_PLAY, PK_NUM, PF_LOG | PF_INT | PF_SYS, 1000, 14000, 1.08f, 9000, "Hz", NULL, "LESS FIZZ AND WHISTLE" },
    [P_GUARD]   = { "HOWL GUARD", "guar", G_PLAY, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "A HOWL GETS A NOTCH" },
    [P_CSIZE]   = { "GRAIN SIZE", "csiz", G_CLOUD, PK_NUM, PF_LOG | PF_INT, 15, 500, 1.1f, 120, "ms", NULL, "HOW LONG EACH GRAIN IS" },
    [P_CDENS]   = { "DENSITY", "cdns", G_CLOUD, PK_NUM, PF_LOG, 1, 80, 1.12f, 14, "", NULL, "GRAINS A SECOND" },
    [P_CREACH]  = { "REACH", "crch", G_CLOUD, PK_NUM, PF_LOG, 0.1f, 2.8f, 1.1f, 1.2f, "s", NULL, "HOW FAR BACK THEY REACH" },
    [P_CUP]     = { "SPARKLE", "cup ", G_CLOUD, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "GRAINS AN OCTAVE UP" },
    [P_CDOWN]   = { "DEPTH", "cdwn", G_CLOUD, PK_NUM, PF_INT, 0, 100, 5, 5, "%", NULL, "GRAINS AN OCTAVE DOWN" },
    [P_CREV]    = { "REVERSE", "crev", G_CLOUD, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "GRAINS PLAYED BACKWARDS" },
    [P_CFREEZE] = { "FREEZE BY", "cfrz", G_CLOUD, PK_ENUM, 0, 0, 3, 1, FRZ_STICK, "", freeze_l, "WHAT HOLDS THE CLOUD STILL" },
    [P_BSUST]   = { "HOLD", "bhld", G_BLOOM, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "HOW LONG A NOTE STAYS" },
    [P_BSWELL]  = { "SWELL", "bswl", G_BLOOM, PK_NUM, PF_LOG, 0.05f, 6.0f, 1.12f, 0.8f, "s", NULL, "HOW SLOWLY IT GROWS IN" },
    [P_BHARM]   = { "HARMONIC", "bhrm", G_BLOOM, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "ITS HARMONICS GROW IN" },
    [P_BSUB]    = { "SUB", "bsub", G_BLOOM, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN OCTAVE UNDER IT" },
    [P_BCATCH]  = { "CATCH", "bcat", G_BLOOM, PK_ENUM, 0, 0, 2, 1, CATCH_ALL, "", catch_l, "WHICH NOTES IT HOLDS" },
    [P_ETIME]   = { "ECHO TIME", "etim", G_FX, PK_NUM, PF_LOG, 0.04f, 1.4f, 1.06f, 0.36f, "s", NULL, "TIME BETWEEN REPEATS" },
    [P_EFB]     = { "REPEATS", "efbk", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HOW LONG THE ECHO GOES ON" },
    [P_ETONE]   = { "ECHO TONE", "etne", G_FX, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "DARK OR BRIGHT REPEATS" },
    [P_EWOW]    = { "TAPE WOW", "ewow", G_FX, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "THE TAPE WAVERS" },
    [P_ESYNC]   = { "ECHO SYNC", "esyn", G_FX, PK_ENUM, 0, 0, 1, 1, 0, "", sync_l, "THE TIME FITS THE LOOP" },
    [P_WQ]      = { "WAH PEAK", "wahq", G_FX, PK_NUM, PF_LOG, 1, 12, 1.1f, 5, "", NULL, "HOW SHARP THE WAH IS" },
    [P_WENV]    = { "AUTO WAH", "wenv", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "PLAYING HARD OPENS IT" },
    [P_WFUZZ]   = { "FUZZ", "fuzz", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "DIRT BEFORE THE WAH" },
    [P_SHIM]    = { "SHIMMER", "shim", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE ROOM RISES IN OCTAVES" },
    [P_SHINT]   = { "SHIMMER UP", "shnt", G_FX, PK_ENUM, 0, 0, 2, 1, 0, "", shint_l, "OCTAVE OR FIFTH OR BOTH" },
    [P_OSUB]    = { "OCT DOWN", "osub", G_FX, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "AN OCTAVE UNDER" },
    [P_OUP]     = { "OCT UP", "oup ", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "AN OCTAVE OVER" },
    [P_OFIFTH]  = { "FIFTH", "ofif", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A FIFTH OVER" },
    [P_MDEPTH]  = { "WOBBLE", "wdep", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HOW FAR THE PITCH WAVERS" },
    [P_MRATE]   = { "WOBBLE RATE", "wrat", G_FX, PK_NUM, PF_LOG, 0.1f, 8.0f, 1.1f, 0.9f, "Hz", NULL, "HOW FAST IT WAVERS" },
    [P_DTOP]    = { "TOP", "dtop", G_DRUMS, PK_ENUM, 0, 0, NDRUM - 1, 1, DR_KICK, "", drum_names, "A SLAP ON THE TOP PLAYS" },
    [P_DSIDE]   = { "SIDE", "dsid", G_DRUMS, PK_ENUM, 0, 0, NDRUM - 1, 1, DR_SNARE, "", drum_names, "A SLAP ON THE SIDE PLAYS" },
    [P_DLEVEL]  = { "DRUM LEVEL", "dlvl", G_DRUMS, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE DRUMS ARE" },
    [P_DTUNE]   = { "DRUM TUNE", "dtun", G_DRUMS, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE DRUMS UP OR DOWN" },
    [P_DDECAY]  = { "DRUM DECAY", "ddcy", G_DRUMS, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "SHORT OR RINGING DRUMS" },
    [P_VTYPE]   = { "VOICE", "vtyp", G_VOICE, PK_ENUM, 0, 0, NVT - 1, 1, VT_BASS, "", vtype_l, "WHAT PLAYS ALONG WITH YOU" },
    [P_VOCT]    = { "OCTAVE", "voct", G_VOICE, PK_NUM, PF_INT | PF_SIGNED, -2, 1, 1, -1, "", NULL, "UNDER OR OVER YOUR NOTE" },
    [P_VGLIDE]  = { "GLIDE", "vgld", G_VOICE, PK_NUM, 0, 0, 0.5f, 0.01f, 0.03f, "s", NULL, "HOW IT SLIDES TO A NOTE" },
    [P_VMIDI]   = { "MIDI NOTES", "vmid", G_VOICE, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "YOUR NOTES GO OUT ON MIDI" },
    [P_LNECK]   = { "BY THE NECK", "lnck", G_LOOP, PK_ENUM, 0, 0, 2, 1, LN_OFF, "", lneck_l, "LIFT: LOUDER OR FASTER" },
    [P_LLEVEL]  = { "LOOP LEVEL", "llvl", G_LOOP, PK_NUM, PF_INT | PF_SYS, 0, 100, 5, 85, "%", NULL, "HOW LOUD THE LOOP PLAYS" },
    [P_KEEP]    = { "KEEP", "lkep", G_LOOP, PK_NUM, PF_INT | PF_SYS, 50, 100, 1, 100, "%", NULL, "EACH PASS KEEPS THIS MUCH" },
    [P_LBARS]   = { "LENGTH", "lbar", G_LOOP, PK_ENUM, SYS, 0, 4, 1, 3, "", bars_l, "BARS IN A TAKE OR FREE" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_LOOP, PK_NUM, PF_INT | PF_SYS, 40, 220, 1, 90, "", NULL, "BEATS A MINUTE" },
    [P_BEATS]   = { "BEATS", "beat", G_LOOP, PK_NUM, PF_INT | SYS, 2, 7, 1, 4, "", NULL, "BEATS A BAR" },
    [P_METRO]   = { "METRONOME", "metr", G_LOOP, PK_ENUM, SYS, 0, 3, 1, MET_COUNT, "", metro_l, "A BAR IN THEN IT RECORDS" },
    [P_START]   = { "START", "lstr", G_LOOP, PK_ENUM, SYS, 0, 1, 1, ST_NOTE, "", start_l, "NO CLICK: WHEN IT STARTS" },
    [P_TRIG]    = { "TRIGGER", "ltrg", G_LOOP, PK_NUM, PF_INT | SYS, -60, -12, 2, -40, "dB", NULL, "HOW LOUD A NOTE STARTS IT" },
    [P_UNDO]    = { "UNDO", "lund", G_LOOP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE NEWEST LAYER GOES" },
    [P_ERASE]   = { "ERASE", "lers", G_LOOP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE LOOP GOES" },
    [P_SETUP]   = { "NECK SETUP", "nset", G_NECK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "SIT STILL THEN LIFT" },
    [P_HOME]    = { "SET HOME", "nhom", G_NECK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "WHERE YOU SIT IS ZERO" },
    [P_RANGE]   = { "NECK RANGE", "nrng", G_NECK, PK_NUM, PF_INT | SYS, 8, 45, 1, 20, "", NULL, "DEGREES FOR THE MOST" },
    [P_LRANGE]  = { "LEAN RANGE", "nlrg", G_NECK, PK_NUM, PF_INT | SYS, 6, 45, 1, 15, "", NULL, "DEGREES OF TIP FOR ALL" },
    [P_STILL]   = { "STILL ZONE", "nstl", G_NECK, PK_NUM, PF_INT | SYS, 0, 10, 1, 3, "", NULL, "DEGREES THAT DO NOTHING" },
    [P_NECKON]  = { "USE NECK", "nckn", G_NECK, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "THE NECK MOVES THE SOUND" },
    [P_LEAN]    = { "LEAN", "nlen", G_NECK, PK_ENUM, SYS, 0, 2, 1, LEAN_NORMAL, "", lean_l, "TIPPING THE TOP UP" },
    [P_SLAP]    = { "SLAPS", "slap", G_NECK, PK_ENUM, SYS, 0, 2, 1, SLAP_AUTO, "", slap_l, "WHAT A HIT ON THE BODY DOES" },
    [P_SENSE]   = { "SLAP SENSE", "ssen", G_NECK, PK_NUM, PF_INT | SYS, 0, 100, 5, 50, "%", NULL, "HOW LIGHT A SLAP COUNTS" },
    [P_SLAPSET] = { "SLAP SETUP", "slst", G_NECK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "STRUM THEN SLAP 3 TIMES" },
    [P_HX]      = { "HOME X", "hmx ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_HY]      = { "HOME Y", "hmy ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_HZ]      = { "HOME Z", "hmz ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_AX]      = { "AXIS X", "axx ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_AY]      = { "AXIS Y", "axy ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_AZ]      = { "AXIS Z", "axz ", G_NECK, PK_NUM, KEPT, -1, 1, 0.001f, 0, "", NULL, "" },
    [P_AXSET]   = { "AXIS SET", "axst", G_NECK, PK_NUM, KEPT | PF_INT, 0, 2, 1, 0, "", NULL, "" },   // 1 NECK SETUP done, 2 skipped
    [P_SLAPTHR] = { "SLAP AT", "slth", G_NECK, PK_NUM, KEPT, 0.2f, 6.0f, 0.01f, 1.2f, "", NULL, "" },          // g: SLAP SETUP's
};

// ---------------------------------------------------------------- sounds
static const preset_t presets[] = {
    { "CLEAN",         "mode=0 lnck=1" },
    { "TAPE LOOP",     "mode=0 lnck=2" },
    { "CLOUD",         "mode=1 wet=80 spce=30 csiz=120 cdns=14 crch=1.2 cup=15 cdwn=5 crev=30" },
    { "HALO",          "mode=1 wet=90 spce=55 rsiz=75 csiz=300 cdns=10 crch=2.5 cup=40 crev=20 cfrz=2 shim=25" },
    { "DUST",          "mode=1 wet=85 spce=20 csiz=25 cdns=60 crch=0.6 cup=10 cdwn=10 crev=50" },
    { "BLOOM",         "mode=2 wet=90 spce=20 bhld=55 bswl=0.8 bhrm=25" },
    { "BLOOM ON WOOD", "mode=2 dry=0 wet=110 spce=0 bhld=70 bswl=1.2 bhrm=40" },
    { "HARMONICS",     "mode=2 wet=100 spce=30 bhld=80 bswl=1.5 bhrm=85" },
    { "ECHO",          "mode=3 wet=55 spce=10 etim=0.36 efbk=40 etne=45 ewow=25" },
    { "DUB",           "mode=3 wet=65 spce=25 rsiz=70 etim=0.48 efbk=60 etne=25 ewow=40 esyn=1" },
    { "SLAPBACK",      "mode=3 wet=45 spce=5 etim=0.095 efbk=12 etne=70 ewow=10" },
    { "NECK WAH",      "mode=4 dry=0 wet=100 wahq=5 spce=8" },
    { "FUZZ WAH",      "mode=4 dry=0 wet=80 wahq=6 fuzz=65 spce=8" },
    { "AUTO WAH",      "mode=4 dry=0 wet=100 wahq=4 wenv=70 spce=8" },
    { "SHIMMER",       "mode=5 spce=55 rsiz=85 shim=55" },
    { "CATHEDRAL",     "mode=5 spce=70 rsiz=100 shim=35 shnt=2" },
    { "OCTAVES",       "mode=6 wet=80 osub=60 oup=40 spce=12" },
    { "TWELVE STRING", "mode=6 wet=55 osub=0 oup=60 spce=15" },
    { "WOBBLE",        "mode=7 dry=0 wet=100 wdep=40 wrat=0.9 spce=12" },
    { "CHORUS",        "mode=7 wet=70 wdep=25 wrat=0.6 spce=15" },
    { "GOLPE",         "mode=8 dtop=1 dsid=2 dlvl=80 spce=10" },
    { "CAJON",         "mode=8 dtop=11 dsid=12 dlvl=90 spce=15" },
    { "BASS",          "mode=9 vtyp=0 voct=-1 wet=90" },
    { "BOW",           "mode=9 vtyp=1 voct=0 dry=70 wet=80 spce=35" },
    { "ORGAN",         "mode=9 vtyp=2 voct=0 wet=70 spce=20" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { H_HOME = LEARN_INST, H_NECK, H_CLOSE, H_STOP, H_SLAP, H_SETUP, H_FREEZE };
static const hint_t hints[] = {
    { "SIT AS YOU PLAY, HOLD THE KNOB: HOME", H_HOME },
    { "LIFT THE NECK: THE SOUND MOVES", H_NECK },
    { "TAP REC, PLAY: A TAKE STARTS", LEARN_REC },
    { "4 BARS AND IT LOOPS BY ITSELF", H_CLOSE },
    { "TURN THE KNOB: CLOUD, BLOOM, ECHO", LEARN_TURN },
    { "HOLD REC TO STOP, LONGER TO ERASE", H_STOP },
    { "GOLPE: SLAP THE TOP, A DRUM", H_SLAP },
    { "CLOUD: PUSH THE STICK, IT FREEZES", H_FREEZE },
    { "NECK > NECK SETUP: IT LEARNS THE GUITAR", H_SETUP },
};

nyui_t NY = { .note = -1 };

// ---------------------------------------------------------------- the grid: TEMPO, BEATS, LENGTH, METRONOME
static void grid_params(void)
{
    E.l_fpb = (float)AUDIO_SR * 60.0f / clampf(pv[P_TEMPO], 20.0f, 300.0f);
    E.l_beats = clampi((int)pv[P_BEATS], 1, 16);
    E.l_bars = bars_of[clampi((int)pv[P_LBARS], 0, 4)];
    E.l_snap = (int)pv[P_METRO] != MET_OFF;
}
static void grid_set(uint32_t beat0) { grid_params(); E.l_grid0 = beat0; }

// ---------------------------------------------------------------- REC: the looper's one button
static bool rec_hold_done, rec_erase_done;
static int  rec_press_state;
static uint32_t rec_press_f;
static uint32_t take_f0;
static bool now_on_release;                  // START AT ONCE without the metronome: the take starts at the let-go

static void rec_logic(const ctl_t *c)
{
    lstate_t s = loop_state();
    if (c->rec_press) {
        rec_press_f = c->frames; rec_press_state = s; rec_hold_done = rec_erase_done = false;
        switch (s) {
        case L_EMPTY: {
            // the grid: its first beat when the button's click has gone by (0.8 s)
            int met = (int)pv[P_METRO];
            grid_set(c->frames + (uint32_t)(AUDIO_SR * 8 / 10));
            if (met != MET_OFF) {                                // the metronome: a bar of count-in, then it records from the one
                loop_cmd(LC_REC_AT, E.l_grid0 + (uint32_t)(E.l_fpb * (float)E.l_beats + 0.5f));
                toast("ONE BAR IN", 800);
            } else if ((int)pv[P_START] == ST_NOW) { loop_cmd(LC_ARM, c->frames + 600u * AUDIO_SR); now_on_release = true; }   // (deaf: it starts at the let-go)
            else { loop_cmd(LC_ARM, c->frames); toast("PLAY: IT STARTS", 900); }
            coach_done(LEARN_REC);
            break;
        }
        case L_ARMED: loop_cmd(LC_DISARM, 0); toast("NOT RECORDING", 700); break;
        case L_REC:
            if (loop_len_s() < 0.15f) { loop_cmd(LC_DISARM, 0); toast("NOT RECORDING", 700); rec_press_state = L_ARMED; break; }   // its own click started it
            loop_cmd(LC_CLOSE_AT, c->frames); coach_done(H_CLOSE); break;
        case L_PLAY: break;                                      // a tap adds, a hold stops: known at the let-go
        case L_DUB: loop_cmd(LC_DUB_OFF, c->frames); break;
        case L_STOP: loop_cmd(LC_PLAY, 0); break;
        }
    }
    if (c->rec && !rec_hold_done && c->rec_ms >= 600) {
        rec_hold_done = true;
        switch (rec_press_state) {
        case L_REC: loop_cmd(LC_ERASE, 0); toast("TAKE DROPPED", 900); break;
        case L_PLAY: case L_DUB: loop_cmd(LC_STOP, 0); toast("STOPPED", 700); coach_done(H_STOP); break;
        case L_STOP: loop_cmd(LC_ERASE, 0); toast("LOOP ERASED", 900); rec_erase_done = true; coach_done(H_STOP); break;
        case L_EMPTY: if ((int)pv[P_START] == ST_NOW || (int)pv[P_METRO] != MET_OFF) { now_on_release = false; loop_cmd(LC_ERASE, 0); toast("TAKE DROPPED", 900); } break;
        default: break;
        }
    }
    if (c->rec && !rec_erase_done && c->rec_ms >= 2000 && (rec_press_state == L_PLAY || rec_press_state == L_DUB)) {
        rec_erase_done = true; loop_cmd(LC_ERASE, 0); toast("LOOP ERASED", 900);
    }
    if (c->rec_release && !rec_hold_done && rec_press_state == L_PLAY) loop_cmd(LC_DUB_AT, c->frames);   // from just after the let-go: its click is not in it
    if (c->rec_release && now_on_release) {                      // START AT ONCE: from 25 ms after the let-go (a hold: nothing)
        now_on_release = false;
        if (!rec_hold_done) { loop_cmd(LC_REC_AT, c->frames + (uint32_t)(AUDIO_SR * 25 / 1000 + 640)); toast("RECORDING", 700); }
        else loop_cmd(LC_DISARM, 0);
    }
}

// ---------------------------------------------------------------- the modes: what the neck does
static bool cloud_held, cloud_neck_frozen, stick_armed = true;
static int   v_note = -1, v_midi = -1; static float v_lost;
static int   cc_neck = -1, cc_lean = -1;

static float tape_rate(float n) { return n >= 0.0f ? exp2f(n) : 1.0f + 2.0f * n; }   // up to 2x; stopped half way down, backwards at the end

static float echo_time(void)
{
    float t = pv[P_ETIME];
    if ((int)pv[P_ESYNC] && loop_state() >= L_PLAY && loop_len_s() > 0.2f) {   // the loop's length over 1 2 3 4 6 8 12 16
        static const int div[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
        float best = t, bd = 1e9f, L = loop_len_s();
        for (int i = 0; i < 10; i++) { float c = L / (float)div[i]; if (c > 1.4f || c < 0.04f) continue; float d = fabsf(log2f(c / t)); if (d < bd) { bd = d; best = c; } }
        t = best;
    }
    return t;
}

static void mode_tick(const ctl_t *c)
{
    float n = NK.n, l = NK.l;
    int mode = (int)pv[P_MODE];
    E.dry = pv[P_DRY] * 0.01f;
    float wet = pv[P_WET] * 0.01f;
    float space = pv[P_SPACE] * 0.01f, shim = pv[P_SHIM] * 0.01f;
    int shint = (int)pv[P_SHINT];
    E.rsize = pv[P_RSIZE] * 0.01f;
    E.lowcut = pv[P_LOWCUT];
    E.hicut = pv[P_HICUT];
    NY.neck_what = NULL; NY.neck_amt = 0.0f; NY.neck_signed = false;
    // CLOUD
    E.c_dens = pv[P_CDENS] * exp2f(1.3f * n);
    E.c_size = pv[P_CSIZE] * 0.001f * exp2f(0.7f * n);
    E.c_reach = pv[P_CREACH];
    E.c_up = clampf(pv[P_CUP] * 0.01f + 0.7f * fmaxf(l, 0.0f), 0.0f, 1.0f);
    E.c_down = clampf(pv[P_CDOWN] * 0.01f + 0.7f * fmaxf(-l, 0.0f), 0.0f, 1.0f - E.c_up);
    E.c_rev = pv[P_CREV] * 0.01f;
    int fz = (int)pv[P_CFREEZE];
    if (fz == FRZ_STICK && !c->has_joy) fz = FRZ_NECK;          // no stick on this board: the neck all the way up
    bool push = false;                                           // the stick pushed any way: one push
    float jm = fmaxf(fabsf(c->jx), fabsf(c->jy));
    if (c->has_joy) { if (stick_armed && jm > 0.6f) { push = true; stick_armed = false; } else if (jm < 0.3f) stick_armed = true; }
    if (fz == FRZ_NECK) { if (n > 0.85f) cloud_neck_frozen = true; else if (n < 0.7f) cloud_neck_frozen = false; } else cloud_neck_frozen = false;
    if ((fz != FRZ_SLAP && fz != FRZ_STICK) || mode != M_CLOUD) cloud_held = false;
    else if (fz == FRZ_STICK && push) { cloud_held = !cloud_held; toast(cloud_held ? "FROZEN" : "FLOWING", 600); coach_done(H_FREEZE); }
    E.c_frozen = mode == M_CLOUD && (cloud_held || cloud_neck_frozen);
    NY.frozen = E.c_frozen;
    // BLOOM
    E.b_hold = clampf(pv[P_BSUST] * 0.01f + 0.6f * n, 0.0f, 1.0f);
    E.b_swell = pv[P_BSWELL];
    E.b_harm = clampf(pv[P_BHARM] * 0.01f + fmaxf(l, 0.0f), 0.0f, 1.0f);
    E.b_sub = clampf(pv[P_BSUB] * 0.01f + fmaxf(-l, 0.0f), 0.0f, 1.0f);
    E.b_catch = (int)pv[P_BCATCH];
    E.b_choke = n < -0.6f;
    // ECHO
    E.e_time = echo_time() * exp2f(0.5f * l);
    E.e_fb = clampf(pv[P_EFB] * 0.01f + 0.75f * n, 0.0f, 1.12f);
    E.e_tone = pv[P_ETONE] * 0.01f;
    E.e_wow = pv[P_EWOW] * 0.01f;
    // WAH: the neck at home is the pedal half way
    E.w_pos = clampf(0.5f + 0.5f * n, 0.0f, 1.0f);
    E.w_q = pv[P_WQ] * exp2f(0.8f * l);
    E.w_env = pv[P_WENV] * 0.01f;
    E.w_fuzz = pv[P_WFUZZ] * 0.01f;
    // OCTAVE
    E.o_sub = clampf(pv[P_OSUB] * 0.01f + fmaxf(-n, 0.0f), 0.0f, 1.0f);
    E.o_up = clampf(pv[P_OUP] * 0.01f + fmaxf(n, 0.0f), 0.0f, 1.0f);
    E.o_fifth = clampf(pv[P_OFIFTH] * 0.01f + fabsf(l), 0.0f, 1.0f);
    // WOBBLE
    E.m_depth = clampf(pv[P_MDEPTH] * 0.01f * (1.0f + n), 0.0f, 2.0f);
    E.m_rate = pv[P_MRATE] * exp2f(1.5f * l);
    // drums
    E.d_tune = pv[P_DTUNE] + (mode == M_GOLPE ? 7.0f * n : 0.0f);
    E.d_decay = pv[P_DDECAY] * (mode == M_GOLPE ? exp2f(l) : 1.0f);
    E.d_level = pv[P_DLEVEL] * 0.01f;
    // VOICE
    E.v_type = (int)pv[P_VTYPE];
    E.v_glide = pv[P_VGLIDE];
    E.v_level = clampf(1.0f + n, 0.0f, 1.5f);

    switch (mode) {
    case M_CLOUD: wet *= clampf(1.0f + 0.8f * n, 0.15f, 1.5f); NY.neck_what = "CLOUD"; NY.neck_amt = n; NY.neck_signed = true; break;
    case M_BLOOM: wet *= clampf(1.0f + 0.5f * n, 0.3f, 1.5f); NY.neck_what = E.b_choke ? "CHOKE" : E.b_hold >= 0.97f ? "HOLD FOREVER" : "HOLD"; NY.neck_amt = E.b_hold; break;
    case M_ECHO: NY.neck_what = E.e_fb > 1.0f ? "RUNAWAY" : "REPEATS"; NY.neck_amt = E.e_fb / 1.12f; break;
    case M_WAH: NY.neck_what = "WAH"; NY.neck_amt = E.w_pos; break;
    case M_SHIMMER:
        shim = clampf(shim * (1.0f + n), 0.0f, 1.0f); space = clampf(space * (1.0f + 0.6f * n), 0.0f, 1.0f);
        if (l < -0.5f) shint = 1;
        NY.neck_what = "SHIMMER"; NY.neck_amt = shim; break;
    case M_OCTAVE: NY.neck_what = n >= 0.0f ? "OCT UP" : "OCT DOWN"; NY.neck_amt = n; NY.neck_signed = true; break;
    case M_WOBBLE: NY.neck_what = "WOBBLE"; NY.neck_amt = E.m_depth * 0.5f; break;
    case M_GOLPE: NY.neck_what = "TUNE"; NY.neck_amt = n; NY.neck_signed = true; break;
    case M_VOICE: NY.neck_what = "VOICE"; NY.neck_amt = E.v_level / 1.5f; break;
    default: break;
    }
    E.wet = wet; E.space = space; E.shim = shim; E.shim_int = (float)shint;
    // the loop
    int ln = (int)pv[P_LNECK];
    E.l_level = pv[P_LLEVEL] * 0.01f * (ln == LN_LEVEL ? clampf(1.0f + n, 0.0f, 1.4f) : 1.0f);
    E.l_rate = ln == LN_TAPE ? tape_rate(n) : 1.0f;
    E.l_keep = pv[P_KEEP] * 0.01f;
    E.l_trig = exp2f(pv[P_TRIG] * 0.16609640f);
    if (ln && !NY.neck_what) { NY.neck_what = ln == LN_TAPE ? "TAPE" : "LOOP"; NY.neck_amt = ln == LN_TAPE ? n : clampf(1.0f + n, 0.0f, 1.4f) / 1.4f; NY.neck_signed = ln == LN_TAPE; }
    E.mode = mode;
    NY.mode = mode;
    // the grid follows TEMPO, BEATS, LENGTH as they change; the click sounds while armed or recording
    grid_params();
    lstate_t ls = loop_state();
    int met = (int)pv[P_METRO];                                  // COUNT-IN ticks only before the take: nothing the mic hears of it is recorded
    E.m_click = (met == MET_SOUND && (ls == L_ARMED || ls == L_REC)) || (met == MET_COUNT && ls == L_ARMED);
}

// ---------------------------------------------------------------- slaps: drums, a frozen cloud, a choked bloom
static void slap_tick(void)
{
    slap_t s;
    int mode = (int)pv[P_MODE], sm = (int)pv[P_SLAP];
    float thr = ear_slap_thr();
    while (ear_slap_pop(&s)) {
        bool drum = sm == SLAP_DRUM || (sm == SLAP_AUTO && mode == M_GOLPE);
        if (mode == M_BLOOM) eng_bloom_cancel++;                 // its thump is not a note to hold
        if (mode == M_CLOUD && (int)pv[P_CFREEZE] == FRZ_SLAP) {
            cloud_held = !cloud_held;
            toast(cloud_held ? "FROZEN" : "FLOWING", 600);
        }
        int vel = clampi((int)(40.0f + 87.0f * (s.jolt - thr) / (2.0f * thr)), 30, 127);
        NY.slap_seq++; NY.slap_top = s.top; NY.slap_vel = (float)vel / 127.0f; NY.slap_drum = 0;
        if (drum) {
            int d = (int)(s.top ? pv[P_DTOP] : pv[P_DSIDE]);
            if (d != DR_OFF) {
                trig_t t = { (uint8_t)d, (uint8_t)vel };
                eng_trigger(&t);
                midi_note_ch(10, drum_gm[d], vel); midi_note_ch(10, drum_gm[d], 0);
                NY.slap_drum = d;
            }
            coach_done(H_SLAP);
        }
        ui_awake();
    }
}

// ---------------------------------------------------------------- VOICE: the note heard, out as MIDI too
static void voice_tick(const ctl_t *c)
{
    bool on = (int)pv[P_MODE] == M_VOICE;
    ear_track(on);
    if (!on) {
        E.v_gate = 0;
        if (v_midi >= 0) { midi_note(v_midi, 0); v_midi = -1; }
        v_note = -1; NY.note = -1;
        return;
    }
    float hz = ear_hz();
    bool sounding = eng_env > 0.004f;
    if (hz > 0.0f && sounding) {
        float m = 69.0f + 12.0f * log2f(hz / 440.0f);
        if (v_note < 0 || fabsf(m - (float)v_note) > 0.6f) v_note = (int)lroundf(m);
        NY.note = v_note; NY.cents = (m - (float)v_note) * 100.0f;
        E.v_hz = hz * exp2f(pv[P_VOCT]);
        E.v_gate = 1;
        v_lost = 0.0f;
    } else {
        v_lost += c->dt;
        if (v_lost > 0.06f || !sounding) { E.v_gate = 0; v_note = -1; NY.note = -1; }
    }
    int want = E.v_gate && pv[P_VMIDI] >= 1.0f ? v_note + 12 * (int)pv[P_VOCT] : -1;
    if (want != v_midi) {
        if (v_midi >= 0) midi_note(v_midi, 0);
        if (want >= 0 && want < 128) midi_note(want, clampi((int)(eng_env * 900.0f) + 40, 40, 127));
        v_midi = want >= 0 && want < 128 ? want : -1;
    }
}

// ---------------------------------------------------------------- MIDI, the LED
static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) != 0x90 || d2 == 0) return;
    switch (d1) {                                                // a MIDI footswitch: undo, stop, erase
    case 61: loop_cmd(LC_UNDO, 0); break;
    case 62: loop_cmd(LC_STOP, 0); break;
    case 63: loop_cmd(LC_ERASE, 0); break;
    default: break;
    }
}

static const uint8_t mode_rgb[NMODE][3] = {
    { 255, 200, 120 }, { 120, 160, 255 }, { 255, 80, 180 }, { 255, 150, 0 }, { 200, 255, 0 },
    { 180, 220, 255 }, { 120, 0, 255 }, { 0, 255, 200 }, { 255, 60, 20 }, { 0, 200, 255 },
};
static void led_tick(const ctl_t *c)
{
    lstate_t s = loop_state();
    static int beat_seen = -1; static float flash; static bool one;
    flash *= 0.7f;
    if (eng_beat != beat_seen) { beat_seen = eng_beat; flash = 1.0f; one = eng_beat >= 0 && eng_beat % clampi((int)pv[P_BEATS], 1, 16) == 0; }
    if ((int)pv[P_METRO] != MET_OFF && (s == L_ARMED || s == L_REC) && flash > 0.1f && eng_beat >= 0) {   // the metronome, as light
        uint8_t v = (uint8_t)(255.0f * flash);
        if (one) led_rgb(v, s == L_REC ? 0 : v, s == L_REC ? 0 : v); else led_rgb(v / 3, v / 3, v / 3);
        return;
    }
    float lv = clampf(eng_out_lvl * 3.0f, 0.12f, 1.0f);
    if (s == L_REC) { float p = 0.6f + 0.4f * sinf((float)c->ticks * 0.3f); led_rgb((uint8_t)(255.0f * p), 0, 0); return; }
    if (s == L_ARMED) { led_rgb((c->ticks / 25) & 1 ? 255 : 30, 0, 0); return; }
    if (s == L_DUB) { led_rgb(255, 90, 0); return; }
    const uint8_t *k = mode_rgb[clampi((int)pv[P_MODE], 0, NMODE - 1)];
    led_rgb((uint8_t)((float)k[0] * lv), (uint8_t)((float)k[1] * lv), (uint8_t)((float)k[2] * lv));
}

static void cc_tick(void)
{
    if (pv[CP(CP_MIDI_OUT)] < 1.0f || !NK.ok) return;
    int a = clampi((int)lroundf((NK.n + 1.0f) * 63.5f), 0, 127), b = clampi((int)lroundf((NK.l + 1.0f) * 63.5f), 0, 127);
    if (abs(a - cc_neck) >= 2 || (a != cc_neck && (a == 0 || a == 127))) { midi_cc(1, a); cc_neck = a; }
    if (abs(b - cc_lean) >= 2 || (b != cc_lean && (b == 0 || b == 127))) { midi_cc(2, b); cc_lean = b; }
}

// ---------------------------------------------------------------- the core's calls
static void tick(const ctl_t *c)
{
    static bool grabbed;
    ear_tick(c);
    if (!ear_setup_state() && !ear_slapset_state() && ear_setup_wanted(c)) menu_leave();   // the first time on a guitar: NECK SETUP by itself
    if (ear_slapset_state()) {                                   // SLAP SETUP has the screen and the knob
        coach_pause();
        if (c->rec_press || c->knob_tap) { ear_slapset_cancel(); toast("SETUP STOPPED", 800); }
    } else if (ear_setup_state()) {                              // NECK SETUP has the screen and the knob
        coach_pause();
        if (c->rec_press) { ear_setup_cancel(); toast("SETUP STOPPED", 800); }
        else if (c->knob_tap) {
            if (ear_setup_state() == SU_TIP) ear_setup_skip();   // the tip is optional
            else { ear_setup_cancel(); toast("SETUP STOPPED", 800); }
        }
        if (ear_setup_state() == SU_DONE) coach_done(H_SETUP);
    } else rec_logic(c);
    if (c->ticks & 1) guard_tick();
    bool flow = ear_setup_state() || ear_slapset_state();
    if (flow && !grabbed) { knob_grab(true); grabbed = true; }
    else if (!flow && grabbed) { knob_grab(false); grabbed = false; }
    mode_tick(c);
    slap_tick();
    voice_tick(c);
    static int caps_seen;                                        // BLOOM caught a note: find its period, the grains go in phase
    if (eng_bloom_caps != caps_seen) {
        caps_seen = eng_bloom_caps;
        int k = eng_bloom_cur;
        eng_bloom_period[k] = ear_period(bloom_buf(k), BLOOM_CAP_N, NULL);
    }
    if (fabsf(NK.n) > 0.6f) coach_done(H_NECK);
    if (eng_env > 0.01f) ui_awake();
    NY.lstate = loop_state();
    NY.tape = loop_rate_now();
    NY.take_ms = (int)(loop_len_s() * 1000.0f);
    led_tick(c);
    cc_tick();
    (void)take_f0;
}

static bool hold(void)
{
    if (!NK.ok && pv[P_NECKON] >= 1.0f) { toast("NO TILT SENSOR", 900); return true; }
    ear_set_home();
    toast("HOME: WHERE YOU SIT", 1000);
    coach_done(H_HOME);
    return true;
}

static void action(int p)
{
    switch (p) {
    case P_SETUP: ear_slapset_cancel(); ear_setup_begin(); menu_leave(); break;
    case P_SLAPSET: ear_setup_cancel(); ear_slapset_begin(); menu_leave(); break;
    case P_HOME: ear_set_home(); toast("HOME: WHERE YOU SIT", 1000); coach_done(H_HOME); break;
    case P_UNDO:
        if (loop_can_undo()) { loop_cmd(LC_UNDO, 0); toast("LAST LAYER OFF", 900); }
        else toast(loop_state() < L_PLAY ? "NO LOOP" : !loop_undo_fits() ? "TOO LONG TO UNDO" : "NOTHING TO UNDO", 900);
        break;
    case P_ERASE: loop_cmd(LC_ERASE, 0); toast("LOOP ERASED", 900); break;
    default: break;
    }
}

static void changed(int p) { (void)p; }
static void init(void) { eng_init(); }
static void audio(const float *in, float *out, int n) { eng_audio(in, out, n); }
static void draw(const view_t *v) { if (ear_slapset_state()) scene_slapset(v); else if (ear_setup_state()) scene_setup(v); else scene_draw(v); }

const instrument_t INST = {
    .name = "NYLON", .tagline = "THE NECK IS THE PEDAL",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 8,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .hold = hold,
};
