// PIXEL console: the GAME BOY (DMG, 1989), played in the style of LSDj.
//
//   P1     LEAD     a pulse: duty 12.5, 25, 50 or 75 %, 4-bit volume, 11-bit frequency
//   P2     CHORDS   the arpeggio's note; the lead's ECHO when no chord sounds; the pulse KICK and TOM of kits 1 and 3
//   WAVE   BASS     32 samples of 4 bits, a wave table per sound (one steps through frames by the tick);
//                   the wave KICK and TOM of kits 0 and 2 take it for a few ticks
//   NOISE  DRUMS    snare and hats: the 15-bit register, or the 7-bit one for metal (kits 1 and 2)
//
// The chip half runs the DMG's own counters in clock cycles (4194304 a second, 131 to one of our samples). Every
// change of the output is put down as a band-limited step at its exact time (bl_step), so nothing aliases. The numbers
// are Pan Docs' (Audio Registers, Audio Details):
//   pulse   f = 131072 / (2048 - x), x 11 bits: the 8-step duty sequence moves every 4 (2048 - x) cycles
//   wave    f = 65536 / (2048 - x): a sample every 2 (2048 - x) cycles; volume 100, 50 or 25 % (a shift), or mute
//   noise   f = 524288 / r / 2^(s+1) (r = 0 counts as 0.5); 15-bit register, feedback bit 0 xor bit 1, also into
//           bit 6 in the 7-bit mode; a trigger sets it to all ones, so every hit starts the same
//   mixer   each channel's 4-bit DAC, summed evenly; then the DMG's high pass (its capacitor loses 0.999958 of its
//           charge a clock cycle: about 28 Hz)
// Not modelled: the envelope, length and sweep units (the driver writes volumes every tick, as LSDj's tables do),
// the two outputs' panning and master volume, the click of a DAC switched off (the DACs stay on).
//
// The driver half: a pulse instrument is a volume table and a duty table by the note's age in ticks, a pitch table
// for its first ticks and a release speed. A wave instrument is a wave table and a volume shape: the driver writes
// the table into the wave RAM scaled about its middle every tick (what LSDj's synth volume does), so the bass has
// 4-bit shapes and its LEVEL without the DC steps of the channel's own volume switch, which stays at 100 %. That is
// no cheat (the RAM is the chip's), but on a real DMG writing the RAM while the channel plays is not this clean.
#include "pixel.h"
#include <string.h>

#define GB_HZ   4194304.0f                         // the DMG's clock
#define SPC     ((float)AUDIO_SR / GB_HZ)          // our samples per clock cycle

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
static const uint8_t DUTY[4] DRAM_ATTR = { 0x80, 0x81, 0xE1, 0x7E };       // bit s: the output at step s (00000001, 10000001, 10000111, 01111110)
static const uint8_t NOISE_DIV[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };    // clock cycles a shift for r = 0..7, before the 2^s
#define MIX (1.0f / 60.0f)                         // four DACs of 0..15, summed evenly: all four full is 1

typedef struct {
    float    t, per;                               // samples to the sequencer's next step; a step in samples (0: at rest)
    uint16_t reg;                                  // the 11-bit frequency
    uint8_t  seq, duty, vol, out;
} pulse_t;
typedef struct { uint8_t tv, rel, duty, part; float k, note; } pdrv_t;    // the driver's side of a pulse

static struct {
    pulse_t  p[2];
    float    wv_t, wv_per;
    uint16_t wv_reg;
    uint8_t  wv_pos, wv_vol, wv_out;               // the place in the wave; the volume code (0 mute, 1: 100 %, 2: 50, 3: 25)
    uint8_t  wave[32];                             // the wave RAM
    float    nz_t, nz_per, nz_lvl;                 // nz_lvl: what the noise puts out now, 0..15 (a fast noise: its mean)
    uint16_t lfsr;
    uint8_t  nz_vol, nz_short, nz_rs;
    float    mix, hp_y;                            // the mixer's output now; the output
    float    buf[AUDIO_BLOCK + BL_TAPS];
    // the driver
    pdrv_t   d[2];
    uint8_t  bass_tv, bass_rel, bass_snd, bass_frame;
    float    bass_k;
    uint8_t  wv_part;                              // who has the wave channel (for view)
    int8_t   nd, nd_age;                           // the drum on the noise channel (ND_*, -1: none) and its age
    int8_t   td, td_age;                           // the kick (0) or the tom (1) on the wave channel or on P2, -1: none
    uint8_t  td_len, td_pulse;
    float    drum_k;
    uint8_t  kit;
} S;

