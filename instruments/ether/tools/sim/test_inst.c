// ETHER on the host: SNIFF's made-up flat (radio_host.c) and CHIRP's crowd of Bluetooth devices
// (sim_ble.c) at the same time. Both worlds are heard; BALANCE (and the stick) moves the music from one to
// the other; one budget holds the notes of both; HUNT follows a phone over Wi-Fi and a device over
// Bluetooth; when one radio fails the other carries on; the grid and the room and the storm still work;
// the busiest case stays inside the CPU budget.
#include "sim.h"
#include "inst.h"
#include "looper.h"
#include "sim_ble.h"
#include <string.h>
#include <time.h>

extern float hr_walker, hr_microwave, hr_target_rssi;
extern int hr_crowd, hr_channel;
extern bool hr_on, hr_fail;
extern uint32_t ether_dbg_at[64], ether_dbg_n, ether_dbg_wifi, ether_dbg_bt;
void sim_menu_close(void);

// Nothing left out: every param is ramped, or changes only what the next note will be.
const char *const inst_click_skip[] = { NULL };

static uint32_t wifi_who(int i) { return (0x9e3779b9u * (uint32_t)(i + 1)) ^ 0x5bd1e995u; }

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
    param_set(P_CHAN, 2 + 5);
    sim_tilt(0, 0); SI.jx = SI.jy = 0;
}

static float avg(volatile float *src, float seconds)
{
    double s = 0; int n = (int)(seconds * 100.0f);
    for (int i = 0; i < n; i++) { sim_tick(); s += *src; }
    return (float)(s / n);
}

typedef struct { uint32_t w, b; float t; } count_t;
static count_t mark(void) { return (count_t){ ether_dbg_wifi, ether_dbg_bt, sim_t }; }
static void rates(count_t m, float *w, float *b) { float d = sim_t - m.t; *w = (float)(ether_dbg_wifi - m.w) / d; *b = (float)(ether_dbg_bt - m.b) / d; }

static void bt_crowd(int extra)
{
    sim_crowd_default();
    for (int i = 0; i < extra; i++) sim_crowd_add(i % sim_payload_count(), -50.0f - (float)(i * 7 % 40), 0.08f + 0.02f * (float)(i % 7));
}

