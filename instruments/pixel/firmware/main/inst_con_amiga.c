// AMIGA: Paula (1985) and a small ProTracker.
//
//   channel 1  DRUMS   one sample at a time: a hit cuts the one before (snare over kick over tom over hats)
//   channel 2  BASS
//   channel 3  CHORDS  a whole chord sampled as one sound and played on the root (the module trick), or a chip wave
//                      arpeggiated by the tick; the lead's ECHO when no chord sounds
//   channel 4  LEAD
//
// The chip half is Paula as a module player meets it: four channels, each a pointer into 8-bit sample memory, a
// length, a loop, a 16-bit period and a volume of 0..64. A channel puts out a new byte every `period` ticks of the
// PAL colour clock (3546895 Hz), so a sample plays at 3546895 / period bytes a second, and holds it until the next:
// no interpolation. Those steps and their images are the Amiga's sound; here each step is band-limited (a two-point
// polynomial step) so that what lies above 16 kHz does not fold back. The four channels are summed (the Amiga is
// stereo, two a side; this instrument is mono) and go through the A500's fixed output filter (one pole at 4.4 kHz:
// 360 ohm and 0.1 uF) and its output capacitor. The "LED" filter is off, as nearly every module sets it.
//
// The driver half is the tracker: instruments are samples, a note is a period from ProTracker's table, slides and
// vibrato move the period in whole steps, the arpeggio changes it every tick, volume is set every tick (the volume
// slide), a chord can start past its beginning (sample offset). The table is ProTracker's tuning +1: with it, loops
// whose cycle is a power of two long (the usual chip waves) sit 5 to 8 cents under concert pitch instead of 15 to 18.
//
// The samples are made in init() (no recordings): chip waves drawn the plain way in three sizes (128, 32 and 8 bytes
// a cycle, for the octaves), a pulse lead "sampled" with its width moving inside the loop, a filter bass and an FM
// bass with an attack and a two-cycle loop, four chords (major, minor, diminished, and root-fifth-octave for sus and
// power chords) each as an attack that runs into a loop holding a whole number of cycles of every note, and two drum
// kits (one played at 16.6 kHz, one at 8.3 kHz). 47.9 KB from app_alloc_fast().
//
// Simplified: a new note starts at once (ProTracker and Paula need a moment: DMA off and on); a period change acts
// at the next byte, not when the running count ends; periods below 113 are not played (ProTracker's top note).
#include "pixel.h"
#include <string.h>

#define PAULA_HZ   3546895.0f                      // the PAL Amiga's colour clock: Paula counts periods in it
#define PAULA_STEP (PAULA_HZ / (float)AUDIO_SR)    // bytes per output sample at period 1
#define OUT_GAIN   0.30f                           // one channel at volume 64 with a full-scale sample

// ---------------------------------------------------------------- the samples
enum { W_SQ, W_PULSE, W_SAW, W_TRI };              // chip waves: three sizes each, SM_CHIP + 3 * wave + size
enum { SM_CHIP = 0, SM_LEAD = 12, SM_LEAD_HI, SM_BASS, SM_BASS_HI, SM_FMB, SM_FMB_HI, SM_MAJ, SM_MIN, SM_DIM, SM_FIFTH,
       SM_KIT, SM_N = SM_KIT + 2 * PX_DRUMS };
typedef struct {
    const int8_t *d;
    uint16_t len, loop;                            // bytes; the loop runs from `loop` to the end (loop == len: none)
    uint8_t  base;                                 // the MIDI note it sounds at the table's first period
} smp_t;
static smp_t SMP[SM_N];
static bool  smp_ok;

#define MEM_BYTES 49052
static const uint16_t KIT_LEN[2][PX_DRUMS] = { { 2400, 2800, 800, 4000, 3000 }, { 1300, 1700, 400, 2000, 1500 } };
static const uint16_t KIT_PERIOD[2] = { 213, 425 };   // kit 0 plays at C-3 (16652 Hz), kit 1 at C-2 (8346 Hz)

// ProTracker's period table, tuning +1: C-1 to B-3 (and the last once more, to slide towards)
static const uint16_t PT[37] = {
    850, 802, 757, 715, 674, 637, 601, 567, 535, 505, 477, 450,
    425, 401, 379, 357, 337, 318, 300, 284, 268, 253, 239, 225,
    213, 201, 189, 179, 169, 159, 150, 142, 134, 126, 119, 113, 113 };

