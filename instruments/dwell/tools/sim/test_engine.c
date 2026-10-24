// DWELL's engine on the host: PURE is one sine at the ratio; the tilt's ratio sounds; DRIFT glides and swells; a
// cluster beats at BEAT; the difference tone; every source in tune at a sane level, and a change of source without a
// click; the tanpura's jawari makes its overtones climb; a sung tone becomes the WAVE table; ROOM PASS in a pretend
// room whose resonances take over pass by pass (and stops by itself where there is no room); the strum; the cost at
// the busiest. WAVs of each for listening.
#include "sim.h"
#include "inst_dwell.h"
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int  P(const char *tag) { int p = param_by_tag(tag); if (p < 0) { printf("no param %s\n", tag); exit(1); } return p; }
static void set(const char *tag, float v) { param_set(P(tag), v); }
static void tilt_cells(float b, float c) { sim_tilt(c * 10.0f, -b * 10.0f); }
static float quiet_mic(float t, void *u) { (void)t; (void)u; return 0.0008f * sim_noise(); }

// one voice, no root, sines, nothing dense, no hall, quick
static void plain(void)
{
    SI.mic = quiet_mic;
    set("srce", SRC_SINE); set("dens", 0); set("voic", 1); set("root", 0); set("drft", 0.5f); set("move", MOVE_SWELL);
    set("spce", 0); set("diff", 0); set("brit", 40); set("levl", 80); set("limt", LIM_7); set("skey", 0); set("octv", 3);
    set("copy", 6); set("beat", 0.5f); set("bsyn", 0); set("knck", 0); set("sprd", 0);
    tilt_cells(0, 0); run(2.0f);
}
static float f11(void) { return D.fund_hz; }

// the output's envelope (rms per 10 ms) between t0 and t1: its period by autocorrelation, lag_min..lag_max seconds
static float env_period(float t0, float t1, float lag_min, float lag_max)
{
    int i0 = (int)(t0 * AUDIO_SR), i1 = (int)(t1 * AUDIO_SR), w = AUDIO_SR / 100;
    int n = (i1 - i0) / w;
    float *e = calloc((size_t)n, sizeof(float)), mean = 0.0f;
    for (int k = 0; k < n; k++) { double s = 0; for (int i = 0; i < w; i++) { float x = sim_out[i0 + k * w + i]; s += (double)x * x; } e[k] = sqrtf((float)(s / w)); mean += e[k]; }
    mean /= (float)n;
    for (int k = 0; k < n; k++) e[k] -= mean;
    int l0 = (int)(lag_min * 100.0f), l1 = (int)(lag_max * 100.0f);
    float best = -1e30f; int bl = 0;
    for (int l = l0; l <= l1 && l < n - 1; l++) {
        double s = 0; for (int k = 0; k + l < n; k++) s += (double)e[k] * e[k + l];
        s /= (double)(n - l);
        if (s > best) { best = (float)s; bl = l; }
    }
    free(e);
    return (float)bl / 100.0f;
}

// the centroid of the first 20 harmonics of f between t0 and t1, in harmonics
static float centroid(float t0, float t1, float f)
{
    double num = 0, den = 0;
    for (int h = 1; h <= 20; h++) { double p = m_goertzel(t0, t1, f * (float)h); num += h * p; den += p; }
    return den > 0 ? (float)(num / den) : 0.0f;
}

// ---------------------------------------------------------------- a pretend room (Lucier's)
// The speaker's sound comes back into the mic 30 ms later: a little of it straight, most of it through the room's two
// resonances (340 and 510 Hz, Q 30), at about a third of the output, plus the room's hiss.
static biquad_t rm_a, rm_b;
static float room_gain = 1.0f;
float room_mic(float t, void *u)
{
    (void)u;
    int k = (int)(t * AUDIO_SR + 0.5f) - 960;
    float x = k >= 0 && k < sim_n ? sim_out[k] : 0.0f;
    float y = 0.15f * x + 1.0f * biquad_run(&rm_a, x) + 0.8f * biquad_run(&rm_b, x);
    return room_gain * 0.35f * y + 0.0008f * sim_noise();
}
void room_mic_reset(void)
{
    biquad_set(&rm_a, BQ_BP, 340.0f, 30.0f, 0.0f); biquad_set(&rm_b, BQ_BP, 510.0f, 30.0f, 0.0f);
    rm_a.z1 = rm_a.z2 = rm_b.z1 = rm_b.z2 = 0.0f;
    room_gain = 1.0f;
}

