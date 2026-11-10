// MEGA: the Mega Drive's Yamaha YM2612 (four-operator FM) with the SN76489 PSG beside it (1988).
//
//   FM 1     BASS
//   FM 2     LEAD
//   FM 3-5   the CHORD, whole (ARP SLOW breaks it: a note a step, one channel after the other, each ringing on).
//            Of a four-note chord the fifth is left out
//   FM 6     the DAC: a kick and a snare (8-bit samples made in init(), one at a time, the snare over the kick)
//   PSG 1    the lead's ECHO
//   PSG 2    the TOM: a square that falls
//   PSG 3    silent: its period clocks the noise (kit 0)
//   noise    the HATS
//
// The FM chip, as it is: a 20-bit phase per operator that advances by fnum * 2^block / 2 (plus the detune table's
// few counts) times the multiple, 53267 times a second (7670454 Hz / 144), so f = fnum * 2^block * 53267 / 2^21. A
// 10-bit sine made of the chip's 256-step log-sine and exponent tables; an operator's 14-bit output shifts the next
// one's phase by up to 8 pi; operator 1 feeds itself with the mean of its last two outputs (pi/16 to 4 pi). The eight
// algorithms. An envelope per operator in 0.094 dB steps: attack (an exponential approach), two decays and a release
// (straight lines in dB) at rates 0..63 = 2 * rate + the key code scaled by KS, the sustain level in 3 dB steps, the
// total level in 0.75 dB steps. A part's LEVEL and an accent go through the carriers' total level. Each channel's
// sum is clipped to 14 bits and leaves through a 9-bit DAC with the YM2612's fault: a step four times too large at
// zero (+4 for positive, -3 for negative, in its own steps), the "ladder" that makes quiet tails gritty. Channel 6 can
// be an 8-bit DAC instead (here fed 10653 bytes a second, a usual rate for the Z80's sample loops). Then the Model 1's
// output low pass (3.39 kHz, one pole).
// The PSG: three squares with a 10-bit period (f = 3579545 / 32 N) and a 16-bit noise register (taps 0 and 3) shifted
// at 3579545 / 512, / 1024, / 2048 or at tone 3's rate, each with 16 levels 2 dB apart.
//
// Simplified: the FM runs at 64 kHz, not 53.267 kHz (the phase steps and the envelope rates are scaled to keep every
// pitch and time), and is brought down to 32 kHz through a 31-tap low pass. The envelopes move every half millisecond
// (the chip: up to 17756 times a second) with the level gliding in between. An operator hears its modulators' output
// of the same sample (the chip delays some paths by one). Not there: the LFO (the player makes the vibrato), SSG-EG,
// channel 3's special mode, CSM, the timers, stereo, the DAC loop's jitter. The PSG's squares are centred, not 0..1.
#include "pixel.h"
#include <string.h>

// ---------------------------------------------------------------- the FM chip
#define FM_HZ      53267.0f                        // the YM2612's own sample rate: 7670454 / 144 (NTSC Mega Drive)
#define FM_OS      2                               // the FM is rendered at FM_OS * 32 kHz
#define EG_CHUNK   16                              // output samples between two envelope steps (0.5 ms)
#define EG_N       (EG_CHUNK * FM_OS)              // FM samples in that
#define EG_CLOCKS  ((float)EG_CHUNK / (float)AUDIO_SR * FM_HZ / 3.0f)   // the chip's envelope clocks in that: 8.88
#define FM_GAIN    (0.32f / 256.0f)                // one channel's full 9-bit swing
#define PSG_GAIN   0.13f                           // one square at attenuation 0
#define DEC_N      31                              // taps of the low pass between the FM's rate and the output's
#define REST       24                              // the six channels' DAC outputs when nothing sounds: 6 * (+4)
#define DAC_HZ     10653.0f                        // the DAC's sample rate (FM_HZ / 5)
#define DAC_BYTES  7500                            // of the 12 KB allowed

enum { EG_OFF, EG_ATT, EG_DEC, EG_SUS, EG_REL };
typedef struct {
    uint32_t ph, inc;                  // the phase (the chip's 20 bits in the top of 32) and its step per FM sample
    int32_t  g, dg, gt;                // the level now (24 bits), its step per FM sample, where it is going
    float    att;                      // the envelope's attenuation, 0..1023 (0.094 dB each)
    float    tl;                       // total level plus the part's level, in the same steps
    uint16_t sl;                       // the sustain level, same steps
    uint8_t  st, r_ar, r_d1, r_d2, r_rr;   // the state; the four rates (0..63) with the key scaling in them
} fmop_t;
typedef struct {
    fmop_t   op[4];
    int32_t  fb1, fb2, fbm;            // operator 1's last two outputs; the feedback's gain (0: none)
    int32_t  m21, m31, m32, m41, m42, m43, c1, c2, c3;   // the algorithm: who modulates whom, who is heard (0 or -1)
    uint8_t  live;                     // something of it can still be heard
    uint8_t  key;                      // key on
    uint8_t  alg, fb;
    uint16_t fnum; uint8_t block;
} fmch_t;
typedef struct { float ph, dt, vol; uint16_t n; uint8_t att; } psq_t;

