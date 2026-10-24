// DWELL's FEEDBACK: HOWL's loop (instruments/howl), tuned to the tones that sound. The mic goes through narrow band
// passes on the drone's own ratios and back out: only those notes can build up, so the room howls in just intonation.
//
//   the bank      a band pass on each tone that sounds (up to NVL), RING = their Q, weighted by the tone's level
//   the loops     AIR: the mic, so the loop runs through the speaker and the room (and between two boxes in one room);
//                 INNER: the bank's output one sample later, inside the box (it howls on headphones), with a little of
//                 the drone itself in it, so a howl starts from the drone's own tones; BOTH
//   the gain      loop gain = what the UI asks (FEEDBACK, raised by the energy) times the governor: an automatic
//                 gain on the log of the level, down fast whenever the howl passes FB LEVEL, up no faster than SWELL
//                 allows, never above 0 dB. A per-sample brake for a loop that grows faster than a block.
//   the ceiling   the loop's soft saturation at 1.3 x FB LEVEL holds its amplitude at every instant; what leaves the box
//                 rises at most 120 dB/s and never passes 1.25 x FB LEVEL (a soft knee): it cannot blow up
//
// All of it HOWL's, measured there with a speaker four times too loud; shift, fold, the tide and the colours left out.
#include "inst_eng.h"
#include "dsp.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

volatile int   eng_fb_mode;
volatile float eng_fb_drive = 0.8f, eng_fb_level = -18.0f, eng_fb_ring = 60.0f, eng_fb_swell = 3.0f, eng_fb_env, eng_fb_gov;

static svf_t   bp[NVL];
static float   f_s[NVL], k_s[NVL], w_s[NVL];
static float   y_prev, drive_s, fb_t_s, mic_s, env, agc_db = -6.0f, g_s = 0.5f, brake = 1.0f;
static float   tgt_s = 0.1f, env_o, allow = 1e-4f, og = 1.0f;
static dcb_t   dcb;
static uint32_t seed = 0x2468ace1u;

void howl_init(void)
{
    for (int b = 0; b < NVL; b++) { f_s[b] = 130.8f; k_s[b] = 1.0f / 60.0f; svf_set(&bp[b], f_s[b], 60.0f); }
}