// ---------------------------------------------------------------- the checks
static void pure(void)
{
    section("engine: PURE");
    plain();
    float t = sim_t;
    run(2.0f);
    float f = f11(), p = m_pitch_at(t + 0.5f, t + 2.0f, 60.0f, 600.0f);
    check(fabsf(p / f - 1.0f) < 0.003f, "DENSE 0, one voice: a sine at 1/1 (%.2f Hz for %.2f)", (double)p, (double)f);
    float p1 = m_goertzel(t + 0.5f, t + 2.0f, f), p2 = m_goertzel(t + 0.5f, t + 2.0f, 2.0f * f), p3 = m_goertzel(t + 0.5f, t + 2.0f, 3.0f * f);
    check(p2 < p1 * 1e-3f && p3 < p1 * 1e-3f, "and nothing else: 2f %.0f dB, 3f %.0f dB under f", (double)(10.0f * log10f(p1 / (p2 + 1e-20f))), (double)(10.0f * log10f(p1 / (p3 + 1e-20f))));
    float r = m_rms(t + 0.5f, t + 2.0f), pk = m_peak(t + 0.5f, t + 2.0f);
    metric("pure_rms", r);
    check(pk > 0.03f && pk < 0.3f, "one pure voice peaks at %.3f (about -20 dBFS)", (double)pk);
    sim_wav("-pure", t, t + 2.0f);

    section("engine: the ratio the tilt picks");
    set("root", 1);
    tilt_cells(1, 0); run(3.0f);
    t = sim_t; run(1.5f);
    float a = m_goertzel(t, t + 1.5f, f * 0.5f), b = m_goertzel(t, t + 1.5f, f * 1.5f), c = m_goertzel(t, t + 1.5f, f);
    check(a > 100.0f * c && b > 100.0f * c, "tipped one step: the root at f/2 and the lead at 3/2 sound, 1/1 is gone (%.2g %.2g against %.2g)", (double)a, (double)b, (double)c);
    set("root", 0); set("voic", 1);
    tilt_cells(0, 1); run(3.0f);
    t = sim_t; run(1.5f);
    p = m_pitch_at(t, t + 1.5f, 60.0f, 600.0f);
    check(fabsf(p / (f * 1.75f) - 1.0f) < 0.003f, "rolled right: 7/4, %.2f Hz for %.2f (31 cents under the piano's B flat)", (double)p, (double)(f * 1.75f));
    tilt_cells(0, 0); run(2.0f);
}

static void drift(void)
{
    section("engine: DRIFT");
    plain();
    set("move", MOVE_GLIDE); set("drft", 4.0f);
    float f = f11();
    tilt_cells(1, 0);
    for (int i = 0; i < 100 && strcmp(D.lead_name, "3/2"); i++) sim_tick();
    float t = sim_t;
    run(0.5f);
    float h05 = eng_slot_hz_now[0] > 0 ? eng_slot_hz_now[0] : 0;
    for (int i = 0; i < NV; i++) if (D.slot[i].is_lead) h05 = eng_slot_hz_now[i];
    run(4.5f);
    float h5 = 0; for (int i = 0; i < NV; i++) if (D.slot[i].is_lead) h5 = eng_slot_hz_now[i];
    check(h05 > f * 1.02f && h05 < f * 1.5f * 0.95f, "GLIDE, DRIFT 4 s: half a second in it is on the way (%.1f Hz, from %.1f to %.1f)", (double)h05, (double)f, (double)(f * 1.5f));
    check(fabsf(h5 / (f * 1.5f) - 1.0f) < 0.01f, "after 5 s it is there (%.2f Hz)", (double)h5);
    float p = m_pitch_at(sim_t - 0.3f, sim_t, 60.0f, 600.0f);
    check(fabsf(p / h5 - 1.0f) < 0.005f, "and the sound agrees with it (%.2f Hz)", (double)p);
    sim_wav("-glide", t - 0.5f, sim_t);

    plain();
    set("voic", 2); set("drft", 4.0f);
    tilt_cells(1, 0);
    for (int i = 0; i < 100 && strcmp(D.lead_name, "3/2"); i++) sim_tick();
    t = sim_t;
    int k = -1; for (int i = 0; i < NV; i++) if (D.slot[i].is_lead) k = i;
    run(1.0f);
    float l1 = eng_slot_lvl_now[k];
    run(4.0f);
    float l5 = eng_slot_lvl_now[k];
    check(k >= 0 && l1 > 0.2f && l1 < 0.75f && l5 > 0.9f, "SWELL: the new tone swells in over seconds (%.2f after 1 s, %.2f after 5 s)", (double)l1, (double)l5);
    float hz = eng_slot_hz_now[k];
    check(fabsf(hz / (f * 1.5f) - 1.0f) < 0.002f, "on its own pitch from the start (%.2f Hz)", (double)hz);
    sim_wav("-swell", t - 0.5f, sim_t);
    tilt_cells(0, 0); run(1.0f);
}

