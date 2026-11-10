// PIXEL console: the NES (Ricoh 2A03, 1983: the Famicom's sound).
//
//   P1        LEAD     a pulse: duty 12.5, 25, 50 or 75 %, 4-bit volume, 11-bit period
//   P2        CHORDS   the arpeggio's note; the lead's ECHO when no chord sounds
//   TRIANGLE  BASS     the 32-step staircase, no volume of its own; a KICK or a TOM takes it for a few ticks
//   NOISE     DRUMS    snare and hats (15-bit shift register, 16 periods; the short mode in kits 2 and 3)
//   DMC       DRUMS    kits 1 and 3: kick, snare and tom as 1-bit delta samples, made and encoded at init
//
// The chip half runs the 2A03's own counters in CPU cycles (1789773 a second, 55.9 to one of our samples). Every
// change of the output is put down as a band-limited step at its exact time (bl_step), so nothing aliases. The numbers
// are the nesdev wiki's (APU Pulse, Triangle, Noise, DMC, Mixer):
//   pulse      f = CPU / (16 (t + 1)), t 11 bits; silent for t < 8; the four duty sequences
//   triangle   f = CPU / (32 (t + 1)); 15..0, 0..15; when it stops it holds its step
//   noise      15-bit register, feedback bit 0 xor bit 1 (bit 6 in the short mode: a 93-step buzz), 16 periods
//   DMC        1 bit a step, the 7-bit level goes up or down by 2, 16 rates; samples are 16 n + 1 bytes
//   mixer      the non-linear one, by the two lookup tables (pulses; triangle, noise and DMC): a loud DMC level
//              turns the triangle and the noise down, as on the chip
//   output     the Famicom's high pass (37 Hz, first order). Our band limit stands in for its 14 kHz low pass
// Not modelled: the chip's envelope, length and sweep units and the frame counter (the driver writes volumes every
// tick, as FamiTracker's instruments do), the phase reset when the period's high bits are written during a note.
//
// The driver half is a small FamiTracker: an instrument is a volume table and a duty table by the note's age in
// ticks, a pitch table for its first ticks and a release speed.
//
// Cheats, all three so that a LEVEL does something: the triangle and the DMC have no volume on the chip. BASS > LEVEL
// scales the triangle about its middle, DRUMS > LEVEL scales the triangle kick and the DMC's samples.
#include "pixel.h"
#include <string.h>

#define CPU_HZ  1789773.0f                         // the 2A03's clock (NTSC)
#define SPC     ((float)AUDIO_SR / CPU_HZ)         // our samples per CPU cycle

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
static const uint8_t  DUTY[4] DRAM_ATTR = { 0x02, 0x06, 0x1E, 0xF9 };      // bit s: the output at sequencer step s
static const uint16_t NOISE_PER[16] DRAM_ATTR = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };   // CPU cycles a shift (NTSC)
static const uint16_t DMC_PER[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };              // CPU cycles a bit (NTSC)
static float PT[31];                               // the mixer: 95.52 / (8128 / n + 100), n = P1 + P2
static float TT[204];                              // 163.67 / (24329 / n + 100), n = 3 triangle + 2 noise + DMC

typedef struct {
    float    t, per;                               // samples to the sequencer's next step; a step in samples (0: silent)
    uint16_t reg;                                  // the 11-bit period
    uint8_t  seq, duty, vol, out;                  // the step 0..7, the duty 0..3, the volume, what it puts out now
} pulse_t;

// the driver's side of a pulse: where its instrument is
typedef struct { uint8_t tv, rel, duty, part; float k, note; } pdrv_t;

static struct {
    pulse_t  p[2];
    float    tri_t, tri_per, tri_k;                // tri_k: 1.5 times the triangle's level (the cheat; 1.5 = all of it)
    uint16_t tri_reg;
    uint8_t  tri_seq, tri_on;
    int      tri_val;                              // 0..15
    float    nz_t, nz_per, nz_lvl;                 // nz_lvl: what the noise puts out now, 0..15 (a fast noise: its mean)
    uint16_t lfsr;
    uint8_t  nz_vol, nz_short, nz_idx;
    float    dm_t, dm_per, dm_gain;
    const uint8_t *dm_ptr;
    uint16_t dm_left;
    uint8_t  dm_sr, dm_bits, dm_on, dm_lvl;        // the level 0..127
    float    mix, hp_y;                            // the mixer's output now; the output
    float    buf[AUDIO_BLOCK + BL_TAPS];
    // the driver
    pdrv_t   d[2];
    uint8_t  bass_on;                              // the triangle plays the bass now (for view)
    int8_t   nd, nd_age;                           // the drum on the noise channel (ND_*, -1: none) and its age
    int8_t   td, td_age;                           // the drum that has the triangle (0 kick, 1 tom, -1: none)
    uint8_t  td_len;
    float    drum_k;
    uint8_t  kit;
} S;