// ---- making them (init: libm is fine here)
static int8_t to8(float x) { int v = (int)lrintf(x); return (int8_t)(v > 127 ? 127 : v < -127 ? -127 : v); }

// Additive: `nn` notes (cycles of cyc[] bytes), harmonics 1..nh each, harmonic h at level amp(h, t) where t runs 0..1
// over the attack and stays 1 in the loop (so the loop is exact: every partial fits it a whole number of times).
// The levels are worked out every 32 bytes and glide between. Two passes: the first finds the peak.
typedef float (*amp_fn)(int h, float t);
static void additive(int8_t *dst, int n, int loop, const float *cyc, int nn, int nh, amp_fn amp, float drive)
{
    enum { MAXP = 48 };
    float s[MAXP], c[MAXP], cw[MAXP], sw[MAXP], a[MAXP], da[MAXP], scale = 1.0f;
    int np = nn * nh;
    if (np > MAXP) return;
    for (int pass = 0; pass < 2; pass++) {
        float pk = 1e-9f;
        for (int k = 0; k < np; k++) {
            int h = k % nh + 1;
            float w = TWO_PI * (float)h / cyc[k / nh], ph = 2.4f * (float)(k * k % 7) + 0.9f * (float)(k / nh);   // spread phases: a lower crest
            if ((float)h * 2.0f >= cyc[k / nh]) w = 0.0f;                                                        // above the sample's own Nyquist: left out
            cw[k] = cosf(w); sw[k] = sinf(w); s[k] = w > 0.0f ? sinf(ph) : 0.0f; c[k] = w > 0.0f ? cosf(ph) : 0.0f;
            a[k] = amp(h, 0.0f) / (float)nn;
        }
        for (int i = 0; i < n; i++) {
            if ((i & 31) == 0) {
                float t = loop > 0 ? (float)(i + 32) / (float)loop : 1.0f;
                for (int k = 0; k < np; k++) da[k] = (amp(k % nh + 1, t > 1.0f ? 1.0f : t) / (float)nn - a[k]) * (1.0f / 32.0f);
            }
            float y = 0.0f;
            for (int k = 0; k < np; k++) {
                y += a[k] * s[k]; a[k] += da[k];
                float ns = s[k] * cw[k] + c[k] * sw[k]; c[k] = c[k] * cw[k] - s[k] * sw[k]; s[k] = ns;
            }
            if (pass) dst[i] = to8(124.0f * tanhf(y * scale) / tanhf(drive)); else if (fabsf(y) > pk) pk = fabsf(y);
        }
        scale = drive / pk;                                            // recorded hot: the peaks lean into the tape
    }
}
// a filter bass: a sawtooth's harmonics through a resonant low pass that closes from the 20th harmonic to the 3rd
static float amp_bass(int h, float t)
{
    float u = 1.0f - t, cut = 3.0f + 17.0f * u * u * u, r = (float)h / cut, r2 = r * r;
    return (1.0f / (float)h) * (1.0f + 1.2f * expf(-(r - 1.0f) * (r - 1.0f) * 6.0f)) / sqrtf(1.0f + r2 * r2);
}
// a chord's notes: bright at the start, settling to a string-like roll-off
static float amp_chord(int h, float t)
{
    float u = 1.0f - t;
    return powf((float)h, -1.0f - 0.9f * (1.0f - u * u)) * (0.75f + 0.25f * u);
}

