// CHIRP on the host: a crowd of stand-in devices (sim_ble.c) sending real advertising payloads through the
// firmware's own parser; checks of what the instrument makes of them, and the animations.
#include "sim.h"
#include "sim_ble.h"
#include "inst_chirp.h"
#include "params.h"
#include "oled.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { NULL };

static void setp(const char *tag, float v) { int p = param_by_tag(tag); if (p < 0) { note("no param %s", tag); return; } param_set(p, v); }

void inst_excite(void)
{
    // a steady texture: the default crowd as a dense swarm over a bed of air
    sim_crowd_default();
    sim_tilt(0, 0); SI.jx = SI.jy = 0;
    setp("mode", MODE_SWARM); setp("dens", 48); setp("air ", 100);
}

// ---------------------------------------------------------------- helpers
static int bird_of(uint32_t who) { for (int i = 0; i < MAXB; i++) if (birds[i].alive && birds[i].who == who) return i; return -1; }
static int note_ons(float t0, float t1, int *notes, int max)
{
    int n = 0;
    for (int i = 0; i < midi_n; i++) {
        midi_ev_t *e = &midi_log[i];
        if (e->t < t0 || e->t >= t1 || (e->st & 0xF0) != 0x90 || !e->d2) continue;
        if (notes && n < max) notes[n] = e->d1;
        n++;
    }
    return n;
}
// onsets: a fast envelope jumping over a slow one, 25 ms apart at least; times in seconds
static int onsets(float t0, float t1, float *at, int max)
{
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR), n = 0, dead = 0;
    if (b > sim_n) b = sim_n;
    float fast = 0, slow = 0, ka = 1.0f - expf(-1.0f / (0.0005f * AUDIO_SR)), kr = 1.0f - expf(-1.0f / (0.006f * AUDIO_SR)), ks = 1.0f - expf(-1.0f / (0.05f * AUDIO_SR));
    for (int i = a; i < b; i++) {
        float x = fabsf(sim_out[i]);
        fast += (x > fast ? ka : kr) * (x - fast);
        float sb = slow;
        slow += ks * (x - slow);
        if (dead > 0) { dead--; continue; }
        if (fast > 0.006f && fast > 2.5f * sb + 0.002f) { if (n < max) at[n] = (float)i / AUDIO_SR; n++; dead = (int)(0.025f * AUDIO_SR); }
    }
    return n < max ? n : max;
}
static float call_duration(float t0, float t1)
{
    // how long the envelope stays above a tenth of its peak
    int a = (int)(t0 * AUDIO_SR), b = (int)(t1 * AUDIO_SR);
    float env = 0, pk = 0;
    static float e[AUDIO_SR * 3];
    int n = b - a; if (n > AUDIO_SR * 3) n = AUDIO_SR * 3;
    for (int i = 0; i < n; i++) { float x = fabsf(sim_out[a + i]); env += (x > env ? 0.2f : 0.004f) * (x - env); e[i] = env; if (env > pk) pk = env; }
    int first = -1, last = -1;
    for (int i = 0; i < n; i++) if (e[i] > 0.1f * pk) { if (first < 0) first = i; last = i; }
    return first < 0 ? 0 : (float)(last - first) / AUDIO_SR;
}
static void quiet_room(void) { sim_crowd_clear(); float f = pv[param_by_tag("frgt")]; sim_run(f + 2.0f); }

// ---------------------------------------------------------------- the checks
static void parser(void)
{
    int right = 0, n = sim_payload_count();
    for (int p = 0; p < n; p++) {
        int got = sim_payload_parse(p);
        if (got == sim_payload_kind(p)) right++;
        else note("%s parsed as %s, not %s", sim_payload_name(p), cat_name[got], cat_name[sim_payload_kind(p)]);
    }
    check(right == n, "the parser names all %d kinds of advertisement right (Apple Continuity, Fast Pair, Find My, iBeacon, Eddystone, appearance...)", n);
}

