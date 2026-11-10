// C64: the MOS 6581 SID (1982) and a sound driver in the manner of Rob Hubbard and Martin Galway.
//
//   voice 1  LEAD    a pulse whose width a slow sweep moves; a sawtooth with a synced start; a pulse through the
//                    filter; a ring modulated bell
//   voice 2  BASS    through the low-pass filter with a cutoff shape, and the DRUMS on the same voice: a drum is a
//                    wave table read by the tick (noise, pulse, triangle in turn, each with a pitch), after which the
//                    bass comes back
//   voice 3  CHORDS  the fast arpeggio; the lead's ECHO when no chord sounds. On this chip voice 1 is synced and ring
//                    modulated by voice 3's oscillator, so for the first ticks of a synced or ring modulated lead note
//                    voice 3 is tuned to what the lead needs (it is heard doing so)
//
// (The plan had the chords on voice 2 and the bass on 3. They are swapped because the chip wires voice 1's sync and
// ring modulation to voice 3, and the arpeggio can lend its oscillator for three ticks; the bass cannot.)
//
// The chip half keeps the SID's registers and renders from them. What is the chip's own:
//   - the oscillator: a 24-bit accumulator that adds the 16-bit frequency register every clock cycle (985248 Hz on a
//     PAL C64), so f = Fn * 985248 / 2^24. Here it is the top 24 bits of a 32-bit phase advanced once a sample
//   - the waveforms are taken from the accumulator as the chip does: sawtooth its top 12 bits, triangle the next 11
//     folded by the top bit, pulse the top 12 compared with the 12-bit width, noise a 23-bit shift register
//     (taps 22 and 17) clocked when accumulator bit 19 rises, read through bits 22 20 16 13 11 7 4 2
//   - ring modulation (the triangle folds on the XOR of its top bit and the other voice's) and hard sync (the
//     accumulator is cleared when the other voice's top bit rises)
//   - the envelope: an 8-bit counter, the rate counter's sixteen periods, the exponential counter's periods at
//     255 93 54 26 14 6, sustain = nibble * 17. A raised sustain does not raise the level, as on the chip. There is no
//     volume register per voice: a part's LEVEL is its sustain nibble (16 steps)
//   - the filter: 11-bit cutoff through the 6581's curve (220 Hz to 18 kHz, with its kink at 1024; the points are
//     reSID's measurements), 4-bit resonance (Q 0.7 to 1.7), low, band and high pass, a routing bit per voice, the
//     3OFF bit, a soft overload at its input; then the 4-bit volume and the C64's 16 Hz output high pass
// What is simplified: every edge is band-limited (a four-point B-spline step, so the output runs two samples late);
// the envelope's rate counter restarts when a rate is written (no ADSR delay bug, so no hard restart is needed) and an
// attack from 255 stays there (the chip wraps to 0); combined waveforms are a plain AND and not band-limited (the
// driver never asks for one: only a glitch does); the waveform and voice DC offsets are left out (every voice is
// centred); the filter is a zero-delay state variable filter at 32 kHz, capped at 14.4 kHz; the test bit only stops
// and clears the oscillator.
#include "pixel.h"
#include <string.h>

// ---------------------------------------------------------------- the chip
#define SID_HZ    985248.0f                        // the PAL C64's clock (17734475 / 18)
#define PH_PER_FN (SID_HZ / (float)AUDIO_SR * 256.0f)   // 32-bit phase a sample for one unit of the frequency register
#define FN_PER_HZ (16777216.0f / SID_HZ)           // the frequency register for 1 Hz
#define ENV_CYC   7882                             // clock cycles in a sample, in 1/256: 985248 / 32000 * 256
#define OUT_GAIN  0.135f                           // one voice at full envelope and volume 15

enum { ENV_ATT, ENV_DEC, ENV_REL };
enum { C_GATE = 1, C_SYNC = 2, C_RING = 4, C_TEST = 8, C_TRI = 0x10, C_SAW = 0x20, C_PULSE = 0x40, C_NOISE = 0x80 };

typedef struct {
    // registers
    uint16_t freq, pw;                 // 16 and 12 bits
    uint8_t  ctrl, ad, sr;
    // the oscillator
    uint32_t acc, prev, step, pw32;
    float    istep;                    // 1 / step: where inside a sample an edge fell (0: the oscillator stands)
    float    pmean;                    // the pulse's mean at this width, taken off so that the voice has no DC
    uint32_t lfsr;                     // 23 bits
    float    nz;                       // the noise register's output, held
    // the envelope
    int32_t  rc, rper;                 // the rate counter and its period, in 1/256 cycles
    uint8_t  env, st, expc, expp, hold0;
    // band-limiting: the waveform two samples back and the corrections still to come
    float    z1, z2, c[4];
} sv_t;