static void clusters(void)
{
    section("engine: clusters beat");
    plain();
    set("brit", 0); set("copy", 4); set("beat", 1.0f); set("dens", 100);
    run(3.0f);
    check(eng_copies_now == 4, "DENSE 100, COPIES 4: four copies (%d)", eng_copies_now);
    float t = sim_t;
    run(6.0f);
    float per = env_period(t, sim_t, 0.4f, 2.5f);
    check(fabsf(per - 1.0f) < 0.05f, "BEAT 1 Hz: the sound swells and fades once a second (period %.2f s)", (double)per);
    sim_wav("-beat1", t, sim_t);
    set("beat", 0.25f);
    run(3.0f);
    t = sim_t;
    run(14.0f);
    per = env_period(t, sim_t, 2.0f, 6.0f);
    check(fabsf(per - 4.0f) < 0.2f, "BEAT 0.25 Hz: every 4 s (period %.2f s)", (double)per);
    float pd = m_peak(t, sim_t);
    set("dens", 0); run(2.0f);
    float pp = m_peak(sim_t - 1.0f, sim_t);
    check(pd > pp * 1.4f && pd < pp * 3.0f, "the cluster is louder than the pure tone (%.2f against %.2f, x%.1f)", (double)pd, (double)pp, (double)(pd / pp));
    set("dens", 100); set("copy", 8); set("beat", 0.3f); set("brit", 60); set("srce", SRC_BOW);
    run(2.0f);
    t = sim_t; run(6.0f);
    sim_wav("-wall", t, sim_t);
    float pk = m_peak(t, sim_t);
    check(pk < 0.6f, "eight bowed copies, bright: peak %.2f", (double)pk);
}

static void difftone(void)
{
    section("engine: the difference tone");
    plain();
    set("root", 1);
    tilt_cells(0, 1); run(3.0f);
    float f = f11(), fd = f * 1.75f - f * 0.5f;
    float t = sim_t; run(1.5f);
    float off = m_goertzel(t, sim_t, fd);
    set("diff", 100); run(2.0f);
    t = sim_t; run(1.5f);
    float on = m_goertzel(t, sim_t, fd);
    check(on > 100.0f * off, "7/4 over the root: DIFF TONE 100 adds %.1f Hz (power x%.0f)", (double)fd, (double)(on / (off + 1e-20f)));
    check(fabsf(eng_diff_hz - fd) < 0.5f, "and reports it (%.1f Hz)", (double)eng_diff_hz);
    sim_wav("-diff", t, sim_t);
    set("diff", 0); tilt_cells(0, 0); run(1.0f);
}

static void sources(void)
{
    section("engine: every source");
    static const char *const name[] = { "sine", "reed", "bow", "tanpura", "shruti", "gurdy", "wave" };
    int bad = 0, clicks = 0;
    plain();
    set("brit", 50); set("dens", 30);
    for (int s = SRC_SINE; s <= SRC_WAVE; s++) {
        set("srce", (float)s);
        float tc = sim_t;
        run(3.0f);
        float c = m_click(tc, NULL, NULL);
        if (c > 3.0f) clicks++;
        float t = sim_t;
        run(3.0f);
        float r = m_rms(t, sim_t), pk = m_peak(t, sim_t), p = m_pitch_at(t, sim_t, 60.0f, 600.0f);
        bool ok = r > 0.008f && r < 0.3f && pk < 0.9f && fabsf(p / f11() - 1.0f) < 0.01f;
        note("%s: rms %.3f, peak %.2f, pitch %.2f Hz for %.2f, change %.1f%s", name[s], (double)r, (double)pk, (double)p, (double)f11(), (double)c, ok ? "" : "  <-- off");
        bad += !ok;
        char wn[32]; snprintf(wn, sizeof wn, "-src-%s", name[s]);
        sim_wav(wn, t, sim_t);
    }
    check(bad == 0, "every source sounds at a sane level and in tune (%d not)", bad);
    check(clicks == 0, "and a change of source never clicks (%d did)", clicks);
}

