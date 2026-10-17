// BREATH (HUFF as a layer) on the host: made live with the knob, then: silent without breath; knocks on
// the box are not breath; every model in tune on every note of the ladder in the song's key and scale;
// the fade when another layer goes live and the near-zero cost after it; HOLD; MIDI in and out; SETUP;
// the cost live. Pictures: the play scene and a GIF of a tune.
#include "sim.h"
#include "jam.h"
#include <math.h>
#include <string.h>
#include <time.h>

bool sim_menu_param(int p);
void sim_menu_close(void);
float breath_reading(void);                 // layer_breath.c, for tests
int   breath_knocks(void);
float breath_level(void);

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }

// ---- the mic: breath is low rumble (a one-pole at ~150 Hz on noise); knocks are short sharp bursts
typedef struct { float level, lp, knock_t, knock_lp; } mic_t;
static mic_t M;
static float mic(float t, void *u)
{
    mic_t *b = u;
    b->lp += 0.03f * (sim_noise() - b->lp);
    float x = 0.0015f * sim_noise() + b->level * b->lp * 6.0f;
    if (b->knock_t > 0 && t >= b->knock_t) {                      // a knock: a thump that dies in about 15 ms
        float dt = t - b->knock_t;
        b->knock_lp += 0.2f * (sim_noise() - b->knock_lp);
        x += 1.2f * b->knock_lp * expf(-dt / 0.006f) + 0.5f * sinf(6.2831853f * 90.0f * dt) * expf(-dt / 0.012f);
        if (dt > 0.08f) b->knock_t = 0;
    }
    return x;
}
static void blow(float level) { M.level = level; SI.mic = mic; SI.mic_u = &M; }

void excite_breath(void) { blow(0.12f); M.knock_t = 0; sim_tilt(0, 0); SI.jx = SI.jy = 0; }

// the ladder, as layer_breath.c builds it: NOTES from the scale, a third of them below the key's home note
static int ladder(int *notes)
{
    int n = (int)pv[tag("hnts")], r = n / 3, o = 5 + (int)pv[tag("hoct")];
    for (int k = 0; k < n; k++) notes[k] = scale_note(k - r, o);
    return n;
}
static float lean_for(int k, int n) { return (((float)k + 0.5f) / (float)n - 0.5f) * 70.0f; }
static float hz(float m) { return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f); }
static const char *const model_name[6] = { "OCARINA", "FLUTE", "PAN PIPE", "SHAKUHACHI", "CLARINET", "BOTTLE" };
static const int model_shift[6] = { 0, 0, 0, 0, 0, -12 };

static void make_live(int l)
{
    for (int i = 0; i < 12 && host_live() != l; i++) sim_turn(host_live() < l ? 1 : -1);
    sim_run(0.1f);
}

static double block_us(bool live)
{
    float in[AUDIO_BLOCK], dry[AUDIO_BLOCK], send[AUDIO_BLOCK];
    for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = 0.05f * sim_noise();
    double best = 1e9;
    for (int k = 0; k < 8; k++) {                                  // the best of 8: other work on the machine only adds
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        for (int r = 0; r < 500; r++) { memset(dry, 0, sizeof dry); memset(send, 0, sizeof send); layer_breath.audio(in, dry, send, AUDIO_BLOCK, live); }
        clock_gettime(CLOCK_MONOTONIC, &b);
        double t = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / 500.0;
        if (t < best) best = t;
    }
    return best;
}

