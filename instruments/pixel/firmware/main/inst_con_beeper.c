// PIXEL console: the BEEPER (ZX Spectrum 48K, 1982: one bit on a little speaker).
//
//   channel 1   BASS
//   channel 2   LEAD, with the CHORDS' arpeggio between its notes and on every third tick of a long note
//   the DRUMS stop both for as long as they last (4 to 50 ms): that stutter is the beeper's sound
//   no ECHO: there is no voice for it
//
// There is no chip: the Z80 flips one bit, and all it can do is flip it at the right moments. This is a two-channel
// pulse engine of the kind the Spectrum's games used: a loop that takes 64 T-states a turn (3.5 MHz / 64 = 54687.5
// turns a second) and counts two periods down in turns. When a counter runs out its channel puts the speaker high for
// a short pulse; the speaker's bit is the two pulses OR'ed. So:
//   pitch    f = 54687.5 / N, N a whole number of turns: fine for the bass, up to 30 cents off above 2 kHz
//   volume   none. A thin pulse is quiet and bright, a wide one loud: a note's shape, its accent and the part's LEVEL
//            are all the pulse's width (in eighths of a turn: 2.3 microseconds)
//   two notes at once are only clean while their pulses are thin: wide ones run into each other
//   drums    the engine leaves its loop and plays the drum: a pulse train that falls for the kick and the tom, random
//            pulses for the snare and the hats (how often they come is their loudness and how it dies away)
// Every flip of the bit is put down as a band-limited step at its exact time (bl_step), then the speaker: a gentle
// low pass (5 kHz) and a 20 Hz high pass.
#include "pixel.h"
#include <string.h>

#define CPU_HZ   3500000.0f                        // the 48K's Z80
#define LOOP_HZ  (CPU_HZ / 64.0f)                  // the engine's loop: 64 T-states a turn
#define TURN     ((float)AUDIO_SR / LOOP_HZ)       // a turn in our samples (0.585)
#define AMP      0.47f                             // the speaker's swing

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
    const float beta = 6.0f, fc = 0.42f;           // the window; the cutoff in cycles per sample (low: thin pulses have much up there)
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

// ---------------------------------------------------------------- the engine
typedef struct {
    uint16_t n;                // the period in turns (0: silent)
    int32_t  cnt;              // turns to its next pulse
    float    w, pw;            // the pulse's width in turns; what is left of the pulse now
} bch_t;

// a drum: a pulse train that falls (tone_len turns: the half cycle starts at h0 turns and grows by `fall` each flip),
// then random pulses (noise_len turns: a new one every `step` turns, high `dens` of the time at first, dying away)
typedef struct { uint16_t tone_len; float h0, fall; uint16_t noise_len; uint8_t step; float dens; } bdrum_t;
#define MS(x) ((uint16_t)((x) * 54.6875f))         // milliseconds in turns
static const bdrum_t DRUM[2][PX_DRUMS] = {
    {   // kit 0
        { MS(40), 90.0f, 1.13f, 0, 1, 0.0f },              // kick: 300 Hz falling to 75
        { MS(3), 55.0f, 1.0f, MS(26), 2, 0.5f },           // snare: a tick, then noise
        { 0, 0.0f, 1.0f, MS(5), 1, 0.5f },                 // hat: 5 ms of noise
        { 0, 0.0f, 1.0f, MS(32), 1, 0.4f },                // open hat
        { MS(50), 62.0f, 1.035f, 0, 1, 0.0f },             // tom: 440 Hz falling to 190
    },
    {   // kit 1: darker, longer
        { MS(48), 120.0f, 1.16f, MS(3), 3, 0.4f },         // kick: lower, with a tail of noise
        { 0, 0.0f, 1.0f, MS(36), 4, 0.5f },                // snare: all noise, coarser
        { 0, 0.0f, 1.0f, MS(4), 2, 0.6f },                 // hat
        { 0, 0.0f, 1.0f, MS(40), 2, 0.35f },               // open hat
        { MS(50), 95.0f, 1.05f, 0, 1, 0.0f },              // tom: 290 Hz falling to 110
    },
};