// the algorithms as bits: op1>2, 1>3, 2>3, 1>4, 2>4, 3>4, and which of 1 2 3 are heard besides 4
static const uint16_t ALG[8] = {
    0x001 | 0x004 | 0x020,             // 0: 1 > 2 > 3 > 4
    0x002 | 0x004 | 0x020,             // 1: (1 + 2) > 3 > 4
    0x004 | 0x008 | 0x020,             // 2: (1 + (2 > 3)) > 4
    0x001 | 0x010 | 0x020,             // 3: ((1 > 2) + 3) > 4
    0x001 | 0x020 | 0x080,             // 4: (1 > 2) + (3 > 4)
    0x001 | 0x002 | 0x008 | 0x180,     // 5: 1 > (2, 3, 4)
    0x001 | 0x180,                     // 6: (1 > 2) + 3 + 4
    0x1C0,                             // 7: 1 + 2 + 3 + 4
};
static const uint8_t CARRIERS[8] = { 8, 8, 8, 8, 0xA, 0xE, 0xE, 0xF };   // bit k: operator k + 1 is heard
// The detune table: counts added to the phase step before the multiple, by detune (1..3) and key code (the chip's
// ROM, as in every OPN emulator). Detune 5..7 take them away.
static const uint8_t DT_TAB[3][32] = {
    { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 8, 8 },
    { 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 16, 16, 16 },
    { 2, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 8, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 22, 22, 22, 22 } };
static const uint8_t KC_LOW[16] = { 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 3, 3, 3, 3, 3, 3 };   // key code's low bits from fnum's top four
// the PSG's 16 levels, 2 dB apart, the last one silence
static DRAM_ATTR const float PSG_VOL[16] = { 1.0f, 0.7943f, 0.6310f, 0.5012f, 0.3981f, 0.3162f, 0.2512f, 0.1995f,
                                             0.1585f, 0.1259f, 0.1f, 0.0794f, 0.0631f, 0.0501f, 0.0398f, 0.0f };

// The low pass before every second FM sample is dropped: 31 taps, the first 16 (it is symmetric), in 1/32768. Flat
// to 10 kHz, -2.7 dB at 12 kHz, 42 dB down at 16 kHz and 63 dB from 16.5 kHz up, so what the FM makes above the
// output's range does not fold back into it. (Parks-McClellan; the sum is exactly 32768.)
static DRAM_ATTR const int16_t DEC[16] = { -22, -98, -82, 116, 281, 23, -506, -464, 513, 1222, 87, -2129, -1982, 2878, 9908, 13278 };
static DRAM_ATTR int16_t SIN[1024];    // the operator's sine at full level: +-8188, made of the chip's two tables
static float EGD[64], EGA[64];         // per envelope step: what a decay adds; what an attack multiplies by
static const uint8_t *DAC_SMP[4];      // kit 0 kick, snare; kit 1 kick, snare
static const uint16_t DAC_LEN[4] = { 1700, 2100, 1300, 2400 };

static struct {
    fmch_t   ch[5];
    int32_t  hist[DEC_N - 1];          // the last FM samples, for the filter that brings them down to 32 kHz
    int      eg_left;                  // output samples until the next envelope step
    // the DAC
    const uint8_t *dac; uint32_t dac_pos, dac_len, dac_ph; int32_t dac_cur, dac_lvl;
    // the PSG
    psq_t    sq[3];
    float    nz_ph, nz_dt, nz_vol, nz_out; uint16_t lfsr; uint8_t nz_att, nz_ctl;
    float    lp; dcb_t dc;
    // the driver
    uint32_t rng;
    uint8_t  part[5];
    uint8_t  chord_rot, chord_arp_i, chord_broken;
    int8_t   tom_age, hat_age; uint8_t hat_open, kit, tom_lvl, hat_lvl;
    uint8_t  echo_att, echo_on;
} S;

// One channel for n FM samples, added to the bus. Integer through and through, as the chip. An operator is the sine
// of (its phase + what modulates it) times its level; a modulator's 14 bits shift the phase by out / 2 of the sine's
// 1024 steps, which is out << 21 of the 32-bit phase. The three shapes do the same sums; the two that are not a chain
// just let the processor see that their operators do not wait for each other.
enum { SHAPE_CHAIN, SHAPE_PAIRS, SHAPE_FAN };              // algorithms 0-3; 4; 5-7
#define OP(p, mod, g) ((SIN[((p) + ((uint32_t)(mod) << 21)) >> 22] * ((g) >> 8)) >> 16)
static inline __attribute__((always_inline)) IRAM_ATTR void fm_loop(fmch_t *c, int32_t *bus, int n, const int shape)
{
    fmop_t *o = c->op;
    uint32_t p1 = o[0].ph, p2 = o[1].ph, p3 = o[2].ph, p4 = o[3].ph;
    const uint32_t i1 = o[0].inc, i2 = o[1].inc, i3 = o[2].inc, i4 = o[3].inc;
    int32_t g1 = o[0].g, g2 = o[1].g, g3 = o[2].g, g4 = o[3].g;
    const int32_t d1 = o[0].dg, d2 = o[1].dg, d3 = o[2].dg, d4 = o[3].dg;
    const int32_t m21 = c->m21, m31 = c->m31, m32 = c->m32, m41 = c->m41, m42 = c->m42, m43 = c->m43, c1 = c->c1, c2 = c->c2, c3 = c->c3;
    int32_t f1 = c->fb1, f2 = c->fb2;
    const int32_t fbm = c->fbm;
    for (int i = 0; i < n; i++) {
        p1 += i1; g1 += d1; p2 += i2; g2 += d2; p3 += i3; g3 += d3; p4 += i4; g4 += d4;
        int32_t o1 = (SIN[(p1 + (uint32_t)((f1 + f2) * fbm)) >> 22] * (g1 >> 8)) >> 16, o2, o3, o4, y;
        f2 = f1; f1 = o1;
        if (shape == SHAPE_PAIRS) {
            o2 = OP(p2, o1, g2); o3 = OP(p3, 0, g3); o4 = OP(p4, o3, g4);
            y = o2 + o4;
        } else if (shape == SHAPE_FAN) {
            o2 = OP(p2, o1 & m21, g2); o3 = OP(p3, o1 & m31, g3); o4 = OP(p4, o1 & m41, g4);
            y = o4 + (o1 & c1) + (o2 & c2) + (o3 & c3);
        } else {
            o2 = OP(p2, o1 & m21, g2); o3 = OP(p3, (o1 & m31) + (o2 & m32), g3); o4 = OP(p4, (o1 & m41) + (o2 & m42) + (o3 & m43), g4);
            y = o4;
        }
        y = y > 8191 ? 8191 : y < -8192 ? -8192 : y;       // the channel's 14 bits
        y >>= 5;                                           // the DAC's 9
        bus[i] += y >= 0 ? y + 4 : y - 3;                  // ... and its step at zero
    }
    o[0].ph = p1; o[1].ph = p2; o[2].ph = p3; o[3].ph = p4;
    o[0].g = g1; o[1].g = g2; o[2].g = g3; o[3].g = g4;
    c->fb1 = f1; c->fb2 = f2;
}
static IRAM_ATTR void fm_run(fmch_t *c, int32_t *bus, int n)
{
    if (c->alg == 4) fm_loop(c, bus, n, SHAPE_PAIRS);
    else if (c->alg > 4) fm_loop(c, bus, n, SHAPE_FAN);
    else fm_loop(c, bus, n, SHAPE_CHAIN);
}