// A drum: a sine that falls from f0 to f1 (time constant tf, dying with ta), noise through a high pass (hp: its
// one-pole coefficient at the sample's rate) dying with tn, a click, an overdrive. Two passes.
typedef struct { float f0, f1, tf, ta, tone, nz, tn, hp, click, drive; } dsyn_t;
static void drum(int8_t *dst, int n, float sr, const dsyn_t *p)
{
    float scale = 1.0f;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t r = 0xA500u;
        float ph = 0.0f, lp = 0.0f, lp2 = 0.0f, pk = 1e-9f;
        for (int i = 0; i < n; i++) {
            float t = (float)i / sr, f = p->f1 + (p->f0 - p->f1) * expf(-t / p->tf);
            ph += f / sr;
            float w = rng_bi(&r);
            lp += p->hp * (w - lp); float hi = w - lp;                 // noise less its low part, twice
            lp2 += p->hp * (hi - lp2); hi -= lp2;
            float x = p->tone * sinf(TWO_PI * ph) * expf(-t / p->ta) + p->nz * hi * expf(-t / p->tn) + p->click * w * expf(-t / 0.0015f);
            if (p->drive > 0.0f) x = tanhf(x * p->drive);
            float e = (float)(n - i) / (0.08f * (float)n);             // the last twelfth fades: it ends on zero
            if (e < 1.0f) x *= e;
            if (i < 4) x *= (float)i * 0.25f;
            if (pass) dst[i] = to8(x * scale); else if (fabsf(x) > pk) pk = fabsf(x);
        }
        scale = 124.0f / pk;
    }
}
static const dsyn_t DRUM_SYN[2][PX_DRUMS] = {
    {   // kit 0: a drum machine, clean
        { 170, 46, 0.030f, 0.060f, 1.0f, 0.00f, 0.010f, 0.5f, 0.30f, 1.6f },     // KICK
        { 260, 180, 0.020f, 0.045f, 0.7f, 0.80f, 0.050f, 0.35f, 0.30f, 1.4f },   // SNARE
        { 0, 0, 1, 1, 0.0f, 1.00f, 0.012f, 0.85f, 0.0f, 0.0f },                  // HAT
        { 0, 0, 1, 1, 0.0f, 1.00f, 0.075f, 0.85f, 0.0f, 0.0f },                  // OPEN
        { 260, 120, 0.070f, 0.070f, 1.0f, 0.10f, 0.010f, 0.4f, 0.15f, 0.0f },    // TOM
    },
    {   // kit 1: harder and lower, sampled at half the rate
        { 220, 58, 0.018f, 0.050f, 1.0f, 0.10f, 0.006f, 0.5f, 0.60f, 3.5f },
        { 210, 150, 0.030f, 0.060f, 0.6f, 1.00f, 0.075f, 0.6f, 0.50f, 1.8f },
        { 0, 0, 1, 1, 0.0f, 1.00f, 0.016f, 0.8f, 0.0f, 0.0f },
        { 0, 0, 1, 1, 0.0f, 1.00f, 0.090f, 0.8f, 0.0f, 0.0f },
        { 170, 75, 0.060f, 0.065f, 1.0f, 0.15f, 0.012f, 0.5f, 0.30f, 2.0f },
    },
};

