// VOICE's synth, audio task: one voice that follows the note the pitch tracker hears (inst_ear.c), as long as the
// string sounds. BASS (a saw and a sine an octave under, low-passed), BOW (two saws a few cents apart, a slow
// attack, vibrato that comes in late), ORGAN (a square and its octave) and PIPE (a sine, its octave, breath).
#include "inst_nylon.h"
#include "dsp.h"
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

static float f_s = 110.0f, ph1, ph2, ph3, env, vib_ph, held, lvl_s;
static svf_t lpf, nbp;
static uint32_t seed = 0xabcdef1u;
static int type_cur = -1;
static float env_lvl;

IRAM_ATTR void synth_run(float *wet, int n, float feed)
{
    memset(wet, 0, (size_t)n * sizeof(float));
    const float bdt = (float)n / (float)AUDIO_SR;
    int type = E.v_type;
    if (type != type_cur) { type_cur = type; }
    bool gate = E.v_gate && feed > 0.01f;
    float ft = clampf(E.v_hz, 20.0f, 4000.0f);
    if (E.v_hz > 0.0f) {                                         // glide in pitch (ratio), at block rate
        float g = clampf(E.v_glide, 0.0f, 2.0f);
        float k = g < 0.005f ? 1.0f : 1.0f - expf(-bdt / g);
        f_s *= exp2f(log2f(ft / f_s) * k);
    }
    static const float att[NVT] = { 0.008f, 0.16f, 0.006f, 0.06f }, rel[NVT] = { 0.25f, 0.5f, 0.08f, 0.2f };
    int ty = clampi(type, 0, NVT - 1);
    float ka = 1.0f - expf(-bdt / att[ty]), kr = 1.0f - expf(-bdt / rel[ty]);
    float e0 = env;
    env += (gate ? ka : kr) * ((gate ? 1.0f : 0.0f) - env);
    env = flush_denormal(env);
    held = gate ? held + bdt : 0.0f;
    float l0 = lvl_s; lvl_s += 0.2f * (clampf(E.v_level, 0.0f, 1.5f) - lvl_s);
    if (env < 1e-5f && e0 < 1e-5f) { eng_synth_lvl = 0.0f; return; }
    float vib = ty == VT_BOW ? 0.005f * smoothstep(0.3f, 0.9f, held) : ty == VT_PIPE ? 0.002f * smoothstep(0.2f, 0.6f, held) : 0.0f;
    vib_ph = wrap1(vib_ph + bdt * 5.2f);
    float f = f_s * (1.0f + vib * sin_turns(vib_ph));
    const float isr = 1.0f / (float)AUDIO_SR;
    float d1 = f * isr;
    switch (ty) {
    case VT_BASS: svf_set(&lpf, fminf(f * 5.0f, 900.0f), 0.9f); break;
    case VT_BOW: svf_set(&lpf, fminf(f * 9.0f, 2600.0f), 0.8f); break;
    case VT_ORGAN: svf_set(&lpf, 3200.0f, 0.7f); break;
    default: svf_set(&lpf, fminf(f * 6.0f, 5000.0f), 0.7f); svf_set(&nbp, fminf(f * 2.0f, 8000.0f), 3.0f); break;
    }
    float pk = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)(i + 1) / (float)n;
        float a = (e0 + (env - e0) * t) * (l0 + (lvl_s - l0) * t);
        float y;
        switch (ty) {
        case VT_BASS:
            ph1 = wrap1(ph1 + d1); ph2 = wrap1(ph2 + 0.5f * d1);
            y = 0.5f * saw_blep(ph1, d1) + 0.7f * sin_turns(ph2);
            y = svf_lp(&lpf, y);
            break;
        case VT_BOW:
            ph1 = wrap1(ph1 + d1 * 1.004f); ph2 = wrap1(ph2 + d1 * 0.996f);
            y = 0.4f * (saw_blep(ph1, d1 * 1.004f) + saw_blep(ph2, d1 * 0.996f));
            y = svf_lp(&lpf, y);
            break;
        case VT_ORGAN:
            ph1 = wrap1(ph1 + d1); ph2 = wrap1(ph2 + 2.0f * d1); ph3 = wrap1(ph3 + 0.5f * d1);
            y = 0.3f * square_blep(ph1, d1) + 0.25f * sin_turns(ph2) + 0.3f * sin_turns(ph3);
            y = svf_lp(&lpf, y);
            break;
        default:
            ph1 = wrap1(ph1 + d1); ph2 = wrap1(ph2 + 2.0f * d1);
            y = 0.6f * sin_turns(ph1) + 0.12f * sin_turns(ph2) + 0.25f * svf_bp(&nbp, rng_bi(&seed), NULL);
            y = svf_lp(&lpf, y);
            break;
        }
        y *= 0.5f * a;
        wet[i] = y;
        float aa = fabsf(y); if (aa > pk) pk = aa;
    }
    env_lvl = fmaxf(pk, env_lvl * 0.97f);
    eng_synth_lvl = env_lvl;
}
