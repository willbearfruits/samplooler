// SING's own checks: the voices (every sound), the loop (alone, with the tilt, the stutter, free), the song's
// notes for the tuning, a MIDI chord, the link with a pretend BAND (its tempo, key and loops, over drifting
// clocks), and the cost. The engines' own measurements are in tools/dsp (run_voice.sh, run_fx.sh).
#include "sim.h"
#include "sing.h"
#include "link.h"
#include "oled.h"
#include <math.h>
#include <string.h>
#include <time.h>

const char *const inst_click_skip[] = { NULL };
bool sim_menu_param(int p);
void sim_menu_close(void);

static int tag(const char *t) { int p = param_by_tag(t); if (p < 0) note("no param '%s'", t); return p; }
static void set(const char *t, float v) { int p = tag(t); if (p >= 0) param_set(p, v); }
static float get(const char *t) { int p = tag(t); return p >= 0 ? pv[p] : 0.0f; }
static void run_action(const char *t) { int p = tag(t); if (p >= 0 && sim_menu_param(p)) { sim_tap(); sim_run(0.1f); sim_menu_close(); } }

// a singer: a glottal pulse train (a falling saw, softened) through two vowel resonances, with a vibrato; f0 and the
// loudness come from the struct, so a test can glide or stop
typedef struct { float f0, amp, vib; float ph, z1[2], z2[2]; } singer_t;
static singer_t sg = { 220.0f, 0.12f, 0.0f, 0, { 0, 0 }, { 0, 0 } };
static float singer(float t, void *u)
{
    singer_t *s = u;
    float f = s->f0 * (1.0f + s->vib * 0.01f * sinf(6.2831853f * 5.5f * t));
    s->ph += f / AUDIO_SR; if (s->ph >= 1.0f) s->ph -= 1.0f;
    float x = (1.0f - 2.0f * s->ph) * (s->ph < 0.6f ? 1.0f : 0.4f) + 0.01f * sim_noise();
    static const float fc[2] = { 730.0f, 1090.0f }, bw[2] = { 90.0f, 110.0f };
    float y = 0;
    for (int k = 0; k < 2; k++) {
        float r = expf(-3.14159f * bw[k] / AUDIO_SR), c = 2.0f * r * cosf(6.2831853f * fc[k] / AUDIO_SR);
        float o = (1.0f - r) * x + c * s->z1[k] - r * r * s->z2[k];
        s->z2[k] = s->z1[k]; s->z1[k] = o; y += o;
    }
    return s->amp * y * 3.0f;
}
static void sing_on(float f0) { sg.f0 = f0; sg.amp = 0.12f; SI.mic = singer; SI.mic_u = &sg; }
// the singer, and REC's switch clicking at the given times (a sharp 2 ms crack, much louder than the voice)
static float *clk_times;
static float clicky(float t, void *u)
{
    float x = singer(t, u);
    for (int k = 0; k < 2 && clk_times; k++) {
        float d = t - clk_times[k];
        if (clk_times[k] > 0 && d >= 0 && d < 0.002f) x += (d < 0.0005f ? 0.8f : -0.5f) * (1.0f - d / 0.002f);
    }
    return x;
}
static void quiet(void) { SI.mic = NULL; SI.mic_u = NULL; SI.jx = SI.jy = 0; sim_tilt(0, 0); }
void inst_excite(void) { sing_on(220.0f); }

static float corr(int a, int b, int n)
{
    double c = 0, e0 = 0, e1 = 0;
    for (int i = 0; i < n; i++) { float x0 = sim_out[a + i], x1 = sim_out[b + i]; c += x0 * x1; e0 += x0 * x0; e1 += x1 * x1; }
    return e0 * e1 > 1e-12 ? (float)(c / sqrt(e0 * e1)) : 0.0f;
}
static void load_sound(int k) { for (int i = 0; i < 40 && strcmp(front_status()->sound, INST.presets[k].name); i++) sim_turn(1); }
static void first_sound(void) { for (int i = 0; i < 40 && strcmp(front_status()->sound, INST.presets[0].name); i++) sim_turn(-1); }

// ---------------------------------------------------------------- the voices
static void voices(void)
{
    section("sing: every voice");
    first_sound();
    sim_gif_begin("voices");
    int ok = 0, n = INST.npresets;
    for (int k = 0; k < n; k++) {
        load_sound(k);
        sing_on(k % 2 ? 196.0f : 262.0f);
        sim_run(1.2f);
        float r = m_rms(sim_t - 0.8f, sim_t), p = m_peak(sim_t - 0.8f, sim_t);
        bool good = r > 0.01f && p < 0.99f;
        if (good) ok++; else note("%s: rms %.4f peak %.3f", INST.presets[k].name, (double)r, (double)p);
        if (k < 6 || k == 8) { char b[32]; snprintf(b, sizeof b, "voice_%d", k); sim_shot(b); }
    }
    sim_gif_end();
    check(ok == n, "every voice sounds when sung into, under full scale (%d of %d)", ok, n);
    quiet(); first_sound(); sim_run(0.5f);
}