static inline IRAM_ATTR float mix_now(void) { return ((float)(S.p[0].out + S.p[1].out + S.wv_out) + S.nz_lvl) * MIX; }

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
        if (S.wv_per > 0.0f) {
            while (S.wv_t < 1.0f) {
                S.wv_pos = (S.wv_pos + 1) & 31;
                uint8_t o = S.wv_vol ? S.wave[S.wv_pos] >> (S.wv_vol - 1) : 0;
                if (o != S.wv_out) { S.wv_out = o; m = mix_now(); bl_step(b, S.wv_t, m - S.mix); S.mix = m; }
                S.wv_t += S.wv_per;
            }
            S.wv_t -= 1.0f;
        }
        if (S.nz_vol) {
            uint32_t l = S.lfsr;
            if (S.nz_per < 1.0f) {                 // several shifts a sample: the mean of the bit over the sample
                float t = S.nz_t, last = 0.0f, acc = 0.0f;
                while (t < 1.0f) {
                    if (!(l & 1u)) acc += t - last;
                    last = t;
                    uint32_t x = (l ^ (l >> 1)) & 1u;
                    l = (l >> 1) | (x << 14);
                    if (S.nz_short) l = (l & ~0x40u) | (x << 6);
                    t += S.nz_per;
                }
                if (!(l & 1u)) acc += 1.0f - last;
                S.nz_t = t - 1.0f;
                S.nz_lvl = acc * (float)S.nz_vol;
                m = mix_now(); *b += m - S.mix; S.mix = m;
            } else {
                while (S.nz_t < 1.0f) {
                    uint32_t x = (l ^ (l >> 1)) & 1u;
                    l = (l >> 1) | (x << 14);
                    if (S.nz_short) l = (l & ~0x40u) | (x << 6);
                    float o = l & 1u ? 0.0f : (float)S.nz_vol;
                    if (o != S.nz_lvl) { S.nz_lvl = o; m = mix_now(); bl_step(b, S.nz_t, m - S.mix); S.mix = m; }
                    S.nz_t += S.nz_per;
                }
                S.nz_t -= 1.0f;
            }
            S.lfsr = (uint16_t)l;
        }
        float y = *b + 0.99451f * S.hp_y;          // the steps summed; the leak is the DMG's high pass
        S.hp_y = y;
        out[i] = clampf(y * 1.5f, -0.95f, 0.95f);
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
    { { 15, 14, 13, 12, 11, 10, 10, 9, 9, 9 },    { 1 }, 1, 0, { 0, 0 }, 3 },                        // 0 the 25 % lead, a decay to a hold
    { { 8, 10, 11, 12, 12, 12, 11, 11, 11, 11 },  { 2 }, 1, 0, { 0, 0 }, 2 },                        // 1 soft: 50 %, a slow start
    { { 14, 13, 12, 11, 10, 10, 9, 9, 9, 9 },     { 0, 0, 0, 1, 1, 1 }, 6, 0, { 0, 0 }, 2 },         // 2 wobble: 12.5 and 25 % in turn
    { { 15, 14, 12, 11, 10, 9, 9, 8, 8, 8 },      { 3, 2, 1, 0 }, 4, 3, { 12, 0 }, 2 },              // 3 zap: an octave blip, the duty closing to 12.5 %
};
static const pinst_t CHORD[4] = {
    { { 11, 10, 9, 9, 8, 8, 8, 8, 7, 7 },         { 0 }, 1, 0, { 0, 0 }, 3 },                        // 0 the arpeggio: 12.5 %
    { { 5, 6, 7, 8, 8, 8, 8, 8, 8, 8 },           { 2 }, 1, 0, { 0, 0 }, 1 },                        // 1 soft: 50 %
    { { 13, 11, 9, 8, 7, 6, 6, 5, 5, 5 },         { 1 }, 1, 0, { 0, 0 }, 2 },                        // 2 plucked: 25 %
    { { 9, 9, 9, 9, 8, 8, 8, 8, 8, 8 },           { 0, 1, 2, 1 }, 4, 0, { 0, 0 }, 2 },               // 3 the duty going round
};

