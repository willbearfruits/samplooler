// AIRDWELLER's engine on the host: the eight drums (each one's character measured), the material (its modes, RING,
// CHORD, BEND, MORPH, the bow, CREAK, DUCK), CUT, smoothness under every sound control, the drag (each voice pulled by
// the gesture that struck it), the notes (BASS, SQUELCH, LEAD, BELL: pitch, level, legato, the hold), SHAPE, a loop's
// NOTES hits, the cost of the busiest block in each role, and sound files to listen to (out/airdweller-eng-*.wav,
// -drag-*.wav, -notes-*.wav). Controls go through the params (the host writes eng_* from them every tick); hits go
// through eng_trigger and eng_strike as the host's own do, notes through eng_note.
//
// The drag is the test's own: before every tick (the audio runs first in a tick, the instrument's tick after it) tick()
// writes eng_drag_* from TD, so what the instrument's stroke detector makes of the still box never reaches these checks.
// The notes' voice and glide go both ways (the param, and eng_note_* before every tick).
// AD_COST_ONLY=1 ./run.sh inst runs only the cost cases here (while working on the cost).
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float st_mic(float t, void *u);
void  st_reset(void);
void  st_hold(vec3_t dir, float dps);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;

#define SRF ((float)AUDIO_SR)
// the test's drag and notes, written before every tick
static struct { int gid; float amt, hold, bend, depth; int nv; float glide, bow, creak; } TD = { 0, 0, 0, 0, 1.0f, NV_BASS, 0.06f, -1.0f, -1.0f };
static void (*tick_fn)(void);                           // something to do before each tick (a sequence), or NULL
static double rec_t[128], rec_last; static int rec_n; static bool rec_on;   // the cost cases: each tick's time in eng_audio
static void  tick(void)
{
    if (tick_fn) tick_fn();
    eng_drag_gid = TD.gid; eng_drag_amt = TD.amt; eng_drag_hold = TD.hold; eng_drag_bend = TD.bend; eng_drag_depth = TD.depth;
    eng_note_voice = TD.nv; eng_note_glide = TD.glide;
    if (TD.bow >= 0.0f) eng_bow = TD.bow;                      // (the cost cases bow and creak the material directly)
    if (TD.creak >= 0.0f) eng_creak = TD.creak;
    ui_awake();                                                 // a player at work: no screensaver comes on (the screens' tests follow)
    sim_tick();
    if (rec_on && rec_n < 128) { rec_t[rec_n++] = eng_audio_ns - rec_last; rec_last = eng_audio_ns; }
}
static void  run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) tick(); }
static int   P(const char *tag) { int p = param_by_tag(tag); if (p < 0) { printf("no param %s\n", tag); exit(1); } return p; }
static void  set(const char *tag, float v) { param_set(P(tag), v); }
static float dbr(float a, float b) { return 20.0f * log10f((a + 1e-12f) / (b + 1e-12f)); }

// ---------------------------------------------------------------- measurements on what was played
static int ix(float t) { int i = (int)(t * SRF); return i < 0 ? 0 : i > sim_n ? sim_n : i; }
static float zcr(float t0, float t1)                     // zero crossings, as a frequency
{
    int a = ix(t0), b = ix(t1), z = 0;
    for (int i = a + 1; i < b; i++) z += (sim_out[i - 1] < 0.0f) != (sim_out[i] < 0.0f);
    return b > a ? 0.5f * (float)z * SRF / (float)(b - a) : 0.0f;
}
static float centroid(float t0, float t1)                // the rms frequency, from the first difference's energy
{
    int a = ix(t0), b = ix(t1);
    double e = 0, d = 0;
    for (int i = a + 1; i < b; i++) { double x = sim_out[i], dx = x - sim_out[i - 1]; e += x * x; d += dx * dx; }
    if (e <= 1e-20) return 0.0f;
    double r = 0.5 * sqrt(d / e); if (r > 1) r = 1;
    return (float)(SRF / M_PI * asin(r));
}
static float hp_share(float t0, float t1, float fc)       // the share of the energy above fc (a 2nd-order high pass)
{
    int a = ix(t0), b = ix(t1);
    double w = 2 * M_PI * fc / SRF, c = cos(w), al = sin(w) / (2 * 0.7071);
    double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = b0, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, e = 0, eh = 0;
    for (int i = a; i < b; i++) {
        double x = sim_out[i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; e += x * x; eh += y * y;
    }
    return e > 0 ? (float)(eh / e) : 0.0f;
}
static float amp_at(float t0, float t1, float f) { return sqrtf(m_goertzel(t0, t1, f)); }
static float peak_freq(float t0, float t1, float lo, float hi)   // the strongest frequency between lo and hi
{
    float best = 0, bf = lo;
    for (float f = lo; f <= hi; f += 0.25f) { float p = m_goertzel(t0, t1, f); if (p > best) { best = p; bf = f; } }
    return bf;
}
static float t60_at(float t0, float f)                   // a partial's T60 from its level 0.2 s and 1.2 s after t0
{
    float a1 = amp_at(t0 + 0.15f, t0 + 0.25f, f), a2 = amp_at(t0 + 1.15f, t0 + 1.25f, f);
    float d = dbr(a1, a2);
    return d > 0.01f ? 60.0f / d : 99.0f;
}
// the slips of a creak: where the first difference jumps over a quarter of its largest, 5 ms apart at least
static float slip_cv(float t0, float t1, float *rate, int *nslip)
{
    int a = ix(t0), b = ix(t1);
    float mx = 0;
    for (int i = a + 1; i < b; i++) { float d = fabsf(sim_out[i] - sim_out[i - 1]); if (d > mx) mx = d; }
    int last = -100000, n = 0; double s = 0, s2 = 0; int prev = -1;
    for (int i = a + 1; i < b; i++) {
        float d = fabsf(sim_out[i] - sim_out[i - 1]);
        if (d > 0.25f * mx && i - last > (int)(0.005f * SRF)) {
            if (prev >= 0) { double iv = (double)(i - prev) / SRF; s += iv; s2 += iv * iv; n++; }
            prev = i; last = i;
        } else if (d > 0.25f * mx) last = i;
    }
    *nslip = n + 1;
    if (n < 3) { *rate = 0; return 0; }
    double m = s / n, sd = sqrt(fmax(s2 / n - m * m, 0));
    *rate = (float)(1.0 / m);
    return (float)(sd / m);
}
// the worst click (m_click) at every tick between t0 and t1
static float worst_click(float t0, float t1)
{
    float w = 0;
    for (float t = t0; t < t1; t += 0.01f) { float r = m_click(t, NULL, NULL); if (r > w) w = r; }
    return w;
}

// ---------------------------------------------------------------- a sound file of our own (two boxes mixed)
static void wav_mix(const char *suffix, float a0, float b0, float len)
{
    char fn[256]; snprintf(fn, sizeof fn, "out/%s%s.wav", sim_name, suffix);
    FILE *f = fopen(fn, "wb"); if (!f) return;
    int n = (int)(len * SRF), ia = ix(a0), ib = ix(b0);
    if (ia + n > sim_n || ib + n > sim_n) { fclose(f); return; }
    uint32_t sr = AUDIO_SR, br = sr * 2, ds = (uint32_t)n * 2, rs = 36 + ds, fl = 16; uint16_t one = 1, bits = 16, ba = 2;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) {
        float v = 0.75f * (sim_out[ia + i] + sim_out[ib + i]);
        v = v > 1 ? 1 : v < -1 ? -1 : v;
        int16_t q = (int16_t)(v * 32767.0f); fwrite(&q, 2, 1, f);
    }
    fclose(f);
}

// ---------------------------------------------------------------- set-ups
static const char *const touched[] = { "role", "pfrt", "plft", "prgt", "pbck", "tune", "decy", "sqez", "matl", "mrph", "tnto", "rato",
    "chrd", "ring", "brit", "bend", "crek", "clmb", "fall", "rstl", "shak", "drve", "tone", "echo", "esyn", "efb ", "emix", "room",
    "rsiz", "levl", "duck", "pair", NULL };
static void defaults(void) { for (int i = 0; touched[i]; i++) { int p = P(touched[i]); param_set(p, param_def(p)); } }
static void still(void) { SI.mic = st_mic; st_reset(); sim_tilt(0, 0); SI.acc = (vec3_t){ 0, 0, 1 }; }
// a clean bus to measure on: no drive, echo or room, tone open, level 100 %, no squeeze
static void clean(void)
{
    still(); defaults();
    set("drve", 0); set("emix", 0); set("room", 0); set("tone", 100); set("levl", 100); set("sqez", 0);
    run(0.6f);
}
static float hit(int voice, int vel)                     // DRUM: the FRONT pad plays this voice; returns when it sounds
{
    set("role", ROLE_DRUM); set("pfrt", (float)voice); run(0.03f);
    float t0 = sim_t;
    eng_trigger(&(trig_t){ .pad = PD_FRONT, .vel = (uint8_t)vel, .src = SRC_LIVE });
    return t0;
}
static float strike(int voice, int vel) { float t0 = sim_t; eng_strike(0, vel, voice); return t0; }
static void shake_tick(void) { static int k; SI.acc = (vec3_t){ 0, 0, (k++ & 1) ? 2.0f : 0.2f }; tick(); }
static void shake(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) shake_tick(); }

// the other hand (for DUCK: the host ducks only when another AIRDWELLER is there)
static int peer = -1;
static void peer_here(void)
{
    if (peer < 0) peer = sim_peer_add("AIRDWELL", 1, 4.0, 0.0); else sim_peer_power(peer, true);
    uint8_t a[16] = { 4, ROLE_DRUM | HAND_LEFT << 3, 0, 36, 0 };
    sim_peer_use(peer); link_set_app(a, 16); link_poke(); sim_peer_use(-1);
}

// ---------------------------------------------------------------- the kick
static void kick(void)
{
    section("engine: the kick");
    clean();
    float t0 = hit(DV_KICK, 127);
    run(1.2f);
    float kz = eng_kick_hz;
    float z0 = zcr(t0, t0 + 0.002f), p60 = m_pitch_at(t0 + 0.06f, t0 + 0.2f, 25.0f, 200.0f);
    check(z0 > 500.0f && fabsf(p60 / kz - 1.0f) < 0.03f, "it falls from %.0f Hz (the first 2 ms) to %.1f Hz by 60 ms: the note is %.1f Hz", (double)z0, (double)p60, (double)kz);
    float p20 = m_pitch_at(t0 + 0.02f, t0 + 0.06f, 25.0f, 400.0f);
    note("20..60 ms: %.1f Hz; 40..140 ms: %.1f Hz", (double)p20, (double)m_pitch_at(t0 + 0.04f, t0 + 0.14f, 25.0f, 200.0f));
    float r0 = m_rms(t0, t0 + 0.05f), r3 = m_rms(t0 + 0.3f, t0 + 0.35f), r1 = m_rms(t0 + 1.0f, t0 + 1.05f);
    check(dbr(r3, r0) > -30.0f, "the sub's tail: %.1f dB at 300 ms (%.1f dB at 1 s)", (double)dbr(r3, r0), (double)dbr(r1, r0));
    float pk = m_peak(t0, t0 + 0.3f);
    check(pk > 0.3f && pk < 0.7f, "full velocity peaks at %.2f (%.1f dBFS), no drive", (double)pk, (double)dbr(pk, 1.0f));
    metric("kick_peak_dbfs", dbr(pk, 1.0f));
    float lo = amp_at(t0 + 0.05f, t0 + 0.25f, kz), hi = amp_at(t0 + 0.05f, t0 + 0.25f, 1000.0f);
    check(dbr(lo, hi) > 20.0f, "heavy at the note: %.0f dB over 1 kHz in its body", (double)dbr(lo, hi));
    // SQUEEZE: the harmonics of the body (2..5 x the note) against the note
    float h[2];
    for (int s = 0; s < 2; s++) {
        set("sqez", s ? 100.0f : 0.0f); run(0.1f);
        float t = hit(DV_KICK, 127); run(0.6f);
        float f = amp_at(t + 0.04f, t + 0.14f, kz), e = 0;
        for (int k = 2; k <= 5; k++) e += m_goertzel(t + 0.04f, t + 0.14f, kz * (float)k);
        h[s] = sqrtf(e) / (f + 1e-9f);
        // the fold keeps the voice within its level (0.5); its asymmetric attack carries a little DC, which the master's
        // DC blocker takes away, moving the peaks a little
        if (s) { float pk2 = m_peak(t, t + 0.3f); check(pk2 < 0.891f, "SQUEEZE 100 stays bounded: peak %.2f after the master (under -1 dBFS)", (double)pk2); }
    }
    check(h[1] > 4.0f * h[0] && h[1] > 0.15f, "SQUEEZE 100 folds it: harmonics %.3f of the note against %.3f at 0", (double)h[1], (double)h[0]);
}