static uint8_t *dmc_mem;                           // the three samples, app_alloc_fast() in init()
enum { DM_KICK, DM_SNARE, DM_TOM, DM_N };
static const uint16_t DM_BYTES[DM_N] = { 465, 577, 641 };     // 16 n + 1 bytes each, as the chip counts them
static const uint16_t DM_OFF[DM_N] = { 0, 465, 1042 };
#define DM_TOTAL 1683

static inline IRAM_ATTR float mix_now(void)
{
    // the triangle about its middle (7.5), so its level (the cheat) moves no DC
    float x = 22.5f + (float)(2 * S.tri_val - 15) * S.tri_k + 2.0f * S.nz_lvl + (float)S.dm_lvl * S.dm_gain;
    int   i = (int)x;
    float fr = x - (float)i;
    return PT[S.p[0].out + S.p[1].out] + TT[i] + fr * (TT[i + 1] - TT[i]);
}

static IRAM_ATTR void render(float *out, int n)
{
    float *b = S.buf;
    float m = mix_now();                           // what the driver wrote at the tick
    if (m != S.mix) { bl_step(b, 0.0f, m - S.mix); S.mix = m; }
    for (int i = 0; i < n; i++, b++) {
        for (int c = 0; c < 2; c++) {
            pulse_t *p = &S.p[c];
            if (p->per <= 0.0f) continue;
            while (p->t < 1.0f) {
                p->seq = (p->seq + 1) & 7;
                uint8_t o = (DUTY[p->duty] >> p->seq) & 1 ? p->vol : 0;
                if (o != p->out) { p->out = o; m = mix_now(); bl_step(b, p->t, m - S.mix); S.mix = m; }
                p->t += p->per;
            }
            p->t -= 1.0f;
        }
        if (S.tri_on) {
            while (S.tri_t < 1.0f) {
                S.tri_seq = (S.tri_seq + 1) & 31;
                S.tri_val = S.tri_seq < 16 ? 15 - S.tri_seq : S.tri_seq - 16;
                m = mix_now(); bl_step(b, S.tri_t, m - S.mix); S.mix = m;
                S.tri_t += S.tri_per;
            }
            S.tri_t -= 1.0f;
        }
        if (S.nz_vol) {
            uint32_t l = S.lfsr;
            int      sh = S.nz_short ? 6 : 1;
            if (S.nz_per < 1.0f) {                 // several shifts a sample: the mean of the bit over the sample
                float t = S.nz_t, last = 0.0f, acc = 0.0f;
                while (t < 1.0f) {
                    if (!(l & 1u)) acc += t - last;
                    last = t;
                    l = (l >> 1) | (((l ^ (l >> sh)) & 1u) << 14);
                    t += S.nz_per;
                }
                if (!(l & 1u)) acc += 1.0f - last;
                S.nz_t = t - 1.0f;
                S.nz_lvl = acc * (float)S.nz_vol;
                m = mix_now(); *b += m - S.mix; S.mix = m;
            } else {
                while (S.nz_t < 1.0f) {
                    l = (l >> 1) | (((l ^ (l >> sh)) & 1u) << 14);
                    float o = l & 1u ? 0.0f : (float)S.nz_vol;
                    if (o != S.nz_lvl) { S.nz_lvl = o; m = mix_now(); bl_step(b, S.nz_t, m - S.mix); S.mix = m; }
                    S.nz_t += S.nz_per;
                }
                S.nz_t -= 1.0f;
            }
            S.lfsr = (uint16_t)l;
        }
        if (S.dm_on) {
            while (S.dm_t < 1.0f) {
                if (!S.dm_bits) {
                    if (!S.dm_left) { S.dm_on = 0; S.dm_t = 1.0f; break; }
                    S.dm_sr = *S.dm_ptr++; S.dm_left--; S.dm_bits = 8;
                }
                if (S.dm_sr & 1) { if (S.dm_lvl <= 125) S.dm_lvl += 2; } else if (S.dm_lvl >= 2) S.dm_lvl -= 2;
                S.dm_sr >>= 1; S.dm_bits--;
                m = mix_now();
                if (m != S.mix) { bl_step(b, S.dm_t, m - S.mix); S.mix = m; }
                S.dm_t += S.dm_per;
            }
            S.dm_t -= 1.0f;
        }
        float y = *b + 0.99276f * S.hp_y;          // the steps summed; the leak is the Famicom's 37 Hz high pass
        S.hp_y = y;
        out[i] = clampf(y * 1.7f, -0.95f, 0.95f);
    }
    S.hp_y = flush_denormal(S.hp_y);
    memmove(S.buf, S.buf + n, sizeof(float) * BL_TAPS);
    memset(S.buf + BL_TAPS, 0, sizeof(float) * (size_t)n);
}