static void arrivals(void)
{
    quiet_room();
    check(bird_count == 0, "a quiet room: every bird has flown off after FORGET (%d left)", bird_count);
    sim_shot("empty");
    sim_crowd_default();
    sim_gif_begin("arrive");
    float t0 = sim_t;
    sim_run(1.0f);
    int early = bird_count;
    sim_run(7.0f);
    sim_gif_end();
    check(early >= 8 && bird_count == 12, "a room of 12 devices: %d birds within a second, %d after 8 s", early, bird_count);
    sim_shot("arrived");
    sim_wav("-arrive", t0, sim_t);
    sim_run(14.0f);
    int good = 0, tot = 0;
    for (int d = 0; d < SIM_DEVS; d++) {
        if (!sim_dev[d].on || sim_dev[d].sent < 10) continue;
        int b = bird_of(sim_dev[d].who);
        if (b < 0) continue;
        tot++;
        float err = fabsf(birds[b].interval - sim_dev[d].interval - 0.005f) / sim_dev[d].interval;
        if (err < 0.15f) good++; else note("%s every %.2f s: estimated %.2f s", sim_payload_name(sim_dev[d].payload), (double)sim_dev[d].interval, (double)birds[b].interval);
    }
    check(tot >= 10 && good == tot, "each bird's rhythm is its device's advertising interval within 15 %% (%d of %d, 12 %% of packets lost)", good, tot);
    // nearness follows signal strength
    int nb = bird_of(sim_dev[2].who), fb = bird_of(sim_dev[9].who);
    check(nb >= 0 && fb >= 0 && birds[nb].near > 0.8f && birds[fb].near < 0.1f, "the AirPods at -50 dBm sing near (%.2f), the beacon at -93 far (%.2f)", nb >= 0 ? (double)birds[nb].near : -1.0, fb >= 0 ? (double)birds[fb].near : -1.0);
    // categories reached the birds
    int cats_ok = 0;
    for (int d = 0; d < 12; d++) { int b = bird_of(sim_dev[d].who); if (b >= 0 && birds[b].cat == sim_dev[d].kind) cats_ok++; }
    check(cats_ok == 12, "every bird knows what kind of device it is (%d of 12)", cats_ok);
    // a device that leaves
    int before = bird_count;
    sim_dev[4].on = false;
    sim_run(pv[param_by_tag("frgt")] + 2.0f);
    check(bird_count == before - 1 && bird_of(sim_dev[4].who) < 0, "a device that goes quiet flies off after FORGET (%d -> %d birds)", before, bird_count);
    sim_dev[4].on = true; sim_dev[4].next = sim_t;
}

static void chorus(void)
{
    sim_tilt(0, 0);
    setp("mode", MODE_CHORUS);
    sim_run(3.0f);
    sim_gif_begin("chorus");
    float t0 = sim_t;
    sim_run(10.0f);
    sim_gif_end();
    sim_shot("chorus");
    sim_wav("-chorus", t0, sim_t);
    int notes[2048], n = note_ons(t0, sim_t, notes, 2048);
    float dens = pv[param_by_tag("dens")];
    check(n > 20 && (float)n / 10.0f <= dens * 1.15f + 0.5f, "CHORUS: %.1f calls a second, never more than CALLS/SEC (%.0f)", (double)n / 10.0, (double)dens);
    // every note is in D major pentatonic (the default scale and key)
    int out = 0;
    for (int i = 0; i < n && i < 2048; i++) { int pc = notes[i] % 12; if (pc != 2 && pc != 4 && pc != 6 && pc != 9 && pc != 11) out++; }
    check(out == 0, "every call is a note of D major pentatonic (%d of %d out)", out, n);
    float rms = m_rms(t0, sim_t), pk = m_peak(t0, sim_t);
    metric("chorus_rms_dbfs", 20.0 * log10(rms + 1e-9));
    metric("chorus_peak_dbfs", 20.0 * log10(pk + 1e-9));
    check(rms > 0.01f && pk < 0.6f, "a chorus at a sensible level: %.1f dBFS RMS, peaks %.1f dBFS", 20.0 * log10(rms), 20.0 * log10(pk));
    // fewer calls when asked
    setp("dens", 3);
    sim_run(2.0f);
    float t1 = sim_t;
    sim_run(10.0f);
    int n2 = note_ons(t1, sim_t, NULL, 0);
    check((float)n2 / 10.0f <= 3.6f && n2 >= 10, "CALLS/SEC 3: %.1f calls a second", (double)n2 / 10.0);
    setp("dens", 12);
    // MIDI: every note on gets its note off
    int on = 0, off = 0;
    for (int i = 0; i < midi_n; i++) { if (midi_log[i].t < t0) continue; uint8_t k = midi_log[i].st & 0xF0; if (k == 0x90 && midi_log[i].d2) on++; if (k == 0x80 || (k == 0x90 && !midi_log[i].d2)) off++; }
    check(abs(on - off) <= 32, "MIDI: %d notes on, %d off", on, off);
}

