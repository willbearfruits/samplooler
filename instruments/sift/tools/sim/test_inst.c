// SIFT on the host. DECOMPOSE first: Daisy Bell is sung into the corpus at boot (its melody at its pitches, its 20
// syllables cut where they start); at 0 the grains follow the head exactly and the recording comes out whole; as D
// rises the grains get shorter, come from further away and fall out of order, syllables are caught and repeated;
// the tilt throws it apart and back together in a moment, tipping it towards you slows the head, stops it and runs
// it backwards; drive, crush and feedback stay bounded; ROT changes the recording and nothing else does; the Amen
// prints and plays. Then MAP on the starter sounds: the order side (the grid, a pattern sorted and tuned), the chaos
// side (a swarm at DENSITY), the tilt as the cursor, REC held and tapped and the ring wrapping, VOICE, a MIDI note,
// the shake, the link, save and load, a Samplooler slot, the USB and ITSELF sources; every sound; the CPU; screens
// and GIFs; the soak. SIFT_WHISPER=1 also asks whisper.cpp (tools/ear.py) how many of Daisy's words it hears at each
// stage of the decomposition.
#include "sim.h"
#include "inst_sift.h"
#include "link.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern spawn_t sift_log[8192];
extern int     sift_log_n;
extern double  sift_audio_ns, sift_audio_max_ns;
extern long    sift_audio_blocks;
extern int   (*sift_sim_usb_in)(int16_t *mono, int n);
void store_sim_put(int bank, const int16_t *x, int n);

const char *const inst_click_skip[] = { "nlnk", "ngrp", "npow", NULL };   // the radio's switches, not the sound

// ---------------------------------------------------------------- helpers
static void run(float s) { int n = (int)(s * 100.0f + 0.5f); for (int i = 0; i < n; i++) sim_tick(); }
static int  P(const char *tag) { int p = param_by_tag(tag); if (p < 0) { printf("no param %s\n", tag); exit(1); } return p; }
static void set(const char *tag, float v) { param_set(P(tag), v); }
static int preset_index(const char *name) { for (int i = 0; i < INST.npresets; i++) if (!strcmp(INST.presets[i].name, name)) return i; return 0; }
static void settle(void) { for (int i = 0; i < 1500 && (S.job != JOB_NONE || S.pending > 0); i++) sim_tick(); run(0.1f); }
static void load(const char *name) { front_midi(0xC0, (uint8_t)preset_index(name), 0); run(0.3f); settle(); }
static void act(int p) { INST.action(p); }
static void bank_load(int bank) { set("bank", (float)bank); act(P_LOAD); run(0.05f); settle(); }
static void starter(void) { bank_load(BK_STARTER); }

// the mic: a quiet room and a tone (its pitch may glide)
static float tone_hz, tone_amp, tone_glide;
static double tone_ph;
static float mic(float t, void *u)
{
    (void)u;
    float f = tone_hz;
    if (tone_glide != 0.0f) f = tone_hz * exp2f(tone_glide * sinf(t * 1.3f));
    tone_ph += (double)f / AUDIO_SR;
    if (tone_ph > 1.0) tone_ph -= 1.0;
    return 0.0008f * sim_noise() + tone_amp * sinf(6.2831853f * (float)tone_ph);
}
static void tone(float hz, float amp) { tone_hz = hz; tone_amp = amp; tone_glide = 0.0f; }

static int usb_tone(int16_t *m, int n)
{
    static double ph;
    for (int i = 0; i < n; i++) { ph += 500.0 / AUDIO_SR; if (ph > 1.0) ph -= 1.0; m[i] = (int16_t)(9000.0f * sinf(6.2831853f * (float)ph)); }
    return n;
}

static void still(void) { sim_tilt(0, 0); }
static void quiet_order(void)                                    // MAP, the order side, nothing random
{
    set("mode", MODE_MAP);
    set("chao", 0); set("drft", 0); set("shak", 0); set("ptch", 0); set("stak", 1); set("grid", 3);
    set("nseq", 6); set("patt", PAT_UP); set("tune", TUNE_SCALE); set("curs", CUR_TILT);
}
static void quiet_decomp(void)                                   // DECOMPOSE, the defaults, no room, nothing pushed
{
    set("mode", MODE_DECOMP);
    set("chao", 0); set("drft", 0); set("shak", 0); set("ptch", 0); set("sped", 100); set("spce", 0);
    set("stut", 50); set("jump", 50); set("gaps", 40); set("bite", 60); set("drve", 0); set("crsh", 0); set("fdbk", 0); set("rotw", 0);
    set("size", 0.09f); still();
}
static int newest_take(void) { int t = 0; for (int u = 0; u < corp_units; u++) if (units[u].state != U_EMPTY && units[u].take > t) t = units[u].take; return t; }
static int count_pitch(float note, float tol, int take)
{
    int n = 0;
    for (int u = 0; u < corp_units; u++) if (units[u].state == U_OK && (take < 0 || units[u].take == take) && fabsf(units[u].pitch - note) < tol) n++;
    return n;
}
static float in_key_share(int key, int scale_minor)
{
    static const int maj[] = { 0, 2, 4, 5, 7, 9, 11 }, min[] = { 0, 2, 3, 5, 7, 8, 10 };
    int n = 0, ok = 0;
    for (int i = 0; i < sift_log_n; i++) {
        const spawn_t *s = &sift_log[i];
        if (s->kind != GK_ORDER || s->unit < 0 || units[s->unit].pitch < 0.0f) continue;
        float nn = units[s->unit].pitch + s->semi;
        int r = (int)lroundf(nn), pc = ((r - key) % 12 + 12) % 12;
        bool in = false;
        for (int k = 0; k < 7; k++) if ((scale_minor ? min[k] : maj[k]) == pc) in = true;
        n++;
        if (in && fabsf(nn - (float)r) < 0.06f) ok++;
    }
    return n ? (float)ok / (float)n : -1.0f;
}
static float pc_share(int pc_want)
{
    int n = 0, ok = 0;
    for (int i = 0; i < sift_log_n; i++) {
        const spawn_t *s = &sift_log[i];
        if (s->kind != GK_ORDER || s->unit < 0 || units[s->unit].pitch < 0.0f) continue;
        float nn = units[s->unit].pitch + s->semi;
        int r = (int)lroundf(nn);
        n++;
        if (((r % 12) + 12) % 12 == pc_want && fabsf(nn - (float)r) < 0.15f) ok++;
    }
    return n ? (float)ok / (float)n : -1.0f;
}
static int count_kind(int kind) { int n = 0; for (int i = 0; i < sift_log_n; i++) n += sift_log[i].kind == kind; return n; }
// how far a frame is from the grid's nearest line (steps of fpb / div from beat 0)
static float off_grid(uint32_t f, int div)
{
    uint32_t o; float fr; link_origin(&o, &fr);
    float st = (float)link_fpb() / (float)div;
    int32_t rel = (int32_t)(f - o);
    float p = fmodf((float)rel - fr, st);
    if (p < 0) p += st;
    return fminf(p, st - p);
}
static void shake_it(float s)
{
    int n = (int)(s * 100.0f);
    for (int i = 0; i < n; i++) { SI.acc = (vec3_t){ (i & 1) ? 1.6f : -1.6f, 0.3f, 1.0f }; sim_tick(); }
    SI.acc = (vec3_t){ 0, 0, 1 };
}
static int32_t ring_d(uint32_t a, uint32_t b) { int32_t d = (int32_t)a - (int32_t)b, h = corp_frames / 2; if (d > h) d -= corp_frames; else if (d < -h) d += corp_frames; return d; }