static void tanpura(void)
{
    section("engine: the tanpura's jawari");
    plain();
    set("srce", SRC_TANPURA); set("pluk", 4.0f);
    float f = f11(), dip[2], bloom[2], tl[2][8];   // dip: the brightness at the pluck
    static const float at[] = { 0.0f, 0.1f, 0.2f, 0.35f, 0.5f, 0.7f, 1.0f, 1.3f };
    // four plucks each (a pluck is noise: one alone says little), averaged
    for (int j = 0; j < 2; j++) {
        set("jawa", j ? 60.0f : 0.0f);
        run(1.0f);
        for (int q = 0; q < 8; q++) tl[j][q] = 0.0f;
        dip[j] = bloom[j] = 0.0f;
        for (int k = 0; k < 4; k++) {
            // a strum plucks the string at a moment we know and starts the cycle again (the next pluck PLUCK = 4 s
            // later): one to clear the cycle, 3.9 s for that pluck to die away (-39 dB), then the one we measure
            eng_strum(0.5f);
            run(3.9f);
            float tp = sim_t;
            eng_strum(0.5f);
            run(1.5f);
            for (int q = 0; q < 8; q++) tl[j][q] += 0.25f * centroid(tp + 0.02f + at[q], tp + 0.1f + at[q], f);
            dip[j] += 0.25f * centroid(tp + 0.02f, tp + 0.1f, f);          // at the pluck
            bloom[j] += 0.25f * centroid(tp + 0.35f, tp + 0.8f, f);        // a third of a second on
            if (k == 0) { char wn[32]; snprintf(wn, sizeof wn, "-tanpura-jawari%d", j ? 60 : 0); sim_wav(wn, tp - 0.2f, sim_t); }
        }
    }
    for (int j = 0; j < 2; j++)
        note("JAWARI %d, brightness (centroid, harmonics) at 0 .1 .2 .35 .5 .7 1 1.3 s: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f", j ? 60 : 0,
             (double)tl[j][0], (double)tl[j][1], (double)tl[j][2], (double)tl[j][3], (double)tl[j][4], (double)tl[j][5], (double)tl[j][6], (double)tl[j][7]);
    check(bloom[1] > 1.15f * dip[1], "JAWARI 60: after the pluck the overtones come up (centroid %.2f at the pluck, %.2f from 0.35 s)", (double)dip[1], (double)bloom[1]);
    check(bloom[0] < dip[0], "JAWARI 0: a plain string only darkens (%.2f -> %.2f)", (double)dip[0], (double)bloom[0]);
    check(bloom[1] > 1.6f * bloom[0], "the jawari string is far brighter in its sustain (%.2f against %.2f)", (double)bloom[1], (double)bloom[0]);
    set("jawa", 50); set("pluk", 0.8f);
    run(1.0f);
    float t = sim_t; run(6.0f);
    sim_wav("-tanpura", t, sim_t);
}

static float tone_hz, tone_amp;
static double tone_ph;
static float tone_mic(float t, void *u)
{
    (void)t; (void)u;
    tone_ph += (double)tone_hz / AUDIO_SR;
    if (tone_ph > 1.0) tone_ph -= 1.0;
    float p = (float)tone_ph;
    return 0.002f * sim_noise() + tone_amp * (0.7f * sinf(6.2831853f * p) + 0.3f * sinf(12.566371f * p + 0.4f) + 0.15f * sinf(18.849556f * p + 1.1f));
}

