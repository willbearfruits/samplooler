// SNIFF on the host: the made-up flat of radio_host.c. AUTO finds the busiest channel; the room plays at
// a sane level; a person walking past the router moves the ROOM layer; a microwave oven makes a storm
// (and a clap of thunder when it starts); HUNT finds a phone and beeps faster as it comes nearer; the grid
// puts notes on the beat; a crowd stays inside the density budget (the rest is rain) and under full
// scale; tilting lengthens the rings; the stick narrows the reach; HOP walks the channels; MIDI goes out.
#include "sim.h"
#include "inst.h"
#include "looper.h"
#include <string.h>
#include <time.h>

extern float hr_walker, hr_microwave, hr_target_rssi;
extern int hr_crowd, hr_channel;
extern bool hr_on;
extern uint32_t sniff_dbg_at[64], sniff_dbg_n;
void sim_menu_close(void);

// Nothing left out: every param is smoothed or only changes what the next frame will sound like.
const char *const inst_click_skip[] = { NULL };

static uint32_t who_of(int i) { return (0x9e3779b9u * (uint32_t)(i + 1)) ^ 0x5bd1e995u; }

static void sound(const char *name)
{
    sim_menu_close();
    int want = -1;
    for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) want = i;
    for (int k = 0; k < 30; k++) sim_turn(-3);
    if (meta.sound == -1) sim_turn(1);
    for (int k = 0; k < want; k++) sim_turn(1);
    sim_run(0.05f);
    check(meta.sound == want, "the knob finds the sound %s", name);
}

void inst_excite(void)
{
    hr_on = true; hr_walker = 0.3f; hr_microwave = 0; hr_crowd = 0;
    param_set(P_CHAN, 2 + 5);                                   // channel 6, where the router is
    sim_tilt(0, 0); SI.jx = SI.jy = 0;
}

static float avg_level(volatile float *src, float seconds)
{
    double s = 0; int n = (int)(seconds * 100.0f);
    for (int i = 0; i < n; i++) { sim_tick(); s += *src; }
    return (float)(s / n);
}

static int notes_on(float t0)
{
    int n = 0;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].t >= t0 && (midi_log[i].st & 0xF0) == 0x90 && midi_log[i].d2 > 0) n++;
    return n;
}

