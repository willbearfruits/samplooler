// SCORE (inst_score.h): what MANTRA's voices sing, placed on the song's beat; the knob's blend from the sound's chant
// to Daisy Bell; the bowl's settings and its strikes on the beat (the bowl itself: inst_bowl.c).
//
// Everything is placed in score units (PPB per song beat). The song's beat (song_beats_at's) is followed every tick,
// unwrapped, so the patterns move with the tempo and with the link. A pattern is a loop of items (a syllable, a vowel
// change, a breath) from an anchor; each item, about 150 ms before its first phoneme starts, is turned into phonemes
// with their audio frames and pushed. A jump of the beat (the link joining a session) flushes what was queued and
// finds the place again.
//
// The blend (mn_blend, the knob) has four zones: the chant alone; its voices hum Daisy's melody over its drone (the
// throat modes: the whistle plays it); its voices sing Daisy's words over the drone; Daisy alone with the waltz. Daisy
// runs from one anchor all along, so the knob never restarts it; a new zone takes over at Daisy's next bar line where
// no note is held across (no syllable is cut), the voices with no part in it stop there, and the drones and the
// voices' controls cross over that bar. The chant starts its phrase again on the bar line it comes back on.
#include "mantra.h"
#include "params.h"
#include <string.h>
#ifndef ESP_PLATFORM
#include <time.h>
#endif

#define PPB    1440                  // units per song beat: halves, thirds, quarters, sixths and the turn's triplets
#define LOOK_F 4800                  // an item is pushed when its first phoneme is this close (150 ms)
#define LATE_F 640                   // an item whose beat passed this long ago is skipped (the beat jumped ahead)

// ---------------------------------------------------------------- params
enum {
    SC_MODE, SC_OCT, SC_PHRASE, SC_MONKS, SC_DRONE, SC_STYLE, SC_MELODY, SC_BOWLS,
    SC_DSPEED, SC_WALTZ, SC_SNAP, SC_BNOTE, SC_BRING, SC_MALLET,
    SC_N
};
static const char *const pages[] = { "CHANT", "CHANT 2" };
static const char *const mode_l[MODE_N] = { "OM", "OM MANI", "THROAT", "KARGYRAA", "DAISY", "VOWELS", "HUM ALONG" };
static const char *const style_l[] = { "KHOOMEI", "SYGYT" };
static const char *const melody_l[] = { "STEPPE", "RIVER", "HORSE", "HIGH", "HANDS ONLY" };
static const char *const bowls_l[] = { "KNOCKS", "PHRASES", "BARS" };
static const char *const dspeed_l[] = { "1/4 BEAT", "1/3 BEAT", "1/2 BEAT", "2/3 BEAT", "1 BEAT" };
static const char *const snap_l[] = { "OFF", "SCALE" };
static const param_t params[SC_N] = {
    [SC_MODE]    = { "MODE", "cmod", 0, PK_ENUM, 0, 0, MODE_N - 1, 1, 0, "", mode_l, "WHAT IT SINGS" },
    [SC_OCT]     = { "OCTAVE", "coct", 0, PK_NUM, PF_INT | PF_SIGNED, -2, 1, 1, 0, "", NULL, "THE SINGERS UP OR DOWN" },
    [SC_PHRASE]  = { "PHRASE", "cphr", 0, PK_NUM, PF_INT, 1, 8, 1, 2, "bars", NULL, "BARS BETWEEN BREATHS" },
    [SC_MONKS]   = { "MONKS", "cmnk", 0, PK_NUM, PF_INT, 1, 5, 1, 1, "", NULL, "HOW MANY SING OM" },
    [SC_DRONE]   = { "DRONE", "cdrn", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "THE LOW VOICE UNDER IT" },
    [SC_STYLE]   = { "THROAT", "csty", 0, PK_ENUM, 0, 0, 1, 1, 0, "", style_l, "WHERE THE WHISTLE SINGS" },
    [SC_MELODY]  = { "MELODY", "cmel", 0, PK_ENUM, 0, 0, 4, 1, 0, "", melody_l, "THE WHISTLE'S TUNE" },
    [SC_BOWLS]   = { "BOWL STRIKES", "cbst", 0, PK_ENUM, 0, 0, 2, 1, 0, "", bowls_l, "WHEN THE BOWL IS STRUCK" },
    [SC_DSPEED]  = { "DAISY SPEED", "cdsp", 1, PK_ENUM, 0, 0, 4, 1, 2, "", dspeed_l, "ONE NOTE OF THE TUNE IS" },
    [SC_WALTZ]   = { "WALTZ", "cwlz", 1, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "THE HUMMED OOM-PAH-PAH" },
    [SC_SNAP]    = { "HUM SNAP", "csnp", 1, PK_ENUM, 0, 0, 1, 1, 1, "", snap_l, "ON THE SONG'S NOTES" },
    [SC_BNOTE]   = { "BOWL NOTE", "cbnt", 1, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "THE BOWL AGAINST THE KEY" },
    [SC_BRING]   = { "BOWL RING", "cbrg", 1, PK_NUM, PF_LOG, 3, 60, 1.12f, 30, "s", NULL, "HOW LONG THE BOWL RINGS" },
    [SC_MALLET]  = { "MALLET", "cmlt", 1, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "SOFT FELT .. HARD WOOD" },
};
// the voices' colour (UNDERTONE, WHISTLE, CHOIR, 8-BIT ...) is VOICE's, in its part of each sound
static const char *const sounds[MN_NSOUNDS] = {
    "cmod=0 cmnk=1 cphr=2 cbst=1",                                 // OM: one deep voice, a bowl each OM
    "cmod=0 cmnk=5 cphr=2 cbst=1",                                 // MONKS
    "cmod=1 cphr=4 cdrn=55 cbst=1",                                // OM MANI
    "cmod=2 csty=0 cmel=0",                                        // KHOOMEI
    "cmod=2 csty=1 cmel=1",                                        // SYGYT
    "cmod=3 cphr=3 cbst=1",                                        // KARGYRAA
    "cmod=4 cdsp=2 cwlz=70",                                       // DAISY
    "cmod=4 cdsp=2 cwlz=60",                                       // DAISY 8-BIT
    "cmod=5",                                                      // VOWELS
    "cmod=6 csnp=1",                                               // HUM ALONG
    "cmod=0 cmnk=5 cphr=4 cbst=1",                                 // SUNN: five monks' OM, long phrases, over the amps
    "cmod=4 cdsp=4 cwlz=0",                                        // DOOM DAISY: a quarter a beat, the amps are the waltz
    "cmod=0 cmnk=3 cphr=3",                                        // ORGAN: three monks over the organ's chord
};
#define P(i) MP(M_SCORE, (i))
int sc_mode(void) { return clampi((int)P(SC_MODE), 0, MODE_N - 1); }

// ---------------------------------------------------------------- what is sung
static uint8_t cons_ms(int ph)       // a consonant's length when there is room for it: VOICE's own (a stop's closure,
{                                    // burst and aspiration together), a little tightened for singing
    return ph == PH_SIL ? 0 : (uint8_t)clampi((int)(mv_ph_len(ph) * 900.0f + 0.5f), 20, 200);
}