static inline IRAM_ATTR int32_t gain_of(float total)                 // attenuation (64 steps halve it) to a 24-bit level
{
    return total >= 832.0f ? 0 : (int32_t)(16777216.0f * fast_exp2(total * (-1.0f / 64.0f)));   // 13 halvings: the chip's output is 0
}
// the envelopes, one step of EG_CHUNK output samples
static IRAM_ATTR void eg_step(void)
{
    for (int k = 0; k < 5; k++) {
        fmch_t *c = &S.ch[k];
        if (!c->live) continue;
        uint8_t live = 0;
        for (int j = 0; j < 4; j++) {
            fmop_t *o = &c->op[j];
            switch (o->st) {
            case EG_ATT:
                o->att = o->r_ar >= 62 ? 0.0f : (o->att + 1.0f) * EGA[o->r_ar] - 1.0f;
                if (o->att <= 0.0f) { o->att = 0.0f; o->st = EG_DEC; }
                break;
            case EG_DEC: o->att += EGD[o->r_d1]; if (o->att >= (float)o->sl) { o->att = (float)o->sl; o->st = EG_SUS; } break;
            case EG_SUS: o->att += EGD[o->r_d2]; if (o->att >= 1023.0f) { o->att = 1023.0f; o->st = EG_OFF; } break;
            case EG_REL: o->att += EGD[o->r_rr]; if (o->att >= 1023.0f) { o->att = 1023.0f; o->st = EG_OFF; } break;
            default: break;
            }
            int32_t gt = o->st == EG_OFF ? 0 : gain_of(o->att + o->tl);
            o->g = o->gt; o->dg = (gt - o->g) / EG_N; o->gt = gt;
            if (o->g | gt | (o->st != EG_OFF)) live = 1;
        }
        c->live = live;
    }
}

