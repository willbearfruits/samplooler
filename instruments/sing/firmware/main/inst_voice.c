// SING's voice engine (see sing.h): pitch tracking and pitch-synchronous overlap-add voices.
//
// Analysis, every block:
//   the input goes into a ring (X, stored twice over so any stretch reads straight), a low-passed copy for the
//   pitch marks (Y), and a copy decimated to 8 kHz (D).
//   Pitch: YIN on D, each lag comparing the newest 10 ms with the 10 ms that lag before them (so a voice is
//   judged on what it sings now; lags for 69..1330 Hz; the difference function from running energies and one dot
//   product per lag), the lags split over two blocks: an estimate every 4 ms. The first dip under 0.1 (else the
//   deepest; over 800 Hz the dip's floor is read from a parabola and 0.15 will do) is refined at 32 kHz (the
//   plain difference over 8 ms around four times the 8 kHz lag, then a parabola), then a median of the last
//   three, except that a new note already clear and loud is taken at once. Voiced needs a clear dip and some
//   level (also against the phrase's own peak, so a tail dies), with hysteresis both ways; a note changing under
//   a held voice is held through for up to 24 ms; a jump to half the pitch is taken only if the dip there is
//   about as clear (YIN's octave errors).
//   Pitch marks: one per period on the peaks of Y (the input low-passed at about twice the pitch, so each period
//   has one clear peak), each searched within a quarter period of where the last one predicts it, on the polarity
//   that has had the bigger peaks lately, and placed between samples by a parabola.
// Synthesis, every voice (the lead and seven extra):
//   a voice keeps the time of its next grain on the output's clock, between samples. A grain is a Hann window
//   two input periods wide around one mark, read at the formant ratio (faster = formants up, the window shorter
//   to match; at ratio 1, between samples by four-point Hermite weights worked out once per grain). Grains are
//   played block by block as the output passes through them (never a whole grain at once: the cost per block
//   stays even, and a grain only needs its input by the time each of its samples is played). The mark is the one
//   nearest the voice's input time (the grain's time minus the latency, minus the voice's own delay for the
//   choir) among those already found. The next grain comes one output period later: the voice's pitch, exact to
//   the hundredth of a cent. Grains closer together (higher) or wider (formants down) sum louder: each is scaled
//   by the square root of its overlap, so a shifted voice stays about as loud as sung.
//   With the lead's pitch untouched (no tune, no shift) its grains follow the marks themselves, so the lead is
//   the input exactly one latency late and mixes with the dry voice without combing.
//   Unvoiced (s, t, breath, silence) a grain is taken at the input time itself, 5 ms wide, spaced to add up to
//   exactly the input (the lead passes consonants untouched); the extra voices fade out then, and back in on the
//   next voiced sound.
// Latency: 16 ms (VOICE_LATENCY_MS) covers periods up to about 270 samples (115 Hz) with the formants as sung.
//   Below that, or with the formants moved down (longer grains), it grows: quickly while nothing voiced is heard
//   (a breath, a consonant, silence), and only a quarter sample per block while a voice sings (the dry voice's
//   pitch moves by less than 7 cents meanwhile). Until it has grown a grain takes the nearest mark already found
//   (a period older than ideal: the stream stays smooth). 40 ms at most. The dry voice (voice_dry_delayed) is
//   delayed by the same.
// Autotune (the lead): the pitch class to land on is chosen from a lightly smoothed pitch (a jump of more than
//   0.8 semitone at once) with a quarter-semitone hysteresis; the correction (target minus what is sung) is pulled
//   in at the speed set (0 = at once, the robot); keep_vib leaves the singer's vibrato (the pitch minus its 3 Hz
//   average) on top of the target.
// Cost (tools/dsp/run_voice.sh, this machine): about 4.5 us per 64-frame block with the lead, three harmonies and
//   four choir voices all sounding and the formants moved; memory: 43 KB from app_alloc, about 10 KB static.
#include "sing.h"
#include <string.h>

#define SR        AUDIO_SR
#define XN        4096u                 // input ring: 128 ms (stored twice over, so any stretch of it reads straight)
#define XM        (XN - 1u)
#define YN        1024u                 // the low-passed copy for the marks: 32 ms
#define YM        (YN - 1u)
#define DN        512u                  // decimated ring, 8 kHz: 64 ms
#define DM        (DN - 1u)
#define MN        256u                  // pitch marks
#define MM        (MN - 1u)
#define L0        (VOICE_LATENCY_MS * SR / 1000)
#define LMAX      1280.0f               // 40 ms
#define UNV       160                   // unvoiced grain half width: 5 ms
#define HIMAX     400                   // a grain's input half width at most (80 Hz)
#define HOMAX     900.0f                // ... and its output half width
#define NV        (1 + VOICE_EXTRA)
#define GMAX      16                    // grains sounding at once in one voice
#define YW        80                    // YIN window at 8 kHz: 10 ms
#define TMIN      6                     // lags at 8 kHz: 1333 Hz ..
#define TMAX      116                   // .. 69 Hz
#define RW        256                   // the 32 kHz refinement window: 8 ms
#define DEC       4

// ---------------------------------------------------------------- parameters from the UI task (seqlock)
static voice_params_t pin;
static volatile uint32_t pin_seq;
static voice_params_t P;                // the audio task's copy

void voice_set(const voice_params_t *p)
{
    __atomic_add_fetch(&pin_seq, 1, __ATOMIC_ACQ_REL);
    pin = *p;
    __atomic_add_fetch(&pin_seq, 1, __ATOMIC_ACQ_REL);
}
static void params_pickup(void)
{
    for (int tries = 0; tries < 3; tries++) {
        uint32_t s = __atomic_load_n(&pin_seq, __ATOMIC_ACQUIRE);
        if (s == 0 || (s & 1u)) continue;
        voice_params_t c = pin;
        if (s == __atomic_load_n(&pin_seq, __ATOMIC_ACQUIRE)) { P = c; return; }
    }
}