// ---------------------------------------------------------------- the driver
#define PV_N 10
typedef struct {
    uint8_t vol[PV_N];         // the volume 0..15 by the note's age in ticks; the last one holds
    uint8_t duty[8];           // the duty by age; from `dloop` to `dn` it goes round
    uint8_t dn, dloop;
    int8_t  pit[2];            // semitones added on the first two ticks
    uint8_t rel;               // after the note: volume steps down a tick
} pinst_t;

static const pinst_t LEAD[4] = {
    { { 15, 14, 13, 12, 12, 11, 11, 10, 10, 10 }, { 0, 1, 2 }, 3, 2, { 0, 0 }, 3 },                  // 0 the square lead: a thin first tick, then 50 %
    { { 11, 12, 12, 11, 11, 10, 10, 10, 9, 9 },   { 1 }, 1, 0, { 0, 0 }, 2 },                        // 1 hollow: 25 %
    { { 15, 13, 11, 10, 9, 8, 8, 7, 7, 7 },       { 0 }, 1, 0, { 12, 0 }, 2 },                       // 2 thin pluck: 12.5 %, an octave blip
    { { 9, 11, 12, 13, 13, 12, 12, 12, 12, 12 },  { 0, 0, 1, 1, 2, 2, 1, 1 }, 8, 0, { 0, 0 }, 2 },   // 3 the duty going round
};
static const pinst_t CHORD[4] = {
    { { 10, 10, 9, 9, 8, 8, 8, 7, 7, 7 },         { 1 }, 1, 0, { 0, 0 }, 3 },                        // 0 the arpeggio: 25 %
    { { 5, 6, 7, 8, 8, 8, 8, 8, 8, 8 },           { 2 }, 1, 0, { 0, 0 }, 1 },                        // 1 soft: 50 %, a slow start
    { { 13, 11, 9, 8, 7, 6, 6, 5, 5, 5 },         { 0 }, 1, 0, { 0, 0 }, 2 },                        // 2 plucked: 12.5 %
    { { 9, 9, 9, 9, 8, 8, 8, 8, 8, 8 },           { 2, 1, 0, 1 }, 4, 0, { 0, 0 }, 2 },               // 3 the duty going round
};

// the triangle has no volume and no shape: its four sounds are what the pitch does
typedef struct { int8_t pit[4]; uint8_t pn, ploop; float vib; } tinst_t;
static const tinst_t BASS[4] = {
    { { 0 }, 1, 0, 0.0f },                 // 0 the triangle bass
    { { 12, 0 }, 2, 1, 0.0f },             // 1 plucked: the first tick an octave up
    { { 0 }, 1, 0, 0.35f },                // 2 wobble: a vibrato
    { { 0, 0, 12, 12 }, 4, 0, 0.0f },      // 3 octaves: two ticks low, two ticks high
};

// drums on the noise channel: the period's index and the volume by the tick (volume 0 ends it)
enum { ND_SNARE, ND_HAT, ND_OPEN, ND_CLICK, ND_N };
typedef struct { uint8_t per[12], vol[12]; } ndrum_t;
static const ndrum_t NDRUM[2][ND_N] = {
    {   // the long register: hiss
        { { 9, 6, 5, 5, 6, 6, 7, 7 },                 { 13, 12, 10, 9, 7, 5, 3, 2 } },
        { { 2, 2, 2 },                                { 12, 6, 2 } },
        { { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },     { 11, 10, 9, 8, 7, 6, 5, 4, 4, 3, 2, 1 } },
        { { 10 },                                     { 9 } },
    },
    {   // the short one: metal
        { { 8, 5, 4, 4, 5, 5, 6, 6 },                 { 13, 12, 10, 9, 7, 5, 3, 2 } },
        { { 3, 3, 3 },                                { 12, 6, 2 } },
        { { 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4 },     { 11, 10, 9, 8, 7, 6, 5, 4, 4, 3, 2, 1 } },
        { { 9 },                                      { 9 } },
    },
};
// drums on the triangle: the note by the tick
static const uint8_t TKICK[4] = { 67, 55, 45, 38 };           // three ticks, so the bass under it is still heard; a fourth with an accent
static const uint8_t TTOM[5] = { 66, 62, 58, 55, 52 };

