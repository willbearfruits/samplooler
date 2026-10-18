// DRONE (inst_drone.h): the wall of amps under the Buddha: WRECK's AMP (instruments/wreck/firmware/main/inst_amp.c)
// at its doom end, the same engine so a MANTRA and a WRECK on the link make one amp wall.
//
// The voice: a power chord (root, fifth, octave; two strings each, a few cents apart) of PolyBLEP saws, from the
// song's chord in the TUNING's register (the root is the chord's note at or above the lowest string), through
//   the fuzz: two asymmetric clipping stages at twice the rate (between halfband filters: no aliasing to speak of),
//   the gate after it, a scooped tone stack (a low pass and a high pass blended, as the big fuzzes do) and MIDS,
//   a power amp, the wah (MOD), the cab: a high pass, the low resonance, the honk cut, a little presence, the top
//   rolled off.
// The bass (a saw an octave down, filtered and driven) and the SUB (a sine there) go around the cab. The amp's HUM goes
// into the cab. FEEDBACK is an upper partial of the root (the 12th, then two octaves, ...) fed into the fuzz: it grows
// over the drone until it sings, and climbs a partial now and then. The ORGAN holds the chord (root, fifth, octave,
// tenth) in triangles under it all.
//
// What it plays: one chord, held. It comes from the song (mantra.h: song.chord, snapped into SCALE: the song's notes
// or a darker scale), taken on the song's bar lines per CHANGE (every bar, 2, 4, 8 bars, or never: 4 bars is the pace
// of WRECK's DRONE zone, where the machine's bar is four song bars, so linked boards move together), sample exact in
// the audio task. The first chord swells in from silence over SWELL (the strings start at rest: the same drone every
// time); a new chord slides in (a 0.15 s glide); the feedback lets go and starts over at the 12th. The breath opens the
// gain (+8 dB) and lifts the swell past 1; a knock (KNOCK: HIT) or POUND (on bar lines) strikes the chord again with an
// accent; the dive sinks everything an octave; the bend is in semitones. Stopped (running false), nothing new starts and
// what rings fades. MIX > DRONE (level) is smoothed here and scales the dry and the send; the send is level x SEND x
// what the host passes.
//
// Dropped from WRECK's amp: the machine grid and its ticks, chugs, tremolo picking, the riffs, stabs, the palm mute
// (its filter stays open: the same tone), the harpsichord, the pinch squeals. Kept: everything else, its tuning table,
// its voicing and levels (the out is WRECK's at LEVEL 70, times MIX > DRONE).
#include "mantra.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"          // render() in IRAM (IRAM_ATTR), its tables in DRAM (plain static arrays)
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- params
enum {
    D_GAIN, D_VOICE, D_TUNING, D_CHANGE, D_SWELL, D_SUB, D_FEEDBACK, D_HUM, D_ORGAN,
    D_TONE, D_MIDS, D_SCALE, D_KNOCK, D_POUND, D_MOD, D_MDEPTH, D_MRATE, D_SEND,
    D_N
};
#define P(i) MP(M_DRONE, (i))
enum { V_GUITAR, V_BASS, V_BOTH };
enum { K_OFF, K_HIT };
enum { MOD_OFF, MOD_WAH, MOD_TREM };

static const char *const pages[] = { "DRONE", "DRONE 2" };
static const char *const voice_l[] = { "GUITAR", "BASS", "BOTH" };
static const char *const tuning_l[] = { "E STD", "DROP D", "DROP C#", "DROP C", "B STD", "DROP A", "DROP G" };
static const int8_t tuning_floor[] = { 40, 38, 37, 36, 35, 33, 31 };            // the lowest string, MIDI (WRECK's)
static const char *const scale_l[] = { "SONG", "MINOR", "PHRYGIAN", "HARM MINOR" };
static const uint16_t dark_scale[] = { 0, 0x5AD, 0x5AB, 0x9AD };                   // on C: natural minor, phrygian, harmonic minor
static const char *const change_l[] = { "EVERY BAR", "2 BARS", "4 BARS", "8 BARS", "NEVER" };
static const int change_bars[] = { 1, 2, 4, 8, 0 };
static const char *const knock_l[] = { "OFF", "HIT" };
static const char *const pound_l[] = { "OFF", "EVERY BAR", "2 BARS", "4 BARS" };
static const int pound_bars[] = { 0, 1, 2, 4 };
static const char *const mod_l[] = { "OFF", "WAH", "TREMOLO" };
static const char *const mrate_l[] = { "4 BARS", "2 BARS", "1 BAR", "1/2 BAR", "1 BEAT" };   // WRECK's 1 BAR .. 1/16 at DRONE
static const int mrate_beats[] = { 16, 8, 4, 2, 1 };