static void make_samples(int8_t *m)
{
    // chip waves, as they were drawn in a tracker's sample editor
    static const uint8_t SIZE[3] = { 128, 32, 8 }, BASE[3] = { 24, 48, 72 };
    for (int w = 0; w < 4; w++) for (int z = 0; z < 3; z++) {
        int L = SIZE[z];
        for (int i = 0; i < L; i++) {
            float x = ((float)i + 0.5f) / (float)L;                    // 0..1 through the cycle
            m[i] = w == W_SQ ? (int8_t)(i < L / 2 ? 100 : -100)
                 : w == W_PULSE ? (int8_t)(i < L / 4 ? 120 : -40)
                 : w == W_SAW ? to8(254.0f * x - 127.0f)
                 : to8(127.0f * (x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x));
        }
        SMP[SM_CHIP + 3 * w + z] = (smp_t){ m, (uint16_t)L, 0, BASE[z] };
        m += L;
    }
    // the lead: a pulse whose width goes from 50 % to 16 % and back once in the loop, each byte the pulse's average
    // over its own time (as a sampler's input filter would have it). 192 cycles of 32 bytes, 512 of 8
    for (int z = 0; z < 2; z++) {
        int L = z ? 8 : 32, n = z ? 4096 : 6144, cycles = n / L;
        for (int cy = 0; cy < cycles; cy++) {
            float duty = 0.33f + 0.17f * cosf(TWO_PI * (float)cy / (float)cycles), edge = duty * (float)L;
            for (int i = 0; i < L; i++) {
                float hi = edge - (float)i; hi = hi > 1.0f ? 1.0f : hi < 0.0f ? 0.0f : hi;
                m[cy * L + i] = to8(72.0f * ((2.0f * hi - 1.0f) - (2.0f * duty - 1.0f)));
            }
        }
        SMP[SM_LEAD + z] = (smp_t){ m, (uint16_t)n, 0, (uint8_t)(z ? 72 : 48) };
        m += n;
    }
    // the filter bass: 14 cycles of attack, 2 of loop; and two octaves up
    for (int z = 0; z < 2; z++) {
        int L = z ? 32 : 128; float cyc = (float)L;
        additive(m, 16 * L, 14 * L, &cyc, 1, z ? 15 : 40, amp_bass, 1.6f);
        SMP[SM_BASS + z] = (smp_t){ m, (uint16_t)(16 * L), (uint16_t)(14 * L), (uint8_t)(z ? 48 : 24) };
        m += 16 * L;
    }
    // the FM bass: a sine modulated by a sine at the same pitch, the depth falling from 4.5 to 1.2 radians
    for (int z = 0; z < 2; z++) {
        int L = z ? 32 : 128, n = 16 * L, loop = 14 * L;
        for (int i = 0; i < n; i++) {
            float th = TWO_PI * (float)i / (float)L, u = i < loop ? 1.0f - (float)i / (float)loop : 0.0f;
            m[i] = to8(120.0f * sinf(th + (1.2f + 3.3f * u * u * u) * sinf(th)) * (i < 6 ? (float)i / 6.0f : 1.0f));
        }
        SMP[SM_FMB + z] = (smp_t){ m, (uint16_t)n, (uint16_t)loop, (uint8_t)(z ? 48 : 24) };
        m += n;
    }
    // The chords, the root 64 bytes a cycle. A loop must hold a whole number of cycles of every note, so the intervals
    // are ratios of whole numbers: major 50:63:75 (a loop of 50 root cycles) and minor 74:88:111, both within 2 cents
    // of equal temperament; diminished 5:6:7, which is not (16 cents wide, 17 narrow) but loops in 5; and root, fifth
    // and octave 2:3:4
    static const struct { uint8_t c[3]; uint16_t attack; } CH[4] = { { { 50, 63, 75 }, 1280 }, { { 74, 88, 111 }, 1024 }, { { 5, 6, 7 }, 1024 }, { { 2, 3, 4 }, 1408 } };
    for (int k = 0; k < 4; k++) {
        int loop = 64 * CH[k].c[0], n = CH[k].attack + loop;
        float cyc[3] = { 64.0f, (float)loop / (float)CH[k].c[1], (float)loop / (float)CH[k].c[2] };
        additive(m, n, CH[k].attack, cyc, 3, 14, amp_chord, 1.0f);
        SMP[SM_MAJ + k] = (smp_t){ m, (uint16_t)n, CH[k].attack, 36 };
        m += n;
    }
    for (int kit = 0; kit < 2; kit++) for (int d = 0; d < PX_DRUMS; d++) {
        int n = KIT_LEN[kit][d];
        drum(m, n, PAULA_HZ / (float)KIT_PERIOD[kit], &DRUM_SYN[kit][d]);
        SMP[SM_KIT + kit * PX_DRUMS + d] = (smp_t){ m, (uint16_t)n, (uint16_t)n, 0 };
        m += n;
    }
}

// ---------------------------------------------------------------- the chip
typedef struct {
    const int8_t *d;                   // the sample, NULL: the channel is off
    uint32_t pos, len, loop;
    float    ph, rate, irate;          // the position between two bytes; bytes per output sample; its inverse
    float    volf;                     // volume / 64 / 128
    int      cur;                      // the byte held now
    // registers, as the driver wrote them
    uint16_t period; uint8_t vol;
} pch_t;

static struct {
    pch_t    ch[4];
    float    z1, z2, c[4], lp;         // the mix two samples back and the step corrections to come; the output filter
    dcb_t    dc;
    // the driver
    uint32_t rng;
    struct { const smp_t *s; float v; uint8_t part, on, ins; } trk[4];   // per channel: the sample, the volume now (0..64)
    uint8_t  drum;
} S;

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

