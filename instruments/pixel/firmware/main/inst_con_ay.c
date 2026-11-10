// PIXEL console: the AY (General Instrument AY-3-8910, 1978, as in the ZX Spectrum 128; the Atari ST's YM2149 is
// its twin).
//
//   A   LEAD     a square, 4-bit volume through the chip's logarithmic DAC, ornaments (a pitch table by the tick)
//   B   CHORDS   the arpeggio's note; the lead's ECHO when no chord sounds; the snare, the hats and the tom take it
//                for their few ticks
//   C   BASS     the buzzer bass (the hardware envelope's sawtooth at the note's frequency, as the waveform), a square,
//                the buzzer cut by a square an octave up and a little off (it phases), a square plucked by the envelope;
//                the KICK takes it
//
// The chip half runs the AY's own counters in clock cycles (1773400 a second, 55.4 to one of our samples). Every
// change of the output is put down as a band-limited step at its exact time (bl_step), so nothing aliases. The numbers
// are the datasheet's, the DAC's levels the measured ones the Ayumi emulator uses:
//   tone       f = clock / (16 TP), TP 12 bits: the square turns over every 8 TP cycles
//   noise      a 17-bit register, feedback bit 0 xor bit 3, shifted every 16 NP cycles, NP 5 bits
//   mixer      per channel: (tone or tone off) and (noise or noise off); both off is a steady level
//   volume     4 bits through the DAC's 16 levels (about 3 dB a step), or the envelope's level instead
//   envelope   16 steps a ramp, a step every 16 EP cycles (EP 16 bits), the 16 shapes: a sawtooth at
//              f = clock / (256 EP), a triangle at clock / (512 EP). Writing the shape starts it again
//   output     the three channels summed (the Spectrum 128 is mono), then a 20 Hz high pass
// A square too high to hear (TP under 5: above 22 kHz) is held high instead of being run.
//
// The driver half is in the manner of the Spectrum's trackers: an instrument is a volume table and an ornament
// (semitones by the tick). A LEVEL moves a voice down the DAC's own steps (the nearest one).
//
// The cheat: a voice that plays the envelope has no volume on the chip (the envelope is its volume). BASS > LEVEL
// and the accent scale channel C's output while it plays the envelope, so the buzzer's LEVEL does something.
// The buzzer's pitch has the chip's coarse steps: fine up to about C3, then up to 30 cents off.
#include "pixel.h"
#include <string.h>

#define AY_HZ   1773400.0f                         // the Spectrum 128's AY clock
#define SPC     ((float)AUDIO_SR / AY_HZ)          // our samples per clock cycle

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
// the DAC: the AY-3-8910's 16 levels as measured (the table of the Ayumi emulator)
static const float DAC[16] DRAM_ATTR = { 0.0f, 0.0099947f, 0.0144503f, 0.0210575f, 0.0307012f, 0.0455482f, 0.0644999f, 0.1073625f,
                                         0.1265888f, 0.2049897f, 0.2922103f, 0.3728389f, 0.4925307f, 0.6353246f, 0.8055848f, 1.0f };
#define ENV_MODE 0x10                              // a volume's bit 4: the envelope's level instead

static struct {
    struct { float t, per; uint16_t tp; uint8_t bit, toff, noff, vol; } ch[3];    // toff, noff: the mixer's bits (1: off)
    float    nz_t, nz_per;
    uint32_t lfsr;
    uint8_t  np, nz_bit, nz_used;
    float    env_t, env_per;
    uint16_t ep;
    int8_t   env_cnt;
    uint8_t  env_att, env_hold, env_alt, env_holding, env_lvl, env_used;
    float    c_gain;                               // the cheat: channel C's level while it plays the envelope
    float    mix, hp_y;                            // the three channels' sum now; the output
    float    buf[AUDIO_BLOCK + BL_TAPS];
    // the driver
    struct { uint8_t tv, rel, part; float k, note; } d[3];
    uint8_t  bass_mode;                            // what C plays for the bass now (BM_*)
    int8_t   bd, bd_age;                           // the drum on channel B (BD_*, -1: none) and its age
    int8_t   kick_age;                             // the kick on channel C, -1: none
    uint8_t  kick_acc;
    float    drum_k;
    uint8_t  kit;
} S;