static inline int vscale(int tv, float k) { int v = (int)((float)tv * k + 0.5f); return v > 15 ? 15 : v; }

static void pulse_play(int c, const pinst_t *in, const px_voice_t *v, float k, float gc, uint32_t *rng)
{
    pulse_t *p = &S.p[c];
    pdrv_t  *d = &S.d[c];
    if (v) {
        int age = v->age;
        d->tv = in->vol[age < PV_N ? age : PV_N - 1];
        d->duty = in->duty[age < in->dn ? age : in->dloop + (age - in->dloop) % (in->dn - in->dloop)];
        d->note = v->note + (age < 2 ? (float)in->pit[age] : 0.0f);
        d->k = k; d->rel = in->rel;
        if (v->trig) p->seq = 0;                   // a new note: the driver writes the period's high byte, which restarts the sequence
    } else {
        d->tv = d->tv > d->rel ? (uint8_t)(d->tv - d->rel) : 0;
        if (!d->tv) d->part = 0xFF;
    }
    int reg = clampi((int)(CPU_HZ / (16.0f * px_hz(d->note)) - 0.5f), 0, 2047);
    reg = (int)px_glitch((uint32_t)reg, 11, gc, rng);
    p->reg = (uint16_t)reg;
    p->duty = (uint8_t)px_glitch(d->duty, 2, gc, rng);
    p->vol = (uint8_t)px_glitch((uint32_t)vscale(d->tv, d->k), 4, gc, rng);
    p->per = reg >= 8 ? (float)(reg + 1) * (2.0f * SPC) : 0.0f;     // the chip silences periods under 8
    p->out = p->per > 0.0f && ((DUTY[p->duty] >> p->seq) & 1) ? p->vol : 0;
}

static void tri_note(float note, float gc, uint32_t *rng)
{
    int reg = (int)(CPU_HZ / (32.0f * px_hz(note)) - 0.5f);
    while (reg > 2047) reg = (reg + 1) / 2 - 1;                     // under 27 Hz: an octave up
    reg = (int)px_glitch((uint32_t)reg, 11, gc, rng);
    S.tri_reg = (uint16_t)reg;
    S.tri_on = reg >= 2;                                            // under 2 it is far above hearing: it stops
    S.tri_per = (float)(reg + 1) * SPC;
}

static void dmc_start(int which, float gain, float gc, uint32_t *rng)
{
    if (!dmc_mem) return;
    S.dm_ptr = dmc_mem + DM_OFF[which]; S.dm_left = DM_BYTES[which];
    S.dm_bits = 0; S.dm_on = 1; S.dm_t = 0.0f; S.dm_gain = gain;
    S.dm_per = (float)DMC_PER[px_glitch(15, 4, gc, rng)] * SPC;     // rate $F; a bad contact plays it slower
}