// ---------------------------------------------------------------- state
static float *X, *Y, *D, *tmp;                  // app_alloc'd
static uint32_t T;                               // frames since start (the next block's first)
static int      NB = AUDIO_BLOCK;                // frames in the block being processed
static uint32_t td;                              // decimated samples written
static int dec_ph;
static biquad_t aa1, aa2, ylp;
static float ylp_fc = -1;
static float env, env_pk;                        // input envelope, and its peak lately (1.5 s)

// tracker
static int   hop;                                // 0: first half of a frame next, 1: second
static float dbuf[TMAX + 2];
static float e_lag[TMAX + 2];                    // the energy of the window lagged by tau
static float f0_hist[3]; static int f0_n;
static float f0 = 0, conf = 0, pmark = 160;      // pitch, confidence, period in samples
static bool  voiced;
static int   unv_blocks;                         // blocks since voiced
static int   hang;                               // frames the voice has been held through a blurred dip

// marks
static uint32_t mk_t[MN]; static float mk_f[MN], mk_p[MN];   // a mark: whole sample, fraction (0..1), period
static uint32_t nmk;                             // marks found (index of the next)
static bool  mk_run;                             // tracking marks period to period
static uint32_t mk_last;                         // the last mark's time
static float pol_pos = 1, pol_neg = 0;           // peak sizes lately, per polarity
static int   pol = 1;

// notes
static float in_note, fast_note, center_note;
static int   target = -1;
static float corr;                               // the lead's pitch correction now, semitones
static float out_note = 60;
static float leff = (float)L0;                   // the latency now (samples)
static float s_shift, s_fr = 1, s_tune, s_kv, s_lead;
static float s_choir, s_spread, s_time, s_hum;
static float s_hg[3];
// one-pole coefficients per block (64 frames), worked out once
static float k20, k12, k_fast, k_center, k_drift, k_tdrift, k_delay, k_gup, k_gdn, k_sp = 1.0f, sp_last = -1.0f;

typedef struct {
    uint32_t c, a_i;                             // centre (output clock) and source centre (input clock), whole samples
    float    cf, a_f;                            // ... and their fractions
    float    ho, k, fr, g;                       // output half width, window table step, read ratio, gain
    int      j0, j1;                             // its first and last sample, from c
    uint32_t base;                               // read ratio 1: the source of j = 0, read between samples at a fixed
    float    hc[4];                              //   fraction (four-point Hermite weights)
    bool     fixed;
} grain_t;
typedef struct {
    bool     on;
    uint32_t si; float sf;                       // next grain's time (output clock)
    float    note, note_t;                       // its pitch now (MIDI), and where it glides to
    float    gain, gain_t;
    float    delay, delay_t;                     // input delay beyond the latency (samples)
    float    form;                               // its own formant offset (semitones)
    float    vib_ph, vib_hz;
    float    drift, drift_t, tdrift, tdrift_t;   // humanize: pitch (cents) and timing (samples)
    uint32_t rng;
    uint32_t hint;                               // the mark it used last (an index into the marks)
    bool     hint_ok, fresh;
    grain_t  gr[GMAX];
    int      ng;
} voice_t;
static voice_t V[NV];
static float dry[AUDIO_BLOCK];

#ifdef VOICE_TEST
int voice_test_solo = -1;                        // the harness: only this extra voice into extra[]
float voice_test_leff(void) { return leff; }
#endif

// ---------------------------------------------------------------- published state (seqlock)
static voice_state_t pub;
static volatile uint32_t pub_seq;
void voice_state(voice_state_t *s)
{
    for (int tries = 0; tries < 4; tries++) {
        uint32_t q = __atomic_load_n(&pub_seq, __ATOMIC_ACQUIRE);
        if (q & 1u) continue;
        *s = pub;
        if (q == __atomic_load_n(&pub_seq, __ATOMIC_ACQUIRE)) return;
    }
}
const float *voice_dry_delayed(void) { return dry; }