static void clocks(void)
{
    setp("mode", MODE_CLOCKS); setp("quan", 1); setp("bpm ", 120); setp("grid", 1); setp("swng", 0); setp("rate", 4);
    setp("spec", 1); setp("verb", 0); setp("air ", 0); setp("dist", 0);
    sim_run(3.0f);
    float t0 = sim_t;
    sim_gif_begin("clocks");
    sim_run(8.0f);
    sim_gif_end();
    sim_shot("clocks");
    sim_wav("-clocks", t0, sim_t);
    static float at[2000];
    int n = onsets(t0, sim_t, at, 2000), on_grid = 0;
    const float unit = 0.125f;                                   // a sixteenth at 120 BPM
    for (int i = 0; i < n; i++) { float e = at[i] - roundf(at[i] / unit) * unit; if (fabsf(e) < 0.006f) on_grid++; }
    check(n > 20 && on_grid >= n * 85 / 100, "CLOCKS + QUANTIZE: %d of %d pulses land on the sixteenth grid at 120 BPM (within 6 ms)", on_grid, n);
    // swing: the odd sixteenths come late
    setp("swng", 50);
    sim_run(2.0f);
    float t1 = sim_t;
    sim_run(8.0f);
    n = onsets(t1, sim_t, at, 2000);
    int odd = 0, odd_late = 0;
    for (int i = 0; i < n; i++) {
        float k = at[i] / unit; int ki = (int)floorf(k + 0.1f);
        float e = at[i] - (float)ki * unit;
        if (ki & 1) { odd++; if (fabsf(e - 0.25f * unit) < 0.008f) odd_late++; }
    }
    check(odd > 5 && odd_late >= odd * 75 / 100, "SWING 50 %%: %d of %d offbeat pulses come a quarter step late", odd_late, odd);
    setp("swng", 0); setp("quan", 0);
    // free clocks: each bird pulses at its own interval times SLOW DOWN
    sim_run(1.0f);
    float t2 = sim_t;
    sim_run(10.0f);
    int pulses = note_ons(t2, sim_t, NULL, 0);
    float want = 0;
    for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving && birds[i].interval > 0) want += 10.0f / fmaxf(birds[i].interval * 4.0f, 0.05f);
    check(fabsf((float)pulses - want) < 0.2f * want + 3.0f, "free CLOCKS: %d pulses in 10 s, the birds' intervals x4 predict %.0f", pulses, (double)want);
}

static void swarm(void)
{
    setp("mode", MODE_SWARM); setp("air ", 50); setp("verb", 40);
    sim_run(2.0f);
    float t0 = sim_t;
    sim_gif_begin("swarm");
    sim_run(6.0f);
    sim_gif_end();
    sim_shot("swarm");
    sim_wav("-swarm", t0, sim_t);
    int gaps = 0, wins = 0;
    for (float t = t0; t < sim_t - 0.05f; t += 0.05f) { wins++; if (m_rms(t, t + 0.05f) < 0.003f) gaps++; }
    check(gaps * 10 <= wins, "SWARM is a continuous flutter: %d of %d 50 ms windows silent", gaps, wins);
    metric("swarm_calls_per_s", calls_per_s);
}

static void hunt(void)
{
    sim_crowd_clear();
    sim_run(pv[param_by_tag("frgt")] + 2.0f);
    setp("mode", MODE_HUNT); setp("verb", 10); setp("air ", 0);
    int d = sim_crowd_add(2, -92, 0.25f);                        // earbuds in another room
    sim_run(3.0f);
    float t0 = sim_t, still_max = 0;
    for (int k = 0; k < 40; k++) { sim_run(0.1f); if (fabsf(hunt_trend) > still_max) still_max = fabsf(hunt_trend); }
    static float at[2000];
    float far_rate = (float)onsets(t0, sim_t, at, 2000) / 4.0f;
    sim_shot("hunt_far");
    check(still_max < 2.0f, "standing still (3 dB of noise on every advert) reads as neither nearer nor farther (at most %.1f dB/s)", (double)still_max);
    // walk towards them
    sim_gif_begin("hunt");
    float trend_max = -99;
    for (int k = 0; k <= 60; k++) { sim_dev[d].rssi = -92.0f + 47.0f * (float)k / 60.0f; sim_run(0.1f); if (hunt_trend > trend_max) trend_max = hunt_trend; }
    sim_run(1.0f);
    float t1 = sim_t; sim_run(3.0f);
    sim_gif_end();
    float near_rate = (float)onsets(t1, sim_t, at, 2000) / 3.0f;
    sim_shot("hunt_near");
    sim_wav("-hunt", t0, sim_t);
    check(near_rate > 5.0f * far_rate && near_rate > 8.0f, "HUNT: %.1f ticks a second far away, %.1f right next to it", (double)far_rate, (double)near_rate);
    check(trend_max > 2.0f, "walking towards it reads as NEARER (%.1f dB/s)", (double)trend_max);
    sim_crowd_default();
    setp("mode", MODE_CHORUS);
    sim_run(3.0f);
}

