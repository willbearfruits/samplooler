// The sounds SIFT starts with, made at boot (no recording of anyone's is in the firmware): about eleven seconds
// chosen to fill the map from corner to corner. Plucked strings (Karplus-Strong) up three octaves of C major's
// pentatonic, FM bells above them, a buzzing voice singing AH, EE and OO, breaths of filtered noise from dark to
// bright, and thumps, clicks and a shaker. Low to high across, tonal to noisy up: TONE by NOISE, the default map.
#include "inst_sift.h"
#include "dsp.h"
#include <math.h>
#include <string.h>

static uint32_t sr = 0x51f7u;
static float noise(void) { return rng_bi(&sr); }

typedef struct { int16_t *dst; int max, at; dcb_t dc; } pen_t;

static void put(pen_t *p, const float *x, int n)             // through a DC blocker, as the mic's high pass would
{
    for (int i = 0; i < n && p->at < p->max; i++) {
        float v = clampf(dcb_run(&p->dc, x[i]), -1.0f, 1.0f);
        p->dst[p->at++] = (int16_t)(v * 30000.0f);
    }
}
static void gap(pen_t *p, float s) { int n = (int)(s * (float)AUDIO_SR); while (n-- > 0 && p->at < p->max) p->dst[p->at++] = 0; }
static void fade_ends(float *x, int n) { int f = AUDIO_SR / 200; for (int i = 0; i < f && i < n; i++) { float g = (float)i / (float)f; x[i] *= g; x[n - 1 - i] *= g; } }

static UI_RAM float buf[AUDIO_SR];               // one event at a time, up to a second
static UI_RAM float ks[1100];

static int pluck(float hz, float dur, float bright)
{
    int n = (int)(dur * (float)AUDIO_SR), L = (int)((float)AUDIO_SR / hz + 0.5f);
    if (L < 4) L = 4;
    if (L > 1099) L = 1099;
    float lp = 0;
    float mean = 0;
    for (int i = 0; i < L; i++) { lp += bright * (noise() - lp); ks[i] = lp; mean += lp; }
    mean /= (float)L;
    for (int i = 0; i < L; i++) ks[i] -= mean;                  // no DC in the string: it would outlast the note
    int w = 0;
    float decay = 0.4985f + 0.0013f * fminf(1.0f, 110.0f / hz);
    for (int i = 0; i < n; i++) {
        int r = w + 1 >= L ? 0 : w + 1;
        float y = ks[w];
        ks[w] = decay * (y + ks[r]);
        w = r;
        buf[i] = 0.9f * y;
    }
    fade_ends(buf, n);
    return n;
}

static int bell(float hz, float dur)
{
    int n = (int)(dur * (float)AUDIO_SR);
    float pc = 0, pm = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SR;
        float idx = 0.4f + 2.2f * expf(-t * 7.0f);
        pm += 2.76f * hz / (float)AUDIO_SR; pm -= (float)(int)pm;
        pc += hz / (float)AUDIO_SR; pc -= (float)(int)pc;
        buf[i] = 0.6f * expf(-t * 4.0f) * sinf(6.2831853f * pc + idx * sinf(6.2831853f * pm));
    }
    fade_ends(buf, n);
    return n;
}

static int voice(float hz, float f1, float f2, float dur)
{
    int n = (int)(dur * (float)AUDIO_SR);
    svf_t a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    svf_set(&a, f1, 6.0f); svf_set(&b, f2, 9.0f);
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SR;
        float vib = 1.0f + 0.006f * sinf(6.2831853f * 5.2f * t) * fminf(1.0f, t * 3.0f);
        float dt = hz * vib / (float)AUDIO_SR;
        ph += dt; if (ph >= 1.0f) ph -= 1.0f;
        float s = saw_blep(ph, dt) + 0.02f * noise();
        float env = fminf(1.0f, t / 0.05f) * fminf(1.0f, (dur - t) / 0.08f);
        buf[i] = env * (1.4f * svf_bp(&a, s, NULL) + 0.9f * svf_bp(&b, s, NULL));
    }
    fade_ends(buf, n);
    return n;
}