// The rate counter's periods in clock cycles for the sixteen attack / decay / release settings (2 ms .. 8 s attack;
// decay and release take three times as long through the exponential counter). From the chip's ROM, as in reSID.
static DRAM_ATTR const int32_t RATE[16] = {
    9 << 8, 32 << 8, 63 << 8, 95 << 8, 149 << 8, 220 << 8, 267 << 8, 313 << 8,
    392 << 8, 977 << 8, 1954 << 8, 3126 << 8, 3907 << 8, 11720 << 8, 19532 << 8, 31251 << 8 };
static const uint16_t ATT_MS[16] = { 2, 8, 16, 24, 38, 56, 68, 80, 100, 250, 500, 800, 1000, 3000, 5000, 8000 };

// The 6581's cutoff: register value to Hz. reSID's measured points (f0_points_6581); straight lines between them.
static const struct { uint16_t fc, hz; } FCURVE[] = {
    { 0, 220 }, { 128, 230 }, { 256, 250 }, { 384, 300 }, { 512, 420 }, { 640, 780 }, { 768, 1600 }, { 832, 2300 },
    { 896, 3200 }, { 960, 4300 }, { 992, 5000 }, { 1008, 5400 }, { 1016, 5700 }, { 1023, 6000 }, { 1024, 4600 },
    { 1032, 4800 }, { 1056, 5300 }, { 1088, 6000 }, { 1120, 6600 }, { 1152, 7200 }, { 1280, 9500 }, { 1408, 12000 },
    { 1536, 14500 }, { 1664, 16000 }, { 1792, 17100 }, { 1920, 17700 }, { 2047, 18000 } };

static struct {
    sv_t     v[3];
    // the filter and the mixer: registers
    uint16_t fc;                       // 11 bits
    uint8_t  resfilt;                  // resonance << 4 | route bits (voice 1 2 3)
    uint8_t  modevol;                  // 3OFF 0x80, HP 0x40, BP 0x20, LP 0x10, volume
    // ... and what render() uses of them
    float    a1, a2, a3, k, ic1, ic2;
    float    fg[3], ng[3];             // each voice into the filter, and past it
    float    mlp, mbp, mhp, vol;
    float    hp;                       // the output high pass's state
    // the driver
    uint32_t tk;                       // ticks since reset()
    uint32_t rng;
    uint8_t  sus[3];                   // the sustain a voice's note has now (it may only fall while the gate is up)
    uint8_t  part[3];                  // what each voice plays, for the screen
    int8_t   drum; uint8_t drum_age, drum_sus, drum_kit;
    uint8_t  bass_has, bass_age;       // voice 2 holds the bass's note; ticks since it got the voice
    uint8_t  v3_mode;                  // 0 free, 1 chord, 2 echo
    uint8_t  lead_filt;
} S;

// one step of the envelope: the rate counter has reached its period
static inline void env_clock(sv_t *v)
{
    if (v->st == ENV_ATT) {
        v->expc = 0;
        if (v->env < 0xFF) v->env++;
        if (v->env == 0xFF) { v->st = ENV_DEC; v->rper = RATE[v->ad & 15]; if (v->rc >= v->rper) v->rc = 0; }
    } else {
        if (++v->expc < v->expp) return;
        v->expc = 0;
        if (v->hold0) return;
        if (v->st == ENV_DEC) { if (v->env == (v->sr >> 4) * 17) return; }
        if (!v->env) { v->hold0 = 1; return; }
        v->env--;
    }
    switch (v->env) {                  // the exponential counter's period changes at these levels
    case 0xFF: v->expp = 1; break;
    case 0x5D: v->expp = 2; break;
    case 0x36: v->expp = 4; break;
    case 0x1A: v->expp = 8; break;
    case 0x0E: v->expp = 16; break;
    case 0x06: v->expp = 30; break;
    case 0x00: v->expp = 1; v->hold0 = 1; break;
    }
}

// A step of height h that fell d samples ago (0..1), spread over four samples: the integral of a cubic B-spline less
// the step itself (Valimaki and others, 2012). c[0] belongs to the sample two back, c[2] to this one.
static inline void blep4(float *c, float h, float d)
{
    float d2 = d * d, d3 = d2 * d, d4 = d2 * d2;
    c[0] += h * (d4 * (1.0f / 24.0f));
    c[1] += h * (d4 * -0.125f + d3 * (1.0f / 6.0f) + d2 * 0.25f + d * (1.0f / 6.0f) + (1.0f / 24.0f));
    c[2] += h * (d4 * 0.125f - d3 * (1.0f / 3.0f) + d * (2.0f / 3.0f) - 0.5f);
    c[3] += h * (d4 * (-1.0f / 24.0f) + d3 * (1.0f / 6.0f) - d2 * 0.25f + d * (1.0f / 6.0f) - (1.0f / 24.0f));
}