// ---------------------------------------------------------------- the notes the tuning uses
static void tuning_notes(void)
{
    section("sing: the notes it tunes to");
    set("skey", 0); set("sscl", 0); set("stto", 0); sim_run(0.05f);
    check(sui.tune_mask == 0x0AB5, "C MAJOR, TUNE TO SCALE: C D E F G A B (mask %03X)", sui.tune_mask);
    set("stto", 2); sim_run(0.05f);
    check(sui.tune_mask == 0, "ALL NOTES: every semitone");
    set("stto", 3); sim_run(0.05f);
    check(sui.tune_mask == 0x0295, "PENTATONIC in C major: C D E G A (mask %03X)", sui.tune_mask);
    set("sscl", 1); sim_run(0.05f);
    check(sui.tune_mask == 0x04A9, "PENTATONIC in C minor: C Eb F G Bb (mask %03X)", sui.tune_mask);
    set("sscl", 0); set("sprg", 0); set("schd", 4); set("stto", 1); sim_run(0.05f);
    check(sui.tune_mask == ((1 << 7) | (1 << 11) | (1 << 2)), "TUNE TO CHORD on V in C: G B D (mask %03X)", sui.tune_mask);
    // a MIDI keyboard holding notes is the chord
    if (INST.midi_in) { INST.midi_in(0x90, 57, 100); INST.midi_in(0x90, 60, 100); INST.midi_in(0x90, 64, 100); }
    sim_run(0.05f);
    check(sui.midi_chord && song.chord_n == 3 && song.chord[0] == 57, "MIDI notes A C E held: the chord is theirs (%d notes from %d)", song.chord_n, song.chord[0]);
    if (INST.midi_in) { INST.midi_in(0x80, 57, 0); INST.midi_in(0x80, 60, 0); INST.midi_in(0x80, 64, 0); }
    sim_run(0.05f);
    check(!sui.midi_chord, "let go: the song's chord again");
    set("stto", 0); set("sprg", 1); set("schd", 0); sim_run(0.05f);
}

