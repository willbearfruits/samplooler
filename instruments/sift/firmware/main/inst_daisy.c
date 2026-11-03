// Daisy Bell (Harry Dacre, 1892, public domain), its first two lines, sung into the corpus by MANTRA's voice
// (inst_voice.c, run offline): "Daisy, Daisy, give me your answer, do! I'm half crazy, all for the love of you!"
// The score and the way each syllable is sung are MANTRA's (inst_score.c, daisy_voice): the onset consonants before
// the beat, the vowel on it, the coda at the note's end, steady notes a man's octave down, a quarter of 0.36 s (where
// whisper.cpp heard the most words in MANTRA's tests). 16.9 s of song with a lead-in and a tail: it fits SIFT's 20 s.
// The render runs in the UI task a slice at a time (daisy_step), so the screen and the old sound go on meanwhile.
// It knows where each syllable starts: the cuts the decomposition breaks at, and the words on the screen.
#include "inst_sift.h"
#include "inst_voice.h"
#include "dsp.h"
#include <string.h>

extern uint32_t (*mv_clock)(void);

typedef struct { const char *txt; uint8_t on[2], nuc, co[2]; } syl_t;
#define X_ PH_SIL
static const syl_t dsyl[DAISY_NSYL] = {
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
};
// the notes: start and length in sixths of a quarter, MIDI as written (in G), the syllable
typedef struct { int16_t p6, l6; int8_t note, syl; } dnote_t;
static const dnote_t dnotes[DAISY_NSYL] = {
    { 0, 18, 74, 0 }, { 18, 18, 71, 1 }, { 36, 18, 67, 2 }, { 54, 18, 62, 3 },
    { 72, 6, 64, 4 }, { 78, 6, 66, 5 }, { 84, 6, 67, 6 }, { 90, 12, 64, 7 }, { 102, 6, 67, 8 }, { 108, 30, 62, 9 },
    { 144, 18, 69, 10 }, { 162, 18, 74, 11 }, { 180, 18, 71, 12 }, { 198, 18, 67, 13 },
    { 216, 6, 64, 14 }, { 222, 6, 66, 15 }, { 228, 6, 67, 16 }, { 234, 12, 69, 17 }, { 246, 6, 71, 18 }, { 252, 30, 69, 19 },
};
static const char *const dlines[DAISY_NLINES] = { "DAISY, DAISY,", "GIVE ME YOUR ANSWER, DO!", "I'M HALF CRAZY,", "ALL FOR THE LOVE OF YOU!" };
static const uint8_t dline_first[DAISY_NLINES + 1] = { 0, 4, 10, 14, 20 };

#define Q_S    0.36f                                  // a quarter
#define LEAD   (AUDIO_SR / 4)                         // before beat 0: room for the first D
#define TAIL   (AUDIO_SR * 6 / 10)                    // after the last note: the vowel's release
#define T0     1000u                                  // the offline clock's start (an event at 0 means "after the last")

static float q_s(void) { return Q_S; }
static uint32_t beat_f(int p6) { return T0 + LEAD + (uint32_t)((float)p6 / 6.0f * q_s() * (float)AUDIO_SR + 0.5f); }
static uint8_t cons_ms(int ph) { return ph == PH_SIL ? 0 : (uint8_t)clampi((int)(mv_ph_len(ph) * 900.0f + 0.5f), 20, 200); }
static uint32_t ms_f(float ms) { return (uint32_t)(ms * (float)AUDIO_SR * 0.001f); }
static int n_of(const uint8_t *ph) { return (ph[0] != PH_SIL) + (ph[1] != PH_SIL); }
static int cons_frames(const uint8_t *ph, int n, float room, uint32_t *f)
{
    float tot = 0;
    for (int i = 0; i < n; i++) tot += (float)cons_ms(ph[i]) * 0.001f;
    float k = tot > room && tot > 0 ? room / tot : 1.0f;
    int sum = 0;
    for (int i = 0; i < n; i++) { f[i] = ph[i] ? ms_f((float)cons_ms(ph[i]) * k) : 0; sum += (int)f[i]; }
    return sum;
}
static float onset_room(int j)
{
    float q = q_s();
    float before = j > 0 ? (float)(dnotes[j].p6 - dnotes[j - 1].p6) / 6.0f * q : 3.0f * q;
    return fminf(0.35f * before, 0.25f);
}

static mv_ev_t evs[96];
static int nev, evi;
static int32_t syl_at[DAISY_NSYL];                   // where each syllable starts in the render (its first onset), frames
static void ev(uint32_t at, int ph, int flags, float dur, float note, float level)
{
    if (nev < (int)(sizeof evs / sizeof evs[0])) evs[nev++] = (mv_ev_t){ at, (uint8_t)ph, (uint8_t)flags, dur, note, clampf(level, 0.0f, 1.0f) };
}

