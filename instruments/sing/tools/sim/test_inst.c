// SING's own checks: the voices (every sound), the loop (alone, with the tilt, the stutter, free), the song's
// notes for the tuning, a MIDI chord, the link with a pretend BAND (its tempo, key and loops, over drifting
// clocks), and the cost. The engines' own measurements are in tools/dsp (run_voice.sh, run_fx.sh).
#include "sim.h"
#include "sing.h"
#include "link.h"
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

void inst_tests(void)
{
    voices();
    tuning_notes();
    loop_alone();
    with_band();
    cost();
}