// the waveform DAC's 12 bits for an accumulator value, centred: -1..1 (not the noise)
static inline float sid_dac(const sv_t *v, uint32_t acc, uint32_t src)
{
    uint32_t wf = v->ctrl >> 4;
    if (wf == 2) return (float)(acc >> 20) * (1.0f / 2047.5f) - 1.0f;
    if (wf == 4) return (acc >= v->pw32 ? 1.0f : -1.0f) - v->pmean;
    uint32_t x = (v->ctrl & C_RING) ? acc ^ src : acc;
    uint32_t tri = (((x & 0x80000000u) ? ~acc : acc) >> 19) & 0xFFFu;
    if (wf == 1) return (float)tri * (1.0f / 2047.5f) - 1.0f;
    if (wf == 0 || (wf & 8)) return 0.0f;
    uint32_t o = 0xFFFu;               // several at once: the chip's waveforms pull each other low. A plain AND here
    if (wf & 1) o &= tri;
    if (wf & 2) o &= acc >> 20;
    if (wf & 4) o &= acc >= v->pw32 ? 0xFFFu : 0u;
    return (float)o * (1.0f / 2047.5f) - 1.0f;
}

static inline float noise_clock(sv_t *v)
{
    uint32_t r = v->lfsr;
    r = ((r << 1) | (((r >> 22) ^ (r >> 17)) & 1u)) & 0x7FFFFFu;
    v->lfsr = r;
    uint32_t b = ((r >> 15) & 0x80u) | ((r >> 14) & 0x40u) | ((r >> 11) & 0x20u) | ((r >> 9) & 0x10u) |
                 ((r >> 8) & 0x08u) | ((r >> 5) & 0x04u) | ((r >> 3) & 0x02u) | ((r >> 2) & 0x01u);
    return (float)b * (1.0f / 127.5f) - 1.0f;
}

// One sample of a voice: waveform times envelope (0..255). s is the voice that syncs and ring modulates it, sacc that
// voice's accumulator after this sample's step and srise whether its top bit rose in it.
static inline float voice_run(sv_t *v, const sv_t *s, uint32_t sacc, bool srise)
{
    uint32_t acc = v->acc, wf = v->ctrl >> 4;
    float w;
    if (wf & 8) {
        // the noise register is clocked where accumulator bit 19 rises: 16 times a cycle, up to twice a sample. The
        // sample is the average of what the register showed during it
        uint32_t k = (((acc - 0x08000000u) >> 28) - ((v->prev - 0x08000000u) >> 28)) & 15u;
        w = v->nz;
        if (k) {
            float d = (float)((acc - 0x08000000u) & 0x0FFFFFFFu) * v->istep;   // since the newest clock, in samples
            if (d > 1.0f) d = 1.0f;
            if (k == 1) { float nw = noise_clock(v); w += (nw - w) * d; v->nz = nw; }
            else {
                float per = 268435456.0f * v->istep, e = d + per > 1.0f ? 1.0f - d : per;
                float m = noise_clock(v), nw = noise_clock(v);
                w = w * (1.0f - d - e) + m * e + nw * d; v->nz = nw;
            }
        }
    } else if (srise && (v->ctrl & C_SYNC)) {
        float d = (float)(sacc - 0x80000000u) * s->istep;                      // the reset fell d samples ago
        if (d > 1.0f) d = 1.0f;
        uint32_t since = (uint32_t)(d * (float)v->step);
        float before = sid_dac(v, acc - since, sacc);
        v->acc = acc = since;
        w = sid_dac(v, acc, sacc);
        blep4(v->c, w - before, d);
    } else {
        w = sid_dac(v, acc, sacc);
        if (wf == 2) { if (acc < v->step) blep4(v->c, -2.0f, (float)acc * v->istep); }
        else if (wf == 4) {
            bool wrap = acc < v->step;
            if (v->pw32) {
                if (wrap && v->prev >= v->pw32) blep4(v->c, -2.0f, (float)acc * v->istep);
                if (acc >= v->pw32 && (wrap || v->prev < v->pw32)) blep4(v->c, 2.0f, (float)(acc - v->pw32) * v->istep);
            }
        } else if (wf == 1 && (v->ctrl & C_RING) && ((s->prev ^ sacc) & 0x80000000u)) {
            float d = (float)(sacc & 0x7FFFFFFFu) * s->istep;                  // the fold flipped: the triangle mirrors
            blep4(v->c, 2.0f * w, d > 1.0f ? 1.0f : d);
        }
    }
    v->rc += ENV_CYC;
    while (v->rc >= v->rper) { v->rc -= v->rper; env_clock(v); }
    float y = (v->z2 + v->c[0]) * (float)v->env;
    v->z2 = v->z1; v->z1 = w;
    v->c[0] = v->c[1]; v->c[1] = v->c[2]; v->c[2] = v->c[3]; v->c[3] = 0.0f;
    return y;
}

