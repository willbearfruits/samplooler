// GOLPE's drums, audio task: KNOCK's synthesized voices (a kick with a pitch drop, a snare, hats, a tom, a clap, a
// rim, a cowbell, a shaker, a conga) and a cajon, low and high. A slap on the guitar's body asks for one
// (eng_trigger, from the UI side); a voice in use is taken over with its last sample fading out, never cut.
#include "inst_nylon.h"
#include "dsp.h"
#include "spsc.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

const char *const drum_names[NDRUM] = { "OFF", "KICK", "SNARE", "HAT", "OPEN HAT", "TOM", "CLAP", "RIM", "COWBELL", "SHAKER", "CONGA", "CAJON LOW", "CAJON HIGH" };
const int drum_gm[NDRUM] = { 0, 36, 38, 42, 46, 45, 39, 37, 56, 70, 63, 35, 40 };

SPSC(trigq, trig_t, 16)
static trigq_t trigs;
void eng_trigger(const trig_t *t) { trigq_push(&trigs, t); }

#define NDV 8
typedef struct {
    uint8_t type; bool on;
    int t;
    float amp, ph, ph2, f0, f1;
    float ae, kae, pe, kpe, xe, kxe;
    float carry, last;
    svf_t fa, fb;
} dvoice_t;
static dvoice_t vo[NDV];
static uint32_t seed = 0x2468ace1u;

static IRAM_ATTR void voice_start(int type, float amp)
{
    int k = -1; float q = 1e9f;
    for (int i = 0; i < NDV; i++) {
        if (!vo[i].on) { k = i; break; }
        float l = vo[i].ae * vo[i].amp;
        if (l < q) { q = l; k = i; }
    }
    dvoice_t *v = &vo[k];
    float carry = v->on ? v->last + v->carry : 0.0f;
    if (type == DR_HAT) for (int i = 0; i < NDV; i++) if (vo[i].on && vo[i].type == DR_OHAT) vo[i].kae = 0.992f;   // the closed hat chokes the open one
    memset(v, 0, sizeof *v);
    v->carry = carry;
    v->type = (uint8_t)type; v->on = true; v->amp = amp; v->ae = 1.0f; v->pe = 1.0f; v->xe = 1.0f;
    float tn = fast_exp2(clampf(E.d_tune, -24.0f, 24.0f) / 12.0f), dc = clampf(E.d_decay, 0.2f, 5.0f);
    switch (type) {
    case DR_KICK:    v->f0 = 46.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.30f * dc); v->f1 = 3.5f; break;
    case DR_SNARE:   v->f0 = 185.0f * tn; svf_set(&v->fa, 1600.0f * tn, 0.7f); v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.15f * dc); break;
    case DR_HAT:     svf_set(&v->fa, 7200.0f * tn, 0.8f); v->kae = 1.0f - onepole_s(0.04f * dc); break;
    case DR_OHAT:    svf_set(&v->fa, 6500.0f * tn, 0.9f); v->kae = 1.0f - onepole_s(0.32f * dc); break;
    case DR_TOM:     v->f0 = 105.0f * tn; v->kpe = 1.0f - onepole_s(0.05f); v->kae = 1.0f - onepole_s(0.38f * dc); break;
    case DR_CLAP:    svf_set(&v->fa, 1100.0f * tn, 1.5f); v->kpe = 1.0f - onepole_s(0.004f); v->kae = 1.0f - onepole_s(0.14f * dc); break;
    case DR_RIM:     v->f0 = 520.0f * tn; svf_set(&v->fa, 1800.0f * tn, 2.0f); v->kae = 1.0f - onepole_s(0.028f * dc); v->kpe = 1.0f - onepole_s(0.006f); break;
    case DR_BELL:    v->f0 = 540.0f * tn; v->f1 = 800.0f * tn; svf_set(&v->fa, 2400.0f * tn, 1.2f); v->kpe = 1.0f - onepole_s(0.02f); v->kae = 1.0f - onepole_s(0.22f * dc); break;
    case DR_SHAKER:  svf_set(&v->fa, 5500.0f * tn, 1.2f); v->kae = 1.0f - onepole_s(0.07f * dc); v->xe = 0.0f; break;
    case DR_CONGA:   v->f0 = 330.0f * tn; v->kpe = 1.0f - onepole_s(0.008f); v->kae = 1.0f - onepole_s(0.2f * dc); break;
    case DR_CAJON_LO:                                         // the box's face struck low: a soft thump, a little air
        v->f0 = 68.0f * tn; v->kpe = 1.0f - onepole_s(0.018f); v->kae = 1.0f - onepole_s(0.22f * dc);
        svf_set(&v->fa, 380.0f * tn, 0.8f); break;
    case DR_CAJON_HI:                                         // struck at the top edge: a crack, the wires buzzing behind it
        v->f0 = 230.0f * tn; v->kpe = 1.0f - onepole_s(0.03f); v->kae = 1.0f - onepole_s(0.06f * dc);
        svf_set(&v->fa, 3200.0f * tn, 1.6f); svf_set(&v->fb, 1200.0f * tn, 1.0f);
        v->kxe = 1.0f - onepole_s(0.11f * dc); break;
    }
}

