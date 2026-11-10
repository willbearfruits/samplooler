// PIXEL console: the ATARI (Atari 2600, the TIA, 1977).
//
//   voice 0   BASS     taken by the DRUMS for their few ticks (a kick falls, a snare and the hats are the 9-bit noise)
//   voice 1   LEAD     with the CHORDS' arpeggio between its notes, and on every third tick of a long note
//   no ECHO: both voices are busy
//
// Each voice is three registers: AUDC (4 bits: the waveform), AUDF (5 bits: the divider) and AUDV (4 bits: volume).
// The chip half is the TIA's own circuit: a 5-bit counter, the audio clock (two a scanline: 3579545 / 114 = 31399.5 Hz)
// divided by AUDF + 1, and two shift registers, one of 5 bits and one of 4, wired by AUDC (the logic below is the
// circuit as the Stella emulator describes it). Run, they give the sound table every 2600 programmer knew:
//   AUDC 0, 11    a steady level           1       a 15-step buzz (the 4-bit polynomial)
//   4, 5          a square, clock / 2      12, 13  a square, clock / 6
//   6, 10         a pulse, clock / 31 (18 high, 13 low)        14  clock / 93
//   7, 9          the 5-bit polynomial, 31 steps               15  the same three times slower
//   8             the 9-bit polynomial: noise, 511 steps       2, 3   the 4-bit one stepped by the others: rumble
// Every change of the output is put down as a band-limited step at its exact time (bl_step). The two voices' volumes
// share one output: a soft sum, 2 v / (30 + v) for v = AUDV0 + AUDV1 (Stella's model of it), then a 20 Hz high pass.
//
// The driver half. There are only 32 pitches to a waveform and few of them are notes: for each note the driver takes
// the (AUDC, AUDF) nearest in cents among the waveforms that suit the sound (the tables are made at init), which is
// how music was written for it. It is out of tune by design; slides and vibrato become steps or nothing.
// A voice at volume 0 is not run (its counters rest), which nobody can hear.
#include "pixel.h"
#include <string.h>

#define TIA_HZ  31399.5f                           // the audio clock (NTSC)
#define SPC     ((float)AUDIO_SR / TIA_HZ)         // our samples per audio clock

// ---------------------------------------------------------------- band-limited steps
// A step of the chip's output at any time between two samples is put down as a low-passed step (a Kaiser-windowed
// sinc, integrated) spread over the next BL_TAPS samples. The buffer holds the steps' sample-to-sample differences
// and render() sums them up with a small leak, which is the output's high pass: so only changes are ever added, a
// level that stands still is exact silence, and no rounding can leave a DC behind. BL[p][k] is that difference for
// the step's place within its sample (p / BL_PH) and the tap. The kernel is BL_TAPS - 1 samples wide, so the taps
// hold the whole step wherever in its sample it falls (each row adds up to 1). The sound comes out 7.5 samples late.
#define BL_TAPS 16
#define BL_PH   16
static float BL[BL_PH + 1][BL_TAPS];

static float bl_i0(float x) { float s = 1.0f, t = 1.0f; for (int k = 1; k < 24; k++) { float q = x / (2.0f * (float)k); t *= q * q; s += t; } return s; }
static void bl_init(void)                          // boot: libm is fine here
{
    const float beta = 6.0f, fc = 0.47f;           // the window; the cutoff in cycles per sample
    const float half = 0.5f * (float)(BL_TAPS - 1);
    const int   n = (BL_TAPS - 1) * BL_PH;         // the kernel, BL_PH points a sample
    float sum = 1.0f, i0b = bl_i0(beta);
    for (int pass = 0; pass < 2; pass++) {
        float acc = 0.0f;
        for (int j = 0; j <= n + BL_PH; j++) {     // the step at t = j / BL_PH - half samples (1 from n on)
            if (pass) {
                float s = acc / sum;
                int k = (j + BL_PH - 1) / BL_PH - 1, p = (k + 1) * BL_PH - j;
                if (k >= 0 && k < BL_TAPS) BL[p][k] = s;
                if (p == 0 && k + 1 < BL_TAPS) BL[BL_PH][k + 1] = s;
            }
            if (j < n) {
                float t = ((float)j + 0.5f) / (float)BL_PH - half, u = t / half;
                float x = 3.14159265f * 2.0f * fc * t;
                acc += (fabsf(x) < 1e-5f ? 1.0f : sinf(x) / x) * bl_i0(beta * sqrtf(1.0f - u * u)) / i0b;
            }
        }
        sum = acc;
    }
    for (int p = 0; p <= BL_PH; p++) for (int k = BL_TAPS - 1; k > 0; k--) BL[p][k] -= BL[p][k - 1];   // steps to differences
}
// a step of height d at `frac` (0..1) of the sample that b[0] belongs to (one copy of it: render() calls it from many places)
static __attribute__((noinline)) IRAM_ATTR void bl_step(float *b, float frac, float d)
{
    float fp = clampf(frac, 0.0f, 0.9999f) * (float)BL_PH;
    int   p = (int)fp;
    float f = fp - (float)p;
    const float *r0 = BL[p], *r1 = BL[p + 1];
    for (int k = 0; k < BL_TAPS; k++) b[k] += d * (r0[k] + f * (r1[k] - r0[k]));
}