static void wave(void)
{
    section("engine: your tone");
    plain();
    SI.mic = tone_mic; tone_hz = 220.0f; tone_amp = 0.25f;
    SI.rec = true; run(1.6f); SI.rec = false; run(0.1f);
    for (int i = 0; i < 300 && D.analysing; i++) sim_tick();
    check(fabsf(eng_wave_hz - 220.0f) < 2.2f, "a sung 220 Hz is heard as %.2f Hz", (double)eng_wave_hz);
    check((int)pv[P_SOURCE] == SRC_WAVE, "and the source is WAVE");
    SI.mic = quiet_mic; tone_amp = 0.0f;
    set("skey", 9); set("octv", 2);
    run(2.0f);
    float t = sim_t; run(2.0f);
    float p = m_pitch_at(t, sim_t, 50.0f, 600.0f);
    check(fabsf(p / 110.0f - 1.0f) < 0.005f, "on 1/1 = A2 it plays at %.2f Hz: the table is one period", (double)p);
    float h2 = m_goertzel(t, sim_t, 220.0f), h1 = m_goertzel(t, sim_t, 110.0f);
    check(h2 > h1 * 0.05f, "with the tone's own second harmonic (%.0f dB under the first)", (double)(10.0f * log10f(h1 / (h2 + 1e-20f))));
    sim_wav("-wave", t, sim_t);
    float keep = eng_wave_hz;
    SI.rec = true; run(1.2f); SI.rec = false; run(0.1f);
    for (int i = 0; i < 300 && D.analysing; i++) sim_tick();
    check(eng_wave_hz == 0.0f, "a take of a quiet room: no tone heard (was %.0f Hz), the table kept", (double)keep);
    set("skey", 0); set("octv", 3); set("srce", SRC_SINE);
    run(1.0f);
}

static void room(void)
{
    section("engine: ROOM PASS in a pretend room");
    plain();
    set("srce", SRC_BOW); set("brit", 70); set("dens", 40); set("root", 1); set("voic", 2);
    tilt_cells(1, 0); run(2.0f);
    room_mic_reset();
    SI.mic = room_mic;
    set("pass", 6); set("plen", 2);
    run(0.5f);
    float t0 = sim_t;
    INST.action(P_PASS);
    sim_tick();                                                        // the audio task takes the request
    for (int i = 0; i < 2000 && eng_room_state == ROOM_PASSING; i++) sim_tick();
    float t1 = sim_t;
    check(eng_room_state == ROOM_DONE && eng_room_have && eng_room_pass == 6, "six passes of 2 s ran (state %d, pass %d, %.1f s)", eng_room_state, eng_room_pass, (double)(t1 - t0));
    // what played in pass 1 (the drone heard once through the room) and in pass 5 (five times)
    float f = f11();
    float ratio[2];
    for (int j = 0; j < 2; j++) {
        float a = t0 + (float)(j ? 5 : 1) * 2.0f + 0.2f, b = a + 1.6f;
        float res = m_goertzel(a, b, 340.0f) + m_goertzel(a, b, 510.0f) + m_goertzel(a, b, f * 2.5f) + m_goertzel(a, b, f * 4.0f);
        float drone = m_goertzel(a, b, f * 0.5f) + m_goertzel(a, b, f) + m_goertzel(a, b, f * 1.5f);
        ratio[j] = res / (drone + 1e-20f);
    }
    note("the room's resonances against the drone's own notes: x%.3g in pass 2, x%.3g in pass 6", (double)ratio[0], (double)ratio[1]);
    check(ratio[1] > 10.0f * ratio[0], "pass by pass the room's resonances take over (x%.0f)", (double)(ratio[1] / ratio[0]));
    check((int)pv[P_SOURCE] == SRC_ROOM, "afterwards the source is ROOM");
    sim_wav("-roompass", t0, t1);
    run(4.0f);
    float t = sim_t; run(4.0f);
    float r = m_rms(t, sim_t), pk = m_peak(t, sim_t);
    check(r > 0.005f && pk < 0.9f, "the room plays as a drone (rms %.3f, peak %.2f)", (double)r, (double)pk);
    sim_wav("-room", t, sim_t);
    int len = 0; const int16_t *lp = eng_room_loop(&len);
    check(lp && len == 2 * AUDIO_SR, "its loop is the last pass, %d frames", len);

    section("engine: ROOM PASS on headphones");
    SI.mic = quiet_mic;
    set("srce", SRC_SINE); run(0.5f);
    INST.action(P_PASS);
    sim_tick();
    for (int i = 0; i < 600 && eng_room_state == ROOM_PASSING; i++) sim_tick();
    check(eng_room_state == ROOM_IDLE && eng_room_pass <= 1, "the mic hears no room: it stops after the first pass (state %d, pass %d)", eng_room_state, eng_room_pass);
    check(eng_room_have, "and the room made before is kept");
    run(1.0f);
}