static inline IRAM_ATTR float mix_now(void)
{
    float s = 0.0f;
    for (int c = 0; c < 3; c++) {
        if (!((S.ch[c].bit | S.ch[c].toff) & (S.nz_bit | S.ch[c].noff))) continue;
        float l = DAC[S.ch[c].vol & ENV_MODE ? S.env_lvl : S.ch[c].vol & 15];
        s += c == 2 && (S.ch[c].vol & ENV_MODE) ? l * S.c_gain : l;
    }
    return s * (1.0f / 3.0f);
}

// writing the envelope's shape (register 13) starts it: bit 3 continue, 2 attack, 1 alternate, 0 hold
static void env_shape(int shape)
{
    S.env_att = shape & 4 ? 15 : 0;
    if (!(shape & 8)) { S.env_hold = 1; S.env_alt = S.env_att != 0; }
    else { S.env_hold = shape & 1; S.env_alt = (shape >> 1) & 1; }
    S.env_cnt = 15; S.env_holding = 0;
    S.env_lvl = (uint8_t)(S.env_cnt ^ S.env_att);
}

static IRAM_ATTR void render(float *out, int n)
{
    float *b = S.buf;
    float m = mix_now();                           // what the driver wrote at the tick
    if (m != S.mix) { bl_step(b, 0.0f, m - S.mix); S.mix = m; }
    for (int i = 0; i < n; i++, b++) {
        for (int c = 0; c < 3; c++) {
            if (S.ch[c].per <= 0.0f) continue;
            while (S.ch[c].t < 1.0f) {
                S.ch[c].bit ^= 1;
                m = mix_now();
                if (m != S.mix) { bl_step(b, S.ch[c].t, m - S.mix); S.mix = m; }
                S.ch[c].t += S.ch[c].per;
            }
            S.ch[c].t -= 1.0f;
        }
        if (S.nz_used) {
            while (S.nz_t < 1.0f) {
                S.lfsr = (S.lfsr >> 1) | (((S.lfsr ^ (S.lfsr >> 3)) & 1u) << 16);
                if ((S.lfsr & 1u) != S.nz_bit) {
                    S.nz_bit = S.lfsr & 1u;
                    m = mix_now();
                    if (m != S.mix) { bl_step(b, S.nz_t, m - S.mix); S.mix = m; }
                }
                S.nz_t += S.nz_per;
            }
            S.nz_t -= 1.0f;
        }
        if (S.env_used && !S.env_holding) {
            while (S.env_t < 1.0f) {
                if (--S.env_cnt < 0) {
                    if (S.env_hold) { if (S.env_alt) S.env_att ^= 15; S.env_holding = 1; S.env_cnt = 0; }
                    else { if (S.env_alt) S.env_att ^= 15; S.env_cnt = 15; }
                }
                S.env_lvl = (uint8_t)(S.env_cnt ^ S.env_att);
                m = mix_now();
                if (m != S.mix) { bl_step(b, S.env_t, m - S.mix); S.mix = m; }
                S.env_t += S.env_per;
                if (S.env_holding) break;
            }
            S.env_t -= 1.0f;
            if (S.env_t < 0.0f) S.env_t = 0.0f;
        }
        float y = *b + 0.99608f * S.hp_y;          // the steps summed; the leak is a 20 Hz high pass
        S.hp_y = y;
        out[i] = clampf(y * 1.3f, -0.95f, 0.95f);
    }
    S.hp_y = flush_denormal(S.hp_y);
    memmove(S.buf, S.buf + n, sizeof(float) * BL_TAPS);
    memset(S.buf + BL_TAPS, 0, sizeof(float) * (size_t)n);
}