static struct {
    bch_t    ch[2];
    float    t;                // where the engine's next turn starts, in samples from the current one
    uint8_t  bit;              // the speaker's bit
    // the drum that plays now (dr_tone + dr_noise turns left)
    int32_t  dr_tone, dr_noise, dr_cnt, dr_noise_len;
    float    dr_h, dr_fall, dr_duty, dr_dens;
    uint8_t  dr_high, dr_step;
    uint32_t rng;
    float    hp_y, lp;
    float    buf[AUDIO_BLOCK + BL_TAPS + 1];
    // the driver
    struct { float w, note; uint8_t part, rel; } d[2];         // rel: the width is a share of the period
    uint8_t  dr_part;
} S;

// one turn of the loop while a drum plays: how much of the turn the speaker is high
static inline IRAM_ATTR float drum_turn(void)
{
    if (S.dr_tone > 0) {
        S.dr_tone--;
        if (--S.dr_cnt <= 0) {
            S.dr_high ^= 1;
            if (S.dr_high) S.dr_h *= S.dr_fall;
            S.dr_cnt = (int32_t)(S.dr_h * (S.dr_high ? S.dr_duty : 2.0f - S.dr_duty));    // LEVEL: the high part's share
            if (S.dr_cnt < 1) S.dr_cnt = 1;
        }
        if (!S.dr_tone) { S.dr_cnt = 0; S.dr_high = 0; }
        return S.dr_tone ? (float)S.dr_high : 0.0f;
    }
    S.dr_noise--;
    if (--S.dr_cnt <= 0) {
        S.dr_cnt = S.dr_step;
        // the pulses thin out as the drum dies away
        float dens = S.dr_dens * (float)S.dr_noise / (float)S.dr_noise_len;
        S.dr_high = (float)(rng_next(&S.rng) >> 8) * (1.0f / 16777216.0f) < dens;
    }
    return S.dr_noise ? (float)S.dr_high : 0.0f;
}

static inline void edge(float *b, float t, float d) { if (t >= 1.0f) bl_step(b + 1, t - 1.0f, d); else bl_step(b, t, d); }

static IRAM_ATTR void render(float *out, int n)
{
    float *b = S.buf;
    for (int i = 0; i < n; i++, b++) {
        while (S.t < 1.0f) {
            float hi = 0.0f;                       // how much of this turn the speaker is high, from its start
            if (S.dr_tone + S.dr_noise > 0) hi = drum_turn();
            else for (int c = 0; c < 2; c++) {
                bch_t *v = &S.ch[c];
                if (!v->n) { v->pw = 0.0f; continue; }
                if (--v->cnt <= 0) { v->cnt = v->n; v->pw = v->w; }
                if (v->pw > 0.0f) { float h = v->pw < 1.0f ? v->pw : 1.0f; if (h > hi) hi = h; v->pw -= 1.0f; }
            }
            if (hi > 0.0f) {
                if (!S.bit) edge(b, S.t, AMP);
                S.bit = hi >= 1.0f;
                if (!S.bit) edge(b, S.t + hi * TURN, -AMP);
            } else if (S.bit) { edge(b, S.t, -AMP); S.bit = 0; }
            S.t += TURN;
        }
        S.t -= 1.0f;
        float y = *b + 0.99608f * S.hp_y;          // the steps summed; the leak is a 20 Hz high pass
        S.hp_y = y;
        S.lp += 0.6254f * (y - S.lp);              // the speaker: a low pass at 5 kHz
        out[i] = clampf(S.lp, -0.95f, 0.95f);
    }
    S.hp_y = flush_denormal(S.hp_y); S.lp = flush_denormal(S.lp);
    memmove(S.buf, S.buf + n, sizeof(float) * (BL_TAPS + 1));
    memset(S.buf + BL_TAPS + 1, 0, sizeof(float) * (size_t)n);
}