// Daisy Bell's chorus (Harry Dacre, 1892), as MANTRA_NOTES.md has it: G major, 3/4. Each syllable: its onset
// consonants (sung before the beat), its vowel (on the beat), its coda (at the note's end).
typedef struct { const char *txt; uint8_t on[2], nuc, co[2]; } syl_t;
#define X_ PH_SIL
static const syl_t dsyl[] = {
    { "DAI", { PH_D, X_ }, PH_EY, { X_, X_ } },     { "SY", { PH_Z, X_ }, PH_IY, { X_, X_ } },
    { "DAI", { PH_D, X_ }, PH_EY, { X_, X_ } },     { "SY", { PH_Z, X_ }, PH_IY, { X_, X_ } },
    { "GIVE", { PH_G, X_ }, PH_IH, { PH_V, X_ } },  { "ME", { PH_M, X_ }, PH_IY, { X_, X_ } },
    { "YOUR", { PH_Y, X_ }, PH_AO, { PH_R, X_ } },  { "AN", { X_, X_ }, PH_AE, { PH_N, X_ } },
    { "SWER", { PH_S, X_ }, PH_ER, { X_, X_ } },    { "DO", { PH_D, X_ }, PH_UW, { X_, X_ } },
    { "I'M", { X_, X_ }, PH_AY, { PH_M, X_ } },     { "HALF", { PH_HH, X_ }, PH_AE, { PH_F, X_ } },
    { "CRA", { PH_K, PH_R }, PH_EY, { X_, X_ } },   { "ZY", { PH_Z, X_ }, PH_IY, { X_, X_ } },
    { "ALL", { X_, X_ }, PH_AO, { PH_L, X_ } },     { "FOR", { PH_F, X_ }, PH_AO, { PH_R, X_ } },
    { "THE", { PH_DH, X_ }, PH_AH, { X_, X_ } },    { "LOVE", { PH_L, X_ }, PH_AH, { PH_V, X_ } },
    { "OF", { X_, X_ }, PH_AH, { PH_V, X_ } },      { "YOU", { PH_Y, X_ }, PH_UW, { X_, X_ } },
    { "IT", { X_, X_ }, PH_IH, { PH_T, X_ } },      { "WON'T", { PH_W, X_ }, PH_OW, { PH_N, PH_T } },
    { "BE", { PH_B, X_ }, PH_IY, { X_, X_ } },      { "A", { X_, X_ }, PH_AH, { X_, X_ } },
    { "STY", { PH_S, PH_T }, PH_AY, { X_, X_ } },   { "LISH", { PH_L, X_ }, PH_IH, { PH_SH, X_ } },
    { "MAR", { PH_M, X_ }, PH_AE, { X_, X_ } },     { "RIAGE", { PH_R, X_ }, PH_IH, { PH_JH, X_ } },
    { "I", { X_, X_ }, PH_AY, { X_, X_ } },         { "CAN'T", { PH_K, X_ }, PH_AE, { PH_N, PH_T } },
    { "AF", { X_, X_ }, PH_AH, { X_, X_ } },        { "FORD", { PH_F, X_ }, PH_AO, { PH_R, PH_D } },
    { "A", { X_, X_ }, PH_AH, { X_, X_ } },         { "CAR", { PH_K, X_ }, PH_AE, { X_, X_ } },
    { "RIAGE", { PH_R, X_ }, PH_IH, { PH_JH, X_ } },
    { "BUT", { PH_B, X_ }, PH_AH, { PH_T, X_ } },   { "YOU'LL", { PH_Y, X_ }, PH_UW, { PH_L, X_ } },
    { "LOOK", { PH_L, X_ }, PH_UH, { PH_K, X_ } },  { "SWEET", { PH_S, PH_W }, PH_IY, { PH_T, X_ } },
    { "UP", { X_, X_ }, PH_AH, { X_, X_ } },        { "ON", { PH_P, X_ }, PH_AA, { PH_N, X_ } },
    { "THE", { PH_DH, X_ }, PH_AH, { X_, X_ } },    { "SEAT", { PH_S, X_ }, PH_IY, { PH_T, X_ } },
    { "OF", { X_, X_ }, PH_AH, { PH_V, X_ } },      { "A", { X_, X_ }, PH_AH, { X_, X_ } },
    { "BI", { PH_B, X_ }, PH_AY, { X_, X_ } },      { "CY", { PH_S, X_ }, PH_IH, { X_, X_ } },
    { "CLE", { PH_K, X_ }, PH_AH, { PH_L, X_ } },   { "BUILT", { PH_B, X_ }, PH_IH, { PH_L, PH_T } },
    { "FOR", { PH_F, X_ }, PH_AO, { PH_R, X_ } },   { "TWO", { PH_T, X_ }, PH_UW, { X_, X_ } },
};
#define NSYL ((int)(sizeof dsyl / sizeof dsyl[0]))
// the notes: start and length in sixths of a quarter, MIDI (as written, in G), the syllable (-1: the turn's next note)
typedef struct { int16_t p6, l6; int8_t note, syl; } dnote_t;
static const dnote_t dnotes[] = {
    { 0, 18, 74, 0 }, { 18, 18, 71, 1 }, { 36, 18, 67, 2 }, { 54, 18, 62, 3 },
    { 72, 6, 64, 4 }, { 78, 6, 66, 5 }, { 84, 6, 67, 6 }, { 90, 12, 64, 7 }, { 102, 6, 67, 8 }, { 108, 30, 62, 9 },
    { 144, 18, 69, 10 }, { 162, 18, 74, 11 }, { 180, 18, 71, 12 }, { 198, 18, 67, 13 },
    { 216, 6, 64, 14 }, { 222, 6, 66, 15 }, { 228, 6, 67, 16 }, { 234, 12, 69, 17 }, { 246, 6, 71, 18 }, { 252, 30, 69, 19 },
    { 282, 6, 71, 20 }, { 288, 6, 72, 21 }, { 294, 6, 71, 22 }, { 300, 6, 69, 23 }, { 306, 12, 74, 24 }, { 318, 6, 71, 25 },
    { 324, 2, 69, 26 }, { 326, 2, 71, -1 }, { 328, 2, 69, -1 },                // "mar": A B A, a triplet turn
    { 330, 24, 67, 27 },
    { 354, 6, 69, 28 }, { 360, 12, 71, 29 }, { 372, 6, 67, 30 }, { 378, 12, 64, 31 }, { 390, 6, 67, 32 }, { 396, 6, 64, 33 },
    { 402, 24, 62, 34 },
    { 426, 6, 62, 35 }, { 432, 12, 67, 36 }, { 444, 6, 71, 37 }, { 450, 18, 69, 38 },
    { 468, 12, 67, 39 }, { 480, 3, 71, 40 }, { 483, 3, 71, 41 }, { 486, 12, 69, 42 }, { 498, 3, 71, 43 }, { 501, 3, 72, 44 },
    { 504, 6, 74, 45 }, { 510, 6, 71, 46 }, { 516, 6, 67, 47 }, { 522, 12, 69, 48 }, { 534, 6, 62, 49 }, { 540, 36, 67, 50 },
};
#define NDN ((int)(sizeof dnotes / sizeof dnotes[0]))
#define DAISY_Q 96                   // quarters in the chorus
#define DAISY_LOOP_Q 99              // and a bar's rest (the waltz goes on through it)
// Daisy's harmony, a chord per bar (scale degrees of G: I, IV, V), the rest bar last
static const int8_t dchord[DAISY_LOOP_Q / 3] = { 0, 0, 3, 0, 3, 3, 4, 4, 4, 4, 0, 0, 3, 4, 4, 4,
                                                 3, 0, 4, 0, 0, 3, 0, 4, 0, 4, 0, 4, 0, 4, 0, 0, 0 };
static const char *const dlines[] = { "DAISY, DAISY,", "GIVE ME YOUR ANSWER, DO!", "I'M HALF CRAZY,", "ALL FOR THE LOVE OF YOU!",
    "IT WON'T BE A", "STYLISH MARRIAGE,", "I CAN'T AFFORD", "A CARRIAGE,", "BUT YOU'LL LOOK SWEET", "UPON THE SEAT",
    "OF A BICYCLE", "BUILT FOR TWO." };
static const uint8_t dline_first[] = { 0, 4, 10, 14, 20, 24, 28, 32, 35, 39, 43, 48, NSYL };
#define NLINES ((int)(sizeof dlines / sizeof dlines[0]))
static int8_t syl_line[NSYL], syl_at[NSYL], syl_len[NSYL];
static const int16_t dspeed_u[] = { PPB / 4, PPB / 3, PPB / 2, 2 * PPB / 3, PPB };   // units per quarter

// the lead's items: a syllable (onsets and vowel), the turn's next notes, a coda
enum { DI_SYL, DI_TURN, DI_CODA };
typedef struct { int16_t p6, s6; uint8_t kind, n, syl; } ditem_t;   // s6: its syllable's start; n: index into dnotes
static ditem_t ditems[2 * NDN];
static int nditems;

// OM MANI PADME HUM
static const char *const mani_line = "OM MANI PADME HUM";
static const int8_t mani_at[6] = { 0, 3, 5, 8, 11, 14 }, mani_len[6] = { 2, 2, 2, 3, 2, 3 };
// the kargyraa's vowels, a cycle per phrase
static const uint8_t karg_v[3][4] = { { PH_UW, PH_OW, PH_AA, PH_OW }, { PH_OW, PH_AA, PH_OW, PH_UW }, { PH_UW, PH_AA, PH_OW, PH_UW } };
static const char *const karg_line = "OO OH AH";
static const char *const vowel_line = "OO OH AH EH EE";

// the throat melodies: a degree of the style's harmonics and a length in half beats; 16 beats each
typedef struct { int8_t deg, half; } mstep_t;
static const mstep_t mel0[] = { { 1, 4 }, { 2, 2 }, { 3, 2 }, { 4, 4 }, { 3, 4 }, { 2, 2 }, { 1, 2 }, { 0, 4 }, { 1, 8 } };
static const mstep_t mel1[] = { { 2, 2 }, { 3, 2 }, { 4, 4 }, { 3, 2 }, { 2, 2 }, { 1, 4 }, { 2, 2 }, { 3, 2 }, { 2, 2 }, { 1, 2 }, { 0, 8 } };
static const mstep_t mel2[] = { { 1, 1 }, { 2, 1 }, { 3, 2 }, { 1, 1 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 3, 2 }, { 2, 2 }, { 1, 2 },
                                { 2, 1 }, { 3, 1 }, { 2, 2 }, { 1, 4 }, { 0, 4 }, { 1, 4 } };
static const mstep_t mel3[] = { { 3, 4 }, { 4, 4 }, { 5, 4 }, { 4, 2 }, { 3, 2 }, { 4, 4 }, { 3, 2 }, { 2, 2 }, { 3, 8 } };
static const mstep_t mel4[] = { { 1, 32 } };
static const mstep_t *const mels[5] = { mel0, mel1, mel2, mel3, mel4 };
static const uint8_t mel_n[5] = { 9, 11, 16, 9, 1 };
static const uint8_t harm[2][6] = { { 6, 8, 9, 10, 12, 16 }, { 8, 9, 10, 12, 13, 16 } };   // khoomei, sygyt
#define MEL_U (16 * PPB)
#define BREATH_U (PPB / 2)            // the throat singer breathes in the melody's last half beat
static const uint8_t hum_ph[3] = { PH_M, PH_OW, PH_AA };   // Daisy hummed: two lines on each

// ---------------------------------------------------------------- the song's clock, unwrapped
static struct {
    bool     ok, jumped;
    uint32_t frames, fpb;
    int32_t  raw, beat, off, u;       // song beat mod the wrap, the unwrapped beat, frames into it, units now
} clk;
static int64_t fdiv(int64_t a, int64_t b) { int64_t q = a / b; return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q; }
static void clock_tick(uint32_t frames)
{
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t span = (int32_t)(fpb * SONG_WRAP_BEATS);
    int32_t m = mn_mod((int32_t)(frames - song.origin), span);
    int32_t raw = m / (int32_t)fpb, off = m % (int32_t)fpb;
    int32_t was_u = clk.u;
    uint32_t was_f = clk.frames;
    if (!clk.ok) { clk.beat = raw; clk.ok = true; was_f = frames; }
    else {
        int32_t d = raw - clk.raw;
        if (d > SONG_WRAP_BEATS / 2) d -= SONG_WRAP_BEATS;
        if (d < -SONG_WRAP_BEATS / 2) d += SONG_WRAP_BEATS;
        clk.beat += d;
    }
    clk.raw = raw; clk.off = off; clk.frames = frames; clk.fpb = fpb;
    clk.u = clk.beat * PPB + (int32_t)((int64_t)off * PPB / fpb);
    // where it should be now from where it was: more than 20 ms off is a jump (the link moved the beat)
    int32_t want = was_u + (int32_t)((int64_t)(int32_t)(frames - was_f) * PPB / fpb);
    int32_t tol = (int32_t)((int64_t)640 * PPB / fpb);
    clk.jumped = was_f != frames && (clk.u - want > tol || want - clk.u > tol);
}
static uint32_t frame_of(int32_t u)          // the audio frame where unit u falls (near now)
{
    int64_t du = (int64_t)u - (int64_t)clk.beat * PPB;
    return clk.frames - (uint32_t)clk.off + (uint32_t)(int32_t)fdiv(du * clk.fpb + PPB / 2, PPB);
}
static int32_t units_of_f(int32_t f) { return (int32_t)((int64_t)f * PPB / clk.fpb); }
static float   s_of_units(int32_t u) { return (float)u * (float)clk.fpb / ((float)PPB * (float)AUDIO_SR); }
static int32_t next_beat_u(int32_t min_frames)   // the first whole beat at least that far ahead
{
    int32_t u = clk.u + units_of_f(min_frames);
    return (int32_t)fdiv(u + PPB - 1, PPB) * PPB;
}