static void build(void)
{
    nev = 0;
    float q = q_s();
    for (int j = 0; j < DAISY_NSYL; j++) {
        const syl_t *S = &dsyl[dnotes[j].syl];
        int end6 = dnotes[j].p6 + dnotes[j].l6;
        bool legato = j + 1 < DAISY_NSYL && dnotes[j + 1].p6 == end6;
        float note = (float)(dnotes[j].note - 12);                   // a man's octave
        float len_s = (float)dnotes[j].l6 / 6.0f * q;
        float lv = dnotes[j].p6 % 18 == 0 ? 0.9f : 0.82f;
        uint32_t F = beat_f(dnotes[j].p6), f[2];
        int no = n_of(S->on);
        int sum = cons_frames(S->on, no, onset_room(j), f);
        uint32_t at = F - (uint32_t)sum;
        syl_at[j] = (int32_t)(at - T0);
        for (int k = 0; k < no; k++) { ev(at, S->on[k], 0, (float)f[k] / AUDIO_SR, note, 0.8f); at += f[k]; }
        bool coda = n_of(S->co) > 0;
        float vdur = coda ? len_s : 0.92f * len_s;
        if (legato && !coda) {
            const syl_t *N = &dsyl[dnotes[j + 1].syl]; uint32_t g2[2];
            int nl = cons_frames(N->on, n_of(N->on), onset_room(j + 1), g2);
            vdur = fmaxf(0.06f, len_s - (float)nl / AUDIO_SR - 0.015f);
        }
        ev(F, S->nuc, (coda ? MVF_HOLD : 0) | (no == 0 ? MVF_ACCENT : 0), vdur, note, lv);
        if (coda) {
            uint32_t fc[2], g[2]; int nc = n_of(S->co);
            float room = fminf(0.35f * len_s, 0.25f);
            int csum = cons_frames(S->co, nc, room, fc), lead_f = 0;
            if (legato) { const syl_t *N = &dsyl[dnotes[j + 1].syl]; lead_f = cons_frames(N->on, n_of(N->on), onset_room(j + 1), g); }
            uint32_t ca = beat_f(end6) - (uint32_t)lead_f - (uint32_t)csum;
            for (int k = 0; k < nc; k++) {
                bool hold = legato && k == nc - 1 && lead_f == 0;
                ev(ca, S->co[k], hold ? MVF_HOLD : 0, (float)fc[k] / AUDIO_SR, note, 0.75f);
                ca += fc[k];
            }
        }
    }
    ev(beat_f(dnotes[DAISY_NSYL - 1].p6 + dnotes[DAISY_NSYL - 1].l6), PH_SIL, 0, 0.5f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------- the render
static uint32_t clk;
static uint32_t dclock(void) { return clk; }
static int16_t *dst;
static int dmax, ddone, dtotal;
static bool busy;

int daisy_length(void) { return LEAD + (int)((float)(dnotes[DAISY_NSYL - 1].p6 + dnotes[DAISY_NSYL - 1].l6) / 6.0f * Q_S * (float)AUDIO_SR) + TAIL; }

bool daisy_begin(int16_t *d, int max)
{
    if (!d || max <= 0) return false;
    mv_init();
    mv_clock = dclock;
    clk = T0;
    mv_ctl_t c;
    memset(&c, 0, sizeof c);
    c.jaw = 0.5f; c.vib_rate = 5.5f; c.breath = 0.15f; c.level = 0.8f; c.send = 0.0f;
    mv_set(&c);
    build();
    evi = 0;
    dst = d; dmax = max; ddone = 0;
    dtotal = daisy_length(); if (dtotal > dmax) dtotal = dmax;
    busy = true;
    return true;
}

int daisy_step(int frames)
{
    if (!busy) return ddone;
    static float o[AUDIO_BLOCK], s[AUDIO_BLOCK];
    for (int r = 0; r < frames && ddone < dtotal; r += AUDIO_BLOCK) {
        while (evi < nev && (int32_t)(evs[evi].at - clk) < AUDIO_SR / 2 && mv_queued(0) < 28) { if (!mv_push(0, &evs[evi])) break; evi++; }
        memset(o, 0, sizeof o); memset(s, 0, sizeof s);
        mv_process(o, s, AUDIO_BLOCK);
        int m = dtotal - ddone < AUDIO_BLOCK ? dtotal - ddone : AUDIO_BLOCK;
        for (int i = 0; i < m; i++) dst[ddone + i] = (int16_t)(soft_tanh(o[i] * 0.7f) * 30000.0f);
        ddone += m;
        clk += AUDIO_BLOCK;
    }
    if (ddone >= dtotal) { busy = false; mv_clock = NULL; }
    return ddone;
}
bool daisy_busy(void) { return busy; }

// ---------------------------------------------------------------- the words
int daisy_cuts(int32_t *out, int max)
{
    int n = 0;
    for (int j = 0; j < DAISY_NSYL && n < max; j++) out[n++] = syl_at[j];
    return n;
}
const char *daisy_syl(int j) { return j >= 0 && j < DAISY_NSYL ? dsyl[j].txt : ""; }
int daisy_syl_line(int j) { for (int k = 0; k < DAISY_NLINES; k++) if (j < dline_first[k + 1]) return k; return DAISY_NLINES - 1; }
int daisy_line_first(int k) { return dline_first[clampi(k, 0, DAISY_NLINES)]; }
const char *daisy_line(int k) { return dlines[clampi(k, 0, DAISY_NLINES - 1)]; }
int32_t daisy_syl_at(int j) { return syl_at[clampi(j, 0, DAISY_NSYL - 1)]; }