// ---------------------------------------------------------------- every drum, each by its character
static void drums(void)
{
    section("engine: the kit");
    clean();
    float t;
    // SNAP: most of its energy over 1 kHz in the first 20 ms
    t = hit(DV_SNAP, 127); run(0.6f);
    float hs = hp_share(t, t + 0.02f, 1000.0f);
    check(hs > 0.6f, "SNAP: %.0f %% of its first 20 ms over 1 kHz (latex)", (double)(hs * 100.0f));
    float sq0 = centroid(t + 0.008f, t + 0.016f), sq1 = centroid(t + 0.02f, t + 0.04f);
    note("SNAP: its band pass whips down: centroid %.0f Hz at 8..16 ms, %.0f Hz at 20..40 ms", (double)sq0, (double)sq1);
    // CLANG: inharmonic partials, ringing past half a second
    t = hit(DV_CLANG, 127); run(1.5f);
    float f0 = 415.0f;
    float inh = m_goertzel(t + 0.05f, t + 0.35f, f0 * 1.504f) + m_goertzel(t + 0.05f, t + 0.35f, f0 * 2.416f);
    float har = m_goertzel(t + 0.05f, t + 0.35f, f0 * 2.0f) + m_goertzel(t + 0.05f, t + 0.35f, f0 * 3.0f);
    float fl = peak_freq(t + 0.05f, t + 0.35f, 300.0f, 500.0f);
    check(inh > 30.0f * har && fabsf(fl / f0 - 1.0f) < 0.01f, "CLANG: its lowest mode %.0f Hz; partials at 1.504 and 2.416 of it %.0f dB over 2x and 3x", (double)fl, (double)(10.0f * log10f(inh / (har + 1e-20f))));
    float c0 = m_rms(t, t + 0.05f), c5 = m_rms(t + 0.5f, t + 0.6f);
    check(dbr(c5, c0) > -30.0f, "CLANG rings: %.1f dB at 0.5 s", (double)dbr(c5, c0));
    // HAT: high, gone within 200 ms
    t = hit(DV_HAT, 127); run(0.5f);
    float h0 = m_rms(t, t + 0.02f), h2 = m_rms(t + 0.2f, t + 0.25f), hc = centroid(t, t + 0.03f);
    check(dbr(h2, h0) < -45.0f && hc > 3000.0f, "HAT: %.0f dB at 200 ms, centroid %.0f Hz", (double)dbr(h2, h0), (double)hc);
    // GLASS: high and long
    t = hit(DV_GLASS, 127); run(1.5f);
    float gc = centroid(t + 0.05f, t + 0.3f), g0 = m_rms(t, t + 0.05f), g6 = m_rms(t + 0.6f, t + 0.7f);
    check(gc > 1000.0f && dbr(g6, g0) > -30.0f, "GLASS: centroid %.0f Hz, %.1f dB at 0.6 s", (double)gc, (double)dbr(g6, g0));
    float b0 = centroid(t + 0.0005f, t + 0.004f), b1 = centroid(t + 0.008f, t + 0.02f);
    note("GLASS: centroid %.0f Hz in its first 4 ms, %.0f Hz at 8..20 ms (the index falls, the blip rises)", (double)b0, (double)b1);
    // SUB: an octave lower at 0.8 s than at 0.05 s
    t = hit(DV_SUB, 127); run(1.5f);
    float s0 = m_pitch_at(t + 0.03f, t + 0.08f, 50.0f, 400.0f), s8 = m_pitch_at(t + 0.76f, t + 0.86f, 20.0f, 200.0f);
    check(s0 > 0.0f && s8 > 0.0f && s0 / s8 >= 1.9f, "SUB: %.1f Hz at 0.05 s, %.1f Hz at 0.8 s (%.2f octaves)", (double)s0, (double)s8, (double)log2f(s0 / (s8 + 1e-9f)));
    // WHIP: its centroid climbs in the first 10 ms, then the crack
    t = hit(DV_WHIP, 127); run(0.5f);
    float w0 = centroid(t + 0.0005f, t + 0.003f), w1 = centroid(t + 0.004f, t + 0.0063f);
    check(w1 > 1.5f * w0, "WHIP: centroid %.0f Hz at 0.5..3 ms, %.0f Hz at 4..6 ms", (double)w0, (double)w1);
    float cr = m_peak(t + 0.0064f, t + 0.0075f), bw = m_peak(t + 0.003f, t + 0.006f);
    note("WHIP: the crack %.2f against the sweep's %.2f", (double)cr, (double)bw);
    // CREAK: slips at irregular intervals
    t = hit(DV_CREAK, 127); run(1.2f);
    float rate; int ns;
    float cv = slip_cv(t + 0.04f, t + 0.4f, &rate, &ns);
    check(cv > 0.15f && rate > 15.0f && rate < 90.0f, "CREAK: %d slips, %.0f a second, their intervals vary by %.0f %%", ns, (double)rate, (double)(cv * 100.0f));
    // every voice at a sane level, with the default SQUEEZE and the default bus
    still(); defaults(); set("levl", 100); run(0.5f);
    int bad = 0;
    for (int v = 0; v < DV_OFF; v++) {
        float tv = hit(v, 127); run(0.8f);
        float pk = m_peak(tv, tv + 0.8f), rm = m_rms(tv, tv + 0.1f);
        if (!(pk > 0.06f && pk < 0.8f && rm > 0.005f)) { bad++; note("%s: peak %.3f rms %.4f", dv_name[v], (double)pk, (double)rm); }
        metric(dv_name[v], dbr(pk, 1.0f));
    }
    check(bad == 0, "every drum sounds at a sane level (peaks between -24 and -2 dBFS with the default bus; above: dBFS)");
    // velocity: softer and darker. Each hit's noise is new (a running seed), so each is measured over eight hits of
    // each, taken in turn: the mean peak (dB) and the mean centroid, and every soft one against every hard one's mean
    clean();
    float lh = 0, ll = 0, ch = 0, cl = 0, cl_max = 0;
    for (int r = 0; r < 8; r++) {
        t = hit(DV_CLANG, 127); run(0.6f);
        lh += dbr(m_peak(t, t + 0.3f), 1.0f) / 8.0f; ch += centroid(t, t + 0.05f) / 8.0f;
        t = hit(DV_CLANG, 40); run(0.6f);
        float c = centroid(t, t + 0.05f);
        ll += dbr(m_peak(t, t + 0.3f), 1.0f) / 8.0f; cl += c / 8.0f; cl_max = fmaxf(cl_max, c);
    }
    check(lh - ll > 6.0f && ch > cl, "velocity 40 against 127 (eight of each): %.1f dB down, centroid %.0f against %.0f Hz", (double)(lh - ll), (double)cl, (double)ch);
    note("the brightest soft hit's centroid %.0f Hz (the hard ones' mean %.0f)", (double)cl_max, (double)ch);
    // polyphony and stealing: 14 clangs, 12 sound, the stolen ones fade (no click where they go)
    clean(); set("decy", 4); run(0.1f);
    float tf = 0;
    for (int i = 0; i < 14; i++) { if (i == 12) tf = sim_t; hit(DV_CLANG, 100); run(0.05f); }
    int von = eng_voices_on;
    float wc = 0;
    for (float tc = tf; tc < tf + 0.12f; tc += 0.002f) { float r = m_click(tc, NULL, NULL); if (r > wc) wc = r; }
    check(von == 12, "polyphony: %d drums at once", von);
    note("a voice stolen: the worst first-difference jump near it is %.1fx the signal's own (new hits are in there too)", (double)wc);
    run(0.5f);
}