// the wave tables. 0: a saw with a soft edge; 1: a triangle; 2: a square with a bump; WKICK: a sine for the drums.
// Sound 3 steps through WFRAME (made at init): a saw closing to a sine over eight ticks
enum { W_SAW, W_TRI, W_SQUARE, W_KICK, W_N };
static const uint8_t WAVES[W_N][32] = {
    { 8, 12, 15, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 1, 0, 3 },
    { 8, 9, 10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7 },
    { 12, 15, 15, 14, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 8, 3, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 7 },
    { 8, 9, 10, 12, 13, 14, 14, 15, 15, 15, 14, 14, 13, 12, 10, 9, 7, 6, 5, 3, 2, 1, 1, 0, 0, 0, 1, 1, 2, 3, 5, 6 },
};
#define WF_N 8
static uint8_t WFRAME[WF_N][32];
typedef struct { uint8_t wave; uint8_t vol[8]; uint8_t rel; } winst_t;     // wave 255: the frames
static const winst_t BASS[4] = {
    { W_SAW,    { 15, 15, 14, 14, 13, 13, 12, 12 }, 4 },       // 0 the wave bass
    { W_TRI,    { 15, 15, 15, 15, 15, 15, 15, 15 }, 5 },       // 1 round: a triangle
    { W_SQUARE, { 15, 14, 12, 11, 10, 10, 9, 9 }, 4 },         // 2 square, plucked
    { 255,      { 15, 15, 15, 15, 14, 14, 14, 13 }, 3 },       // 3 the frames: bright, then closing
};

// drums on the noise channel: s << 4 | r and the volume by the tick (volume 0 ends it)
enum { ND_SNARE, ND_HAT, ND_OPEN, ND_CLICK, ND_N };
typedef struct { uint8_t rs[12], vol[12]; } ndrum_t;
static const ndrum_t NDRUM[2][ND_N] = {
    {   // 15 bits: hiss
        { { 0x32, 0x31, 0x21, 0x21, 0x21, 0x21, 0x21, 0x21 }, { 13, 12, 10, 8, 6, 5, 3, 2 } },
        { { 0x01, 0x01, 0x01 },                               { 11, 5, 2 } },
        { { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 }, { 10, 9, 8, 7, 6, 6, 5, 4, 4, 3, 2, 1 } },
        { { 0x33 },                                           { 9 } },
    },
    {   // 7 bits: metal
        { { 0x31, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11, 0x11 }, { 13, 12, 10, 8, 6, 5, 3, 2 } },
        { { 0x01, 0x01, 0x01 },                               { 12, 6, 2 } },
        { { 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02 }, { 10, 9, 8, 7, 6, 6, 5, 4, 4, 3, 2, 1 } },
        { { 0x22 },                                           { 9 } },
    },
};
// the kick and the tom: the note by the tick, on the wave channel (kits 0 and 2) or on P2 with a volume (kits 1 and 3)
static const uint8_t TKICK[4] = { 64, 52, 43, 36 };            // three ticks, so the bass under it is still heard; a fourth with an accent
static const uint8_t TTOM[5] = { 66, 62, 58, 55, 52 };
static const uint8_t PKICK[4] = { 67, 55, 45, 38 }, PKICK_V[4] = { 15, 15, 12, 8 };
static const uint8_t PTOM[5] = { 70, 66, 62, 59, 56 }, PTOM_V[5] = { 15, 14, 12, 10, 7 };