// ---------------------------------------------------------------- the loop
static void loop_alone(void)
{
    section("sing: the loop, alone");
    first_sound(); quiet();
    set("qsyn", 0); set("stmp", 100);
    run_action("qclr"); sim_run(0.3f);
    // nothing in time and no other board: a 3.1 s take sets the tempo (whole bars), and plays back pass after pass
    sing_on(220.0f);
    float t0 = sim_t;
    SI.rec = true; sim_run(3.1f); SI.rec = false; sim_tick();
    sim_run(0.5f);
    int beats = loop_beats();
    float bpm = song.bpm;
    check(loop_state() == LOOPER_PLAY && beats % 4 == 0 && bpm >= 70.0f && bpm <= 160.0f,
          "a 3.1 s take alone: a loop of %d beats (whole bars), and the tempo is now %.1f BPM", beats, (double)bpm);
    check((int)get("stmp") == (int)(bpm + 0.5f), "TEMPO shows it (%d)", (int)get("stmp"));
    float len = loop_len_s();
    // singing from before the press: the take starts once the press's click has passed (0.12 s) and ends before the
    // release's (0.04 s): 2.94 s, as whole bars at the tempo it sets
    check(fabsf(len - 2.94f) < 0.05f, "the loop is the take without REC's clicks (%.3f s of 3.1 held)", (double)len);
    quiet();
    sim_run(len);
    float a0 = sim_t;
    sim_run(2.0f * len + 0.1f);
    int L = (int)(len * AUDIO_SR), s0 = (int)(a0 * AUDIO_SR);
    float c = corr(s0, s0 + L, L - 64), played = m_rms(a0, a0 + len), sung = m_rms(t0 + 0.5f, t0 + 2.8f);
    check(played > 0.3f * sung * 0.8f && c > 0.95f, "it plays the take back the same every pass (rms %.3f of %.3f sung, pass to pass %.3f)", (double)played, (double)sung, (double)c);
    sim_shot("loop_playing");
    // a tap stops it, another plays it
    sim_rec(0.1f); sim_run(3.0f);                                 // its echo and reverb ring out
    float stopped = m_rms(sim_t - 0.3f, sim_t);
    sim_rec(0.1f); sim_run(0.6f);
    float again = m_rms(sim_t - 0.3f, sim_t);
    check(stopped < 0.05f * again && again > 0.01f, "a tap on REC stops it (rms %.4f), another plays it (%.3f)", (double)stopped, (double)again);
    // adding: a second take keeps the length
    sing_on(330.0f);
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick();
    quiet(); sim_run(0.2f);
    check(fabsf(loop_len_s() - len) < 0.001f && loop_layers() >= 2, "adding keeps the length (%.3f s), %d takes", (double)loop_len_s(), loop_layers());
    // the tilt: nose down faster, back slower then backwards; flat again, back on the beat
    sim_gif_begin("tilt");
    sim_tilt(0, 40); sim_run(0.8f);
    float fast = loop_speed();
    sim_tilt(0, -40); sim_run(0.8f);
    float back = loop_speed();
    sim_tilt(0, 0); sim_run(1.5f);
    sim_gif_end();
    check(fast > 1.5f && back < -0.4f, "the tilt plays it faster (%.2fx) and backwards (%.2fx)", (double)fast, (double)back);
    // on the beat: where the loop is against the frames since the take began
    float ph1 = loop_pos();
    uint32_t fpb = song.fpb;
    sim_run(2.0f * len);
    float ph2 = loop_pos();
    float d = (ph2 - ph1) * len * 1000.0f;
    while (d > len * 500.0f) d -= len * 1000.0f;
    while (d < -len * 500.0f) d += len * 1000.0f;
    check(fabsf(d) < 1.0f && fabsf(loop_speed() - 1.0f) < 0.01f, "flat again it is back in time (%.2f ms off after two passes)", (double)d);
    (void)fpb;
    // the stutter: rolled right, a slice repeats (a beat long at first, shorter further)
    sing_on(220.0f);
    sim_tilt(30, 0); sim_run(0.3f + 3.0f * (float)song.fpb / AUDIO_SR);
    int lag = (int)(song.fpb / 2u);
    int b0 = (int)(sim_t * AUDIO_SR) - 2 * lag - 64;
    float cs = corr(b0, b0 + lag, lag);
    sim_tilt(0, 0); quiet(); sim_run(0.5f);
    check(cs > 0.9f, "rolled right: the stutter repeats a half-beat slice (pass to pass %.3f)", (double)cs);
    sim_tilt(-35, 0); sim_run(0.2f);
    float thr = sui.throw_;
    sim_tilt(0, 0); sim_run(0.2f);
    check(thr > 0.3f && sui.throw_ < 0.01f, "rolled left: the echo throw opens (%.2f), and closes flat", (double)thr);
    // KEEP PITCH (the default): the tilt changes the speed, not the pitch; TAPE moves the pitch with it
    {
        quiet(); run_action("qclr"); sim_run(0.3f);
        set("qspm", 0);
        sing_on(220.0f); sg.vib = 0;
        SI.rec = true; sim_run(2.6f); SI.rec = false; sim_tick();
        while (loop_state() == LOOPER_REC) sim_tick();
        quiet(); sim_run(0.5f);
        float f_flat = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        float w_keep = sim_t;
        sim_tilt(0, 40); sim_run(1.0f);
        float sp_k = loop_speed(), f_fast = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        sim_tilt(0, -25); sim_run(1.0f);
        float sp_s = loop_speed(), f_slow = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        sim_tilt(0, -40); sim_run(1.0f);
        float sp_b = loop_speed(), f_back = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        sim_tilt(0, 0); sim_run(1.5f);
        sim_wav("-tilt-keep-pitch", w_keep, sim_t);
        check(sp_k > 1.5f && sp_s < 0.9f && sp_b < -0.4f, "KEEP PITCH: the tilt still moves the speed (%.2fx, %.2fx, %.2fx)", (double)sp_k, (double)sp_s, (double)sp_b);
        float c1 = 1200.0f * log2f(f_fast / f_flat), c2 = 1200.0f * log2f(f_slow / f_flat), c3 = 1200.0f * log2f(f_back / f_flat);
        check(fabsf(c1) < 30.0f && fabsf(c2) < 30.0f && fabsf(c3) < 30.0f,
              "KEEP PITCH: the pitch stays (%.0f Hz flat; faster %+.0f c, slower %+.0f c, backwards %+.0f c)", (double)f_flat, (double)c1, (double)c2, (double)c3);
        float a = m_rms(sim_t - 0.5f, sim_t);
        check(a > 0.01f && fabsf(loop_speed() - 1.0f) < 0.01f, "flat again: in time, still playing (rms %.3f)", (double)a);
        set("qspm", 2); sim_run(0.1f);
        float w_tape = sim_t;
        sim_tilt(0, 40); sim_run(1.0f);
        float sp_t = loop_speed(), f_tape = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        sim_tilt(0, 0); sim_run(1.5f);
        sim_wav("-tilt-tape", w_tape, sim_t);
        float ct = 1200.0f * log2f(f_tape / f_flat), want = 1200.0f * log2f(sp_t);
        check(fabsf(ct - want) < 60.0f, "TAPE: the pitch goes with the speed (%.2fx: %+.0f c, %+.0f c heard)", (double)sp_t, (double)want, (double)ct);
        set("qspm", 1); sim_run(0.1f);
        float w_oct = sim_t;
        sim_tilt(0, 40); sim_run(1.0f);
        float o_up = loop_speed();
        sim_tilt(0, -20); sim_run(1.0f);
        float o_half = loop_speed();
        sim_tilt(0, -40); sim_run(1.0f);
        float o_back = loop_speed();
        sim_tilt(0, 0); sim_run(1.5f);
        sim_wav("-tilt-octaves", w_oct, sim_t);
        check(fabsf(o_up - 2.0f) < 0.01f && fabsf(o_half - 0.5f) < 0.01f && fabsf(o_back + 1.0f) < 0.01f,
              "OCTAVES: only 2x, 1/2x and backwards (%.2f, %.2f, %.2f)", (double)o_up, (double)o_half, (double)o_back);
        set("qspm", 0); sim_run(0.1f);
        // stopped, then held: a new take in place of the old one
        sim_rec(0.1f); sim_run(0.5f);
        int st0 = loop_state();
        sing_on(330.0f);
        SI.rec = true; sim_run(2.6f); SI.rec = false; sim_tick();
        while (loop_state() == LOOPER_REC) sim_tick();
        quiet(); sim_run(0.5f);
        float f_new = m_pitch_at(sim_t - 0.4f, sim_t, 80.0f, 1000.0f);
        check(st0 == LOOPER_STOP && loop_state() == LOOPER_PLAY && loop_layers() == 1 && fabsf(1200.0f * log2f(f_new / 330.0f)) < 50.0f,
              "stopped, then REC held: a new take replaces the old (%d take, %.0f Hz)", loop_layers(), (double)f_new);
    }
    run_action("qclr"); sim_run(0.3f);
    check(loop_state() == LOOPER_EMPTY, "CLEAR LOOP empties it");
    // FREE: any length, the tempo untouched
    set("qsyn", 1); set("stmp", 120); sim_run(0.1f);
    sing_on(262.0f);
    SI.rec = true; sim_run(1.37f); SI.rec = false; sim_tick();
    quiet(); sim_run(0.3f);
    check(fabsf(loop_len_s() - 1.204f) < 0.03f && (int)get("stmp") == 120, "LOOP FREE: as long as held less the clicks (%.3f s), the tempo left alone", (double)loop_len_s());
    run_action("qclr"); sim_run(0.3f);
    // the silence before the first sound is cut: REC held, 0.8 s of nothing, then 1.5 s of singing
    quiet(); sim_run(0.5f);
    SI.rec = true; sim_run(0.8f);
    int armed = loop_state();
    sim_shot("loop_armed");
    sing_on(262.0f); sim_run(1.5f); SI.rec = false; sim_tick();
    quiet(); sim_run(0.3f);
    check(armed == LOOPER_ARM && fabsf(loop_len_s() - 1.47f) < 0.05f, "the silence before the first sound is cut: a %.3f s loop from 1.5 s of singing after 0.8 s of nothing", (double)loop_len_s());
    float lstart = sim_t;
    sim_run(2.0f * loop_len_s());
    float lr = m_rms(lstart, sim_t);
    check(lr > 0.02f, "and it plays (rms %.3f)", (double)lr);
    run_action("qclr"); sim_run(0.3f);
    // nothing sung: no loop
    SI.rec = true; sim_run(1.0f); SI.rec = false; sim_tick(); sim_run(0.3f);
    check(loop_state() == LOOPER_EMPTY, "REC held over silence and let go: no loop");
    // REC's clicks: a sharp click into the mic at the press and at the release stays out of the loop
    {
        static float t_clicks[2];
        t_clicks[0] = sim_t + 0.010f; t_clicks[1] = -1;
        sing_on(220.0f); sg.amp = 0.06f;
        struct { singer_t *s; float *tc; } u = { &sg, t_clicks };
        (void)u;
        clk_times = t_clicks;
        SI.mic = clicky; SI.mic_u = &sg;
        SI.rec = true; sim_run(1.6f);
        t_clicks[1] = sim_t - 0.015f;                            // the release's click, a little before the release is seen
        sim_run(0.02f); SI.rec = false; sim_tick();
        sim_run(0.3f);
        quiet(); sim_run(0.5f);
        float L2 = loop_len_s(), a0 = sim_t;
        sim_run(2.0f * L2);
        float worst = 0, typ = 0;
        int i0 = (int)(a0 * AUDIO_SR), i1 = (int)(sim_t * AUDIO_SR);
        if (i1 > sim_n) i1 = sim_n;                               // (sim_t is a float: six minutes in it rounds a sample past what was written)
        for (int i = i0 + 1; i < i1; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > worst) worst = d; typ += d; }
        typ /= (float)(i1 - i0);
        check(worst < 12.0f * typ + 0.02f, "a click at the press and at the release stays out of the loop (worst step %.3f, typical %.4f)", (double)worst, (double)typ);
        clk_times = NULL;
    }
    run_action("qclr"); set("qsyn", 0); sim_run(0.3f);
}