// ---------------------------------------------------------------- the material
static void material(void)
{
    section("engine: the material");
    clean();
    set("role", ROLE_PAINT); set("matl", MAT_METAL); set("chrd", CH_ONE); set("ring", 2.5f); run(0.5f);
    float f0 = eng_mat_hz;
    // a heavy strike rings at the note with RING's T60
    float t = strike(DV_KICK, 127); run(2.0f);
    float pf = peak_freq(t + 0.1f, t + 0.6f, f0 * 0.9f, f0 * 1.1f), t60 = t60_at(t, f0);
    check(fabsf(pf / f0 - 1.0f) < 0.005f && fabsf(t60 / 2.5f - 1.0f) < 0.3f, "METAL struck: it rings at %.1f Hz (the note %.1f) with a T60 of %.2f s (RING 2.5 s)", (double)pf, (double)f0, (double)t60);
    float pk = m_peak(t, t + 0.5f);
    check(pk > 0.1f && pk < 0.6f, "a heavy strike at full velocity peaks at %.2f (%.1f dBFS)", (double)pk, (double)dbr(pk, 1.0f));
    metric("material_strike_peak_dbfs", dbr(pk, 1.0f));
    // a bright strike rings every mode: METAL's partials, not what lies between them
    t = strike(DV_SNAP, 127); run(1.0f);
    static const float on_r[4] = { 1.0f, 1.505f, 1.811f, 2.598f }, off_r[4] = { 1.25f, 2.2f, 3.05f, 1.66f };
    float mn = 1e9f, mx = 0;
    for (int k = 0; k < 4; k++) { float a = m_goertzel(t + 0.02f, t + 0.22f, f0 * on_r[k]); if (a < mn) mn = a; float b = m_goertzel(t + 0.02f, t + 0.22f, f0 * off_r[k]); if (b > mx) mx = b; }
    check(mn > 10.0f * mx, "METAL's modes (1, 1.505, 1.811, 2.598 x the note) each %.0f dB over the gaps between them", (double)(10.0f * log10f(mn / (mx + 1e-20f))));
    // RING is the lowest mode's T60
    set("ring", 1.0f); run(0.3f);
    t = strike(DV_KICK, 127); run(1.6f);
    float t60b = t60_at(t, f0);
    check(fabsf(t60b - 1.0f) < 0.3f, "RING 1 s: T60 %.2f s", (double)t60b);
    set("ring", 2.5f); run(1.0f);
    // CHORD: FIFTH adds the 3/2 (on GLASS, which has no mode near 1.5)
    set("matl", MAT_GLASS); run(2.5f);
    float e[2];
    for (int c = 0; c < 2; c++) {
        set("chrd", c ? CH_FIFTH : CH_ONE); run(0.3f);
        t = strike(DV_KICK, 127); run(1.0f);
        e[c] = m_goertzel(t + 0.05f, t + 0.3f, f0 * 1.5f) / (m_goertzel(t + 0.05f, t + 0.3f, f0) + 1e-20f);
        run(2.0f);
    }
    check(e[1] > 30.0f * e[0], "CHORD FIFTH: the 3/2 rings (%.0f dB over the note's share with ONE)", (double)(10.0f * log10f(e[1] / (e[0] + 1e-20f))));
    set("chrd", CH_ONE); set("matl", MAT_METAL); run(2.5f);
    // MORPH: RUBBER against METAL: shorter, and its own (nearly harmonic) partials
    float dm[2], pm[2];
    for (int m = 0; m < 2; m++) {
        set("matl", m ? MAT_RUBBER : MAT_METAL); run(0.5f);
        t = strike(DV_SNAP, 127); run(1.5f);
        float a0 = m_rms(t + 0.02f, t + 0.07f), a1 = m_rms(t + 0.3f, t + 0.35f);
        dm[m] = dbr(a1, a0);
        pm[m] = m_goertzel(t + 0.1f, t + 0.2f, f0 * 2.01f) / (m_goertzel(t + 0.1f, t + 0.2f, f0 * 2.598f) + 1e-20f);
    }
    check(dm[1] < dm[0] - 10.0f, "RUBBER dies sooner: %.1f dB at 0.3 s against METAL's %.1f dB", (double)dm[1], (double)dm[0]);
    check(pm[1] > 10.0f && pm[0] < 0.1f, "and rings on other partials: 2.01 over 2.598 x the note %.0f dB (RUBBER), %.0f dB (METAL)", (double)(10.0f * log10f(pm[1])), (double)(10.0f * log10f(pm[0])));
    set("matl", MAT_METAL); run(1.5f);
    // BEND: a twist bends it by TWIST BEND semitones
    set("bend", 7); run(0.1f);
    st_hold(ST_TWIST, 300.0f); run(0.5f);
    float be = eng_bend;
    t = strike(DV_KICK, 127); run(0.4f);
    float want = f0 * powf(2.0f, be / 12.0f), got = peak_freq(t + 0.05f, t + 0.35f, f0 * 1.2f, f0 * 1.8f);
    check(be > 6.5f && fabsf(got / want - 1.0f) < 0.006f, "TWIST BEND 7 with a full twist: bent %.2f semitones, it rings at %.1f Hz (%.1f wanted)", (double)be, (double)got, (double)want);
    st_hold(ST_TWIST, 0.0f); run(1.0f);
    // LATEX: a strike, then the pitch squeaks up and back
    set("matl", MAT_LATEX); set("ring", 2.0f); run(0.5f);
    t = strike(DV_KICK, 127); run(0.6f);
    float l0 = peak_freq(t + 0.004f, t + 0.016f, f0 * 0.8f, f0 * 1.6f), l1 = peak_freq(t + 0.03f, t + 0.07f, f0 * 0.8f, f0 * 1.6f), l2 = peak_freq(t + 0.4f, t + 0.55f, f0 * 0.8f, f0 * 1.6f);
    check(l1 > l0 * 1.03f && fabsf(l2 / f0 - 1.0f) < 0.02f, "LATEX squeaks: %.0f Hz at the strike, %.0f Hz 50 ms on, back to %.0f Hz", (double)l0, (double)l1, (double)l2);
    set("matl", MAT_METAL); set("ring", 2.5f); run(2.0f);
    // DRUM: no material, strikes ignored
    set("role", ROLE_DRUM); run(0.5f);
    float q0 = m_rms(sim_t - 0.1f, sim_t);
    strike(DV_KICK, 127); run(0.5f);
    float q1 = m_rms(sim_t - 0.45f, sim_t);
    check(q1 < 1e-4f && q0 < 1e-4f && eng_mat_lvl == 0.0f, "DRUM: the material is silent and the other hand's strikes are ignored (rms %.6f)", (double)q1);
}

// ---------------------------------------------------------------- the bow (PUMP), CREAK, DUCK, CUT
static float rms_win(float t0, float t1, float w, float *mn, float *mx)
{
    *mn = 1e9f; *mx = 0;
    for (float t = t0; t + w <= t1 + 1e-4f; t += w) { float r = m_rms(t, t + w); if (r < *mn) *mn = r; if (r > *mx) *mx = r; }
    return m_rms(t0, t1);
}
static void bow(void)
{
    section("engine: the bow, CREAK, DUCK, CUT");
    clean();
    // PUMP moved: the energy climbs and the bow sustains a drone
    set("role", ROLE_PUMP); set("clmb", 1); set("chrd", CH_ONE); run(0.5f);
    float r_rest = m_rms(sim_t - 0.3f, sim_t);
    st_hold(ST_FRONT, 400.0f); run(3.0f);
    float mn, mx, r = rms_win(sim_t - 1.5f, sim_t, 0.25f, &mn, &mx);
    check(eng_bow > 0.9f && r > 0.01f && dbr(mx, mn) < 3.0f, "PUMP moved: BOW %.2f, a steady drone (rms %.3f, 0.25 s windows within %.1f dB)", (double)eng_bow, (double)r, (double)dbr(mx, mn));
    check(dbr(r, r_rest) > 15.0f, "the drone moved is %.0f dB over the drone at rest (REST LEVEL)", (double)dbr(r, r_rest));
    float pf = peak_freq(sim_t - 1.0f, sim_t, eng_mat_hz * 0.95f, eng_mat_hz * 1.05f);
    check(fabsf(pf / eng_mat_hz - 1.0f) < 0.005f, "it drones on the material's note (%.1f Hz, the note %.1f)", (double)pf, (double)eng_mat_hz);
    // CUT: silence within 5 ms, no click, back
    float tc = sim_t;
    eng_cut = 1; run(0.3f);
    float rc = m_rms(tc + 0.005f, tc + 0.25f), kc = m_click(tc, NULL, NULL);
    float tb = sim_t;
    eng_cut = 0; run(0.3f);
    float kb = m_click(tb, NULL, NULL), rb = m_rms(tb + 0.05f, tb + 0.25f);
    // what is left after 5 ms is the master's DC blocker settling (a 16 ms exponential, no sound): its steps are tiny
    float dmax = 0, dref = 0;
    for (int i = ix(tc + 0.005f) + 1; i < ix(tc + 0.25f); i++) dmax = fmaxf(dmax, fabsf(sim_out[i] - sim_out[i - 1]));
    for (int i = ix(tc - 0.2f) + 1; i < ix(tc); i++) dref = fmaxf(dref, fabsf(sim_out[i] - sim_out[i - 1]));
    note("CUT: peak %.4f in its 3 ms fade, %.6f from 5 ms on (the master's DC blocker settling, rms %.7f)", (double)m_peak(tc, tc + 0.003f), (double)m_peak(tc + 0.005f, tc + 0.25f), (double)rc);
    check(dbr(dmax, dref) < -60.0f && kc <= 3.0f, "CUT: no sound from 5 ms on (its largest step %.0f dB under the drone's), no click (%.2f)", (double)dbr(dmax, dref), (double)kc);
    check(kb <= 3.0f && dbr(rb, r) > -2.0f, "back: no click (%.2f), the drone as it was (%.1f dB)", (double)kb, (double)dbr(rb, r));
    st_hold(ST_FRONT, 0.0f);                                    // the stroke model stops writing SI.acc: the shake is the test's
    // the drone with the shake's CREAK, against without
    float cr[2], cc[2], ce = 0;
    for (int c = 0; c < 2; c++) {
        set("crek", c ? 100.0f : 0.0f); set("shak", 100); run(0.5f);
        shake(1.0f);
        if (c) ce = eng_creak;
        cr[c] = m_rms(sim_t - 0.6f, sim_t); cc[c] = centroid(sim_t - 0.6f, sim_t);
        SI.acc = (vec3_t){ 0, 0, 1 }; run(1.0f);
    }
    check(ce > 0.5f && dbr(cr[1], cr[0]) > 3.0f && cc[1] > cc[0], "a shake with CREAK 100 (%.2f) scrapes: %.1f dB louder, centroid %.0f Hz against %.0f", (double)ce, (double)dbr(cr[1], cr[0]), (double)cc[1], (double)cc[0]);
    // DUCK: the other hand's strike pushes the drone down and lets it back (the box moved again: the drone pumped)
    st_hold(ST_FRONT, 400.0f);
    peer_here(); run(3.0f);
    check(eng_duck > 0.4f, "another AIRDWELLER here: DUCK %.2f", (double)eng_duck);
    float before = m_rms(sim_t - 0.1f, sim_t);
    float ts = strike(DV_HAT, 4); run(0.6f);
    float dip = m_rms(ts + 0.03f, ts + 0.07f), back = m_rms(ts + 0.4f, ts + 0.5f);
    check(dbr(dip, before) < -3.0f && fabsf(dbr(back, before)) < 1.5f, "a strike from the other hand ducks the drone %.1f dB, %.1f dB 0.4 s on", (double)dbr(dip, before), (double)dbr(back, before));
    st_hold(ST_FRONT, 0.0f);
    sim_peer_power(peer, false);
    set("role", ROLE_DRUM); run(3.0f);
}

// ---------------------------------------------------------------- smoothness: every sound control jumped end to end, sounding
static void smooth(void)
{
    section("engine: every control end to end, sounding");
    clean(); defaults();
    set("role", ROLE_PUMP); set("clmb", 1); set("chrd", CH_TRIAD); run(0.3f);
    st_hold(ST_FRONT, 400.0f); run(2.5f);
    static const char *const tags[] = { "matl", "rato", "chrd", "ring", "brit", "crek", "drve", "tone", "echo", "esyn", "efb ", "emix",
        "room", "rsiz", "levl", "rstl", "duck", "tnto", "sqez", "decy", "tune", "mrph", "bend", NULL };
    int bad = 0, n = 0;
    for (int i = 0; tags[i]; i++) {
        int p = P(tags[i]); const param_t *d = param(p);
        float keep = pv[p], worst = 0;
        for (int dir = 0; dir < 2; dir++) {
            param_set(p, dir ? d->min : d->max); run(0.35f);
            param_set(p, dir ? d->max : d->min);
            float tc = sim_t; run(0.2f);
            float r = m_click(tc, NULL, NULL); if (r > worst) worst = r;
        }
        param_set(p, keep); run(0.3f); n++;
        if (worst > 3.0f) { bad++; note("%s: %.1fx", d->name, (double)worst); }
    }
    check(bad == 0, "PUMP droning (TRIAD): %d sound controls jump end to end without a click (%d click)", n, bad);
    // MORPH swept by the tilt, while it drones and is struck: no click anywhere in it
    set("matl", MAT_GLASS); set("mrph", 100); sim_tilt(0, 30); run(1.0f);
    float t0 = sim_t;
    for (int i = 0; i <= 300; i++) {
        sim_tilt(0, 30.0f - 75.0f * (float)i / 300.0f);         // morph 0 .. 2.5
        tick();
    }
    set("matl", MAT_LATEX); run(1.0f);                          // and on to LATEX
    float wc = worst_click(t0 + 0.05f, sim_t - 0.2f);
    check(wc <= 3.0f, "MORPH swept METAL > GLASS > RUBBER > LATEX while it drones: worst %.2fx", (double)wc);
    sim_wav("-eng-morph", t0 - 0.5f, sim_t);
    st_hold(ST_FRONT, 0.0f); sim_tilt(0, 0);
    set("role", ROLE_DRUM); defaults(); run(1.0f);
}