static IRAM_ATTR void render(float *out, int n)
{
    int32_t bus[DEC_N - 1 + EG_N];
    float lp = S.lp;
    while (n > 0) {
        int k = n < S.eg_left ? n : S.eg_left, n2 = k * FM_OS;
        // the six FM channels' DAC outputs, summed, at 64 kHz. A silent channel still shows its +4
        int32_t idle = S.dac ? 0 : 4;
        for (int c = 0; c < 5; c++) if (!S.ch[c].live) idle += 4;
        for (int i = 0; i < DEC_N - 1; i++) bus[i] = S.hist[i];
        for (int i = 0; i < n2; i++) bus[DEC_N - 1 + i] = idle;
        for (int c = 0; c < 5; c++) if (S.ch[c].live) fm_run(&S.ch[c], bus + DEC_N - 1, n2);
        if (S.dac) {
            // channel 6 as the DAC: a byte is held until the next. Where it changes, the FM sample is the mean of both
            for (int i = 0; i < n2; i++) {
                int32_t v = S.dac_cur;
                S.dac_ph += (uint32_t)(DAC_HZ / (float)(AUDIO_SR * FM_OS) * 65536.0f);
                if (S.dac_ph >= 65536u) {
                    S.dac_ph -= 65536u;
                    int32_t nw = 0;
                    if (++S.dac_pos < S.dac_len) nw = (((int32_t)S.dac[S.dac_pos] - 128) * S.dac_lvl) >> 7;   // 9 bits: the byte * 2, at the part's level
                    else S.dac = NULL;
                    v += (nw - v) * (int32_t)S.dac_ph / (int32_t)(DAC_HZ / (float)(AUDIO_SR * FM_OS) * 65536.0f);
                    S.dac_cur = nw;
                }
                bus[DEC_N - 1 + i] += v >= 0 ? v + 4 : v - 3;
            }
        }
        for (int i = 0; i < DEC_N - 1; i++) S.hist[i] = bus[n2 + i];
        for (int j = 0; j < k; j++) {
            // down to 32 kHz
            const int32_t *x = bus + 2 * j + 1;
            int32_t a = DEC[15] * x[15];
            for (int i = 0; i < 15; i++) a += DEC[i] * (x[i] + x[DEC_N - 1 - i]);
            // six channels at rest show 6 * 4: taken off here in whole numbers, so that rest is exactly zero
            float y = (float)(a - REST * 32768) * (FM_GAIN / 32768.0f);
            // the PSG
            float p = 0.0f;
            for (int q = 0; q < 2; q++) {
                psq_t *s = &S.sq[q];
                if (s->vol <= 0.0f) continue;
                s->ph += s->dt; if (s->ph >= 1.0f) s->ph -= 1.0f;
                float p2 = s->ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                p += ((s->ph < 0.5f ? 1.0f : -1.0f) + poly_blep(s->ph, s->dt) - poly_blep(p2, s->dt)) * s->vol;
            }
            if (S.nz_vol > 0.0f) {
                S.nz_ph += S.nz_dt;
                float w = S.nz_out;
                if (S.nz_ph >= 1.0f) {                         // the register shifts: white noise from taps 0 and 3
                    S.nz_ph -= 1.0f;
                    uint32_t r = S.lfsr, fbk = (S.nz_ctl & 4) ? ((r ^ (r >> 3)) & 1u) : (r & 1u);
                    r = (r >> 1) | (fbk << 15); S.lfsr = (uint16_t)r;
                    float nw = (r & 1u) ? 1.0f : -1.0f, d = S.nz_ph / S.nz_dt;
                    w += (nw - w) * (d > 1.0f ? 1.0f : d);     // the mean over the sample it changed in
                    S.nz_out = nw;
                }
                p += w * S.nz_vol;
            }
            y += p * PSG_GAIN;
            lp += 0.486f * (y - lp);                           // the Model 1's output filter: 1 - exp(-2 pi 3390 / 32000)
            y = dcb_run(&S.dc, lp);
            out[j] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
        }
        out += k; n -= k; S.eg_left -= k;
        if (S.eg_left <= 0) { eg_step(); S.eg_left = EG_CHUNK; }
    }
    S.lp = flush_denormal(lp); S.dc.y1 = flush_denormal(S.dc.y1);
}

// ---------------------------------------------------------------- the driver
// A voice, the registers of one channel. Operators are numbered as in the algorithm drawings (the chip's register
// order is 1 3 2 4). mul: the multiple (0: a half). dt: detune 0..7. tl: total level, 0.75 dB a step. ks: key scaling.
// ar d1r d2r: attack and the two decay rates 0..31. sl: sustain level, 3 dB a step. rr: release rate 0..15.
// The carriers' levels leave room: their sum stays inside the channel's 14 bits.
typedef struct { uint8_t mul, dt, tl, ks, ar, d1r, d2r, sl, rr; } vop_t;
typedef struct { uint8_t alg, fb; int8_t semi; vop_t op[4]; } voice_t;

static const voice_t BASS[4] = {
    // 0: the synth bass: two stacks, one whose modulator feeds itself, one whose modulator sits an octave up and dies fast
    { 4, 7, 0, { { 1, 0, 25, 1, 31, 12, 0, 2, 10 }, { 1, 0, 9, 1, 31, 6, 0, 1, 10 }, { 2, 0, 22, 2, 31, 19, 0, 8, 10 }, { 1, 0, 9, 1, 31, 9, 0, 2, 10 } } },
    // 1: a round bass: little modulation, a soft pluck
    { 4, 3, 0, { { 1, 0, 36, 1, 31, 10, 0, 2, 10 }, { 1, 0, 10, 1, 31, 8, 3, 1, 10 }, { 1, 0, 40, 1, 31, 16, 0, 6, 10 }, { 1, 0, 10, 1, 31, 10, 3, 2, 10 } } },
    // 2: a slap: a bright stack into the carrier beside a plain modulator
    { 2, 6, 0, { { 1, 0, 30, 1, 31, 12, 0, 3, 10 }, { 3, 0, 26, 2, 31, 20, 0, 10, 10 }, { 1, 0, 22, 2, 31, 17, 0, 6, 10 }, { 1, 0, 2, 1, 31, 9, 2, 2, 10 } } },
    // 3: an organ bass: three carriers, held
    { 6, 4, 0, { { 1, 0, 34, 0, 31, 0, 0, 0, 10 }, { 1, 0, 14, 0, 31, 0, 0, 0, 10 }, { 2, 0, 20, 0, 31, 6, 0, 3, 10 }, { 1, 0, 14, 0, 31, 0, 0, 0, 10 } } },
};
static const voice_t LEAD[4] = {
    // 0: the brass lead: one self-fed modulator into three carriers: the main one, a quieter one detuned against it
    // (it sways, it does not cancel), one an octave up
    { 5, 6, 0, { { 1, 0, 27, 1, 26, 9, 0, 2, 10 }, { 1, 0, 8, 1, 28, 4, 0, 1, 10 }, { 1, 3, 24, 1, 28, 4, 0, 1, 10 }, { 2, 0, 19, 1, 28, 6, 0, 2, 10 } } },
    // 1: a square: modulators at twice the pitch leave the odd harmonics
    { 4, 0, 0, { { 2, 0, 30, 1, 31, 5, 0, 1, 10 }, { 1, 0, 11, 1, 31, 3, 0, 0, 10 }, { 2, 0, 30, 1, 31, 5, 0, 1, 10 }, { 1, 2, 22, 1, 31, 3, 0, 0, 10 } } },
    // 2: a flute: nearly a sine with a little of the octave and the twelfth, a soft start with a short chiff
    { 6, 0, 0, { { 3, 0, 24, 1, 31, 22, 0, 15, 12 }, { 1, 0, 6, 0, 22, 6, 0, 1, 10 }, { 2, 0, 28, 0, 24, 8, 0, 3, 10 }, { 3, 0, 34, 0, 22, 4, 0, 1, 10 } } },
    // 3: a bell: a modulator at 3.5 times the pitch (the voice is played an octave down with multiples 7 and 2)
    { 4, 0, -12, { { 7, 0, 26, 2, 31, 13, 0, 5, 8 }, { 2, 0, 10, 2, 31, 11, 6, 4, 8 }, { 2, 0, 36, 1, 31, 12, 0, 4, 8 }, { 2, 3, 12, 1, 31, 10, 6, 3, 8 } } },
};
static const voice_t CHORD[4] = {
    // 0: the electric piano: a tine (a modulator 14 times up, soon gone) and a body
    { 4, 2, 0, { { 14, 0, 38, 2, 31, 20, 0, 10, 9 }, { 1, 0, 20, 1, 31, 12, 5, 3, 9 }, { 1, 0, 34, 1, 31, 12, 0, 4, 9 }, { 1, 3, 16, 1, 31, 10, 5, 2, 9 } } },
    // 1: strings: the brass's shape with a slow attack and less edge
    { 5, 6, 0, { { 1, 0, 26, 0, 18, 4, 0, 1, 9 }, { 1, 2, 21, 0, 17, 3, 0, 1, 9 }, { 1, 6, 28, 0, 17, 3, 0, 1, 9 }, { 2, 0, 33, 0, 17, 4, 0, 2, 9 } } },
    // 2: an organ: four carriers as drawbars
    { 7, 4, 0, { { 1, 0, 26, 0, 31, 0, 0, 0, 11 }, { 2, 0, 32, 0, 31, 0, 0, 0, 11 }, { 4, 0, 36, 0, 31, 4, 0, 3, 11 }, { 1, 2, 34, 0, 31, 0, 0, 0, 11 } } },
    // 3: a clavinet: bright, plucked, dying
    { 4, 5, 0, { { 3, 0, 24, 2, 31, 17, 0, 6, 10 }, { 1, 0, 15, 1, 31, 13, 6, 5, 10 }, { 1, 0, 28, 2, 31, 15, 0, 5, 10 }, { 2, 0, 20, 1, 31, 14, 6, 6, 10 } } },
};