// ---------------------------------------------------------------- init
void voice_init(void)
{
    if (!X) {
        size_t n = 2 * XN + YN + DN + 1024;
        float *m = app_alloc(n * sizeof(float));
        if (!m) { app_log("voice: no memory"); return; }
        X = m; Y = X + 2 * XN; D = Y + YN; tmp = D + DN;
    }
    memset(X, 0, 2 * XN * sizeof(float)); memset(Y, 0, YN * sizeof(float)); memset(D, 0, DN * sizeof(float));
    T = 0; td = 0; dec_ph = 0; env = 0; env_pk = 0; hop = 0; f0_n = 0; f0 = 0; conf = 0; pmark = 160; voiced = false; unv_blocks = 1000; hang = 0;
    nmk = 0; mk_run = false; mk_last = 0; pol_pos = 1; pol_neg = 0; pol = 1;
    in_note = fast_note = center_note = 0; target = -1; corr = 0; out_note = 60; leff = (float)L0;
    s_shift = 0; s_fr = 1; s_tune = 0; s_kv = 0; s_lead = 0; s_choir = s_spread = s_time = s_hum = 0;
    for (int k = 0; k < 3; k++) s_hg[k] = 0;
    memset(V, 0, sizeof V);
    static const float form_off[VOICE_EXTRA] = { 0, 0, 0, 1.1f, -1.0f, 0.6f, -1.3f };
    static const float vib_hz[VOICE_EXTRA] = { 5.1f, 5.6f, 4.8f, 5.9f, 5.3f, 4.6f, 6.1f };
    for (int k = 0; k < NV; k++) {
        V[k].rng = 0x9e3779b9u * (uint32_t)(k + 1);
        V[k].fresh = true;
        if (k) { V[k].form = form_off[k - 1]; V[k].vib_hz = vib_hz[k - 1]; V[k].vib_ph = 0.13f * (float)k; }
    }
    memset(&pub, 0, sizeof pub);
    memset(dry, 0, sizeof dry);
    biquad_set(&aa1, BQ_LP, 3000.0f, 0.5412f, 0);       // fourth-order Butterworth before taking one in four
    biquad_set(&aa2, BQ_LP, 3000.0f, 1.3066f, 0);
    biquad_set(&ylp, BQ_LP, 600.0f, 0.707f, 0); ylp_fc = 600.0f;
    const float bs = (float)AUDIO_BLOCK / (float)SR;
    k20 = 1.0f - expf(-bs / 0.02f); k12 = 1.0f - expf(-bs / 0.012f);
    k_fast = 1.0f - expf(-bs * 6.2831853f * 12.0f); k_center = 1.0f - expf(-bs * 6.2831853f * 3.0f);
    k_drift = 1.0f - expf(-bs / 0.4f); k_tdrift = 1.0f - expf(-bs / 0.6f); k_delay = 1.0f - expf(-bs / 0.05f);
    k_gup = 1.0f - expf(-bs / 0.012f); k_gdn = 1.0f - expf(-bs / 0.02f);
    sp_last = -1.0f; k_sp = 1.0f;
    memset(&P, 0, sizeof P);
    P.lead_gain = 1;
}

// ---------------------------------------------------------------- notes and scales
static inline bool allowed(int note, uint16_t mask) { return !mask || (mask >> (((note % 12) + 12) % 12) & 1u); }
static int nearest_allowed(float note, uint16_t mask)
{
    int base = (int)floorf(note + 0.5f);
    if (!mask) return base;
    for (int d = 0; d <= 6; d++) {
        int a = base - d, b = base + d;
        bool ia = allowed(a, mask), ib = allowed(b, mask);
        if (ia && ib) return fabsf(note - (float)a) <= fabsf(note - (float)b) ? a : b;
        if (ia) return a;
        if (ib) return b;
    }
    return base;
}
// the n-th allowed note above (n > 0) or below (n < 0) a note, by pitch class
static int step_allowed(int note, int n, uint16_t mask)
{
    if (!mask) mask = 0xFFF;
    int x = note;
    while (n > 0) { x++; for (int g = 0; g < 12 && !allowed(x, mask); g++) x++; n--; }
    while (n < 0) { x--; for (int g = 0; g < 12 && !allowed(x, mask); g++) x--; n++; }
    return x;
}
static inline float note_hz(float note) { return 440.0f * fast_exp2((note - 69.0f) * (1.0f / 12.0f)); }

// ---------------------------------------------------------------- the tracker
// Every lag compares the newest YW samples with the YW that came tau before them (so a high voice is judged on
// the last few milliseconds, not on the oldest part of a window sized for the lowest): d(tau) = E(last) +
// E(tau before) - 2 r(tau), only the dot product costs per lag
static void yin_part(int lo, int hi)
{
    const float *b = tmp + TMAX;                       // the newest YW
    for (int tau = lo; tau <= hi; tau++) {
        const float *a = tmp + TMAX - tau;
        float r = 0;
        for (int j = 0; j < YW; j++) r += a[j] * b[j];
        float d = e_lag[0] + e_lag[tau] - 2.0f * r;
        dbuf[tau] = d > 0.0f ? d : 0.0f;
    }
}
static void yin_start(void)
{
    uint32_t w0 = td - (uint32_t)(YW + TMAX);
    for (int j = 0; j < YW + TMAX; j++) tmp[j] = D[(w0 + (uint32_t)j) & DM];
    float e = 0;
    for (int j = TMAX; j < TMAX + YW; j++) e += tmp[j] * tmp[j];
    e_lag[0] = e;
    for (int tau = 1; tau <= TMAX; tau++) {
        float in_ = tmp[TMAX - tau], out_ = tmp[TMAX - tau + YW];
        e += in_ * in_ - out_ * out_; e_lag[tau] = e;
    }
}