// ---------------------------------------------------------------- the chip
typedef struct {
    float   t, per;                                // samples to its next clock; AUDF + 1 audio clocks in samples (0: at rest)
    uint8_t audc, audf, audv;
    uint8_t pulse, noise;                          // the 4-bit and the 5-bit shift registers
    uint8_t nb4, hold, nfb;                        // what the circuit latches between two clocks
    uint8_t out;                                   // what it puts out now: 0 or AUDV
} tia_t;

static float MIXT[31];                             // the two volumes on one output: 2 v / (30 + v)

static struct {
    tia_t   v[2];
    float   mix, hp_y;                             // the output's level now; the output
    float   buf[AUDIO_BLOCK + BL_TAPS];
    // the driver
    struct { uint8_t tv, rel, part; float k; int16_t note; uint8_t set; } d[2];
    int8_t  dr, dr_age;                            // the drum on voice 0 (0..4, -1: none) and its age
    float   drum_k;
    uint8_t kit;
} S;

// one clock of a voice's two shift registers: the 4-bit one's lowest bit is the output
static inline IRAM_ATTR void tia_clock(tia_t *v)
{
    int pfb;
    switch (v->audc >> 2) {
    case 0:  pfb = (((v->pulse >> 1) ^ v->pulse) & 1) && v->pulse != 0x0A && (v->audc & 3); break;
    case 1:  pfb = !(v->pulse & 8); break;
    case 2:  pfb = !v->nb4; break;
    default: pfb = !((v->pulse & 2) || !(v->pulse & 0x0E)); break;
    }
    v->noise = (uint8_t)((v->noise >> 1) | (v->nfb ? 0x10 : 0));
    if (!v->hold) v->pulse = (uint8_t)((~(v->pulse >> 1) & 7) | (pfb ? 8 : 0));
    // then the circuit sets up the next clock
    v->nb4 = v->noise & 1;
    v->hold = (v->audc & 3) == 2 ? (v->noise & 0x1E) != 0x02 : (v->audc & 3) == 3 ? !v->nb4 : 0;
    if (!(v->audc & 3)) v->nfb = ((v->pulse ^ v->noise) & 1) || !(v->noise || v->pulse != 0x0A) || !(v->audc & 0x0C);
    else v->nfb = (((v->noise >> 2) ^ v->noise) & 1) || !v->noise;
}

static IRAM_ATTR void render(float *out, int n)
{
    float *b = S.buf;
    float m = MIXT[S.v[0].out + S.v[1].out];       // what the driver wrote at the tick
    if (m != S.mix) { bl_step(b, 0.0f, m - S.mix); S.mix = m; }
    for (int i = 0; i < n; i++, b++) {
        for (int c = 0; c < 2; c++) {
            tia_t *v = &S.v[c];
            if (v->per <= 0.0f) continue;
            while (v->t < 1.0f) {
                tia_clock(v);
                uint8_t o = v->pulse & 1 ? v->audv : 0;
                if (o != v->out) { v->out = o; m = MIXT[S.v[0].out + S.v[1].out]; bl_step(b, v->t, m - S.mix); S.mix = m; }
                v->t += v->per;
            }
            v->t -= 1.0f;
        }
        float y = *b + 0.99608f * S.hp_y;          // the steps summed; the leak is a 20 Hz high pass
        S.hp_y = y;
        out[i] = clampf(y * 0.52f, -0.95f, 0.95f);
    }
    S.hp_y = flush_denormal(S.hp_y);
    memmove(S.buf, S.buf + n, sizeof(float) * BL_TAPS);
    memset(S.buf + BL_TAPS, 0, sizeof(float) * (size_t)n);
}