// ---- init: the tables and the DAC's samples (libm is fine here)
typedef struct { float f0, f1, tf, ta, tone, nz, tn, hp, click, drive; } dsyn_t;
static const dsyn_t DAC_SYN[4] = {
    { 165, 48, 0.028f, 0.055f, 1.0f, 0.00f, 0.010f, 0.5f, 0.35f, 1.8f },     // kit 0 kick: deep, a little driven
    { 250, 185, 0.020f, 0.050f, 0.6f, 0.90f, 0.060f, 0.45f, 0.30f, 1.3f },   // kit 0 snare
    { 230, 62, 0.014f, 0.038f, 1.0f, 0.10f, 0.004f, 0.5f, 0.70f, 3.0f },     // kit 1 kick: short and hard
    { 200, 150, 0.030f, 0.085f, 0.5f, 1.00f, 0.110f, 0.30f, 0.40f, 2.0f },   // kit 1 snare: long, more noise
};
// a drum for the DAC: a sine falling from f0 to f1, noise less its lows, a click, an overdrive; unsigned bytes
static void dac_drum(uint8_t *dst, int n, const dsyn_t *p)
{
    float scale = 1.0f;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t r = 0x2612u;
        float ph = 0.0f, lp = 0.0f, pk = 1e-9f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / DAC_HZ, f = p->f1 + (p->f0 - p->f1) * expf(-t / p->tf);
            ph += f / DAC_HZ;
            float w = rng_bi(&r);
            lp += p->hp * (w - lp);
            float x = p->tone * sinf(TWO_PI * ph) * expf(-t / p->ta) + p->nz * (w - lp) * expf(-t / p->tn) + p->click * w * expf(-t / 0.0015f);
            if (p->drive > 0.0f) x = tanhf(x * p->drive);
            float e = (float)(n - i) / (0.08f * (float)n);
            if (e < 1.0f) x *= e;
            if (i < 3) x *= (float)i * (1.0f / 3.0f);
            if (pass) { int v = 128 + (int)lrintf(x * scale); dst[i] = (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v); }
            else if (fabsf(x) > pk) pk = fabsf(x);
        }
        scale = 126.0f / pk;
    }
}