// ---------------------------------------------------------------- the driver
#define PV_N 10
typedef struct {
    uint8_t vol[PV_N];         // the volume 0..15 by the note's age in ticks; the last one holds
    int8_t  orn[4];            // the ornament: semitones by age; from `oloop` to `on` it goes round
    uint8_t on, oloop;
    uint8_t rel;               // after the note: volume steps down a tick
} ainst_t;

static const ainst_t LEAD[4] = {
    { { 15, 15, 14, 14, 14, 14, 13, 13, 13, 13 }, { 0 }, 1, 0, 2 },                // 0 the square lead
    { { 15, 14, 13, 12, 11, 11, 10, 10, 10, 10 }, { 12, 12, 0 }, 3, 2, 2 },        // 1 bell: two ticks an octave up, a longer fall
    { { 9, 11, 12, 13, 13, 13, 13, 13, 13, 13 },  { 0 }, 1, 0, 1 },                // 2 soft: a slow start
    { { 14, 14, 13, 13, 13, 12, 12, 12, 12, 12 }, { 0, 12 }, 2, 0, 2 },            // 3 octaves: the note and its octave in turn
};
static const ainst_t CHORD[4] = {
    { { 13, 12, 12, 11, 11, 11, 10, 10, 10, 10 }, { 0 }, 1, 0, 3 },                // 0 the arpeggio
    { { 8, 9, 10, 11, 11, 11, 11, 11, 11, 11 },   { 0 }, 1, 0, 1 },                // 1 soft
    { { 14, 13, 12, 11, 10, 10, 9, 9, 9, 9 },     { 0 }, 1, 0, 2 },                // 2 plucked
    { { 12, 12, 11, 11, 11, 10, 10, 10, 10, 10 }, { 0, 12 }, 2, 0, 2 },            // 3 octaves
};
// the bass on channel C. BM_BUZZ: the envelope's sawtooth (shape 12) at the note, no tone. BM_SQUARE: a tone with a
// volume table. BM_PHASE: the sawtooth cut by a tone an octave and 12 cents above it: two cuts a cycle that wander
// through the sawtooth (the chip cannot tie the two together, so each note starts somewhere else in the sweep).
// BM_PLUCK: a tone whose volume is the envelope's one fall (shape 9), a third of a second long
enum { BM_BUZZ, BM_SQUARE, BM_PHASE, BM_PLUCK };
#define PLUCK_EP 2400                              // 256 EP / clock = 0.35 s
static const uint8_t BASS_VOL[PV_N] = { 14, 13, 13, 12, 12, 12, 11, 11, 11, 11 }; // BM_SQUARE's table