static void tracker_frame(void)
{
    // cumulative mean normalised difference, the first dip under 0.1 (else the deepest). Over 800 Hz the lag is
    // under 10 samples at 8 kHz and the true dip falls between two of them: the dip's floor is read from a
    // parabola through three, and 0.15 will do; a pitch found that way gives way to the octave below if the dip
    // there is far clearer (a strong second harmonic, not a high voice)
    float cum = 0, dn[TMAX + 2], cm[TMAX + 2];
    dn[0] = 1.0f; cm[0] = 0;
    for (int tau = 1; tau <= TMAX; tau++) { cum += dbuf[tau]; cm[tau] = cum; dn[tau] = cum > 1e-12f ? dbuf[tau] * (float)tau / cum : 1.0f; }
    int best = -1;
    float found = 1.0f;
    for (int tau = TMIN; tau <= TMAX; tau++) {
        float v = dn[tau], thr = 0.1f;
        if (tau <= 9) {
            thr = 0.15f;
            float a = dbuf[tau - 1], b = dbuf[tau], c = dbuf[tau + 1], den = a - 2.0f * b + c;
            if (b <= a && b <= c && den > 1e-12f && cm[tau] > 1e-12f) {
                float bi = b - (a - c) * (a - c) / (8.0f * den);
                v = (bi > 0.0f ? bi : 0.0f) * (float)tau / cm[tau];
            }
        }
        if (v < thr) { while (tau + 1 <= TMAX && dn[tau + 1] < dn[tau]) tau++; best = tau; found = v; break; }
    }
    if (best >= 0 && best <= 9 && found >= 0.1f && 2 * best + 1 <= TMAX) {
        int b2 = 2 * best;
        if (dn[b2 - 1] < dn[b2]) b2--; else if (dn[b2 + 1] < dn[b2]) b2++;
        if (dn[b2] < 0.3f * found) best = b2; else dn[best] = found;
    }
    if (best < 0) { best = TMIN; for (int tau = TMIN + 1; tau <= TMAX; tau++) if (dn[tau] < dn[best]) best = tau; }
    float dmin = dn[best];
    // a pitch an octave below the last one, from a dip only a little less clear, is the octave error YIN is known for
    if (voiced && f0 > 0) {
        float t_last = (float)SR / (float)DEC / f0;
        int b2 = best / 2;
        if (fabsf((float)best - 2.0f * t_last) < 0.12f * (float)best && b2 > TMIN && b2 < TMAX) {
            if (dn[b2 - 1] < dn[b2]) b2--; else if (dn[b2 + 1] < dn[b2]) b2++;
            if (dn[b2] < dmin + 0.12f) { best = b2; dmin = dn[best]; }
        }
    }
    // loud enough to be someone singing at the mic (on the board a quiet room's hum and the speaker's hiss sit near
    // -50 dBFS and are periodic enough to pass for a low voice), and not just the tail of the last note
    bool loud = env > (voiced ? 0.005f : 0.008f) && env > (voiced ? 0.03f : 0.05f) * env_pk;
    bool v = loud && dmin < (voiced ? 0.35f : 0.2f) && best > TMIN && best < TMAX;
    // a note changing under a held voice blurs the dip for a few frames: still loud and half periodic, the voice
    // holds its pitch for up to 24 ms (the grains go on, the extra voices do not drop out on every new note)
    if (!v && voiced && hang < 6 && env > 0.25f * env_pk && dmin < 0.6f) { hang++; conf = 1.0f - dmin; return; }
    if (!v) { voiced = false; f0_n = 0; conf = 1.0f - dmin; hang = 0; return; }
    hang = 0;
    float tf = (float)best;
    {
        float a = dn[best - 1], b = dn[best], c = dn[best + 1], den = a - 2.0f * b + c;
        if (den > 1e-9f) tf += 0.5f * (a - c) / den;
    }
    // refine at 32 kHz: the plain difference over 8 ms around four times the lag
    int c = (int)(tf * (float)DEC + 0.5f), lo = c - 4, hi = c + 4;
    if (lo < 20) lo = 20;
    uint32_t base = T + (uint32_t)NB - 1u - (uint32_t)(RW + hi);     // everything in the ring
    for (int j = 0; j < RW + hi + 1; j++) tmp[j] = X[(base + (uint32_t)j) & XM];
    const float *nb = tmp + hi + 1;                    // the newest RW, against the RW tau before them
    float e0 = 0;
    for (int j = 0; j < RW; j++) e0 += nb[j] * nb[j];
    float el = 0;
    for (int j = hi + 1 - lo; j < hi + 1 - lo + RW; j++) el += tmp[j] * tmp[j];
    float dd[16]; int nd = hi - lo + 1, bi = 0;
    for (int k = 0; k < nd; k++) {
        int tau = lo + k;
        const float *a = tmp + hi + 1 - tau;
        if (k) el += a[0] * a[0] - a[RW] * a[RW];
        float r = 0;
        for (int j = 0; j < RW; j++) r += a[j] * nb[j];
        dd[k] = e0 + el - 2.0f * r;
        if (dd[k] < dd[bi]) bi = k;
    }
    float t32 = (float)(lo + bi);
    if (bi > 0 && bi < nd - 1) {
        float a = dd[bi - 1], b = dd[bi], cc = dd[bi + 1], den = a - 2.0f * b + cc;
        if (den > 1e-12f) t32 += 0.5f * (a - cc) / den;
    }
    float fn = (float)SR / t32;
    // the median of the last three (a lone wrong frame never gets through); a new note that is already clear and
    // loud is taken at once (a sung jump, not a glitch: the hard tune's step must not wait two frames)
    if (!voiced) f0_n = 0;
    if (voiced && f0 > 0 && dmin < 0.1f && fabsf(fn - f0) > 0.04f * f0 && env > 0.2f * env_pk) { f0_hist[0] = f0_hist[1] = f0_hist[2] = fn; f0_n = 3; }
    f0_hist[f0_n % 3] = fn; f0_n++;
    float m = fn;
    if (f0_n >= 3) {
        float a = f0_hist[0], b = f0_hist[1], cc = f0_hist[2];
        m = a > b ? (b > cc ? b : (a > cc ? cc : a)) : (a > cc ? a : (b > cc ? cc : b));
    } else if (f0_n == 2) m = 0.5f * (f0_hist[0] + f0_hist[1]);
    f0 = m; conf = 1.0f - dmin; voiced = true;
    pmark = (float)SR / f0;
}

// ---------------------------------------------------------------- the marks
static void push_mark(uint32_t t, float p)
{
    // the peak between samples: a parabola through the three around it
    float a = Y[(t - 1u) & YM], b = Y[t & YM], c = Y[(t + 1u) & YM], den = a - 2.0f * b + c, f = 0;
    if (den != 0.0f) f = clampf(0.5f * (a - c) / den, -0.5f, 0.5f);
    if (f < 0.0f) { t--; f += 1.0f; }
    mk_t[nmk & MM] = t; mk_f[nmk & MM] = f; mk_p[nmk & MM] = p; nmk++; mk_last = t;
}