// ---------------------------------------------------------------- the link with a pretend BAND
static void with_band(void)
{
    section("sing: linked to a BAND");
    first_sound(); quiet();
    set("stmp", 100); sim_run(0.1f);
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);                // its crystal 25 ppm fast; it is the clock
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(9, 1, 6, 0, 0, -1); }   // 120 BPM, A minor
    sim_peer_use(-1);
    sim_run(2.0f);
    check(!link_is_clock() && link_synced(), "SING takes the BAND as the clock and syncs to it");
    check(song.fpb == 16000 && (int)get("stmp") == 120, "the BAND's 120 BPM (TEMPO %d)", (int)get("stmp"));
    check((int)get("skey") == 9 && (int)get("sscl") == 1 && (int)get("sprg") == 6, "its A minor and its chords (KEY %d SCALE %d CHORDS %d)",
          (int)get("skey"), (int)get("sscl"), (int)get("sprg"));
    sim_run(3.0f);
    float g = sim_peer_gap_ms(k);
    check(fabsf(g) < 1.0f, "the beats line up: %.3f ms apart", (double)g);
    // a first loop with the BAND playing: whole bars at its tempo, the tempo kept
    sing_on(220.0f);
    SI.rec = true; sim_run(4.4f); SI.rec = false; sim_tick();   // 4.4 s at 120 BPM: two bars and a little
    float wait0 = sim_t;
    while (loop_state() == LOOPER_REC && sim_t - wait0 < 3.0f) sim_tick();
    quiet(); sim_run(0.3f);
    check(loop_beats() == 8 && song.fpb == 16000, "a 4.4 s take is two bars at the BAND's tempo (%d beats), the tempo kept", loop_beats());
    sim_peer_use(k); int sb = link_time()->beats; bool fl = link_flags_any() & LINKF_LOOP; sim_peer_use(-1);
    check(sb == 8 && fl, "the BAND hears of it: the session's loop is %d beats", sb);
    // over a minute of drifting clocks the loop stays where it was against the BAND's beat
    float e1, e2;
    {
        float lb = (float)loop_beats();
        float pb = (float)fmod(sim_peer_beats(k), (double)lb);
        e1 = loop_pos() * lb - pb;
    }
    sim_run(60.0f);
    {
        float lb = (float)loop_beats();
        float pb = (float)fmod(sim_peer_beats(k), (double)lb);
        e2 = loop_pos() * lb - pb;
    }
    float de = e2 - e1;
    while (de > 4.0f) de -= 8.0f;
    while (de < -4.0f) de += 8.0f;
    float ms = de * 500.0f;                                       // a beat is 500 ms at 120 BPM
    check(fabsf(ms) < 1.0f, "after a minute (the BAND's crystal 25 ppm fast: 1.5 ms of drift) the loop moved %.3f ms against its beat", (double)ms);
    // the tempo cannot move while there is a loop
    set("stmp", 90); sim_run(0.3f);
    check((int)get("stmp") == 120, "TEMPO stays while there is a loop (%d)", (int)get("stmp"));
    // a key set here goes to the BAND
    set("skey", 2); sim_run(0.4f);
    sim_peer_use(k); int pk = link_harm()->key; sim_peer_use(-1);
    check(pk == 2, "KEY D on SING reaches the BAND (%d)", pk);
    // a chord the BAND plays live is the chord here
    sim_peer_use(k); link_set_harm(2, 1, 6, 0, 0, 3); sim_peer_use(-1);
    sim_run(0.3f);
    check(song.degree == 3, "the BAND's bass leans to iv: SING's chord follows (degree %d)", song.degree);
    sim_peer_use(k); link_set_harm(2, 1, 6, 0, 0, -1); sim_peer_use(-1);
    run_action("qclr"); sim_run(0.5f);
    sim_peer_use(k); bool fl2 = link_flags_any() & LINKF_LOOP; sim_peer_use(-1);
    check(!fl2, "CLEAR LOOP: the BAND hears the loop is gone");
    // the BAND switched off with a loop playing here: SING becomes the clock and the loop does not move
    sing_on(220.0f);
    SI.rec = true; sim_run(2.2f); SI.rec = false; sim_tick();
    while (loop_state() == LOOPER_REC) sim_tick();
    quiet(); sim_run(1.0f);
    float lb = (float)loop_beats(), q1 = loop_pos() * lb, b1 = (float)fmod(sim_peer_beats(-1), (double)lb);
    sim_peer_power(k, false);
    sim_run(2.0f);
    float q2 = loop_pos() * lb, b2 = (float)fmod(sim_peer_beats(-1), (double)lb);
    float moved = (q2 - b2) - (q1 - b1);
    while (moved > lb * 0.5f) moved -= lb;
    while (moved < -lb * 0.5f) moved += lb;
    check(link_is_clock(), "the BAND switched off: SING keeps its time alone");
    check(fabsf(moved * 500.0f) < 1.0f, "and its loop stays on its beat (%.3f ms)", (double)(moved * 500.0f));
    run_action("qclr"); sim_run(0.3f);
    set("skey", 0); set("sscl", 0); set("sprg", 1); set("stmp", 100); sim_run(0.2f);
}

