// The singing bowl (inst_score.h): a Tibetan bowl struck with a mallet. Six partials at the ratios measured on a real
// bowl (operational modal analysis of a copper-alloy singing bowl, NPUST: 121.1, 361.7, 700.0, 1123.4, 1614.1 and
// 2152.3 Hz, i.e. 1 : 2.99 : 5.78 : 9.28 : 13.33 : 17.77), each a pair of modes a few tenths of a percent apart (the
// bowl is never quite round: the pair beats, the bowl's "wah"). Each mode is a decaying phasor (a complex one-pole),
// so a strike only adds to what rings: several strikes build up, and a retune keeps the ringing whole. The strike
// pours its energy in over a millisecond or few, in step with each mode's own phase (a soft mallet takes longer and
// leaves the high partials quieter), with a short felt-on-metal tock. The fundamental rings for half a minute at full
// (BOWL RING), the high partials die sooner.
#include "mantra.h"
#include "params.h"
#include "spsc.h"
#include <string.h>

#define BP 6                         // partials
#define BM (2 * BP)                  // modes: each partial a pair
static const float ratio[BP] = { 1.0f, 2.987f, 5.780f, 9.277f, 13.329f, 17.773f };
static const float split[BP] = { 0.0060f, 0.0042f, 0.0050f, 0.0062f, 0.0045f, 0.0052f };   // the pair's spacing / f
static const float ring_k[BP] = { 1.0f, 0.62f, 0.40f, 0.26f, 0.17f, 0.12f };               // T60 against the fundamental's
static const float weight[BP] = { 1.0f, 0.60f, 0.38f, 0.24f, 0.15f, 0.10f };

typedef struct {
    uint32_t at;                     // the audio frame it lands on (0: now)
    int      na;                     // the attack's length (frames)
    float    cr[BM], ci[BM];         // each mode's rotation and decay per frame
    float    ur[BM], ui[BM];         // ... its rotation alone (the attack keeps in step with it)
    float    g[BM];                  // how much each mode gets
    float    tock, tock_lp;          // the mallet's noise and its tone
} strike_t;
SPSC(sq, strike_t, 8)
static sq_t q;

// UI side
static int   p_mbow = -2;           // the host's BOWL level (its tag, found once)
static float b_hz = 196.0f, b_t60 = 30.0f, b_hard = 0.4f;
static uint32_t rng_ui = 0x8badf00du;
// audio side
static float zr[BM], zi[BM], cr[BM], ci[BM], ur[BM], ui_[BM];
static float ar[BM], ai[BM];         // the attack's phasors (amplitude per frame)
static int   att_left;
static int   live;                   // modes still worth running: the first `live` (the high partials die first)
static float tock_env, tock_k, tock_z, tock_peak, tock_step;
static uint32_t rng_au = 0x1234567u;
static strike_t pend;
static bool  has_pend, ringing;
static float send_s;
static volatile float lvl_v;

void bowl_set(float hz, float t60, float hard)
{
    b_hz = clampf(hz, 40.0f, 1200.0f); b_t60 = clampf(t60, 1.0f, 90.0f); b_hard = clampf(hard, 0.0f, 1.0f);
    if (p_mbow == -2) p_mbow = param_by_tag("mbow");                 // the host's BOWL level also sets its echo
}

void bowl_strike_at(float strength, uint32_t frame)
{
    strike_t s;
    memset(&s, 0, sizeof s);
    float st = clampf(strength, 0.0f, 1.0f);
    float h = clampf(0.65f * b_hard + 0.35f * st, 0.0f, 1.0f);      // harder mallet, harder strike: brighter, quicker
    s.at = frame;
    s.na = (int)((0.0045f - 0.0037f * h) * (float)AUDIO_SR);         // 4.5 ms (felt, softly) .. 0.8 ms (wood, hard)
    float amp = 0.13f * st * (0.55f + 0.45f * st);
    float theta = rng_uni(&rng_ui) * 0.785f;                         // where on the rim: how the pairs share it
    for (int k = 0; k < BP; k++) {
        float f = b_hz * ratio[k];
        if (f > 0.45f * (float)AUDIO_SR) f = 0.45f * (float)AUDIO_SR;
        float t60 = b_t60 * ring_k[k];
        float r = expf(-6.9078f / (t60 * (float)AUDIO_SR));
        float c = cosf((float)(k + 2) * theta), share = 0.25f + 0.5f * c * c;   // never all in one of the pair
        float tilt = powf(0.25f + 0.75f * h, (float)k);
        for (int d = 0; d < 2; d++) {
            int m = 2 * k + d;
            float fm = f * (1.0f + (d ? 0.5f : -0.5f) * split[k]);
            float w = TWO_PI * fm / (float)AUDIO_SR;
            s.ur[m] = cosf(w); s.ui[m] = sinf(w);
            s.cr[m] = r * s.ur[m]; s.ci[m] = r * s.ui[m];
            s.g[m] = amp * weight[k] * tilt * (d ? share : 1.0f - share) / (float)(s.na > 0 ? s.na : 1);
        }
    }
    s.tock = 0.03f * st * (0.3f + 0.7f * h);
    s.tock_lp = onepole_hz(900.0f + 3500.0f * h);
    sq_push(&q, &s);
}
void bowl_strike(float strength) { bowl_strike_at(strength, 0); }
float bowl_level(void) { return clampf(lvl_v * 4.0f, 0.0f, 1.0f); }