// ---------------------------------------------------------------- the score's state
enum { TK_NONE, TK_OM, TK_MANI, TK_KARG, TK_DAISY, TK_WALTZ, TK_BOWL };
typedef struct { uint8_t kind; int32_t A, L, loop; int i, n; } trk_t;
enum { TC, TD, TW, TB, NTRK };       // the chant's, Daisy's singers, the waltz, the bowl's strikes
static trk_t T[NTRK];
enum { Z_CHANT, Z_HUM, Z_CHOIR, Z_DAISY };
static struct { int now, old, next, target; bool pending, committed; int32_t S, xf; } Z;
static int32_t DA, dq = PPB / 2;     // Daisy's anchor (it never moves but for its speed) and its quarter, in units
static bool   daisy_set;
static int   cur_mode = -1;
static float last_phrase, last_dspeed, last_monks, last_bowls;
static uint32_t last_at[MV_N];       // what each voice was last given (pushes stay in order)
static bool  seq_voice[MV_N];
static float acc;                    // the pump's accent, decaying
static bool  accent_next;
static int   karg_rot;
static float yaw_ref;
static bool  reset_yaw = true;
static uint32_t rng = 0x5eed1234u;
static float stag[5], stag_d[5];     // the monks' and the choir's small delays (s)
static float ot_cur, ot_hand0, yaw0; // the whistle
static bool  hand_on;
static float hand_t;
static int   harm_now;
static float hum_note, hum_hold;     // HUM ALONG
static bool  hum_on;
typedef struct { const char *line; int8_t at, len; uint32_t frame, end; } disp_t;   // the screen's words
static disp_t disp[16];
static int    disp_w;
static sc_view_t view;
static int p_hrol = -1, p_hyaw = -1; // the host's hands settings (by tag)
static int p_vund = -1, p_vrou = -1, p_vtrc = -1, p_vshp = -1;   // VOICE's colour (by tag)
static float vpar(int p) { return p >= 0 ? pv[p] * 0.01f : 0.0f; }
static bool bar_clean[DAISY_LOOP_Q / 3];   // Daisy's bar lines with no note held across them

#ifndef ESP_PLATFORM
// tools/sim: what the score pushed, and what it cost
typedef struct { uint32_t at; uint8_t v, ph, flags, kind; float note, level, dur; int16_t tag; } sc_log_t;
sc_log_t sc_log[32768];
int sc_log_n;
double sc_tick_ns;
int sc_tick_n;
mv_ctl_t sc_last_ctl;
int sc_drops;                        // pushes a full queue refused
#endif
// what a pushed phoneme is (the tests read it); tag: Daisy's syllable, the mantra's syllable, the vowel ...
enum { K_ONSET, K_VOWEL, K_TURN, K_CODA, K_BREATH, K_WALTZ, K_HUM, K_STOP, K_DRONE, K_DHUM };

static void push(int v, uint32_t at, int ph, int flags, float dur, float note, float level, int kind, int tag)
{
    if (v < 0 || v >= MV_N) return;
    if (at == 0) at = 1;
    if (seq_voice[v] && (int32_t)(at - last_at[v]) < 1) at = last_at[v] + 1;   // never before what it was given
    last_at[v] = at; seq_voice[v] = true;
    mv_ev_t e = { at, (uint8_t)ph, (uint8_t)flags, dur, note, clampf(level, 0.0f, 1.0f) };
    if (mv_queued(v) >= 30 || !mv_push(v, &e)) {
#ifndef ESP_PLATFORM
        sc_drops++;
#endif
        return;
    }
#ifndef ESP_PLATFORM
    if (sc_log_n < (int)(sizeof sc_log / sizeof sc_log[0]))
        sc_log[sc_log_n++] = (sc_log_t){ at, (uint8_t)v, (uint8_t)ph, (uint8_t)flags, (uint8_t)kind, note, e.level, dur, (int16_t)tag };
#else
    (void)kind; (void)tag;
#endif
}
static disp_t disp_new;               // what the item being built shows, kept once it is pushed
static bool   disp_has;
static void show(const char *line, int at, int len, uint32_t frame, uint32_t end)
{
    disp_new = (disp_t){ line, (int8_t)at, (int8_t)len, frame, end };
    disp_has = true;
}
static void flush_all(void)
{
    for (int v = 0; v < MV_N; v++) { if (seq_voice[v]) mv_flush(v); seq_voice[v] = false; }
}
static float frand(void) { return rng_uni(&rng); }

// ---------------------------------------------------------------- pitches
static int oct(void) { return 12 * (int)P(SC_OCT); }
static int key_pc(void) { return ((song.key % 12) + 12) % 12; }
static int place(int pc, int lo) { return lo + ((pc - lo) % 12 + 12) % 12; }   // pitch class pc in [lo, lo + 11]
static float om_root(void) { return (float)(place(key_pc(), 34) + oct()); }      // A#1..A2: about 58..110 Hz
static float drone_root(void) { return (float)(place(key_pc(), 41) + oct()); }   // F2..E3: about 87..165 Hz
static float mani_tone(void) { return (float)(place(key_pc(), 39) + oct()); }    // D#2..D3: about 78..147 Hz
static float karg_note(void) { return (float)(place(key_pc(), 36) + oct()); }    // C2..B2
static int daisy_shift(void) { return ((key_pc() - 7 + 6) % 12 + 12) % 12 - 6; } // G to the key, -6..+5
static float daisy_note(int written) { return (float)(written - 12 + daisy_shift() + oct()); }   // a man's octave
static int monks(void) { return clampi((int)P(SC_MONKS), 1, 5); }

// ---------------------------------------------------------------- who sings what in each zone
typedef struct { int8_t n, v[5]; } vset_t;
static const int8_t monk_v[5] = { 0, 1, 4, 2, 3 };   // the lead, the octave below, three in unison (2, 3: the waltz's)
static vset_t singers(int mode, int z)    // Daisy's singers (the first is the lead)
{
    vset_t s = { 0, { 0, 0, 0, 0, 0 } };
    if (mode == MODE_DAISY) { static const int8_t dv[4] = { 0, 1, 4, 5 }; s.n = (int8_t)(4 - z); memcpy(s.v, dv, 4); return s; }
    if (z == Z_CHANT) return s;
    bool lead0 = mode == MODE_OM || mode == MODE_MANI;
    if (z == Z_DAISY) { s.n = 1; s.v[0] = lead0 ? 0 : 1; return s; }
    if (mode == MODE_OM) { s.n = (int8_t)monks(); memcpy(s.v, monk_v, 5); return s; }
    if ((mode == MODE_THROAT || mode == MODE_KARGYRAA) && z == Z_HUM) return s;   // the whistle has the tune
    s.n = 1; s.v[0] = lead0 ? 0 : 1;
    return s;
}
static vset_t chanters(int mode)          // the chant's own pushed voices (zone 0)
{
    vset_t s = { 0, { 0, 0, 0, 0, 0 } };
    if (mode == MODE_OM) { s.n = (int8_t)monks(); memcpy(s.v, monk_v, 5); }
    else if (mode == MODE_MANI || mode == MODE_KARGYRAA) { s.n = 1; s.v[0] = 0; }
    return s;
}
static bool in_set(const vset_t *s, int v) { for (int k = 0; k < s->n; k++) if (s->v[k] == v) return true; return false; }
static bool has_waltz(int mode, int z) { return mode == MODE_DAISY || z == Z_DAISY; }
static bool words_in(int mode, int z) { return mode == MODE_DAISY || z >= Z_CHOIR; }
static bool karg_drone(int mode, int z) { return mode == MODE_KARGYRAA && (z == Z_HUM || z == Z_CHOIR); }
static bool whistle_in(int mode, int z) { return (mode == MODE_THROAT && z <= Z_HUM) || (mode == MODE_KARGYRAA && z == Z_HUM); }
static bool seq_used(int mode, int z, int v)   // voice v is pushed phonemes in that zone
{
    vset_t a = singers(mode, z), b = chanters(mode);
    return in_set(&a, v) || (z == Z_CHANT && in_set(&b, v)) || (has_waltz(mode, z) && (v == 2 || v == 3)) || (karg_drone(mode, z) && v == 0);
}
static int zone_at(int32_t u) { return Z.pending && u - Z.S >= 0 ? Z.next : Z.now; }
static int32_t dbar(void) { return 3 * dq; }
// a voice that joins at a zone's start comes in over its first bar
static float join_gain(int v, int32_t u)
{
    int32_t start; int prev;
    if (Z.pending && u - Z.S >= 0) { start = Z.S; prev = Z.now; }
    else if (u - Z.xf >= 0 && u - Z.xf < dbar()) { start = Z.xf; prev = Z.old; }
    else return 1.0f;
    if (seq_used(cur_mode, prev, v)) return 1.0f;
    return 0.35f + 0.65f * clampf((float)(u - start) / (float)dbar(), 0.0f, 1.0f);
}