static const param_t params[D_N] = {
    [D_GAIN]     = { "GAIN", "dgan", 0, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "CLEAN TO WIDE OPEN" },
    [D_VOICE]    = { "VOICE", "dvoc", 0, PK_ENUM, 0, 0, 2, 1, V_BOTH, "", voice_l, "GUITAR, BASS OR BOTH" },
    [D_TUNING]   = { "TUNING", "dtun", 0, PK_ENUM, 0, 0, 6, 1, 3, "", tuning_l, "HOW LOW IT IS TUNED" },
    [D_CHANGE]   = { "CHANGE", "dchg", 0, PK_ENUM, 0, 0, 4, 1, 2, "", change_l, "WHEN THE CHORD MOVES" },
    [D_SWELL]    = { "SWELL", "dswl", 0, PK_NUM, PF_LOG, 0.5f, 12.0f, 1.12f, 3.0f, "s", NULL, "HOW SLOW IT SWELLS IN" },
    [D_SUB]      = { "SUB", "dsub", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "A SINE AN OCTAVE UNDER" },
    [D_FEEDBACK] = { "FEEDBACK", "dfbk", 0, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SINGS OVER THE CHORD" },
    [D_HUM]      = { "HUM", "dhum", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE AMP'S HUM" },
    [D_ORGAN]    = { "ORGAN", "dorg", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN ORGAN UNDER THE AMPS" },
    [D_TONE]     = { "TONE", "dton", 1, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "THE FUZZ: DARK TO BRIGHT" },
    [D_MIDS]     = { "MIDS", "dmid", 1, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "SCOOPED TO PUSHED" },
    [D_SCALE]    = { "SCALE", "dscl", 1, PK_ENUM, 0, 0, 3, 1, 0, "", scale_l, "THE SONG'S NOTES, OR DARKER" },
    [D_KNOCK]    = { "KNOCK", "dkno", 1, PK_ENUM, 0, 0, 1, 1, K_OFF, "", knock_l, "A KNOCK HITS THE CHORD" },
    [D_POUND]    = { "POUND", "dpnd", 1, PK_ENUM, 0, 0, 3, 1, 0, "", pound_l, "A HIT ON THESE BAR LINES" },
    [D_MOD]      = { "MOD", "dmod", 1, PK_ENUM, 0, 0, 2, 1, MOD_OFF, "", mod_l, "WAH OR TREMOLO" },
    [D_MDEPTH]   = { "MOD DEPTH", "dmdp", 1, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW DEEP THE MOD IS" },
    [D_MRATE]    = { "MOD RATE", "dmrt", 1, PK_ENUM, 0, 0, 4, 1, 2, "", mrate_l, "ITS CYCLE, ON THE SONG" },
    [D_SEND]     = { "SEND", "dsnd", 1, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "INTO THE ECHO AND THE HALL" },
};

// Each sound's DRONE part (the host sets MIX > DRONE: 0 for the first ten, so these say what the amps do if the player
// turns it up). Enums by index.
static const char *const sounds[MN_NSOUNDS] = {
    // OM: a clean amp with an organ under it, the sub; a chord every 4 bars
    "dgan=15 dton=55 dmid=40 dsub=40 dfbk=0 dhum=0 dorg=60 dswl=6 dchg=2 dsnd=70",
    // MONKS: a little grit, the feedback faint, slow swells
    "dgan=30 dton=45 dsub=50 dfbk=20 dhum=10 dorg=40 dswl=8 dchg=2 dsnd=70",
    // OM MANI: clean, a tremolo on the beat under the chant, the chord every bar
    "dgan=20 dton=55 dmid=45 dsub=40 dfbk=0 dhum=0 dorg=30 dswl=4 dchg=0 dmod=2 dmdp=40 dmrt=4 dsnd=60",
    // KHOOMEI: the bass alone, dropped to A: a low drone the whistle rides
    "dgan=30 dvoc=1 dtun=5 dsub=60 dfbk=30 dhum=0 dorg=0 dswl=6 dchg=2 dsnd=50",
    // SYGYT: the same, quieter and higher
    "dgan=20 dvoc=1 dtun=1 dsub=40 dfbk=20 dhum=0 dorg=0 dswl=6 dchg=2 dsnd=50",
    // KARGYRAA: a deep sub drone, DROP G, the sub full
    "dgan=40 dvoc=1 dtun=6 dsub=100 dfbk=0 dhum=20 dorg=0 dswl=8 dchg=2 dsnd=40",
    // DAISY: a light clean guitar, the chord every bar, a little tremolo
    "dgan=10 dvoc=0 dtun=1 dton=70 dmid=60 dsub=0 dfbk=0 dhum=0 dorg=0 dswl=1 dchg=0 dmod=2 dmdp=30 dmrt=4 dsnd=40",
    // DAISY 8-BIT: a bright clean organ
    "dgan=5 dvoc=0 dtun=1 dton=85 dmid=60 dsub=0 dfbk=0 dhum=0 dorg=70 dswl=0.5 dchg=0 dsnd=30",
    // VOWELS: a warm pad
    "dgan=20 dton=40 dsub=30 dfbk=20 dhum=0 dorg=40 dswl=4 dchg=2 dsnd=70",
    // HUM ALONG: a little fuzz to hum over
    "dgan=35 dsub=50 dfbk=30 dhum=20 dorg=0 dswl=3 dchg=2 dsnd=50",
    // SUNN: the fuzz wide open, DROP A, the sub, the feedback singing, the hum, a 6 s swell, the chord every 4 bars
    "dgan=100 dtun=5 dton=35 dmid=20 dsub=70 dfbk=90 dhum=70 dorg=0 dswl=6 dchg=2 dkno=1 dsnd=70",
    // DOOM DAISY: the waltz is the amps now: gain 80, DROP C, the feedback, a 3 s swell, the chord every 2 bars
    "dgan=80 dtun=3 dton=40 dmid=25 dsub=50 dfbk=60 dhum=40 dorg=0 dswl=3 dchg=1 dkno=1 dsnd=60",
    // ORGAN: the organ up, the gain low, no feedback, no hum
    "dgan=10 dton=60 dmid=50 dsub=30 dfbk=0 dhum=0 dorg=80 dswl=5 dchg=2 dsnd=80",
};

// ---------------------------------------------------------------- shared with the UI task
static dr_ctl_t ctl;                       // what the host passed last (32-bit fields: read whole enough)
static volatile uint32_t hit_n; static volatile float hit_s;
static volatile float    v_level, v_fb, v_swell;
static volatile int      v_note = -1, v_playing;

#ifndef ESP_PLATFORM
// tools/sim/test_drone.c: the drone's own output, and what it started when (not on the device)
float   *dr_tap; int dr_tap_cap, dr_tap_n;
typedef struct { uint32_t frame; int kind, note; } dr_ev_t;
dr_ev_t  dr_evlog[1024]; int dr_evn;
float    dr_pre_peak;                      // the loudest sample before the safety clip
float    dr_fb_hz, dr_fb_lvl;              // the feedback partial now (Hz) and its level
static void ev_log(uint32_t frame, int kind, int note)
{ dr_ev_t *e = &dr_evlog[dr_evn++ & 1023]; e->frame = frame; e->kind = kind; e->note = note; }
static void tap_block(const float *out, int n)
{ if (dr_tap && dr_tap_n + n <= dr_tap_cap) { memcpy(dr_tap + dr_tap_n, out, (size_t)n * sizeof(float)); dr_tap_n += n; } }
#else
#define ev_log(a, b, c) do { } while (0)
#define tap_block(o, n) do { } while (0)
#endif

// ---------------------------------------------------------------- the voice (audio task)
#define SR ((float)AUDIO_SR)
enum { NT_DRONE, NT_HIT };
enum { EV_DRONE = 300, EV_HIT = 301, EV_CHANGE = 302 };   // the event log's kinds: struck, hit, the chord moved on a bar
typedef struct {
    float pitch, target, glide;            // MIDI; glide: one-pole per sample (1: a jump)
    float bend;                            // semitones (the stick), smoothed per sample
    float pluck;                           // a hit's sharp start, semitones
    float env, peak, att, dec, sus, rel;   // into the fuzz; att per sample (linear), dec / rel one-pole
    int   stage;                           // 0 off, 1 attack, 2 decay / sustain, 3 release
    int32_t gate;                          // samples until the release, -1: held
    float gk;                              // the gate after the fuzz: open while env * gk >= 1
    float swell, swell_to, swell_k;        // after the fuzz (the drone's swell)
    float dipg, dip_step; int dip_n;       // a hit's short dip (a chord struck again through a fuzz still speaks)
    int32_t swell_hold;                    // samples the swell stays down before it rises (a chord struck over one)
    float accp;                            // a knock's accent after the fuzz, back to 1
    int   kind;
    uint32_t start;                        // the frame it started (the drone's age)
    float ph[6], phb, phf;                 // the strings; the bass and the sub; the feedback
} gvoice_t;
static gvoice_t V;

// the strings: root, fifth, octave, two each a cent or two apart (a just fifth: the fuzz's difference tones stay in
// tune), the double quieter: in mono two equal strings would cancel for seconds at the bottom of their slow beat
static float RAT[6] = { 1.0000000f, 1.0006934f, 1.4991338f, 1.5017339f, 2.0011556f, 1.9971140f };
static float WGT[6] = { 0.17f, 0.08f, 0.14f, 0.07f, 0.10f, 0.05f };
// the halfband (polyphase allpass, 3 coefficients: flat to 12 kHz, 58 dB down from 20 kHz at 64 kHz)
static float HBC[3] = { 0.114475f, 0.397837f, 0.769943f };
typedef struct { float x[3], y[3]; } hb_t;
static hb_t up_f, dn_f;

// coefficients (init)
static float k_open, k_prehp, k1hp, k2lp, k_dchp, k_tlp, k_thp, k_bass, k_hlp;
static float k_pluck, k_dive, k_dipup, k_accp, k_cab;

// filter states
static float pmz, prehp, z1hp, z2lp, dchp, tlp, thp, cabz, blp1, blp2, org_lp;
static biquad_t cab[3];
static svf_t wah;
static float g1 = 1.0f, g2 = 1.0f, g1_t = 1.0f, g2_t = 1.0f, dive_s, dive_t, bend_s, bend_t;
static float hum_c = 1.0f, hum_s, phl;    // the hum's oscillator (cos, sin); the mod LFO's phase
#define HUM_COS 0.99951809f                // 50 Hz at 32 kHz
#define HUM_SIN 0.03104242f
static uint32_t seed = 0x1badb002u;

// block ramps: a value that moves linearly across a block towards where a block-rate one-pole puts it
typedef struct { float v, d; } ramp_t;
static inline void ramp(ramp_t *r, float target, float k, int n)
{
    float e = target - r->v;
    if (fabsf(e) < 2e-5f) { r->v = target; r->d = 0.0f; }      // there: snap (no endless tail, no denormals)
    else r->d = k * e / (float)n;
}
static inline void ramp_skip(ramp_t *r, int n) { r->v += r->d * (float)n; }
static ramp_t r_lvl, r_gtr, r_bass, r_sub, r_org, r_hum, r_send, r_mk, r_tone, r_wah, r_trem, r_fb;
static float mids_s = -1.0f, mids_set = -100.0f, breath_s;

// feedback
static int   fb_h = 3, fb_next;            // the partial (3: the 12th); climb to the next one
static float fb_wob;
static int   fb_partials[4] = { 3, 4, 6, 4 };
static uint32_t fb_since;                  // the frame the partial began (the strike, or its last climb)

// the organ (WRECK's HV voices in organ mode)
#define NH 4
typedef struct { float ph, inc, env, amp, att, ldk; int stage; } hv_t;   // ldk: log2 of the decay per sample
static hv_t HV[NH];

// what it plays
static int   h_root_pc = 9;                // the chord's root (pitch class), in the amp's scale
static uint16_t h_mask = 0x0FFF;
static int8_t h_iv[12]; static int h_n = 7;
static int32_t bar_window;                 // frames after a bar line in which a late chord (the song moves in the UI task) is still taken
static int32_t take_wait;                  // frames until the bar's chord is taken (without host_degree_at: a tick's lag after the line)
static int   quiet_n;                      // samples since the voice was last heard (the tails, then sleep)
static bool  was_live;
static uint32_t hit_seen;
static int   tun_was = -1;

static inline float wrapf(float p) { p -= (float)(int)p; return p < 0.0f ? p + 1.0f : p; }

// ---------------------------------------------------------------- the harmony
static uint16_t amp_mask(void)
{
    int s = (int)P(D_SCALE);
    if (s <= 0 || s > 3) return song.scale_mask ? song.scale_mask : 0x0FFF;
    int k = ((song.key % 12) + 12) % 12;
    uint16_t m = dark_scale[s];
    return (uint16_t)(((m << k) | (m >> (12 - k))) & 0x0FFF);
}
static int snap(int n, uint16_t mask)       // the nearest note in the mask, lower first
{
    for (int d = 0; d < 6; d++) {
        if (mask & (1u << (((n - d) % 12 + 12) % 12))) return n - d;
        if (mask & (1u << (((n + d) % 12 + 12) % 12))) return n + d;
    }
    return n;
}
static int chord_pc(uint16_t m)            // the song's chord root, snapped into the amp's scale
{
    int pc = ((song.chord[0] % 12) + 12) % 12;
    return ((snap(pc + 24, m) % 12) + 12) % 12;
}
// WRECK's host has host_degree_at(frame): the chord's degree exact at a frame (song.degree lags a tick on a bar line,
// the song moving in the UI task). MANTRA's host may add it; until then the bar's chord is taken as it stands and a
// late one is still taken in the bar window (dr_process).
int host_degree_at(uint32_t frame) __attribute__((weak));
static int chord_pc_at(uint16_t m, uint32_t frame)
{
    int pc = host_degree_at ? ((scale_note(host_degree_at(frame), 3) % 12) + 12) % 12 : ((song.chord[0] % 12) + 12) % 12;
    return ((snap(pc + 24, m) % 12) + 12) % 12;
}
static void latch_pc(int pc)
{
    h_root_pc = pc;
    int n = 0;
    for (int i = 0; i < 12; i++) if (h_mask & (1u << ((h_root_pc + i) % 12))) h_iv[n++] = (int8_t)i;
    if (n < 3) { static const int8_t pw[3] = { 0, 7, 12 }; n = 3; memcpy(h_iv, pw, 3); }
    h_n = n;
}
static void latch(void) { h_mask = amp_mask(); latch_pc(chord_pc(h_mask)); }                        // the harmony now
static void latch_at(uint32_t frame) { h_mask = amp_mask(); latch_pc(chord_pc_at(h_mask, frame)); }   // ... at a bar line
static int floor_note(void) { return tuning_floor[clampi((int)P(D_TUNING), 0, 6)]; }
static int root_in(int fl) { return fl + ((h_root_pc - fl) % 12 + 12) % 12; }
static int root_note(void) { return root_in(floor_note()); }
static int scale_up(int k) { return 12 * (k / h_n) + h_iv[k % h_n]; }      // k scale steps above the root (k >= 0)

// ---------------------------------------------------------------- notes
static void release(float tau)
{
    if (V.stage == 1 || V.stage == 2) { V.stage = 3; V.rel = onepole_s(tau); }
}

// The strings at rest: the same phases at every strike from silence, so a chord struck from silence sounds the same
// every time (the doubled strings beat over seconds: left where the last chord ended, a drone's first seconds and its
// feedback's rise would depend on the whole history). Only from silence: a chord over a ringing one keeps its phases.
static void strings_rest(gvoice_t *v)
{
    for (int k = 0; k < 6; k++) v->ph[k] = 0.137f * (float)k + 0.05f;
    v->phb = 0.0f; v->phf = 0.0f;
}

static void play(int note, int kind, float acc, int32_t len, uint32_t frame)
{
    gvoice_t *v = &V;
    bool ringing = v->stage != 0 && v->env > 0.03f;
    if (v->stage == 0 || v->env < 1e-2f) { strings_rest(v); fb_wob = 0.0f; }   // (and the feedback's wobble starts over)
    v->target = (float)note;
    float glide = (kind == NT_DRONE && ringing) ? 0.45f : 0.0f;
    if (glide > 0.0f) v->glide = onepole_s(glide * 0.33f);
    else { v->glide = 1.0f; v->pitch = v->target; }
    float att, dec, sus, rel, gk, dip;
    if (kind == NT_DRONE) { att = 0.4f; dec = 0.0f; sus = 1.0f; rel = 1.2f; gk = 3.0f; dip = 0.0f; }
    else                  { att = 0.001f; dec = 2.5f; sus = 0.5f; rel = 0.3f; gk = 3.0f; dip = 0.5f; }
    v->peak = acc;
    v->att = acc / (att * SR);
    v->stage = 1;
    v->dec = dec > 0.0f ? onepole_s(dec) : 0.0f;
    v->sus = sus * acc;
    v->pluck = (kind == NT_DRONE ? 0.0f : 0.12f) * acc;
    if (dip > 0.0f && ringing) { v->dip_n = 24; v->dip_step = (v->dipg - (1.0f - dip)) / 24.0f; }
    v->start = frame;
    v->rel = onepole_s(rel);
    v->gk = gk;
    v->gate = len;
    v->kind = kind;
    if (kind == NT_DRONE) {
        if (!ringing) v->swell = 0.0f;
        v->swell_to = 1.0f; v->swell_k = onepole_s(P(D_SWELL) * 0.33f);
    } else { v->swell_to = 1.0f; v->swell_k = onepole_s(0.004f); }
    v->swell_hold = 0;
    if (kind == NT_HIT) { v->accp = 1.0f + 0.7f * acc; v->swell_k = onepole_s(0.003f); }
    quiet_n = 0;
}

static void organ_chord(void)              // the organ holds root, fifth, octave, tenth (in the amp's scale)
{
    int base = 48 + ((h_root_pc - 48) % 12 + 12) % 12;
    int notes[4] = { base, base + scale_up(4), base + 12, base + 12 + scale_up(2) };
    for (int k = 0; k < NH; k++) {
        hv_t *h = &HV[k];
        bool held = h->stage == 1 || h->stage == 2;
        h->inc = 0.01375f * fast_exp2(((float)notes[k] - 69.0f) * (1.0f / 12.0f));
        if (!held) { h->stage = 1; h->amp = 1.0f; h->att = 1.0f / (0.6f * SR); h->ldk = 0.0f; }
    }
}
static void organ_off(void) { for (int k = 0; k < NH; k++) if (HV[k].stage) { HV[k].stage = 3; HV[k].ldk = -1.442695f / (0.5f * SR); } }
static bool organ_held(void) { for (int k = 0; k < NH; k++) if (HV[k].stage == 1 || HV[k].stage == 2) return true; return false; }

static void drone_on(uint32_t frame, bool enter)   // the chord: struck (enter: from nothing, or over a hit) or moved
{
    play(root_note(), NT_DRONE, 1.0f, -1, frame);
    fb_since = frame;
    if (enter) { fb_h = 3; fb_next = 0; }                          // a new drone: the feedback starts at the 12th
    if (enter && V.swell > 0.15f) {                                // a note rings: it dips, then the chord swells in
        V.swell_to = 0.1f; V.swell_k = onepole_s(0.02f); V.swell_hold = (int32_t)(0.08f * SR);
    }
    if (P(D_ORGAN) > 0.5f) organ_chord();
    ev_log(frame, EV_DRONE, root_note());
}
static void hit(float s, uint32_t frame)   // a knock, or POUND: the chord struck again, hard
{
    s = clampf(s, 0.0f, 1.0f);
    if ((V.stage == 1 || V.stage == 2) && V.kind == NT_DRONE) {
        V.accp = 1.0f + 0.8f * s; V.dip_n = 24; V.dip_step = (V.dipg - 0.5f) / 24.0f; V.swell_k = onepole_s(0.01f); V.pluck = 0.15f;
    } else play(root_note(), NT_HIT, 0.7f + 0.3f * s, (int32_t)(1.5f * SR), frame);
    ev_log(frame, EV_HIT, root_note());
}

// the bar's chord is taken: a new one slides in
static void take_now(uint32_t frame)
{
    int was = root_note();
    latch_at(frame); bar_window = 720;                             // the song's chord may still come later (the link)
    bool moved = root_note() != was;
    if (moved && V.stage) fb_next = 1;                             // a new chord: the feedback lets go
    if (moved || V.stage == 0 || V.stage == 3 || V.kind != NT_DRONE) drone_on(frame, V.kind != NT_DRONE);
    else if (P(D_ORGAN) > 0.5f) organ_chord();
    if (moved) ev_log(frame, EV_CHANGE, root_note());
}
// a song bar line (bar: counted from the song's beat 0), while the drone is live
static void on_bar(uint32_t bar, uint32_t frame)
{
    int per = change_bars[clampi((int)P(D_CHANGE), 0, 4)];
    bool take = per > 0 && bar % (uint32_t)per == 0;
    bool climb_bar = per > 0 ? take : bar % 4 == 0;
    if (take) {
        if (host_degree_at) take_now(frame);                       // exact
        else take_wait = 384;                                      // 12 ms: the song's chord is a tick late at most
    }
    if (climb_bar && (int32_t)(frame - fb_since) > (int32_t)(12.0f * SR)) fb_next = 1;   // sung a while: it climbs a partial
    int pp = pound_bars[clampi((int)P(D_POUND), 0, 3)];
    if (pp > 0 && bar % (uint32_t)pp == 0) hit(0.8f, frame);
}

// ---------------------------------------------------------------- audio
static inline float clip3(float x)        // smooth: slope 1 at 0, |y| <= 1 (reached at |x| = 1.5)
{
    x = fminf(fmaxf(x, -1.5f), 1.5f);
    return x - 0.14814815f * x * x * x;
}
static inline float sin01(float p)        // sin(2 pi p) for p in 0..1 (sin_turns without the floor)
{
    float x = 2.0f * p - 1.0f, y = 4.0f * x * (1.0f - fabsf(x));
    return -(0.225f * (y * fabsf(y) - y) + y);
}
// the fuzz's two stages are biased, as a transistor is: one clips the negative side first, the other the positive
#define BIAS1 (-0.35f)
#define BIAS2 0.3f

typedef struct { bool str, cab, bass, org, wah, trem, fb, hum; float fb_ratio, dr, lfo_inc; } rflags_t;

// One segment of a block, in passes (each a short loop the CPU can run many samples of at once): the voice's
// controls, the strings, into the fuzz and up to 64 kHz, the fuzz, down and the tone, the cab, the bass and the
// sub, the organ and the output.
static IRAM_ATTR __attribute__((noinline)) void render(float *restrict out, float *restrict send, int n, const rflags_t *restrict F, float *restrict pk)
{
    // the work arrays (static: the audio task's stack stays small; render is never re-entered)
    static float inc[AUDIO_BLOCK], env[AUDIO_BLOCK], post[AUDIO_BLOCK], a[AUDIO_BLOCK], up[2 * AUDIO_BLOCK], fbs[AUDIO_BLOCK];
    gvoice_t *v = &V;
    const float fn = (float)n;
    // ---- 1: the voice's controls: the pitch (glide, the bend, the pluck, the dive), the envelope into the fuzz,
    //      the gate and the swell after it
    {
        float pitch = v->pitch, target = v->target, glide = v->glide, bend = bend_s, bt = bend_t, pluck = v->pluck, ds = dive_s, dt_ = dive_t;
        float e = v->env, sw = v->swell, swt = v->swell_to, swk = v->swell_k;
        float dip = v->dipg, accp = v->accp, gk = v->gk;
        int stage = v->stage; int32_t gate_n = v->gate; int dipn = v->dip_n;
        const float att = v->att, peak = v->peak, dec = v->dec, sus = v->sus, rel = v->rel, dst = v->dip_step;
        float sref = pitch + bend + pluck - 12.0f * ds, iref = 0.01375f * fast_exp2((sref - 69.0f) * (1.0f / 12.0f));
        for (int i = 0; i < n; i++) {
            pitch += glide * (target - pitch);
            bend += k_dive * (bt - bend);
            pluck *= k_pluck;
            ds += k_dive * (dt_ - ds);
            float semi = pitch + bend + pluck - 12.0f * ds;
            float u = semi - sref;                                // 2^(u/12) near the segment's pitch: a Taylor step
            if (fabsf(u) > 1.5f) { sref = semi; iref = 0.01375f * fast_exp2((semi - 69.0f) * (1.0f / 12.0f)); u = 0.0f; }
            inc[i] = iref * (1.0f + u * (0.057762265f + u * (0.0016682396f + u * 3.2120e-5f)));
            switch (stage) {
            case 1: e += att; if (e >= peak) { e = peak; stage = 2; } break;
            case 2: e += dec * (sus - e); break;
            case 3: e -= rel * e; if (e < 1e-4f) { e = 0.0f; stage = 0; } break;
            default: break;
            }
            if (gate_n > 0 && (stage == 1 || stage == 2) && --gate_n == 0) stage = 3;
            sw += swk * (swt - sw);
            if (dipn > 0) { dipn--; dip -= dst; } else dip += k_dipup * (1.0f - dip);
            accp += k_accp * (1.0f - accp);
            float gate = e * gk; gate = gate > 1.0f ? 1.0f : gate;
            env[i] = e;
            post[i] = gate * sw * dip * accp;
        }
        if (pluck < 1e-6f) pluck = 0.0f;
        if (fabsf(ds - dt_) < 1e-6f) ds = dt_;
        if (fabsf(bend - bt) < 1e-6f) bend = bt;
        v->pitch = pitch; v->pluck = pluck; dive_s = ds; bend_s = bend; v->env = e; v->stage = stage;
        v->gate = gate_n; v->dip_n = dipn;
        v->swell = sw; v->dipg = dip; v->accp = accp;
    }
    float lvl0 = r_lvl.v, lvld = r_lvl.d;
    if (F->str) {
        // ---- 2: the strings
        {
            float p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], p4 = v->ph[4], p5 = v->ph[5];
            const float r0 = RAT[0], r1 = RAT[1], r2 = RAT[2], r3 = RAT[3], r4 = RAT[4], r5 = RAT[5];
            const float w0 = WGT[0], w1 = WGT[1], w2 = WGT[2], w3 = WGT[3], w4 = WGT[4], w5 = WGT[5];
#define STRING(p, r) ({ float d_ = inc[i] * (r); p += d_; if (p >= 1.0f) p -= 1.0f; saw_blep(p, d_); })
            for (int i = 0; i < n; i++)
                a[i] = w0 * STRING(p0, r0) + w1 * STRING(p1, r1) + w2 * STRING(p2, r2) + w3 * STRING(p3, r3) + w4 * STRING(p4, r4) + w5 * STRING(p5, r5);
#undef STRING
            v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3; v->ph[4] = p4; v->ph[5] = p5;
        }
        // ---- 3: into the fuzz: the level, the input's tone (WRECK's palm mute, open), the feedback (a partial of
        //      the string), the input's high pass, the gain; up to 64 kHz
        {
            float z = pmz, h = prehp, fb = r_fb.v, fbd = r_fb.d, phf = v->phf, gg = g1, gd = (g1_t - g1) * 0.25f / fn;
            const float hk = k_prehp, hm = 1.0f - k_prehp, c0 = HBC[0], c1 = HBC[1], c2 = HBC[2], ko = k_open, km = 1.0f - k_open;
            float ux[3], uy[3]; memcpy(ux, up_f.x, sizeof ux); memcpy(uy, up_f.y, sizeof uy);
            for (int i = 0; i < n; i++) {
                float x = a[i] * env[i];
                z = z * km + ko * x; x = z;
                if (F->fb) { fb += fbd; phf += inc[i] * F->fb_ratio; if (phf >= 1.0f) phf -= 1.0f; fbs[i] = fb * sin01(phf); x += 0.32f * fbs[i]; }
                h = h * hm + hk * x; x -= h;
                gg += gd;
                x *= gg;
                float p = x, q = x, t;
                t = (c0 * p + ux[0]) - c0 * uy[0]; ux[0] = p; uy[0] = t; p = t;
                t = (c1 * q + ux[1]) - c1 * uy[1]; ux[1] = q; uy[1] = t; q = t;
                t = (c2 * p + ux[2]) - c2 * uy[2]; ux[2] = p; uy[2] = t; p = t;
                up[2 * i] = p; up[2 * i + 1] = q;
            }
            pmz = z; prehp = h; v->phf = phf; g1 = gg;
            memcpy(up_f.x, ux, sizeof ux); memcpy(up_f.y, uy, sizeof uy);
        }
        // ---- 4: the fuzz at 64 kHz: stage 1 clips the negative side first, the coupling cap takes the asymmetry's
        //      DC, a little low pass, stage 2 clips the positive side first
        {
            float h1 = z1hp, l2 = z2lp, gg = g2, gd = (g2_t - g2) * 0.25f / (2.0f * fn);
            const float a1 = k1hp, m1 = 1.0f - k1hp, a3 = k2lp, m3 = 1.0f - k2lp, c1 = clip3(BIAS1), c2 = clip3(BIAS2);
            for (int j = 0; j < 2 * n; j++) {
                float x = up[j];
                float y = clip3(x + BIAS1) - c1;
                h1 = h1 * m1 + a1 * y; y -= h1;                         // the coupling cap: the bias's DC goes
                gg += gd;
                y = clip3(y * gg + BIAS2) - c2;
                l2 = l2 * m3 + a3 * y;
                up[j] = l2;
            }
            z1hp = h1; z2lp = l2; g2 = gg;
        }
        // ---- 5: back to 32 kHz, the DC, the tone stack (a low pass and a high pass blended: the scoop between them),
        //      MIDS, the gate and the swell, the power amp, the wah, VOICE
        {
            float dx[3], dy[3]; memcpy(dx, dn_f.x, sizeof dx); memcpy(dy, dn_f.y, sizeof dy);
            float dc = dchp, lp = tlp, hp = thp, tn = r_tone.v, tnd = r_tone.d, mk = r_mk.v, mkd = r_mk.d;
            const float c0 = HBC[0], c1 = HBC[1], c2 = HBC[2], dk = k_dchp, dm = 1.0f - k_dchp;
            const float lk = k_tlp, lm = 1.0f - k_tlp, hk = k_thp, hm = 1.0f - k_thp;
            float gtd = r_gtr.d, wd = r_wah.v, wdd = F->wah ? r_wah.d : 0.0f;
            for (int i = 0; i < n; i++) {
                float p = up[2 * i + 1], q = up[2 * i], t;
                t = (c0 * p + dx[0]) - c0 * dy[0]; dx[0] = p; dy[0] = t; p = t;
                t = (c1 * q + dx[1]) - c1 * dy[1]; dx[1] = q; dy[1] = t; q = t;
                t = (c2 * p + dx[2]) - c2 * dy[2]; dx[2] = p; dy[2] = t; p = t;
                float d = 0.5f * (p + q);
                dc = dc * dm + dk * d; d -= dc;
                lp = lp * lm + lk * d; hp = hp * hm + hk * d;
                tn += tnd; mk += mkd;
                float y = (1.0f - tn) * lp * 1.6f + tn * (d - hp) * 2.2f;
                a[i] = y * post[i] * mk;                                          // the gate
            }
            if (F->fb) for (int i = 0; i < n; i++) a[i] += 0.35f * fbs[i] * post[i];   // the feedback sings over the fuzz
            for (int i = 0; i < n; i++) a[i] = 1.15f * clip3(a[i] * 0.87f);             // the power amp
            if (F->wah) {
                svf_t w = wah;
                for (int i = 0; i < n; i++) { wd += wdd; float l, b = svf_bp(&w, a[i], &l); a[i] += wd * (2.2f * b + 0.25f * l - a[i]); }
                wah = w;
            }
            float gt = r_gtr.v;
            for (int i = 0; i < n; i++) { gt += gtd; a[i] *= 0.3f * gt; }          // VOICE; the guitar's level
            memcpy(dn_f.x, dx, sizeof dx); memcpy(dn_f.y, dy, sizeof dy);
            dchp = dc; tlp = lp; thp = hp;
        }
    } else for (int i = 0; i < n; i++) a[i] = 0.0f;
    // ---- 6: the hum (50 Hz, the rectifier's buzz, a hiss), the cab, the tremolo
    if (F->cab) {
        if (F->hum) {
            float hv = r_hum.v, hd = r_hum.d, c = hum_c, sn = hum_s;
            uint32_t sd = seed;
            for (int i = 0; i < n; i++) {
                hv += hd;
                float c2 = c * HUM_COS - sn * HUM_SIN; sn = sn * HUM_COS + c * HUM_SIN; c = c2;
                a[i] += hv * (0.6f * sn + 0.5f * (fabsf(sn) - 0.6366f) + 0.05f * rng_bi(&sd)) * 0.07f;
            }
            float m = 1.5f - 0.5f * (c * c + sn * sn);                  // keep it on the circle
            hum_c = c * m; hum_s = sn * m; seed = sd;
        }
        biquad_t c0 = cab[0], c1 = cab[1], c2 = cab[2];
        float cz = cabz;
        for (int i = 0; i < n; i++) {
            float t = biquad_run(&c2, biquad_run(&c1, biquad_run(&c0, a[i])));
            cz = cz * (1.0f - k_cab) + k_cab * t;
            a[i] = cz;
        }
        cab[0] = c0; cab[1] = c1; cab[2] = c2; cabz = cz;
        if (F->trem) {
            float tv = r_trem.v, td = r_trem.d, ph = phl;
            for (int i = 0; i < n; i++) { tv += td; ph += F->lfo_inc; if (ph >= 1.0f) ph -= 1.0f; a[i] *= 1.0f - tv * (0.5f + 0.5f * sin01(ph)); }
            phl = ph;
        }
    }
    // ---- 7: the bass (a saw an octave down, filtered, driven) and the sub (a sine there), around the cab
    if (F->bass) {
        float ph = v->phb, b1 = blp1, b2 = blp2, bv = r_bass.v, bd = r_bass.d, sv = r_sub.v, sd = r_sub.d, drv = 1.0f + 0.25f * g2;
        bool saw = bv > 1e-5f || bd != 0.0f;
        for (int i = 0; i < n; i++) {
            float dt = inc[i] * 0.5f;
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            bv += bd; sv += sd;
            float b = 0.0f;
            if (saw) {
                float x = saw_blep(ph, dt) * env[i];
                b1 = b1 * (1.0f - k_bass) + k_bass * x; b2 = b2 * (1.0f - k_bass) + k_bass * b1;
                b = clip3(b2 * drv) * 0.2f * bv;
            }
            a[i] += (b + sin01(ph) * 0.11f * sv) * post[i];
        }
        v->phb = ph; blp1 = b1; blp2 = b2;
    }
    // ---- 8: the level, the organ, a safety net, out
    for (int i = 0; i < n; i++) a[i] *= lvl0 + lvld * (float)(i + 1);
    if (F->org) {
        // each voice's envelope across the segment is a ramp (exact for the attack, linear between the decay's ends)
        static float osum[AUDIO_BLOCK];
        float ph[NH], d[NH], e0[NH], sl[NH], cap[NH];
        bool org = false;
        for (int k = 0; k < NH; k++) {
            hv_t *h = &HV[k];
            ph[k] = h->ph; d[k] = h->stage ? h->inc * F->dr : 0.0f;
            if (!h->stage) { e0[k] = sl[k] = 0.0f; cap[k] = 0.0f; continue; }
            org = true;
            float e = h->env, end;
            if (h->stage == 1) {
                end = fminf(1.0f, e + h->att * fn);
                e0[k] = e * h->amp; sl[k] = h->att * h->amp; cap[k] = h->amp;
                if (end >= 1.0f) h->stage = 2;
            } else {
                end = e * fast_exp2(fn * h->ldk);
                e0[k] = e * h->amp; sl[k] = (end - e) * h->amp / fn; cap[k] = 1e9f;
                if (end < 1e-4f) { end = 0.0f; h->stage = 0; }
            }
            h->env = end;
        }
        if (org) {                                                     // the organ: triangles (flute-like, nothing to band-limit)
            float p0 = ph[0], p1 = ph[1], p2 = ph[2], p3 = ph[3];
            for (int i = 0; i < n; i++) {
                float fi = (float)(i + 1), sum = 0.0f;
#define TRI(p, k) do { p += d[k]; if (p >= 1.0f) p -= 1.0f; sum += (4.0f * fabsf(p - 0.5f) - 1.0f) * fminf(cap[k], e0[k] + sl[k] * fi); } while (0)
                TRI(p0, 0); TRI(p1, 1); TRI(p2, 2); TRI(p3, 3);
#undef TRI
                osum[i] = sum;
            }
            HV[0].ph = p0; HV[1].ph = p1; HV[2].ph = p2; HV[3].ph = p3;
            float ol = org_lp, go = 0.06f * r_org.v;
            for (int i = 0; i < n; i++) {
                ol = ol * (1.0f - k_hlp) + k_hlp * osum[i];
                a[i] += go * ol * (lvl0 + lvld * (float)(i + 1));
            }
            org_lp = ol;
        }
    }
    float sv = r_send.v, sd = r_send.d, p = *pk;
    for (int i = 0; i < n; i++) {
        float o = a[i], m = fabsf(o);
        p = m > p ? m : p;
        o = 0.9f * clip3(o * 1.1111111f);                           // a safety net: never past 0.9
        sv += sd;
        out[i] += o;
        send[i] += o * sv;
    }
    *pk = p;
    // the ramps move on
    r_lvl.v += lvld * fn; r_send.v = sv; r_tone.v += r_tone.d * fn; r_mk.v += r_mk.d * fn; r_gtr.v += r_gtr.d * fn;
    r_bass.v += r_bass.d * fn; r_sub.v += r_sub.d * fn; r_hum.v += r_hum.d * fn; r_fb.v += r_fb.d * fn;
    if (F->wah) r_wah.v += r_wah.d * fn;
    if (F->trem) r_trem.v += r_trem.d * fn;
    if (F->org) r_org.v += r_org.d * fn;
}

static void flush_states(void)             // recursive states that decay: never denormal
{
    float *z[] = { &pmz, &prehp, &z1hp, &z2lp, &dchp, &tlp, &thp, &cabz, &blp1, &blp2, &org_lp,
                   &cab[0].z1, &cab[0].z2, &cab[1].z1, &cab[1].z2, &cab[2].z1, &cab[2].z2, &wah.ic1, &wah.ic2 };
    for (unsigned k = 0; k < sizeof z / sizeof z[0]; k++) *z[k] = flush_denormal(*z[k]);
    for (int k = 0; k < 3; k++) { up_f.x[k] = flush_denormal(up_f.x[k]); up_f.y[k] = flush_denormal(up_f.y[k]);
                                  dn_f.x[k] = flush_denormal(dn_f.x[k]); dn_f.y[k] = flush_denormal(dn_f.y[k]); }
}

static void flags(rflags_t *F)
{
    bool voice_on = V.stage != 0;
    bool tails = voice_on || quiet_n < 6000;                       // the filters ring a little after the note
    F->str = tails && (r_gtr.v > 1e-4f || r_gtr.d != 0.0f);
    F->hum = r_hum.v > 1e-5f || r_hum.d != 0.0f;
    F->fb = r_fb.v > 1e-5f || r_fb.d != 0.0f;
    if (F->fb && !F->str) F->str = r_gtr.v > 1e-4f || r_gtr.d != 0.0f;
    F->cab = F->str || F->hum || quiet_n < 9000;
    F->bass = tails && (r_bass.v > 1e-4f || r_bass.d != 0.0f || r_sub.v > 1e-4f || r_sub.d != 0.0f);
    F->org = r_org.d != 0.0f;
    for (int k = 0; k < NH; k++) if (HV[k].stage) F->org = true;
    F->wah = F->str && (r_wah.v > 1e-4f || r_wah.d != 0.0f);
    F->trem = F->cab && (r_trem.v > 1e-4f || r_trem.d != 0.0f);
}

// Once a block: in flash (CORE.md). The per-sample work is render(), in IRAM.
void dr_process(float *out, float *send, int n)
{
    uint32_t f0 = audio_frames();
    uint32_t fpb = song.fpb ? song.fpb : 19200, origin = song.origin;
    bool run = ctl.running;
    float level_t = clampf(ctl.level, 0.0f, 1.0f);
    bool live = run && level_t > 0.001f;                           // a chord is held: MIX > DRONE up and the song running
    // ---- the song's bar line in this block, if any (bars counted from the song's beat 0, as WRECK's grid counts)
    uint32_t bar_len = fpb * (uint32_t)(song.bar_beats > 0 ? song.bar_beats : 4);
    int32_t  pos = mn_mod((int32_t)(f0 - origin), (int32_t)(fpb * SONG_WRAP_BEATS));
    uint32_t bar = (uint32_t)pos / bar_len, in_bar = (uint32_t)pos % bar_len;
    int bar_at = -1;                                               // the bar line's offset in this block, -1: none
    if (in_bar == 0) bar_at = 0;
    else if (in_bar + (uint32_t)n > bar_len) { bar_at = (int)(bar_len - in_bar); bar++; }
    // ---- live, or not: the first chord is struck (from silence: the same every time); stopped, what rings fades
    if (live && !was_live) { latch(); drone_on(f0, true); }
    else if (!live && was_live) { release(run ? 0.5f : 0.15f); organ_off(); bar_window = 0; take_wait = 0; }   // (MIX at 0: its ramp hides the fade)
    was_live = live;
    if (live && (V.stage == 0 || V.stage == 3)) { latch(); drone_on(f0, V.kind != NT_DRONE); }   // a hit's note has let go: the chord comes back
    // ---- a knock: a chord hit
    uint32_t hn = hit_n;
    if (hn != hit_seen) {
        hit_seen = hn;
        if (live && (int)P(D_KNOCK) == K_HIT) hit(hit_s, f0);
    }
    // ---- the bar's chord, a tick after its line (the song has moved by then)
    if (take_wait > 0 && (take_wait -= n) <= 0) { if (live) take_now(f0); }
    // ---- the drone's dip is over: swell
    if (V.swell_hold > 0 && (V.swell_hold -= n) <= 0) { V.swell_to = 1.0f; V.swell_k = onepole_s(P(D_SWELL) * 0.33f); }
    // ---- a chord that came late for its bar line (the song moves in the UI task): take it while the glide is young
    if (bar_window > 0) {
        bar_window -= n;
        if (live && chord_pc(amp_mask()) != h_root_pc) {
            int was = root_note();
            latch();
            int d = root_note() - was;
            if (V.stage) { V.target += (float)d; if (V.glide >= 1.0f) V.glide = onepole_s(0.45f * 0.33f); fb_next = 1; ev_log(f0, EV_CHANGE, root_note()); }
            if (P(D_ORGAN) > 0.5f) organ_chord();
        }
    }
    // ---- a new key or scale: now, not on the bar
    if (live && amp_mask() != h_mask) {
        int was = root_note();
        latch();
        if (V.stage) { V.target += (float)(root_note() - was); V.glide = onepole_s(0.03f); }
        if (P(D_ORGAN) > 0.5f) organ_chord();
    }
    // ---- the tuning moved: slide there
    int tun = clampi((int)P(D_TUNING), 0, 6);
    if (tun != tun_was) {
        if (tun_was >= 0 && V.stage) { V.target += (float)(root_note() - root_in(tuning_floor[tun_was])); V.glide = onepole_s(0.03f); }
        tun_was = tun;
    }
    // ---- ORGAN turned up or down while it drones
    if (live && V.kind == NT_DRONE && V.stage && V.stage != 3) {
        bool held = organ_held();
        if (P(D_ORGAN) > 0.5f && !held) organ_chord();
        else if (P(D_ORGAN) <= 0.5f && held) organ_off();
    }
    // ---- block-rate settings
    breath_s += 0.1f * (clampf(ctl.swell, 0.0f, 1.0f) - breath_s);
    if (breath_s < 1e-4f) breath_s = 0.0f;
    float gain01 = P(D_GAIN) * 0.01f;
    float gdb = 48.0f * gain01 + 8.0f * breath_s;                  // the breath opens the gain a little
    g1_t = exp2f(gdb * 0.16609640f * 0.55f);
    g2_t = exp2f(gdb * 0.16609640f * 0.45f);
    dive_t = clampf(ctl.dive, 0.0f, 1.0f);
    bend_t = clampf(ctl.bend, -24.0f, 24.0f);
    if (live && V.kind == NT_DRONE && V.stage == 2 && V.swell_hold <= 0) V.swell_to = 1.0f + 0.3f * breath_s;   // ... and lifts the swell
    int voice = (int)P(D_VOICE);
    const float kb = 0.12f;                                        // block one-pole: about 30 ms
    ramp(&r_lvl, 0.7f * level_t, kb, n);                           // WRECK's LEVEL 70, times MIX > DRONE
    ramp(&r_gtr, voice != V_BASS ? 1.0f : 0.0f, kb, n);
    ramp(&r_bass, voice != V_GUITAR ? 1.0f : 0.0f, kb, n);
    ramp(&r_sub, P(D_SUB) * 0.01f, kb, n);
    ramp(&r_org, P(D_ORGAN) * 0.01f, kb, n);
    ramp(&r_send, clampf(level_t * P(D_SEND) * 0.01f * clampf(ctl.send, 0.0f, 1.0f), 0.0f, 1.0f), kb, n);
    { float u = (1.0f - gain01) * (1.0f - gain01); ramp(&r_mk, 1.0f + 3.0f * u * u, kb, n); }   // a clean amp as loud as a fuzzed one
    ramp(&r_tone, 0.12f + 0.76f * P(D_TONE) * 0.01f, kb, n);
    ramp(&r_hum, live ? P(D_HUM) * 0.01f : 0.0f, 0.03f, n);
    float m_t = P(D_MIDS) * 0.01f;
    mids_s = mids_s < 0.0f ? m_t : mids_s + 0.15f * (m_t - mids_s);
    int mod = (int)P(D_MOD);
    float depth = P(D_MDEPTH) * 0.01f;
    ramp(&r_wah, mod == MOD_WAH ? depth : 0.0f, kb, n);
    ramp(&r_trem, mod == MOD_TREM ? 0.9f * depth : 0.0f, kb, n);
    // the LFO, locked to the song (it slews to the song's phase: a new rate never jumps)
    float lfo_inc;
    {
        uint32_t cyc = fpb * (uint32_t)mrate_beats[clampi((int)P(D_MRATE), 0, 4)];
        float ph_t = (float)((uint32_t)pos % cyc) / (float)cyc;
        lfo_inc = 1.0f / (float)cyc;
        float e = ph_t - phl; e -= (float)(int)(e + (e >= 0.0f ? 0.5f : -0.5f)); phl = wrapf(phl + 0.08f * e);
    }
    // ---- the feedback: it grows over the drone, climbs a partial now and then
    float fb_t = 0.0f;
    if (live && V.stage && V.kind == NT_DRONE) {
        float age = (float)(f0 - V.start) / SR;
        fb_t = P(D_FEEDBACK) * 0.01f * clampf((age - 1.0f) / 5.0f, 0.0f, 1.0f);
        fb_wob = wrapf(fb_wob + (float)n * (0.13f / SR));
        fb_t *= 0.85f + 0.15f * sin_turns(fb_wob);
        if (fb_next) {
            fb_t = 0.0f;
            if (r_fb.v < 0.02f) { int k = 0; while (k < 4 && fb_partials[k] != fb_h) k++; fb_h = fb_partials[(k + 1) & 3]; fb_next = 0; fb_since = f0; }
        }
    }
    ramp(&r_fb, fb_t, fb_t > r_fb.v ? 0.012f : 0.04f, n);
    if (fb_t <= 0.0f && r_fb.v < 1e-5f) { r_fb.v = 0.0f; r_fb.d = 0.0f; }
    rflags_t F;
    F.fb_ratio = (float)fb_h * (1.0f + 0.003f * sin_turns(fb_wob * 3.0f));
    F.lfo_inc = lfo_inc;
    F.dr = exp2f(-dive_s);
    if (V.stage || r_fb.v > 1e-5f) quiet_n = 0; else if (quiet_n < (1 << 20)) quiet_n += n;
    flags(&F);
    if (fabsf(mids_s - mids_set) > 0.002f) { biquad_set(&cab[1], BQ_PEAK, 1000.0f, 1.1f, -7.0f + 13.0f * mids_s); mids_set = mids_s; }   // the cab's honk, cut; MIDS pushes it
    if (F.wah) svf_set(&wah, 380.0f * exp2f(2.5f * (0.5f + 0.5f * sin_turns(phl))), 4.5f);
    // ---- asleep: settle the states, cost nothing
    bool bar_now = live && bar_at >= 0;
    if (!F.cab && !F.bass && !F.org && !bar_now) {
        if (quiet_n < 20000) {
            pmz = prehp = z1hp = z2lp = dchp = tlp = thp = cabz = blp1 = blp2 = org_lp = 0.0f;
            memset(&up_f, 0, sizeof up_f); memset(&dn_f, 0, sizeof dn_f);
            for (int k = 0; k < 3; k++) cab[k].z1 = cab[k].z2 = 0.0f;
            wah.ic1 = wah.ic2 = 0.0f;
            quiet_n = 20000;
        }
        ramp_skip(&r_lvl, n); ramp_skip(&r_gtr, n); ramp_skip(&r_bass, n); ramp_skip(&r_sub, n); ramp_skip(&r_hum, n);
        ramp_skip(&r_send, n); ramp_skip(&r_mk, n); ramp_skip(&r_tone, n); ramp_skip(&r_org, n);
        ramp_skip(&r_wah, n); ramp_skip(&r_trem, n); ramp_skip(&r_fb, n);
        phl = wrap1(phl + lfo_inc * (float)n);
        dive_s = dive_t; bend_s = bend_t;
        float lv = v_level * 0.97f; v_level = lv < 1e-4f ? 0.0f : lv;
        v_note = -1; v_fb = 0.0f; v_swell = 0.0f; v_playing = 0;
        tap_block(out, n);
        return;
    }
    flush_states();
    // ---- render; the bar line splits the block
    bool wah0 = F.wah, trem0 = F.trem, org0 = F.org;
    float pk = 0.0f;
    if (bar_now && bar_at > 0) render(out, send, bar_at, &F, &pk);
    if (bar_now) {
        on_bar(bar, f0 + (uint32_t)(bar_at > 0 ? bar_at : 0));
        if (V.stage) quiet_n = 0;
        bool w = F.wah, t = F.trem, h = F.org;
        flags(&F);
        F.wah = F.wah && w; F.trem = F.trem && t;                   // these two start on the next block (their ramps)
        F.org = F.org || h;
        if (n > bar_at) render(out + bar_at, send + bar_at, n - bar_at, &F, &pk);
    } else render(out, send, n, &F, &pk);
    if (!wah0 && !F.wah) ramp_skip(&r_wah, n);
    if (!trem0 && !F.trem) { ramp_skip(&r_trem, n); phl = wrap1(phl + lfo_inc * (float)n); }
    if (!org0 && !F.org) ramp_skip(&r_org, n);
#ifndef ESP_PLATFORM
    if (pk > dr_pre_peak) dr_pre_peak = pk;
    dr_fb_hz = 440.0f * exp2f((V.pitch + bend_s - 12.0f * dive_s - 69.0f) / 12.0f) * (float)fb_h;
    dr_fb_lvl = r_fb.v;
#endif
    tap_block(out, n);
    float lv = v_level * 0.97f;
    v_level = pk > lv ? pk : lv;
    v_note = V.stage == 1 || V.stage == 2 ? (int)(V.target + 0.5f) : -1;
    v_fb = clampf(r_fb.v * 1.2f, 0.0f, 1.0f);
    v_swell = clampf(V.swell, 0.0f, 1.0f);
    v_playing = live && V.stage != 0 && V.stage != 3;
}

// ---------------------------------------------------------------- the UI side
void dr_set(const dr_ctl_t *c)
{
    ctl = *c;
    if (c->hit) { hit_s = c->hit_s; hit_n = hit_n + 1u; }
}

void dr_view(dr_view_t *v)
{
    v->level = v_level;
    v->feedback = v_fb;
    v->swell = v_swell;
    v->note = v_note;
    v->playing = v_playing != 0;
}

static void init(void)
{
    const float sr2 = 2.0f * SR;
    k_open = onepole_hz(6000.0f);
    k_prehp = onepole_hz(35.0f);
    k1hp = 1.0f - expf(-TWO_PI * 30.0f / sr2);
    k2lp = 1.0f - expf(-TWO_PI * 6500.0f / sr2);
    k_dchp = onepole_hz(15.0f);
    k_tlp = onepole_hz(650.0f); k_thp = onepole_hz(1500.0f);
    k_bass = onepole_hz(750.0f);
    k_hlp = onepole_hz(2600.0f);
    k_pluck = 1.0f - onepole_s(0.025f);
    k_dive = onepole_s(0.02f);
    k_dipup = onepole_s(0.004f);
    k_accp = onepole_s(0.6f);
    k_cab = onepole_hz(7000.0f);
    biquad_set(&cab[0], BQ_HP, 70.0f, 1.2f, 0.0f);                // the cab: a high pass with the low resonance on it,
    biquad_set(&cab[1], BQ_PEAK, 1000.0f, 1.1f, -4.0f);           // the honk cut (MIDS moves it),
    biquad_set(&cab[2], BQ_LP, 4300.0f, 1.2f, 0.0f);              // the top rolled off, a little presence before it
    svf_set(&wah, 800.0f, 4.5f);
    strings_rest(&V);
    V.pitch = V.target = 45.0f; V.glide = 1.0f; V.swell = 1.0f; V.swell_to = 1.0f; V.swell_k = onepole_s(0.004f);
    V.dipg = 1.0f; V.accp = 1.0f; V.gate = -1;
    r_gtr.v = 1.0f; r_bass.v = 1.0f; r_sub.v = 0.5f; r_mk.v = 1.0f; r_tone.v = 0.45f;
    quiet_n = 1 << 20;
    ctl.running = true; ctl.send = 0.5f;
    latch();
}
static void changed(int p) { (void)p; }

const module_t mod_drone = {
    .name = "DRONE", .letter = 'd', .pages = pages, .npages = 2, .params = params, .nparams = D_N, .sounds = sounds,
    .init = init, .tick = NULL, .changed = changed, .action = NULL,
};