void inst_excite(void)
{
    if (map_count() == 0) { starter(); run(0.5f); }
    SI.mic = mic; tone(0, 0);
}

// ---------------------------------------------------------------- DECOMPOSE
static void boot(void)
{
    section("boot: Daisy");
    SI.mic = mic;
    still();
    settle();
    check(S.material == MAT_DAISY && S.mode == MODE_DECOMP && map_count() > 350, "at power-on Daisy Bell is sung into the corpus and sifted (%d units, material %d)", map_count(), S.material);
    // the melody: "Dai-sy, Dai-sy" on D4 B3 G3 D3 (a man's octave)
    static const float want[4] = { 62, 59, 55, 50 };
    int good = 0;
    for (int j = 0; j < 4; j++) {
        int u0 = daisy_syl_at(j) / UNIT, u1 = (j < 3 ? daisy_syl_at(j + 1) : daisy_syl_at(j) + 30000) / UNIT, hit = 0, n = 0;
        for (int u = u0; u < u1; u++) if (units[u].pitch >= 0) { n++; hit += fabsf(units[u].pitch - want[j]) < 0.6f; }
        note("syllable %d: %d of %d pitched units at %.0f", j, hit, n, (double)want[j]);
        good += n > 4 && hit * 2 > n;
    }
    check(good == 4, "it sings the tune: the first four syllables at D4 B3 G3 D3 (%d of 4)", good);
    int ncut = 0; for (int u = 0; u < corp_units; u++) ncut += eng_cut[u];
    check(ncut == DAISY_NSYL, "its 20 syllables are where the cuts are (%d cuts)", ncut);
    run(1.0f);
    float r = m_rms(sim_t - 1.0f, sim_t), pk = m_peak(sim_t - 1.0f, sim_t);
    metric("daisy_rms", r); metric("daisy_peak", pk);
    check(r > 0.01f && r < 0.25f && pk < 0.9f, "it plays at a sane level (rms %.3f, peak %.2f)", (double)r, (double)pk);
    sim_shot("daisy");
}

static void whole(void)
{
    section("decompose: whole at 0");
    quiet_decomp();
    run(1.0f);
    sift_log_n = 0;
    float t0 = sim_t;
    run(3.0f);
    // each grain starts half a grain after the one before, where the head is
    int ok = 0, tot = 0;
    for (int i = 0; i + 1 < sift_log_n; i++) {
        if (sift_log[i].kind != GK_ORDER || sift_log[i + 1].kind != GK_ORDER) continue;
        int32_t d = ring_d(sift_log[i + 1].pos, sift_log[i].pos);
        tot++;
        if (abs(d - 1920) <= 1 && sift_log[i].len == 3840) ok++;
    }
    check(tot > 40 && ok >= tot * 95 / 100, "CHAOS 0: every grain 120 ms, every one 60 ms on from the last, as the head moves (%d of %d)", ok, tot);
    // so what comes out is the recording: the output against the corpus where the head read it
    double bestc = 0; bool found = false;
    for (int i = 0; i < sift_log_n && !found; i++) {
        const spawn_t *s = &sift_log[i];
        if (s->kind != GK_ORDER || (int)s->pos + 40000 > daisy_length() || s->frame < (uint32_t)(t0 * AUDIO_SR) + 2000) continue;
        int f = (int)s->frame + 4000, p = (int)s->pos + 4000, n = 24000;
        double xy = 0, xx = 0, yy = 0;
        for (int k = 0; k < n; k++) { double x = sim_out[f + k], y = corp[p + k]; xy += x * y; xx += x * x; yy += y * y; }
        bestc = xx > 0 && yy > 0 ? xy / sqrt(xx * yy) : 0;
        found = true;
    }
    check(found && bestc > 0.97, "what comes out is the recording itself (correlation %.3f over 0.75 s)", bestc);
    sim_wav("-whole", sim_t - 3.0f, sim_t);
}

typedef struct { float len_ms, dist_s, order, rate, rep; } stat_t;
static stat_t measure(float D, float secs)
{
    set("chao", roundf(D * 100.0f));
    run(0.6f);
    sift_log_n = 0;
    run(secs);
    stat_t st = { 0, 0, 0, 0, 0 };
    double len = 0, dist = 0; int n = 0, ord = 0, rep = 0;
    for (int i = 0; i < sift_log_n; i++) {
        const spawn_t *s = &sift_log[i];
        if (s->kind == GK_BURST) continue;
        n++; len += s->len; dist += fabsf(s->dist); ord += s->kind == GK_ORDER; rep += s->kind == GK_REPEAT;
    }
    if (n) { st.len_ms = (float)(len / n) / 32.0f; st.dist_s = (float)(dist / n) / AUDIO_SR; st.order = (float)ord / (float)n; st.rep = (float)rep / (float)n; }
    st.rate = (float)n / secs;
    return st;
}