void test_layer_breath(void)
{
    sim_menu_close();
    make_live(L_BREATH);
    check(host_live() == L_BREATH, "the knob makes BREATH live");
    // only BREATH in the mix, dry, no vibrato, for measuring
    set("mlvd", 0); set("mlvb", 0); set("mlvw", 0); set("hsnd", 0); set("hvib", 0); set("hhrm", 0); set("hhld", 0);
    blow(0); sim_tilt(0, 0); SI.jx = SI.jy = 0;
    sim_run(1.5f);
    check(m_rms(sim_t - 0.8f, sim_t) < 0.002f, "no breath: silent (rms %.5f)", (double)m_rms(sim_t - 0.8f, sim_t));
    blow(0.12f); sim_run(1.0f);
    check(m_rms(sim_t - 0.5f, sim_t) > 0.04f, "a normal breath plays (rms %.3f, breath reads %.2f)", (double)m_rms(sim_t - 0.5f, sim_t), (double)breath_reading());
    sim_shot("breath_playing");
    blow(0); sim_run(0.6f);
    check(m_rms(sim_t - 0.3f, sim_t) < 0.002f, "the breath stops: so does the sound");

    // knocks on the box are not breath
    int k0 = breath_knocks();
    float t0 = sim_t;
    float lv_max = 0, rd_max = 0;
    for (int i = 0; i < 6; i++) {
        M.knock_t = sim_t + 0.05f;
        for (int k = 0; k < 35; k++) {
            sim_tick(); if (breath_level() > lv_max) lv_max = breath_level(); if (breath_reading() > rd_max) rd_max = breath_reading();
        }
    }
    note("knocks: the breath layer's own level peaked at %.3f, its breath reading at %.3f", (double)lv_max, (double)rd_max);
    check(m_peak(t0, sim_t) < 0.02f && breath_knocks() - k0 >= 5, "six knocks on the box: no sound (peak %.4f), %d taken for knocks", (double)m_peak(t0, sim_t), breath_knocks() - k0);
    blow(0.12f); sim_run(1.0f);
    check(m_rms(sim_t - 0.4f, sim_t) > 0.04f, "a breath right after: it plays");
    blow(0); sim_run(0.6f);

    // every model on every note of the ladder, in the song's key and scale (D minor)
    set("skey", 2); set("sscl", 1); sim_run(0.1f);
    int notes[16], N = ladder(notes);
    float worst_all = 0;
    for (int m = 0; m < 6; m++) {
        set("hmdl", (float)m);
        float worst = 0; int silent = 0;
        for (int k = 0; k < N; k++) {
            sim_tilt(0, lean_for(k, N)); blow(0); sim_run(0.3f);
            blow(0.12f); sim_run(0.9f);
            float want = hz((float)(notes[k] + model_shift[m]));
            if (want > 2000.0f && m >= 1 && m <= 3) want *= 0.5f;       // jets fold above B6
            float f = m_pitch_at(sim_t - 0.4f, sim_t, want * 0.75f, want * 1.33f);
            if (f <= 0) { silent++; continue; }
            float c = fabsf(1200.0f * log2f(f / want));
            if (c > worst) worst = c;
        }
        check(silent == 0 && worst < 6.0f, "%s: all %d notes of D minor sound, in tune within 6 cents (worst %.1f)", model_name[m], N, (double)worst);
        if (worst > worst_all) worst_all = worst;
        blow(0); sim_run(0.3f);
    }
    metric("breath_cents_worst", worst_all);
    set("hmdl", 0);

    // the ladder follows the song's key when it changes
    set("skey", 7); set("sscl", 0); sim_run(0.1f);
    N = ladder(notes);
    sim_tilt(0, lean_for(N / 3, N)); blow(0); sim_run(0.3f); blow(0.12f); sim_run(1.0f);
    float f = m_pitch_at(sim_t - 0.4f, sim_t, 300, 1500);
    check(fabsf(1200.0f * log2f(f / hz((float)notes[N / 3]))) < 6.0f, "SONG > KEY to G: the home rung plays G5 (%.1f Hz)", (double)f);
    sim_shot("breath_g");

    // the stick bends by BEND RANGE
    float want = hz((float)notes[N / 3]);
    SI.jx = 1.0f; sim_run(0.8f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, want, want * 1.5f);
    check(fabsf(1200.0f * log2f(f / want) - 200.0f) < 15.0f, "stick right: up two semitones (%.0f cents)", (double)(1200.0f * log2f(f / want)));
    SI.jx = 0; sim_run(0.4f);

    // not live: it fades out in about 50 ms and then costs nothing
    double us_live = block_us(true);
    make_live(L_HOWL);
    float tf = sim_t;
    sim_run(0.5f);
    check(m_rms(tf + 0.08f, tf + 0.4f) < 0.001f, "another layer live: BREATH is gone within 80 ms (rms %.5f)", (double)m_rms(tf + 0.08f, tf + 0.4f));
    double us_off = block_us(false);
    metric("breath_us_per_block_not_live", us_off);
    check(us_off < 0.2, "not live and faded: %.3f us per block", us_off);
    // HOLD keeps it going as a drone
    make_live(L_BREATH); set("hhld", 1); blow(0.12f); sim_run(1.0f);
    make_live(L_HOWL); blow(0); sim_run(1.0f);
    check(m_rms(sim_t - 0.5f, sim_t) > 0.04f, "HOLD on: it plays on when another layer is live (rms %.3f)", (double)m_rms(sim_t - 0.5f, sim_t));
    make_live(L_BREATH); set("hhld", 0); blow(0); sim_run(0.8f);
    check(m_rms(sim_t - 0.3f, sim_t) < 0.002f, "HOLD off and no breath: silent");

    // no click when any of BREATH's params jumps end to end while it plays (the core's click check runs on
    // whichever layer is live then)
    {
        static const char *const tags[] = { "hmdl", "hoct", "hnts", "hgld", "hvib", "hhrm", "hsnd", "hsns", "hgat", "hovb", "hair", "hbrt", "hbnd", NULL };
        blow(0.12f); sim_run(1.0f);
        int bad = 0;
        for (int i = 0; tags[i]; i++) {
            int p = tag(tags[i]); if (p < 0) continue;
            const param_t *d = param(p);
            float keep = pv[p], worst = 0;
            for (int dir = 0; dir < 2; dir++) {
                param_set(p, dir ? d->min : d->max); sim_run(0.35f);
                param_set(p, dir ? d->max : d->min);
                float tc = sim_t; sim_run(0.2f);
                float r = m_click(tc, NULL, NULL); if (r > worst) worst = r;
            }
            param_set(p, keep); sim_run(0.3f);
            if (worst > 3.0f) { bad++; note("%s: a jump %.1fx the signal's own", d->name, (double)worst); }
        }
        check(bad == 0, "no click when any of BREATH's 13 sound params jumps end to end while it plays (%d click)", bad);
        blow(0); sim_run(0.5f);
    }

    // the cost live, with the harmony voice
    set("hhrm", 2); blow(0.12f); sim_run(0.5f);
    double us_harm = block_us(true);
    metric("breath_us_per_block_live", us_live);
    metric("breath_us_per_block_live_harmony", us_harm);
    check(us_harm < 7.0, "live with the harmony voice: %.2f us per block (<= 7)", us_harm);
    set("hhrm", 0);

    // MIDI out: notes with their offs, the breath as CC 2
    blow(0); sim_run(0.5f);
    midi_n = 0;
    for (int k = 0; k < 4; k++) { sim_tilt(0, lean_for(k + 2, N)); sim_run(0.3f); blow(0.12f); sim_run(0.4f); blow(0); sim_run(0.3f); }
    int on = 0, off = 0, cc2 = 0;
    int bch = (int)pv[CP(CP_MIDI_CH)] - 1;                         // BREATH plays on the base channel; the others' loops may be going
    for (int i = 0; i < midi_n; i++) {
        if ((midi_log[i].st & 0x0F) != bch) continue;
        int s = midi_log[i].st & 0xF0;
        if (s == 0x90 && midi_log[i].d2 > 0) on++; else if (s == 0x80 || (s == 0x90 && midi_log[i].d2 == 0)) off++;
        if (s == 0xB0 && midi_log[i].d1 == 2) cc2++;
    }
    check(on == 4 && off == 4 && cc2 > 8, "four breaths: four notes out with their offs, %d CC 2 (%d on, %d off)", cc2, on, off);
    // MIDI in: a note sets the pitch, CC 2 blows
    front_midi(0x90, 67, 100); front_midi(0xB0, 2, 90); sim_run(1.0f);
    f = m_pitch_at(sim_t - 0.4f, sim_t, 300, 1200);
    check(fabsf(1200.0f * log2f(f / hz(67.0f))) < 6.0f, "MIDI note 67 and CC 2 in: G4 with no breath at the mic (%.1f Hz)", (double)f);
    front_midi(0x80, 67, 0); front_midi(0xB0, 2, 0); sim_run(2.6f);

    // SETUP: turn it to GO, stay quiet, blow
    float r0 = pv[tag("hrng")];
    set("hrng", 0.5f);
    sim_menu_param(tag("hset")); sim_tap(); sim_turn(1); sim_run(0.1f);
    check(front_menu_level() == 0, "SETUP: turning it to GO closes the menu and listens");
    blow(0); sim_run(1.0f); sim_shot("breath_setup_quiet");
    sim_run(1.3f); blow(0.09f); sim_run(1.5f); sim_shot("breath_setup_blow"); sim_run(2.2f);
    blow(0.09f); sim_run(0.5f);
    float rd = 0;                                                  // the reading is gusty: its average over half a second
    for (int i = 0; i < 50; i++) { sim_tick(); rd += breath_reading() / 50.0f; }
    check(pv[tag("hrng")] < 0.3f && rd > 0.35f && rd < 0.85f && pv[tag("hset")] == 0, "after SETUP a normal breath reads %.0f %% (range %.3f, was %.3f)", (double)(rd * 100.0f), (double)pv[tag("hrng")], (double)r0);
    // the knob in play now turns SETUP (the core's quick edit): a turn up must not start it again
    blow(0); sim_run(0.3f);
    sim_turn(1); sim_run(0.3f);
    check(front_menu_level() == 0 && pv[tag("hset")] == 0 && m_rms(sim_t - 0.2f, sim_t) < 0.002f, "the knob in play turning SETUP up does not start it again");
    memset(meta.quick, 0, 4);
    blow(0); sim_run(0.5f);

    // pictures: a tune on the ocarina, in the song's key (C major), with the FX send back on
    set("skey", 0); set("sscl", 0); set("hsnd", 35); set("hvib", 15); set("hmdl", 0);
    set("mlvd", 80); set("mlvb", 80); set("mlvw", 80);
    N = ladder(notes);
    sim_gif_begin("breath");
    static const int tune[] = { 3, 4, 5, 6, 5, 4, 3, 2, 3, 5, 7, 6, 5 };
    for (int i = 0; i < 13; i++) {
        sim_tilt(0, lean_for(tune[i], N));
        blow(0.11f); sim_run(i == 6 ? 0.6f : 0.32f);
        if (i == 6) { blow(0.2f); sim_run(0.5f); }
        if (i == 9) { SI.jx = 0.8f; sim_run(0.3f); SI.jx = 0; }
        if (i == 4) sim_shot("breath_tune");
        blow(0); sim_run(0.06f);
    }
    blow(0); sim_run(1.0f);
    sim_gif_end();
    sim_wav("-breath", sim_t - 7.5f, sim_t);
    // leave things as the host's tests left them: the mic off, level tilt, the stick centred, the knob on the layers
    SI.mic = NULL; SI.mic_u = NULL; sim_tilt(0, 0); SI.jx = SI.jy = 0;
    memset(meta.quick, 0, 4);
}