static void controls(void)
{
    setp("mode", MODE_CHORUS); setp("spec", 0);
    sim_run(2.0f);
    // the stick picks a bird
    SI.joy = true;
    SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.3f);
    int a = spot;
    SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.3f);
    check(a >= 0 && spot >= 0 && spot != a, "stick right picks a bird, again the next one (%d, then %d)", a, spot);
    sim_shot("spotlight");
    // lean shifts the birds up the scale
    int b = -1; for (int i = 0; i < MAXB; i++) if (birds[i].alive && !birds[i].leaving) { b = i; break; }
    int m0 = birds[b].midi;
    sim_tilt(35, 0); sim_run(0.6f);
    int m1 = birds[b].midi;
    sim_tilt(0, 0); sim_run(0.6f);
    check(m1 > m0, "leaning right moves the birds up the scale (%d -> %d)", m0, m1);
    // tilt: longer calls; and MIDI keys play a call at their pitch
    quiet_room();
    setp("spec", 2); setp("verb", 0);
    front_midi(0x90, 69, 110); float t0 = sim_t; sim_run(1.2f);
    float f = m_pitch_at(t0 + 0.05f, t0 + 0.2f, 200, 2000), dflat = call_duration(t0, t0 + 1.2f);
    check(fabsf(hz_to_midi(f) - 69.0f) < 1.2f, "MIDI note 69 in plays a call at A4 (%.0f Hz)", (double)f);
    sim_tilt(0, 30); sim_run(0.5f);
    front_midi(0x90, 69, 110); t0 = sim_t; sim_run(1.5f);
    float dtilt = call_duration(t0, t0 + 1.5f);
    sim_tilt(0, 0);
    check(dtilt > 1.7f * dflat, "tipping the bottom edge down makes the calls longer (%.2f s -> %.2f s)", (double)dflat, (double)dtilt);
    // MAX BIRDS keeps the nearest
    sim_crowd_default();
    setp("maxb", 6); setp("verb", 35);
    sim_run(4.0f);
    float weakest_kept = 0, strongest_left = -200;
    for (int d = 0; d < 12; d++) { int k = bird_of(sim_dev[d].who); bool kept = k >= 0 && !birds[k].leaving; if (kept && (weakest_kept == 0 || sim_dev[d].rssi < weakest_kept)) weakest_kept = sim_dev[d].rssi; if (!kept && sim_dev[d].rssi > strongest_left) strongest_left = sim_dev[d].rssi; }
    check(bird_count <= 6 && weakest_kept > strongest_left, "MAX BIRDS 6: %d birds, the nearest ones (weakest kept %.0f dBm, strongest left out %.0f)", bird_count, (double)weakest_kept, (double)strongest_left);
    setp("maxb", 18);
}

static void cpu(void)
{
    // the busiest it gets: a dense swarm, air, the reverb, all sixteen voices
    inst_excite();
    setp("verb", 60);
    sim_run(2.0f);
    static float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    struct timespec a, b;
    double worst = 0, total = 0;
    for (int k = 0; k < 2000; k++) {
        if (k % 5 == 0) sim_tick();
        clock_gettime(CLOCK_MONOTONIC, &a);
        front_audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double us = (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3;
        total += us; if (us > worst) worst = us;
    }
    metric("host_us_per_block", total / 2000.0);
    metric("host_us_per_block_worst", worst);
    check(total / 2000.0 < 20.0, "%.1f us of host time per 64-frame block, busiest case (budget 20)", total / 2000.0);
    setp("verb", 35); setp("mode", 0); setp("dens", 12); setp("air ", 20);
}

void inst_tests(void)
{
    // back to the first sound, flat, no stick
    for (int i = 0; i < 20; i++) sim_turn(-1);
    sim_run(0.5f);
    SI.joy = false; SI.jx = SI.jy = 0; sim_tilt(0, 0);
    parser();
    arrivals();
    chorus();
    clocks();
    swarm();
    hunt();
    controls();
    cpu();
    for (int i = 0; i < 20; i++) sim_turn(-1);
    sim_run(2.0f);
    sim_shot("play");
}