// ---------------------------------------------------------------- the drag
static void td_none(void) { TD.gid = 0; TD.amt = TD.hold = TD.bend = 0.0f; }
// no drag, and on until the last 0.1 s is silent and no drum sounds (20 s at most): the next case starts from nothing
static void quiet_down(void) { td_none(); run(0.2f); for (int i = 0; i < 2000 && (m_rms(sim_t - 0.1f, sim_t) > 1e-5f || eng_voices_on > 0); i++) tick(); }
static float hitg(int voice, int vel, int gid)          // DRUM: the FRONT pad plays this voice, struck by gesture gid
{
    set("role", ROLE_DRUM); set("pfrt", (float)voice); run(0.03f);
    float t0 = sim_t;
    eng_trigger(&(trig_t){ .pad = PD_FRONT, .vel = (uint8_t)vel, .src = SRC_LIVE, .gid = (uint8_t)gid });
    return t0;
}
// a slow drag of gesture g after its hit: from `from` s on, amt eased up to `to` over `secs`, the hold `hold` while it
// moves (from `from`), the bend eased to `bend` with it (or, twist != 0, swinging +-bend at `twist` Hz); `len` s in all
static void drag_play(int g, float from, float to, float secs, float hold, float bend, float twist, float len)
{
    float t0 = sim_t;
    TD.gid = g;
    int n = (int)(len * 100.0f + 0.5f);
    for (int i = 0; i < n; i++) {
        float t = (float)i * 0.01f + 0.01f - from, u = clampf(t / secs, 0.0f, 1.0f), e = u * u * (3.0f - 2.0f * u);
        TD.amt = t >= 0.0f ? to * e : 0.0f;
        TD.hold = t >= 0.0f ? hold : 0.0f;
        TD.bend = t < 0.0f ? 0.0f : twist != 0.0f ? bend * e * sinf(6.2831853f * twist * t) : bend * e;
        tick();
    }
    (void)t0;
}
static float peak_fine(float t0, float t1, float lo, float hi)   // the strongest frequency, to 0.02 Hz
{
    float c = peak_freq(t0, t1, lo, hi), best = 0, bf = c;
    for (float f = c - 0.3f; f <= c + 0.3f; f += 0.02f) { float p = m_goertzel(t0, t1, f); if (p > best) { best = p; bf = f; } }
    return bf;
}
static float cents(float got, float want) { return 1200.0f * log2f(got / want); }

static void drag_tests(void)
{
    section("engine: the drag (a hit, then the hand pulls it)");
    clean(); set("drag", 100); TD.depth = 1.0f; quiet_down();
    float kz = eng_kick_hz;
    // KICK: plain, then dragged to a full drag (amt 1) over 0.25 s from 30 ms on, no hold
    float tp = hitg(DV_KICK, 120, 0); run(1.6f);
    float td = hitg(DV_KICK, 120, 11); drag_play(11, 0.03f, 1.0f, 0.25f, 0.0f, 0.0f, 0.0f, 1.6f);
    float pp = m_pitch_at(tp + 0.4f, tp + 0.6f, 20.0f, 200.0f), pd = m_pitch_at(td + 0.4f, td + 0.6f, 20.0f, 200.0f);
    float dp = dbr(m_rms(tp + 0.9f, tp + 1.0f), m_rms(tp + 0.1f, tp + 0.15f)), dd = dbr(m_rms(td + 0.9f, td + 1.0f), m_rms(td + 0.1f, td + 0.15f));
    check(pp > 0.0f && pd > 0.0f && fabsf(12.0f * log2f(pp / pd) - 12.0f) < 0.6f, "KICK dragged in full: %.1f Hz at 0.4..0.6 s against %.1f plain (%.2f semitones down, the note %.1f)", (double)pd, (double)pp, (double)(12.0f * log2f(pp / pd)), (double)kz);
    check(dd - dp > 20.0f, "and its body stretched: %.1f dB from 0.1 to 0.9 s, against %.1f plain", (double)dd, (double)dp);
    quiet_down();
    // KICK held (hold 1 from 20 ms, no amt): the sub stays; let go at 1.6 s, it dies
    float th = hitg(DV_KICK, 120, 12); drag_play(12, 0.02f, 0.0f, 0.1f, 1.0f, 0.0f, 0.0f, 1.6f);
    TD.hold = 0.0f; run(1.2f);
    float h0 = m_rms(th + 0.1f, th + 0.15f), h1 = m_rms(th + 1.45f, th + 1.55f), h2 = m_rms(th + 2.6f, th + 2.7f);
    check(dbr(h1, h0) > -9.0f && dbr(h2, h0) < -30.0f, "KICK held: %.1f dB at 1.5 s (plain: %.1f dB at 0.9 s); let go, %.1f dB a second later", (double)dbr(h1, h0), (double)dp, (double)dbr(h2, h0));
    quiet_down();
    // HAT: plain against opened (amt 1 from 10 ms) and held (hold 1 from 20 ms)
    float ta = hitg(DV_HAT, 120, 0); run(0.6f);
    float tb = hitg(DV_HAT, 120, 13); drag_play(13, 0.01f, 1.0f, 0.03f, 0.0f, 0.0f, 0.0f, 0.6f);
    quiet_down();
    float tc = hitg(DV_HAT, 120, 14); drag_play(14, 0.02f, 0.0f, 0.1f, 1.0f, 0.0f, 0.0f, 0.6f);
    quiet_down();
    float a0 = m_rms(ta + 0.005f, ta + 0.02f), la = dbr(m_rms(ta + 0.25f, ta + 0.3f), a0);
    float lb = dbr(m_rms(tb + 0.25f, tb + 0.3f), m_rms(tb + 0.005f, tb + 0.02f)), lc = dbr(m_rms(tc + 0.25f, tc + 0.3f), m_rms(tc + 0.005f, tc + 0.02f));
    check(lb > la + 30.0f && lc > la + 30.0f && lc > -30.0f, "HAT: at 0.25 s %.0f dB plain, %.0f dB opened (amt 1), %.0f dB held", (double)la, (double)lb, (double)lc);
    // SNAP: the tail hangs on (held), and slides lower with amt
    float sp = hitg(DV_SNAP, 120, 0); run(0.6f);
    float s0 = hitg(DV_SNAP, 120, 15); drag_play(15, 0.015f, 0.0f, 0.1f, 1.0f, 0.0f, 0.0f, 0.6f);
    quiet_down();
    float s1 = hitg(DV_SNAP, 120, 16); drag_play(16, 0.015f, 1.0f, 0.15f, 1.0f, 0.0f, 0.0f, 0.6f);
    quiet_down();
    float lsp = dbr(m_rms(sp + 0.2f, sp + 0.3f), m_rms(sp, sp + 0.02f)), ls0 = dbr(m_rms(s0 + 0.2f, s0 + 0.3f), m_rms(s0, s0 + 0.02f));
    float cs0 = centroid(s0 + 0.25f, s0 + 0.45f), cs1 = centroid(s1 + 0.25f, s1 + 0.45f);
    check(ls0 > lsp + 30.0f && ls0 > -24.0f, "SNAP held: its tail %.0f dB at 0.2..0.3 s (plain %.0f dB)", (double)ls0, (double)lsp);
    check(cs1 < 0.6f * cs0, "SNAP dragged in full: its band slid down, centroid %.0f Hz against %.0f held alone", (double)cs1, (double)cs0);
    // CLANG: held rings on and darkens with amt; the bend bends it
    float c0 = hitg(DV_CLANG, 120, 0); run(1.6f);
    float c1 = hitg(DV_CLANG, 120, 17); drag_play(17, 0.02f, 0.0f, 0.1f, 1.0f, 0.0f, 0.0f, 1.6f);
    quiet_down();
    float c2 = hitg(DV_CLANG, 120, 18); drag_play(18, 0.02f, 1.0f, 0.3f, 1.0f, 0.0f, 0.0f, 1.6f);
    quiet_down();
    float c3 = hitg(DV_CLANG, 120, 19); drag_play(19, 0.02f, 0.0f, 0.1f, 1.0f, 5.0f, 0.0f, 1.0f);
    td_none(); run(1.5f);
    float lc0 = dbr(m_rms(c0 + 1.3f, c0 + 1.5f), m_rms(c0 + 0.05f, c0 + 0.1f)), lc1 = dbr(m_rms(c1 + 1.3f, c1 + 1.5f), m_rms(c1 + 0.05f, c1 + 0.1f));
    float cc1 = centroid(c1 + 1.0f, c1 + 1.4f), cc2 = centroid(c2 + 1.0f, c2 + 1.4f);
    float fb = peak_fine(c3 + 0.5f, c3 + 0.9f, 415.0f * 1.2f, 415.0f * 1.45f), want = 415.0f * powf(2.0f, 5.0f / 12.0f);
    check(lc1 > lc0 + 6.0f && cc2 < 0.8f * cc1, "CLANG held: %.1f dB at 1.3 s (plain %.1f); dragged in full it darkens: centroid %.0f Hz against %.0f", (double)lc1, (double)lc0, (double)cc2, (double)cc1);
    check(fabsf(cents(fb, want)) < 10.0f, "CLANG bent 5 semitones: its lowest mode at %.1f Hz (%.1f wanted, %.1f cents)", (double)fb, (double)want, (double)cents(fb, want));
    // GLASS held, bent
    float g0 = hitg(DV_GLASS, 120, 0); run(1.5f);
    float g1 = hitg(DV_GLASS, 120, 20); drag_play(20, 0.02f, 0.0f, 0.1f, 1.0f, -7.0f, 0.0f, 1.5f);
    quiet_down();
    float lg0 = dbr(m_rms(g0 + 1.2f, g0 + 1.4f), m_rms(g0 + 0.05f, g0 + 0.1f)), lg1 = dbr(m_rms(g1 + 1.2f, g1 + 1.4f), m_rms(g1 + 0.05f, g1 + 0.1f));
    float gf = peak_fine(g1 + 0.6f, g1 + 1.0f, 900.0f, 1300.0f), gw = 1568.0f * powf(2.0f, -7.0f / 12.0f);
    check(lg1 > lg0 + 10.0f && fabsf(cents(gf, gw)) < 10.0f, "GLASS held: %.1f dB at 1.2 s (plain %.1f); bent -7: it rings at %.1f Hz (%.1f wanted)", (double)lg1, (double)lg0, (double)gf, (double)gw);
    // SUB: falls further, held
    float u0 = hitg(DV_SUB, 120, 0); run(1.2f);
    float u1 = hitg(DV_SUB, 120, 21); drag_play(21, 0.02f, 1.0f, 0.3f, 1.0f, 0.0f, 0.0f, 1.2f);
    quiet_down();
    float q0 = m_pitch_at(u0 + 0.7f, u0 + 0.95f, 15.0f, 200.0f), q1 = m_pitch_at(u1 + 0.7f, u1 + 0.95f, 15.0f, 200.0f);
    check(q0 > 0.0f && q1 > 0.0f && log2f(q0 / q1) > 0.5f, "SUB dragged in full: %.1f Hz at 0.7..0.95 s against %.1f plain (%.2f octaves further)", (double)q1, (double)q0, (double)log2f(q0 / (q1 + 1e-9f)));
    // WHIP: a second sweep, rising
    float w0 = hitg(DV_WHIP, 120, 0); run(0.8f);
    float w1 = hitg(DV_WHIP, 120, 22); drag_play(22, 0.03f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.8f);
    quiet_down();
    float lw0 = m_rms(w0 + 0.15f, w0 + 0.55f), lw1 = m_rms(w1 + 0.15f, w1 + 0.55f);
    float cw1 = centroid(w1 + 0.1f, w1 + 0.2f), cw2 = centroid(w1 + 0.45f, w1 + 0.55f);
    check(dbr(lw1, lw0) > 30.0f && cw2 > 2.0f * cw1, "WHIP dragged: a second sweep %.0f dB over the plain whip's silence, rising: centroid %.0f Hz, then %.0f Hz", (double)dbr(lw1, lw0), (double)cw1, (double)cw2);
    // CREAK: keeps creaking while held; amt: the slips faster
    float k0 = hitg(DV_CREAK, 120, 0); run(1.6f);
    float k1 = hitg(DV_CREAK, 120, 23); drag_play(23, 0.02f, 0.0f, 0.1f, 1.0f, 0.0f, 0.0f, 1.6f);
    quiet_down();
    float k2 = hitg(DV_CREAK, 120, 24); drag_play(24, 0.02f, 1.0f, 0.1f, 1.0f, 0.0f, 0.0f, 1.6f);
    quiet_down();
    float lk0 = dbr(m_rms(k0 + 1.3f, k0 + 1.5f), m_rms(k0 + 0.05f, k0 + 0.3f)), lk1 = dbr(m_rms(k1 + 1.3f, k1 + 1.5f), m_rms(k1 + 0.05f, k1 + 0.3f));
    float r1, r2; int n1, n2;
    slip_cv(k1 + 0.6f, k1 + 1.4f, &r1, &n1); slip_cv(k2 + 0.6f, k2 + 1.4f, &r2, &n2);
    check(lk1 > lk0 + 20.0f && lk1 > -15.0f, "CREAK held: %.1f dB at 1.3 s (plain %.1f)", (double)lk1, (double)lk0);
    check(r2 > 2.0f * r1, "CREAK dragged in full: %.0f slips a second against %.0f held alone", (double)r2, (double)r1);
    // no clicks: every drum struck, then at 60 ms the drag jumps (amt 1, hold 1, bend +5), at 0.3 s back to 0, and at
    // 0.45 s the drag moves to another gesture
    int bad = 0; float worst = 0;
    for (int v = 0; v < DV_OFF; v++) {
        float t0 = hitg(v, 110, 30 + v);
        TD.gid = 30 + v; run(0.06f);
        float j1 = sim_t; TD.amt = 1.0f; TD.hold = 1.0f; TD.bend = 5.0f; run(0.24f);
        float j2 = sim_t; TD.amt = 0.0f; TD.hold = 0.0f; TD.bend = 0.0f; run(0.15f);
        float j3 = sim_t; TD.gid = 99; run(0.3f);
        float w = fmaxf(fmaxf(m_click(j1, NULL, NULL), m_click(j2, NULL, NULL)), m_click(j3, NULL, NULL));
        if (w > worst) worst = w;
        if (w > 3.0f) { bad++; note("%s: a jump in the drag %.1fx", dv_name[v], (double)w); }
        (void)t0;
        quiet_down();
    }
    check(bad == 0, "the drag jumping end to end on every drum, and moving to another gesture: no click (worst %.2fx)", (double)worst);
    // the material's own strike (PAINT): held, bent
    set("role", ROLE_PAINT); set("matl", MAT_METAL); set("chrd", CH_ONE); set("ring", 1.0f); run(0.6f);
    float f0 = eng_mat_hz;
    float m0 = sim_t; eng_trigger(&(trig_t){ .pad = PD_LEFT, .vel = 120, .src = SRC_LIVE, .gid = 40 }); run(2.0f);
    float m1 = sim_t; eng_trigger(&(trig_t){ .pad = PD_LEFT, .vel = 120, .src = SRC_LIVE, .gid = 41 });
    drag_play(41, 0.02f, 0.0f, 0.1f, 1.0f, 3.0f, 0.0f, 2.0f);
    float jm = sim_t; td_none(); run(1.0f);
    float lm0 = dbr(m_rms(m0 + 1.5f, m0 + 1.7f), m_rms(m0 + 0.05f, m0 + 0.1f)), lm1 = dbr(m_rms(m1 + 1.5f, m1 + 1.7f), m_rms(m1 + 0.05f, m1 + 0.1f));
    float fm = peak_fine(m1 + 0.8f, m1 + 1.4f, f0 * 1.05f, f0 * 1.3f), fw = f0 * powf(2.0f, 3.0f / 12.0f);
    check(lm1 > lm0 + 20.0f && fabsf(cents(fm, fw)) < 10.0f, "PAINT: its own strike held rings on (%.1f dB at 1.5 s, %.1f not held) and bent 3: %.1f Hz (%.1f wanted)", (double)lm1, (double)lm0, (double)fm, (double)fw);
    float fz = peak_fine(jm + 0.5f, jm + 0.9f, f0 * 0.9f, f0 * 1.1f);
    check(fabsf(cents(fz, f0)) < 10.0f && m_click(jm, NULL, NULL) <= 3.0f, "the drag let go: the material back on its note (%.1f Hz, the note %.1f), no click", (double)fz, (double)f0);
    set("ring", 2.5f); set("role", ROLE_DRUM); td_none(); run(1.5f);
}