// ---------------------------------------------------------------- the driver
// The waveforms that play notes, and how many audio clocks one cycle takes at AUDF 0
enum { W_BUZZ4, W_SQ2, W_P31, W_BUZZ5, W_SQ6, W_P93, W_BUZZ15, W_N };
static const uint8_t W_AUDC[W_N] = { 1, 4, 6, 7, 12, 14, 15 };
static const uint8_t W_LEN[W_N] = { 15, 2, 31, 31, 6, 93, 93 };
// A set: the waveforms a sound may use, each with a handicap in cents (255: not in the set). A waveform with a
// handicap is taken only when it is that much nearer the note than the sound's own
enum { SET_PURE_HI, SET_BUZZ_HI, SET_PURE_LO, SET_BUZZ_LO, SET_REED_LO, SET_N };
static const uint8_t SET_PEN[SET_N][W_N] = {
    //  BUZZ4 SQ2  P31  BUZZ5 SQ6  P93  BUZZ15
    {   255,  0,   25,  255,  0,   255, 255 },     // the pure tones of the upper voice
    {   0,    45,  255, 25,   45,  255, 255 },     // the buzz first
    {   255,  255, 0,   255,  10,  0,   255 },     // the pure tones of the bass
    {   60,   255, 60,  0,    255, 255, 0   },     // the 5-bit buzz first
    {   0,    255, 50,  30,   255, 255, 255 },     // the 4-bit buzz first
};
#define NOTE_LO 12
#define NOTE_N  108
static uint8_t NOTE_REG[SET_N][NOTE_N];            // for each note 12..119: waveform << 5 | AUDF (made at init)

#define PV_N 10
typedef struct {
    uint8_t set;
    uint8_t vol[PV_N];         // the volume 0..15 by the note's age in ticks; the last one holds
    int8_t  pit[2];            // semitones added on the first two ticks
    uint8_t trem;              // 1: every other three ticks at half the volume
    uint8_t rel;               // after the note: volume steps down a tick
} tinst_t;
static const tinst_t LEAD[4] = {
    { SET_PURE_HI, { 13, 12, 12, 11, 11, 11, 10, 10, 10, 10 }, { 0, 0 }, 0, 4 },    // 0 the pure tone
    { SET_BUZZ_HI, { 13, 12, 11, 11, 10, 10, 10, 10, 10, 10 }, { 0, 0 }, 0, 4 },    // 1 buzz (where a note has one)
    { SET_PURE_HI, { 15, 13, 11, 9, 8, 7, 6, 6, 5, 5 },        { 12, 0 }, 0, 3 },   // 2 plucked, the first tick an octave up
    { SET_PURE_HI, { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 }, { 0, 0 }, 1, 4 },    // 3 tremolo
};
static const tinst_t CHORD[4] = {
    { SET_PURE_HI, { 10, 10, 9, 9, 8, 8, 8, 8, 8, 8 },         { 0, 0 }, 0, 4 },    // 0 the arpeggio, pure
    { SET_BUZZ_HI, { 10, 10, 9, 9, 8, 8, 8, 8, 8, 8 },         { 0, 0 }, 0, 4 },    // 1 buzz
    { SET_PURE_HI, { 12, 10, 8, 7, 6, 5, 5, 4, 4, 4 },         { 0, 0 }, 0, 3 },    // 2 plucked
    { SET_BUZZ_HI, { 12, 10, 8, 7, 6, 5, 5, 4, 4, 4 },         { 0, 0 }, 0, 3 },    // 3 buzz, plucked
};
static const tinst_t BASS[4] = {
    { SET_REED_LO, { 15, 14, 13, 13, 12, 12, 12, 12, 12, 12 }, { 0, 0 }, 0, 5 },    // 0 the 4-bit buzz: the best in tune from C2 to B2
    { SET_PURE_LO, { 15, 14, 13, 13, 12, 12, 12, 12, 12, 12 }, { 0, 0 }, 0, 5 },    // 1 the low pulse: rounder, and further off
    { SET_BUZZ_LO, { 15, 14, 13, 13, 12, 12, 12, 12, 12, 12 }, { 0, 0 }, 0, 5 },    // 2 the 5-bit buzz: rougher
    { SET_REED_LO, { 15, 13, 11, 9, 8, 7, 6, 5, 5, 4 },        { 12, 0 }, 0, 3 },   // 3 plucked, the first tick an octave up
};

