// AIRDWELLER's engine on the host: the eight drums (each one's character measured), the material (its modes, RING,
// CHORD, BEND, MORPH, the bow, CREAK, DUCK), CUT, smoothness under every sound control, the cost of the busiest
// block, and sound files to listen to (out/airdweller-eng-*.wav). Controls go through the params (the host writes
// eng_* from them every tick); hits go through eng_trigger and eng_strike as the host's own do.
#include "sim.h"
#include "inst_ad.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float st_mic(float t, void *u);
void  st_reset(void);
void  st_hold(vec3_t dir, float dps);
extern const vec3_t ST_FRONT, ST_BACK, ST_LEFT, ST_RIGHT, ST_TWIST;

#define SRF ((float)AUDIO_SR)
static void  run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
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
    eng_trigger(&(trig_t){ PD_FRONT, (uint8_t)vel, SRC_LIVE, 0 });
    return t0;
}
static float strike(int voice, int vel) { float t0 = sim_t; eng_strike(0, vel, voice); return t0; }
static void shake_tick(void) { static int k; SI.acc = (vec3_t){ 0, 0, (k++ & 1) ? 2.0f : 0.2f }; sim_tick(); }
static void shake(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) shake_tick(); }

// the other hand (for DUCK: the host ducks only when another AIRDWELLER is there)
static int peer = -1;
static void peer_here(void)
{
    if (peer < 0) peer = sim_peer_add("AIRDWELL", 1, 4.0, 0.0); else sim_peer_power(peer, true);
    uint8_t a[16] = { 3, ROLE_DRUM | HAND_LEFT << 2, 0, 36, 0 };
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
    // velocity: softer and darker
    clean();
    t = hit(DV_CLANG, 127); run(0.6f);
    float lh = m_peak(t, t + 0.3f), ch = centroid(t, t + 0.05f);
    t = hit(DV_CLANG, 40); run(0.6f);
    float ll = m_peak(t, t + 0.3f), cl = centroid(t, t + 0.05f);
    check(dbr(lh, ll) > 6.0f && ch > cl, "velocity 40 against 127: %.1f dB down, centroid %.0f against %.0f Hz", (double)dbr(lh, ll), (double)cl, (double)ch);
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
    // DUCK: the other hand's strike pushes the drone down and lets it back
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
        sim_tick();
    }
    set("matl", MAT_LATEX); run(1.0f);                          // and on to LATEX
    float wc = worst_click(t0 + 0.05f, sim_t - 0.2f);
    check(wc <= 3.0f, "MORPH swept METAL > GLASS > RUBBER > LATEX while it drones: worst %.2fx", (double)wc);
    sim_wav("-eng-morph", t0 - 0.5f, sim_t);
    st_hold(ST_FRONT, 0.0f); sim_tilt(0, 0);
    set("role", ROLE_DRUM); defaults(); run(1.0f);
}

// ---------------------------------------------------------------- the cost: the busiest block
// Ten of the heaviest drums at DECAY 4 still ringing as the box turns to PUMP: the TRIAD bowed and creaking, struck,
// through drive, echo and room. Measured three times (the host has other work: the least of the three is the figure).
static double busy_once(int *v0, int *v1)
{
    still(); defaults();
    set("decy", 4); set("chrd", CH_TRIAD); set("crek", 100); set("shak", 100); set("clmb", 1);
    set("drve", 60); set("emix", 30); set("efb ", 50); set("room", 40); set("rsiz", 70); run(0.3f);
    static const int kit[10] = { DV_CLANG, DV_CREAK, DV_CLANG, DV_GLASS, DV_CREAK, DV_CLANG, DV_KICK, DV_CREAK, DV_GLASS, DV_CLANG };
    st_hold(ST_FRONT, 400.0f);
    for (int i = 0; i < 10; i++) { set("pfrt", (float)kit[i]); set("tune", (float)(i - 5)); shake_tick(); shake_tick(); eng_trigger(&(trig_t){ PD_FRONT, 120, SRC_LIVE, 0 }); }
    set("role", ROLE_PUMP); shake(0.15f);
    *v0 = eng_voices_on;
    eng_audio_ns = 0; eng_audio_max_ns = 0; eng_audio_blocks = 0;
    strike(DV_KICK, 120); shake(0.15f); strike(DV_CREAK, 120); shake(0.15f);
    double us = eng_audio_ns / (double)eng_audio_blocks / 1000.0;
    *v1 = eng_voices_on;
    set("role", ROLE_DRUM); st_hold(ST_FRONT, 0.0f); still(); defaults(); run(0.5f);
    for (int i = 0; i < 1500 && eng_voices_on > 0; i++) sim_tick();   // the tails die before the next
    return us;
}
static void cost(void)
{
    section("engine: the busiest block");
    double best = 1e9, all[3], mx = 0; int v0 = 0, v1 = 0, ok = 1;
    for (int r = 0; r < 3; r++) {
        int a, b;
        all[r] = busy_once(&a, &b);
        if (eng_audio_max_ns / 1000.0 > mx) mx = eng_audio_max_ns / 1000.0;
        if (all[r] < best) { best = all[r]; v0 = a; v1 = b; }
        ok &= a >= 10 && b >= 10;
    }
    check(ok, "busy: %d then %d drums sounding (each run), the TRIAD bowed and creaking, struck, drive, echo, room", v0, v1);
    check(best < 15.0, "the busiest case: %.2f us a block (runs: %.2f %.2f %.2f; the worst block %.1f), the budget 15", best, all[0], all[1], all[2], mx);
    metric("engine_us_per_block_busiest", best);
    // and a plain one: a beat of four drums through the default bus
    eng_audio_ns = 0; eng_audio_max_ns = 0; eng_audio_blocks = 0;
    for (int b = 0; b < 8; b++) { hit(b & 1 ? DV_SNAP : DV_KICK, 110); run(0.1f); hit(DV_HAT, 80); run(0.12f); }
    metric("engine_us_per_block_drumming", eng_audio_ns / (double)eng_audio_blocks / 1000.0);
    run(1.0f);
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
    run(1.5f);
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
    kick();
    drums();
    material();
    bow();
    smooth();
    cost();
    sounds();
    check(m_bad() == 0, "the engine's tests: no NaN or inf");
    still(); defaults(); run(0.5f);
}