// ---------------------------------------------------------------- the cost
static void cost(void)
{
    section("sing: cost");
    // the busiest: a choir with harmonies and the vocoder, every effect, the loop playing, the stutter
    first_sound();
    set("vtun", 100); set("vspd", 0); set("vlvl", 100); set("hmod", 2); set("hiv1", 1); set("hiv2", 3); set("hiv3", 10);
    set("cmix", 100); set("ctyp", 2); set("olvl", 100); set("ocar", 2); set("oban", 20);
    set("fech", 60); set("frev", 60); set("fshm", 60); set("igat", 50); set("icmp", 60); set("ides", 50);
    sing_on(220.0f); sg.vib = 1.0f;
    SI.rec = true; sim_run(2.6f); SI.rec = false; sim_tick();
    sim_run(1.0f);
    float in[AUDIO_BLOCK], out[AUDIO_BLOCK];
    struct timespec a, b;
    double tsum = 0; int nblk = 3000;
    float tt = sim_t;
    { float junk[5]; sing_prof(junk, 5); }
    for (int k = 0; k < nblk; k++) {
        for (int i = 0; i < AUDIO_BLOCK; i++) in[i] = SI.mic ? SI.mic(tt + (float)i / (float)AUDIO_SR, SI.mic_u) : 0.0f;
        tt += (float)AUDIO_BLOCK / (float)AUDIO_SR;
        clock_gettime(CLOCK_MONOTONIC, &a);
        front_audio(in, out, AUDIO_BLOCK);
        clock_gettime(CLOCK_MONOTONIC, &b);
        tsum += (double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) * 1e-3;
    }
    double us = tsum / nblk;
    float parts[5]; sing_prof(parts, 5);
    note("the parts, us per block (host): input %.1f  voices %.1f  vocoder %.1f  loop+mix %.1f  fx %.1f",
         (double)(parts[0] * 0.96f), (double)(parts[1] * 0.96f), (double)(parts[2] * 0.96f), (double)(parts[3] * 0.96f), (double)(parts[4] * 0.96f));
    metric("sing_us_per_block", us);
    check(us < 20.0, "everything at once takes %.1f us per 64-frame block (budget 20: about 70 %% of the board)", us);
    sim_gif_begin("sing");
    sim_run(3.0f);
    sim_gif_end();
    sim_shot("busiest");
    sg.vib = 0; quiet();
    run_action("qclr"); first_sound(); sim_run(0.5f);
}