// the peak (on the polarity in use) between two absolute times, both in Y
static uint32_t peak(uint32_t a, uint32_t b, float *pos, float *neg)
{
    uint32_t bt = a; float bv = -1e30f, mx = 0, mn = 0;
    for (uint32_t t = a; (int32_t)(b - t) >= 0; t++) {
        float y = Y[t & YM];
        if (y > mx) mx = y;
        if (y < mn) mn = y;
        float v = (float)pol * y;
        if (v > bv) { bv = v; bt = t; }
    }
    *pos = mx; *neg = -mn;
    return bt;
}

static void marks(uint32_t t_avail)
{
    if (!voiced) { mk_run = false; return; }
    float p = pmark;
    int q = (int)(0.25f * p + 0.5f);
    float pp, pn;
    if (!mk_run) {                                   // seed: the biggest peak of the last whole period
        uint32_t b = t_avail - (uint32_t)q, a = b - (uint32_t)(p + 0.5f);
        uint32_t m = peak(a, b, &pp, &pn);
        int want = pn > pp ? -1 : 1;
        if (want != pol) { pol = want; m = peak(a, b, &pp, &pn); }
        pol_pos = pp; pol_neg = pn;
        push_mark(m, p);
        mk_run = true;
    }
    for (int guard = 0; guard < 16; guard++) {
        uint32_t c = mk_last + (uint32_t)(p + 0.5f);     // where the next is expected
        if ((int32_t)(t_avail - (c + (uint32_t)q)) < 0) break;
        uint32_t m = peak(c - (uint32_t)q, c + (uint32_t)q, &pp, &pn);
        // the polarity follows the bigger peaks, slowly
        pol_pos += 0.2f * (pp - pol_pos); pol_neg += 0.2f * (pn - pol_neg);
        if (pol > 0 && pol_neg > 1.4f * pol_pos) pol = -1;
        else if (pol < 0 && pol_pos > 1.4f * pol_neg) pol = 1;
        push_mark(m, p);
    }
}

// the found mark nearest input time u (relative to ref); its ring index, or -1 if none is within 1.5 periods
static int pick_mark(voice_t *v, float u_rel, uint32_t ref)
{
    if (nmk == 0) return -1;
    uint32_t first = nmk > MN ? nmk - MN + 4u : 0u, last = nmk - 1u;
    uint32_t k = v->hint_ok ? v->hint : last;
    if (k < first || k > last) k = last;
    #define REL(i) ((float)(int32_t)(mk_t[(i) & MM] - ref))
    while (k + 1 <= last && fabsf(REL(k + 1) - u_rel) <= fabsf(REL(k) - u_rel)) k++;
    while (k > first && fabsf(REL(k - 1) - u_rel) < fabsf(REL(k) - u_rel)) k--;
    v->hint = k; v->hint_ok = true;
    if (fabsf(REL(k) - u_rel) > 1.5f * mk_p[k & MM]) return -1;
    #undef REL
    return (int)(k & MM);
}

// ---------------------------------------------------------------- grains
static void add_grain(voice_t *v, uint32_t c, float cf, uint32_t a_i, float a_f, int hi, float fr, float g)
{
    if (v->ng >= GMAX) return;
    float ho = (float)hi / fr;
    if (ho > HOMAX) ho = HOMAX;
    grain_t *G = &v->gr[v->ng++];
    G->c = c; G->cf = cf; G->a_i = a_i; G->a_f = a_f; G->ho = ho; G->fr = fr; G->g = g;
    G->k = 1.0f / ho;
    // the samples whose distance from the true centre is under the half width
    float lo = cf - ho, hi_ = cf + ho;
    G->j0 = (int)(lo + 65536.0f) - 65536 + 1;
    G->j1 = (int)(hi_ + 65536.0f) - 65536;
    if ((float)G->j1 >= hi_) G->j1--;
    G->fixed = fabsf(fr - 1.0f) < 1e-4f;
    if (G->fixed) {
        float s = a_f - cf;                          // source = a_i + j + s
        int si = (int)(s + 65536.0f) - 65536;
        float f = s - (float)si;
        G->base = a_i + (uint32_t)si;
        if (f < 1e-4f) { G->hc[0] = 0; G->hc[1] = 1; G->hc[2] = 0; G->hc[3] = 0; }
        else {
            float f2 = f * f, f3 = f2 * f;
            G->hc[0] = -0.5f * f3 + f2 - 0.5f * f;
            G->hc[1] = 1.5f * f3 - 2.5f * f2 + 1.0f;
            G->hc[2] = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
            G->hc[3] = 0.5f * f3 - 0.5f * f2;
        }
    }
}

// The Hann window, 0.5 + 0.5 cos(pi u) = cos(pi u / 2)^2 for |u| < 1, with the cosine as an even quartic that is
// exact at 0 and 1 (within 0.2 %): no table, no gathering, and the loops below vectorise.
static inline float hann(float u) { float u2 = u * u, c = 1.0f + u2 * (-1.2337f + 0.2337f * u2); return c * c; }