static inline int vscale(int tv, float k) { int v = (int)((float)tv * k + 0.5f); return v > 15 ? 15 : v; }
// The wave channel at its loudest is quieter than a pulse at its loudest (as on the DMG), so the pulses' tables are
// played at 0.8 of their volume to sit with the bass
#define PULSE_TRIM 0.8f

static void pulse_write(int c, float note, int duty, int vol, float gc, uint32_t *rng)
{
    pulse_t *p = &S.p[c];
    int reg = clampi(2048 - (int)(131072.0f / px_hz(note) + 0.5f), 0, 2047);     // under 64 Hz: its lowest note
    reg = (int)px_glitch((uint32_t)reg, 11, gc, rng);
    p->reg = (uint16_t)reg;
    p->duty = (uint8_t)px_glitch((uint32_t)duty, 2, gc, rng);
    p->vol = (uint8_t)px_glitch((uint32_t)vol, 4, gc, rng);
    p->per = 2048 - reg >= 8 ? (float)(2048 - reg) * (4.0f * SPC) : 0.0f;         // above 16 kHz nobody hears it: it rests
    p->out = (DUTY[p->duty] >> p->seq) & 1 ? p->vol : 0;
}

static void pulse_play(int c, const pinst_t *in, const px_voice_t *v, float k, float gc, uint32_t *rng)
{
    pdrv_t *d = &S.d[c];
    if (v) {
        int age = v->age;
        d->tv = in->vol[age < PV_N ? age : PV_N - 1];
        d->duty = in->duty[age < in->dn ? age : in->dloop + (age - in->dloop) % (in->dn - in->dloop)];
        d->note = v->note + (age < 2 ? (float)in->pit[age] : 0.0f);
        d->k = k; d->rel = in->rel;
    } else {
        d->tv = d->tv > d->rel ? (uint8_t)(d->tv - d->rel) : 0;
        if (!d->tv) d->part = 0xFF;
    }
    pulse_write(c, d->note, d->duty, vscale(d->tv, d->k), gc, rng);
}

// the wave channel: a table into the RAM, scaled about its middle by k (0..1), and the note
static void wave_write(const uint8_t *w, float k, float note, float gc, uint32_t *rng)
{
    for (int i = 0; i < 32; i++) S.wave[i] = (uint8_t)(7.5f + ((float)w[i] - 7.5f) * k + 0.5f);
    if (gc > 0.0f) { uint32_t r = rng_next(rng); S.wave[r & 31] = (uint8_t)px_glitch(S.wave[r & 31], 4, gc * 4.0f, rng); }   // a bad contact on the RAM
    int reg = 2048 - (int)(65536.0f / px_hz(note) + 0.5f);
    while (reg < 0) reg = 2048 - (2048 - reg) / 2;                                // under 32 Hz: an octave up
    reg = (int)px_glitch((uint32_t)(reg > 2047 ? 2047 : reg), 11, gc, rng);
    S.wv_reg = (uint16_t)reg;
    S.wv_vol = (uint8_t)px_glitch(1, 2, gc, rng);
    S.wv_per = 2048 - reg >= 16 ? (float)(2048 - reg) * (2.0f * SPC) : 0.0f;      // above 4 kHz it rests (the driver never goes there)
}