static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.eg_left = EG_CHUNK; S.lfsr = 0x8000; S.rng = 0x2612u;
    for (int k = 0; k < 5; k++) {
        S.part[k] = 0xFF;
        for (int j = 0; j < 4; j++) { S.ch[k].op[j].att = 1023.0f; S.ch[k].op[j].tl = 1016.0f; }
    }
    S.tom_age = S.hat_age = -1;
    for (int q = 0; q < 3; q++) S.sq[q].att = 15;
    S.nz_att = 15; S.echo_att = 15;
    for (int i = 0; i < DEC_N - 1; i++) S.hist[i] = REST;  // every FM channel and the DAC rest at +4
}
static void init(void)
{
    if (!SIN[256]) {
        // The chip's sine: a quarter wave of 256 steps kept as its logarithm (12 bits, 256 to the octave), turned
        // back by a 256-step exponent table with a 10-bit mantissa: 13 bits and a sign
        for (int i = 0; i < 1024; i++) {
            int q = i & 255; if (i & 256) q = 255 - q;
            int ls = (int)lrintf(-log2f(sinf(((float)q + 0.5f) * (3.14159265f / 512.0f))) * 256.0f);
            int ex = (int)lrintf((powf(2.0f, (float)(255 - (ls & 255)) / 256.0f) - 1.0f) * 1024.0f);
            int v = ((ex | 0x400) << 2) >> (ls >> 8);
            SIN[i] = (int16_t)((i & 512) ? -v : v);
        }
        // The envelope: at rate r the chip adds, on average, (4 + r % 4) * 2^(r / 4 - 14) of its 1024 steps on each of
        // its clocks (never more than 8; nothing below rate 4). An attack takes (level + 1) * that / 16 away instead
        for (int r = 0; r < 64; r++) {
            float inc = r < 4 ? 0.0f : ldexpf((float)(4 + (r & 3)), (r >> 2) - 14);
            if (inc > 8.0f) inc = 8.0f;
            EGD[r] = inc * EG_CLOCKS; EGA[r] = powf(1.0f - inc * (1.0f / 16.0f), EG_CLOCKS);
        }
        uint8_t *m = app_alloc_fast(DAC_BYTES);
        for (int k = 0; k < 4 && m; k++) { dac_drum(m, DAC_LEN[k], &DAC_SYN[k]); DAC_SMP[k] = m; m += DAC_LEN[k]; }
    }
    reset();
}

static inline float log2_fast(float x)                     // for 0 < x: within 0.01
{
    union { float f; uint32_t i; } u = { .f = x };
    float e = (float)((int)(u.i >> 23) - 128);              // the mantissa's parabola below gives log2(m) + 1
    u.i = (u.i & 0x007FFFFFu) | 0x3F800000u;
    float m = u.f;
    return e + (-0.33582878f * m + 2.0f) * m - 0.65871759f;
}

// A channel's pitch: fnum and block for a note, then every operator's phase step with its multiple and detune, and
// its rates with the key scaling
static void fm_pitch(fmch_t *c, const voice_t *V, float note, float g)
{
    note += (float)V->semi;
    int blk = note < 12.0f ? 0 : (int)((note - 12.0f) * (1.0f / 12.0f));
    if (blk > 7) blk = 7;
    float ff = px_hz(note) * (2097152.0f / FM_HZ) / (float)(1 << blk);   // fnum = f * 2^21 / 53267 / 2^block
    uint32_t fn = px_glitch(ff > 2047.0f ? 2047u : (uint32_t)(ff + 0.5f), 11, g, &S.rng);
    c->fnum = (uint16_t)fn; c->block = (uint8_t)blk;
    int kc = (blk << 2) | KC_LOW[fn >> 7];
    int32_t base = (int32_t)((fn << blk) >> 1);
    for (int j = 0; j < 4; j++) {
        const vop_t *p = &V->op[j]; fmop_t *o = &c->op[j];
        int32_t v = base;
        if (p->dt & 3) v += (p->dt & 4) ? -(int32_t)DT_TAB[(p->dt & 3) - 1][kc] : (int32_t)DT_TAB[(p->dt & 3) - 1][kc];
        if (v < 0) v = 0;
        v = p->mul ? v * p->mul : v >> 1;
        // the chip adds v to 20 bits 53267 times a second; here 32 bits, 64000 times: v * 4096 * 53267 / 64000
        o->inc = (uint32_t)(((uint64_t)(uint32_t)v * (uint64_t)(4096.0f * 1024.0f * FM_HZ / (float)(AUDIO_SR * FM_OS))) >> 10);
        int rks = kc >> (3 - (p->ks & 3));
#define RATE(R) (uint8_t)((R) ? ((R) * 2 + rks > 63 ? 63 : (R) * 2 + rks) : 0)
        o->r_ar = RATE(p->ar); o->r_d1 = RATE(p->d1r); o->r_d2 = RATE(p->d2r); o->r_rr = RATE(p->rr * 2 + 1);
#undef RATE
        o->sl = (uint16_t)(p->sl == 15 ? 31 * 32 : p->sl * 32);
    }
}
// the levels: the voice's own, with the part's LEVEL and the accent on the carriers (and an accent opens the
// modulators a little: brighter)
static void fm_level(fmch_t *c, const voice_t *V, float level, bool accent, float g)
{
    int alg = (int)px_glitch(V->alg, 3, g * 0.3f, &S.rng), fb = (int)px_glitch(V->fb, 3, g * 0.3f, &S.rng);
    uint16_t a = ALG[alg];
    c->alg = (uint8_t)alg; c->fb = (uint8_t)fb;
    c->m21 = -(int32_t)(a & 1); c->m31 = -(int32_t)((a >> 1) & 1); c->m32 = -(int32_t)((a >> 2) & 1);
    c->m41 = -(int32_t)((a >> 3) & 1); c->m42 = -(int32_t)((a >> 4) & 1); c->m43 = -(int32_t)((a >> 5) & 1);
    c->c1 = -(int32_t)((a >> 6) & 1); c->c2 = -(int32_t)((a >> 7) & 1); c->c3 = -(int32_t)((a >> 8) & 1);
    c->fbm = fb ? 1 << (12 + fb) : 0;                      // (out1 + out2) >> (10 - fb) sine steps, in 32-bit phase
    int cut = level <= 0.0f ? 127 : (int)(8.03f * -log2_fast(level < 1.0f ? level : 1.0f) + 0.5f) + (accent ? 0 : 4);   // 0.75 dB steps
    for (int j = 0; j < 4; j++) {
        int tl = V->op[j].tl;
        if ((CARRIERS[alg] >> j) & 1) tl += cut; else if (accent) tl -= 3;
        tl = (int)px_glitch((uint32_t)(tl < 0 ? 0 : tl > 127 ? 127 : tl), 7, g * 0.3f, &S.rng);
        c->op[j].tl = (float)(tl * 8);
    }
}
static void fm_key(fmch_t *c, bool on)
{
    if (on) { for (int j = 0; j < 4; j++) { c->op[j].st = EG_ATT; c->op[j].ph = 0; } c->live = 1; c->fb1 = c->fb2 = 0; }
    else for (int j = 0; j < 4; j++) if (c->op[j].st != EG_OFF) c->op[j].st = EG_REL;
    c->key = on;
}
// a part on a channel for this tick
static void fm_part(int k, const voice_t *V, float note, bool on, bool trig, bool accent, float level, uint8_t part, float g)
{
    fmch_t *c = &S.ch[k];
    if (on) {
        fm_pitch(c, V, note, g); fm_level(c, V, level, accent, g);
        if (trig || !c->key) { fm_key(c, false); if (level > 0.0f) fm_key(c, true); }
        S.part[k] = part;
    } else {
        if (c->key) fm_key(c, false);
        if (!c->live) S.part[k] = 0xFF;
    }
}
static void psg_tone(int q, float hz, int att, float g)
{
    psq_t *s = &S.sq[q];
    uint32_t n = hz > 0.0f ? (uint32_t)(3579545.0f / 32.0f / hz + 0.5f) : 1023u;
    while (n > 1023u) n >>= 1;                             // too low for 10 bits: an octave up
    n = px_glitch(n < 1 ? 1 : n, 10, g, &S.rng);
    att = att < 0 ? 0 : att > 15 ? 15 : att;
    if (att < 15) att = (int)px_glitch((uint32_t)att, 4, g * 0.3f, &S.rng);    // (a voice that is off stays off)
    s->n = (uint16_t)(n < 1 ? 1 : n); s->att = (uint8_t)att;
    s->dt = 3579545.0f / 32.0f / (float)AUDIO_SR / (float)s->n;
    if (s->dt > 0.45f) s->dt = 0.45f;
    s->vol = PSG_VOL[att];
}
static inline int psg_cut(float level) { return level <= 0.0f ? 15 : (int)(3.01f * -log2_fast(level < 1.0f ? level : 1.0f) + 0.5f); }   // 2 dB steps