// ---------------------------------------------------------------- the items of each pattern
typedef struct { uint8_t v, ph, flags, kind; int16_t tag; uint32_t at; float dur, note, level; } sev_t;
#define MAXEV 40
static int ev_n;
static sev_t evs[MAXEV];
static void ev(int v, uint32_t at, int ph, int flags, float dur, float note, float level, int kind, int tag)
{
    if (ev_n < MAXEV) evs[ev_n++] = (sev_t){ (uint8_t)v, (uint8_t)ph, (uint8_t)flags, (uint8_t)kind, (int16_t)tag, at, dur, note, level };
}
static uint32_t ms_f(float ms) { return (uint32_t)(ms * (float)AUDIO_SR * 0.001f); }

static int mani_bars(void) { return clampi((int)P(SC_PHRASE), 1, 8); }
static int32_t bar_u(void) { return (song.bar_beats > 0 ? song.bar_beats : 4) * PPB; }
static const float om_at[5] = { 0.0f, 0.07f, 0.40f, 0.55f, 0.88f };
static const float karg_at[5] = { 0.0f, 0.22f, 0.44f, 0.66f, 0.90f };
#define MANI_K 11
static const float mani_off[MANI_K] = { 0.0f, 0.62f, 1.0f, 1.5f, 2.0f, 2.5f, 2.5f, 3.0f, 3.25f, 3.5f, 3.68f };
static const float mani_off_last[MANI_K] = { 0.0f, 0.62f, 1.0f, 1.5f, 2.0f, 2.5f, 2.5f, 3.0f, 3.2f, 3.4f, 3.68f };
static int32_t item_off(const trk_t *t, int i)   // an item's place in its loop (units)
{
    switch (t->kind) {
    case TK_OM:    return (int32_t)(om_at[i] * (float)t->L);
    case TK_KARG:  return (int32_t)(karg_at[i] * (float)t->L);
    case TK_MANI: { int b = i / MANI_K, k = i % MANI_K; const float *o = b == t->n / MANI_K - 1 ? mani_off_last : mani_off;
                    return b * bar_u() + (int32_t)(o[k] * (float)PPB); }
    case TK_DAISY: return ditems[i].p6 * dq / 6;
    case TK_WALTZ: return i * dq;
    default:       return 0;
    }
}

// consonants: their lengths in frames, squeezed to fit in `room` seconds
static int cons_frames(const uint8_t *ph, int n, float room, uint32_t *f)
{
    float tot = 0;
    for (int i = 0; i < n; i++) tot += (float)cons_ms(ph[i]) * 0.001f;
    float k = tot > room && tot > 0 ? room / tot : 1.0f;
    int sum = 0;
    for (int i = 0; i < n; i++) { f[i] = ph[i] ? ms_f((float)cons_ms(ph[i]) * k) : 0; sum += (int)f[i]; }
    return sum;
}
static int n_of(const uint8_t *ph) { return (ph[0] != PH_SIL) + (ph[1] != PH_SIL); }
static int dn_end6(int j) { return dnotes[j].p6 + dnotes[j].l6; }
static int syl_last_note(int j) { while (j + 1 < NDN && dnotes[j + 1].syl < 0) j++; return j; }   // the turn's last note
static float onset_room(int j)       // the time the onset may take from the note (or the rest) before
{
    float q = s_of_units(dq);
    float before = j > 0 ? (float)(dnotes[j].p6 - dnotes[j - 1].p6) / 6.0f * q : 3.0f * q;
    if (j > 0 && dnotes[j - 1].syl < 0) before = 0.67f * q;
    return fminf(0.35f * before, 0.25f);
}

// Daisy's syllable (or its turn, or its coda) for one singer
static void daisy_voice(const ditem_t *it, uint32_t F, int v, float note_off, float gain, bool words, bool lead)
{
    int j = it->n, s = it->syl, tg = 100 + s;    // Daisy's phonemes are tagged 100 + the syllable
    float q = s_of_units(dq);
    const syl_t *S = &dsyl[s];
    int hph = hum_ph[(syl_line[s] / 2) % 3];
    if (it->kind == DI_TURN) {
        ev(v, F, words ? S->nuc : hph, MVF_HOLD | MVF_GLIDE, (float)dnotes[j].l6 / 6.0f * q, daisy_note(dnotes[j].note) + note_off, 0.82f * gain, K_TURN, tg);
        return;
    }
    int last = syl_last_note(j), end6 = dn_end6(last);
    bool legato = last + 1 < NDN && dnotes[last + 1].p6 == end6;
    float note = daisy_note(dnotes[j].note) + note_off;
    float lvl_acc = lead && accent_next ? 1.15f : 1.0f;
    int   fl_acc = lead && accent_next ? MVF_ACCENT : 0;
    if (it->kind == DI_SYL) {
        float len_s = (float)(end6 - dnotes[j].p6) / 6.0f * q;
        float lv = (dnotes[j].p6 % 18 == 0 ? 0.9f : 0.82f) * lvl_acc * gain;
        if (!words) {                                              // hummed: the line's vowel, legato
            ev(v, F, hph, (legato ? MVF_HOLD : 0) | MVF_GLIDE | fl_acc, legato ? len_s : 0.92f * len_s, note, lv, K_DHUM, tg);
            return;
        }
        uint32_t f[2]; int no = n_of(S->on);
        int sum = cons_frames(S->on, no, onset_room(j), f);
        uint32_t at = F - (uint32_t)sum;
        for (int k = 0; k < no; k++) { ev(v, at, S->on[k], 0, (float)f[k] / AUDIO_SR, note, 0.8f * gain, K_ONSET, tg); at += f[k]; }
        // sung as the 1961 machine sang: steady notes, each syllable apart (the vowel ends just before the next
        // onset), a vowel with nothing before it starts hard. Measured: whisper.cpp heard about 30 of the chorus'
        // 40 words this way at a quarter of 0.36 s, against 20 sung legato with glides.
        bool coda = n_of(S->co) > 0, hold = coda;
        float vdur = coda ? len_s : 0.92f * len_s;
        if (legato && !coda) {
            const syl_t *N = &dsyl[dnotes[last + 1].syl]; uint32_t g2[2];
            int nl = cons_frames(N->on, n_of(N->on), onset_room(last + 1), g2);
            vdur = fmaxf(0.06f, len_s - (float)nl / AUDIO_SR - 0.015f);
        }
        ev(v, F, S->nuc, (hold ? MVF_HOLD : 0) | (no == 0 ? MVF_ACCENT : 0) | fl_acc, vdur, note, lv, K_VOWEL, tg);
    } else if (words) {                                            // the coda: it ends where the next onset begins
        uint32_t f[2], g[2]; int nc = n_of(S->co);
        float room = fminf(0.35f * (float)(end6 - dnotes[j].p6) / 6.0f * q, 0.25f);
        int sum = cons_frames(S->co, nc, room, f), lead_f = 0;
        if (legato) { const syl_t *N = &dsyl[dnotes[last + 1].syl]; lead_f = cons_frames(N->on, n_of(N->on), onset_room(last + 1), g); }
        uint32_t at = F - (uint32_t)lead_f - (uint32_t)sum;
        note = daisy_note(dnotes[last].note) + note_off;
        for (int k = 0; k < nc; k++) {
            bool hold = legato && k == nc - 1 && lead_f == 0;
            ev(v, at, S->co[k], hold ? MVF_HOLD : 0, (float)f[k] / AUDIO_SR, note, 0.75f * gain, K_CODA, tg);
            at += f[k];
        }
    }
}