static void tick(const px_frame_t *f)
{
    uint32_t rng = f->rng | 1u;
    float gc = f->glitch * (0.05f + 0.25f * f->glitch);            // the chance that a write gets a bit wrong

    // the DRUMS: the noise channel takes the snare, else the open hat, else the hat. The kick (or the tom) takes the
    // wave channel from the bass for its few ticks; in kits 1 and 3 it is a pulse and takes P2
    if (f->drums && f->drum_level > 0.0f) {
        S.kit = f->kit & 3;
        S.drum_k = f->drum_level * (f->drum_accent ? 1.0f : 0.8f);
        int nd = -1;
        if ((f->drums & DR_KICK) && f->drum_accent) nd = ND_CLICK;  // an accented kick: a click of noise with it
        if (f->drums & DR_HAT) nd = ND_HAT;
        if (f->drums & DR_OPEN) nd = ND_OPEN;
        if (f->drums & DR_SNARE) nd = ND_SNARE;
        if (nd >= 0 && !(nd == ND_CLICK && S.nd >= 0)) { S.nd = (int8_t)nd; S.nd_age = 0; S.lfsr = 0x7FFF; }   // the trigger sets the register
        if (f->drums & DR_TOM) { S.td = 1; S.td_age = 0; S.td_len = 5; S.td_pulse = S.kit & 1; }
        if (f->drums & DR_KICK) { S.td = 0; S.td_age = 0; S.td_len = f->drum_accent ? 4 : 3; S.td_pulse = S.kit & 1; }
    }
    S.nz_vol = 0;
    if (S.nd >= 0) {
        int metal = S.kit == 1 || S.kit == 2;
        const ndrum_t *d = &NDRUM[metal][S.nd];
        if (S.nd_age < 12 && d->vol[S.nd_age]) {
            S.nz_rs = (uint8_t)px_glitch(d->rs[S.nd_age], 7, gc, &rng);
            S.nz_short = (uint8_t)px_glitch((uint32_t)metal, 1, gc, &rng);
            S.nz_vol = (uint8_t)px_glitch((uint32_t)vscale(d->vol[S.nd_age], S.drum_k), 4, gc, &rng);
            int s = S.nz_rs >> 4;
            S.nz_per = (float)((uint32_t)NOISE_DIV[S.nz_rs & 7] << (s > 13 ? 13 : s)) * SPC;
            S.nd_age++;
        } else S.nd = -1;
    }
    if (!S.nz_vol) S.nz_lvl = 0.0f;
    else if (S.nz_per >= 1.0f) S.nz_lvl = S.lfsr & 1u ? 0.0f : (float)S.nz_vol;
    bool drum = S.td >= 0 && S.td_age < S.td_len;
    if (!drum) S.td = -1;

    // P1: the LEAD
    if (f->lead.on) S.d[0].part = PART_LEAD;                       // (a trigger does not restart a pulse's duty sequence on the DMG)
    pulse_play(0, &LEAD[f->lead.sound & 3], f->lead.on ? &f->lead : NULL, f->lead.level * (f->lead.accent ? 1.0f : 0.8f) * PULSE_TRIM, gc, &rng);

    // P2: a pulse drum, else the CHORDS' arpeggio, else the lead's ECHO
    if (drum && S.td_pulse) {
        S.d[1].part = PART_DRUMS; S.d[1].tv = 0;
        pulse_write(1, (float)(S.td ? PTOM[S.td_age] : PKICK[S.td_age]), 2, vscale(S.td ? PTOM_V[S.td_age] : PKICK_V[S.td_age], S.drum_k), gc, &rng);
    } else if (f->chord.on) {
        px_voice_t v = { 1, f->chord.trig, 0, f->chord.sound, f->chord.age, f->chord.note, f->chord.level };
        S.d[1].part = PART_CHORDS;
        pulse_play(1, &CHORD[f->chord.sound & 3], &v, f->chord.level * PULSE_TRIM, gc, &rng);
    } else if (f->echo.on) {
        S.d[1].part = PART_LEAD;
        pulse_play(1, &LEAD[f->echo.sound & 3], &f->echo, f->echo.level * PULSE_TRIM, gc, &rng);
    } else pulse_play(1, &LEAD[0], NULL, 0.0f, gc, &rng);

    // the wave channel: a wave drum, else the BASS. The bass's shape goes on under a drum
    if (f->bass.on) {
        const winst_t *in = &BASS[f->bass.sound & 3];
        int age = f->bass.age;
        S.bass_tv = in->vol[age < 8 ? age : 7]; S.bass_rel = in->rel; S.bass_snd = f->bass.sound & 3;
        S.bass_k = f->bass.level * (f->bass.accent ? 1.0f : 0.8f);
        S.bass_frame = (uint8_t)(age < WF_N ? age : WF_N - 1);
        if (f->bass.accent && age < 3) S.bass_frame = 0;            // an accent holds the frames' bright start
        if (f->bass.trig) S.wv_pos = 0;                             // a trigger starts the wave from its first sample
    } else S.bass_tv = S.bass_tv > S.bass_rel ? (uint8_t)(S.bass_tv - S.bass_rel) : 0;
    if (drum && !S.td_pulse) {
        wave_write(WAVES[W_KICK], clampf(S.drum_k * 1.25f, 0.0f, 1.0f), (float)(S.td ? TTOM[S.td_age] : TKICK[S.td_age]), gc, &rng);
        S.wv_part = PART_DRUMS;
    } else {
        const winst_t *in = &BASS[S.bass_snd];
        wave_write(in->wave == 255 ? WFRAME[S.bass_frame] : WAVES[in->wave], (float)S.bass_tv * (1.0f / 15.0f) * S.bass_k, f->bass.note, gc, &rng);
        S.wv_part = S.bass_tv ? PART_BASS : 0xFF;
    }
    if (drum) S.td_age++;
}