static IRAM_ATTR void render(float *out, int n)
{
    float lp = S.lp;
    for (int i = 0; i < n; i++) {
        float mix = 0.0f;
        for (int k = 0; k < 4; k++) {
            pch_t *c = &S.ch[k];
            if (!c->d) continue;
            c->ph += c->rate;
            if (c->ph >= 1.0f) {                   // Paula's counter ran out: the next byte
                c->ph -= 1.0f;
                int nw;
                if (++c->pos >= c->len) {
                    if (c->loop < c->len) c->pos = c->loop;
                    else c->pos = c->len;          // played once: it ends on zero (ProTracker loops two zero bytes)
                }
                nw = c->pos < c->len ? c->d[c->pos] : 0;
                float d = c->ph * c->irate;                           // how long ago, in output samples
                blep4(S.c, (float)(nw - c->cur) * c->volf, d > 1.0f ? 1.0f : d);
                c->cur = nw;
                if (c->pos >= c->len) c->d = NULL;
            }
            mix += (float)c->cur * c->volf;
        }
        float y = S.z2 + S.c[0];
        S.z2 = S.z1; S.z1 = mix; S.c[0] = S.c[1]; S.c[1] = S.c[2]; S.c[2] = S.c[3]; S.c[3] = 0.0f;
        // The A500's output filter is one pole at 4.4 kHz. The step correction above already rolls the top off
        // (3.6 dB at 8 kHz), so the pole here sits at 8.6 kHz: 1 - exp(-2 pi 8600 / 32000). Together they follow
        // the A500's curve within 1.5 dB up to 11 kHz and fall below it above (3 dB at 13 kHz, 6 dB at 15 kHz)
        lp += 0.815f * (y - lp);
        y = dcb_run(&S.dc, lp * OUT_GAIN);
        out[i] = y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y;
    }
    S.lp = flush_denormal(lp); S.dc.y1 = flush_denormal(S.dc.y1);
}

// ---------------------------------------------------------------- the driver
// An instrument: a sample for each range of octaves (a module would have three instruments), a volume by the note's
// age in ticks, then a slide down to a floor; after the note, a slide to nothing.
typedef struct {
    uint8_t smp[3];                    // lowest octaves first; 0xFF: none
    uint8_t vol[4], dec, sus;          // 0..64 for ticks 0..3, then down by `dec` a tick to `sus`
    uint8_t rel;                       // after the note: down by this a tick
    uint8_t chip;                      // a plain loop: the driver may change to another size in the middle of a note
} ains_t;
#define NO 0xFF
#define CHIP(w) { SM_CHIP + 3 * (w), SM_CHIP + 3 * (w) + 1, SM_CHIP + 3 * (w) + 2 }
static const ains_t LEAD[4] = {
    { { SM_LEAD, SM_LEAD_HI, NO }, { 50, 50, 47, 45 }, 0, 45, 8, 0 },            // 0: the sampled pulse lead
    { CHIP(W_SQ), { 46, 41, 38, 35 }, 1, 28, 6, 1 },                             // 1: a square chip wave
    { CHIP(W_SAW), { 52, 48, 46, 44 }, 0, 44, 7, 1 },                            // 2: a sawtooth chip wave
    { CHIP(W_TRI), { 64, 52, 42, 36 }, 2, 6, 3, 1 },                             // 3: a triangle that dies away: mallets
};
static const ains_t BASS[4] = {
    { { SM_BASS, SM_BASS_HI, NO }, { 64, 64, 62, 60 }, 1, 48, 9, 0 },            // 0: the filter bass
    { CHIP(W_SQ), { 44, 40, 37, 35 }, 1, 27, 7, 1 },                             // 1: a square chip wave
    { { SM_FMB, SM_FMB_HI, NO }, { 56, 56, 52, 50 }, 1, 38, 8, 0 },              // 2: the FM bass
    { CHIP(W_TRI), { 44, 44, 44, 44 }, 0, 44, 9, 1 },                            // 3: a triangle, low and plain
};
static const ains_t CHORD[4] = {
    { { SM_MAJ, NO, NO }, { 60, 58, 56, 54 }, 1, 42, 6, 0 },                     // 0: the sampled chord
    { CHIP(W_PULSE), { 30, 28, 27, 26 }, 0, 26, 5, 1 },                          // 1: a pulse chip wave, arpeggiated
    { { SM_MAJ, NO, NO }, { 12, 24, 36, 46 }, 0, 46, 3, 0 },                     // 2: the chord from its loop, faded in: a pad
    { CHIP(W_SAW), { 34, 32, 31, 30 }, 0, 30, 5, 1 },                            // 3: a sawtooth chip wave, arpeggiated
};

static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.rng = 0xA500u;
    for (int k = 0; k < 4; k++) S.trk[k].part = 0xFF;
}
static void init(void)
{
    if (!smp_ok) {
        int8_t *m = app_alloc_fast(MEM_BYTES);
        if (m) { make_samples(m); smp_ok = true; }
    }
    reset();
}