// ---------------------------------------------------------------- the notes
static void nev(int on, int note, int vel, int gid, int legato, uint32_t at)
{
    nev_t e = { .at = at, .on = (uint8_t)on, .note = (uint8_t)note, .vel = (uint8_t)vel, .gid = (uint8_t)gid, .legato = (uint8_t)legato };
    eng_note(&e);
}
static void voice(int nv) { set("nvoc", (float)nv); TD.nv = nv; }
static void glide(float s) { set("glid", s); TD.glide = s; }
static float midi_f(int n) { return 440.0f * powf(2.0f, (float)(n - 69) / 12.0f); }

static void notes_tests(void)
{
    section("engine: the notes");
    still(); defaults(); set("levl", 100); set("role", ROLE_NOTES); set("drag", 100); TD.depth = 1.0f; td_none(); glide(0.06f); run(0.5f);
    static const int tn[NNV] = { 45, 45, 69, 76 };           // A2, A2, A4, E5
    int gid = 50;
    for (int v = 0; v < NNV; v++) {
        voice(v); run(0.05f);
        float t0 = sim_t, want = midi_f(tn[v]);
        nev(1, tn[v], 110, ++gid, 0, 0); run(0.9f);
        nev(0, tn[v], 0, gid, 0, 0); run(1.2f);
        float got = peak_fine(t0 + (v == NV_BELL ? 0.35f : 0.3f), t0 + 0.85f, want * 0.97f, want * 1.03f);
        float pk = m_peak(t0, t0 + 1.2f), lv = eng_note_lvl;
        bool ok = fabsf(cents(got, want)) < 5.0f && pk > 0.063f && pk < 0.794f && m_bad() == 0;
        check(ok, "%s: note %d at %.2f Hz (%.2f wanted, %+.1f cents), peak %.1f dBFS on the default bus", nv_name[v], tn[v], (double)got, (double)want, (double)cents(got, want), (double)dbr(pk, 1.0f));
        metric(nv_name[v], dbr(pk, 1.0f));
        (void)lv;
    }
    // eng_note_lvl reports
    voice(NV_BASS); nev(1, 40, 110, ++gid, 0, 0); run(0.2f);
    float lvl = eng_note_lvl;
    nev(0, 40, 0, gid, 0, 0); run(1.0f);
    check(lvl > 0.2f && eng_note_lvl < 0.05f, "eng_note_lvl: %.2f while a note sounds, %.3f a second after it", (double)lvl, (double)eng_note_lvl);
    // a note let go: gone within 0.5 s; held by the drag's hold past its end, it sustains, and goes when the hold falls
    // (on a clean bus: the echo would be heard as the note)
    clean(); set("role", ROLE_NOTES); run(0.2f);
    for (int v = 0; v < 3; v++) {
        voice(v); run(0.05f);
        float t0 = sim_t; nev(1, 57, 110, ++gid, 0, 0); run(0.2f);
        nev(0, 57, 0, gid, 0, 0); run(0.8f);
        float t1 = sim_t; int g = ++gid;
        nev(1, 57, 110, g, 0, 0); TD.gid = g; run(0.05f); TD.hold = 1.0f; run(0.15f);
        nev(0, 57, 0, g, 0, 0); run(0.8f);
        float tl = sim_t; TD.hold = 0.0f; run(1.0f);
        td_none();
        float r0 = m_rms(t0 + 0.1f, t0 + 0.18f), rg = dbr(m_rms(t0 + 0.7f, t0 + 0.8f), r0);
        float r1 = m_rms(t1 + 0.1f, t1 + 0.18f), rs = dbr(m_rms(t1 + 0.85f, t1 + 0.95f), r1), re = dbr(m_rms(tl + 0.7f, tl + 0.8f), r1);
        check(rg < -40.0f && rs > -6.0f && re < -40.0f, "%s: let go at 0.2 s, %.0f dB 0.5 s on; held by the hold, %.1f dB 0.75 s after its end; the hold falls: %.0f dB 0.7 s on", nv_name[v], (double)rg, (double)rs, (double)re);
    }
    // legato: SQUELCH slides from A2 to E3 over GLIDE 0.12 s, without a new attack
    voice(NV_SQUELCH); glide(0.12f); run(0.05f);
    float ts = sim_t; int g = ++gid;
    nev(1, 45, 100, g, 0, 0); run(0.5f);
    float tg = sim_t; nev(1, 52, 100, g, 1, 0); run(0.5f);
    nev(0, 52, 0, g, 0, 0); run(0.5f);
    float pa = m_pitch_at(tg - 0.1f, tg - 0.01f, 60.0f, 400.0f), pm = m_pitch_at(tg + 0.05f, tg + 0.07f, 60.0f, 400.0f);
    float pb = m_pitch_at(tg + 0.13f, tg + 0.17f, 60.0f, 400.0f), pz = m_pitch_at(tg + 0.25f, tg + 0.45f, 60.0f, 400.0f);
    float fA = midi_f(45), fE = midi_f(52);
    check(fabsf(cents(pa, fA)) < 15.0f && pm > fA * 1.1f && pm < fE * 0.95f && fabsf(cents(pb, fE)) < 15.0f && fabsf(cents(pz, fE)) < 10.0f,
          "SQUELCH legato over GLIDE 0.12 s: %.1f Hz before, %.1f halfway, %.1f at 0.13..0.17 s, %.1f after (A2 %.1f, E3 %.1f)", (double)pa, (double)pm, (double)pb, (double)pz, (double)fA, (double)fE);
    // no new attack: against the same move made as a new note (legato 0), the slide's level and brightness hardly move
    float before = m_peak(tg - 0.15f, tg), after = m_peak(tg, tg + 0.1f), cb = centroid(tg - 0.08f, tg - 0.01f), ca = centroid(tg + 0.01f, tg + 0.08f);
    g = ++gid; run(0.3f);
    float tr = sim_t; nev(1, 45, 100, g, 0, 0); run(0.5f);
    float tq = sim_t; nev(1, 52, 100, ++gid, 0, 0); run(0.5f);
    nev(0, 52, 0, gid, 0, 0); run(0.5f);
    float rb_ = m_peak(tq - 0.15f, tq), ra_ = m_peak(tq, tq + 0.1f), qb = centroid(tq - 0.08f, tq - 0.01f), qa = centroid(tq + 0.01f, tq + 0.08f);
    check(dbr(after, before) < 2.0f && ca / cb < 0.7f * (qa / qb), "no new attack on the slide: its peak %+.1f dB, its centroid x%.2f (the same move as a new note: %+.1f dB, x%.2f)",
          (double)dbr(after, before), (double)(ca / cb), (double)dbr(ra_, rb_), (double)(qa / qb));
    (void)ts; (void)tr;
    // a slide from the last note: a legato note of a new gesture while one sounds (a 303's slide) glides on, no attack
    g = ++gid; nev(1, 45, 100, g, 0, 0); run(0.4f);
    float tz = sim_t; nev(1, 50, 100, ++gid, 1, 0); run(0.4f);
    nev(0, 45, 0, g, 0, 0); run(0.1f);                          // the old gesture letting go does not stop it
    float lz = m_rms(sim_t - 0.05f, sim_t);
    nev(0, 50, 0, gid, 0, 0); run(0.4f);
    float pzz = m_pitch_at(tz + 0.2f, tz + 0.35f, 60.0f, 400.0f), bz = m_peak(tz - 0.15f, tz), az = m_peak(tz, tz + 0.1f);
    check(fabsf(cents(pzz, midi_f(50))) < 10.0f && dbr(az, bz) < 2.0f && lz > 0.05f, "SQUELCH slides from another gesture's note: %.1f Hz (D3 %.1f), its peak %+.1f dB, on after the old note's end (rms %.3f)",
          (double)pzz, (double)midi_f(50), (double)dbr(az, bz), (double)lz);
    // a sounding note: the drag opens the filter and bends it, no click; a new note (not legato) takes over, no click
    voice(NV_BASS); glide(0.06f); run(0.05f);
    g = ++gid; nev(1, 40, 100, g, 0, 0); TD.gid = g; run(0.5f);
    float j1 = sim_t; TD.amt = 1.0f; TD.hold = 1.0f; TD.bend = 5.0f; run(0.3f);
    float j2 = sim_t; TD.amt = 0.0f; TD.bend = 0.0f; run(0.3f);
    float cb0 = centroid(j1 - 0.2f, j1), cb1 = centroid(j1 + 0.15f, j1 + 0.3f);
    float j3 = sim_t; nev(1, 43, 100, ++gid, 0, 0); run(0.4f);
    float j4 = sim_t; voice(NV_LEAD); nev(1, 55, 100, ++gid, 0, 0); run(0.4f);
    nev(0, 55, 0, gid, 0, 0); td_none(); run(0.6f);
    float wk = fmaxf(fmaxf(m_click(j1, NULL, NULL), m_click(j2, NULL, NULL)), fmaxf(m_click(j3, NULL, NULL), m_click(j4, NULL, NULL)));
    check(cb1 > 1.3f * cb0 && wk <= 3.0f, "BASS dragged: its filter opens (centroid %.0f Hz against %.0f), the drag's jumps, a new note and another voice taking over: no click (worst %.2fx)", (double)cb1, (double)cb0, (double)wk);
    // BELL: four at once, then a fifth (the quietest gives way)
    voice(NV_BELL); run(0.05f);
    static const int ch[5] = { 72, 76, 79, 83, 86 };
    float tb = sim_t;
    for (int k = 0; k < 4; k++) nev(1, ch[k], 100, ++gid, 0, 0);
    run(0.7f);
    float mn = 1e9f, mx = 0;
    for (int k = 0; k < 4; k++) { float a = m_goertzel(tb + 0.3f, tb + 0.6f, midi_f(ch[k])); mn = fminf(mn, a); mx = fmaxf(mx, a); }
    float t5 = sim_t; nev(1, ch[4], 100, ++gid, 0, 0); run(1.0f);
    float a5 = m_goertzel(t5 + 0.2f, t5 + 0.5f, midi_f(ch[4])), pkb = m_peak(tb, tb + 1.7f);
    check(10.0f * log10f(mx / (mn + 1e-20f)) < 15.0f && a5 > 0.1f * mx && m_click(t5, NULL, NULL) <= 3.0f && pkb < 0.794f,
          "BELL: four ring at once (within %.1f dB of each other), a fifth sounds (%.0f dB under the loudest's earlier level) without a click; peak %.1f dBFS",
          (double)(10.0f * log10f(mx / (mn + 1e-20f))), (double)(10.0f * log10f(mx / (a5 + 1e-20f))), (double)dbr(pkb, 1.0f));
    for (int k = 0; k <= gid; k++) nev(0, 0, 0, k, 0, 0);
    run(2.0f);
    // notes sound in the other roles too (here DRUM and PAINT), not in SHAPE (below)
    float nr[2];
    voice(NV_BASS);
    for (int r = 0; r < 2; r++) {
        set("role", r ? ROLE_PAINT : ROLE_DRUM); run(0.3f);
        float t0 = sim_t; nev(1, 45, 100, ++gid, 0, 0); run(0.3f); nev(0, 45, 0, gid, 0, 0); run(0.5f);
        nr[r] = m_rms(t0 + 0.05f, t0 + 0.25f);
    }
    check(nr[0] > 0.02f && nr[1] > 0.02f, "a note sounds in DRUM (rms %.3f) and in PAINT (%.3f)", (double)nr[0], (double)nr[1]);
    check(m_bad() == 0, "the notes: no NaN or inf");
    set("role", ROLE_DRUM); td_none(); run(0.5f);
}