// ---------------------------------------------------------------- the driver
// An instrument is the pulse's width by the note's age in ticks. `rel` 0: in eighths of a turn (2.3 us: the thin
// pulse, the same at every pitch, so low notes are thinner in sound). `rel` 1: in 1/256 of the note's period (a pulse
// wave: the same sound at every pitch)
#define PV_N 10
typedef struct {
    uint8_t rel;
    uint8_t w[PV_N];           // the width by age; from `loop` to PV_N it goes round (loop = PV_N - 1: the last one holds)
    uint8_t loop;
    int8_t  pit[2];            // semitones added on the first two ticks
    uint8_t oct;               // 1: every other tick an octave up
} binst_t;
static const binst_t LEAD[4] = {
    { 0, { 60, 52, 46, 42, 38, 36, 34, 32, 32, 32 }, 9, { 0, 0 }, 0 },                // 0 the thin pulse: 140 us closing to 70
    { 1, { 30, 28, 26, 24, 22, 20, 20, 18, 18, 18 }, 9, { 0, 0 }, 0 },                // 1 a pulse wave: 12 % closing to 7 %
    { 0, { 90, 60, 44, 34, 28, 24, 22, 20, 20, 20 }, 9, { 12, 0 }, 0 },               // 2 plucked: wide to very thin, an octave blip
    { 1, { 6, 10, 16, 22, 28, 32, 28, 22, 16, 10 }, 0, { 0, 0 }, 0 },                 // 3 the width going round (a slow sweep)
};
static const binst_t CHORD[4] = {
    { 0, { 70, 64, 58, 54, 50, 48, 46, 44, 44, 44 }, 9, { 0, 0 }, 0 },                // 0 the arpeggio, thin
    { 1, { 12, 11, 10, 10, 9, 9, 9, 9, 9, 9 }, 9, { 0, 0 }, 0 },                      // 1 a pulse wave
    { 0, { 110, 80, 60, 46, 38, 32, 28, 26, 24, 24 }, 9, { 0, 0 }, 0 },               // 2 plucked
    { 1, { 4, 6, 9, 12, 15, 17, 15, 12, 9, 6 }, 0, { 0, 0 }, 0 },                     // 3 the width going round
};
static const binst_t BASS[4] = {
    { 0, { 250, 220, 200, 190, 180, 176, 170, 170, 170, 170 }, 9, { 0, 0 }, 0 },      // 0 the buzz: 570 us closing to 390
    { 1, { 26, 24, 22, 21, 20, 19, 18, 18, 18, 18 }, 9, { 0, 0 }, 0 },                // 1 fat: a pulse wave, 10 % closing to 7 %
    { 0, { 255, 210, 170, 136, 110, 90, 76, 66, 60, 60 }, 9, { 0, 0 }, 0 },            // 2 plucked
    { 0, { 250, 220, 200, 190, 180, 176, 170, 170, 170, 170 }, 9, { 0, 0 }, 1 },      // 3 octaves: the note and its octave, a tick each
};

// a channel's period and width from a note; a bad contact flips a bit of either
static void ch_write(int c, float note, float w8, bool rel, float gc, uint32_t *rng)
{
    bch_t *v = &S.ch[c];
    int n = clampi((int)(LOOP_HZ / px_hz(note) + 0.5f), 2, 4095);
    n = (int)px_glitch((uint32_t)n, 11, gc, rng);
    float w = rel ? w8 * (1.0f / 256.0f) * (float)n : w8 * 0.125f;
    w = (float)px_glitch((uint32_t)(w * 8.0f), 8, gc, rng) * 0.125f;
    if (w > 0.5f * (float)n) w = 0.5f * (float)n;                  // at most a square
    if (n < 2 || w <= 0.0f) { v->n = 0; return; }
    v->n = (uint16_t)n; v->w = w;
    if (v->cnt > n) v->cnt = n;
}

static void voice_play(int c, const binst_t *in, bool on, int age, float note, float k, float gc, uint32_t *rng)
{
    if (on) {
        int a = age < PV_N ? age : in->loop + (age - in->loop) % (PV_N - in->loop);
        S.d[c].w = (float)in->w[a] * k;
        S.d[c].note = note + (age < 2 ? (float)in->pit[age] : 0.0f) + (in->oct && (age & 1) ? 12.0f : 0.0f);
        S.d[c].rel = in->rel;
    } else {
        S.d[c].w *= 0.5f;                                          // the note is over: thinner and gone in a few ticks
        if (S.d[c].w < (S.d[c].rel ? 2.0f : 6.0f)) { S.d[c].w = 0.0f; S.d[c].part = 0xFF; }
    }
    ch_write(c, S.d[c].note, S.d[c].w, S.d[c].rel, gc, rng);
}