static void build(const trk_t *t, int i, uint32_t F, int32_t u)
{
    ev_n = 0; disp_has = false;
    float lvl_acc = accent_next ? 1.15f : 1.0f;
    int   fl_acc = accent_next ? MVF_ACCENT : 0;
    switch (t->kind) {
    case TK_OM: {
        if (zone_at(u) != Z_CHANT) break;
        int n = monks();
        float root = om_root(), P_s = s_of_units(t->L);
        float g = n > 1 ? 0.62f : 1.0f;
        if (i == 0) for (int k = 0; k < 5; k++) stag[k] = k ? 0.02f + 0.08f * frand() : 0.0f;   // monks, not a choir
        for (int k = 0; k < n; k++) {
            int v = monk_v[k];
            uint32_t at = F + ms_f(stag[k] * 1000.0f);
            float note = k == 1 ? root - 12.0f : root, lv = (k == 1 ? 0.8f : 0.9f) * g * join_gain(v, u);
            switch (i) {
            case 0: ev(v, at, PH_OW, MVF_HOLD | fl_acc, 0.07f * P_s, note, 0.65f * lv * lvl_acc, K_VOWEL, 0); break;
            case 1: ev(v, at, PH_AO, MVF_HOLD, 0.33f * P_s, note, lv, K_VOWEL, 1); break;
            case 2: ev(v, at, PH_OW, MVF_HOLD, 0.15f * P_s, note, 0.9f * lv, K_VOWEL, 2); break;
            case 3: ev(v, at, PH_M, 0, 0.30f * P_s, note, 0.9f * lv, K_HUM, 3); break;   // the M: about 8 dB under the vowel
            case 4: ev(v, at, PH_HH, 0, fminf(0.09f * P_s, 0.9f), note, 0.12f, K_BREATH, 4); break;
            }
        }
        if (i == 0) show("OM", 0, 2, F, F + (uint32_t)(0.85f * P_s * (float)AUDIO_SR));
        break;
    }
    case TK_KARG: {
        if (zone_at(u) != Z_CHANT) break;
        float note = karg_note(), P_s = s_of_units(t->L), g = join_gain(0, u);
        if (i < 4) {
            int ph = karg_v[(t->loop + karg_rot) % 3][(i + karg_rot) % 4];
            ev(0, F, ph, (i < 3 ? MVF_HOLD : 0) | (i == 0 ? MVF_ACCENT : 0) | fl_acc, (i < 3 ? 0.22f : 0.24f) * P_s, note, 0.9f * lvl_acc * g, K_VOWEL, ph);
            int at = ph == PH_UW ? 0 : ph == PH_OW ? 3 : 6;
            show(karg_line, at, 2, F, F + (uint32_t)(0.22f * P_s * (float)AUDIO_SR));
        } else ev(0, F, PH_HH, 0, fminf(0.08f * P_s, 0.8f), note, 0.12f, K_BREATH, 0);
        break;
    }
    case TK_MANI: {
        if (zone_at(u) != Z_CHANT) break;
        float tone = mani_tone(), beat_s = s_of_units(PPB), g = join_gain(0, u);
        int b = i / MANI_K, k = i % MANI_K;
        bool last = b == t->n / MANI_K - 1;
        uint32_t f[2], hb = (uint32_t)(0.5f * beat_s * AUDIO_SR);
        float room = 0.15f * beat_s;
        switch (k) {
        case 0: ev(0, F, PH_AO, MVF_HOLD | MVF_ACCENT, 0.6f * beat_s, tone, 0.95f * g, K_VOWEL, 0);
                show(mani_line, mani_at[0], mani_len[0], F, F + 2 * hb); break;
        case 1: ev(0, F, PH_M, MVF_HOLD, 0.38f * beat_s, tone, 0.8f * g, K_CODA, 0); break;
        case 2: ev(0, F, PH_AA, MVF_HOLD | fl_acc, 0.5f * beat_s, tone, 0.85f * lvl_acc * g, K_VOWEL, 1);
                show(mani_line, mani_at[1], mani_len[1], F, F + hb); break;
        case 3: { uint8_t on[2] = { PH_N, 0 }; int s = cons_frames(on, 1, room, f);
                ev(0, F - (uint32_t)s, PH_N, MVF_HOLD, (float)s / AUDIO_SR, tone, 0.8f * g, K_ONSET, 2);
                ev(0, F, PH_IY, MVF_HOLD | fl_acc, 0.5f * beat_s, tone, 0.85f * lvl_acc * g, K_VOWEL, 2);
                show(mani_line, mani_at[2], mani_len[2], F, F + hb); break; }
        case 4: { uint8_t on[2] = { PH_P, 0 }; int s = cons_frames(on, 1, room, f);
                ev(0, F - (uint32_t)s, PH_P, 0, (float)s / AUDIO_SR, tone, 0.8f * g, K_ONSET, 3);
                ev(0, F, PH_AA, MVF_HOLD | fl_acc, 0.5f * beat_s, tone, 0.9f * lvl_acc * g, K_VOWEL, 3);
                show(mani_line, mani_at[3], mani_len[3], F, F + hb); break; }
        case 5: { uint8_t co[2] = { PH_D, PH_M }; cons_frames(co, 2, room, f);
                ev(0, F - f[0] - f[1], PH_D, 0, (float)f[0] / AUDIO_SR, tone, 0.8f * g, K_CODA, 3); break; }
        case 6: { uint8_t co[2] = { PH_D, PH_M }; cons_frames(co, 2, room, f);
                ev(0, F - f[1], PH_M, MVF_HOLD, (float)f[1] / AUDIO_SR, tone, 0.8f * g, K_ONSET, 4);
                ev(0, F, PH_EY, MVF_HOLD | fl_acc, 0.5f * beat_s, tone, 0.85f * lvl_acc * g, K_VOWEL, 4);
                show(mani_line, mani_at[4], mani_len[4], F, F + hb); break; }
        case 7: { uint8_t on[2] = { PH_HH, 0 }; int s = cons_frames(on, 1, room, f);
                ev(0, F - (uint32_t)s, PH_HH, 0, (float)s / AUDIO_SR, tone, 0.6f * g, K_ONSET, 5);
                ev(0, F, PH_UW, MVF_HOLD | fl_acc, 0.25f * beat_s, tone, 0.9f * lvl_acc * g, K_VOWEL, 5);
                show(mani_line, mani_at[5], mani_len[5], F, F + (last ? 13 : 20) * hb / 10); break; }
        case 8: ev(0, F, PH_UW, MVF_HOLD | MVF_GLIDE, 0.25f * beat_s, tone - 1.5f, 0.85f * g, K_TURN, 5); break;   // HUM falls
        case 9: ev(0, F, PH_M, last ? MVF_GLIDE : MVF_HOLD | MVF_GLIDE, (last ? 0.25f : 0.5f) * beat_s, tone - 3.0f, 0.75f * g, K_CODA, 5); break;
        case 10: if (last) ev(0, F, PH_HH, 0, 0.28f * beat_s, tone, 0.15f, K_BREATH, 6); break;
        }
        break;
    }
    case TK_DAISY: {
        const ditem_t *it = &ditems[i];
        int32_t us = t->A + t->loop * t->L + it->s6 * dq / 6;     // the syllable's start: its zone
        int z = zone_at(us);
        vset_t S = singers(cur_mode, z);
        if (S.n == 0) break;
        bool words = words_in(cur_mode, z);
        int s = it->syl;
        if (it->kind == DI_SYL && s == dline_first[syl_line[s]]) for (int k = 0; k < 5; k++) stag_d[k] = k ? 0.005f + 0.02f * frand() : 0.0f;
        for (int k = 0; k < S.n; k++) {                            // VOICE's CHOIR spreads the unison
            int v = S.v[k];
            float off = v == 1 && k > 0 ? -12.0f : 0.0f;
            float g = (S.n > 1 ? 0.62f : 1.0f) * (k == 0 ? 1.0f : 0.85f) * join_gain(v, us);
            daisy_voice(it, F + ms_f(stag_d[k] * 1000.0f), v, off, g, words, k == 0);
        }
        if (it->kind == DI_SYL) {
            float len_s = (float)(dn_end6(syl_last_note(it->n)) - dnotes[it->n].p6) / 6.0f * s_of_units(dq);
            if (words) show(syl_line[s] >= 0 ? dlines[syl_line[s]] : NULL, syl_at[s], syl_len[s], F, F + (uint32_t)(len_s * AUDIO_SR));
            else show(NULL, 0, 0, F, F);
        }
        break;
    }
    case TK_WALTZ: {
        float w = P(SC_WALTZ) * 0.01f;
        if (w <= 0.0f || !has_waltz(cur_mode, zone_at(u))) break;
        w *= join_gain(2, u);
        int b = i / 3, beat = i % 3, deg = dchord[b];
        int root = (7 + (deg == 3 ? 5 : deg == 4 ? 7 : 0) + daisy_shift() + 24) % 12;
        float q = s_of_units(dq);
        if (beat == 0) {                                           // oom: the root, low
            float bass = (float)place(root, 36);
            ev(2, F, PH_UW, 0, 0.45f * q, bass, 0.55f * w, K_WALTZ, b);
            ev(2, F + ms_f(450.0f * q), PH_M, 0, 0.38f * q, bass, 0.5f * w, K_WALTZ, b);
        } else {                                                   // pah, pah: the third and the fifth, hummed
            float third = (float)place((root + 4) % 12, 44), fifth = (float)place((root + 7) % 12, 44);
            ev(2, F, PH_M, 0, 0.55f * q, third, 0.38f * w, K_WALTZ, b);
            ev(3, F, PH_M, 0, 0.55f * q, fifth, 0.36f * w, K_WALTZ, b);
        }
        break;
    }
    default: break;
    }
}

// the loop: every item whose first phoneme starts within LOOK_F is built and pushed, in order
static void run_track(trk_t *t)
{
    if (t->kind == TK_NONE || t->L <= 0 || t->n <= 0) return;
    for (int guard = 0; guard < 64; guard++) {
        int32_t u = t->A + t->loop * t->L + item_off(t, t->i);
        uint32_t F = frame_of(u);
        int32_t until = (int32_t)(F - clk.frames);
        if (until > LOOK_F + 12000) break;                          // nothing starts this early (no lead is 375 ms)
        if (t->kind == TK_BOWL) {
            if (until > LOOK_F) break;
            if (until > -LATE_F) bowl_strike_at((int)P(SC_BOWLS) == 2 ? 0.42f + 0.1f * frand() : 0.55f + 0.12f * frand(), F);
        } else {
            build(t, t->i, F, u);
            uint32_t first = F;
            for (int k = 0; k < ev_n; k++) if ((int32_t)(evs[k].at - first) < 0) first = evs[k].at;
            if ((int32_t)(first - clk.frames) > LOOK_F) break;
            if (until > -LATE_F) {
                bool lead_vowel = false;
                for (int k = 0; k < ev_n; k++) {
                    push(evs[k].v, evs[k].at, evs[k].ph, evs[k].flags, evs[k].dur, evs[k].note, evs[k].level, evs[k].kind, evs[k].tag);
                    if (evs[k].kind == K_VOWEL || evs[k].kind == K_DHUM) lead_vowel = true;
                }
                if (lead_vowel && (t->kind != TK_OM || t->i == 0)) accent_next = false;
                if (disp_has) { disp[disp_w & 15] = disp_new; disp_w++; }
            }
        }
        if (++t->i >= t->n) { t->i = 0; t->loop++; }
    }
}

// find the place in a loop again: the first item at or after unit u
static void seek(trk_t *t, int32_t u)
{
    if (t->kind == TK_NONE || t->L <= 0) return;
    int32_t rel = u - t->A;
    t->loop = (int32_t)fdiv(rel, t->L);
    int32_t within = rel - t->loop * t->L;
    t->i = 0;
    while (t->i < t->n && item_off(t, t->i) < within) t->i++;
    if (t->i >= t->n) { t->i = 0; t->loop++; }
}
static int32_t ahead_u(void) { return clk.u + units_of_f(LOOK_F); }