static void strum(void)
{
    section("engine: the strum");
    plain();
    set("voic", 3); set("root", 1); set("strm", 2.0f);
    tilt_cells(1, 0); run(1.5f); tilt_cells(1, 1); run(3.0f);
    float before = m_rms(sim_t - 0.5f, sim_t);
    float t = sim_t;
    eng_strum(1.0f);
    run(0.3f);
    float up = m_rms(sim_t - 0.2f, sim_t);
    float c = m_click(t, NULL, NULL);
    run(3.0f);
    float after = m_rms(sim_t - 0.3f, sim_t);
    check(up > before * 1.6f, "a strum lifts every tone (rms %.3f -> %.3f)", (double)before, (double)up);
    check(after < before * 1.2f, "and rings out over STRUM (back to %.3f after 3 s)", (double)after);
    check(c <= 3.0f, "without a click (%.1f)", (double)c);
    sim_wav("-strum", t - 0.5f, sim_t);
    tilt_cells(0, 0); run(1.0f);
}

static void cost(void)
{
    section("engine: the cost");
    static const int src[] = { SRC_SINE, SRC_REED, SRC_BOW, SRC_TANPURA, SRC_SHRUTI, SRC_GURDY, SRC_WAVE, SRC_ROOM };
    static const char *const name[] = { "SINE", "REED", "BOW", "TANPURA", "SHRUTI", "GURDY", "WAVE", "ROOM" };
    plain();
    set("dens", 100); set("copy", 8); set("voic", 6); set("root", 1); set("spce", 100); set("tail", 100); set("diff", 100); set("brit", 100);
    float cells[5][2] = { { 1, 0 }, { 1, 1 }, { 2, 1 }, { 2, 2 }, { 0, -1 } };
    for (int k = 0; k < 5; k++) { tilt_cells(cells[k][0], cells[k][1]); run(0.8f); }
    run(1.0f);
    int on = 0; for (int i = 0; i < NV; i++) on += D.slot[i].on;
    check(on == 6, "six tones sound (%d)", on);
    double worst = 0, worst_max = 0; const char *wn = "";
    for (int s = 0; s < 8; s++) {
        set("srce", (float)src[s]);
        run(0.5f);
        eng_strum(1.0f);
        eng_audio_ns = 0; eng_audio_max_ns = 0; eng_audio_blocks = 0;
        float t = sim_t;
        run(3.0f);
        double mean = eng_audio_ns / (double)eng_audio_blocks / 1000.0, mx = eng_audio_max_ns / 1000.0;
        note("%s: %.1f us a block on average, %.1f at most; peak %.2f", name[s], mean, mx, (double)m_peak(t, sim_t));
        if (mean > worst) { worst = mean; worst_max = mx; wn = name[s]; }
    }
    metric("engine_us_busiest", worst);
    metric("engine_us_busiest_max", worst_max);
    check(worst < 15.0, "the busiest (%s, 6 tones x 8 copies, the hall, the difference tone, a strum): %.1f us a block (budget 15)", wn, worst);
    set("srce", SRC_SINE); set("dens", 0); set("voic", 4); set("spce", 25); set("tail", 50); set("diff", 0); set("brit", 40);
    tilt_cells(0, 0); run(2.0f);
}

// DWELL_ENGINE=tanpura (or pure, drift, clusters, diff, sources, wave, room, strum, cost) runs only that section
static bool want(const char *name) { const char *e = getenv("DWELL_ENGINE"); return !e || !*e || strstr(e, name); }
void test_engine(void)
{
    float t0 = sim_t;
    if (want("pure")) pure();
    if (want("drift")) drift();
    if (want("clusters")) clusters();
    if (want("diff")) difftone();
    if (want("sources")) sources();
    if (want("tanpura")) tanpura();
    if (want("wave")) wave();
    if (want("room")) room();
    if (want("strum")) strum();
    if (want("cost")) cost();
    section("engine: health");
    check(m_bad() == 0, "no NaN or inf");
    float pk = m_peak(t0, sim_t);
    check(pk < 0.9f, "never over 0.9 in the engine's sections (peak %.2f)", (double)pk);
    plain();
    set("voic", 4); set("root", 1);
}