static IRAM_ATTR void render(float *out, int n)
{
    sv_t *v0 = &S.v[0], *v1 = &S.v[1], *v2 = &S.v[2];
    float ic1 = S.ic1, ic2 = S.ic2, hp = S.hp;
    for (int i = 0; i < n; i++) {
        v0->prev = v0->acc; v0->acc += v0->step;
        v1->prev = v1->acc; v1->acc += v1->step;
        v2->prev = v2->acc; v2->acc += v2->step;
        uint32_t a0 = v0->acc, a1 = v1->acc, a2 = v2->acc;
        bool r0 = (~v0->prev & a0) >> 31, r1 = (~v1->prev & a1) >> 31, r2 = (~v2->prev & a2) >> 31;
        float o0 = voice_run(v0, v2, a2, r2);      // voice 1 is synced and ring modulated by 3, 2 by 1, 3 by 2
        float o1 = voice_run(v1, v0, a0, r0);
        float o2 = voice_run(v2, v1, a1, r1);
        // into the filter. The 6581's filter overloads softly and not evenly: a cubic and a little square
        float x = (o0 * S.fg[0] + o1 * S.fg[1] + o2 * S.fg[2]) * (0.7f / 255.0f);
        x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
        x = (x + 0.06f * x * x - (4.0f / 27.0f) * x * x * x) * (1.0f / 0.7f);
        float v3 = x - ic2, t1 = S.a1 * ic1 + S.a2 * v3, t2 = ic2 + S.a2 * ic1 + S.a3 * v3;
        ic1 = 2.0f * t1 - ic1; ic2 = 2.0f * t2 - ic2;
        // the chip's low and high pass come out inverted, the band pass not
        float f = S.mbp * t1 - S.mlp * t2 - S.mhp * (x - S.k * t1 - t2);
        float y = ((o0 * S.ng[0] + o1 * S.ng[1] + o2 * S.ng[2]) * (1.0f / 255.0f) + f) * S.vol;
        hp += 0.00314f * (y - hp);                 // the C64's output: 16 Hz high pass (2 pi 16 / 32000)
        y -= hp;
        out[i] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
    }
    S.ic1 = flush_denormal(ic1); S.ic2 = flush_denormal(ic2); S.hp = flush_denormal(hp);
}

// ---- register writes (tick rate)
static void sid_adsr(sv_t *v, uint8_t ad, uint8_t sr)
{
    v->ad = ad; v->sr = sr;
    v->rper = RATE[v->st == ENV_ATT ? ad >> 4 : v->st == ENV_DEC ? ad & 15 : sr & 15];
    if (v->rc >= v->rper) v->rc = 0;               // (the chip would count on to 32768 first: the ADSR delay bug)
}
static void sid_ctrl(sv_t *v, uint8_t c)
{
    if ((c & C_GATE) && !(v->ctrl & C_GATE)) { v->st = ENV_ATT; v->hold0 = 0; }
    else if (!(c & C_GATE) && (v->ctrl & C_GATE)) v->st = ENV_REL;
    v->ctrl = c;
    sid_adsr(v, v->ad, v->sr);
}
static void sid_freq(sv_t *v, uint32_t fn, uint32_t pw)
{
    v->freq = (uint16_t)(fn > 65535u ? 65535u : fn); v->pw = (uint16_t)(pw & 0xFFFu);
}
// after a tick's writes: what render() needs from the registers
static void sid_latch(void)
{
    for (int i = 0; i < 3; i++) {
        sv_t *v = &S.v[i];
        v->step = (v->ctrl & C_TEST) ? 0u : (uint32_t)((float)v->freq * PH_PER_FN);
        if (v->ctrl & C_TEST) v->acc = 0;
        v->istep = v->step ? 1.0f / (float)v->step : 0.0f;
        v->pw32 = (uint32_t)v->pw << 20;
        v->pmean = 1.0f - (float)v->pw * (2.0f / 4096.0f);
        bool route = (S.resfilt >> i) & 1;
        S.fg[i] = route ? 1.0f : 0.0f;
        S.ng[i] = route || (i == 2 && (S.modevol & 0x80)) ? 0.0f : 1.0f;
    }
    int fc = S.fc & 0x7FF, k = 1;
    while (k < (int)(sizeof FCURVE / sizeof FCURVE[0]) - 1 && FCURVE[k].fc < fc) k++;
    float span = (float)(FCURVE[k].fc - FCURVE[k - 1].fc);
    float hz = (float)FCURVE[k - 1].hz + (float)(FCURVE[k].hz - FCURVE[k - 1].hz) * (span > 0.0f ? (float)(fc - FCURVE[k - 1].fc) / span : 1.0f);
    if (hz > 0.45f * (float)AUDIO_SR) hz = 0.45f * (float)AUDIO_SR;
    float p = hz * (0.5f / (float)AUDIO_SR), g = sin_turns(p) / sin_turns(p + 0.25f);   // tan(pi f / SR)
    S.k = 1.0f / (0.707f + (float)(S.resfilt >> 4) * (1.0f / 15.0f));                   // Q 0.707 .. 1.707 (reSID's 6581)
    S.a1 = 1.0f / (1.0f + g * (g + S.k)); S.a2 = g * S.a1; S.a3 = g * S.a2;
    S.mlp = (S.modevol & 0x10) ? 1.0f : 0.0f; S.mbp = (S.modevol & 0x20) ? 1.0f : 0.0f; S.mhp = (S.modevol & 0x40) ? 1.0f : 0.0f;
    S.vol = (float)(S.modevol & 15) * (OUT_GAIN / 15.0f);
}