static int32_t phrase_L(int mode)
{
    switch (mode) {
    case MODE_OM: case MODE_KARGYRAA: case MODE_MANI: return clampi((int)P(SC_PHRASE), 1, 8) * bar_u();
    case MODE_THROAT: return MEL_U;
    default: return 2 * bar_u();
    }
}
static void chant_track(int mode, int32_t A)
{
    switch (mode) {
    case MODE_OM:       T[TC] = (trk_t){ TK_OM, A, phrase_L(mode), 0, 0, 5 }; break;
    case MODE_KARGYRAA: T[TC] = (trk_t){ TK_KARG, A, phrase_L(mode), 0, 0, 5 }; break;
    case MODE_MANI:     T[TC] = (trk_t){ TK_MANI, A, phrase_L(mode), 0, 0, MANI_K * mani_bars() }; break;
    default:            T[TC] = (trk_t){ TK_NONE, A, phrase_L(mode), 0, 0, 0 }; break;   // THROAT: its melody's anchor
    }
}
// the bowl's strikes: on the chant's phrases (zone 0) or Daisy's choruses; or each bar
static void bowl_track(int mode, int z, int32_t from)
{
    int b = (int)P(SC_BOWLS);
    T[TB] = (trk_t){ TK_NONE, 0, 0, 0, 0, 0 };
    if (b <= 0) return;
    bool chant = z == Z_CHANT && mode != MODE_DAISY;
    int32_t A = chant ? T[TC].A : DA;
    T[TB] = (trk_t){ TK_BOWL, A, b == 2 ? bar_u() : chant ? phrase_L(mode) : DAISY_LOOP_Q * dq, 0, 0, 1 };
    seek(&T[TB], from);
}
static void daisy_tracks(int32_t from)
{
    T[TD] = (trk_t){ TK_DAISY, DA, DAISY_LOOP_Q * dq, 0, 0, nditems };
    T[TW] = (trk_t){ TK_WALTZ, DA, DAISY_LOOP_Q * dq, 0, 0, DAISY_LOOP_Q };
    seek(&T[TD], from); seek(&T[TW], from);
}
// the first of Daisy's bar lines at or after u with no note held across it
static int32_t next_clean_bar(int32_t u)
{
    int32_t bar = dbar(), k = (int32_t)fdiv(u - DA + bar - 1, bar);
    for (int g = 0; g < DAISY_LOOP_Q / 3 + 1; g++, k++) if (bar_clean[mn_mod(k, DAISY_LOOP_Q / 3)]) break;
    return DA + k * bar;
}
static int knob_zone(void)
{
    float b = mn_blend;
    int raw = b < 0.25f ? 0 : b < 0.5f ? 1 : b < 0.75f ? 2 : 3, t = Z.target;
    if (t < 0) return raw;
    float lo = 0.25f * (float)t, hi = lo + 0.25f;                   // a little hysteresis at the edges
    return (b < lo - 0.02f && t > 0) || (b > hi + 0.02f && t < 3) ? raw : t;
}

static void start_mode(int mode)
{
    flush_all();
    for (int k = 0; k < NTRK; k++) T[k] = (trk_t){ TK_NONE, 0, 0, 0, 0, 0 };
    cur_mode = mode;
    Z.target = -1; Z.target = knob_zone();
    Z.now = Z.old = Z.target; Z.pending = false; Z.xf = clk.u - 4 * dbar();
    chant_track(mode, next_beat_u(LOOK_F + 9600));                  // the chant at the next beat
    seek(&T[TC], T[TC].A);
    // Daisy where she is; her singers from the next clean bar line
    daisy_tracks(Z.now == Z_CHANT && mode != MODE_DAISY ? ahead_u() : next_clean_bar(clk.u + units_of_f(LOOK_F + 9600)));
    bowl_track(mode, Z.now, ahead_u());
    if (karg_drone(mode, Z.now))                                    // the growl's drone under the tune, from the next beat
        push(0, frame_of(T[TC].A), PH_ER, MVF_HOLD, 2.0f, karg_note(), 0.85f, K_DRONE, 0);
    memset(disp, 0, sizeof disp); disp_w = 0;                       // no words left from the sound before
    ot_cur = 0; hand_on = false;
    karg_rot = 0; accent_next = false; hum_on = false;
    last_phrase = P(SC_PHRASE); last_dspeed = P(SC_DSPEED); last_monks = P(SC_MONKS); last_bowls = P(SC_BOWLS);
    reset_yaw = true;
}

// the zones: the knob asks, the next clean bar line of Daisy answers
static void prepare(int mode)        // the tracks for the zone coming at Z.S
{
    if (Z.next == Z_CHANT) { chant_track(mode, Z.S); seek(&T[TC], Z.S); }    // the chant starts its phrase there
    bowl_track(mode, Z.next, Z.S);
}
static void zones_tick(int mode)
{
    if (Z.pending && clk.u - Z.S >= 0) { Z.old = Z.now; Z.now = Z.next; Z.xf = Z.S; Z.pending = false; }
    Z.target = knob_zone();
    if (Z.pending && !Z.committed && Z.next != Z.target) {
        if (Z.target == Z.now) { Z.pending = false; bowl_track(mode, Z.now, ahead_u()); }
        else { Z.next = Z.target; prepare(mode); }
    }
    if (!Z.pending && Z.target != Z.now) {
        Z.pending = true; Z.committed = false; Z.next = Z.target;
        Z.S = next_clean_bar(clk.u + units_of_f(LOOK_F + 16000));
        prepare(mode);
    }
}
// once everything before the bar line is pushed: the voices with no part after it stop there
static void commit(int mode)
{
    if (!Z.pending || Z.committed || (int32_t)(frame_of(Z.S) - clk.frames) > LOOK_F) return;
    Z.committed = true;
    uint32_t FS = frame_of(Z.S), FB = frame_of(Z.S + dbar()), FH = frame_of(Z.S + dbar() / 2);
    float q = s_of_units(dq);
    for (int v = 0; v < MV_N; v++) {
        if (!seq_used(mode, Z.now, v) || seq_used(mode, Z.next, v)) continue;
        if (mode == MODE_KARGYRAA && v == 0) {                       // the growl fades over the bar
            push(0, FS, PH_ER, MVF_HOLD, 1.5f * q, karg_note(), 0.5f, K_DRONE, 0);
            push(0, FH, PH_ER, MVF_HOLD, 1.5f * q, karg_note(), 0.22f, K_DRONE, 0);
            push(0, FB, PH_SIL, 0, 0.05f, 0.0f, 0.0f, K_STOP, 0);
        } else push(v, FS, PH_SIL, 0, 0.05f, 0.0f, 0.0f, K_STOP, 0);
    }
    if (karg_drone(mode, Z.next) && !karg_drone(mode, Z.now)) {    // the growl holds a drone under the tune
        if (Z.now == Z_DAISY) {
            push(0, FS, PH_ER, MVF_HOLD, 1.5f * q, karg_note(), 0.35f, K_DRONE, 0);
            push(0, FH, PH_ER, MVF_HOLD, 1.5f * q, karg_note(), 0.6f, K_DRONE, 0);
            push(0, FB, PH_ER, MVF_HOLD, 3.0f * q, karg_note(), 0.85f, K_DRONE, 0);
        } else push(0, FS, PH_ER, MVF_HOLD, 3.0f * q, karg_note(), 0.85f, K_DRONE, 0);
    }
}

// a pattern's settings changed while it sings
static void restyle(void)
{
    int mode = cur_mode;
    if (P(SC_PHRASE) != last_phrase && (mode == MODE_OM || mode == MODE_MANI || mode == MODE_KARGYRAA)) {
        // the new phrase starts where the one singing now ends
        trk_t *t = &T[TC];
        int32_t A = t->A + (t->loop + (t->i > 0 ? 1 : 0)) * t->L;
        chant_track(mode, A);
        if (Z.now == Z_CHANT) bowl_track(mode, Z.now, ahead_u());
    }
    if (P(SC_DSPEED) != last_dspeed) {
        // the same place in the tune, at the new speed, its quarters on the beat's grid
        int32_t q_new = dspeed_u[clampi((int)P(SC_DSPEED), 0, 4)];
        int32_t pos6 = (int32_t)fdiv((int64_t)(clk.u - DA) * 6, dq);
        int32_t A = clk.u - (int32_t)((int64_t)pos6 * q_new / 6);
        DA = (int32_t)fdiv(A + q_new / 2, q_new) * q_new;
        dq = q_new;
        daisy_tracks(ahead_u());
        if (Z.pending && !Z.committed) { Z.pending = false; Z.target = -1; }   // asked again on the new bars
        bowl_track(mode, Z.now, ahead_u());
    }
    if (P(SC_MONKS) < last_monks) for (int v = 0; v < MV_N; v++) {
        if (seq_voice[v] && !seq_used(mode, Z.now, v)) { mv_flush(v); seq_voice[v] = false; }
    }
    if (P(SC_BOWLS) != last_bowls) bowl_track(mode, Z.now, ahead_u());
    last_phrase = P(SC_PHRASE); last_dspeed = P(SC_DSPEED); last_monks = P(SC_MONKS); last_bowls = P(SC_BOWLS);
}

// where in its loop a track is now, 0..1
static float loop_pos(const trk_t *t)
{
    if (t->L <= 0) return 0.0f;
    int32_t rel = clk.u - t->A;
    if (rel < 0) return 0.0f;
    return (float)(rel % t->L) / (float)t->L;
}
static int daisy_p6(void) { return (int)mn_mod((int32_t)fdiv((int64_t)(clk.u - DA) * 6, dq), DAISY_LOOP_Q * 6); }