static int breath(float fc, float q, float dur)
{
    int n = (int)(dur * (float)AUDIO_SR);
    svf_t f; memset(&f, 0, sizeof f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / dur / (float)AUDIO_SR;
        if ((i & 63) == 0) svf_set(&f, fc * (0.8f + 0.5f * t), q);
        float env = sinf(3.14159265f * t);
        buf[i] = 0.9f * env * env * svf_bp(&f, noise(), NULL);
    }
    fade_ends(buf, n);
    return n;
}

static int thump(float dur)
{
    int n = (int)(dur * (float)AUDIO_SR);
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SR;
        float f = 45.0f + 120.0f * expf(-t * 30.0f);
        ph += f / (float)AUDIO_SR; ph -= (float)(int)ph;
        buf[i] = 0.95f * expf(-t * 14.0f) * sinf(6.2831853f * ph) + 0.3f * expf(-t * 300.0f) * noise();
    }
    fade_ends(buf, n);
    return n;
}

static int click(float fc, float dur)
{
    int n = (int)(dur * (float)AUDIO_SR);
    svf_t f; memset(&f, 0, sizeof f); svf_set(&f, fc, 1.5f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_SR;
        float x = noise();
        buf[i] = 0.9f * expf(-t * (fc > 4000.0f ? 45.0f : 80.0f)) * (x - svf_lp(&f, x));
    }
    fade_ends(buf, n);
    return n;
}

int seed_render(int16_t *dst, int max)
{
    pen_t p = { dst, max, 0, { 0, 0 } };
    sr = 0x51f7u;
    static const int penta[] = { 0, 2, 4, 7, 9 };
    // plucks up three octaves, then a few back down
    for (int k = 0; k < 15; k++) {
        int oct = k / 5, d = penta[k % 5];
        float hz = midi_hz((float)(36 + 12 * oct + d));
        put(&p, buf, pluck(hz, 0.32f, 0.35f + 0.12f * (float)oct)); gap(&p, 0.02f);
    }
    put(&p, buf, pluck(midi_hz(79.0f), 0.4f, 0.8f)); gap(&p, 0.03f);
    put(&p, buf, pluck(midi_hz(84.0f), 0.4f, 0.9f)); gap(&p, 0.03f);
    // bells
    static const float bells[] = { 72, 76, 79, 84, 88, 91 };
    for (int k = 0; k < 6; k++) { put(&p, buf, bell(midi_hz(bells[k]), 0.5f)); gap(&p, 0.02f); }
    // the voice: AH, EE, OO at three pitches
    put(&p, buf, voice(midi_hz(48.0f), 700, 1220, 0.5f)); gap(&p, 0.03f);
    put(&p, buf, voice(midi_hz(55.0f), 300, 2300, 0.5f)); gap(&p, 0.03f);
    put(&p, buf, voice(midi_hz(60.0f), 320, 870, 0.5f)); gap(&p, 0.03f);
    put(&p, buf, voice(midi_hz(64.0f), 650, 1100, 0.5f)); gap(&p, 0.03f);
    put(&p, buf, voice(midi_hz(67.0f), 280, 2500, 0.45f)); gap(&p, 0.03f);
    // breaths from dark to bright
    static const float br[][2] = { { 300, 1.2f }, { 800, 2.0f }, { 1800, 2.5f }, { 4000, 2.0f }, { 8000, 1.5f } };
    for (int k = 0; k < 5; k++) { put(&p, buf, breath(br[k][0], br[k][1], 0.42f)); gap(&p, 0.03f); }
    // thumps, clicks, a shaker
    for (int k = 0; k < 3; k++) {
        put(&p, buf, thump(0.22f)); gap(&p, 0.04f);
        put(&p, buf, click(2500.0f + 1500.0f * (float)k, 0.06f)); gap(&p, 0.05f);
        put(&p, buf, click(7000.0f, 0.12f)); gap(&p, 0.05f);
    }
    // a last bell chord to close
    {
        int n = bell(midi_hz(60.0f), 0.9f);
        static UI_RAM float chord[AUDIO_SR];
        memcpy(chord, buf, sizeof(float) * (size_t)n);
        bell(midi_hz(64.0f), 0.9f); for (int i = 0; i < n; i++) chord[i] += 0.7f * buf[i];
        bell(midi_hz(67.0f), 0.9f); for (int i = 0; i < n; i++) chord[i] = 0.55f * (chord[i] + 0.7f * buf[i]);
        put(&p, chord, n);
    }
    gap(&p, 0.04f);
    return p.at;
}