// ---------------------------------------------------------------- the driver
// An instrument, as the old drivers kept them: the ADSR bytes and a wave table read by the note's age in ticks. Each
// step is the control register (waveform, gate, sync, ring) and semitones over the note; the last step stays.
typedef struct { uint8_t ctrl; int8_t semi; } wstep_t;
typedef struct {
    uint8_t  ad, rel;                  // attack and decay; release
    uint8_t  sus, sus_acc;             // the sustain nibble at full LEVEL: plain, accented
    uint8_t  n; wstep_t w[4];
    uint16_t pw_lo, pw_hi; uint8_t pw_rate, pw_note;   // the pulse width moves between lo and hi by pw_rate a tick:
                                       // to and fro all the time, or (pw_note) once down from hi at every note
    uint8_t  borrow; float borrow_semi;   // LEAD: for so many ticks voice 3's oscillator sits this far over the note
    uint8_t  res; uint16_t fc0, fc1; int8_t fc_d;   // through the filter (res > 0): cutoff from fc0 to fc1 by fc_d a tick
} inst_t;

#define P_ (C_PULSE | C_GATE)
#define S_ (C_SAW | C_GATE)
#define T_ (C_TRI | C_GATE)
static const inst_t LEAD[4] = {
    // 0: Hubbard's lead: a pulse, its width swept slowly all the time
    { 0x03, 7, 11, 15, 1, { { P_, 0 } }, 0x280, 0x800, 0x14, 0, 0, 0, 0, 0, 0, 0 },
    // 1: a sawtooth that starts synced to voice 3: three ticks of a falling formant, then the plain saw
    { 0x02, 6, 10, 14, 4, { { S_ | C_SYNC, 22 }, { S_ | C_SYNC, 16 }, { S_ | C_SYNC, 9 }, { S_, 0 } }, 0x800, 0x800, 0, 0, 3, 0.0f, 0, 0, 0, 0 },
    // 2: a square through the low pass, the cutoff falling at every note
    { 0x02, 6, 13, 15, 1, { { P_, 0 } }, 0x780, 0x780, 0, 0, 0, 0, 15, 0x440, 0x2E0, -0x1C },
    // 3: a bell: the triangle ring modulated by voice 3 at 2.76 times its pitch while it is struck, then let go
    { 0x02, 9, 15, 15, 4, { { T_ | C_RING, 0 }, { T_ | C_RING, 0 }, { C_TRI | C_RING, 0 }, { C_TRI, 0 } }, 0x800, 0x800, 0, 0, 3, 17.6f, 0, 0, 0, 0 },
};
static const inst_t CHORD[4] = {
    { 0x02, 4, 7, 7, 1, { { P_, 0 } }, 0x500, 0x800, 0x10, 0, 0, 0, 0, 0, 0, 0 },      // 0: the pulse arpeggio
    { 0x02, 4, 7, 7, 1, { { S_, 0 } }, 0x800, 0x800, 0, 0, 0, 0, 0, 0, 0, 0 },         // 1: sawtooth
    { 0x01, 5, 12, 12, 1, { { T_, 0 } }, 0x800, 0x800, 0, 0, 0, 0, 0, 0, 0, 0 },       // 2: triangle, soft
    { 0x02, 3, 8, 8, 1, { { P_, 0 } }, 0x1C0, 0x1C0, 0, 0, 0, 0, 0, 0, 0, 0 },       // 3: a thin pulse, reedy
};
static const inst_t BASS[4] = {
    // 0: a pulse through the low pass, the cutoff snapping shut: the plucked SID bass
    { 0x02, 8, 13, 15, 1, { { P_, 0 } }, 0x600, 0x800, 0x08, 0, 0, 0, 13, 0x3A0, 0x220, -0x30 },
    // 1: a sawtooth through the low pass, opening as the note goes on
    { 0x02, 8, 13, 15, 1, { { S_, 0 } }, 0x800, 0x800, 0, 0, 0, 0, 7, 0x260, 0x360, 0x14 },
    // 2: a triangle with one tick of pulse an octave up in front
    { 0x01, 8, 15, 15, 2, { { P_, 12 }, { T_, 0 } }, 0x800, 0x800, 0, 0, 0, 0, 0, 0, 0, 0 },
    // 3: a pulse that thins at every note
    { 0x02, 7, 11, 14, 1, { { P_, 0 } }, 0x180, 0x800, 0xA0, 1, 0, 0, 0, 0, 0, 0 },
};