// ---------------------------------------------------------------- boot
static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.lfsr = 0x7FFF; S.nd = S.td = -1;
    S.d[0].part = S.d[1].part = S.wv_part = 0xFF;
    memset(S.wave, 8, sizeof S.wave); S.wv_out = 8; S.wv_vol = 1;
    S.mix = mix_now();
}

static void init(void)
{
    bl_init();
    // the frames of bass sound 3: a saw's first harmonics, fewer in each frame (12 down to 1), as a filter closing
    for (int fr = 0; fr < WF_N; fr++) {
        static const uint8_t nh[WF_N] = { 12, 9, 7, 5, 4, 3, 2, 1 };
        float y[32], pk = 0.0f;
        for (int i = 0; i < 32; i++) {
            y[i] = 0.0f;
            for (int h = 1; h <= nh[fr]; h++) y[i] += sinf(TWO_PI * (float)(h * i) / 32.0f) / (float)h * (h == nh[fr] && h > 1 ? 1.6f : 1.0f);   // a bump at the edge
            if (fabsf(y[i]) > pk) pk = fabsf(y[i]);
        }
        for (int i = 0; i < 32; i++) WFRAME[fr][i] = (uint8_t)clampi((int)(7.5f + 7.5f * y[i] / pk + 0.5f), 0, 15);
    }
    reset();
}

static int view(px_vview_t *v, int max)
{
    static const float dw[4] = { 0.125f, 0.25f, 0.5f, 0.75f };
    if (max < 4) return 0;
    for (int c = 0; c < 2; c++) {
        const pulse_t *p = &S.p[c];
        bool on = p->vol && p->per > 0.0f;
        v[c] = (px_vview_t){ on ? VW_PULSE : VW_OFF, on ? S.d[c].part : 0xFF, 131072.0f / (float)(2048 - p->reg), (float)p->vol * (1.0f / 15.0f), dw[p->duty & 3] };
    }
    bool won = S.wv_part != 0xFF;
    v[2] = (px_vview_t){ won ? VW_WAVE : VW_OFF, S.wv_part, 65536.0f / (float)(2048 - S.wv_reg), won ? (S.wv_part == PART_DRUMS ? S.drum_k : (float)S.bass_tv * (1.0f / 15.0f) * S.bass_k) : 0.0f,
                         S.bass_snd == 3 ? (float)S.bass_frame * (1.0f / (float)(WF_N - 1)) : (float)S.bass_snd * 0.25f };
    v[3] = (px_vview_t){ S.nz_vol ? VW_NOISE : VW_OFF, S.nz_vol ? PART_DRUMS : 0xFF, 0.0f, (float)S.nz_vol * (1.0f / 15.0f), S.nz_short ? 1.0f : 0.0f };
    return 4;
}

// the menu's names: the four LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four KITs
static const char *const NAMES[16] = { "PULSE", "SOFT", "WOBBLE", "ZAP",
                                       "WAVE", "ROUND", "SQUARE", "FRAMES",
                                       "ARP", "SOFT", "PLUCK", "SWEEP",
                                       "WAVE", "PULS MTL", "WAVE MTL", "PULSE" };
const px_console_t con_gameboy = { "GAME BOY", "DMG", "1989", 4, init, reset, tick, render, view, NAMES };