void inst_tests(void)
{
    hr_on = true; hr_walker = 0; hr_microwave = 0; hr_crowd = 0;
    midi_n = 0;
    bt_crowd(0);
    sound("BOTH WORLDS");
    sim_tilt(0, 0);

    // ---------------------------------------------------------------- both radios, both worlds
    section("both worlds");
    check(E.wifi_ok && E.bt_ok, "both radios started");
    param_set(P_CHAN, CH_AUTO);
    sim_run(5.5f);
    check(!E.scanning && E.chan == 6, "Wi-Fi settled on channel 6, the busiest (%d)", E.chan);
    count_t m = mark();
    float t0 = sim_t;
    sim_gif_begin("play");
    sim_run(4.0f);
    hr_walker = 1.0f;
    sim_run(4.0f);
    sim_gif_end();
    hr_walker = 0;
    float rw, rb; rates(m, &rw, &rb);
    check(E.ndev >= 5 && E.nbirds >= 6, "the ground has its Wi-Fi senders (%d), the sky its birds (%d)", E.ndev, E.nbirds);
    check(rw > 3.0f && rb > 1.5f, "both are heard: %.1f Wi-Fi notes and %.1f bird calls a second", (double)rw, (double)rb);
    float rms = m_rms(t0 + 1.0f, t0 + 4.0f), pk = m_peak(t0 + 1.0f, t0 + 4.0f);
    check(rms > 0.01f && rms < 0.25f && pk < 0.9f, "together at a sane level (rms %.3f, peak %.3f)", (double)rms, (double)pk);
    int ch1 = 0, ch2 = 0;
    for (int i = 0; i < midi_n; i++) if ((midi_log[i].st & 0xF0) == 0x90 && midi_log[i].d2 > 0) { if ((midi_log[i].st & 15) == 0) ch1++; else if ((midi_log[i].st & 15) == 1) ch2++; }
    check(ch1 > 10 && ch2 > 5, "MIDI: Wi-Fi notes on channel 1 (%d), birds on channel 2 (%d)", ch1, ch2);
    sim_shot("both_worlds");

    // ---------------------------------------------------------------- BALANCE
    section("balance");
    param_set(P_BAL, -100); sim_run(1.0f);
    m = mark(); float lw = avg(&snd_out_wifi, 3.0f), lb = avg(&snd_out_bt, 0.01f); rates(m, &rw, &rb);
    check(rb < 0.2f && rw > 3.0f && lb < 0.01f, "BALANCE -100: Wi-Fi only (%.1f Wi-Fi, %.1f bird notes a second)", (double)rw, (double)rb);
    (void)lw;
    sim_shot("balance_wifi");
    param_set(P_BAL, 100); sim_run(1.0f);
    m = mark(); sim_run(3.0f); rates(m, &rw, &rb);
    check(rw < 0.2f && rb > 1.5f, "BALANCE +100: Bluetooth only (%.1f Wi-Fi, %.1f bird notes a second)", (double)rw, (double)rb);
    sim_shot("balance_bt");
    param_set(P_BAL, 0); sim_run(0.5f);
    sim_gif_begin("balance");
    float rws[2], rbs[2];
    for (int k = 0; k < 2; k++) {
        SI.jx = k ? 1.0f : -1.0f; sim_run(0.8f);
        m = mark(); sim_run(2.5f); rates(m, &rws[k], &rbs[k]);
    }
    SI.jx = 0; sim_run(0.8f);
    sim_gif_end();
    check(rws[0] > 3.0f * rws[1] + 1.0f && rbs[1] > 3.0f * rbs[0] + 0.5f, "the stick does it too: left %.1f/%.1f, right %.1f/%.1f (Wi-Fi/birds a second)",
          (double)rws[0], (double)rbs[0], (double)rws[1], (double)rbs[1]);

    // ---------------------------------------------------------------- one budget
    section("budget");
    hr_crowd = 40; bt_crowd(28);
    param_set(P_FLOCK, FL_CHORUS);
    sim_run(2.0f);
    m = mark();
    uint32_t n0 = ether_dbg_n; (void)n0;
    float c0 = sim_t;
    sim_run(4.0f);
    rates(m, &rw, &rb);
    check(E.rate > 1000.0f && E.adverts > 150.0f, "a crowd in both worlds: %.0f Wi-Fi frames and %.0f adverts a second", (double)E.rate, (double)E.adverts);
    check(rw + rb <= pv[P_DENS] * 1.3f + 5.0f, "one budget holds both: %.1f + %.1f notes a second (DENSITY %.0f)", (double)rw, (double)rb, (double)pv[P_DENS]);
    check(rw > 0.25f * pv[P_DENS] && rb > 0.15f * pv[P_DENS], "and both worlds get their share of it");
    check(m_peak(c0, sim_t) <= 0.95f, "under full scale (peak %.3f)", (double)m_peak(c0, sim_t));
    sim_shot("crowd");
    // the busiest case for the CPU: both crowds, SWARM, the storm, someone walking
    param_set(P_FLOCK, FL_SWARM); hr_microwave = 1.0f; hr_walker = 1.0f; param_set(P_DENS, 400);
    sim_run(2.0f);
    struct timespec ta, tb;
    clock_gettime(CLOCK_MONOTONIC, &ta);
    sim_run(3.0f);
    clock_gettime(CLOCK_MONOTONIC, &tb);
    double tick_us = ((double)(tb.tv_sec - ta.tv_sec) * 1e6 + (double)(tb.tv_nsec - ta.tv_nsec) / 1e3) / 300.0;
    sim_shot("busiest");
    float in[AUDIO_BLOCK] = { 0 }, out[AUDIO_BLOCK];
    double best = 1e9;
    for (int rep = 0; rep < 3; rep++) {
        for (int k = 0; k < 20; k++) sim_tick();                   // keep the voices busy between the timed runs
        clock_gettime(CLOCK_MONOTONIC, &ta);
        for (int i = 0; i < 1500; i++) front_audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &tb);
        double us = ((double)(tb.tv_sec - ta.tv_sec) * 1e6 + (double)(tb.tv_nsec - ta.tv_nsec) / 1e3) / 1500.0;
        if (us < best) best = us;
    }
    metric("host_us_per_block_busiest", best);
    metric("host_us_per_tick_busiest", tick_us);
    check(best < 20.0, "the busiest case: %.1f us a block on this machine (under 20)", best);
    hr_crowd = 0; hr_microwave = 0; hr_walker = 0; bt_crowd(0);
    sound("BOTH WORLDS");
    sim_run(8.0f);

    // ---------------------------------------------------------------- HUNT on either radio
    section("hunt");
    hr_target_rssi = -88.0f;
    sound("HUNT");
    sim_run(1.0f);
    check(E.mode == MODE_HUNT && E.target >= 0, "HUNT picks the loudest device of either radio (%s)", E.tw == W_WIFI ? "Wi-Fi" : "Bluetooth");
    int flicks = 0;
    while (flicks < 40 && !(E.tw == W_WIFI && E.target >= 0 && E.dev[E.target].who == wifi_who(4))) { SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.05f); flicks++; }
    check(E.tw == W_WIFI && E.target >= 0 && E.dev[E.target].who == wifi_who(4), "flicks of the stick reach the Wi-Fi phone (%d flicks)", flicks);
    sim_run(2.0f);
    uint32_t b0 = snd_beeps; sim_run(2.0f); float far_rate = (float)(snd_beeps - b0) / 2.0f;
    sim_gif_begin("hunt");
    for (int i = 0; i <= 80; i++) { hr_target_rssi = -88.0f + 48.0f * (float)i / 80.0f; sim_run(0.08f); }
    sim_shot("hunt_wifi");
    b0 = snd_beeps; sim_run(2.0f); float near_rate = (float)(snd_beeps - b0) / 2.0f;
    check(near_rate > 3.0f * far_rate && E.chan == 6, "Wi-Fi: walking up to it, %.1f -> %.1f beeps a second; it stays on its channel", (double)far_rate, (double)near_rate);
    // now a Bluetooth device: the Garmin watch of the default crowd (index 3), far
    sim_dev[3].rssi = -90.0f;
    sim_run(2.0f);
    flicks = 0;
    while (flicks < 40 && !(E.tw == W_BT && E.target >= 0 && E.birds[E.target].who == sim_dev[3].who)) { SI.jx = 1.0f; sim_run(0.05f); SI.jx = 0; sim_run(0.05f); flicks++; }
    check(E.tw == W_BT && E.target >= 0 && E.birds[E.target].who == sim_dev[3].who, "and on to a Bluetooth watch (%d flicks)", flicks);
    sim_run(3.0f);
    b0 = snd_beeps; sim_run(3.0f); far_rate = (float)(snd_beeps - b0) / 3.0f;
    for (int i = 0; i <= 80; i++) { sim_dev[3].rssi = -90.0f + 45.0f * (float)i / 80.0f; sim_run(0.08f); }
    sim_shot("hunt_bt");
    b0 = snd_beeps; sim_run(3.0f); near_rate = (float)(snd_beeps - b0) / 3.0f;
    sim_gif_end();
    check(near_rate > 3.0f * far_rate, "Bluetooth: walking up to it, %.1f -> %.1f beeps a second", (double)far_rate, (double)near_rate);
    sim_dev[3].rssi = -66.0f; hr_target_rssi = -80.0f;
    sound("BOTH WORLDS");
    sim_run(2.0f);

    // ---------------------------------------------------------------- one radio down
    section("one radio down");
    radio_stop(); hr_fail = true; radios_start(); hr_fail = false;
    sim_run(2.0f);
    m = mark(); sim_run(4.0f); rates(m, &rw, &rb);
    check(!E.wifi_ok && E.bt_ok && rw < 0.1f && rb > 1.5f, "Wi-Fi would not start: the birds sing on (%.1f calls a second)", (double)rb);
    sim_shot("no_wifi");
    radios_start();
    sim_run(6.0f);
    check(E.wifi_ok, "Wi-Fi back");
    sim_ble_fail = true; radios_start(); sim_ble_fail = false;
    sim_run(13.0f);
    m = mark(); sim_run(4.0f); rates(m, &rw, &rb);
    check(E.wifi_ok && !E.bt_ok && rb < 0.1f && rw > 3.0f && E.nbirds == 0, "Bluetooth would not start: the ground plays on (%.1f notes a second), the sky empties", (double)rw);
    sim_shot("no_bluetooth");
    radios_start();
    sim_run(4.0f);
    check(E.bt_ok && E.nbirds > 3, "Bluetooth back (%d birds)", E.nbirds);

    // ---------------------------------------------------------------- the grid, for both
    section("rhythm");
    sound("MUSIC BOX");
    sim_run(3.0f);
    uint32_t na = ether_dbg_n, w0 = ether_dbg_wifi, bb0 = ether_dbg_bt;
    sim_run(3.0f);
    int on = 0, tot = 0;
    float step = 60.0f / 96.0f * 0.25f * (float)AUDIO_SR;
    for (uint32_t i = na; i < ether_dbg_n && tot < 64; i++) {
        float ph = fmodf((float)ether_dbg_at[i & 63], step);
        if (ph < 2.0f || step - ph < 2.0f) on++;
        tot++;
    }
    check(tot > 5 && on == tot && ether_dbg_wifi > w0 && ether_dbg_bt > bb0, "MUSIC BOX: Wi-Fi notes and bird pulses all on the 1/16 grid (%d of %d)", on, tot);
    sim_shot("music_box");

    // ---------------------------------------------------------------- the room and the storm still work
    section("room and storm");
    sound("BOTH WORLDS");
    sim_run(5.0f);
    float still = avg(&snd_out_room, 2.0f);
    hr_walker = 1.0f; sim_run(3.0f);
    float walk = avg(&snd_out_room, 2.0f);
    check(E.motion > 0.5f && walk > 2.0f * still, "someone walking: the room moves (%.2f) and its chord swells (%.4f -> %.4f)", (double)E.motion, (double)still, (double)walk);
    sim_shot("someone_walking");
    hr_walker = 0;
    sim_run(3.0f);
    uint32_t th0 = sc.thunder;
    sim_gif_begin("storm");
    hr_microwave = 1.0f; sim_run(2.5f);
    sim_shot("storm");
    sim_gif_end();
    check(E.storm > 0.4f && sc.thunder != th0, "a microwave oven: storm %.2f and a clap of thunder", (double)E.storm);
    hr_microwave = 0; sim_run(8.0f);

    // ---------------------------------------------------------------- the sounds
    section("sounds");
    static const char *const show[] = { "DAWN", "RAIN & BIRDS", "CLOCK SHOP", "GAMELAN", "STARLINGS", "NIGHT", "ONLY WI-FI", "ONLY BIRDS", "GLITCH" };
    for (unsigned i = 0; i < sizeof show / sizeof show[0]; i++) {
        sound(show[i]);
        float s0 = sim_t;
        sim_run(4.0f);
        float r = m_rms(s0 + 1.0f, s0 + 4.0f);
        check(r > 0.004f && m_peak(s0, sim_t) < 0.95f, "%s plays (rms %.3f)", show[i], (double)r);
        char lb[40]; snprintf(lb, sizeof lb, "sound_%s", show[i]);
        for (char *c = lb; *c; c++) if (*c == ' ' || *c == '&') *c = '_';
        sim_shot(lb);
        char wv[48]; snprintf(wv, sizeof wv, "-%s", lb + 6);
        for (char *c = wv; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        sim_wav(wv, s0, sim_t);
    }
    sound("BOTH WORLDS");
    hr_on = false; sim_crowd_clear();
    sim_run(14.0f);
    check(E.rate < 0.5f && E.nbirds == 0, "nothing on the air: no notes, the dog sleeps");
    sim_shot("asleep");
    hr_on = true; bt_crowd(0);
    sim_run(3.0f);
}