static void tick(const px_frame_t *f)
{
    float g = f->glitch;
    if (g > 0.0f) { S.rng ^= f->rng; if (!S.rng) S.rng = 1; }

    fm_part(0, &BASS[f->bass.sound & 3], f->bass.note, f->bass.on, f->bass.trig, f->bass.accent, f->bass.level, PART_BASS, g);
    fm_part(1, &LEAD[f->lead.sound & 3], f->lead.note, f->lead.on, f->lead.trig, f->lead.accent, f->lead.level, PART_LEAD, g);

    // the chord on FM 3-5
    const voice_t *C = &CHORD[f->chord.sound & 3];
    if (f->chord.on && f->chord.arp == ARP_SLOW) {
        // broken: each new note of the arpeggio takes the next channel and the others ring on
        if (f->chord.trig) { for (int k = 2; k < 5; k++) if (S.ch[k].key) fm_key(&S.ch[k], false); S.chord_rot = 0; }
        if (f->chord.trig || f->chord.arp_i != S.chord_arp_i || !S.chord_broken) {
            int k = 2 + S.chord_rot; S.chord_rot = (uint8_t)((S.chord_rot + 1) % 3);
            fm_part(k, C, f->chord.note, true, true, true, f->chord.level, PART_CHORDS, g);
        }
        for (int k = 2; k < 5; k++) if (S.ch[k].key) fm_level(&S.ch[k], C, f->chord.level, true, g);
        S.chord_arp_i = f->chord.arp_i; S.chord_broken = 1;
    } else {
        bool trig = f->chord.trig || S.chord_broken;
        for (int k = 0; k < 3; k++) {
            int i = f->chord.n == 4 && k == 2 ? 3 : k;     // four notes: root, third and seventh
            fm_part(2 + k, C, f->chord.notes[i], f->chord.on, trig, true, f->chord.level, PART_CHORDS, g);
        }
        S.chord_broken = 0;
    }

    // the drums: kick and snare on the DAC, the tom on PSG 2, the hats on the noise
    if (f->drums) {
        S.kit = f->kit & 1;
        int lvl = (int)(f->drum_level * (f->drum_accent ? 225.0f : 180.0f));
        if (f->drums & (DR_KICK | DR_SNARE)) {
            int d = S.kit * 2 + ((f->drums & DR_SNARE) ? 1 : 0);
            if (DAC_SMP[d] && lvl > 0) { S.dac = DAC_SMP[d]; S.dac_len = DAC_LEN[d]; S.dac_pos = 0; S.dac_ph = 0; S.dac_lvl = lvl; }
        }
        int cut = psg_cut(f->drum_level) + (f->drum_accent ? 0 : 1);
        if (f->drums & DR_TOM) { S.tom_age = 0; S.tom_lvl = (uint8_t)cut; }
        if (f->drums & (DR_HAT | DR_OPEN)) { S.hat_age = 0; S.hat_open = (f->drums & DR_OPEN) != 0; S.hat_lvl = (uint8_t)cut; }
    }
    if (S.dac && g > 0.0f && rng_uni(&S.rng) < g * 0.2f) S.dac_pos = rng_next(&S.rng) % S.dac_len;   // a bad contact
    if (S.tom_age >= 0) {
        // a square falling a little under two semitones a tick, 2 dB quieter every tick or two
        static const uint8_t TOM_ATT[2][12] = { { 0, 0, 1, 2, 3, 5, 7, 9, 11, 13, 15, 15 }, { 0, 0, 0, 1, 2, 3, 4, 6, 8, 10, 12, 14 } };
        int a = S.tom_age;
        float hz = (S.kit ? 250.0f : 370.0f) * fast_exp2(-(float)a * (S.kit ? 0.10f : 0.14f));
        int att = a < 12 ? TOM_ATT[S.kit][a] + S.tom_lvl : 15;
        psg_tone(1, hz, att, g);
        if (att >= 15 || S.tom_lvl >= 15) { S.tom_age = -1; S.sq[1].vol = 0.0f; S.sq[1].att = 15; } else S.tom_age++;
    }
    {   // the noise. Kit 0: white, clocked by tone 3 at a short period (bright); kit 1: white at 3579545 / 512 (dull)
        static const uint8_t HAT_ATT[2][16] = { { 1, 6, 12, 15 }, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15 } };
        int att = 15;
        if (S.hat_age >= 0) {
            int a = S.hat_age, n = S.hat_open ? 16 : 4;
            att = a < n ? HAT_ATT[S.hat_open][a] + S.hat_lvl : 15;
            if (att >= 15) S.hat_age = -1; else S.hat_age++;
        }
        if (att > 15) att = 15;
        if (att < 15) att = (int)px_glitch((uint32_t)att, 4, g * 0.3f, &S.rng);
        S.nz_ctl = S.kit ? 4 : 7;
        S.sq[2].n = (uint16_t)px_glitch(5, 10, g, &S.rng); S.sq[2].att = 15; S.sq[2].vol = 0.0f;
        uint32_t nn = (S.nz_ctl & 3) == 3 ? S.sq[2].n : 16u << (S.nz_ctl & 3);
        S.nz_dt = 3579545.0f / 32.0f / (float)AUDIO_SR / (float)(nn < 4 ? 4 : nn);
        S.nz_att = (uint8_t)att; S.nz_vol = PSG_VOL[att];
    }

    // PSG 1: the echo. It fades by the PSG's own steps
    if (f->echo.on && f->echo.level > 0.0f) {
        int a = f->echo.age, att = 2 + psg_cut(f->echo.level) + (a < 2 ? 0 : a < 5 ? 1 : a < 9 ? 2 : 3);
        psg_tone(0, px_hz(f->echo.note), att, g);
        S.echo_att = (uint8_t)(att > 15 ? 15 : att); S.echo_on = 1;
    } else if (S.echo_on) {
        if (S.echo_att < 13) { S.echo_att += 2; psg_tone(0, 3579545.0f / 32.0f / (float)S.sq[0].n, S.echo_att, g); }
        else { S.echo_on = 0; S.sq[0].vol = 0.0f; S.sq[0].att = 15; S.echo_att = 15; }
    }
}