// ---------------------------------------------------------------- SHAPE: silent but for the tails
static void shape_tests(void)
{
    section("engine: SHAPE");
    clean(); td_none(); voice(NV_BASS); run(0.2f);
    hitg(DV_CLANG, 110, 60); nev(1, 45, 100, 61, 0, 0); run(0.3f);
    float t0 = sim_t; set("role", ROLE_SHAPE); run(0.3f);
    float k = m_click(t0, NULL, NULL);
    eng_trigger(&(trig_t){ .pad = PD_LEFT, .vel = 120, .src = SRC_LIVE, .gid = 62 }); eng_strike(0, 120, DV_KICK);
    nev(1, 50, 120, 63, 0, 0);
    float t1 = sim_t; run(0.5f);
    // the role reaches the engine with the next tick (10 ms) and fades over 20 ms; what is left from 45 ms on is the
    // master's DC blocker settling (as after CUT): no sound, its steps tiny
    float dmax = 0, dref = 0;
    for (int i = ix(t0 + 0.045f) + 1; i < ix(t1 + 0.5f); i++) dmax = fmaxf(dmax, fabsf(sim_out[i] - sim_out[i - 1]));
    for (int i = ix(t0 - 0.2f) + 1; i < ix(t0); i++) dref = fmaxf(dref, fabsf(sim_out[i] - sim_out[i - 1]));
    float r2 = m_rms(t1 + 0.05f, t1 + 0.5f);
    check(dbr(dmax, dref) < -60.0f && r2 < 1e-4f && k <= 3.0f, "SHAPE: what sounded fades out without a click (%.2f), then no sound (its largest step %.0f dB under before), and a hit, a strike and a note do nothing (rms %.6f)", (double)k, (double)dbr(dmax, dref), (double)r2);
    // with the echo and the room: only their tails, falling
    set("role", ROLE_DRUM); still(); defaults(); set("emix", 40); set("efb ", 60); set("room", 50); run(0.3f);
    hitg(DV_SNAP, 120, 64); nev(1, 52, 110, 65, 0, 0); run(0.2f);
    nev(0, 52, 0, 65, 0, 0);
    float t2 = sim_t; set("role", ROLE_SHAPE); run(0.1f);
    for (int i = 0; i < 6; i++) { eng_trigger(&(trig_t){ .pad = PD_LEFT, .vel = 120, .src = SRC_LIVE, .gid = 66 }); nev(1, 50, 120, 67, 0, 0); run(0.3f); }
    float e1 = m_rms(t2 + 0.1f, t2 + 0.4f), e2 = m_rms(t2 + 0.7f, t2 + 1.0f), e3 = m_rms(t2 + 1.4f, t2 + 1.9f);
    check(e1 > 1e-4f && e2 < e1 && e3 < 0.7f * e2, "SHAPE with echo and room: their tails ring out and fall (rms %.4f, %.4f, %.4f)", (double)e1, (double)e2, (double)e3);
    nev(0, 50, 0, 67, 0, 0);
    set("role", ROLE_DRUM); defaults(); run(2.0f);
}

// ---------------------------------------------------------------- a loop's NOTES hits are the UI's, not the engine's
static void note_hit_loop(void)
{
    section("engine: a loop holding NOTES hits");
    clean(); td_none(); run(0.2f);
    loop_t *l = &eng_loops[NLOOP - 1];
    int keep_bars = l->bars, keep_on = l->on, keep_n = l->n;
    hit_t keep[3]; memcpy(keep, l->hits, sizeof keep);
    grid_t g; eng_get_grid(&g);
    int64_t now = eng_tick_at(&g, audio_frames());
    l->on = 0; l->bars = 0; l->n = 0;
    l->hits[0] = (hit_t){ .tick = PPB / 2, .pad = NOTE_HIT + 60, .vel = 100, .born = -1000, .len = PPB };
    l->hits[1] = (hit_t){ .tick = PPB, .pad = 0xFF, .vel = 100, .born = -1000, .len = 0 };
    l->hits[2] = (hit_t){ .tick = 2 * PPB, .pad = NOTE_HIT + 127, .vel = 127, .born = -1000, .len = 0 };
    l->start = eng_floor_div(now, BAR_TICKS) * BAR_TICKS;
    l->n = 3; l->bars = 1; l->on = 1; eng_run = 1;
    int m0 = midi_n, von = 0;
    for (int i = 0; i < 450; i++) { tick(); if (eng_voices_on > von) von = eng_voices_on; }
    int drums = 0;
    for (int i = m0; i < midi_n; i++) if (midi_log[i].st == 0x99 && midi_log[i].d2 > 0) drums++;
    check(drums == 0 && von == 0 && m_bad() == 0, "two bars of a loop holding only NOTES hits (and a pad 255): no drum played, nothing reported (%d), no voice (%d), no NaN", drums, von);
    l->on = 0; l->n = 0; memcpy(l->hits, keep, sizeof keep); l->bars = keep_bars; l->n = keep_n; l->on = keep_on;
    run(0.5f);
}