// drums on channel B, by the tick: what the mixer lets through (1 tone, 2 noise, 3 both: the tone cuts the noise),
// the tone's note, the noise's period and the volume (0 ends it)
enum { BD_SNARE, BD_HAT, BD_OPEN, BD_TOM, BD_N };
typedef struct { uint8_t mix[8], note[8], np[8], vol[8]; } bdrum_t;
static const bdrum_t BDRUM[2][BD_N] = {
    {   // kit 0: noise
        { { 3, 2, 2, 2, 2, 2 },             { 55 },                              { 6, 8, 10, 10, 12, 12 },         { 13, 13, 12, 11, 9, 7 } },
        { { 2, 2 },                         { 0 },                               { 1, 1 },                         { 12, 8 } },
        { { 2, 2, 2, 2, 2, 2, 2, 2 },       { 0 },                               { 2, 2, 2, 2, 2, 2, 2, 2 },       { 12, 12, 11, 10, 9, 8, 7, 6 } },
        { { 1, 1, 1, 1, 1, 1 },             { 62, 58, 55, 52, 50, 48 },          { 0 },                            { 13, 13, 13, 12, 12, 10 } },
    },
    {   // kit 1: the tone cuts the noise
        { { 3, 3, 3, 3, 3, 3 },             { 57, 55, 53, 52, 51, 50 },          { 4, 4, 5, 5, 6, 6 },             { 13, 13, 13, 12, 11, 9 } },
        { { 3, 3 },                         { 113, 113 },                        { 1, 1 },                         { 14, 10 } },
        { { 3, 3, 3, 3, 3, 3, 3, 3 },       { 108, 108, 108, 108, 108, 108, 108, 108 }, { 2, 2, 2, 2, 2, 2, 2, 2 }, { 14, 13, 12, 11, 10, 9, 8, 7 } },
        { { 1, 1, 1, 1, 1, 1 },             { 55, 52, 49, 47, 45, 43 },          { 0 },                            { 13, 13, 13, 12, 12, 10 } },
    },
};
// the kick on channel C: a tone falling fast (kit 0 with a click of noise on its first tick). It is short so that the
// bass under it is still heard
static const uint8_t KICK[2][4] = { { 62, 50, 40, 33 }, { 57, 47, 39, 33 } };       // kit 0: three ticks (a fourth with an accent); kit 1: four
static const uint8_t KICK_V[4] = { 13, 13, 13, 12 };

// the chip's volume step nearest to a table volume scaled by k
static int ay_vol(int tv, float k)
{
    if (tv <= 0 || k <= 0.0f) return 0;
    float want = DAC[tv] * k;
    int   v = tv;
    while (v > 1 && DAC[v - 1] * DAC[v] > want * want) v--;
    return v;
}
// the squares are played under their tables' volume to sit with the buzzer, whose level the chip fixes
#define LEAD_TRIM  0.7f
#define CHORD_TRIM 0.85f
static inline int tone_tp(float note) { return clampi((int)(AY_HZ / (16.0f * px_hz(note)) + 0.5f), 1, 4095); }

// the registers of one channel: the tone's period, the mixer's two bits, the volume. A bad contact flips a bit
static void ch_write(int c, int tp, bool tone, bool noise, int vol, float gc, uint32_t *rng)
{
    tp = (int)px_glitch((uint32_t)tp, 12, gc, rng);
    vol = (int)px_glitch((uint32_t)vol, 5, gc, rng);
    uint32_t mx = px_glitch((tone ? 0u : 1u) | (noise ? 0u : 2u), 2, gc, rng);
    S.ch[c].tp = (uint16_t)tp; S.ch[c].vol = (uint8_t)vol;
    S.ch[c].toff = mx & 1; S.ch[c].noff = (mx >> 1) & 1;
    bool sounds = (vol & ENV_MODE) || (vol & 15);
    if (!S.ch[c].toff && sounds && tp >= 5) S.ch[c].per = (float)tp * (8.0f * SPC);
    else { S.ch[c].per = 0.0f; if (tp < 5) S.ch[c].bit = 1; }      // too high to hear: held high
}

static void voice_play(int c, const ainst_t *in, const px_voice_t *v, float k, float gc, uint32_t *rng)
{
    if (v) {
        int age = v->age;
        S.d[c].tv = in->vol[age < PV_N ? age : PV_N - 1];
        S.d[c].note = v->note + (float)in->orn[age < in->on ? age : in->oloop + (age - in->oloop) % (in->on - in->oloop)];
        S.d[c].k = k; S.d[c].rel = in->rel;
    } else {
        S.d[c].tv = S.d[c].tv > S.d[c].rel ? (uint8_t)(S.d[c].tv - S.d[c].rel) : 0;
        if (!S.d[c].tv) S.d[c].part = 0xFF;
    }
    ch_write(c, tone_tp(S.d[c].note), true, false, ay_vol(S.d[c].tv, S.d[c].k), gc, rng);
}