static int view(px_vview_t *v, int max)
{
    if (max < 10) return 0;
    for (int k = 0; k < 5; k++) {
        const fmch_t *c = &S.ch[k];
        int32_t top = 0;
        for (int j = 0; j < 4; j++) if (((CARRIERS[c->alg & 7] >> j) & 1) && c->op[j].gt > top) top = c->op[j].gt;
        v[k].kind = top ? VW_FM : VW_OFF; v[k].part = top ? S.part[k] : 0xFF;
        v[k].hz = (float)c->fnum * (float)(1 << c->block) * (FM_HZ / 2097152.0f);
        v[k].level = (float)top * (1.0f / 16777216.0f);
        v[k].shape = (float)c->op[0].gt * (1.0f / 16777216.0f);            // the modulator's level: how bright
    }
    v[5] = (px_vview_t){ S.dac ? VW_SAMPLE : VW_OFF, S.dac ? PART_DRUMS : 0xFF, 0.0f, S.dac ? (float)S.dac_lvl * (1.0f / 256.0f) : 0.0f,
                         S.dac ? (float)S.dac_pos / (float)S.dac_len : 0.0f };
    for (int q = 0; q < 3; q++) {
        const psq_t *s = &S.sq[q];
        v[6 + q] = (px_vview_t){ s->vol > 0.0f ? VW_PULSE : VW_OFF, s->vol > 0.0f ? (q == 0 ? PART_LEAD : PART_DRUMS) : 0xFF,
                                 3579545.0f / 32.0f / (float)(s->n ? s->n : 1), s->vol, 0.5f };
    }
    v[9] = (px_vview_t){ S.nz_vol > 0.0f ? VW_NOISE : VW_OFF, S.nz_vol > 0.0f ? PART_DRUMS : 0xFF, 0.0f, S.nz_vol, 0.0f };
    return 10;
}

static const char *const NAMES[16] = { "BRASS", "SQUARE", "FLUTE", "BELL",   "SYNTH", "ROUND", "SLAP", "ORGAN",
                                       "E.PIANO", "STRINGS", "ORGAN", "CLAV",   "DEEP", "HARD", "DEEP", "HARD" };
const px_console_t con_mega = { "MEGA", "YM2612", "1988", 10, init, reset, tick, render, view, NAMES };
