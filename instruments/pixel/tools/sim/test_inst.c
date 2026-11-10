// PIXEL's own checks (first ones: the rest follow with the editor and the library).
#include "sim.h"
#include "pixel.h"
#include "link.h"
#include <math.h>
#include <string.h>

// the console, the tempo, the swing, the frame rate, the key and the scale move the whole machine: a new chip or a
// new tempo is a new sound, not a control gliding
const char *const inst_click_skip[] = { "cons", "fram", "tmpo", "swng", "key ", "scal", "nlnk", "ngrp", "npow", NULL };

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; SI.joy = true; SI.imu = true; sim_tilt(0, 0); }

void inst_excite(void) { if (!px_pos.running && !px_pos.armed) px_play_song(0); }

static void plays(void)
{
    section("pixel: it plays");
    quiet(); px_stop(); sim_run(1.5f);
    check(!px_pos.running && m_rms(sim_t - 0.3f, sim_t) < 0.001f, "stopped, it waits in silence");
    sim_rec(0.1f); sim_run(4.0f);
    float r = m_rms(sim_t - 2.0f, sim_t), p = m_peak(sim_t - 2.0f, sim_t);
    check(px_pos.running && r > 0.02f && p < 0.99f, "REC tapped: the song plays (rms %.3f, peak %.3f)", (double)r, (double)p);
    sim_shot("play");
    sim_rec(0.1f); sim_run(1.5f);
    check(!px_pos.running && m_rms(sim_t - 0.5f, sim_t) < 0.001f, "REC tapped again: it stops and falls silent");
}

static void consoles(void)
{
    section("pixel: the knob's consoles");
    quiet(); px_play_song(0); sim_run(0.2f);
    float t0 = sim_t;
    for (int c = 0; c < CON_N; c++) {
        set("cons", (float)c); sim_run(3.2f);
        float r = m_rms(sim_t - 2.0f, sim_t);
        check(px_pos.console == c && r > 0.02f, "%s plays the song (rms %.3f)", PX_CONSOLES[c]->name, (double)r);
    }
    sim_wav("-consoles", t0, sim_t);
    px_stop(); sim_run(1.0f); set("cons", (float)CON_NES);
}

void inst_tests(void)
{
    plays();
    consoles();
    check(m_bad() == 0, "no NaN or inf anywhere");
}