// the sample of an instrument for a note: the highest range that still has the note an octave into the table
static const smp_t *pick(const ains_t *I, float note)
{
    const smp_t *s = &SMP[I->smp[0]];
    for (int k = 1; k < 3 && I->smp[k] != NO; k++) if (note - (float)SMP[I->smp[k]].base >= 11.5f) s = &SMP[I->smp[k]];
    return s;
}
// the period for a note on a sample: between two notes of the table it is a whole number in between (a slide)
static uint32_t period_of(const smp_t *s, float note)
{
    float idx = note - (float)s->base;
    int oct = 0;
    while (idx < 0.0f && oct < 3) { idx += 12.0f; oct++; }             // under the table: Paula goes on where ProTracker stops
    while (idx > 35.0f) idx -= 12.0f;                                  // over it: an octave down
    if (idx < 0.0f) idx = 0.0f;
    int k = (int)idx;
    float p = (float)PT[k] + ((float)PT[k + 1] - (float)PT[k]) * (idx - (float)k);
    return (uint32_t)(p * (float)(1 << oct) + 0.5f);
}
static void pa_start(int k, const smp_t *s, uint32_t offset)
{
    pch_t *c = &S.ch[k];
    c->d = s->d; c->len = s->len; c->loop = s->loop;
    c->pos = offset - 1u; c->ph = 1.0f;                                // the first byte comes with the next sample
    S.trk[k].s = s;
}
static void pa_set(int k, uint32_t period, float vol, float g)
{
    pch_t *c = &S.ch[k];
    period = px_glitch(period, 11, g, &S.rng);
    uint32_t v = px_glitch((uint32_t)(vol + 0.5f), 7, g * 0.5f, &S.rng);
    if (v > 64) v = 64;                                                // as the chip: anything with bit 6 set is 64
    if (period < 113) period = 113;
    c->period = (uint16_t)period; c->vol = (uint8_t)v;
    c->rate = PAULA_STEP / (float)period; c->irate = (float)period * (1.0f / PAULA_STEP);
    c->volf = (float)v * (1.0f / (64.0f * 128.0f));
    if (g > 0.0f && c->d && rng_uni(&S.rng) < g * 0.15f) c->pos = rng_next(&S.rng) % c->len;   // a bad contact: the pointer jumps
}
// a note of an instrument on a channel
static void play(int k, const ains_t *I, float note, int age, bool trig, bool accent, float level, uint8_t part, float g)
{
    const smp_t *s = S.trk[k].s;
    if (trig || !s || S.trk[k].ins != part + 1 || !S.ch[k].d) { s = pick(I, note); pa_start(k, s, 0); }
    else if (I->chip) { const smp_t *w = pick(I, note); if (w != s) { s = w; pa_start(k, s, 0); } }
    int v = age < 4 ? I->vol[age] : I->vol[3] - (age - 3) * I->dec;
    if (v < I->sus) v = I->sus;
    S.trk[k].v = (float)v * level * (accent ? 1.0f : 0.8f);
    S.trk[k].on = 1; S.trk[k].part = part; S.trk[k].ins = (uint8_t)(part + 1);
    pa_set(k, period_of(s, note), S.trk[k].v, g);
}
// after the note: the volume slides down, and at nothing the channel stops
static void fade(int k, uint8_t rel, float g)
{
    if (!S.ch[k].d) { S.trk[k].part = 0xFF; S.trk[k].on = 0; return; }
    S.trk[k].on = 0;
    S.trk[k].v -= (float)rel;
    if (S.trk[k].v < 0.5f) { S.ch[k].d = NULL; S.ch[k].cur = 0; S.ch[k].vol = 0; S.trk[k].v = 0; S.trk[k].part = 0xFF; return; }
    pa_set(k, S.ch[k].period, S.trk[k].v, g);
}