static void tick(const px_frame_t *f)
{
    uint32_t rng = f->rng | 1u;
    float gc = f->glitch * (0.05f + 0.25f * f->glitch);            // the chance that a write gets a bit wrong
    int   np = S.np;

    if (f->drums && f->drum_level > 0.0f) {
        S.kit = f->kit & 1;                                        // kits 2 and 3 are 0 and 1 again
        S.drum_k = f->drum_level * (f->drum_accent ? 1.0f : 0.8f);
        int bd = -1;
        if (f->drums & DR_HAT) bd = BD_HAT;
        if (f->drums & DR_OPEN) bd = BD_OPEN;
        if (f->drums & DR_TOM) bd = BD_TOM;
        if (f->drums & DR_SNARE) bd = BD_SNARE;
        if (bd >= 0) { S.bd = (int8_t)bd; S.bd_age = 0; }
        if (f->drums & DR_KICK) { S.kick_age = 0; S.kick_acc = f->drum_accent; }
    }

    // A: the LEAD
    if (f->lead.on) S.d[0].part = PART_LEAD;
    voice_play(0, &LEAD[f->lead.sound & 3], f->lead.on ? &f->lead : NULL, f->lead.level * (f->lead.accent ? 1.0f : 0.8f) * LEAD_TRIM, gc, &rng);

    // B: a drum, else the CHORDS' arpeggio, else the lead's ECHO
    const bdrum_t *bd = S.bd >= 0 ? &BDRUM[S.kit][S.bd] : NULL;
    if (bd && S.bd_age < 8 && bd->vol[S.bd_age]) {
        int a = S.bd_age++;
        if (bd->mix[a] & 2) np = bd->np[a];
        ch_write(1, bd->note[a] ? tone_tp((float)bd->note[a]) : 1, bd->mix[a] & 1, bd->mix[a] & 2, ay_vol(bd->vol[a], S.drum_k), gc, &rng);
        S.d[1].part = PART_DRUMS; S.d[1].tv = 0;
    } else {
        S.bd = -1;
        if (f->chord.on) {
            px_voice_t v = { 1, f->chord.trig, 0, f->chord.sound, f->chord.age, f->chord.note, f->chord.level };
            S.d[1].part = PART_CHORDS;
            voice_play(1, &CHORD[f->chord.sound & 3], &v, f->chord.level * CHORD_TRIM, gc, &rng);
        } else if (f->echo.on) {
            S.d[1].part = PART_LEAD;
            voice_play(1, &LEAD[f->echo.sound & 3], &f->echo, f->echo.level * LEAD_TRIM, gc, &rng);
        } else voice_play(1, &LEAD[0], NULL, 0.0f, gc, &rng);
    }

    // C: the kick, else the BASS
    int klen = S.kit || S.kick_acc ? 4 : 3;
    if (S.kick_age >= 0 && S.kick_age < klen) {
        int a = S.kick_age++;
        bool click = !S.kit && a == 0 && S.bd < 0;                 // kit 0: noise on its first tick, if no other drum has the noise
        if (click) np = 12;
        ch_write(2, tone_tp((float)KICK[S.kit][a]), true, click, ay_vol(KICK_V[a], S.drum_k), gc, &rng);
        S.d[2].part = PART_DRUMS; S.d[2].tv = 0;
    } else {
        S.kick_age = -1;
        const px_voice_t *v = &f->bass;
        int mode = v->sound & 3;
        float k = v->level * (v->accent ? 1.0f : 0.8f);
        if (v->on && k > 0.0f && mode != BM_SQUARE) {
            // the envelope is the waveform (its period from the note) or the pluck; its shape is written when the note starts
            int ep = mode == BM_PLUCK ? PLUCK_EP : clampi((int)(AY_HZ / (256.0f * px_hz(v->note)) + 0.5f), 1, 65535);
            if (v->trig || S.bass_mode != mode || S.d[2].part != PART_BASS) env_shape(mode == BM_PLUCK ? 9 : 12);
            S.ep = (uint16_t)px_glitch((uint32_t)ep, 16, gc, &rng);
            S.c_gain = k > 1.0f ? 1.0f : k;
            ch_write(2, tone_tp(v->note + (mode == BM_PHASE ? 12.12f : 0.0f)), mode != BM_BUZZ, false, ENV_MODE, gc, &rng);
            S.d[2].part = PART_BASS; S.d[2].tv = 0; S.d[2].note = v->note;
        } else if (v->on && mode == BM_SQUARE) {
            S.d[2].tv = BASS_VOL[v->age < PV_N ? v->age : PV_N - 1]; S.d[2].k = k; S.d[2].rel = 3; S.d[2].note = v->note; S.d[2].part = PART_BASS;
            ch_write(2, tone_tp(v->note), true, false, ay_vol(S.d[2].tv, k), gc, &rng);
        } else {                                                   // the note is over: a square fades fast, the envelope voices stop
            S.d[2].tv = S.d[2].tv > S.d[2].rel ? (uint8_t)(S.d[2].tv - S.d[2].rel) : 0;
            if (!S.d[2].tv) S.d[2].part = 0xFF;
            ch_write(2, tone_tp(S.d[2].note), true, false, ay_vol(S.d[2].tv, S.d[2].k), gc, &rng);
        }
        S.bass_mode = (uint8_t)mode;
    }

    // the noise's and the envelope's periods; they run only while a channel that sounds uses them
    S.np = (uint8_t)px_glitch((uint32_t)np, 5, gc, &rng);
    S.nz_per = (float)(S.np ? S.np : 1) * (16.0f * SPC);
    S.env_per = (float)(S.ep ? S.ep : 1) * (16.0f * SPC);
    S.nz_used = S.env_used = 0;
    for (int c = 0; c < 3; c++) {
        if (S.ch[c].vol & ENV_MODE) S.env_used = 1;
        if (!S.ch[c].noff && S.ch[c].vol) S.nz_used = 1;
    }
}