// A drum: a wave table as above, but each step names the frequency register's high byte itself (the oscillator's
// pitch is byte * 15 Hz; the noise is clocked 16 times as fast). For `busy` ticks it keeps the bass off voice 2;
// after that its release rings on only while the bass has nothing to play.
typedef struct { uint8_t ad, rel, busy, n; struct { uint8_t ctrl, fhi; } w[5]; } drum_t;
#define N_ (C_NOISE | C_GATE)
static const drum_t KIT[2][PX_DRUMS] = {
    {   // 0: noise for the stroke, a pulse for the body
        { 0x01, 5, 3, 4, { { N_, 0x20 }, { P_, 0x0A }, { P_, 0x06 }, { C_PULSE, 0x04 } } },                      // KICK
        { 0x01, 6, 4, 5, { { N_, 0x60 }, { P_, 0x10 }, { N_, 0x50 }, { N_, 0x58 }, { C_NOISE, 0x58 } } },        // SNARE
        { 0x00, 1, 1, 2, { { N_, 0xF8 }, { C_NOISE, 0xF8 } } },                                                  // HAT
        { 0x00, 7, 3, 4, { { N_, 0xF0 }, { N_, 0xE0 }, { N_, 0xF0 }, { C_NOISE, 0xE8 } } },                      // OPEN
        { 0x01, 5, 4, 5, { { T_, 0x1C }, { T_, 0x18 }, { T_, 0x14 }, { T_, 0x10 }, { C_TRI, 0x0C } } },          // TOM
    },
    {   // 1: softer: a triangle kick, a snare of noise alone, darker hats, a pulse tom
        { 0x01, 6, 3, 4, { { T_, 0x12 }, { T_, 0x0A }, { T_, 0x06 }, { C_TRI, 0x04 } } },
        { 0x01, 7, 3, 4, { { N_, 0x38 }, { N_, 0x48 }, { N_, 0x40 }, { C_NOISE, 0x40 } } },
        { 0x00, 0, 1, 2, { { N_, 0xB0 }, { C_NOISE, 0xB0 } } },
        { 0x00, 8, 3, 3, { { N_, 0xA0 }, { N_, 0xB0 }, { C_NOISE, 0xA8 } } },
        { 0x01, 4, 4, 5, { { P_, 0x18 }, { P_, 0x14 }, { P_, 0x11 }, { P_, 0x0E }, { C_PULSE, 0x0B } } },
    },
};

static void reset(void)
{
    memset(&S, 0, sizeof S);
    for (int i = 0; i < 3; i++) {
        sv_t *v = &S.v[i];
        v->lfsr = 0x7FFFF8u; v->st = ENV_REL; v->hold0 = 1; v->expp = 1; v->rper = RATE[0];
        S.part[i] = 0xFF;
    }
    S.drum = -1; S.rng = 0x1982C64u;
    S.modevol = 0x1F; S.fc = 0x400;
    sid_latch();
}
static void init(void) { reset(); }

static inline uint32_t fn_of(float note) { float f = px_hz(note) * FN_PER_HZ; return f > 65535.0f ? 65535u : (uint32_t)(f + 0.5f); }
static inline uint8_t sus_of(int full, float level) { int s = (int)((float)full * level + 0.5f); return (uint8_t)(s > 15 ? 15 : s < 1 ? (level > 0.0f ? 1 : 0) : s); }