static void tick(const px_frame_t *f)
{
    uint32_t rng = f->rng | 1u;
    float gc = f->glitch * (0.05f + 0.25f * f->glitch);            // the chance that a write gets a bit wrong

    // P1: the LEAD
    if (f->lead.on) S.d[0].part = PART_LEAD;
    pulse_play(0, &LEAD[f->lead.sound & 3], f->lead.on ? &f->lead : NULL, f->lead.level * (f->lead.accent ? 1.0f : 0.8f), gc, &rng);

    // P2: the CHORDS' arpeggio, else the lead's ECHO
    if (f->chord.on) {
        px_voice_t v = { 1, f->chord.trig, 0, f->chord.sound, f->chord.age, f->chord.note, f->chord.level };
        S.d[1].part = PART_CHORDS;
        pulse_play(1, &CHORD[f->chord.sound & 3], &v, f->chord.level, gc, &rng);
    } else if (f->echo.on) {
        S.d[1].part = PART_LEAD;
        pulse_play(1, &LEAD[f->echo.sound & 3], &f->echo, f->echo.level, gc, &rng);
    } else pulse_play(1, &LEAD[0], NULL, 0.0f, gc, &rng);

    // the DRUMS: the noise channel takes the snare, else the open hat, else the hat; the kick (or the tom) takes the
    // triangle from the bass for its few ticks. Kits 1 and 3 play kick, snare and tom on the DMC instead
    if (f->drums && f->drum_level > 0.0f) {
        bool dmc = (f->kit & 1) && dmc_mem, acc = f->drum_accent;
        S.kit = f->kit & 3;
        S.drum_k = f->drum_level * (acc ? 1.0f : 0.8f);
        int nd = -1;
        if (f->drums & DR_HAT) nd = ND_HAT;
        if (f->drums & DR_OPEN) nd = ND_OPEN;
        if ((f->drums & DR_SNARE) && !dmc) nd = ND_SNARE;
        // a noise channel with nothing to do adds a click to the triangle's kick (to the DMC's hits only with an accent)
        if (nd < 0 && (S.nd < 0 || S.nd == ND_CLICK) && (f->drums & (DR_KICK | DR_SNARE)) && (!dmc || acc)) nd = ND_CLICK;
        if (nd >= 0) { S.nd = (int8_t)nd; S.nd_age = 0; }
        if (dmc) {
            if (f->drums & DR_TOM) dmc_start(DM_TOM, f->drum_level, gc, &rng);
            if (f->drums & DR_KICK) dmc_start(DM_KICK, f->drum_level, gc, &rng);
            if (f->drums & DR_SNARE) dmc_start(DM_SNARE, f->drum_level, gc, &rng);
        } else {
            if (f->drums & DR_TOM) { S.td = 1; S.td_age = 0; S.td_len = 5; }
            if (f->drums & DR_KICK) { S.td = 0; S.td_age = 0; S.td_len = acc ? 4 : 3; }
        }
    }
    S.nz_vol = 0;
    if (S.nd >= 0) {
        const ndrum_t *d = &NDRUM[(S.kit >> 1) & 1][S.nd];
        int v = S.nd_age < 12 ? vscale(d->vol[S.nd_age], S.drum_k) : 0;
        if (S.nd_age < 12 && d->vol[S.nd_age]) {
            S.nz_idx = (uint8_t)px_glitch(d->per[S.nd_age], 4, gc, &rng);
            S.nz_short = (uint8_t)px_glitch((S.kit >> 1) & 1, 1, gc, &rng);
            S.nz_vol = (uint8_t)px_glitch((uint32_t)v, 4, gc, &rng);
            S.nz_per = (float)NOISE_PER[S.nz_idx] * SPC;
            S.nd_age++;
        } else S.nd = -1;
    }
    if (!S.nz_vol) S.nz_lvl = 0.0f;
    else if (S.nz_per >= 1.0f) S.nz_lvl = S.lfsr & 1u ? 0.0f : (float)S.nz_vol;

    // the triangle: a drum, else the BASS
    if (S.td >= 0 && S.td_age < S.td_len) {
        tri_note((float)(S.td ? TTOM[S.td_age] : TKICK[S.td_age]), gc, &rng);
        S.tri_k = 1.5f * clampf(S.drum_k * 1.25f, 0.0f, 1.0f);      // the drums' accent is on the noise; the triangle only has LEVEL
        S.td_age++; S.bass_on = 0;
    } else {
        S.td = -1;
        if (f->bass.on && f->bass.level > 0.0f) {
            const tinst_t *in = &BASS[f->bass.sound & 3];
            int age = f->bass.age;
            float note = f->bass.note + (float)in->pit[age < in->pn ? age : in->ploop + (age - in->ploop) % (in->pn - in->ploop)];
            if (in->vib > 0.0f) note += in->vib * sin_turns((float)age * 0.11f);
            if (f->bass.accent && age == 0) note += 12.0f;          // an accent: the first tick an octave up (two for the plucked one)
            tri_note(note, gc, &rng);
            S.tri_k = 1.5f * clampf(f->bass.level, 0.0f, 1.0f);
            S.bass_on = 1;
        } else { S.tri_on = 0; S.bass_on = 0; }                     // the triangle stops where it is: no click
    }
}