// what a voice's grains add to out[] in the block [T, T + n); finished grains go
static void render(voice_t *v, float *out, int n)
{
    for (int q = 0; q < v->ng;) {
        grain_t *G = &v->gr[q];
        int32_t off = (int32_t)(T - G->c);              // j of the block's first sample
        int j0 = G->j0 > off ? G->j0 : off, j1 = G->j1 < off + n - 1 ? G->j1 : off + n - 1;
        const float g = G->g, k = G->k, cf = G->cf;
        float *o = out + (j0 - off);
        int cnt = j1 - j0 + 1;
        if (cnt > 0 && G->fixed) {
            const float h0 = G->hc[0], h1 = G->hc[1], h2 = G->hc[2], h3 = G->hc[3];
            const float *xs = X + ((G->base + (uint32_t)j0) & XM);     // straight through the mirror
            float x = (float)j0 - cf;
            if (h1 == 1.0f) {
                for (int i = 0; i < cnt; i++) o[i] += g * hann((x + (float)i) * k) * xs[i];
            } else {
                uint32_t i0 = (G->base + (uint32_t)j0) & XM;   // from the sample before: at 0 that is the first copy's last
                xs = i0 ? xs - 1 : X + XN - 1;
                for (int i = 0; i < cnt; i++) o[i] += g * hann((x + (float)i) * k) * (h0 * xs[i] + h1 * xs[i + 1] + h2 * xs[i + 2] + h3 * xs[i + 3]);
            }
        } else if (cnt > 0) {
            const float fr = G->fr;
            float x = (float)j0 - cf, p = G->a_f + x * fr;
            for (int i = 0; i < cnt; i++, x += 1.0f, p += fr) {
                int ip = (int)(p + 65536.0f) - 65536;      // floor
                float f = p - (float)ip;
                const float *xs = X + ((G->a_i + (uint32_t)ip) & XM);
                o[i] += g * hann(x * k) * (xs[0] + f * (xs[1] - xs[0]));
            }
        }
        if (G->j1 <= off + n - 1) v->gr[q] = v->gr[--v->ng];   // its last sample has been played
        else q++;
    }
}

// start every grain of voice k that begins in this block; sync: the lead's pitch is untouched, follow the marks
static void schedule(int k, float note, float fr, float g, float delay, bool sync)
{
    voice_t *v = &V[k];
    uint32_t t_end = T + (uint32_t)NB;
    float hg = voiced ? (pmark < (float)HIMAX ? pmark : (float)HIMAX) : (float)UNV;
    float hog = hg / fr;
    if (hog > HOMAX) hog = HOMAX;
    if (!v->on) { v->si = T + (uint32_t)(hog + 1.0f); v->sf = 0; v->on = true; v->hint_ok = false; v->ng = 0; }
    // a voice that fell behind (it could not start grains for a while) starts again from now
    if ((int32_t)(v->si - T) < -(int32_t)HOMAX) { v->si = T + (uint32_t)(hog + 1.0f); v->sf = 0; v->hint_ok = false; }
    for (int guard = 0; guard < 32; guard++) {
        // following the marks: the grain after the last one sits on the next mark, one latency later
        if (sync && v->hint_ok && v->hint + 1u < nmk) {
            uint32_t nx = (v->hint + 1u) & MM;
            float lf = floorf(leff), cfr = mk_f[nx] + (leff - lf);
            uint32_t cm = mk_t[nx] + (uint32_t)(int)lf;
            if (cfr >= 1.0f) { cm++; cfr -= 1.0f; }
            int32_t dc = (int32_t)(cm - v->si);
            if (dc > -(int32_t)(hg * 0.5f) && dc < (int32_t)(hg * 0.5f)) { v->si = cm; v->sf = cfr; }
        }
        uint32_t c = v->si;
        float c_rel = (float)(int32_t)(c - t_end) + v->sf;
        if (c_rel - hog >= 0.0f) break;                 // it starts in a later block
        float u_rel = c_rel - leff - delay - v->tdrift;
        int m = pick_mark(v, u_rel, t_end);
        float spacing;
        if (m >= 0) {
            float pm = mk_p[m];
            int hi = pm < (float)HIMAX ? (int)(pm + 0.5f) : HIMAX;
            spacing = (float)SR / note_hz(note);
            // grains closer together (higher) or wider (formants down) overlap more and sum louder: the level is
            // kept about as sung (the square root of the overlap)
            float comp = sqrtf(clampf(fr * spacing / pm, 0.36f, 2.25f));
            add_grain(v, c, v->sf, mk_t[m], mk_f[m], hi, fr, g * comp);   // (a full voice drops it, keeping its time)
        } else {
            // unvoiced: the grain at the input time itself, spaced to add up to the input exactly
            float a = u_rel;
            float ai = floorf(a);
            add_grain(v, c, v->sf, t_end + (uint32_t)(int32_t)ai, a - ai, UNV, fr, g);
            spacing = (float)UNV / fr;
            v->hint_ok = false;
        }
        if (spacing < 24.0f) spacing = 24.0f;
        if (spacing > 1200.0f) spacing = 1200.0f;
        float s2 = v->sf + spacing;
        int si = (int)s2;
        v->si += (uint32_t)si; v->sf = s2 - (float)si;
    }
}