// ---------------------------------------------------------------- the whistle
static float melody_h(int32_t rel, bool *breath)   // THROAT's own melodies
{
    int style = clampi((int)P(SC_STYLE), 0, 1), mi = clampi((int)P(SC_MELODY), 0, 4);
    const mstep_t *m = mels[mi];
    int n = mel_n[mi];
    rel = (int32_t)(rel % MEL_U);
    if (rel < 0) rel += MEL_U;
    *breath = rel >= MEL_U - BREATH_U;
    int32_t at = 0;
    int k = 0;
    for (; k < n - 1; k++) { int32_t len = m[k].half * (PPB / 2); if (rel < at + len) break; at += len; }
    float cur = harm[style][m[k].deg], prev = harm[style][m[(k + n - 1) % n].deg];
    return prev + (cur - prev) * smoothstep(0.0f, 0.09f, s_of_units(rel - at));
}
static float daisy_h(int j) { return clampf(roundf(8.0f * exp2f((float)(dnotes[j].note - 67) / 12.0f)), 4.0f, 16.0f); }
static float daisy_whistle(void)     // Daisy's tune on the drone's harmonics: her tonic on the 8th
{
    int p6 = daisy_p6(), j = 0;
    for (int k = 0; k < NDN; k++) if (dnotes[k].p6 <= p6) j = k;
    float cur = daisy_h(j), prev = daisy_h(j > 0 ? j - 1 : NDN - 1);
    float since = s_of_units((int32_t)(p6 - dnotes[j].p6) * dq / 6);
    return prev + (cur - prev) * smoothstep(0.0f, 0.06f, since);
}
// the hands take the whistle: the yaw a harmonic per eighth of a turn, the roll (HANDS > ROLL: OVERTONE) four
static float hands_whistle(const ctl_t *c, float target)
{
    bool use_yaw = p_hyaw >= 0 && (int)pv[p_hyaw] == 0, use_roll = p_hrol >= 0 && (int)pv[p_hrol] == 1;
    bool moving = (use_yaw && fabsf(hands.yaw_rate) > 0.04f) || (use_roll && fabsf(hands.roll) > 0.12f);
    if (moving) {
        if (!hand_on) { hand_on = true; yaw0 = hands.yaw; ot_hand0 = roundf(ot_cur > 0 ? ot_cur : target); }
        hand_t = 3.0f;
    } else if (hand_on && (hand_t -= c->dt) <= 0.0f) hand_on = false;
    if (!hand_on) return target;
    float h = ot_hand0 + (use_yaw ? (hands.yaw - yaw0) * 8.0f : 0.0f) + (use_roll ? hands.roll * 4.0f : 0.0f);
    return clampf(roundf(h), 4.0f, 16.0f);
}

// ---------------------------------------------------------------- the voices' controls in each zone
// VOICE adds its UNDERTONE, ROUGH and TRACT (the sound's) to these: the chant keeps them, Daisy's zones take them
// away little by little (a negative offset), so Daisy alone is sung plainly
typedef struct { float sub, rough, tract; } zc_t;
static zc_t zone_ctl(int mode, int z)
{
    static const float keep[4] = { 1.0f, 0.6f, 0.3f, 0.0f };
    float k = mode == MODE_DAISY ? 1.0f : keep[z];
    return (zc_t){ -vpar(p_vund) * (1.0f - k), -vpar(p_vrou) * (1.0f - k), -vpar(p_vtrc) * (1.0f - k) };
}
static float drone_lv(int mode, int z, const ctl_t *c, bool *throat_breath)
{
    if (z == Z_DAISY || mode == MODE_DAISY) return 0.0f;
    switch (mode) {
    case MODE_OM:     return z == Z_CHANT ? 0.0f : 0.45f;
    case MODE_MANI:   return 0.6f * P(SC_DRONE) * 0.01f;
    case MODE_THROAT: return (*throat_breath && z == Z_CHANT) ? 0.0f : clampi((int)P(SC_STYLE), 0, 1) ? 0.72f : 0.85f;
    case MODE_VOWELS: return z == Z_CHANT ? 0.85f : 0.7f;
    case MODE_HUM:    return hum_on ? (z == Z_CHANT ? 0.8f : 0.65f) : 0.0f;
    default:          (void)c; return 0.0f;
    }
}

// ---------------------------------------------------------------- the tick
void sc_tick(const ctl_t *c, mv_ctl_t *m)
{
#ifndef ESP_PLATFORM
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
    if (p_hrol < 0) {
        p_hrol = param_by_tag("hrol"); p_hyaw = param_by_tag("hyaw");
        p_vund = param_by_tag("vund"); p_vrou = param_by_tag("vrou"); p_vtrc = param_by_tag("vtrc"); p_vshp = param_by_tag("vshp");
    }
    clock_tick(c->frames);
    if (!daisy_set) { DA = 0; dq = dspeed_u[clampi((int)P(SC_DSPEED), 0, 4)]; last_dspeed = P(SC_DSPEED); daisy_set = true; }
    int mode = sc_mode();
    if (!hands.running) {
        if (cur_mode >= 0) { flush_all(); cur_mode = -1; }
        return;
    }
    if (clk.beat > 900000) {                                        // keep the units in range
        int32_t d = 786432 * PPB;
        clk.beat -= 786432; clk.u -= d; DA -= d; Z.S -= d; Z.xf -= d;
        for (int k = 0; k < NTRK; k++) T[k].A -= d;
    }
    if (mode != cur_mode) start_mode(mode);
    else restyle();
    if (clk.jumped) {                                               // the beat moved under us: find the place again
        flush_all();
        Z.pending = false; Z.xf = clk.u - 4 * dbar();
        for (int k = 0; k < NTRK; k++) seek(&T[k], ahead_u());
    }
    zones_tick(mode);
    if (reset_yaw) { yaw_ref = hands.yaw; reset_yaw = false; }
    // the pump: an accent (the next syllable, the drone), the level and the breath swell
    if (hands.pumped) {
        acc = 1.0f; accent_next = true;
        if (mode == MODE_KARGYRAA && Z.now == Z_CHANT && !Z.pending && mv_queued(0) == 0) {   // the next vowel now
            karg_rot++;
            int ph = karg_v[(T[TC].loop + karg_rot) % 3][(T[TC].i % 5 + karg_rot) % 4];
            push(0, clk.frames + 640, ph, MVF_HOLD | MVF_ACCENT, 0.5f, karg_note(), 1.0f, K_VOWEL, ph);
        }
    } else acc *= expf(-c->dt / 0.18f);

    // the crossing: the zone before and the zone now, over the bar after the switch
    float x = clampf((float)(clk.u - Z.xf) / (float)dbar(), 0.0f, 1.0f);
    int zo = Z.old, zn = Z.now;
    float wo = zo == zn ? 0.0f : 1.0f - x, wn = 1.0f - wo;
    zc_t co = zone_ctl(mode, zo), cn = zone_ctl(mode, zn);
    m->sub = wo * co.sub + wn * cn.sub;
    m->rough = wo * co.rough + wn * cn.rough;
    m->tract = wo * co.tract + wn * cn.tract;
    float dw = wo * (float)(zo == Z_DAISY || mode == MODE_DAISY) + wn * (float)(zn == Z_DAISY || mode == MODE_DAISY);
    m->breath = lerpf(m->breath, 0.04f + 0.4f * hands.pump, dw);   // Daisy's 1961 voice had no air in it

    // the free voices: the drones
    for (int v = 0; v < MV_N; v++) m->v[v] = (mv_free_t){ false, 0.0f, 0.0f };
    bool tbreath = false;
    int32_t trel = clk.u - T[TC].A;
    float mel = 0.0f;
    if (mode == MODE_THROAT) mel = trel >= 0 ? melody_h(trel, &tbreath) : harm[clampi((int)P(SC_STYLE), 0, 1)][1];
    if (mode == MODE_HUM) {
        float tr = mv_track();
        if (tr > 0.0f) {
            float n = tr;
            if ((int)P(SC_SNAP) == 1 && song.scale_mask) {          // the nearest note of the song's scale
                int r = (int)lroundf(tr), best = r;
                for (int d = 0; d <= 6; d++) {
                    if (song.scale_mask & (1u << (((r + d) % 12 + 12) % 12))) { best = r + d; break; }
                    if (song.scale_mask & (1u << (((r - d) % 12 + 12) % 12))) { best = r - d; break; }
                }
                n = (float)best;
            }
            hum_note = n + (float)oct(); hum_hold = 0.15f; hum_on = true;
        } else if (hum_on && (hum_hold -= c->dt) <= 0.0f) hum_on = false;
    }
    float dl = wo * drone_lv(mode, zo, c, &tbreath) + wn * drone_lv(mode, zn, c, &tbreath);
    dl = clampf(dl * (1.0f + 0.35f * acc), 0.0f, 1.0f);
    switch (mode) {
    case MODE_OM:     m->v[5] = (mv_free_t){ dl > 0.001f, om_root(), dl }; break;
    case MODE_MANI:   m->v[1] = (mv_free_t){ dl > 0.001f, mani_tone() - 12.0f, dl }; break;
    case MODE_THROAT: m->v[0] = (mv_free_t){ dl > 0.001f, drone_root(), dl }; break;
    case MODE_VOWELS: m->v[0] = (mv_free_t){ dl > 0.001f, (float)(scale_note(0, 3) + oct()), dl }; break;
    case MODE_HUM:    m->v[0] = (mv_free_t){ dl > 0.001f, hum_note, dl }; break;
    default: break;
    }
    // OM's mouth shuts to M at each phrase's end (the chant's zone)
    float cw = wo * (float)(zo == Z_CHANT) + wn * (float)(zn == Z_CHANT);
    if (mode == MODE_OM && cw > 0.0f) {
        float ph = loop_pos(&T[TC]);
        float hum = ph < 0.85f ? smoothstep(0.5f, 0.62f, ph) : 1.0f - smoothstep(0.85f, 0.88f, ph);
        m->hum = fmaxf(m->hum, hum * cw);
    }
    // the whistle: THROAT's melody, or Daisy's on the harmonics; the hands take it; elsewhere the yaw alone
    float ww = wo * (float)whistle_in(mode, zo) + wn * (float)whistle_in(mode, zn);
    if (ww > 0.0f) {
        bool dz = (zn == Z_HUM && wn >= 0.5f) || (zo == Z_HUM && wo > 0.5f);
        float target = mode == MODE_THROAT && !dz ? mel : daisy_whistle();
        target = hands_whistle(c, target);
        if (ot_cur <= 0.0f) ot_cur = target;
        ot_cur += (target - ot_cur) * (1.0f - expf(-c->dt / 0.025f));
        m->overtone = ot_cur;
        m->sharp = -vpar(p_vshp) * (1.0f - ww);                    // VOICE's WHISTLE, fading as the zone goes
        harm_now = (int)(ot_cur + 0.5f);
        if (mode == MODE_THROAT) {                                  // "er", "ng": the jaw low, a little hum
            m->jaw = clampf(0.28f + 0.25f * (hands.jaw - 0.5f), 0.0f, 1.0f);
            m->hum = fmaxf(m->hum, 0.2f);
        }
    } else if (p_hyaw >= 0 && (int)pv[p_hyaw] == 0) {               // an eighth of a turn per harmonic, from 4 up
        float dy = fabsf(hands.yaw - yaw_ref);
        if (dy > 0.0625f) {
            float h = clampf(4.0f + roundf((dy - 0.0625f) * 8.0f), 4.0f, 16.0f);
            ot_cur = ot_cur <= 0.0f ? h : ot_cur + (h - ot_cur) * (1.0f - expf(-c->dt / 0.025f));
            m->overtone = ot_cur;
            harm_now = (int)(ot_cur + 0.5f);
        } else { ot_cur = 0.0f; harm_now = 0; }
    } else { ot_cur = 0.0f; harm_now = 0; }
    if (mode == MODE_THROAT && (zn != Z_CHANT || zo != Z_CHANT) && ww <= 0.0f) {   // the drone's vowel stays
        m->jaw = clampf(0.28f + 0.25f * (hands.jaw - 0.5f), 0.0f, 1.0f);
    }
    // the pump: the level and the breath swell with each stroke
    m->level = clampf(m->level * (1.0f + 0.25f * acc), 0.0f, 1.0f);
    m->breath = clampf(m->breath + 0.2f * acc, 0.0f, 1.0f);

    for (int k = 0; k < NTRK; k++) run_track(&T[k]);
    commit(mode);

    // the screen: the zone's breath and voices
    int zv = zn;
    vset_t sv = singers(mode, zv), ch = chanters(mode);
    int voices = sv.n + (has_waltz(mode, zv) && P(SC_WALTZ) > 0.0f ? 2 : 0) + (zv == Z_CHANT ? ch.n : 0) + (karg_drone(mode, zv) ? 1 : 0);
    for (int v = 0; v < MV_N; v++) if (m->v[v].on) voices++;
    view.monks = clampi(voices, 0, MV_N);
    if (zv == Z_CHANT && mode != MODE_DAISY) {
        switch (mode) {
        case MODE_OM: { float ph = loop_pos(&T[TC]); view.breath = ph < 0.86f ? 1.0f - ph / 0.86f : (ph - 0.86f) / 0.14f; break; }
        case MODE_MANI: { float ph = loop_pos(&T[TC]), e = 1.0f - 0.08f / (float)mani_bars(); view.breath = ph < e ? 1.0f - ph / e : (ph - e) / (1.0f - e); break; }
        case MODE_KARGYRAA: { float ph = loop_pos(&T[TC]); view.breath = ph < 0.9f ? 1.0f - ph / 0.9f : (ph - 0.9f) / 0.1f; break; }
        case MODE_THROAT: { int32_t r = mn_mod(trel, MEL_U); view.breath = tbreath ? (float)(r - (MEL_U - BREATH_U)) / (float)BREATH_U : 1.0f - (float)r / (float)(MEL_U - BREATH_U); break; }
        default: view.breath = 0.5f + 0.5f * sin_turns((float)c->ticks * 0.0025f); break;
        }
    } else {                                                        // Daisy's lungs: full at each line, empty at its end
        int p6 = daisy_p6(), l = 0, a6 = 0, b6 = DAISY_LOOP_Q * 6;
        for (int k = 0; k < NDN; k++) if (dnotes[k].syl >= 0 && dnotes[k].p6 <= p6) l = syl_line[dnotes[k].syl];
        for (int k = 0; k < NDN; k++) {
            if (dnotes[k].syl == dline_first[l]) a6 = dnotes[k].p6;
            if (l + 1 < NLINES && dnotes[k].syl == dline_first[l + 1]) b6 = dnotes[k].p6;
        }
        float xl = b6 > a6 ? (float)(p6 - a6) / (float)(b6 - a6) : 0.0f;
        view.breath = xl < 0.9f ? 1.0f - xl / 0.9f : (xl - 0.9f) / 0.1f;
    }
    view.harmonic = m->overtone > 0.0f ? harm_now : 0;
    view.mode = mode;

    // the bowl: near the key's root (octave 3), its ring and the mallet
    bowl_set(midi_hz((float)(place(key_pc(), 48) + (int)P(SC_BNOTE))), P(SC_BRING), P(SC_MALLET) * 0.01f);
    view.bowl = bowl_level();
#ifndef ESP_PLATFORM
    sc_last_ctl = *m;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    sc_tick_ns += (double)(t1.tv_sec - t0.tv_sec) * 1e9 + (double)(t1.tv_nsec - t0.tv_nsec);
    sc_tick_n++;
#endif
}

