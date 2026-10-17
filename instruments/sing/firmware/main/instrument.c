// SING's host (sing.h): the params and the sounds, the song and the link, the mixer and the controls.
//
//   the knob      turn: the next voice (a sound); tap: the menu; hold: hold the motion
//   REC           hold: loop what comes out (or add to it); tap: stop / play the loop; stopped, hold: a new take
//   the tilt      TILT: LOOP SPEED (the Samplooler: nose down faster, nose up slower and backwards; flat = in time;
//                 SPEED MODE: KEEP PITCH stays in key, OCTAVES snaps to 2x 1x 1/2x, TAPE as it was), or PITCH, or
//                 FORMANT, or nothing
//   the roll      left throws the voice into the echo, right stutters it (the Samplooler's roll)
//   the stick     X the formant, Y the pitch (a bend), while it is pushed
//   MIDI notes    held notes are the chord: the vocoder plays them, CHORD harmonies and tuning use them
//
// The song: tempo, key, scale and chords, shared with a BAND (or any board on the link) both ways. The chord moves
// by bar as SONG > CHORDS says, within the session's loop once there is one, exactly as BAND counts it.
#include "sing.h"
#include "ui.h"
#include "params.h"
#include "front.h"
#include "link.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#define CYCLES() esp_cpu_get_cycle_count()
#else
#include <time.h>
static inline uint32_t CYCLES(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_nsec / 4); }
#endif
// What each part of the audio costs, in CPU cycles summed over blocks (the log shows it every 10 s): the input stage,
// the voices, the vocoder, the loop, the FX bus. A block is 2 ms: 480000 cycles at 240 MHz (on the host the unit is
// 4 ns, so the same arithmetic gives microseconds x 0.96).
static volatile uint32_t prof_cyc[5], prof_blocks;
void sing_prof(float *us, int n)
{
    uint32_t b = prof_blocks ? prof_blocks : 1;
    for (int i = 0; i < n && i < 5; i++) us[i] = (float)prof_cyc[i] / (float)b / 240.0f;
    for (int i = 0; i < 5; i++) prof_cyc[i] = 0;
    prof_blocks = 0;
}

// ---------------------------------------------------------------- params
enum {
    P_TUNE, P_SPEED, P_VIB, P_SHIFT, P_FORMANT, P_LEAD, P_DRY,
    P_HMODE, P_H1, P_H2, P_H3, P_HLVL,
    P_CHOIR, P_CTYPE, P_SPREAD, P_CTIME, P_HUMAN,
    P_VOC, P_VCAR, P_VOCT, P_VBANDS, P_VSHARP, P_VREL, P_VSIB, P_VBRIGHT, P_VDET,
    P_ECHO, P_ETIME, P_EFB, P_ETONE, P_EDUCK, P_REV, P_RTIME, P_RTONE, P_SHIM,
    P_TUNETO, P_TEMPO, P_KEY, P_SCALE, P_PROG, P_HOLD, P_BARS,
    P_GATE, P_COMP, P_DEESS, P_GUARD, P_MONITOR,
    P_LSYNC, P_LSTART, P_LLVL, P_LKEEP, P_LTILT, P_LSPMODE, P_LROLL, P_LCLEAR,
    P_LINK, P_GROUP, P_RANGE, P_PEERS,
    P_N
};
enum { G_VOICE, G_HARM, G_CHOIR, G_VOC, G_FX, G_SONG, G_INPUT, G_LOOP, G_LINK, G_N };
static const char *const groups[G_N] = { "VOICE", "HARMONY", "CHOIR", "VOCODER", "ECHO + VERB", "SONG", "INPUT", "LOOP", "LINK" };