// the drums on voice 0, by the tick: AUDC, AUDF, AUDV (0 ends it)
typedef struct { uint8_t c[8], f[8], v[8]; } drum_t;
static const drum_t DRUM[2][PX_DRUMS] = {
    {   // kit 0
        { { 6, 6, 6 },                  { 5, 12, 24 },                  { 15, 15, 13 } },                   // kick: the low pulse falls (three ticks: the bass is under it)
        { { 8, 8, 8, 8, 8 },            { 7, 5, 4, 4, 4 },              { 15, 12, 9, 6, 3 } },              // snare: noise
        { { 8 },                        { 0 },                          { 9 } },                            // hat: one tick of the highest noise
        { { 8, 8, 8, 8, 8, 8, 8 },      { 1, 1, 1, 1, 1, 1, 1 },        { 10, 9, 8, 7, 5, 4, 2 } },         // open hat
        { { 6, 6, 6, 6, 6, 6 },         { 3, 4, 5, 6, 7, 8 },           { 15, 14, 13, 12, 10, 8 } },        // tom
    },
    {   // kit 1: buzz and falling noise
        { { 15, 15, 15, 15 },           { 1, 4, 9, 15 },                { 15, 15, 14, 12 } },
        { { 8, 8, 8, 8, 8 },            { 3, 6, 10, 14, 18 },           { 15, 13, 10, 7, 4 } },
        { { 8, 8 },                     { 2, 2 },                       { 8, 3 } },
        { { 8, 8, 8, 8, 8, 8, 8 },      { 2, 2, 2, 2, 2, 2, 2 },        { 10, 9, 8, 7, 5, 4, 2 } },
        { { 7, 7, 7, 7, 7, 7 },         { 3, 4, 5, 6, 7, 8 },           { 15, 14, 13, 12, 10, 8 } },
    },
};

static inline int vscale(int tv, float k) { int v = (int)((float)tv * k + 0.5f); return v > 15 ? 15 : v; }

// the three registers of a voice. A bad contact flips a bit
static void tia_write(int c, int audc, int audf, int audv, float gc, uint32_t *rng)
{
    tia_t *v = &S.v[c];
    v->audc = (uint8_t)px_glitch((uint32_t)audc, 4, gc, rng);
    v->audf = (uint8_t)px_glitch((uint32_t)audf, 5, gc, rng);
    v->audv = (uint8_t)px_glitch((uint32_t)audv, 4, gc, rng);
    v->per = v->audv ? (float)(v->audf + 1) * SPC : 0.0f;
    if (!(v->audc & 3) && !(v->audc & 0x0C)) v->pulse |= 1;        // AUDC 0: a steady level (nothing needs clocking to hear it)
    v->out = v->pulse & 1 ? v->audv : 0;
}

// a melodic voice: its instrument's volume by age (or the release), the note through the set's table
static void voice_play(int c, const tinst_t *in, bool on, int age, float note, float k, float gc, uint32_t *rng)
{
    if (on) {
        int tv = in->vol[age < PV_N ? age : PV_N - 1];
        if (in->trem && ((age / 3) & 1)) tv /= 2;
        S.d[c].tv = (uint8_t)tv; S.d[c].k = k; S.d[c].rel = in->rel; S.d[c].set = in->set;
        S.d[c].note = (int16_t)clampi((int)(note + 0.5f) + (age < 2 ? in->pit[age] : 0), NOTE_LO, NOTE_LO + NOTE_N - 1);
    } else {
        S.d[c].tv = S.d[c].tv > S.d[c].rel ? (uint8_t)(S.d[c].tv - S.d[c].rel) : 0;
        if (!S.d[c].tv) S.d[c].part = 0xFF;
    }
    uint8_t r = NOTE_REG[S.d[c].set][S.d[c].note - NOTE_LO];
    tia_write(c, W_AUDC[r >> 5], r & 31, vscale(S.d[c].tv, S.d[c].k), gc, rng);
}