static void start(const strike_t *s)
{
    for (int m = 0; m < BM; m++) {
        cr[m] = s->cr[m]; ci[m] = s->ci[m]; ur[m] = s->ur[m]; ui_[m] = s->ui[m];
        // in step with what rings (it builds up), or from the sine's zero
        float a = zr[m] * zr[m] + zi[m] * zi[m];
        float pr = 1.0f, pi = 0.0f;
        if (a > 1e-12f) { float n = 1.0f / sqrtf(a); pr = zr[m] * n; pi = zi[m] * n; }
        ar[m] = s->g[m] * pr; ai[m] = s->g[m] * pi;
    }
    att_left = s->na > 0 ? s->na : 1;
    tock_peak = s->tock; tock_step = s->tock / (float)att_left; tock_env = 0.0f; tock_k = s->tock_lp;
    ringing = true; live = BM;
}

static float run(float *out, float *send, int a, int b, float sg)
{
    float pk = 0.0f;
    const int nm = live;
    for (int i = a; i < b; i++) {
        float y = 0.0f;
        if (att_left > 0) {                                           // the mallet's contact: energy pours in
            for (int m = 0; m < nm; m++) {
                float r = zr[m] * cr[m] - zi[m] * ci[m] + ar[m], im = zr[m] * ci[m] + zi[m] * cr[m] + ai[m];
                zr[m] = r; zi[m] = im; y += im;
                float nr = ar[m] * ur[m] - ai[m] * ui_[m]; ai[m] = ar[m] * ui_[m] + ai[m] * ur[m]; ar[m] = nr;
            }
            att_left--;
        } else {
            for (int m = 0; m < nm; m++) {
                float r = zr[m] * cr[m] - zi[m] * ci[m], im = zr[m] * ci[m] + zi[m] * cr[m];
                zr[m] = r; zi[m] = im; y += im;
            }
        }
        if (tock_peak > 0.0f) {                                       // the tock rises with the contact, then dies
            tock_env += tock_step;
            if (tock_env >= tock_peak) { tock_env = tock_peak; tock_peak = 0.0f; }
        }
        if (tock_env > 1e-5f) {
            float nz = rng_bi(&rng_au) * tock_env;
            tock_z += tock_k * (nz - tock_z);
            y += tock_z;
            if (tock_peak <= 0.0f) tock_env *= 0.9965f;              // about 9 ms
        }
        out[i] += y; send[i] += y * sg;
        float ay = fabsf(y);
        if (ay > pk) pk = ay;
    }
    return pk;
}

void bowl_process(float *out, float *send, int n)
{
    float sg_to = 0.55f * (p_mbow >= 0 ? pv[p_mbow] * 0.01f : 1.0f);
    if (!has_pend && sq_pop(&q, &pend)) has_pend = true;
    int at = -1;
    if (has_pend) {
        int32_t d = (int32_t)(pend.at - audio_frames());
        if (pend.at == 0 || d < n) at = d < 0 || pend.at == 0 ? 0 : d;
        if (d < -AUDIO_SR) at = 0;                                    // long past: now
    }
    if (!ringing && at < 0) { lvl_v = flush_denormal(lvl_v * 0.9f); send_s = sg_to; return; }
    send_s += 0.05f * (sg_to - send_s);
    float pk = 0.0f;
    if (at >= 0) {
        if (ringing) pk = run(out, send, 0, at, send_s);
        start(&pend); has_pend = false;
        float p2 = run(out, send, at, n, send_s);
        if (p2 > pk) pk = p2;
    } else pk = run(out, send, 0, n, send_s);
    lvl_v = pk > lvl_v ? pk : lvl_v * 0.995f + pk * 0.005f;
    // silence costs nothing: a mode under -100 dB is dropped from the top down (the high partials go first, so a
    // long ring is soon three or four pairs, not six); once every mode is gone, stop
    if (att_left <= 0) {
        while (live > 0) {
            int m = live - 1;
            if (zr[m] * zr[m] + zi[m] * zi[m] >= 1e-10f) break;
            zr[m] = zi[m] = 0.0f; live--;
        }
        if (live == 0 && tock_env <= 1e-5f && tock_peak <= 0.0f) { tock_z = 0.0f; tock_env = 0.0f; ringing = false; }
    }
}

#ifndef ESP_PLATFORM
void bowl_test_reset(void)           // tools/sim: silence, nothing queued
{
    strike_t s;
    while (sq_pop(&q, &s)) { }
    memset(zr, 0, sizeof zr); memset(zi, 0, sizeof zi); memset(ar, 0, sizeof ar); memset(ai, 0, sizeof ai);
    att_left = 0; live = 0; tock_env = tock_z = tock_peak = 0.0f; ringing = false; has_pend = false; lvl_v = 0.0f;
}
#endif