// ---------------------------------------------------------------- the block
void voice_process(const float *in, float *lead, float *extra, int n)
{
    if (n > AUDIO_BLOCK) {                               // the core's blocks are 64 frames; anything longer goes in 64s
        for (int o = 0; o < n; o += AUDIO_BLOCK) voice_process(in + o, lead + o, extra + o, n - o < AUDIO_BLOCK ? n - o : AUDIO_BLOCK);
        return;
    }
    memset(lead, 0, (size_t)n * sizeof(float)); memset(extra, 0, (size_t)n * sizeof(float));
    if (!X || n <= 0) return;
    NB = n;
    params_pickup();
    const float bs = (float)n / (float)SR;               // seconds per block
    // ---- input: the ring, the envelope, the 8 kHz copy, the low-passed copy for the marks
    if (voiced && f0 > 0) {
        float fc = clampf(2.0f * f0, 150.0f, 1100.0f);
        if (fabsf(fc - ylp_fc) > 0.03f * ylp_fc) { float z1 = ylp.z1, z2 = ylp.z2; biquad_set(&ylp, BQ_LP, fc, 0.707f, 0); ylp.z1 = z1; ylp.z2 = z2; ylp_fc = fc; }
    }
    for (int i = 0; i < n; i++) {
        float x = in[i];
        uint32_t t = T + (uint32_t)i;
        X[t & XM] = x; X[(t & XM) + XN] = x;            // the mirror: X[i .. i + XN) straight for any i < XN
        Y[t & YM] = biquad_run(&ylp, x);
        float a = fabsf(x);
        env += (a > env ? 0.25f : 0.0031f) * (a - env);  // 0.1 ms up, 10 ms down
        env_pk = env > env_pk ? env : env_pk * 0.99998f;
        float d = biquad_run(&aa2, biquad_run(&aa1, x));
        if (++dec_ph >= DEC) { dec_ph = 0; D[td & DM] = d; td++; }
    }
    aa1.z1 = flush_denormal(aa1.z1); aa1.z2 = flush_denormal(aa1.z2);
    aa2.z1 = flush_denormal(aa2.z1); aa2.z2 = flush_denormal(aa2.z2);
    ylp.z1 = flush_denormal(ylp.z1); ylp.z2 = flush_denormal(ylp.z2);
    env = flush_denormal(env);
    uint32_t t_avail = T + (uint32_t)n - 1u;

    // ---- the pitch: a frame every two blocks, its lags split between them
    if (hop == 0) { yin_start(); yin_part(1, TMAX / 2); hop = 1; }
    else {
        yin_part(TMAX / 2 + 1, TMAX);
        tracker_frame();
        hop = 0;
        if (voiced) {
            float nn = 69.0f + 12.0f * log2f(f0 / 440.0f);
            if (in_note <= 0.0f || unv_blocks > 4) fast_note = center_note = nn;
            in_note = nn;
        }
    }
    unv_blocks = voiced ? 0 : unv_blocks + 1;
    marks(t_avail);

    // ---- params, smoothed per block
    const float ka = k20;
    float fr_t = fast_exp2(clampf(P.formant, -12.0f, 12.0f) * (1.0f / 12.0f));
    s_fr += ka * (fr_t - s_fr);
    s_shift += k12 * (clampf(P.shift, -24.0f, 24.0f) - s_shift);
    s_tune += ka * (clampf(P.tune, 0.0f, 1.0f) - s_tune);
    s_kv += ka * (clampf(P.keep_vib, 0.0f, 1.0f) - s_kv);
    s_lead += ka * (clampf(P.lead_gain, 0.0f, 1.0f) - s_lead);
    s_choir += ka * (clampf(P.choir, 0.0f, 1.0f) - s_choir);
    s_spread += ka * (clampf(P.choir_spread, 0.0f, 40.0f) - s_spread);
    s_time += ka * (clampf(P.choir_time_ms, 0.0f, 60.0f) - s_time);
    s_hum += ka * (clampf(P.humanize, 0.0f, 1.0f) - s_hum);
    for (int k = 0; k < 3; k++) s_hg[k] += ka * (clampf(P.harm_gain[k], 0.0f, 1.0f) - s_hg[k]);
    float fr = clampf(s_fr, 0.5f, 2.0f);

    // ---- the lead's note: autotune
    if (voiced && in_note > 0.0f) {
        if (fabsf(in_note - fast_note) > 0.8f) fast_note = in_note;   // a new note, not a wobble: at once
        else fast_note += k_fast * (in_note - fast_note);
        center_note += k_center * (in_note - center_note);
        uint16_t mask = P.scale_mask & 0xFFFu;
        int cand = nearest_allowed(fast_note, mask);
        if (target < 0 || !allowed(target, mask) || (cand != target && fabsf(fast_note - (float)cand) + 0.25f < fabsf(fast_note - (float)target))) {
            target = cand;
            center_note = in_note;                            // a new note is not vibrato
        }
        float vib = clampf(in_note - center_note, -1.0f, 1.0f);
        float want = s_tune * ((float)target + s_kv * vib - in_note);
        if (P.speed_ms != sp_last) { sp_last = P.speed_ms; k_sp = sp_last < 0.5f ? 1.0f : 1.0f - expf(-bs * 1000.0f / sp_last); }
        corr += k_sp * (want - corr);
        out_note = in_note + corr + s_shift;
    }
    bool sync = fabsf(corr + s_shift) < 0.01f && fabsf(P.shift) < 0.01f && (P.tune <= 0.0f || s_tune < 0.001f);

    // ---- the latency: a grain's mark must be found by the time the grain starts
    {
        float p = voiced ? pmark : f0 > 0 ? (float)SR / f0 : 160.0f;
        float hi = p < (float)HIMAX ? p : (float)HIMAX;
        float want = clampf(hi / fr + 0.75f * p + 32.0f, (float)L0, LMAX);
        if (!voiced) leff = (float)(int)(leff + clampf(want - leff, -4.0f, 4.0f) + 0.5f);   // whole samples, unheard
        else if (want > leff) leff += clampf(want - leff, 0.0f, 0.25f);                      // a slow glide up
    }

    // ---- the extra voices: which notes, how loud
    int nh = 0;
    float hn[3] = { 0 };
    bool hact[3] = { false, false, false };
    if (P.harm_mode != HARM_OFF && target >= 0) {
        uint16_t sm = P.scale_mask & 0xFFFu, cm = P.chord_mask & 0xFFFu;
        for (int k = 0; k < 3; k++) {
            int iv = P.harm[k];
            if (!iv || P.harm_gain[k] <= 0.0f) continue;
            int tn;
            switch (P.harm_mode) {
            case HARM_SCALE: tn = step_allowed(target, iv, sm); break;
            case HARM_CHORD: tn = cm ? step_allowed(target, iv, cm) : step_allowed(target, 2 * iv, sm); break;
            default:         tn = target + iv; break;
            }
            hn[k] = (float)(tn - target); hact[k] = true; nh++;
        }
    }
    int free_n = VOICE_EXTRA - nh;
    int nc = P.choir > 0.0f ? (int)(P.choir * (float)free_n + 0.999f) : 0;
    if (nc > free_n) nc = free_n;
    float cg = nc ? (0.35f + 0.65f * s_choir) * 0.85f / sqrtf((float)nc) : 0.0f;
    static const float spread_pat[VOICE_EXTRA] = { 1.0f, -1.0f, 0.5f, -0.5f, 1.5f, -1.5f, 0.25f };
    int ci = 0;
    for (int k = 1; k < NV; k++) {
        voice_t *v = &V[k];
        int slot = k - 1;
        float iv = 0, g = 0, dl = 0, det = 0;
        bool act = false;
        if (slot < 3 && hact[slot]) { act = true; iv = hn[slot]; g = s_hg[slot]; dl = 8.0f * (float)(slot + 1); }
        else if (ci < nc) {
            int j = ci++;
            act = true; g = cg;
            det = s_spread * spread_pat[j];
            switch (P.choir_mode) {
            case CHOIR_OCTAVES: iv = (j & 1) ? -12.0f : 12.0f; if (j >= 4) iv *= 2.0f; break;
            case CHOIR_CHORD: {
                uint16_t cm = P.chord_mask & 0xFFFu;
                int step = j / 2 + 1, dir = (j & 1) ? -1 : 1;
                int tn = target >= 0 ? (cm ? step_allowed(target, dir * step, cm) : step_allowed(target, dir * 2 * step, P.scale_mask & 0xFFFu)) : 0;
                iv = target >= 0 ? (float)(tn - target) : 0.0f;
                break;
            }
            default: iv = 0; break;
            }
            dl = s_time * 0.001f * (float)SR * (float)(j + 1) / (float)nc * (0.7f + 0.075f * (float)((j * 7 + 3) % 5));
        }
        // humanize: a slow drift of pitch and timing, and each voice its own vibrato
        if (act) {
            if (rng_uni(&v->rng) < bs * 2.0f) { v->drift_t = 12.0f * s_hum * rng_bi(&v->rng); v->tdrift_t = 0.008f * (float)SR * s_hum * rng_uni(&v->rng); }
            v->drift += k_drift * (v->drift_t - v->drift);
            v->tdrift += k_tdrift * (v->tdrift_t - v->tdrift);
            v->vib_ph += v->vib_hz * bs; if (v->vib_ph >= 1.0f) v->vib_ph -= 1.0f;
            float vib = 25.0f * s_hum * sin_turns(v->vib_ph);
            v->note_t = out_note + iv + (det + v->drift + vib) * 0.01f;   // fading out, a voice keeps its note
        }
        v->gain_t = act && voiced ? g : 0.0f;
        v->delay_t = dl;
        if (v->fresh || v->gain < 0.002f) { v->note = v->note_t; v->delay = v->delay_t; v->fresh = false; }
        else v->note += k12 * (v->note_t - v->note);
        v->delay += k_delay * (v->delay_t - v->delay);
        v->gain += (v->gain_t > v->gain ? k_gup : k_gdn) * (v->gain_t - v->gain);
        if (v->gain_t == 0.0f && v->gain < 0.0005f && v->ng == 0) { v->gain = 0; v->on = false; }
    }
    V[0].note = out_note;

    // ---- grains: start the new ones, then play every one through this block
    if (s_lead > 0.0005f || P.lead_gain > 0.0f) schedule(0, out_note, fr, s_lead, 0.0f, sync);
    else if (V[0].ng == 0) V[0].on = false;
    render(&V[0], lead, n);
    for (int k = 1; k < NV; k++) {
        voice_t *v = &V[k];
        if (v->gain_t > 0.0f || v->gain >= 0.0005f) {
            float frk = clampf(fr * fast_exp2(v->form * (0.4f + 0.6f * s_hum) * (1.0f / 12.0f)), 0.5f, 2.0f);
            schedule(k, v->note, frk, v->gain, v->delay, false);
        }
        if (!v->ng) continue;
#ifdef VOICE_TEST
        if (voice_test_solo >= 0 && voice_test_solo != k - 1) { static float sink[AUDIO_BLOCK]; render(v, sink, n); continue; }
#endif
        render(v, extra, n);
    }

    // ---- the dry voice, as late as the voices (between samples while the latency glides)
    {
        float lf = leff, li = floorf(lf), f = lf - li;
        uint32_t ia = T - (uint32_t)(int)li;
        if (f < 1e-4f) for (int i = 0; i < n; i++) dry[i] = X[(ia + (uint32_t)i) & XM];
        else for (int i = 0; i < n; i++) { float x1 = X[(ia + (uint32_t)i) & XM], x0 = X[(ia + (uint32_t)i - 1u) & XM]; dry[i] = x1 + f * (x0 - x1); }
    }
    T += (uint32_t)n;

    // ---- what it hears, for the screen
    __atomic_add_fetch(&pub_seq, 1, __ATOMIC_ACQ_REL);
    pub.voiced = voiced; pub.f0 = voiced ? f0 : 0.0f; pub.level = clampf(env * 2.0f, 0.0f, 1.0f);
    pub.in_note = voiced ? in_note : 0.0f; pub.target = (float)(target >= 0 ? target : 0); pub.out_note = out_note;
    int nv = 0;
    for (int k = 1; k < NV; k++) {
        pub.vnote[k - 1] = V[k].note; pub.vlevel[k - 1] = V[k].gain;
        if (V[k].gain > 0.01f) nv++;
    }
    pub.nvoices = nv;
    __atomic_add_fetch(&pub_seq, 1, __ATOMIC_ACQ_REL);
}