static int pw_of(const inst_t *I, int age)
{
    int span = I->pw_hi - I->pw_lo;
    if (span <= 0 || !I->pw_rate) return I->pw_lo;
    if (I->pw_note) { int p = I->pw_hi - age * I->pw_rate; return p < I->pw_lo ? I->pw_lo : p; }
    int x = (int)((S.tk * I->pw_rate) % (uint32_t)(2 * span));
    return I->pw_lo + (x < span ? x : 2 * span - x);
}
static int fc_of(const inst_t *I, int age, bool accent)
{
    int fc = I->fc0 + age * I->fc_d + (accent ? 0x40 : 0);
    return I->fc_d < 0 ? (fc < I->fc1 ? I->fc1 : fc) : (fc > I->fc1 ? I->fc1 : fc);
}

// A note of a wave-table instrument on a voice. trig: start it (the gate drops and rises again in the same tick, so
// the envelope attacks from where it is). The sustain may only fall while the gate is up: the chip cannot raise it.
static void play(int vi, const inst_t *I, float note, int age, bool trig, bool accent, float level, float g)
{
    sv_t *v = &S.v[vi];
    const wstep_t *w = &I->w[age < I->n ? age : I->n - 1];
    uint8_t sus = sus_of(accent ? I->sus_acc : I->sus, level), ctrl = w->ctrl;
    if (trig || !(v->ctrl & C_GATE)) { if (trig) sid_ctrl(v, v->ctrl & (uint8_t)~C_GATE); S.sus[vi] = sus; }
    else if (sus < S.sus[vi]) S.sus[vi] = sus;
    if (!S.sus[vi]) ctrl &= (uint8_t)~C_GATE;      // LEVEL 0: the note is not struck at all
    sid_adsr(v, I->ad, (uint8_t)(S.sus[vi] << 4 | I->rel));
    sid_freq(v, px_glitch(fn_of(note + (float)w->semi), 16, g, &S.rng), px_glitch((uint32_t)pw_of(I, age), 12, g, &S.rng));
    sid_ctrl(v, (uint8_t)px_glitch(ctrl, 8, g * 0.5f, &S.rng));
}