static const char *const hmode_l[] = { "OFF", "IN KEY", "CHORD", "FIXED" };
static const char *const iv_l[] = { "OFF", "3RD UP", "4TH UP", "5TH UP", "6TH UP", "OCT UP", "3RD DOWN", "4TH DOWN", "5TH DOWN", "6TH DOWN", "OCT DOWN" };
static const int8_t iv_steps[] = { 0, 2, 3, 4, 5, 7, -2, -3, -4, -5, -7 };        // scale steps
static const int8_t iv_tones[] = { 0, 1, 1, 2, 2, 3, -1, -1, -2, -2, -3 };        // chord tones
static const int8_t iv_semis[] = { 0, 4, 5, 7, 9, 12, -3, -5, -7, -8, -12 };      // fixed
static const char *const ctype_l[] = { "UNISON", "OCTAVES", "CHORD" };
static const char *const vcar_l[] = { "CHORD", "NOTE", "CHORD+NOTE", "NOISE" };
static const char *const etime_l[] = { "1/16", "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2", "1 BAR" };
static const float etime_beats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f };
static const char *const tuneto_l[] = { "SCALE", "CHORD", "ALL NOTES", "PENTATONIC", "BLUES" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_l[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
// progressions: scale degrees, one per bar (0 = I); the same list as BAND's, so the link's numbers mean the same
typedef struct { int n; int8_t d[12]; } prog_t;
static const prog_t progs[] = {
    { 1, { 0 } }, { 4, { 0, 4, 5, 3 } }, { 4, { 5, 3, 0, 4 } }, { 4, { 0, 5, 3, 4 } }, { 2, { 0, 3 } },
    { 4, { 1, 4, 0, 0 } }, { 4, { 0, 6, 5, 4 } }, { 2, { 0, 3 } }, { 12, { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 } },
};
#define NPROGS ((int)(sizeof progs / sizeof progs[0]))
static const char *const prog_l[NPROGS] = { "HOLD", "I V vi IV", "vi IV I V", "I vi IV V", "I IV", "ii V I", "i VII VI V", "i iv", "12 BAR BLUES" };
static const char *const chord_l[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const bars_l[] = { "1 BAR", "2 BARS", "HALF BAR" };
static const char *const sync_l[] = { "BARS", "FREE" };
static const char *const start_l[] = { "SOUND", "PRESS" };
static const char *const tilt_l[] = { "LOOP SPEED", "PITCH", "FORMANT", "OFF" };
static const char *const spmode_l[] = { "KEEP PITCH", "OCTAVES", "TAPE" };
static const char *const roll_l[] = { "ECHO+STUTTER", "OFF" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };

#define SYS PF_SYS
static const param_t params[P_N] = {
    [P_TUNE]    = { "TUNE", "vtun", G_VOICE, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HOW HARD IT PULLS TO A NOTE" },
    [P_SPEED]   = { "TUNE SPEED", "vspd", G_VOICE, PK_NUM, PF_INT, 0, 400, 10, 80, "ms", NULL, "0: THE HARD ROBOT STEP" },
    [P_VIB]     = { "KEEP VIBRATO", "vvib", G_VOICE, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "YOUR OWN WOBBLE STAYS" },
    [P_SHIFT]   = { "PITCH", "vshf", G_VOICE, PK_NUM, PF_INT | PF_SIGNED, -24, 24, 1, 0, "st", NULL, "THE VOICE UP OR DOWN" },
    [P_FORMANT] = { "THROAT", "vfmt", G_VOICE, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "BIGGER OR SMALLER SINGER" },
    [P_LEAD]    = { "TUNED", "vlvl", G_VOICE, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE TUNED VOICE'S LEVEL" },
    [P_DRY]     = { "PLAIN", "vdry", G_VOICE, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "YOUR VOICE AS IT IS" },
    [P_HMODE]   = { "HARMONY", "hmod", G_HARM, PK_ENUM, 0, 0, 3, 1, 0, "", hmode_l, "HOW THE VOICES FIND NOTES" },
    [P_H1]      = { "VOICE 1", "hiv1", G_HARM, PK_ENUM, 0, 0, 10, 1, 1, "", iv_l, "A SINGER ABOVE OR BELOW" },
    [P_H2]      = { "VOICE 2", "hiv2", G_HARM, PK_ENUM, 0, 0, 10, 1, 0, "", iv_l, "A SECOND ONE" },
    [P_H3]      = { "VOICE 3", "hiv3", G_HARM, PK_ENUM, 0, 0, 10, 1, 0, "", iv_l, "A THIRD ONE" },
    [P_HLVL]    = { "HARM LEVEL", "hlvl", G_HARM, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "HOW LOUD THEY SING" },
    [P_CHOIR]   = { "CHOIR", "cmix", G_CHOIR, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "HOW MANY OF YOU, HOW LOUD" },
    [P_CTYPE]   = { "CHOIR TYPE", "ctyp", G_CHOIR, PK_ENUM, 0, 0, 2, 1, 0, "", ctype_l, "SAME NOTE, OCTAVES, CHORD" },
    [P_SPREAD]  = { "SPREAD", "cspr", G_CHOIR, PK_NUM, PF_INT, 0, 40, 2, 14, "c", NULL, "HOW OUT OF TUNE THEY ARE" },
    [P_CTIME]   = { "LOOSENESS", "ctim", G_CHOIR, PK_NUM, PF_INT, 0, 60, 2, 20, "ms", NULL, "HOW LATE THEY COME IN" },
    [P_HUMAN]   = { "HUMAN", "chum", G_CHOIR, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "SLOW DRIFT IN EVERY VOICE" },
    [P_VOC]     = { "VOCODER", "olvl", G_VOC, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "YOUR VOICE PLAYS A SYNTH" },
    [P_VCAR]    = { "PLAYS", "ocar", G_VOC, PK_ENUM, 0, 0, 3, 1, 0, "", vcar_l, "THE CHORD, YOUR NOTE, NOISE" },
    [P_VOCT]    = { "OCTAVE", "ooct", G_VOC, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "THE SYNTH UP OR DOWN" },
    [P_VBANDS]  = { "BANDS", "oban", G_VOC, PK_NUM, PF_INT, 8, 20, 1, 16, "", NULL, "MORE: CLEARER WORDS" },
    [P_VSHARP]  = { "SHARPNESS", "osha", G_VOC, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SMOOTH OR RINGING BANDS" },
    [P_VREL]    = { "RELEASE", "orel", G_VOC, PK_NUM, PF_LOG, 5, 300, 1.15f, 40, "s", NULL, "HOW LONG THE BANDS RING" },
    [P_VSIB]    = { "ESSES", "osib", G_VOC, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "YOUR S AND T ON TOP" },
    [P_VBRIGHT] = { "BUZZ", "obri", G_VOC, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK OR BUZZY SYNTH" },
    [P_VDET]    = { "DETUNE", "odet", G_VOC, PK_NUM, PF_INT, 0, 30, 1, 8, "c", NULL, "THE SYNTH'S WIDTH" },
    [P_ECHO]    = { "ECHO", "fech", G_FX, PK_NUM, PF_INT, 0, 100, 5, 15, "%", NULL, "HOW LOUD THE ECHO IS" },
    [P_ETIME]   = { "ECHO TIME", "fetm", G_FX, PK_ENUM, 0, 0, 6, 1, 3, "", etime_l, "IN TIME WITH THE SONG" },
    [P_EFB]     = { "REPEATS", "fefb", G_FX, PK_NUM, PF_INT, 0, 95, 5, 35, "%", NULL, "HOW LONG IT GOES ON" },
    [P_ETONE]   = { "ECHO TONE", "feto", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "DARK TAPE TO BRIGHT" },
    [P_EDUCK]   = { "DUCK", "fedk", G_FX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "ECHO WAITS WHILE YOU SING" },
    [P_REV]     = { "REVERB", "frev", G_FX, PK_NUM, PF_INT, 0, 100, 5, 20, "%", NULL, "HOW LOUD THE ROOM IS" },
    [P_RTIME]   = { "ROOM SIZE", "frtm", G_FX, PK_NUM, PF_LOG, 0.3f, 12.0f, 1.12f, 1.8f, "s", NULL, "HOW LONG IT RINGS" },
    [P_RTONE]   = { "ROOM TONE", "frto", G_FX, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO BRIGHT" },
    [P_SHIM]    = { "SHIMMER", "fshm", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN OCTAVE UP IN THE ROOM" },
    [P_TUNETO]  = { "TUNE TO", "stto", G_SONG, PK_ENUM, 0, 0, 4, 1, 0, "", tuneto_l, "THE NOTES IT TUNES TO" },
    [P_TEMPO]   = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT | SYS, 60, 180, 1, 100, "bpm", NULL, "SHARED WITH A LINKED BAND" },
    [P_KEY]     = { "KEY", "skey", G_SONG, PK_ENUM, SYS, 0, 11, 1, 0, "", key_l, "THE KEY YOU SING IN" },
    [P_SCALE]   = { "SCALE", "sscl", G_SONG, PK_ENUM, SYS, 0, NSCALES - 1, 1, 0, "", scale_l, "THE NOTES OF THE KEY" },
    [P_PROG]    = { "CHORDS", "sprg", G_SONG, PK_ENUM, SYS, 0, NPROGS - 1, 1, 1, "", prog_l, "HOW THE CHORDS MOVE BY BAR" },
    [P_HOLD]    = { "HOLD CHORD", "schd", G_SONG, PK_ENUM, SYS, 0, 6, 1, 0, "", chord_l, "THE CHORD FOR CHORDS: HOLD" },
    [P_BARS]    = { "CHORD BARS", "sbar", G_SONG, PK_ENUM, SYS, 0, 2, 1, 0, "", bars_l, "HOW LONG EACH CHORD LASTS" },
    [P_GATE]    = { "GATE", "igat", G_INPUT, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "SILENCE BETWEEN WORDS" },
    [P_COMP]    = { "EVEN OUT", "icmp", G_INPUT, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "SOFT AND LOUD CLOSER" },
    [P_DEESS]   = { "DE-ESS", "ides", G_INPUT, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "SHARP S SOUNDS SOFTER" },
    [P_GUARD]   = { "HOWL GUARD", "igrd", G_INPUT, PK_NUM, PF_INT | SYS, 0, 100, 5, 60, "%", NULL, "STOPS THE SPEAKER RINGING" },
    [P_MONITOR] = { "MONITOR", "imon", G_INPUT, PK_NUM, PF_INT | SYS, 0, 100, 5, 100, "%", NULL, "LOWER IF THE SPEAKER RINGS" },
    [P_LSYNC]   = { "LOOP", "qsyn", G_LOOP, PK_ENUM, SYS, 0, 1, 1, 0, "", sync_l, "WHOLE BARS, OR ANY LENGTH" },
    [P_LSTART]  = { "LOOP START", "qsta", G_LOOP, PK_ENUM, SYS, 0, 1, 1, 0, "", start_l, "AT YOUR FIRST SOUND, OR REC" },
    [P_LLVL]    = { "LOOP LEVEL", "qlvl", G_LOOP, PK_NUM, PF_INT | SYS, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE LOOP PLAYS" },
    [P_LKEEP]   = { "LOOP KEEP", "qkep", G_LOOP, PK_NUM, PF_INT | SYS, 0, 100, 5, 100, "%", NULL, "LESS: OLD LAYERS FADE" },
    [P_LTILT]   = { "TILT", "qtlt", G_LOOP, PK_ENUM, SYS, 0, 3, 1, 0, "", tilt_l, "WHAT LEANING FORWARD DOES" },
    [P_LSPMODE] = { "SPEED MODE", "qspm", G_LOOP, PK_ENUM, SYS, 0, 2, 1, 0, "", spmode_l, "THE PITCH WHEN TILTED" },
    [P_LROLL]   = { "ROLL", "qrol", G_LOOP, PK_ENUM, SYS, 0, 1, 1, 0, "", roll_l, "LEFT: ECHO  RIGHT: STUTTER" },
    [P_LCLEAR]  = { "CLEAR LOOP", "qclr", G_LOOP, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "PRESS TO ERASE IT" },
    [P_LINK]    = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]   = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_NOCC | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]   = { "RANGE", "npow", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]   = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
};
#define P(i) pv[(i)]

// ---------------------------------------------------------------- the song
song_t song = { .bpm = 100, .fpb = 19200, .bar_beats = 4, .scale = scales[0], .scale_n = 7, .chord = { 48, 52, 55 }, .chord_n = 3 };
sing_ui_t sui;
static int chord_live = -1;                     // a chord played live on a linked board (its bass's lean)
static uint16_t midi_held[8];                   // MIDI notes held: 128 bits
static bool link_up, applying;
static int  peers_was;
static float out_env_v;                         // the output's envelope, for the guard (audio task)

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static int32_t imod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
static int scale_note(int d, int o)
{
    int n = 7, oct = d >= 0 ? d / n : -((-d + n - 1) / n), step = d - oct * n;
    return 12 * (o + 1 + oct) + song.key + song.scale[step];
}
static uint16_t pc_mask_of(const int *notes, int n) { uint16_t m = 0; for (int i = 0; i < n; i++) m |= (uint16_t)(1u << (((notes[i] % 12) + 12) % 12)); return m; }
static uint16_t rot(uint16_t m, int k) { k = ((k % 12) + 12) % 12; return (uint16_t)(((m << k) | (m >> (12 - k))) & 0x0FFF); }

static void set_song(uint32_t frame)
{
    int s = (int)P(P_SCALE);
    song.key = (int)P(P_KEY); song.scale = scales[s]; song.scale_n = 7;
    uint16_t m = 0;
    for (int i = 0; i < 7; i++) m |= (uint16_t)(1u << song.scale[i]);
    song.scale_mask = rot(m, song.key);
    // the bar in the chord cycle: within the session's loop when there is one (as BAND counts it)
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t span = song.loop_beats ? (int32_t)(fpb * (uint32_t)song.loop_beats) : (int32_t)(fpb * 12288u);
    float beat = (float)imod((int32_t)(frame - song.origin), span) / (float)fpb;
    static const float bar_beats[] = { 4.0f, 8.0f, 2.0f };
    uint32_t bar = (uint32_t)(beat / bar_beats[(int)P(P_BARS)]);
    const prog_t *p = &progs[(int)P(P_PROG)];
    int d = chord_live >= 0 ? chord_live : (int)P(P_PROG) == 0 ? (int)P(P_HOLD) : p->d[bar % (uint32_t)p->n];
    song.degree = d;
    int root = scale_note(d, 3), pc = ((root % 12) + 12) % 12;
    song.chord_n = 3;
    song.chord[0] = 48 + pc;                                       // root in C3..B3, the third and fifth above it
    song.chord[1] = song.chord[0] + ((scale_note(d + 2, 3) - root) % 12 + 12) % 12;
    song.chord[2] = song.chord[0] + ((scale_note(d + 4, 3) - root) % 12 + 12) % 12;
    // a MIDI keyboard holding notes is the chord
    int held[8], nh = 0;
    for (int n = 0; n < 128 && nh < 4; n++) if (midi_held[n >> 4] & (1u << (n & 15))) held[nh++] = n;
    sui.midi_chord = nh > 0;
    if (nh) { song.chord_n = nh; for (int i = 0; i < nh; i++) song.chord[i] = held[i]; }
    song.chord_mask = pc_mask_of(song.chord, song.chord_n);
}

// ---------------------------------------------------------------- the link (as BAND's: see its instrument.c)
static void link_start(void)
{
    link_init("SING", 1);
    link_boot_time(tempo_fpb(P(P_TEMPO)), song.origin, song.bar_beats);
    link_boot_harm((int)P(P_KEY), (int)P(P_SCALE), (int)P(P_PROG), (int)P(P_HOLD), (int)P(P_BARS));
    link_enable(P(P_LINK) >= 1.0f, (int)P(P_GROUP), (int)P(P_RANGE));
    link_up = true;
}

bool host_tempo_heard(void)
{
    link_peer_t pr[LINK_PEERS_MAX];
    return link_peers(pr, LINK_PEERS_MAX) > 0 || link_time()->beats > 0;
}
void host_set_tempo(uint32_t fpb, uint32_t origin)
{
    song.fpb = fpb; song.origin = origin; song.origin_frac = 0;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    applying = true; param_set(P_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false;
    link_set_time(fpb, origin, song.bar_beats, 0);
}
void host_loop_made(int beats)
{
    if (beats > 0) {
        if (!link_time()->beats) link_set_time(song.fpb, song.origin, song.bar_beats, beats);
        link_set_flags(LINKF_LOOP);
    } else link_set_flags(0);
}

static void tempo_set_here(void)
{
    uint32_t fpb = tempo_fpb(P(P_TEMPO));
    if (fpb == song.fpb) return;
    if (loop_state() != LOOPER_EMPTY || (link_flags_any() & LINKF_LOOP)) {
        applying = true; param_set(P_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false;
        toast("A LOOP KEEPS THE TEMPO", 1200);
        return;
    }
    uint32_t now = audio_frames();
    int32_t span = (int32_t)(song.fpb * 12288u);
    float beat = (float)imod((int32_t)(now - song.origin), span) / (float)song.fpb;
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song.fpb = fpb; song.origin = origin; song.origin_frac = 0;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    link_set_time(fpb, origin, song.bar_beats, 0);
}
static void harm_set_here(void) { link_set_harm((int)P(P_KEY), (int)P(P_SCALE), (int)P(P_PROG), (int)P(P_HOLD), (int)P(P_BARS), -1); }

static void link_follow(void)
{
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb) {
            bool tempo = fpb != song.fpb;
            song.fpb = fpb; song.origin = o; song.origin_frac = f;
            song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
            if (tempo && (int)(song.bpm + 0.5f) != (int)P(P_TEMPO)) { applying = true; param_set(P_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false; }
        }
    }
    song.loop_beats = link_time()->beats;
    link_time_news();
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        param_set(P_KEY, h->key); param_set(P_SCALE, h->scale); param_set(P_PROG, h->prog);
        param_set(P_HOLD, h->hold); param_set(P_BARS, h->bars);
        applying = false;
        chord_live = h->live;
    }
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (np != peers_was) {
        char b[40];
        if (np > peers_was && np > 0) snprintf(b, sizeof b, "LINKED: %s", pr[np - 1].name);
        else snprintf(b, sizeof b, np ? "A BOARD LEFT" : "LINK: ALONE");
        toast(b, 1500);
        peers_was = np;
    }
    sui.peers = np;
}

// ---------------------------------------------------------------- the engines' settings from the params
static float bend_s, fmt_s;
static voice_params_t vp;
static voc_params_t op;
static fx_in_params_t ip;
static fx_bus_params_t bp;
static loop_ctl_t lc = { .sync = true, .speed = 1, .level = 0.8f, .keep = 1 };
static smooth_t g_dry, g_voc, g_mon;                            // (the tuned voice's level is the engine's, lead_gain)
static volatile float t_dry, t_lead, t_voc, t_mon;

static uint16_t tune_mask(void)
{
    switch ((int)P(P_TUNETO)) {
    case 1: return song.chord_mask;
    case 2: return 0;
    case 3: {                                                      // the pentatonic of the key's scale (major or minor)
        static const int maj[] = { 0, 1, 2, 4, 5 }, min[] = { 0, 2, 3, 4, 6 };
        bool minor = song.scale[2] == 3;
        uint16_t m = 0;
        for (int i = 0; i < 5; i++) m |= (uint16_t)(1u << song.scale[minor ? min[i] : maj[i]]);
        return rot(m, song.key);
    }
    case 4: return rot((uint16_t)((1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10)),
                       song.scale[2] == 3 ? song.key : song.key + 9);   // the blues scale on the key (minor), or its relative minor
    default: return song.scale_mask;
    }
}

static void engines(void)
{
    uint16_t tm = tune_mask();
    sui.tune_mask = tm; sui.tune_to = (int)P(P_TUNETO);
    vp.scale_mask = tm;
    vp.tune = P(P_TUNE) * 0.01f;
    vp.speed_ms = P(P_SPEED);
    vp.keep_vib = P(P_VIB) * 0.01f;
    vp.shift = P(P_SHIFT) + bend_s;
    vp.formant = clampf(P(P_FORMANT) + fmt_s, -12.0f, 12.0f);
    vp.lead_gain = P(P_LEAD) * 0.01f;                             // the engine skips the tuned voice at 0
    int hm = (int)P(P_HMODE);
    vp.harm_mode = hm == 0 ? HARM_OFF : hm == 1 ? HARM_SCALE : hm == 2 ? HARM_CHORD : HARM_SEMIS;
    for (int k = 0; k < 3; k++) {
        int iv = (int)P(P_H1 + k);
        vp.harm[k] = hm == 1 ? iv_steps[iv] : hm == 2 ? iv_tones[iv] : iv_semis[iv];
        vp.harm_gain[k] = iv && hm ? P(P_HLVL) * 0.01f : 0.0f;
    }
    vp.chord_mask = song.chord_mask;
    vp.choir = P(P_CHOIR) * 0.01f;
    vp.choir_mode = (int)P(P_CTYPE) == 0 ? CHOIR_UNISON : (int)P(P_CTYPE) == 1 ? CHOIR_OCTAVES : CHOIR_CHORD;
    vp.choir_spread = P(P_SPREAD);
    vp.choir_time_ms = P(P_CTIME);
    vp.humanize = P(P_HUMAN) * 0.01f;
    voice_set(&vp);

    voice_state_t vs; voice_state(&vs);
    int car = (int)P(P_VCAR);
    op.carrier = car == 0 ? VOC_CHORD : car == 1 ? VOC_NOTE : car == 2 ? VOC_CHORD_NOTE : VOC_NOISE;
    op.chord_n = song.chord_n > 4 ? 4 : song.chord_n;
    for (int i = 0; i < op.chord_n; i++) op.chord[i] = song.chord[i];
    op.note = vs.voiced ? vs.out_note : 0.0f;
    op.octave = (int)P(P_VOCT);
    op.bands = P(P_VBANDS);
    op.sharp = P(P_VSHARP) * 0.01f;
    op.release_ms = P(P_VREL) * 1000.0f;
    op.sibilance = P(P_VSIB) * 0.01f;
    op.bright = P(P_VBRIGHT) * 0.01f;
    op.detune = P(P_VDET);
    vocoder_set(&op);

    ip.gate = P(P_GATE) * 0.01f; ip.comp = P(P_COMP) * 0.01f; ip.guard = P(P_GUARD) * 0.01f; ip.deess = P(P_DEESS) * 0.01f;
    fx_set_input(&ip);
    bp.echo = P(P_ECHO) * 0.01f;
    bp.echo_frames = etime_beats[(int)P(P_ETIME)] * (float)(song.fpb ? song.fpb : 19200);
    bp.echo_fb = P(P_EFB) * 0.01f;
    bp.echo_tone = P(P_ETONE) * 0.01f;
    bp.echo_duck = P(P_EDUCK) * 0.01f;
    bp.reverb = P(P_REV) * 0.01f;
    bp.reverb_s = P(P_RTIME);
    bp.reverb_tone = P(P_RTONE) * 0.01f;
    bp.shimmer = P(P_SHIM) * 0.01f;
    bp.throw_ = sui.throw_;
    fx_set_bus(&bp);

    lc.sync = P(P_LSYNC) < 1.0f;
    lc.from_sound = P(P_LSTART) < 1.0f;
    lc.level = P(P_LLVL) * 0.01f;
    lc.keep = P(P_LKEEP) * 0.01f;
    lc.speed = sui.speed;
    lc.keep_pitch = (int)P(P_LSPMODE) == 0;
    lc.stutter = sui.stutter;
    loop_set(&lc);

    t_dry = P(P_DRY) * 0.01f; t_lead = P(P_LEAD) * 0.01f; t_voc = P(P_VOC) * 0.01f; t_mon = P(P_MONITOR) * 0.01f;
    sui.dry = t_dry; sui.lead = t_lead; sui.voc = t_voc;
    sui.harm = vp.harm_mode != HARM_OFF ? P(P_HLVL) * 0.01f : 0.0f; sui.choir = vp.choir;
}

// ---------------------------------------------------------------- audio
static void init(void)
{
    voice_init();
    vocoder_init();
    fx_init();
    loop_init();
    smooth_init(&g_dry, 1.0f, 0.02f); smooth_init(&g_voc, 0.0f, 0.02f); smooth_init(&g_mon, 1.0f, 0.02f);
}

static void audio(const float *in, float *out, int n)
{
    static float x[AUDIO_BLOCK], lead[AUDIO_BLOCK], extra[AUDIO_BLOCK], voc[AUDIO_BLOCK], voice[AUDIO_BLOCK], loop[AUDIO_BLOCK];
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    memcpy(x, in, (size_t)n * sizeof(float));
    uint32_t c0 = CYCLES();
    fx_input(x, n, out_env_v);
    uint32_t c1 = CYCLES();
    voice_process(x, lead, extra, n);
    uint32_t c2 = CYCLES();
    const float td = t_dry, tv = t_voc, tm = t_mon;
    if (tv > 0.0f || g_voc.y > 1e-4f) vocoder_process(x, voc, n);   // a vocoder at 0 costs nothing
    else memset(voc, 0, (size_t)n * sizeof(float));
    uint32_t c3 = CYCLES();
    for (int i = 0; i < n; i++)
        voice[i] = x[i] * smooth_to(&g_dry, td) + lead[i] + extra[i] + voc[i] * smooth_to(&g_voc, tv);
    loop_run(voice, loop, n);
    for (int i = 0; i < n; i++) out[i] = voice[i] * smooth_to(&g_mon, tm) + loop[i];
    loop_stutter(out, n);
    uint32_t c4 = CYCLES();
    memcpy(voice, out, (size_t)n * sizeof(float));
    fx_bus(voice, out, n);
    uint32_t c5 = CYCLES();
    prof_cyc[0] += c1 - c0; prof_cyc[1] += c2 - c1; prof_cyc[2] += c3 - c2; prof_cyc[3] += c4 - c3; prof_cyc[4] += c5 - c4;
    prof_blocks++;
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[i]); if (a > pk) pk = a; }
    out_env_v = pk > out_env_v ? pk : out_env_v * 0.95f;
}

// ---------------------------------------------------------------- the UI side
static bool rec_was, rec_confirmed;
static int  rec_ms, beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;
static int  note_out = -1;

static void controls(const ctl_t *c)
{
    // the tilt (lean_y: the bottom edge down > 0; the Samplooler's nose down plays faster)
    float u = c->has_imu ? clampf(c->lean_y / 45.0f, -1.0f, 1.0f) : 0.0f;
    float dz = 5.0f / 45.0f, mag = fabsf(u);
    float t = mag < dz ? 0.0f : (mag - dz) / (1.0f - dz), t_lin = t;
    t = t * t * (1.6f - 0.6f * t);
    float sgn = u < 0 ? -1.0f : 1.0f;
    int tilt = (int)P(P_LTILT);
    sui.speed = 1.0f;
    float bend = 0, fmt = 0;
    if (tilt == 0) sui.speed = sgn > 0 ? 1.0f + t : 1.0f - 2.0f * t;   // forward up to 2x; back: slower, stopped, then backwards
    if (tilt == 0 && (int)P(P_LSPMODE) == 1) {                      // OCTAVES: 2x, 1x, half, half backwards, backwards
        // (from 21 degrees forward; back from 15, 27 and 37 degrees)
        static const float edge[4] = { -0.8f, -0.55f, -0.25f, 0.4f }, step[5] = { -1.0f, -0.5f, 0.5f, 1.0f, 2.0f };
        static int zone = 3;
        float v = sgn * t_lin;
        int lo = 0, hi = 0;                                         // the zones 0.03 either side: between them, stay
        while (lo < 4 && v - 0.03f >= edge[lo]) lo++;
        while (hi < 4 && v + 0.03f >= edge[hi]) hi++;
        if (zone < lo || zone > hi) zone = lo == hi ? lo : (zone < lo ? lo : hi);
        sui.speed = step[zone];
    }
    else if (tilt == 1) bend = 12.0f * t * sgn;
    else if (tilt == 2) fmt = 9.0f * t * sgn;
    if (tilt == 0 && t > 0.3f && loop_state() != LOOPER_EMPTY) coach_done(LEARN_INST + 1);
    // the roll: left = into the echo, right = the stutter
    float r = c->has_imu ? c->lean_x : 0.0f;
    float a = fabsf(r) < 15.0f ? 0.0f : clampf((fabsf(r) - 15.0f) / 30.0f, 0.0f, 1.0f);
    bool roll = P(P_LROLL) < 1.0f;
    sui.throw_ = roll && r < 0 ? a : 0.0f;
    sui.stutter = roll && r > 0 ? a : 0.0f;
    if (a > 0.5f && roll) coach_done(LEARN_INST + 2);
    // the stick: X the throat, Y the pitch
    if (c->has_joy) { fmt += 7.0f * c->jx; bend += 12.0f * c->jy; }
    bend_s += 0.25f * (bend - bend_s);
    fmt_s += 0.25f * (fmt - fmt_s);
    sui.bend = bend_s; sui.formant = fmt_s;
}

static void rec_logic(const ctl_t *c)
{
    bool rec = c->rec;
    sui.rec = rec;
    if (rec && !rec_was) { loop_press(); rec_confirmed = false; rec_ms = 0; }
    if (rec) rec_ms += 10;
    if (rec && !rec_confirmed && rec_ms >= 300) { loop_confirm(); rec_confirmed = true; coach_done(LEARN_REC); }
    if (!rec && rec_was) loop_release(rec_confirmed);
    rec_was = rec;
}

static void tick(const ctl_t *c)
{
    if ((c->ticks % 1000 == 0 || (c->ticks < 3000 && c->ticks % 100 == 0)) && prof_blocks) {   // the cost of each part, us per block:
                                                             // every second for the first 30 s, then every 10 s
        float us[5]; sing_prof(us, 5);
        app_log("cpu us/block: input %.0f voices %.0f vocoder %.0f loop+mix %.0f fx %.0f (of 2000)",
                (double)us[0], (double)us[1], (double)us[2], (double)us[3], (double)us[4]);
    }
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    set_song(c->frames);
    controls(c);
    rec_logic(c);
    engines();
    loop_tick();
    // the beat, for the header and the LED
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    uint32_t b = (uint32_t)imod((int32_t)(c->frames - song.origin), (int32_t)(fpb * 12288u)) / fpb;
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    sui.beat_flash = beat_flash;
    // MIDI out: the tuned voice's note
    voice_state_t vs; voice_state(&vs);
    bool sung = vs.voiced && vs.level > 0.04f && fx_gate_open() > 0.5f;   // a voice, not the room
    int nn = sung ? (int)(vs.out_note + 0.5f) : -1;
    if (nn != note_out) {
        if (note_out >= 0) midi_note(note_out, 0);
        if (nn >= 0) midi_note(nn, 100);
        note_out = nn;
    }
    if (sung) { coach_done(LEARN_INST + 0); ui_awake(); }
    // the LED: the voice in cyan, red recording, green on the beat while the loop plays
    int st = loop_state();
    float lv = clampf(vs.level * 3.0f, 0.0f, 1.0f);
    if (st == LOOPER_ARM) led_rgb((uint8_t)(beat_flash ? 120 : 40), 0, 0);   // armed: waiting for you to sing
    else if (st == LOOPER_REC || st == LOOPER_DUB) led_rgb((uint8_t)(255.0f * (beat_flash ? 1.0f : 0.4f)), st == LOOPER_DUB ? 60 : 0, 0);
    else if (st == LOOPER_PLAY) led_rgb(0, (uint8_t)(40 + 200 * (beat_flash ? 1 : 0)), (uint8_t)(200.0f * lv));
    else led_rgb(0, (uint8_t)(60.0f * lv), (uint8_t)(30.0f + 220.0f * lv));
}

static void changed(int p)
{
    if (!link_up || applying) return;
    if (p < 0) { tempo_set_here(); harm_set_here(); link_enable(P(P_LINK) >= 1.0f, (int)P(P_GROUP), (int)P(P_RANGE)); return; }
    switch (p) {
    case P_TEMPO: tempo_set_here(); break;
    case P_KEY: case P_SCALE: case P_PROG: case P_HOLD: case P_BARS: harm_set_here(); break;
    case P_LINK: case P_GROUP: case P_RANGE:
        link_enable(false, 1, 1);
        link_enable(P(P_LINK) >= 1.0f, (int)P(P_GROUP), (int)P(P_RANGE));
        peers_was = 0;
        break;
    }
}

static void action(int p)
{
    if (p == P_LCLEAR) { loop_clear(); toast("LOOP CLEARED", 1000); return; }
    if (p == P_PEERS) {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[48];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)P(P_GROUP));
        else {
            int k = snprintf(b, sizeof b, "%s", pr[0].name);
            for (int i = 1; i < np && k < (int)sizeof b - 10; i++) k += snprintf(b + k, sizeof b - (size_t)k, " + %s", pr[i].name);
        }
        toast(b, 2500);
    }
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) midi_held[d1 >> 4] |= (uint16_t)(1u << (d1 & 15));
    else if (k == 0x80 || (k == 0x90 && d2 == 0)) midi_held[d1 >> 4] &= (uint16_t)~(1u << (d1 & 15));
}

// ---------------------------------------------------------------- sounds and hints
// tags: vtun vspd vvib vshf vfmt vlvl vdry | hmod (0 OFF 1 IN KEY 2 CHORD 3 FIXED) hiv1..3 (0 OFF 1 3RD UP 2 4TH UP 3 5TH UP
// 4 6TH UP 5 OCT UP 6 3RD DOWN 7 4TH DOWN 8 5TH DOWN 9 6TH DOWN 10 OCT DOWN) hlvl | cmix ctyp (0 UNISON 1 OCTAVES 2 CHORD)
// cspr ctim chum | olvl ocar (0 CHORD 1 NOTE 2 CHORD+NOTE 3 NOISE) ooct oban osha orel osib obri odet | fech fetm (0 1/16
// 1 1/8 2 1/8 DOT 3 1/4 4 1/4 DOT 5 1/2 6 1 BAR) fefb feto fedk frev frtm frto fshm | stto (0 SCALE 1 CHORD 2 ALL NOTES
// 3 PENTATONIC 4 BLUES) | igat icmp ides
static const preset_t presets[] = {
    { "CLEAN",          "" },
    { "NATURAL TUNE",   "vtun=60 vspd=140 vvib=80 vlvl=100 vdry=0 frev=25" },
    { "HARD TUNE",      "vtun=100 vspd=0 vvib=0 vlvl=100 vdry=0 fech=20 fetm=2 frev=20" },
    { "CHOIR",          "vtun=50 vspd=90 vlvl=80 vdry=30 cmix=80 ctyp=2 cspr=18 ctim=26 chum=50 frev=55 frtm=3.5" },
    { "BARBERSHOP",     "vtun=60 vspd=60 vlvl=100 vdry=0 hmod=2 hiv1=1 hiv2=3 hiv3=10 hlvl=70 frev=30" },
    { "THIRDS",         "vtun=40 vspd=100 vlvl=100 vdry=0 hmod=1 hiv1=1 hlvl=80 frev=25 fech=20" },
    { "OCTAVE DOWN",    "vlvl=100 vdry=0 hmod=3 hiv1=10 hlvl=90 icmp=60" },
    { "ROBOT",          "vtun=100 vspd=0 vvib=0 vlvl=40 vdry=0 olvl=90 ocar=1 obri=75 odet=4 osib=40 stto=1 frev=15" },
    { "VOCODER",        "vlvl=0 vdry=0 olvl=100 ocar=0 oban=16 osha=55 osib=55 obri=55 odet=10 frev=30 fech=15" },
    { "TALKBOX",        "vtun=80 vspd=20 vlvl=20 vdry=0 olvl=100 ocar=2 oban=20 osha=70 obri=85 odet=6 icmp=70" },
    { "WHISPER",        "vlvl=0 vdry=0 olvl=100 ocar=3 oban=18 osib=80 frev=50 frtm=2.5" },
    { "CHIPMUNK",       "vlvl=100 vdry=0 vshf=7 vfmt=7" },
    { "MONSTER",        "vlvl=100 vdry=0 vshf=-12 vfmt=-6 frev=40 frto=30" },
    { "BIG THROAT",     "vlvl=100 vdry=0 vfmt=-5 frev=20" },
    { "SPACE ECHO",     "fech=70 fetm=4 fefb=65 feto=25 fedk=50 frev=30" },
    { "CATHEDRAL",      "vtun=40 vspd=150 vlvl=60 vdry=60 cmix=40 ctyp=1 frev=80 frtm=8 frto=40" },
    { "SHIMMER",        "frev=70 frtm=6 fshm=70 fech=30 fetm=3" },
    { "DUB VOICE",      "fech=85 fetm=2 fefb=80 feto=15 fedk=30 frev=40 icmp=60" },
};
// LEARN_INST + 0: sang; + 1: tilted a loop; + 2: rolled
static const hint_t hints[] = {
    { "SING INTO THE MIC", LEARN_INST + 0 },
    { "TURN THE KNOB FOR ANOTHER VOICE", LEARN_TURN },
    { "HOLD REC TO LOOP WHAT YOU SING", LEARN_REC },
    { "TILT: THE LOOP FASTER OR BACKWARDS", LEARN_INST + 1 },
    { "ROLL LEFT FOR ECHO, RIGHT TO STUTTER", LEARN_INST + 2 },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

static void draw(const view_t *v) { scene_draw(v); }
static bool splash(int f) { return scene_splash(f); }
static void saver(uint32_t f) { scene_saver(f); }

const instrument_t INST = {
    .name = "SING", .tagline = "TUNE IT, STACK IT, LOOP IT",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = (int)(sizeof presets / sizeof presets[0]),
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false, .knob = NULL, .midi_notes = true,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver,
};