static void tick(const px_frame_t *f)
{
    uint32_t rng = f->rng | 1u;
    float gc = f->glitch * (0.05f + 0.25f * f->glitch);            // the chance that a write gets a bit wrong

    // voice 1: the LEAD; the CHORDS' arpeggio between its notes, and on every third tick once a note is a step old
    bool lead = f->lead.on && !(f->chord.on && f->lead.age >= f->tps && f->lead.age % 3 == 2);
    if (lead) {
        S.d[1].part = PART_LEAD;
        voice_play(1, &LEAD[f->lead.sound & 3], true, f->lead.age, f->lead.note, f->lead.level * (f->lead.accent ? 1.0f : 0.8f), gc, &rng);
    } else if (f->chord.on) {
        S.d[1].part = PART_CHORDS;
        voice_play(1, &CHORD[f->chord.sound & 3], true, f->chord.age, f->chord.note, f->chord.level, gc, &rng);
    } else voice_play(1, &LEAD[0], false, 0, 0.0f, 0.0f, gc, &rng);

    // voice 0: a drum, else the BASS (its shape goes on under the drum)
    if (f->drums && f->drum_level > 0.0f) {
        S.kit = f->kit & 1;                                        // kits 2 and 3 are 0 and 1 again
        S.drum_k = f->drum_level * (f->drum_accent ? 1.0f : 0.8f);
        S.dr = f->drums & DR_SNARE ? 1 : f->drums & DR_KICK ? 0 : f->drums & DR_TOM ? 4 : f->drums & DR_OPEN ? 3 : 2;
        S.dr_age = 0;
    }
    const drum_t *d = S.dr >= 0 ? &DRUM[S.kit][S.dr] : NULL;
    if (d && S.dr_age < 8 && d->v[S.dr_age]) {
        int a = S.dr_age++;
        tia_write(0, d->c[a], d->f[a], vscale(d->v[a], S.drum_k), gc, &rng);
        S.d[0].part = PART_DRUMS;
        if (!f->bass.on) S.d[0].tv = 0;
    } else {
        S.dr = -1;
        if (f->bass.on) S.d[0].part = PART_BASS;
        else if (S.d[0].part == PART_DRUMS) { S.d[0].part = 0xFF; S.d[0].tv = 0; }
        voice_play(0, &BASS[f->bass.sound & 3], f->bass.on, f->bass.age, f->bass.note, f->bass.level * (f->bass.accent ? 1.0f : 0.8f), gc, &rng);
    }
}

// ---------------------------------------------------------------- boot
static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.dr = -1;
    S.d[0].part = S.d[1].part = 0xFF;
    S.d[0].note = S.d[1].note = 60;
}

static void init(void)
{
    bl_init();
    for (int v = 0; v < 31; v++) MIXT[v] = 2.0f * (float)v / (30.0f + (float)v);
    // every note's nearest (waveform, AUDF) in each set. A pair's pitch as a (fractional) note is base[w] - lg[AUDF]
    float lg[32], base[W_N];
    for (int a = 0; a < 32; a++) lg[a] = 12.0f * log2f((float)(a + 1));
    for (int w = 0; w < W_N; w++) base[w] = 69.0f + 12.0f * log2f(TIA_HZ / ((float)W_LEN[w] * 440.0f));
    for (int s = 0; s < SET_N; s++) for (int n = 0; n < NOTE_N; n++) {
        float best = 1e9f;
        for (int w = 0; w < W_N; w++) {
            if (SET_PEN[s][w] == 255) continue;
            for (int a = 0; a < 32; a++) {
                float e = fabsf(base[w] - lg[a] - (float)(n + NOTE_LO)) * 100.0f + (float)SET_PEN[s][w];
                if (e < best) { best = e; NOTE_REG[s][n] = (uint8_t)(w << 5 | a); }
            }
        }
    }
    reset();
}

static int view(px_vview_t *v, int max)
{
    if (max < 2) return 0;
    for (int c = 0; c < 2; c++) {
        const tia_t *t = &S.v[c];
        static const uint8_t len[16] = { 0, 15, 0, 0, 2, 2, 31, 31, 0, 31, 31, 0, 6, 6, 93, 93 };       // audio clocks a cycle; 0: no pitch
        static const uint8_t kind[16] = { VW_OFF, VW_BUZZ, VW_NOISE, VW_NOISE, VW_PULSE, VW_PULSE, VW_PULSE, VW_BUZZ, VW_NOISE, VW_BUZZ, VW_PULSE, VW_OFF, VW_PULSE, VW_PULSE, VW_PULSE, VW_BUZZ };
        bool on = t->audv && kind[t->audc] != VW_OFF;
        v[c] = (px_vview_t){ on ? kind[t->audc] : VW_OFF, on ? S.d[c].part : 0xFF, len[t->audc] ? TIA_HZ / (float)(len[t->audc] * (t->audf + 1)) : 0.0f,
                             (float)t->audv * (1.0f / 15.0f), t->audc == 6 || t->audc == 10 ? 0.58f : 0.5f };
    }
    return 2;
}

// the menu's names: the four LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four KITs
static const char *const NAMES[16] = { "PURE", "BUZZ", "PLUCK", "TREMOLO",
                                       "BUZZ", "LOW", "GROWL", "PLUCK",
                                       "PURE", "BUZZ", "PLUCK", "BUZZ PLK",
                                       "NOISE", "BUZZ", "NOISE", "BUZZ" };
const px_console_t con_atari = { "ATARI", "TIA", "1977", 2, init, reset, tick, render, view, NAMES };