static void apart(void)
{
    section("decompose: coming apart");
    quiet_decomp();
    static const float Ds[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    stat_t st[5];
    for (int k = 0; k < 5; k++) {
        st[k] = measure(Ds[k], 3.0f);
        note("D %.2f: grains %.0f ms, %.2f s from the head, %.0f %% in order, %.0f %% repeats, %.0f a second", (double)Ds[k],
             (double)st[k].len_ms, (double)st[k].dist_s, (double)(st[k].order * 100.0f), (double)(st[k].rep * 100.0f), (double)st[k].rate);
        char b[32]; snprintf(b, sizeof b, "-d%03d", (int)(Ds[k] * 100.0f)); sim_wav(b, sim_t - 3.0f, sim_t);
    }
    check(st[0].len_ms > st[2].len_ms && st[2].len_ms > st[3].len_ms && st[3].len_ms > st[4].len_ms && st[4].len_ms < 12.0f,
          "the grains get shorter: %.0f, %.0f, %.0f, %.0f, %.0f ms", (double)st[0].len_ms, (double)st[1].len_ms, (double)st[2].len_ms, (double)st[3].len_ms, (double)st[4].len_ms);
    check(st[0].dist_s < 0.01f && st[3].dist_s > st[1].dist_s && st[4].dist_s > st[2].dist_s && st[4].dist_s > 2.0f,
          "they come from further away: %.2f, %.2f, %.2f, %.2f, %.2f s from the head", (double)st[0].dist_s, (double)st[1].dist_s, (double)st[2].dist_s, (double)st[3].dist_s, (double)st[4].dist_s);
    check(st[0].order > 0.99f && st[2].order < st[1].order && st[4].order < 0.2f,
          "and fall out of order: %.0f, %.0f, %.0f, %.0f, %.0f %% in step with the head", (double)(st[0].order * 100.0f), (double)(st[1].order * 100.0f), (double)(st[2].order * 100.0f), (double)(st[3].order * 100.0f), (double)(st[4].order * 100.0f));
    // the syllables come loose: damaged where they start, stammered over and over
    set("stut", 100);
    set("chao", 34); run(0.6f);
    sift_log_n = 0; run(16.0f);                                         // one pass through Daisy
    int at_cut = 0, all = 0, rep = 0;
    for (int i = 0; i < sift_log_n; i++) {
        const spawn_t *s = &sift_log[i];
        if (s->kind == GK_BURST) continue;
        all++;
        rep += s->kind == GK_REPEAT;
        int u = (int)(s->pos / UNIT);
        if (eng_cut[u] && abs((int)(s->pos % UNIT) - eng_cut_at[u]) <= 2) at_cut++;
    }
    check(all > 100 && at_cut >= 8, "at 34: syllables damaged where they start, the stream whole between them (%d of %d grains at a syllable's start)", at_cut, all);
    check(rep >= 6, "STUTTER 100: syllables stammered, said again and again (%d of %d grains)", rep, all);
    sim_wav("-syllables", sim_t - 16.0f, sim_t);
    set("stut", 50);
    // a slow sweep from whole to dust and back: the piece
    sim_gif_begin("decompose");
    for (int i = 0; i <= 200; i++) { float t = (float)i / 200.0f; set("chao", roundf(100.0f * (t < 0.5f ? 2.0f * t : 2.0f - 2.0f * t))); run(0.04f); }
    sim_gif_end();
    sim_wav("-sweep", sim_t - 8.0f, sim_t);
    set("chao", 0);
}

static void ear(void)
{
    const char *e = getenv("SIFT_WHISPER");                  // "1": the four stages; or a list of CHAOS values, "0,10,20"
    if (!e) return;
    section("decompose: what whisper.cpp hears");
    quiet_decomp();
    int Ds[12] = { 0, 30, 55, 90 }, nd = 4;
    if (strchr(e, ',')) { nd = 0; for (const char *q = e; *q && nd < 12; ) { Ds[nd++] = atoi(q); while (*q && *q != ',') q++; if (*q) q++; } }
    char files[12][64];
    for (int k = 0; k < nd; k++) {
        set("chao", (float)Ds[k]); run(0.6f);
        float t0 = sim_t;
        run(18.0f);
        snprintf(files[k], sizeof files[k], "-ear%02d", Ds[k]);
        sim_wav(files[k], t0, sim_t);
    }
    int heard[12] = { 0 };
    for (int k = 0; k < nd; k++) {
        char cmd[256]; snprintf(cmd, sizeof cmd, "python3 ../ear.py out/%s%s.wav", sim_name, files[k]);
        FILE *p = popen(cmd, "r");
        char line[1024] = { 0 };
        if (p) { if (!fgets(line, sizeof line, p)) line[0] = 0; pclose(p); }
        heard[k] = atoi(line);
        note("CHAOS %d: %s", Ds[k], line);
        char key[32]; snprintf(key, sizeof key, "words_heard_%d", Ds[k]); metric(key, heard[k]);
    }
    bool down = true;
    for (int k = 1; k < nd; k++) if (heard[k] > heard[k - 1] + 2) down = false;
    check(heard[0] >= 12 && heard[nd - 1] <= 2 && down, "the words go as it comes apart (of 16, at each CHAOS asked: %d .. %d)", heard[0], heard[nd - 1]);
    set("chao", 0);
}

static void tilted(void)
{
    section("decompose: the tilt");
    quiet_decomp();
    run(0.5f);
    sim_tilt(30, 0); run(0.3f);
    float a = S.chaos;
    still(); run(0.3f);
    float b = S.chaos;
    check(a > 0.9f && b < 0.05f, "the knob at 0: tilted right it is in pieces in 0.3 s, level it is whole again in 0.3 s (%.2f, %.2f)", (double)a, (double)b);
    set("chao", 60);
    sim_tilt(-30, 0); run(0.3f);
    check(S.chaos < 0.05f, "the knob at 60: tilted left it comes back together (%.2f)", (double)S.chaos);
    still(); run(0.3f);
    set("chao", 0);
    // tipped towards you: slower, frozen half way, backwards at the end; away: faster
    sim_tilt(0, 17); run(0.5f);
    float h0 = eng_head; run(1.0f);
    int32_t frozen = ring_d((uint32_t)eng_head, (uint32_t)h0);
    sim_tilt(0, 30); run(0.5f);
    h0 = eng_head; run(1.0f);
    int32_t back = ring_d((uint32_t)eng_head, (uint32_t)h0);
    sim_tilt(0, -30); run(0.5f);
    h0 = eng_head; run(1.0f);
    int32_t fast = ring_d((uint32_t)eng_head, (uint32_t)h0);
    check(abs(frozen) < UNIT && back < -20000 && fast > 50000, "tipped towards you half way the head stops, all the way it runs back, away it runs at twice (%d, %d, %d frames in 1 s)", frozen, back, fast);
    sim_shot("frozen");
    still(); run(0.5f);
}

static void dirt(void)
{
    section("decompose: dirt, feedback, rot");
    quiet_decomp();
    set("chao", 40); set("fdbk", 100); set("fbtm", 0.05f); set("drve", 0);
    int bad0 = m_bad();
    run(6.0f);
    float pk = m_peak(sim_t - 4.0f, sim_t), r = m_rms(sim_t - 4.0f, sim_t);
    check(m_bad() == bad0 && pk <= 1.0f && r > 0.01f, "FEEDBACK 100 with no drive stays bounded (rms %.3f, peak %.2f)", (double)r, (double)pk);
    set("drve", 100); set("crsh", 60);
    run(4.0f);
    pk = m_peak(sim_t - 3.0f, sim_t); r = m_rms(sim_t - 3.0f, sim_t);
    check(m_bad() == bad0 && pk <= 1.0f && r > 0.02f, "with the drive and the crush up it screams, and stays bounded (rms %.3f, peak %.2f)", (double)r, (double)pk);
    sim_wav("-feedback", sim_t - 3.0f, sim_t);
    set("fdbk", 0); set("drve", 0); set("crsh", 0);
    run(1.0f);
    // nothing but ROT writes into the recording; ROT does
    static int16_t copy[CORP_SECS * AUDIO_SR];
    memcpy(copy, corp, (size_t)corp_frames * 2);
    set("chao", 70); run(5.0f);
    check(memcmp(copy, corp, (size_t)daisy_length() * 2) == 0, "five seconds torn apart without ROT: the recording is untouched");
    set("rotw", 60);
    run(25.0f);
    double dif = 0, ref = 0;
    for (int i = 0; i < daisy_length(); i++) { dif += fabs((double)corp[i] - copy[i]); ref += fabs((double)copy[i]); }
    set("rotw", 0); set("chao", 0);
    run(0.5f);
    check(dif > 0.3 * ref && map_count() > 300, "25 s with ROT 60 at 70: the recording itself has changed (%.0f %% of it) and is still sifted (%d units)", 100.0 * dif / ref, map_count());
    sim_shot("rotted");
    // a Daisy sound brings Daisy back whole
    load("DAISY");
    double dif2 = 0;
    for (int i = 0; i < daisy_length(); i++) dif2 += fabs((double)corp[i] - copy[i]);
    check(S.material == MAT_DAISY && dif2 < 0.02 * ref, "choosing DAISY again sings it again, whole (%.1f %% off)", 100.0 * dif2 / ref);
}

static void amen(void)
{
    section("decompose: the Amen");
    load("AMEN");
    check(S.material == MAT_AMEN && map_count() > 300, "AMEN prints four takes of the break into the corpus (%d units)", map_count());
    int ncut = 0; for (int u = 0; u < corp_units; u++) ncut += eng_cut[u];
    check(ncut > 60, "its hits are the cuts (%d)", ncut);
    still(); run(1.0f);
    float r = m_rms(sim_t - 1.0f, sim_t);
    check(r > 0.02f, "it plays (rms %.3f)", (double)r);
    sim_wav("-amen", sim_t - 1.0f, sim_t);
    load("AMEN SHRED");
    sim_gif_begin("amen");
    run(3.0f);
    sim_gif_end();
    sim_wav("-amen-shred", sim_t - 3.0f, sim_t);
    // a sound with Daisy over your own recording: yours stays
    set("tkmd", TK_NEW); tone(300.0f, 0.3f); sim_rec(1.0f); tone(0, 0); settle();
    load("DAISY");
    check(S.material == MAT_KEEP && count_pitch(62.2f, 0.4f, -1) > 20, "DAISY chosen after you recorded: your take stays (material %d)", S.material);
    set("tkmd", TK_ADD);
    bank_load(BK_DAISY);
    check(S.material == MAT_DAISY, "TAKE > BANK DAISY, LOAD: Daisy again");
}

// ---------------------------------------------------------------- MAP
static void order(void)
{
    section("map: the order side");
    starter();
    quiet_order(); still();
    run(1.5f);
    sift_log_n = 0;
    run(4.0f);
    int no = count_kind(GK_ORDER), nc = count_kind(GK_CHAOS);
    float bpm = 60.0f * AUDIO_SR / (float)link_fpb();
    int want = (int)(4.0f * bpm / 60.0f * 4.0f);
    check(nc == 0 && abs(no - want) <= 2, "CHAOS 0: only order grains, one on every 1/16 (%d grains, %d expected at %.0f BPM)", no, want, (double)bpm);
    float worst = 0;
    for (int i = 0; i < sift_log_n; i++) worst = fmaxf(worst, off_grid(sift_log[i].frame, 4));
    check(worst <= 1.0f, "every one on the grid's line (the worst %.2f frames off)", (double)worst);
    int k = S.nseq;
    bool asc = true;
    for (int i = 0; i + 1 < k; i++) {
        int a = S.seq_unit[i], b = S.seq_unit[i + 1];
        float ka = map_tone_of(a), kb = map_tone_of(b);
        if (units[a].pitch >= 0) { for (int j = 0; j < sift_log_n; j++) if (sift_log[j].unit == a) { ka = units[a].pitch + sift_log[j].semi; break; } }
        if (units[b].pitch >= 0) { for (int j = 0; j < sift_log_n; j++) if (sift_log[j].unit == b) { kb = units[b].pitch + sift_log[j].semi; break; } }
        if (kb < ka - 0.01f) asc = false;
    }
    int rep = 0, cmp = 0;
    for (int i = 0; i + k < sift_log_n; i++) { cmp++; rep += sift_log[i].unit == sift_log[i + k].unit; }
    check(k == 6 && asc && rep >= cmp * 9 / 10, "NOTES 6, UP: six grains, low to high, round and round (%d in the pattern, %s, %d of %d repeat)", k, asc ? "ascending" : "NOT ascending", rep, cmp);
    float share = in_key_share(0, 0);
    check(share > 0.99f, "TUNE TO SCALE: every pitched grain lands on a note of C major (%.0f %%)", (double)(share * 100.0f));
    sim_wav("-order", sim_t - 4.0f, sim_t);
    sim_shot("order");
    set("patt", PAT_BOUNCE); run(0.5f);
    check(S.nseq == 10, "BOUNCE: up and back down without the ends twice (%d steps for 6 notes)", S.nseq);
    set("patt", PAT_PLAYED); run(0.5f);
    bool tord = true;
    for (int i = 0; i + 1 < S.nseq; i++) if (units[S.seq_unit[i]].d[D_TIME] > units[S.seq_unit[i + 1]].d[D_TIME]) tord = false;
    check(tord, "AS PLAYED: in the order they were recorded");
    set("patt", PAT_UP);
    set("stak", 3); run(0.5f);
    sift_log_n = 0; run(2.0f);
    int same = 0;
    for (int i = 0; i + 2 < sift_log_n; i += 3) same += sift_log[i].frame == sift_log[i + 1].frame && sift_log[i].frame == sift_log[i + 2].frame;
    check(same >= sift_log_n / 3 - 1 && sift_log_n > 30, "STACK 3: three grains at once on every step (%d chords of %d grains)", same, sift_log_n);
    set("stak", 1);
    static const int divs[] = { 1, 2, 3, 4, 6, 8 };
    int bad = 0;
    for (int g = 0; g < 6; g++) {
        set("grid", (float)g); run(0.3f);
        sift_log_n = 0; run(1.0f);
        float w = 0; for (int i = 0; i < sift_log_n; i++) w = fmaxf(w, off_grid(sift_log[i].frame, divs[g]));
        int exp_n = (int)(bpm / 60.0f * (float)divs[g]);
        if (w > 1.0f || abs(sift_log_n - exp_n) > 2) { bad++; note("GRID %d: %d grains (%d expected), %.1f frames off", g, sift_log_n, exp_n, (double)w); }
    }
    check(bad == 0, "each GRID, the triplets too, puts its grains on its own lines");
    set("grid", 3);
}

static void chaos(void)
{
    section("map: the chaos side");
    set("mode", MODE_MAP);
    set("chao", 100); set("dens", 40); set("revs", 40); set("scat", 7); set("shak", 0);
    run(1.0f);
    sift_log_n = 0;
    run(5.0f);
    int nc = count_kind(GK_CHAOS), no = count_kind(GK_ORDER), rev = 0, on = 0;
    double s2 = 0;
    for (int i = 0; i < sift_log_n; i++) {
        rev += sift_log[i].rev;
        on += off_grid(sift_log[i].frame, 4) < 2.0f;
        s2 += (double)sift_log[i].semi * sift_log[i].semi;
    }
    float sd = sift_log_n ? sqrtf((float)(s2 / sift_log_n)) : 0;
    check(no == 0 && nc > 140 && nc < 260, "CHAOS 100, DENSITY 40: a swarm of about 200 grains in 5 s, none from the grid (%d chaos, %d order)", nc, no);
    check(on < nc / 10, "they fall anywhere, not on the grid (%d of %d within 2 frames of a line)", on, nc);
    check(rev > nc / 4 && rev < nc * 11 / 20, "REVERSE 40: about 40 %% play backwards (%d of %d)", rev, nc);
    check(sd > 2.5f && sd < 6.0f, "SCATTER 7: pitches thrown up and down (spread %.1f st)", (double)sd);
    sim_wav("-chaos", sim_t - 5.0f, sim_t);
    sim_shot("chaos");
    set("chao", 50); run(1.0f);
    sift_log_n = 0; run(4.0f);
    no = count_kind(GK_ORDER); nc = count_kind(GK_CHAOS);
    check(no > 10 && nc > 10, "CHAOS 50: the grid thins and the swarm comes in (%d order, %d chaos)", no, nc);
    set("chao", 30);
}

static void tilt(void)
{
    section("map: the tilt is the cursor");
    quiet_order();
    sim_tilt(30, 0); run(1.0f);
    float sx = 0; for (int i = 0; i < S.nseq; i++) sx += map_ux(S.seq_unit[i]);
    sx /= (float)(S.nseq ? S.nseq : 1);
    check(S.cx > 0.9f && sx > 0.65f, "the right edge down: the cursor goes right, the grains come from the right (cursor %.2f, pattern at %.2f)", (double)S.cx, (double)sx);
    sim_shot("tilt-right");
    sim_tilt(-30, 0); run(1.0f);
    sx = 0; for (int i = 0; i < S.nseq; i++) sx += map_ux(S.seq_unit[i]);
    sx /= (float)(S.nseq ? S.nseq : 1);
    check(S.cx < 0.1f && sx < 0.35f, "the left edge down: left (cursor %.2f, pattern at %.2f)", (double)S.cx, (double)sx);
    sim_tilt(0, 30); run(1.0f);
    float y1 = S.cy;
    sim_tilt(0, -30); run(1.0f);
    check(y1 < 0.1f && S.cy > 0.9f, "the bottom edge down: down; up: up (%.2f, %.2f)", (double)y1, (double)S.cy);
    still(); run(0.5f);
}

static void rec(void)
{
    section("REC");
    starter();
    set("mode", MODE_MAP);
    set("tkmd", TK_ADD); set("srce", SRC_MIC);
    int n0 = map_count();
    tone(440.0f, 0.3f);
    sim_rec(1.5f);
    tone(0, 0);
    run(0.5f);
    int t = newest_take(), a4 = count_pitch(69.0f, 0.3f, t);
    check(a4 >= 38 && map_count() >= n0 + 38, "REC held 1.5 s on an A: about 46 new units at its pitch, the rest kept (%d at A4, %d -> %d on the map)", a4, n0, map_count());
    check(S.rec == REC_IDLE && eng_rec_state == RS_OFF, "let go: it stops");
    set("tkmd", TK_NEW);
    tone(220.0f, 0.3f);
    sim_gif_begin("rec");
    sim_rec(1.0f);
    tone(0, 0);
    run(0.6f);
    sim_gif_end();
    int a3 = count_pitch(57.0f, 0.3f, -1);
    check(map_count() >= 26 && map_count() <= 33 && a3 >= map_count() - 2, "TAKES NEW: the old grains go, only the new take is there (%d units, %d at A3)", map_count(), a3);
    set("tkmd", TK_ADD);
    tone(330.0f, 0.2f);
    SI.rec = true; run(0.15f); SI.rec = false; run(2.0f);
    bool live = S.rec == REC_LIVE && eng_rec_state == RS_ON;
    sim_shot("live");
    SI.rec = true; run(0.05f); SI.rec = false; run(0.3f);
    tone(0, 0);
    check(live && S.rec == REC_IDLE && eng_rec_state == RS_OFF && count_pitch(64.0f, 0.3f, -1) > 55,
          "REC tapped: LIVE, still recording 2 s later; tapped again it stops (%d units at E4)", count_pitch(64.0f, 0.3f, -1));
    tone(550.0f, 0.2f);
    SI.rec = true; run(0.1f); SI.rec = false;
    run(24.0f);
    int cs5 = count_pitch(73.0f, 0.4f, -1);
    SI.rec = true; run(0.05f); SI.rec = false; run(0.5f);
    tone(0, 0);
    check(cs5 > corp_units - 20 && m_bad() == 0, "LIVE for 24 s: the ring wraps, the corpus is the last 20 s (%d of %d units at C#5)", cs5, corp_units);
    set("tkmd", TK_NEW);
    tone(262.0f, 0.2f);
    SI.rec = true; run(21.0f);
    bool stopped = eng_rec_state == RS_OFF && eng_rec_full;
    SI.rec = false; run(0.5f);
    tone(0, 0);
    check(stopped && S.rec == REC_IDLE && map_count() > corp_units - 10, "TAKES NEW held 21 s: it stops when the 20 s are full (%d units)", map_count());
    // DECOMPOSE plays what you recorded too: a take of a gliding tone, whole at 0
    tone(200.0f, 0.3f); tone_glide = 1.0f;
    sim_rec(3.0f); tone(0, 0); settle();
    quiet_decomp(); run(1.0f);
    sift_log_n = 0; run(2.0f);
    int ok = 0;
    for (int i = 0; i + 1 < sift_log_n; i++) ok += sift_log[i].kind == GK_ORDER && abs(ring_d(sift_log[i + 1].pos, sift_log[i].pos) - 1920) <= 1;
    check(S.material == MAT_KEEP && ok > 25, "your own take in DECOMPOSE: played whole at 0 (%d grains in step)", ok);
    set("tkmd", TK_ADD);
    starter();
}

static void voice(void)
{
    section("map: the cursor on VOICE");
    quiet_order();
    set("curs", CUR_VOICE); set("mtch", 1);
    tone(330.0f, 0.2f);
    run(1.5f);
    sift_log_n = 0;
    sim_gif_begin("voice");
    run(2.0f);
    sim_gif_end();
    float want = map_norm(D_TONE, log2f(330.0f));
    check(S.voice && fabsf(S.cx - want) < 0.08f, "it hears an E and the cursor goes to the grains that sound like it (cursor %.2f, E4 at %.2f)", (double)S.cx, (double)want);
    float sh = pc_share(4);
    check(sh > 0.8f, "SING ALONG: the pitched grains take the note, an E (%.0f %%)", (double)(sh * 100.0f));
    float before = m_rms(sim_t - 1.0f, sim_t);
    sim_shot("voice");
    set("spce", 0);
    tone(0, 0);
    run(1.2f);
    float after = m_rms(sim_t - 0.3f, sim_t);
    check(eng_gate < 0.05f && after < before * 0.2f, "silence: the cloud falls quiet (rms %.4f -> %.4f)", (double)before, (double)after);
    set("spce", 35);
    set("curs", CUR_TILT);
    run(0.5f);
    front_midi(0x90, 67, 100);
    run(0.5f);
    sift_log_n = 0; run(2.0f);
    sh = pc_share(7);
    front_midi(0x80, 67, 0);
    check(sh > 0.8f, "a MIDI G held: the pitched grains play G (%.0f %%)", (double)(sh * 100.0f));
}

static void shake(void)
{
    section("the shake");
    quiet_order(); set("shak", 70); set("chao", 10);
    run(1.0f);
    sift_log_n = 0;
    sim_gif_begin("shake");
    run(0.3f);
    shake_it(0.4f);
    float sk = S.shake, ce = S.chaos;
    int bursts = count_kind(GK_BURST);
    run(2.6f);
    sim_gif_end();
    check(sk > 0.5f && ce > 0.6f && bursts > 2, "a shake throws it towards chaos and scatters a burst (shake %.2f, chaos %.2f, %d grains in bursts)", (double)sk, (double)ce, bursts);
    run(2.0f);
    check(S.shake < 0.1f && S.chaos < 0.2f, "it settles back in a few seconds (shake %.2f)", (double)S.shake);
    set("shak", 0);
}

static void linked(void)
{
    section("the link");
    quiet_order();
    int k = sim_peer_add("BAND", 2, 25.0, 9.0e9);
    sim_peer_use(k);
    {   uint32_t o; float f; link_origin(&o, &f); link_set_time(16000, o, 4, 0); link_set_harm(2, 1, 0, 0, 0, -1); }   // 120 BPM, D minor
    sim_peer_use(-1);
    float t_join = sim_t;
    run(2.5f);
    check(S.peers == 1 && link_fpb() == 16000 && (int)pv[P_TEMPO] == 120, "a BAND at 120 BPM: SIFT follows (%d peers, %u frames a beat, TEMPO %d)", S.peers, link_fpb(), (int)pv[P_TEMPO]);
    check((int)pv[P_KEY] == 2 && (int)pv[P_SCALE] == 1, "and takes its key, D minor (KEY %d, SCALE %d)", (int)pv[P_KEY], (int)pv[P_SCALE]);
    for (int i = 0; i < 500 && link_error_ms() > 0.05f; i++) sim_tick();    // the clock's mapping slews onto the BAND's first
    note("the mapping settled %.1f s after joining", (double)(sim_t - t_join));
    sift_log_n = 0; run(2.0f);
    float w = 0; for (int i = 0; i < sift_log_n; i++) w = fmaxf(w, off_grid(sift_log[i].frame, 4));
    float sh = in_key_share(2, 1);
    check(w <= 16.0f && sift_log_n >= 15 && sh > 0.99f, "its grains sit on the flock's 1/16 within the link's 0.5 ms, and in D minor (%d grains in 2 s, %.1f frames off, %.0f %% in key)", sift_log_n, (double)w, (double)(sh * 100.0f));
    sim_shot("linked");
    set("tmpo", 90); run(0.5f);
    sim_peer_use(k); uint32_t peer_fpb = link_fpb(); sim_peer_use(-1);
    check(link_fpb() == 21333 && peer_fpb == 21333, "TEMPO 90 here goes out to the flock (%u here, %u there)", link_fpb(), peer_fpb);
    set("skey", 0); set("sscl", 0); run(0.5f);
    sim_peer_use(k); int pk = link_harm()->key; sim_peer_use(-1);
    check(pk == 0, "and so does the key (the BAND has %d)", pk);
    sim_peer_power(k, false);
    set("tmpo", 100);
    run(2.0f);
}

static void storage(void)
{
    section("save and load");
    starter();
    set("spce", 0);
    set("bank", NBUILTIN + 0);
    act(P_SAVE);
    run(0.05f);
    bool saving = S.job == JOB_SAVE;
    sim_shot("save");
    run(2.0f);
    int n0 = map_count();
    check(saving && S.job == JOB_NONE && store_frames(0) > n0 * UNIT * 9 / 10, "SAVE into SIFT 1: every unit written, in the order they were heard (%d frames for %d units)", store_frames(0), n0);
    act(P_CLEAR); run(0.5f);
    float r = m_rms(sim_t - 0.2f, sim_t);
    check(map_count() == 0 && S.empty && r < 0.002f, "CLEAR: no grains, silence (%d units, rms %.4f)", map_count(), (double)r);
    sim_shot("empty");
    act(P_LOAD); run(0.05f);
    bool loading = S.job == JOB_LOAD;
    settle(); run(1.0f);
    r = m_rms(sim_t - 1.0f, sim_t);
    check(loading && abs(map_count() - n0) <= n0 / 20 && r > 0.005f, "LOAD brings them back (%d units, %d before; rms %.3f)", map_count(), n0, (double)r);
    static int16_t take[5 * AUDIO_SR];
    double ph = 0;
    for (int i = 0; i < 5 * AUDIO_SR; i++) {
        float f = 110.0f * exp2f(3.0f * (float)i / (5.0f * AUDIO_SR));
        ph += (double)f / AUDIO_SR; if (ph > 1.0) ph -= 1.0;
        take[i] = (int16_t)(12000.0f * sinf(6.2831853f * (float)ph));
    }
    store_sim_put(NBANK_OWN + 2, take, 5 * AUDIO_SR);
    bank_load(NBUILTIN + NBANK_OWN + 2);
    float plo = 200, phi = 0;
    for (int u = 0; u < corp_units; u++) if (units[u].state == U_OK && units[u].pitch >= 0) { plo = fminf(plo, units[u].pitch); phi = fmaxf(phi, units[u].pitch); }
    check(map_count() > 140 && plo < 47.0f && phi > 79.0f, "SLOT 3, a Samplooler take: loaded and sifted (%d units, pitches %.0f..%.0f)", map_count(), (double)plo, (double)phi);
    act(P_SAVE); run(0.2f);
    check(S.job == JOB_NONE && store_frames(NBANK_OWN + 2) == 5 * AUDIO_SR, "a Samplooler slot is never written over (SAVE there does nothing)");
    starter();
    set("spce", 35);
    check(map_count() > 250 && S.material == MAT_STARTER, "TAKE > BANK STARTER: the sounds of the first version (%d units)", map_count());
}

static void sources(void)
{
    section("what REC records");
    set("srce", SRC_USB); set("tkmd", TK_NEW);
    sift_sim_usb_in = usb_tone;
    sim_rec(1.0f); run(0.5f);
    sift_sim_usb_in = NULL;
    int b4 = count_pitch(71.2f, 0.35f, -1);
    check(b4 >= map_count() - 2 && map_count() > 25, "RECORD USB: the computer's sound (%d of %d units at 500 Hz)", b4, map_count());
    starter();
    set("mode", MODE_MAP);
    set("srce", SRC_SELF); set("tkmd", TK_ADD);
    set("chao", 40);
    sim_rec(1.0f); run(0.5f);
    int t = newest_take(), ok = 0;
    for (int u = 0; u < corp_units; u++) ok += units[u].take == t && units[u].state == U_OK;
    check(ok > 15, "RECORD ITSELF: its own sound becomes grains (%d new units)", ok);
    set("srce", SRC_MIC);
    starter();
}

static void sounds(void)
{
    section("sounds");
    bank_load(BK_DAISY);
    int bad = 0;
    for (int i = 0; i < INST.npresets; i++) {
        still();
        load(INST.presets[i].name);
        bool needs_voice = (int)pv[P_CURSOR] == CUR_VOICE && (int)pv[P_MODE] == MODE_MAP;
        if (needs_voice) { tone(262.0f, 0.15f); tone_glide = 0.5f; }
        int bad0 = m_bad();
        run(2.5f);
        float r = m_rms(sim_t - 2.0f, sim_t), pk = m_peak(sim_t - 2.0f, sim_t);
        tone(0, 0);
        char key[48]; snprintf(key, sizeof key, "rms_%s", INST.presets[i].name); metric(key, r);
        if (m_bad() != bad0 || r < 0.004f || r > 0.3f || pk > 0.99f) { bad++; note("%s: rms %.4f, peak %.2f", INST.presets[i].name, (double)r, (double)pk); }
        char sfx[32]; snprintf(sfx, sizeof sfx, "-%02d", i);
        sim_wav(sfx, sim_t - 2.0f, sim_t);
    }
    check(bad == 0, "every sound (%d) plays at a sane level, nothing broken", INST.npresets);
    load("DAISY");
}

static void cpu(void)
{
    section("CPU");
    starter();
    set("mode", MODE_MAP);
    set("chao", 100); set("dens", 200); set("size", 0.8f); set("spce", 100); set("stak", 4); set("grid", 5);
    set("drve", 60); set("crsh", 40); set("fdbk", 50);
    run(1.5f);
    double avg = 1e9;                                                   // the best of three seconds: the host's own load is noise
    for (int k = 0; k < 3; k++) {
        sift_audio_ns = 0; sift_audio_blocks = 0; sift_audio_max_ns = 0;
        run(1.0f);
        double a = sift_audio_ns / (double)(sift_audio_blocks ? sift_audio_blocks : 1) / 1000.0;
        if (a < avg) avg = a;
    }
    metric("cpu_us_per_block_worst", avg);
    metric("cpu_us_per_block_max", sift_audio_max_ns / 1000.0);
    check(eng_voices_on() >= NV - 4, "the worst case: every voice busy, the dirt and the loop on (%d of %d voices)", eng_voices_on(), NV);
    check(avg < 15.0, "the sound costs %.1f us a block on the host at its busiest (budget about 15)", avg);
    set("drve", 0); set("crsh", 0); set("fdbk", 0);
    load("DAISY ROT");
    run(0.5f);
    double d = 1e9;
    for (int k = 0; k < 3; k++) {
        sift_audio_ns = 0; sift_audio_blocks = 0;
        run(1.0f);
        double a = sift_audio_ns / (double)(sift_audio_blocks ? sift_audio_blocks : 1) / 1000.0;
        if (a < d) d = a;
    }
    metric("cpu_us_per_block_daisy_rot", d);
    check(d < 15.0, "DAISY ROT (the decomposition, the loop, the rot): %.1f us a block", d);
    load("DAISY");
}

static void pictures(void)
{
    section("screens");
    load("DAISY"); still(); run(1.0f);
    sim_shot("whole");
    sim_gif_begin("sweep");
    for (int i = 0; i <= 150; i++) {
        float t = (float)i / 150.0f;
        set("chao", roundf(t * 100.0f));
        run(0.04f);
    }
    sim_gif_end();
    sim_shot("dust");
    set("chao", 0); run(0.5f);
    sim_gif_begin("tilt");
    for (int i = 0; i < 100; i++) { sim_tilt(30.0f * sinf((float)i * 0.08f), 0); run(0.04f); }
    sim_gif_end();
    still();
    starter();
    load("LATTICE");
    sim_gif_begin("play");
    for (int i = 0; i < 100; i++) { sim_tilt(18.0f * sinf((float)i * 0.04f), 10.0f * sinf((float)i * 0.03f)); run(0.04f); }
    sim_gif_end();
    sim_shot("lattice");
    load("SWARM");
    sim_gif_begin("swarm");
    for (int i = 0; i < 75; i++) { sim_tilt(10.0f * sinf((float)i * 0.05f), 0); run(0.04f); }
    sim_gif_end();
    sim_shot("swarm");
    still();
    load("DAISY");
    run(0.5f);
}

static void soak(void)
{
    section("soak");
    const char *e = getenv("SIFT_SOAK");
    float secs = e ? (float)atof(e) : 90.0f;
    const char *tags[] = { "chao", "size", "shap", "ptch", "drft", "levl", "grid", "nseq", "patt", "stak", "tune", "dens", "sprd", "scat", "revs",
                           "mapx", "mapy", "tone", "spce", "tail", "tkmd", "mode", "sped", "stut", "jump", "gaps", "bite", "drve", "crsh", "fdbk", "fbtm", "rotw" };
    srand(7);
    int bad0 = m_bad(), takes = 0, shakes = 0, loads = 0;
    float peak = 0;
    for (float t = 0; t < secs; t += 0.5f) {
        int r = rand() % 100;
        if (r < 50) { int p = P(tags[rand() % (int)(sizeof tags / sizeof tags[0])]); const param_t *d = param(p); set(d->tag, d->min + (d->max - d->min) * (float)(rand() % 1001) / 1000.0f); }
        else if (r < 62) sim_tilt((float)(rand() % 70 - 35), (float)(rand() % 70 - 35));
        else if (r < 66) { shake_it(0.2f); shakes++; }
        else if (r < 72) { tone(80.0f + (float)(rand() % 900), 0.05f + 0.3f * (float)(rand() % 100) / 100.0f); SI.rec = true; run(0.2f + (float)(rand() % 150) / 100.0f); SI.rec = false; tone(0, 0); takes++; }
        else if (r < 74) { set("bank", (float)(rand() % NBUILTIN)); act(P_LOAD); loads++; }
        else if (r < 75) { set("curs", (float)(rand() % 3)); }
        else if (r < 77) { front_midi(0xC0, (uint8_t)(rand() % INST.npresets), 0); }
        run(0.5f);
        peak = fmaxf(peak, m_peak(sim_t - 0.5f, sim_t));
    }
    settle();
    load("DAISY"); set("curs", CUR_TILT); still();
    bank_load(BK_DAISY); run(1.0f);
    float r = m_rms(sim_t - 1.0f, sim_t);
    check(m_bad() == bad0 && peak <= 1.0f && r > 0.005f, "%.0f s of everything at random (%d takes, %d shakes, %d loads): no NaN, peak %.2f, still playing (rms %.3f)", (double)secs, takes, shakes, loads, (double)peak, (double)r);
}

void inst_tests(void)
{
    boot();
    whole();
    apart();
    ear();
    tilted();
    dirt();
    amen();
    order();
    chaos();
    tilt();
    rec();
    voice();
    shake();
    linked();
    storage();
    sources();
    sounds();
    cpu();
    pictures();
    soak();
}