// ---------------------------------------------------------------- the cost: the busiest block in each role
// Each role's busiest case, through a heavy bus (DRIVE 60, ECHO MIX 30 with FEEDBACK 50, ROOM 40 at SIZE 70), each
// measured three times (the host has other work: the least of the three is the figure), the budget 15 us a block:
//   DRUM   twelve drums at DECAY 4 sounding at once, three of them one gesture's, dragged (the amount rising, held,
//          the bend swinging, so their modes are retuned every block)
//   NOTES  a mono note (each of BASS, SQUELCH, LEAD) sliding every 0.1 s and four bells ringing, all one gesture's,
//          dragged the same way
//   PAINT  the material (TRIAD: four notes of eight modes) bowed as PAINT bows it (0.5) and creaking (1.0), struck by
//          this hand every 0.1 s and the other hand every 0.25 s, the newest strike dragged (held, the bend swinging)
// 0.1 measured one case that took the roles together: ten drums ringing as the box turned to PUMP, the TRIAD bowed and
// struck. It is measured still (the role switch, "turning to PUMP"), but a box in DRUM has no material sounding and
// in PAINT or PUMP a hit strikes the material instead of a drum, so ten drums and the bowed TRIAD only meet for the
// few seconds the kit's tails last after the switch.
// Two figures for each: the least of the three runs' means (as 0.1 measured it), and each tick's least time over the
// three runs, summed (the runs play the same ticks: a tick the host took the thread away from in one run is counted
// as the other runs found it). The checks use the second when the host is loaded (its load over 4), the first otherwise.
static void heavy_bus(void) { set("drve", 60); set("emix", 30); set("efb ", 50); set("room", 40); set("rsiz", 70); }
enum { CC_DRUM, CC_BASS, CC_SQUELCH, CC_LEAD, CC_PAINT, CC_SWITCH, NCC };
static double cc_t[NCC][3][128]; static int cc_n[NCC], cc_case = -1, cc_run;
static void cost_start_case(int which) { eng_audio_ns = 0; eng_audio_max_ns = 0; eng_audio_blocks = 0; rec_n = 0; rec_last = 0; rec_on = true; cc_case = which; }
static void cost_start(void) { cost_start_case(-1); }
static double cost_us(void)
{
    rec_on = false;
    if (cc_case >= 0) { memcpy(cc_t[cc_case][cc_run], rec_t, sizeof rec_t); cc_n[cc_case] = rec_n; }
    return eng_audio_blocks ? eng_audio_ns / (double)eng_audio_blocks / 1000.0 : 0.0;
}
static double cc_least_ticks(int c)                     // us a block: each tick's least over the runs, summed
{
    double sum = 0;
    for (int k = 0; k < cc_n[c]; k++) { double m = cc_t[c][0][k]; for (int r = 1; r < 3; r++) if (cc_t[c][r][k] < m) m = cc_t[c][r][k]; sum += m; }
    return cc_n[c] ? sum / (double)(cc_n[c] * (TICK_FRAMES / AUDIO_BLOCK)) / 1000.0 : 0.0;
}
static void drag_swing(int g, float t) { TD.gid = g; TD.amt = fminf(t * 2.0f, 1.0f); TD.hold = 1.0f; TD.bend = 5.0f * sinf(6.2831853f * 1.5f * t); }
static double cost_drum(int *v0, int *v1)
{
    still(); defaults(); td_none(); heavy_bus(); set("decy", 4); set("sqez", 60); run(0.3f);
    static const int kit[12] = { DV_CLANG, DV_CREAK, DV_CLANG, DV_GLASS, DV_CREAK, DV_KICK, DV_SNAP, DV_GLASS, DV_SUB, DV_HAT, DV_WHIP, DV_CLANG };
    set("role", ROLE_DRUM);
    for (int i = 0; i < 12; i++) {
        set("pfrt", (float)kit[i]); set("tune", (float)(i - 6)); tick();
        eng_trigger(&(trig_t){ .pad = PD_FRONT, .vel = 120, .src = SRC_LIVE, .gid = (uint8_t)(i >= 9 ? 90 : 70 + i) });
    }
    tick(); tick();
    *v0 = eng_voices_on;
    cost_start_case(CC_DRUM);
    for (int i = 0; i < 50; i++) { drag_swing(90, (float)i * 0.01f); tick(); }
    double us = cost_us();
    *v1 = eng_voices_on;
    quiet_down(); defaults(); still();
    return us;
}
static double cost_notes(int mono, int *n0)
{
    still(); defaults(); td_none(); heavy_bus(); set("role", ROLE_NOTES); glide(0.08f); run(0.3f);
    voice(NV_BELL); tick();
    static const int ch[4] = { 72, 76, 79, 84 };
    for (int k = 0; k < 4; k++) nev(1, ch[k], 110, 91, 0, 0);
    voice(mono); tick();
    nev(1, 45, 120, 91, 0, 0); tick(); tick();
    cost_start_case(CC_BASS + mono);
    *n0 = 0;
    for (int i = 0; i < 50; i++) {
        if (i % 10 == 9) nev(1, (i / 10) & 1 ? 45 : 52, 110, 91, 1, 0);
        drag_swing(91, (float)i * 0.01f); tick();
    }
    double us = cost_us();
    nev(0, 0, 0, 91, 0, 0);
    *n0 = eng_note_lvl > 0.05f;
    quiet_down(); defaults(); still(); voice(NV_BASS);
    return us;
}
static double cost_paint(float *lvl)
{
    still(); defaults(); td_none(); heavy_bus();
    set("role", ROLE_PAINT); set("matl", MAT_METAL); set("chrd", CH_TRIAD); set("crek", 100); set("ring", 4); run(0.3f);
    TD.bow = 0.5f; TD.creak = 1.0f; run(0.5f);
    cost_start_case(CC_PAINT);
    int g = 100;
    for (int i = 0; i < 60; i++) {
        if (i % 10 == 0) { g++; eng_trigger(&(trig_t){ .pad = (uint8_t)(i / 10 & 3), .vel = 110, .src = SRC_LIVE, .gid = (uint8_t)g }); }
        if (i % 25 == 0) strike(DV_KICK, 120);
        drag_swing(g, (float)(i % 10) * 0.01f + 0.1f); tick();
    }
    double us = cost_us();
    *lvl = eng_mat_lvl;
    TD.bow = TD.creak = -1.0f;
    set("role", ROLE_DRUM); quiet_down(); defaults(); still();
    return us;
}
// 0.1's case: ten drums at DECAY 4 ringing as the box turns to PUMP, the TRIAD bowed and creaking, struck
static double cost_switch(int *v0)
{
    still(); defaults(); td_none(); heavy_bus();
    set("decy", 4); set("chrd", CH_TRIAD); set("crek", 100); run(0.3f);
    static const int kit[10] = { DV_CLANG, DV_CREAK, DV_CLANG, DV_GLASS, DV_CREAK, DV_CLANG, DV_KICK, DV_CREAK, DV_GLASS, DV_CLANG };
    set("role", ROLE_DRUM);
    for (int i = 0; i < 10; i++) { set("pfrt", (float)kit[i]); set("tune", (float)(i - 5)); tick(); tick(); eng_trigger(&(trig_t){ .pad = PD_FRONT, .vel = 120, .src = SRC_LIVE }); }
    set("role", ROLE_PUMP); TD.bow = 1.0f; TD.creak = 1.0f; run(0.15f);
    *v0 = eng_voices_on;
    cost_start_case(CC_SWITCH);
    strike(DV_KICK, 120); run(0.15f); strike(DV_CREAK, 120); run(0.15f);
    double us = cost_us();
    TD.bow = TD.creak = -1.0f;
    set("role", ROLE_DRUM); quiet_down(); defaults(); still();
    return us;
}
static double least(double *r, int n) { double b = 1e9; for (int i = 0; i < n; i++) if (r[i] < b) b = r[i]; return b; }
static float host_load(void) { float l = -1.0f; FILE *f = fopen("/proc/loadavg", "r"); if (f) { if (fscanf(f, "%f", &l) != 1) l = -1.0f; fclose(f); } return l; }
static void cost(void)
{
    section("engine: the busiest block in each role");
    float load0 = host_load();
    double d[3], nb[3][3], p[3], w[3];
    int v0 = 99, v1 = 99, nn = 1, sw = 99; float ml = 1.0f;
    for (int r = 0; r < 3; r++) {
        int a, b, c;
        cc_run = r;
        d[r] = cost_drum(&a, &b); if (a < v0) v0 = a; if (b < v1) v1 = b;
        for (int m = 0; m < 3; m++) { nb[m][r] = cost_notes(m, &c); nn &= c; }
        float l; p[r] = cost_paint(&l); if (l < ml) ml = l;
        w[r] = cost_switch(&c); if (c < sw) sw = c;
    }
    double bd = least(d, 3), bp = least(p, 3), bw = least(w, 3), bn = 0; int wn = 0;
    float load1 = host_load(), load = fmaxf(load0, load1);
    note("the host's load (1 min) %.1f before, %.1f after (other work on the machine moves these figures)", (double)load0, (double)load1);
    metric("host_load_1min", load1);
    for (int m = 0; m < 3; m++) { double x = least(nb[m], 3); metric(nv_name[m], x); if (x > bn) { bn = x; wn = m; } }
    double td = cc_least_ticks(CC_DRUM), tp = cc_least_ticks(CC_PAINT), tw = cc_least_ticks(CC_SWITCH), tn = 0;
    for (int m = 0; m < 3; m++) tn = fmax(tn, cc_least_ticks(CC_BASS + m));
    const bool busy = load > 4.0f;
    const char *by = busy ? "each tick's least" : "the runs' means";
    check(v0 >= 12 && v1 >= 12 && nn && ml > 0.1f && sw >= 10, "the cases sound: %d then %d drums; the notes (all three mono voices); the material (level %.2f); %d drums at the switch", v0, v1, (double)ml, sw);
    check((busy ? td : bd) < 15.0, "DRUM busiest: %.2f us a block by the runs' means (%.2f %.2f %.2f), %.2f by each tick's least; checked: %s, the budget 15", bd, d[0], d[1], d[2], td, by);
    check((busy ? tn : bn) < 15.0, "NOTES busiest (%s and four bells): %.2f us a block by the runs' means, %.2f by each tick's least; checked: %s, the budget 15", nv_name[wn], bn, tn, by);
    check((busy ? tp : bp) < 15.0, "PAINT busiest: %.2f us a block by the runs' means (%.2f %.2f %.2f), %.2f by each tick's least; checked: %s, the budget 15", bp, p[0], p[1], p[2], tp, by);
    check((busy ? tw : bw) < 15.0, "0.1's case (drums ringing as the box turns to PUMP, the TRIAD bowed and struck): %.2f us a block by the runs' means (%.2f %.2f %.2f), %.2f by each tick's least; checked: %s, the budget 15", bw, w[0], w[1], w[2], tw, by);
    metric("engine_us_per_block_drum", bd);
    metric("engine_us_per_block_notes", bn);
    metric("engine_us_per_block_paint", bp);
    metric("engine_us_per_block_busiest", bw);
    metric("engine_us_per_block_drum_ticks", td);
    metric("engine_us_per_block_notes_ticks", tn);
    metric("engine_us_per_block_paint_ticks", tp);
    metric("engine_us_per_block_busiest_ticks", tw);
    // and a plain one: a beat of four drums through the default bus
    cost_start();
    for (int b = 0; b < 8; b++) { hit(b & 1 ? DV_SNAP : DV_KICK, 110); run(0.1f); hit(DV_HAT, 80); run(0.12f); }
    metric("engine_us_per_block_drumming", cost_us());
    run(1.0f);
}