// ---------------------------------------------------------------- boot
// The DMC's samples: a curve is made (a falling sine for the kick and the tom, a sine and noise for the snare, about
// a middle that rises fast and sinks back over the sample's length, so the level rests near 0 between hits) and encoded
// the way the chip plays it: one bit a step, up 2 or down 2. Rate $F: 33144 bits a second.
static void dmc_make(int which, uint8_t *dst, int bytes)
{
    const float rate = CPU_HZ / 54.0f;
    int   bits = bytes * 8, lvl = 0;
    float ph = 0.0f, nz = 0.0f, dur = (float)bits / rate;
    uint32_t rng = 0x9E3779B9u;
    for (int i = 0; i < bits; i++) {
        float t = (float)i / rate, y;
        float c = 40.0f * fminf(1.0f, t / 0.003f) * (1.0f - t / dur);
        if (which == DM_KICK) { ph += (48.0f + 170.0f * expf(-t / 0.018f)) / rate; y = sinf(TWO_PI * ph) * expf(-t / 0.06f); }
        else if (which == DM_SNARE) {
            ph += (170.0f + 80.0f * expf(-t / 0.01f)) / rate;
            if (!(i & 3)) nz = rng_bi(&rng);
            y = 0.45f * sinf(TWO_PI * ph) * expf(-t / 0.025f) + 0.75f * nz * expf(-t / 0.045f);
        } else { ph += (95.0f + 130.0f * expf(-t / 0.05f)) / rate; y = sinf(TWO_PI * ph) * expf(-t / 0.06f); }
        if ((c * (1.0f + 0.9f * y)) > (float)lvl) { if (lvl <= 125) lvl += 2; dst[i >> 3] |= (uint8_t)(1u << (i & 7)); }
        else if (lvl >= 2) lvl -= 2;
    }
}

static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.lfsr = 1; S.nd = S.td = -1;
    S.tri_seq = 15; S.tri_val = 0;
    S.d[0].part = S.d[1].part = 0xFF;
    S.mix = mix_now();
}

static void init(void)
{
    bl_init();
    for (int i = 1; i < 31; i++) PT[i] = 95.52f / (8128.0f / (float)i + 100.0f);
    for (int i = 1; i < 204; i++) TT[i] = 163.67f / (24329.0f / (float)i + 100.0f);
    if (!dmc_mem) dmc_mem = app_alloc_fast(DM_TOTAL);
    if (dmc_mem) { memset(dmc_mem, 0, DM_TOTAL); for (int i = 0; i < DM_N; i++) dmc_make(i, dmc_mem + DM_OFF[i], DM_BYTES[i]); }
    reset();
}

static int view(px_vview_t *v, int max)
{
    static const float dw[4] = { 0.125f, 0.25f, 0.5f, 0.75f };
    if (max < 5) return 0;
    for (int c = 0; c < 2; c++) {
        const pulse_t *p = &S.p[c];
        bool on = p->vol && p->per > 0.0f;
        v[c] = (px_vview_t){ on ? VW_PULSE : VW_OFF, on ? S.d[c].part : 0xFF, CPU_HZ / (16.0f * (float)(p->reg + 1)), (float)p->vol * (1.0f / 15.0f), dw[p->duty & 3] };
    }
    v[2] = (px_vview_t){ S.tri_on ? VW_TRI : VW_OFF, S.tri_on ? (S.bass_on ? PART_BASS : PART_DRUMS) : 0xFF, CPU_HZ / (32.0f * (float)(S.tri_reg + 1)), S.tri_on ? S.tri_k * (1.0f / 1.5f) : 0.0f, 0.0f };
    v[3] = (px_vview_t){ S.nz_vol ? VW_NOISE : VW_OFF, S.nz_vol ? PART_DRUMS : 0xFF, 0.0f, (float)S.nz_vol * (1.0f / 15.0f), (float)(15 - S.nz_idx) * (1.0f / 15.0f) };
    v[4] = (px_vview_t){ S.dm_on ? VW_SAMPLE : VW_OFF, S.dm_on ? PART_DRUMS : 0xFF, 0.0f, S.dm_on ? S.dm_gain : 0.0f, (float)S.dm_lvl * (1.0f / 127.0f) };
    return 5;
}

// the menu's names: the four LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four KITs
static const char *const NAMES[16] = { "SQUARE", "HOLLOW", "PLUCK", "SWEEP",
                                       "TRIANGLE", "PLUCK", "WOBBLE", "OCTAVES",
                                       "ARP", "SOFT", "PLUCK", "SWEEP",
                                       "2A03", "DPCM", "METAL", "DPCM MTL" };
const px_console_t con_nes = { "NES", "2A03", "1983", 5, init, reset, tick, render, view, NAMES };