static IRAM_ATTR float voice_run(dvoice_t *v)
{
    float y = 0;
    const float isr = 1.0f / (float)AUDIO_SR;
    switch (v->type) {
    case DR_KICK:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + v->f1 * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->t < 48) y += 0.4f * rng_bi(&seed) * (1.0f - (float)v->t / 48.0f);
        break;
    case DR_SNARE:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + 0.4f * v->pe) * isr);
        y = 0.45f * sin_turns(v->ph) * v->pe + 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case DR_HAT: case DR_OHAT:
        y = 0.75f * svf_hp(&v->fa, rng_bi(&seed)) * v->ae;
        break;
    case DR_TOM: case DR_CONGA:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + (v->type == DR_TOM ? 0.45f : 0.25f) * v->pe) * isr);
        y = sin_turns(v->ph) * v->ae;
        if (v->type == DR_CONGA && v->t < 40) y += 0.25f * rng_bi(&seed);
        break;
    case DR_CLAP: {
        if (v->t == 352 || v->t == 704) v->pe = 1.0f;
        float e = v->t > 704 && v->ae > v->pe ? v->ae : v->pe;
        y = 1.5f * svf_bp(&v->fa, rng_bi(&seed), NULL) * e;
        break;
    }
    case DR_RIM:
        v->ph = wrap1(v->ph + v->f0 * isr);
        y = 0.6f * sin_turns(v->ph) * v->ae + 1.2f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->pe;
        break;
    case DR_BELL: {
        float d0 = v->f0 * isr, d1 = v->f1 * isr;
        v->ph = wrap1(v->ph + d0); v->ph2 = wrap1(v->ph2 + d1);
        float s = 0.5f * (square_blep(v->ph, d0) + square_blep(v->ph2, d1));
        y = 0.8f * svf_bp(&v->fa, s, NULL) * (0.4f * v->pe + 0.6f) * v->ae;
        break;
    }
    case DR_SHAKER:
        v->xe += (1.0f - v->xe) * 0.03f;
        y = 1.1f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->ae * v->xe;
        break;
    case DR_CAJON_LO:
        v->ph = wrap1(v->ph + v->f0 * (1.0f + 1.2f * v->pe) * isr);
        y = 0.9f * sin_turns(v->ph) * v->ae + 0.35f * svf_bp(&v->fa, rng_bi(&seed), NULL) * v->pe;
        break;
    case DR_CAJON_HI: {
        v->ph = wrap1(v->ph + v->f0 * (1.0f + 0.3f * v->pe) * isr);
        float wires = v->t > 90 ? svf_bp(&v->fa, rng_bi(&seed), NULL) * v->xe : 0.0f;
        y = 0.55f * sin_turns(v->ph) * v->ae + 0.6f * svf_bp(&v->fb, rng_bi(&seed), NULL) * v->pe * v->ae + 0.7f * wires;
        if (v->t > 90) v->xe *= v->kxe;
        break;
    }
    }
    v->ae *= v->kae; v->pe *= v->kpe;
    v->t++;
    y *= v->amp;
    v->last = y;
    float c = v->carry; v->carry = flush_denormal(v->carry * 0.93f);
    float tail = v->type == DR_CAJON_HI ? v->xe : 0.0f;
    if (v->ae < 1e-4f && tail < 1e-4f && v->t > 64) v->on = false;
    return y + c;
}

IRAM_ATTR void drum_run(float *io, int n)
{
    trig_t t;
    while (trigq_pop(&trigs, &t)) {
        if (t.drum == DR_OFF || t.drum >= NDRUM) continue;
        float v = (float)t.vel / 127.0f;
        voice_start(t.drum, 0.55f * v * v * clampf(E.d_level, 0.0f, 1.5f));
        eng_drum_last = t.drum; eng_drum_seq++;
    }
    bool any = false;
    for (int k = 0; k < NDV; k++) any |= vo[k].on || vo[k].carry != 0.0f;
    if (!any) return;
    for (int i = 0; i < n; i++) {
        float s = 0;
        for (int k = 0; k < NDV; k++) if (vo[k].on) s += voice_run(&vo[k]); else if (vo[k].carry != 0.0f) { s += vo[k].carry; vo[k].carry = flush_denormal(vo[k].carry * 0.93f); }
        io[i] += s;
    }
}

void drum_init(void) { memset(vo, 0, sizeof vo); }