// ---------------------------------------------------------------- rev E: the left knob (the choir), the hand (the echo)
void front_midi(uint8_t st, uint8_t d1, uint8_t d2);
static float pitch_now(float lo, float hi) { return m_pitch_at(sim_t - 0.4f, sim_t, lo, hi); }
static float cents(float f, float ref) { return f > 0 && ref > 0 ? 1200.0f * log2f(f / ref) : 9999.0f; }
static float dbr(float a, float b) { return 20.0f * log10f((a + 1e-9f) / (b + 1e-9f)); }
// where the sound's weight sits: the mean frequency of the note's first eight harmonics, by amplitude
static float centre(float f0)
{
    float num = 0, den = 0;
    for (int k = 1; k <= 8; k++) { float a = sqrtf(m_goertzel(sim_t - 0.4f, sim_t, f0 * (float)k)); num += a * f0 * (float)k; den += a; }
    return den > 0 ? num / den : 0;
}
// a short sung note with the hand at that height, then silence with the hand gone: how loud the 1.2 s after it are
static float echo_tail(int mm)
{
    quiet(); SI.hand_mm = 0; sim_run(2.5f);
    SI.hand_mm = mm; sim_run(0.5f);
    sing_on(220.0f); sim_run(0.6f);
    SI.mic = NULL; SI.mic_u = NULL; float t0 = sim_t;
    sim_run(0.3f); SI.hand_mm = 0; sim_run(1.1f);
    return m_rms(t0 + 0.2f, t0 + 1.4f);
}
static float choir_level(const voice_state_t *v) { float a = 0; for (int k = 0; k < v->nvoices && k < VOICE_EXTRA; k++) a += v->vlevel[k]; return a; }
static int edge_lit(void) { int n = 0; for (int y = 17; y <= 47; y++) for (int x = OLED_W - 2; x < OLED_W; x++) n += oled_get(x, y) ? 1 : 0; return n; }