// ---------------------------------------------------------------- boot
static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.lfsr = 1; S.np = 1; S.ep = 1;
    S.bd = S.kick_age = -1;
    for (int c = 0; c < 3; c++) { S.ch[c].toff = S.ch[c].noff = 1; S.ch[c].tp = 1; S.d[c].part = 0xFF; S.d[c].note = 60.0f; }
    env_shape(0);
    S.mix = mix_now();
}

static void init(void) { bl_init(); reset(); }

static int view(px_vview_t *v, int max)
{
    if (max < 3) return 0;
    for (int c = 0; c < 3; c++) {
        bool env = S.ch[c].vol & ENV_MODE, on = env || (S.ch[c].vol & 15);
        int  kind = !on ? VW_OFF : env ? VW_BUZZ : !S.ch[c].noff ? VW_NOISE : VW_PULSE;
        float hz = env && S.ch[c].toff ? AY_HZ / (256.0f * (float)(S.ep ? S.ep : 1)) : kind == VW_NOISE ? 0.0f : AY_HZ / (16.0f * (float)(S.ch[c].tp ? S.ch[c].tp : 1));
        v[c] = (px_vview_t){ (uint8_t)kind, on ? S.d[c].part : 0xFF, hz, !on ? 0.0f : env ? S.c_gain : DAC[S.ch[c].vol & 15], env ? (float)S.env_lvl * (1.0f / 15.0f) : 0.5f };
    }
    return 3;
}

// the menu's names: the four LEAD sounds, the four BASS sounds, the four CHORDS sounds, the four KITs
static const char *const NAMES[16] = { "SQUARE", "BELL", "SOFT", "OCTAVES",
                                       "BUZZER", "SQUARE", "PHASE", "PLUCK",
                                       "ARP", "SOFT", "PLUCK", "OCTAVES",
                                       "NOISE", "TONE", "NOISE", "TONE" };
const px_console_t con_ay = { "AY", "AY-3-8910", "1978", 3, init, reset, tick, render, view, NAMES };