static void tick(const px_frame_t *f)
{
    float g = f->glitch;
    if (g > 0.0f) { S.rng ^= f->rng; if (!S.rng) S.rng = 1; }
    const inst_t *L = &LEAD[f->lead.sound & 3];
    sv_t *v1 = &S.v[0], *v2 = &S.v[1], *v3 = &S.v[2];
    int fc = 0x400; uint8_t res = 0, route = 0;

    // voice 1: the LEAD
    if (f->lead.on) {
        play(0, L, f->lead.note, f->lead.age, f->lead.trig, f->lead.accent, f->lead.level, g);
        S.part[0] = PART_LEAD;
    } else { sid_ctrl(v1, v1->ctrl & (uint8_t)~(C_GATE | C_SYNC | C_RING)); if (!v1->env) S.part[0] = 0xFF; }
    if (L->res) { res = L->res; route |= 1; fc = fc_of(L, f->lead.age, f->lead.accent); }

    // voice 3: the CHORDS' arpeggio, else the lead's ECHO, a soft one: the gate is up for one tick with an attack
    // slow enough to reach only the echo's level in that time, then it rings out
    if (f->chord.on) {
        play(2, &CHORD[f->chord.sound & 3], f->chord.note, f->chord.age, f->chord.trig || S.v3_mode != 1, false, f->chord.level, g);
        S.v3_mode = 1; S.part[2] = PART_CHORDS;
    } else if (f->echo.on && f->echo.trig && f->echo.level > 0.0f) {
        float tick_ms = 1000.0f / (f->tick_hz > 1.0f ? f->tick_hz : 60.0f), best = 9.0f;
        uint8_t att = 2;
        for (uint8_t a = 2; a < 10; a++) {
            float reach = tick_ms / (float)ATT_MS[a], d = (reach > 1.0f ? 1.0f : reach) - f->echo.level;
            if (d < 0.0f) d = -d;
            if (d < best) { best = d; att = a; }
        }
        sid_ctrl(v3, v3->ctrl & (uint8_t)~C_GATE);
        sid_adsr(v3, (uint8_t)(att << 4), 0xF8);
        sid_freq(v3, px_glitch(fn_of(f->echo.note), 16, g, &S.rng), (uint32_t)pw_of(L, 0));
        sid_ctrl(v3, (uint8_t)((L->w[L->n - 1].ctrl & 0xF0) | C_GATE));
        S.v3_mode = 2; S.part[2] = PART_LEAD;
    } else {
        if (S.v3_mode == 2 && f->echo.on) sid_freq(v3, fn_of(f->echo.note), v3->pw);   // it follows a slide or the vibrato
        sid_ctrl(v3, v3->ctrl & (uint8_t)~C_GATE);
        if (!v3->env) { S.part[2] = 0xFF; S.v3_mode = 0; }
    }
    // a synced or ring modulated lead takes voice 3's oscillator for its first ticks
    if (f->lead.on && f->lead.age < L->borrow)
        sid_freq(v3, px_glitch(fn_of(f->lead.note + L->borrow_semi), 16, g, &S.rng), v3->pw);

    // voice 2: the DRUMS when one is struck, else the BASS
    const inst_t *B = &BASS[f->bass.sound & 3];
    if (f->drums) {
        S.drum = f->drums & DR_SNARE ? 1 : f->drums & DR_KICK ? 0 : f->drums & DR_TOM ? 4 : f->drums & DR_OPEN ? 3 : 2;
        S.drum_age = 0; S.drum_kit = f->kit & 1;
        S.drum_sus = sus_of(f->drum_accent ? 15 : 13, f->drum_level);
        S.bass_has = 0;
    }
    if (S.drum >= 0) {
        const drum_t *D = &KIT[S.drum_kit][S.drum];
        if (S.drum_age >= D->busy && f->bass.on) S.drum = -1;          // the bass wants the voice back
        else {
            int k = S.drum_age < D->n ? S.drum_age : D->n - 1;
            uint8_t ctrl = D->w[k].ctrl;
            if (!S.drum_sus) ctrl &= (uint8_t)~C_GATE;
            if (!S.drum_age) sid_ctrl(v2, v2->ctrl & (uint8_t)~C_GATE);
            sid_adsr(v2, D->ad, (uint8_t)(S.drum_sus << 4 | D->rel));
            sid_freq(v2, px_glitch((uint32_t)D->w[k].fhi << 8, 16, g, &S.rng), 0x800);
            sid_ctrl(v2, (uint8_t)px_glitch(ctrl, 8, g * 0.5f, &S.rng));
            if (S.drum_age < 255) S.drum_age++;
            S.part[1] = PART_DRUMS;
            if (!v2->env && k == D->n - 1 && S.drum_age > D->n) { S.drum = -1; S.part[1] = 0xFF; }
        }
    }
    if (S.drum < 0) {
        if (f->bass.on) {
            bool trig = f->bass.trig || !S.bass_has;
            if (trig) S.bass_age = 0;
            play(1, B, f->bass.note, S.bass_age, trig, f->bass.accent, f->bass.level, g);
            if (S.bass_age < 255) S.bass_age++;
            S.bass_has = 1; S.part[1] = PART_BASS;
        } else if (S.bass_has) { sid_ctrl(v2, v2->ctrl & (uint8_t)~C_GATE); if (!v2->env) { S.part[1] = 0xFF; S.bass_has = 0; } }
        // the one filter: the bass has it, unless the lead's sound is the filtered one: then the lead's notes move it
        if (B->res && S.bass_has) { route |= 2; if (!L->res) { res = B->res; fc = fc_of(B, S.bass_age ? S.bass_age - 1 : 0, f->bass.accent); } }
    }
    S.fc = (uint16_t)px_glitch((uint32_t)fc, 11, g, &S.rng);
    S.resfilt = (uint8_t)(res << 4 | route);
    S.modevol = (uint8_t)px_glitch(0x1F, 8, g * 0.3f, &S.rng);         // low pass, volume 15
    S.tk++;
    sid_latch();
}

static int view(px_vview_t *v, int max)
{
    if (max < 3) return 0;
    for (int i = 0; i < 3; i++) {
        const sv_t *s = &S.v[i];
        uint8_t wf = s->ctrl >> 4;
        v[i].kind = !s->env ? VW_OFF : wf & 8 ? VW_NOISE : wf & 4 ? VW_PULSE : wf & 2 ? VW_SAW : wf & 1 ? VW_TRI : VW_OFF;
        v[i].part = S.part[i];
        v[i].hz = wf & 8 ? 0.0f : (float)s->freq * (1.0f / FN_PER_HZ);
        v[i].level = (float)s->env * (1.0f / 255.0f);
        v[i].shape = wf & 4 ? 1.0f - (float)s->pw * (1.0f / 4096.0f) : (float)s->env * (1.0f / 255.0f);
    }
    return 3;
}

static const char *const NAMES[16] = { "PULSE", "SYNC SAW", "FILTER", "BELL",   "PLUCK", "SAW", "TRIANGLE", "RUBBER",
                                       "PULSE", "SAW", "SOFT", "THIN",          "HARD", "SOFT", "HARD", "SOFT" };
const px_console_t con_c64 = { "C64", "SID 6581", "1982", 3, init, reset, tick, render, view, NAMES };