void inst_tests(void)
{
    hr_on = true; hr_walker = 0; hr_microwave = 0; hr_crowd = 0;
    midi_n = 0;                                                 // the core's checks filled the log
    sound("THE ROOM");
    sim_tilt(0, 0);

    // ---------------------------------------------------------------- AUTO finds the busiest channel
    section("channels");
    param_set(P_CHAN, CH_AUTO);
    sim_run(1.0f);
    check(S.scanning, "AUTO starts by listening to every channel");
    sim_shot("scanning");
    sim_run(4.5f);
    check(!S.scanning && S.chan == 6, "after one pass it settles on channel 6, the busiest (on %d)", S.chan);
    metric("frames_per_s_on_6", S.hist[6]);

    // ---------------------------------------------------------------- the room plays
    section("listening");
    float t0 = sim_t;
    sim_gif_begin("play");
    sim_run(4.0f);
    hr_walker = 1.0f;
    sim_run(4.0f);
    sim_gif_end();
    hr_walker = 0;
    float rms = m_rms(t0 + 1.0f, t0 + 4.0f), pk = m_peak(t0 + 1.0f, t0 + 4.0f);
    check(rms > 0.01f && rms < 0.25f && pk < 0.9f, "the flat on channel 6 plays at a sane level (rms %.3f, peak %.3f)", (double)rms, (double)pk);
    check(S.ndev >= 5, "it has heard the flat's devices (%d)", S.ndev);
    int nn = notes_on(t0);
    check(nn > 20, "every sounding frame goes out as a MIDI note (%d in 8 s)", nn);
    int cc3 = 0;
    for (int i = 0; i < midi_n; i++) if (midi_log[i].t >= t0 && (midi_log[i].st & 0xF0) == 0xB0 && midi_log[i].d1 == 3) cc3++;
    check(cc3 > 0, "the frame rate goes out on CC 3 (%d messages)", cc3);
    check(sim_led[0] || sim_led[1] || sim_led[2], "the LED lights with the frames (%d %d %d)", sim_led[0], sim_led[1], sim_led[2]);
    sim_shot("play");

    // ---------------------------------------------------------------- a person walks past the router
    section("room");
    hr_walker = 0;
    sim_run(6.0f);
    float still_m = S.motion, still_room = avg_level(&snd_out_room, 3.0f);
    sim_gif_begin("ghost");
    hr_walker = 1.0f;
    sim_run(3.0f);
    float walk_m = S.motion, walk_room = avg_level(&snd_out_room, 3.0f);
    sim_shot("someone_walking");
    sim_gif_end();
    hr_walker = 0;
    check(S.room_live, "the router's CSI is arriving (the ROOM's source)");
    check(still_m < 0.25f && walk_m > 0.5f, "nobody moving: motion %.2f; someone walking: %.2f", (double)still_m, (double)walk_m);
    check(walk_room > 2.0f * still_room, "the ROOM layer swells when someone walks (%.4f -> %.4f)", (double)still_room, (double)walk_room);

    // ---------------------------------------------------------------- a microwave oven
    section("storm");
    sim_run(3.0f);
    float calm_storm = avg_level(&snd_out_storm, 2.0f);
    uint32_t th0 = sc.thunder;
    sim_gif_begin("storm");
    hr_microwave = 1.0f;
    sim_run(1.5f);
    sim_shot("storm");
    float oven_storm = avg_level(&snd_out_storm, 2.5f);
    sim_gif_end();
    check(S.storm > 0.4f, "the oven's broken frames and noise make a storm (%.2f)", (double)S.storm);
    check(sc.thunder != th0, "and a clap of thunder when it starts");
    check(oven_storm > 3.0f * calm_storm + 0.002f, "the STORM layer is up (%.4f -> %.4f)", (double)calm_storm, (double)oven_storm);
    hr_microwave = 0;
    sim_run(8.0f);
    check(S.storm < 0.15f, "oven off: the storm passes (%.2f)", (double)S.storm);

    // ---------------------------------------------------------------- HUNT
    section("hunt");
    hr_target_rssi = -88.0f;
    sound("HUNT");
    sim_run(1.0f);
    check(S.mode == MODE_HUNT && S.target >= 0, "HUNT picks a device to follow");
    int flicks = 0;
    while (flicks < 20 && !(S.target >= 0 && S.dev[S.target].who == who_of(4))) { SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.05f); flicks++; }
    check(S.target >= 0 && S.dev[S.target].who == who_of(4), "a flick of the stick moves to the next device until it is the phone (%d flicks)", flicks);
    sim_run(2.0f);
    uint32_t b0 = snd_beeps; sim_run(2.0f); float far_rate = (float)(snd_beeps - b0) / 2.0f;
    float far_pitch = sc.hunt_pitch;
    sim_gif_begin("hunt");
    int warmer = 0;
    for (int i = 0; i <= 100; i++) { hr_target_rssi = -88.0f + 48.0f * (float)i / 100.0f; sim_run(0.08f); if (S.trend > 0) warmer++; }
    sim_shot("hunt_near");
    b0 = snd_beeps; sim_run(2.0f); float near_rate = (float)(snd_beeps - b0) / 2.0f;
    sim_gif_end();
    check(near_rate > 3.0f * far_rate && sc.hunt_pitch > 2.0f * far_pitch, "walking up to it: %.1f -> %.1f beeps a second, %.0f -> %.0f Hz", (double)far_rate, (double)near_rate, (double)far_pitch, (double)sc.hunt_pitch);
    check(warmer > 30, "it says WARMER on the way (%d of 101 steps)", warmer);
    check(S.chan == 6, "it stays on the phone's channel");
    hr_target_rssi = -80.0f;

    // ---------------------------------------------------------------- the grid
    section("rhythm");
    sound("MUSIC BOX");
    sim_run(1.0f);
    uint32_t n0 = sniff_dbg_n;
    sim_run(3.0f);
    int on = 0, tot = 0;
    float step = 60.0f / 96.0f * 0.25f * (float)AUDIO_SR;
    for (uint32_t i = n0; i < sniff_dbg_n && tot < 64; i++) {
        uint32_t a = sniff_dbg_at[i & 63];
        float ph = fmodf((float)a, step);
        if (ph < 2.0f || step - ph < 2.0f) on++;
        tot++;
    }
    check(tot > 5 && on == tot, "MUSIC BOX: every note on a 1/16 at 96 BPM (%d of %d)", on, tot);
    sim_shot("music_box");

    // ---------------------------------------------------------------- a crowd
    section("crowd");
    sound("THE ROOM");
    hr_crowd = 40;
    sim_run(1.0f);
    n0 = sniff_dbg_n;
    float c0 = sim_t;
    struct timespec ta, tb;
    clock_gettime(CLOCK_MONOTONIC, &ta);
    sim_run(4.0f);
    clock_gettime(CLOCK_MONOTONIC, &tb);
    double tick_us = ((double)(tb.tv_sec - ta.tv_sec) * 1e6 + (double)(tb.tv_nsec - ta.tv_nsec) / 1e3) / 400.0;
    float notes_s = (float)(sniff_dbg_n - n0) / 4.0f;
    check(S.rate > 1000.0f, "about forty busy devices: %.0f frames a second", (double)S.rate);
    check(notes_s <= pv[P_DENS] * 1.3f + 5.0f && sc.rain > 100.0f, "notes stay inside DENSITY (%.0f a second, budget %.0f); the rest rain (%.0f drops a second)", (double)notes_s, (double)pv[P_DENS], (double)sc.rain);
    check(m_peak(c0, sim_t) <= 0.95f, "under full scale (peak %.3f)", (double)m_peak(c0, sim_t));
    sim_shot("crowd");
    // the audio alone, with the voices busy
    float in[AUDIO_BLOCK] = { 0 }, out[AUDIO_BLOCK];
    clock_gettime(CLOCK_MONOTONIC, &ta);
    for (int i = 0; i < 4000; i++) front_audio(in, out, AUDIO_BLOCK);
    clock_gettime(CLOCK_MONOTONIC, &tb);
    double blk_us = ((double)(tb.tv_sec - ta.tv_sec) * 1e6 + (double)(tb.tv_nsec - ta.tv_nsec) / 1e3) / 4000.0;
    metric("host_us_per_block", blk_us);
    metric("host_us_per_tick_with_crowd", tick_us);
    check(blk_us < 20.0, "the audio takes %.1f us a block on this machine (under 20)", blk_us);
    hr_crowd = 0;
    sim_run(1.0f);

    // ---------------------------------------------------------------- tilt and the stick
    section("motion");
    param_set(P_ROOM, 0); param_set(P_VERB, 0); param_set(P_ECHO, 0); param_set(P_STORM, 0); param_set(P_TICKS, 0);
    float tail[2];
    for (int k = 0; k < 2; k++) {
        sim_tilt(0, k ? 35.0f : -35.0f);
        hr_on = true; sim_run(1.5f);
        hr_on = false; float ts = sim_t; sim_run(1.6f);
        tail[k] = m_rms(ts + 0.5f, ts + 1.5f);
    }
    hr_on = true;
    check(tail[1] > 2.0f * tail[0], "tilting the bottom edge down makes the notes ring longer (tail rms %.4f vs %.4f)", (double)tail[1], (double)tail[0]);
    sim_tilt(0, 0);
    param_set(P_DENS, 400);                                     // no budget in the way
    sim_run(1.0f);
    n0 = sniff_dbg_n; sim_run(3.0f); float free_rate = (float)(sniff_dbg_n - n0) / 3.0f;
    SI.jy = 1.0f; sim_run(0.5f);
    n0 = sniff_dbg_n; sim_run(3.0f); float near_only = (float)(sniff_dbg_n - n0) / 3.0f;
    SI.jy = 0;
    check(near_only < 0.6f * free_rate, "stick up: only what is near (%.1f -> %.1f notes a second)", (double)free_rate, (double)near_only);
    sound("THE ROOM");

    // ---------------------------------------------------------------- HOP and a fixed channel
    section("radio");
    param_set(P_CHAN, CH_HOP); param_set(P_DWELL, 0.2f);
    int seen = 0; bool chans[14] = { 0 };
    for (int i = 0; i < 400; i++) { sim_tick(); if (!chans[S.chan]) { chans[S.chan] = true; seen++; } }
    check(seen >= 12, "HOP walks the channels (%d of 13 in 4 s at 0.2 s each)", seen);
    sim_shot("hop");
    param_set(P_CHAN, 2 + 10);
    sim_run(0.5f);
    check(S.chan == 11 && hr_channel == 11, "CHANNEL 11 stays on 11");

    // ---------------------------------------------------------------- the sounds, and silence
    section("sounds");
    static const char *const show[] = { "RAIN", "BELL TOWER", "BIRDS", "GHOSTS", "GLITCH", "DRONE" };
    for (unsigned i = 0; i < sizeof show / sizeof show[0]; i++) {
        sound(show[i]);
        float s0 = sim_t;
        hr_walker = !strcmp(show[i], "GHOSTS") ? 1.0f : 0.0f;
        sim_run(3.0f);
        float r = m_rms(s0 + 1.0f, s0 + 3.0f);
        check(r > 0.004f && m_peak(s0, sim_t) < 0.95f, "%s plays (rms %.3f)", show[i], (double)r);
        char lb[32]; snprintf(lb, sizeof lb, "sound_%s", show[i]);
        for (char *c = lb; *c; c++) if (*c == ' ') *c = '_';
        sim_shot(lb);
        char wv[40]; snprintf(wv, sizeof wv, "-%s", lb + 6);
        for (char *c = wv; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        sim_wav(wv, s0, sim_t);
    }
    hr_walker = 0;
    sound("THE ROOM");
    hr_on = false;
    sim_run(12.0f);
    check(S.rate < 0.5f, "no Wi-Fi at all: nothing heard, the dog sleeps");
    sim_shot("asleep");
    hr_on = true;
    sim_run(3.0f);
}