// n samples of the howl into out[], from the mic in[] (and, in the inner loop, a little of the drone itself, self[]);
// hz[]/wt[] the tones that sound (nb of them)
IRAM_ATTR __attribute__((noinline)) void howl_run(const float *in, const float *self, float *out, int n, const float *hz, const float *wt, int nb)
{
    int mode = eng_fb_mode;
    if (mode == FB_OFF && env < 1e-5f && env_o < 1e-5f && fb_t_s < 1e-3f && mic_s < 1e-3f && drive_s < 1e-3f) {
        memset(out, 0, (size_t)n * sizeof(float));
        eng_fb_env = 0.0f;
        return;
    }
    const float bdt = (float)n / (float)AUDIO_SR;
    // ---- per block: the bank, its centres gliding (0.1 s), its Q gliding in ratio, the one-sample loop's phase made up
    float glide_k = 1.0f - expf(-bdt / 0.1f);
    float ring = clampf(eng_fb_ring, 5.0f, 300.0f);
    float w[NVL], cf[NVL][4], dc_[NVL][4];
    for (int b = 0; b < NVL; b++) {
        float ft = b < nb ? hz[b] : f_s[b];
        if (ft < 30.0f) ft = 30.0f;
        if (ft > 8000.0f) ft = 8000.0f;
        f_s[b] += glide_k * (ft - f_s[b]);
        float kt = 1.0f / ring;
        k_s[b] *= exp2f(0.0133f * log2f(kt / k_s[b]));
        // The one-sample loop adds a phase of -w: a howl locks where the band pass gives +w back, a little below its
        // centre and cos(w) down in gain. Tuned up and boosted by that much, every band howls on its note at the same gain.
        float wc = TWO_PI * f_s[b] / (float)AUDIO_SR, tight = fb_t_s * (1.0f / 0.92f);
        float fc = f_s[b] * (1.0f + tight * 0.5f * wc * k_s[b]);
        svf_t nx; svf_set(&nx, fc, 1.0f / k_s[b]);
        cf[b][0] = bp[b].a1; cf[b][1] = bp[b].a2; cf[b][2] = bp[b].a3; cf[b][3] = bp[b].k;
        dc_[b][0] = (nx.a1 - bp[b].a1) / (float)n; dc_[b][1] = (nx.a2 - bp[b].a2) / (float)n;
        dc_[b][2] = (nx.a3 - bp[b].a3) / (float)n; dc_[b][3] = (nx.k - bp[b].k) / (float)n;
        w_s[b] += 0.05f * ((b < nb ? clampf(wt[b], 0.0f, 1.0f) : 0.0f) - w_s[b]);
        w[b] = w_s[b] * (1.0f + tight * (1.0f / cosf(wc) - 1.0f));
    }
    // ---- per block: the loops and the gain asked for
    float fb_t_t = mode == FB_INNER ? 0.92f : mode == FB_BOTH ? 0.6f : 0.0f;
    float mic_t = mode == FB_AIR || mode == FB_BOTH ? 1.0f : 0.0f;
    float drive_t = mode == FB_OFF ? 0.0f : clampf(eng_fb_drive, 0.0f, 1.6f);
    float swell = clampf(eng_fb_swell, 0.2f, 30.0f);
    float up = 1.0f - expf(-1.0f / ((float)AUDIO_SR * swell)), down = 1.0f - expf(-1.0f / ((float)AUDIO_SR * 0.05f));
    float seed_amp = mode == FB_INNER || mode == FB_BOTH ? 3e-5f : 0.0f;   // a whisper, so the inner loop has something to start from
    // the governor: proportional on the log of the level, down fast, up no faster than SWELL allows
    float target_db = clampf(eng_fb_level, -40.0f, -3.0f), target = exp2f(target_db * 0.16609640f);
    {
        float env_db = env > 1e-6f ? 20.0f * log10f(env) : -120.0f;
        float err = target_db - env_db;
        if (err < 0) agc_db += fmaxf(err * 0.05f, -0.6f);
        else agc_db += fminf(err * 0.02f, bdt * 30.0f / swell);
        agc_db = clampf(agc_db, -60.0f, 0.0f);
    }
    float g1 = exp2f(agc_db * 0.16609640f), g = g_s, dg = (g1 - g_s) / (float)n;
    g_s = g1;
    brake = fminf(1.0f, brake * exp2f(bdt * 15.0f / swell));
    float t0_ = tgt_s;
    tgt_s += 0.15f * (target - tgt_s);
    const float dt_ = (tgt_s - t0_) / (float)n;
    const float rise = 1.000432f;                                    // what leaves the box rises at most 120 dB/s
    for (int i = 0; i < n; i++) {
        g += dg;
        drive_s += (drive_t > drive_s ? up : down) * (drive_t - drive_s);
        fb_t_s += 0.002f * (fb_t_t - fb_t_s);
        mic_s += 0.002f * (mic_t - mic_s);
        float x = in[i] * mic_s + seed_amp * rng_bi(&seed) + fb_t_s * (y_prev + 0.1f * self[i]);   // the drone starts its own howl
        float s = 0.0f;
        for (int b = 0; b < NVL; b++) {
            bp[b].a1 = cf[b][0] += dc_[b][0]; bp[b].a2 = cf[b][1] += dc_[b][1];
            bp[b].a3 = cf[b][2] += dc_[b][2]; bp[b].k = cf[b][3] += dc_[b][3];
            if (w[b] < 1e-4f) continue;
            s += svf_bp(&bp[b], x, NULL) * w[b];
        }
        float v = s * drive_s * g * brake;
        // the loop saturates at 1.3 x FB LEVEL: it limits its own amplitude there at every instant, whatever the gain
        float tg = t0_ + dt_ * (float)i, ts = 1.3f * tg, ceil_ = 1.25f * tg;
        float y = ts * soft_tanh(v / ts);
        y = dcb_run(&dcb, y);
        if (!(fabsf(y) < 4.0f)) y = 0.0f;                             // a runaway: never
        float a = fabsf(y);
        env += (a > env ? 0.05f : 0.0004f) * (a - env);
        if (env > 1.5f * tg) brake *= 0.996f;
        y_prev = flush_denormal(y);
        // what leaves the box never rises faster than 120 dB/s and never passes 1.25 x FB LEVEL
        env_o += (a > env_o ? 0.2f : 0.0015f) * (a - env_o);
        if (env_o > allow) allow *= rise; else allow = env_o;
        if (allow < 1e-4f) allow = 1e-4f;
        float og_t = env_o > allow ? allow / env_o : 1.0f;
        og += 0.03f * (og_t - og);
        out[i] = ceil_ * soft_knee(y * og / ceil_);
    }
    if (brake < 1e-3f) brake = 1e-3f;
    for (int b = 0; b < NVL; b++) { bp[b].ic1 = flush_denormal(bp[b].ic1); bp[b].ic2 = flush_denormal(bp[b].ic2); }
    env = flush_denormal(env); env_o = flush_denormal(env_o);
    eng_fb_env = env;
    eng_fb_gov = agc_db;
}