// ---------------------------------------------------------------- sound files: the drag and the notes
// A sequence of note events and drags, played before each tick at their exact frames (at 120: a beat is 0.5 s)
enum { Q_ON, Q_OFF, Q_LEG, Q_DRAG };
typedef struct { float beat; int kind, note, vel, gid; float amt, hold, bend, secs; } qev_t;
static const qev_t *q_ev; static int q_n, q_i; static uint32_t q_f0; static float q_spb = 0.5f;
static float q_from_amt, q_to_amt, q_t0, q_secs = 0.0f;
static void q_step(void)
{
    uint32_t now = audio_frames(), until = now + TICK_FRAMES;
    while (q_i < q_n) {
        const qev_t *e = &q_ev[q_i];
        uint32_t at = q_f0 + (uint32_t)(e->beat * q_spb * SRF);
        if ((int32_t)(at - until) >= 0) break;
        switch (e->kind) {
        case Q_ON:   nev(1, e->note, e->vel, e->gid, 0, at); break;
        case Q_OFF:  nev(0, e->note, 0, e->gid, 0, at); break;
        case Q_LEG:  nev(1, e->note, e->vel ? e->vel : 100, e->gid, 1, at); break;
        case Q_DRAG: TD.gid = e->gid; TD.hold = e->hold; TD.bend = e->bend; q_from_amt = TD.amt; q_to_amt = e->amt; q_t0 = sim_t; q_secs = e->secs; break;
        }
        q_i++;
    }
    if (q_secs > 0.0f) {                                        // the drag's amount eased towards its target
        float u = clampf((sim_t - q_t0) / q_secs, 0.0f, 1.0f);
        TD.amt = q_from_amt + (q_to_amt - q_from_amt) * u * u * (3.0f - 2.0f * u);
    } else if (q_i > 0) TD.amt = q_to_amt;
}
static void q_play(const qev_t *ev, int n, int times, float beats, float tail)
{
    for (int r = 0; r < times; r++) {
        q_ev = ev; q_n = n; q_i = 0; q_f0 = audio_frames() + TICK_FRAMES; q_secs = 0.0f; q_to_amt = 0.0f;
        tick_fn = q_step;
        run(beats * q_spb);
    }
    tick_fn = NULL; td_none(); run(tail);
}
#define ON(b, n, v, g)    { b, Q_ON, n, v, g, 0, 0, 0, 0 }
#define OFF(b, g)         { b, Q_OFF, 0, 0, g, 0, 0, 0, 0 }
#define LEG(b, n, g)      { b, Q_LEG, n, 0, g, 0, 0, 0, 0 }
#define DRG(b, g, a, h, bd, s) { b, Q_DRAG, 0, 0, g, a, h, bd, s }
// a bassline in A minor, two bars: short notes, slides, and a last note dragged open
static const qev_t BASSLINE[] = {
    ON(0.00f, 33, 120, 1), OFF(0.45f, 1),
    ON(0.75f, 45, 90, 2), OFF(0.95f, 2),
    ON(1.00f, 40, 110, 3), LEG(1.50f, 43, 3), OFF(1.95f, 3),
    ON(2.00f, 45, 100, 4), OFF(2.20f, 4),
    ON(2.50f, 33, 120, 5), OFF(2.70f, 5),
    ON(2.75f, 48, 90, 6), OFF(2.95f, 6),
    ON(3.00f, 47, 110, 7), LEG(3.50f, 45, 7), OFF(3.90f, 7),
    ON(4.00f, 29, 120, 8), OFF(4.45f, 8),
    ON(4.75f, 41, 90, 9), OFF(4.95f, 9),
    ON(5.00f, 31, 110, 10), LEG(5.50f, 38, 10), OFF(5.90f, 10),
    ON(6.00f, 36, 100, 11), OFF(6.20f, 11),
    ON(6.50f, 40, 110, 12), OFF(6.70f, 12),
    ON(6.75f, 43, 90, 13), OFF(6.95f, 13),
    ON(7.00f, 33, 127, 14), DRG(7.05f, 14, 1.0f, 1.0f, 0.0f, 0.35f), OFF(7.20f, 14), DRG(7.90f, 14, 1.0f, 0.0f, 0.0f, 0.1f),
};
static const qev_t LEADLINE[] = {
    ON(0.0f, 69, 110, 21), DRG(0.1f, 21, 0.6f, 1.0f, 0.0f, 0.4f), LEG(1.0f, 72, 21), OFF(1.4f, 21), DRG(1.9f, 21, 0.6f, 0.0f, 0.0f, 0.1f),
    ON(2.0f, 71, 100, 22), OFF(2.4f, 22),
    ON(2.5f, 67, 100, 23), OFF(2.9f, 23),
    ON(3.0f, 69, 110, 24), OFF(3.9f, 24),
    ON(4.0f, 76, 115, 25), LEG(4.5f, 74, 25), OFF(4.9f, 25),
    ON(5.0f, 72, 100, 26), OFF(5.4f, 26),
    ON(5.5f, 71, 100, 27), OFF(5.9f, 27),
    ON(6.0f, 69, 120, 28), DRG(6.1f, 28, 1.0f, 1.0f, 3.0f, 1.2f), OFF(6.3f, 28), DRG(7.8f, 28, 1.0f, 0.0f, 3.0f, 0.1f),
};
static const qev_t BELLS[] = {
    ON(0.0f, 69, 100, 31), ON(0.5f, 72, 90, 32), ON(1.0f, 76, 95, 33), ON(1.5f, 81, 90, 34),
    ON(2.0f, 65, 100, 35), ON(2.5f, 69, 90, 36), ON(3.0f, 72, 95, 37), ON(3.5f, 77, 90, 38),
    ON(4.0f, 67, 100, 39), ON(4.5f, 71, 90, 40), ON(5.0f, 74, 95, 41), ON(5.5f, 79, 90, 42),
    ON(6.0f, 64, 110, 43), DRG(6.05f, 43, 0.8f, 1.0f, -2.0f, 1.0f), ON(6.5f, 76, 80, 44), ON(7.0f, 71, 80, 45),
    OFF(0.2f, 31), OFF(0.7f, 32), OFF(1.2f, 33), OFF(1.7f, 34), OFF(2.2f, 35), OFF(2.7f, 36), OFF(3.2f, 37), OFF(3.7f, 38),
    OFF(4.2f, 39), OFF(4.7f, 40), OFF(5.2f, 41), OFF(5.7f, 42), OFF(6.2f, 43), OFF(6.7f, 44), OFF(7.2f, 45), DRG(7.6f, 43, 0.8f, 0.0f, -2.0f, 0.1f),
};
static int q_sorted(qev_t *dst, const qev_t *src, int n, int transpose, int accent)   // in time order; notes moved, accents
{
    for (int i = 0; i < n; i++) {
        qev_t e = src[i];
        if (e.kind == Q_ON || e.kind == Q_LEG) e.note += transpose;
        if (accent && e.kind == Q_ON && e.vel >= 110) e.vel = 127;
        else if (accent && e.kind == Q_ON) e.vel = 80;
        int k = i;
        while (k > 0 && dst[k - 1].beat > e.beat) { dst[k] = dst[k - 1]; k--; }
        dst[k] = e;
    }
    return n;
}
static void drag_wav(int v, const char *sfx)              // the same hit plain, dragged slowly (held), dragged with a twist
{
    quiet_down();
    float t0 = sim_t;
    hitg(v, 115, 0); run(1.4f);
    hitg(v, 115, 120 + v); drag_play(120 + v, 0.06f, 1.0f, 0.7f, 1.0f, 0.0f, 0.0f, 1.6f); td_none(); run(1.2f);
    hitg(v, 115, 140 + v); drag_play(140 + v, 0.06f, 1.0f, 0.7f, 1.0f, 5.0f, 1.3f, 2.0f); td_none(); run(1.5f);
    sim_wav(sfx, t0, sim_t);
    note("out/%s%s.wav: %s plain, dragged slowly (held), dragged with a twist (DRAG BEND 5)", sim_name, sfx, dv_name[v]);
}
static void drag_notes_sounds(void)
{
    section("engine: sound files (the drag, the notes)");
    still(); defaults(); td_none(); set("role", ROLE_DRUM); TD.depth = 0.7f; set("drag", 70); run(1.0f);
    note("the default bus and DRAG 70 %% (as a player hears them)");
    drag_wav(DV_KICK, "-drag-kick");
    drag_wav(DV_SNAP, "-drag-snap");
    drag_wav(DV_HAT, "-drag-hat");
    drag_wav(DV_CLANG, "-drag-clang");
    // the notes, at 120, twice through two bars
    set("role", ROLE_NOTES); run(0.5f);
    const int nb = (int)(sizeof BASSLINE / sizeof BASSLINE[0]), nl = (int)(sizeof LEADLINE / sizeof LEADLINE[0]), nbl = (int)(sizeof BELLS / sizeof BELLS[0]);
    qev_t seq[64];
    static const char *const sfx[NNV] = { "-notes-bass", "-notes-squelch", "-notes-lead", "-notes-bell" };
    for (int v = 0; v < NNV; v++) {
        voice(v); glide(v == NV_SQUELCH ? 0.05f : v == NV_LEAD ? 0.09f : 0.06f); run(0.3f);
        int n = v == NV_LEAD ? q_sorted(seq, LEADLINE, nl, 0, 0) : v == NV_BELL ? q_sorted(seq, BELLS, nbl, 0, 0)
              : q_sorted(seq, BASSLINE, nb, v == NV_SQUELCH ? 12 : 0, v == NV_SQUELCH);
        float t0 = sim_t;
        q_play(seq, n, 2, 8.0f, 2.0f);
        sim_wav(sfx[v], t0, sim_t);
        note("out/%s%s.wav: %s, two bars at 120 twice (the last note of each pass dragged)", sim_name, sfx[v], nv_name[v]);
    }
    check(m_bad() == 0, "the sound files: no NaN or inf");
    voice(NV_BASS); glide(0.06f); set("drag", 70); TD.depth = 1.0f;
    set("role", ROLE_DRUM); defaults(); quiet_down();
}

// ---------------------------------------------------------------- sound files
static void sounds(void)
{
    section("engine: sound files");
    still(); defaults(); run(1.0f);
    static const char *const nm[DV_OFF] = { "kick", "snap", "clang", "hat", "glass", "sub", "whip", "creak" };
    for (int v = 0; v < DV_OFF; v++) {
        float t0 = sim_t;
        hit(v, 127); run(0.6f); hit(v, 70); run(0.6f);
        set("sqez", 100); hit(v, 127); run(1.2f); set("sqez", param_def(P("sqez")));
        char sfx[32]; snprintf(sfx, sizeof sfx, "-eng-%s", nm[v]);
        sim_wav(sfx, t0, sim_t);
        note("out/%s%s.wav: velocity 127, 70, then SQUEEZE 100", sim_name, sfx);
    }
    // two boxes: the left hand's kick on every beat, the right hand's METAL struck by it (rendered apart, mixed)
    run(1.0f);
    float ta = sim_t;
    for (int b = 0; b < 8; b++) { hit(DV_KICK, 120); run(0.47f); }
    run(1.5f);
    set("role", ROLE_PAINT); set("matl", MAT_METAL); set("chrd", CH_FIFTH); run(1.0f);
    float tb = sim_t;
    for (int b = 0; b < 8; b++) { strike(DV_KICK, 120); run(0.5f); }
    run(1.6f);                                                  // (a little past the 5.5 s the files take)
    wav_mix("-eng-pair", ta, tb, 5.5f);
    sim_wav("-eng-pair-kick", ta, ta + 5.5f);
    sim_wav("-eng-pair-metal", tb, tb + 5.5f);
    note("out/%s-eng-pair.wav: a kick on every beat in one hand, the METAL it strikes in the other (and -kick, -metal apart)", sim_name);
    // PUMP: moved for 5 s, then still
    set("role", ROLE_PUMP); set("chrd", CH_OCTAVE); set("clmb", 4); set("fall", 8); run(1.0f);
    float tp = sim_t;
    run(1.0f);
    st_hold(ST_FRONT, 380.0f); run(5.0f);
    st_hold(ST_FRONT, 0.0f); run(5.0f);
    sim_wav("-eng-pump", tp, sim_t);
    note("out/%s-eng-pump.wav: PUMP at rest, moved 5 s, still 5 s (OCTAVE, METAL)", sim_name);
    note("out/%s-eng-morph.wav: the tilt sweeping METAL to LATEX while it drones", sim_name);
    set("role", ROLE_DRUM); defaults(); run(2.0f);
}

void test_engine(void)
{
    if (getenv("AD_COST_ONLY")) { cost(); return; }
    kick();
    drums();
    material();
    bow();
    smooth();
    drag_tests();
    notes_tests();
    shape_tests();
    note_hit_loop();
    cost();
    sounds();
    drag_notes_sounds();
    check(m_bad() == 0, "the engine's tests: no NaN or inf");
    still(); defaults(); run(0.5f);
}