void sc_view(sc_view_t *v)
{
    *v = view;
    uint32_t now = audio_frames();
    v->line = NULL; v->word_at = v->word_len = 0;
    int z = Z.now;
    if ((cur_mode == MODE_VOWELS || cur_mode == MODE_HUM) && z <= Z_HUM) {   // the jaw's vowel
        int k = clampi((int)(hands.jaw * 4.0f + 0.5f), 0, 4);
        v->line = vowel_line; v->word_at = 3 * k; v->word_len = 2;
        if (cur_mode == MODE_HUM && !hum_on) v->word_len = 0;
        return;
    }
    const disp_t *best = NULL;
    for (int k = 0; k < 16 && k < disp_w; k++) {
        const disp_t *d = &disp[(disp_w - 1 - k) & 15];
        if ((int32_t)(now - d->frame) >= 0) { best = d; break; }
    }
    if (!best || !best->line) return;
    v->line = best->line;
    if ((int32_t)(now - best->end) < 0) { v->word_at = best->at; v->word_len = best->len; }
}

// ---------------------------------------------------------------- setup
void sc_init(void)
{
    // the lead's items: each syllable, the turn's notes, each coda (just before the syllable after it)
    nditems = 0;
    for (int j = 0; j < NDN; j++) {
        int s = dnotes[j].syl;
        if (s < 0) continue;
        ditems[nditems++] = (ditem_t){ dnotes[j].p6, dnotes[j].p6, DI_SYL, (uint8_t)j, (uint8_t)s };
        int last = syl_last_note(j);
        for (int k = j + 1; k <= last; k++) ditems[nditems++] = (ditem_t){ dnotes[k].p6, dnotes[j].p6, DI_TURN, (uint8_t)k, (uint8_t)s };
        if (n_of(dsyl[s].co) > 0) ditems[nditems++] = (ditem_t){ (int16_t)dn_end6(last), dnotes[j].p6, DI_CODA, (uint8_t)j, (uint8_t)s };
        j = last;
    }
    // the bar lines where nothing is held across
    for (int b = 0; b < DAISY_LOOP_Q / 3; b++) {
        bar_clean[b] = true;
        for (int j = 0; j < NDN; j++) if (dnotes[j].p6 < 18 * b && dn_end6(j) > 18 * b) bar_clean[b] = false;
    }
    // each syllable's span in its line of the lyric
    for (int l = 0; l < NLINES; l++) {
        const char *s = dlines[l];
        int at = 0, len = (int)strlen(s);
        for (int k = dline_first[l]; k < dline_first[l + 1]; k++) {
            const char *t = dsyl[k].txt;
            int n = (int)strlen(t);
            while (at < len && s[at] != t[0]) at++;
            syl_line[k] = (int8_t)l;
            if (at + n <= len && !strncmp(s + at, t, (size_t)n)) { syl_at[k] = (int8_t)at; syl_len[k] = (int8_t)n; at += n; }
            else { syl_at[k] = 0; syl_len[k] = 0; }
        }
    }
    memset(&view, 0, sizeof view);
    Z.target = -1;
}

static void init(void) { }
static void changed(int p) { if (p < 0) reset_yaw = true; }
const module_t mod_score = { .name = "SCORE", .letter = 'c', .pages = pages, .npages = 2, .params = params, .nparams = SC_N,
                             .sounds = sounds, .init = init, .changed = changed };

#ifndef ESP_PLATFORM
// tools/sim: the tune as the score has it, and where things are
int sc_test_daisy(int k, int *p6, int *l6, int *note, int *syl)
{
    if (k < 0 || k >= NDN) return NDN;
    *p6 = dnotes[k].p6; *l6 = dnotes[k].l6; *note = dnotes[k].note; *syl = dnotes[k].syl;
    return NDN;
}
int sc_test_chord(int bar) { return bar >= 0 && bar < DAISY_LOOP_Q / 3 ? dchord[bar] : -1; }
const char *sc_test_syl(int s, int *nuc) { if (s < 0 || s >= NSYL) return NULL; *nuc = dsyl[s].nuc; return dsyl[s].txt; }
int sc_test_line(int s) { return s >= 0 && s < NSYL ? syl_line[s] : -1; }
int sc_test_anchor(int k) { return k >= 0 && k < NTRK ? T[k].A : 0; }
int sc_test_harm(int style, int deg) { return harm[style][deg]; }
int sc_test_melody(int mi, int k, int *half) { if (k >= mel_n[mi]) return -1; *half = mels[mi][k].half; return mels[mi][k].deg; }
// Daisy's clock: her anchor and quarter in song beats, the zone now and the one coming (and where, in song beats)
void sc_test_daisy_clock(double *anchor_beats, double *quarter_beats) { *anchor_beats = (double)DA / PPB; *quarter_beats = (double)dq / PPB; }
int sc_test_zone(int *next, double *at_beats) { *next = Z.pending ? Z.next : -1; *at_beats = (double)Z.S / PPB; return Z.now; }
// the song's beat as the score counts it (unwrapped) at an audio frame near now
double sc_test_beats(uint32_t frame) { return (double)clk.beat + ((double)clk.off + (double)(int32_t)(frame - clk.frames)) / (double)clk.fpb; }
bool sc_test_bar_clean(int b) { return b >= 0 && b < DAISY_LOOP_Q / 3 && bar_clean[b]; }
#endif