static void tick(const px_frame_t *f)
{
    float g = f->glitch;
    if (g > 0.0f) { S.rng ^= f->rng; if (!S.rng) S.rng = 1; }
    if (!smp_ok) return;

    // channel 1: the drums
    if (f->drums) {
        int d = f->drums & DR_SNARE ? 1 : f->drums & DR_KICK ? 0 : f->drums & DR_TOM ? 4 : f->drums & DR_OPEN ? 3 : 2, kit = f->kit & 1;
        pa_start(0, &SMP[SM_KIT + kit * PX_DRUMS + d], 0);
        S.trk[0].v = 64.0f * f->drum_level * (f->drum_accent ? 1.0f : 0.8f);
        S.trk[0].part = PART_DRUMS; S.drum = (uint8_t)kit;
    }
    if (S.ch[0].d) pa_set(0, KIT_PERIOD[S.drum], S.trk[0].v, g); else S.trk[0].part = 0xFF;

    // channel 2: the bass
    const ains_t *B = &BASS[f->bass.sound & 3];
    if (f->bass.on) play(1, B, f->bass.note, f->bass.age, f->bass.trig, f->bass.accent, f->bass.level, PART_BASS, g);
    else fade(1, B->rel, g);

    // channel 4: the lead
    const ains_t *L = &LEAD[f->lead.sound & 3];
    if (f->lead.on) play(3, L, f->lead.note, f->lead.age, f->lead.trig, f->lead.accent, f->lead.level, PART_LEAD, g);
    else fade(3, L->rel, g);

    // channel 3: the chord, else the lead's echo
    const ains_t *C = &CHORD[f->chord.sound & 3];
    // the whole chord as one sample on the root, picked by its third and fifth; a sus or power chord gets root, fifth
    // and octave; a chord there is no sample of (augmented) is arpeggiated on the pulse wave instead
    int third = (int)(f->chord.notes[1] - f->chord.notes[0] + 0.5f), fifth = (int)(f->chord.notes[2] - f->chord.notes[0] + 0.5f);
    int cs = third == 4 && fifth == 7 ? SM_MAJ : third == 3 && fifth == 7 ? SM_MIN : third == 3 && fifth == 6 ? SM_DIM
           : (third == 5 && fifth == 7) || (third == 7 && fifth == 12) ? SM_FIFTH : -1;
    if (f->chord.on) {
        if (C->chip || cs < 0) play(2, C->chip ? C : &CHORD[1], f->chord.note, f->chord.age, f->chord.trig, true, f->chord.level, PART_CHORDS, g);
        else {
            const smp_t *s = &SMP[cs];
            if (f->chord.trig || S.trk[2].ins != PART_CHORDS + 1 || S.trk[2].s != s || !S.ch[2].d)
                pa_start(2, s, (f->chord.sound & 3) == 2 ? s->loop : 0);
            int age = f->chord.age, v = age < 4 ? C->vol[age] : C->vol[3] - (age - 3) * C->dec;
            if (v < C->sus) v = C->sus;
            S.trk[2].v = (float)v * f->chord.level;
            S.trk[2].on = 1; S.trk[2].part = PART_CHORDS; S.trk[2].ins = PART_CHORDS + 1;
            pa_set(2, period_of(s, f->chord.notes[0]), S.trk[2].v, g);
        }
    } else if (f->echo.on) {
        play(2, L, f->echo.note, f->echo.age, f->echo.trig, true, f->echo.level, PART_LEAD, g);
    } else fade(2, S.trk[2].ins == PART_CHORDS + 1 ? C->rel : L->rel, g);
}

static int view(px_vview_t *v, int max)
{
    if (max < 4) return 0;
    for (int k = 0; k < 4; k++) {
        const pch_t *c = &S.ch[k];
        const smp_t *s = S.trk[k].s;
        bool live = c->d && c->vol;
        v[k].kind = live ? VW_SAMPLE : VW_OFF;
        v[k].part = live ? S.trk[k].part : 0xFF;
        // the pitch: the table's first period is the sample's base note
        v[k].hz = live && s && s->base && c->period ? 440.0f * fast_exp2(((float)s->base - 69.0f) * (1.0f / 12.0f)) * 850.0f / (float)c->period : 0.0f;
        v[k].level = live ? (float)c->vol * (1.0f / 64.0f) : 0.0f;
        v[k].shape = live && c->len ? (float)c->pos / (float)c->len : 0.0f;
    }
    return 4;
}

static const char *const NAMES[16] = { "SAMPLED", "SQUARE", "SAW", "MALLET",   "FILTER", "SQUARE", "FM", "TRIANGLE",
                                       "SAMPLED", "ARP", "PAD", "SAW ARP",     "MACHINE", "LO-FI", "MACHINE", "LO-FI" };
const px_console_t con_amiga = { "AMIGA", "PAULA", "1985", 4, init, reset, tick, render, view, NAMES };