static void rev_e(void)
{
    sim_in_t keep = SI;
    uint32_t learned = meta.learned; float hints_on = pv[CP(CP_HINTS)];
    const ctl_t *c = front_ctl();
    voice_state_t vs;
    pv[CP(CP_HINTS)] = 0;

    section("sing: rev E, the board with the joystick first");
    quiet(); front_midi(0xC0, 0, 0); sim_run(0.5f);                // CLEAN, as it comes
    sing_on(220.0f); sim_run(1.5f);
    float r_c = m_rms(sim_t - 0.8f, sim_t);
    SI.lk = 3; sim_tick(); SI.hand_mm = 60; sim_run(0.8f);
    check(!c->has_lknob && !c->has_hand && (int)get("cmix") == 0 && sui.throw_ == 0.0f && !sui.has_hand && edge_lit() == 0 && fabsf(dbr(m_rms(sim_t - 0.6f, sim_t), r_c)) < 0.5f,
          "counts on the left knob's pins and something over the sensor's place do nothing there: CHOIR %d, no throw, the level %+.2f dB", (int)get("cmix"), (double)dbr(m_rms(sim_t - 0.6f, sim_t), r_c));
    SI.hand_mm = 0; SI.lk = 0;
    check(tag("qhnd") == INST.nparams - 1 && param_cc_number(tag("qhnd")) > 0 && param_cc_number(tag("qrol")) == param_cc_number(tag("qspm")) + 1,
          "LOOP > HAND is the last setting, on CC %d: every other setting keeps its CC", param_cc_number(tag("qhnd")));
    int nk = 0, nh = 0; for (int i = 0; i < INST.nhints; i++) { nk += INST.hints[i].learn == LEARN_LKNOB; nh += INST.hints[i].learn == LEARN_HAND; }
    check(nk == 1 && nh == 1, "a hint for the left knob and one for the hand (the core shows them only on a board with the part)");

    section("sing: rev E, the left knob is the choir");
    sim_rev_e(); sim_run(1.0f);
    voice_state(&vs);
    float r0 = m_rms(sim_t - 0.6f, sim_t); int n0 = vs.nvoices;
    check(c->has_lknob && c->has_hand && !c->has_joy && n0 == 0, "rev E: the left knob and the sensor, no joystick; CLEAN is one voice");
    int right = 0;
    for (int k = 1; k <= 4; k++) { sim_lturn(1); sim_run(0.3f); if ((int)get("cmix") == 5 * k) right++; }
    sim_shot("rev_e_choir_toast");
    sim_run(1.2f);
    voice_state(&vs); int n20 = vs.nvoices; float r20 = m_rms(sim_t - 0.6f, sim_t), l20 = choir_level(&vs);
    for (int k = 5; k <= 20; k++) { sim_lturn(1); sim_run(0.1f); if ((int)get("cmix") == 5 * k) right++; }
    sim_run(1.5f);
    voice_state(&vs); int n100 = vs.nvoices; float r100 = m_rms(sim_t - 0.6f, sim_t), l100 = choir_level(&vs), f100 = pitch_now(150.0f, 330.0f);
    sim_shot("rev_e_choir_full");
    // (a few copies a little out of tune can take as much from the level as they add: the copies' own levels are counted)
    check(right == 20 && n20 > 0 && n100 > n20 && l20 > 0.0f && l100 > 2.0f * l20 && r100 > r0 && fabsf(cents(f100, 220.0f)) < 25.0f,
          "each detent is 5 %% of CHOIR > CHOIR, up to 100: at 20 %% %d more voices (their levels together %.2f, the output %+.1f dB), at 100 %% %d (%.2f, %+.1f dB), still on the note (%+.0f cents)",
          n20, (double)l20, (double)dbr(r20, r0), n100, (double)l100, (double)dbr(r100, r0), (double)cents(f100, 220.0f));
    sim_lturn(3); sim_run(0.2f); int top = (int)get("cmix");
    sim_lturn(-3); sim_run(0.2f); int three = (int)get("cmix");
    for (int k = 0; k < 20; k++) { sim_lturn(-1); sim_run(0.05f); }
    sim_lturn(-2); sim_run(2.0f);
    voice_state(&vs);
    float r_back = m_rms(sim_t - 0.6f, sim_t);
    check(top == 100 && three == 85 && (int)get("cmix") == 0 && vs.nvoices == 0 && fabsf(dbr(r_back, r0)) < 1.0f,
          "it stops at 100 and at 0; three detents on one tick are 15 %%; turned back down the choir is gone (%d voices, the level %+.2f dB)", vs.nvoices, (double)dbr(r_back, r0));
    front_midi(0xC0, 3, 0); sim_run(0.5f); int own = (int)get("cmix");
    sim_lturn(-2); sim_run(0.2f); int stepped = (int)get("cmix");
    front_midi(0xC0, 0, 0); sim_run(0.5f);
    check(own == 80 && stepped == 70 && (int)get("cmix") == 0, "it is the voice's own setting: the CHOIR voice comes with 80, two detents down are 70, and CLEAN comes back with 0");

    section("sing: rev E, a hand throws the voice into the echo");
    float t_none = echo_tail(0), t_half = echo_tail(145), t_down = echo_tail(40);
    check(t_down > 2.0f * t_none && t_half > t_none && t_down > t_half,
          "a note sung with the hand over the sensor rings on in the echo: the 1.2 s after it are %+.1f dB with the hand at 145 mm and %+.1f dB at 40 mm, against no hand",
          (double)dbr(t_half, t_none), (double)dbr(t_down, t_none));
    quiet(); sim_hand(40, 1.0f);
    float th = sui.throw_; int lit = edge_lit();
    sim_shot("rev_e_hand_echo");
    sim_hand(0, 2.5f);
    check(th > 0.95f && lit > 30 && sui.throw_ == 0.0f && edge_lit() == 0, "at 40 mm the throw is full (%.2f) and the bar at the screen's edge is up (%d dots); the hand gone, both are gone", (double)th, lit);
    set("qrol", 1); float t_roll_off = echo_tail(40); set("qrol", 0);
    param_set(CP(CP_HAND), 0); float t_off = echo_tail(40); param_set(CP(CP_HAND), 1); sim_run(0.3f);
    check(t_roll_off > 2.0f * t_none && fabsf(dbr(t_off, t_none)) < 1.0f,
          "ROLL: OFF leaves the hand its throw (%+.1f dB); SYSTEM > HAND OFF takes it away (%+.1f dB)", (double)dbr(t_roll_off, t_none), (double)dbr(t_off, t_none));
    // with the roll: the larger of the two counts, and the roll alone is as it was
    quiet(); sim_tilt(-45, 0); sim_run(1.0f); float roll_alone = sui.throw_;
    sim_hand(145, 1.0f); float both = sui.throw_;
    sim_tilt(0, 0); sim_hand(0, 2.5f);
    check(roll_alone > 0.95f && both == roll_alone, "rolled left as well: the larger of the two (the roll %.2f, with a hand half way down %.2f)", (double)roll_alone, (double)both);

    section("sing: rev E, HAND does the stick's throat or bend");
    quiet(); set("vlvl", 100); set("vdry", 0); set("fech", 0); set("frev", 0);
    sing_on(220.0f); sim_run(1.5f);
    float f_a = pitch_now(80.0f, 520.0f), c_a = centre(f_a);
    set("qhnd", 2);
    sim_hand(145, 1.2f); float f_h = pitch_now(80.0f, 520.0f);
    sim_hand(40, 1.2f); float f_d = pitch_now(80.0f, 520.0f);
    sim_shot("rev_e_hand_bend");
    sim_hand(0, 2.5f); float f_b = pitch_now(80.0f, 520.0f);
    check(fabsf(cents(f_h, f_a) + 600.0f) < 25.0f && fabsf(cents(f_d, f_a) + 1200.0f) < 25.0f && fabsf(cents(f_b, f_a)) < 10.0f && sui.throw_ == 0.0f,
          "BEND DOWN: half way down the tuned voice is %.0f cents down, at 40 mm %.0f (the stick's octave), the hand gone %+.0f; nothing is thrown", (double)cents(f_h, f_a), (double)cents(f_d, f_a), (double)cents(f_b, f_a));
    set("qhnd", 3);
    sim_hand(40, 1.2f); float f_u = pitch_now(80.0f, 900.0f);
    sim_hand(0, 2.5f);
    check(fabsf(cents(f_u, f_a) - 1200.0f) < 25.0f, "BEND UP: at 40 mm %.0f cents up", (double)cents(f_u, f_a));
    set("qhnd", 1);
    sim_hand(40, 1.2f); float f_t = pitch_now(80.0f, 520.0f), c_t = centre(f_a), fm = sui.formant;
    sim_shot("rev_e_hand_throat");
    sim_hand(0, 2.5f); float c_b = centre(f_a);
    check(fabsf(fm + 7.0f) < 0.2f && fabsf(cents(f_t, f_a)) < 25.0f && c_t < 0.85f * c_a && fabsf(c_b / c_a - 1.0f) < 0.05f,
          "BIG THROAT: at 40 mm the throat is %.1f semitones (the stick's 7), the note stays (%+.0f cents), the sound's weight falls from %.0f to %.0f Hz and comes back (%.0f)",
          (double)fm, (double)cents(f_t, f_a), (double)c_a, (double)c_t, (double)c_b);
    set("qhnd", 0);

    // a clip for the page (out/sing-rev-e.gif and .wav, the same stretch): CLEAN, a little tune sung; the left knob
    // brings the choir in; a hand comes down and the last notes ring on in the echo; the knob takes the choir out
    {   quiet(); front_midi(0xC0, 0, 0); sim_run(1.0f);
        static const float tune[8] = { 220.0f, 246.94f, 261.63f, 293.66f, 329.63f, 293.66f, 261.63f, 220.0f };
        float t0 = sim_t; int note_i = 0;
        sim_gif_begin("rev-e");
        sing_on(tune[0]);
        for (int i = 0; i < 1150; i++) {
            ui_awake();
            if (i % 60 == 0 && i < 720) { sg.f0 = tune[(note_i++) % 8]; }
            if (i == 720) { SI.mic = NULL; SI.mic_u = NULL; }          // she stops: what is left is the echo
            if (i >= 150 && i < 330 && i % 15 == 0) SI.lk = 1;           // twelve detents: CHOIR 60 %
            if (i >= 480 && i < 700) SI.hand_mm = 240 - (i - 480) * 200 / 220;
            if (i >= 700 && i < 800) SI.hand_mm = 40;
            if (i >= 800 && i < 900) SI.hand_mm = 40 + (i - 800) * 2;
            if (i == 900) SI.hand_mm = 0;
            if (i >= 960 && i < 1080 && i % 10 == 0) SI.lk = -1;
            sim_tick();
        }
        sim_gif_end();
        sim_wav("-rev-e", t0, sim_t);
    }

    section("sing: back on the board with the joystick");
    SI = keep; quiet(); meta.learned = learned; pv[CP(CP_HINTS)] = hints_on;
    front_midi(0xC0, 0, 0); sim_run(3.0f);
    check(!c->has_lknob && !c->has_hand && (int)get("cmix") == 0 && (int)get("qhnd") == 0 && sui.throw_ == 0.0f && sui.hand == 0.0f, "no left knob, no hand, CHOIR 0, nothing thrown");
}

void inst_tests(void)
{
    voices();
    tuning_notes();
    loop_alone();
    with_band();
    cost();
    rev_e();
}