static void tick(const px_frame_t *f)
{
    uint32_t rng = f->rng | 1u;
    float gc = f->glitch * (0.05f + 0.25f * f->glitch);            // the chance that a write gets a bit wrong

    // channel 1: the BASS
    if (f->bass.on) S.d[0].part = PART_BASS;
    voice_play(0, &BASS[f->bass.sound & 3], f->bass.on, f->bass.age, f->bass.note, f->bass.level * (f->bass.accent ? 1.3f : 1.0f), gc, &rng);

    // channel 2: the LEAD; the CHORDS' arpeggio between its notes, and on every third tick once a note is a step old
    bool lead = f->lead.on && !(f->chord.on && f->lead.age >= f->tps && f->lead.age % 3 == 2);
    if (lead) {
        S.d[1].part = PART_LEAD;
        voice_play(1, &LEAD[f->lead.sound & 3], true, f->lead.age, f->lead.note, f->lead.level * (f->lead.accent ? 1.3f : 1.0f), gc, &rng);
    } else if (f->chord.on) {
        S.d[1].part = PART_CHORDS;
        voice_play(1, &CHORD[f->chord.sound & 3], true, f->chord.age, f->chord.note, f->chord.level, gc, &rng);
    } else voice_play(1, &LEAD[0], false, 0, 0.0f, 0.0f, gc, &rng);

    // the DRUMS: one at a time, and nothing else meanwhile
    if (f->drums && f->drum_level > 0.0f) {
        int d = f->drums & DR_SNARE ? 1 : f->drums & DR_KICK ? 0 : f->drums & DR_TOM ? 4 : f->drums & DR_OPEN ? 3 : 2;
        const bdrum_t *dr = &DRUM[f->kit & 1][d];                  // kits 2 and 3 are 0 and 1 again
        float k = clampf(f->drum_level * (f->drum_accent ? 1.0f : 0.75f), 0.0f, 1.0f);
        S.dr_tone = (int32_t)px_glitch(dr->tone_len, 11, gc, &rng);
        S.dr_noise = S.dr_noise_len = dr->noise_len;
        S.dr_h = dr->h0 / dr->fall; S.dr_fall = dr->fall; S.dr_duty = k;
        S.dr_step = (uint8_t)px_glitch(dr->step, 3, gc, &rng); if (!S.dr_step) S.dr_step = 1;
        S.dr_dens = dr->dens * k;
        S.dr_cnt = 0; S.dr_high = 0;
        S.dr_part = (uint8_t)d;
    }
}

// ---------------------------------------------------------------- boot
static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.rng = 0x1982CAFEu;
    S.d[0].part = S.d[1].part = 0xFF;
    S.d[0].note = S.d[1].note = 60.0f;
    S.dr_noise_len = 1;
}

static void init(void) { bl_init(); reset(); }

static int view(px_vview_t *v, int max)
{
    if (max < 2) return 0;
    bool drum = S.dr_tone + S.dr_noise > 0;
    for (int c = 0; c < 2; c++) {
        const bch_t *b = &S.ch[c];
        bool on = b->n && !drum;
        v[c] = (px_vview_t){ on ? VW_PULSE : VW_OFF, on ? S.d[c].part : 0xFF, b->n ? LOOP_HZ / (float)b->n : 0.0f, on ? clampf(b->w * 0.06f, 0.0f, 1.0f) : 0.0f, b->n ? b->w / (float)b->n : 0.0f };
    }
    if (drum) v[0] = (px_vview_t){ S.dr_tone > 0 ? VW_PULSE : VW_NOISE, PART_DRUMS, S.dr_tone > 0 ? LOOP_HZ / (2.0f * S.dr_h) : 0.0f, S.dr_duty, 0.5f * S.dr_duty };
    return 2;
}

// the menu's names: the four LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four KITs
static const char *const NAMES[16] = { "THIN", "PULSE", "PLUCK", "SWEEP",
                                       "BUZZ", "FAT", "PLUCK", "OCTAVES",
                                       "THIN", "PULSE", "PLUCK", "SWEEP",
                                       "CLICK", "DARK", "CLICK", "DARK" };
const px_console_t con_beeper = { "BEEPER", "1 BIT", "1982", 2, init, reset, tick, render, view, NAMES };
