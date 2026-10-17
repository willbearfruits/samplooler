// AMP (inst_amp.h): the guitar and the bass of the doom end, and the metal of the middle.
//
// The voice: a power chord (root, fifth, octave; two strings each, a few cents apart) of PolyBLEP saws, from the
// song's chord in the TUNING's register (the root is the chord's note at or above the lowest string), through
//   a palm mute (a darker tone before the fuzz),
//   the fuzz: two asymmetric clipping stages at twice the rate (between halfband filters: no aliasing to speak of),
//   the gate after it (a palm mute is short, a chug stops dead), a scooped tone stack (a low pass and a high pass
//   blended, as the big fuzzes do) and MIDS, a power amp, the wah,
//   the cab: a high pass, the low resonance, the honk cut, a little presence, the top rolled off.
// The bass (a saw an octave down, filtered and driven) and the SUB (a sine there) go around the cab. The amp's HUM goes
// into the cab. FEEDBACK is an upper partial of the root (the 12th, then two octaves, ...) fed into the fuzz: it grows
// over the drone until it sings over the chord, and climbs a partial now and then. The HARPSI is four plucked narrow
// pulses (a quill's comb in the spectrum); at DRONE the same voices hold the chord as an ORGAN.
//
// What it plays: the machine's grid (wreck.h) in ticks of an eighth of a step, sample exact. Chords move on the
// amp's bar lines only (the song's bar, or the machine's when the machine is slower). By zone:
//   BREAKCORE  SILENT, STABS (a chord on the bar and on some kicks) or CHUGS (as HARDCORE, twice as fast)
//   HARDCORE   bars of palm-muted chugs on BEAT's kicks (CHUGS: how many), or of tremolo picking (TREM PICK: how
//              many bars; PICK RATE). perf.chaos adds chugs off the kicks (the flat 2nd, the flat 5th) and pinch squeals
//   SLUDGE     the RIFF (16 steps of the machine: power chords, the minor and phrygian notes, the flat 2nd and 5th,
//              bends, slides, vibrato), in the chord's key; POUND is a heavy hit on every kick
//   DRONE      (DRONE AUTO, or ON anywhere) one chord held: it swells in over SWELL, the feedback grows, the amp hums;
//              a new chord slides in on a bar line. DRONE OFF: the riff goes on, slower
// The hands: perf.drive opens the gain (+15 dB), perf.dive bends everything down (an octave at full), a knock is a
// chord hit (SLUDGE and DRONE). Stopped (perf.running false): nothing new starts, what rings fades.
#include "wreck.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"          // render() in IRAM (IRAM_ATTR), its tables in DRAM (plain static arrays)
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- params
enum {
    A_LEVEL, A_GAIN, A_VOICE, A_TUNING, A_SCALE, A_BREAK, A_CHUGS, A_TREM, A_PICK, A_RIFF, A_DRONE, A_HARPSI,
    A_TONE, A_MIDS, A_SUB, A_FEEDBACK, A_HUM, A_SWELL, A_ORGAN, A_MOD, A_MDEPTH, A_MRATE, A_SEND,
    A_N
};
#define P(i) MP(M_AMP, (i))
enum { V_GUITAR, V_BASS, V_BOTH };
enum { B_SILENT, B_STABS, B_CHUGS };
enum { D_OFF, D_ON, D_AUTO };
enum { MOD_OFF, MOD_WAH, MOD_TREM };
#define NRIFFS 6
#define RIFF_POUND 5

static const char *const pages[] = { "AMP", "AMP 2" };
static const char *const voice_l[] = { "GUITAR", "BASS", "BOTH" };
static const char *const tuning_l[] = { "E STD", "DROP D", "DROP C#", "DROP C", "B STD", "DROP A", "DROP G" };
static const int8_t tuning_floor[] = { 40, 38, 37, 36, 35, 33, 31 };            // the lowest string, MIDI
static const char *const scale_l[] = { "SONG", "MINOR", "PHRYGIAN", "HARM MINOR" };
static const uint16_t dark_scale[] = { 0, 0x5AD, 0x5AB, 0x9AD };                   // on C: natural minor, phrygian, harmonic minor
static const char *const brk_l[] = { "SILENT", "STABS", "CHUGS" };
static const char *const pick_l[] = { "1/16", "1/32", "1/64" };
static const char *const riff_l[NRIFFS] = { "TRITONE", "CREEP", "SMOKE", "BENDS", "TWANG", "POUND" };
static const char *const drone_l[] = { "OFF", "ON", "AUTO" };
static const char *const mod_l[] = { "OFF", "WAH", "TREMOLO" };
static const char *const mrate_l[] = { "1 BAR", "1/2 BAR", "1/4", "1/8", "1/16" };
static const int mrate_steps[] = { 16, 8, 4, 2, 1 };                               // machine steps per cycle

static const param_t params[A_N] = {
    [A_LEVEL]    = { "LEVEL", "alvl", 0, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "HOW LOUD THE AMP IS" },
    [A_GAIN]     = { "GAIN", "agan", 0, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "CLEAN TO WIDE OPEN" },
    [A_VOICE]    = { "VOICE", "avoc", 0, PK_ENUM, 0, 0, 2, 1, V_BOTH, "", voice_l, "GUITAR, BASS OR BOTH" },
    [A_TUNING]   = { "TUNING", "atun", 0, PK_ENUM, 0, 0, 6, 1, 3, "", tuning_l, "HOW LOW IT IS TUNED" },
    [A_SCALE]    = { "SCALE", "ascl", 0, PK_ENUM, 0, 0, 3, 1, 0, "", scale_l, "THE SONG'S NOTES, OR DARKER" },
    [A_BREAK]    = { "BREAKCORE", "abrk", 0, PK_ENUM, 0, 0, 2, 1, B_SILENT, "", brk_l, "WHAT IT PLAYS AT BREAKCORE" },
    [A_CHUGS]    = { "CHUGS", "achg", 0, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW MANY KICKS IT CHUGS ON" },
    [A_TREM]     = { "TREM PICK", "atrm", 0, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "BARS OF TREMOLO PICKING" },
    [A_PICK]     = { "PICK RATE", "aprt", 0, PK_ENUM, 0, 0, 2, 1, 1, "", pick_l, "HOW FAST IT TREMOLO PICKS" },
    [A_RIFF]     = { "RIFF", "arif", 0, PK_ENUM, 0, 0, NRIFFS - 1, 1, 0, "", riff_l, "THE SLOW RIFF AT SLUDGE" },
    [A_DRONE]    = { "DRONE", "adrn", 0, PK_ENUM, 0, 0, 2, 1, D_AUTO, "", drone_l, "AUTO: A HELD CHORD AT DRONE" },
    [A_HARPSI]   = { "HARPSI", "ahrp", 0, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A HARPSICHORD ARPEGGIO" },
    [A_TONE]     = { "TONE", "aton", 1, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "THE FUZZ: DARK TO BRIGHT" },
    [A_MIDS]     = { "MIDS", "amid", 1, PK_NUM, PF_INT, 0, 100, 5, 25, "%", NULL, "SCOOPED TO PUSHED" },
    [A_SUB]      = { "SUB", "asub", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "A SINE AN OCTAVE UNDER" },
    [A_FEEDBACK] = { "FEEDBACK", "afbk", 1, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "SINGS OVER THE DRONE" },
    [A_HUM]      = { "HUM", "ahum", 1, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE AMP'S HUM AT DRONE" },
    [A_SWELL]    = { "SWELL", "aswl", 1, PK_NUM, PF_LOG, 0.5f, 12.0f, 1.12f, 3.0f, "s", NULL, "HOW SLOW THE DRONE SWELLS" },
    [A_ORGAN]    = { "ORGAN", "aorg", 1, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN ORGAN UNDER THE DRONE" },
    [A_MOD]      = { "MOD", "amod", 1, PK_ENUM, 0, 0, 2, 1, MOD_OFF, "", mod_l, "WAH OR TREMOLO" },
    [A_MDEPTH]   = { "MOD DEPTH", "amdp", 1, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW DEEP THE MOD IS" },
    [A_MRATE]    = { "MOD RATE", "amrt", 1, PK_ENUM, 0, 0, 4, 1, 2, "", mrate_l, "ITS SPEED, ON THE BEAT" },
    [A_SEND]     = { "SEND", "asnd", 1, PK_NUM, PF_INT, 0, 100, 5, 35, "%", NULL, "INTO THE REVERB" },
};

// Each sound's AMP part (WRECK_NOTES.md has the sounds). Enums by index.
static const char *const sounds[WR_NSOUNDS] = {
    // WRECK: silent at BREAKCORE, chugs at HARDCORE, the tritone riff, then one chord held
    "agan=75 atun=3 arif=0 adrn=2 afbk=50 ahum=30 asnd=40",
    // SNARES: a cleaner amp; at DRONE slow swells, an organ under them like strings
    "agan=25 aton=60 amid=50 avoc=0 asub=20 arif=3 adrn=2 aswl=7 afbk=25 ahum=0 aorg=45 asnd=60",
    // DRILL: glassy at the far end: clean and bright, the feedback high, a quiet organ, a fast shimmer
    "agan=10 aton=85 amid=60 avoc=0 asub=0 arif=4 adrn=2 afbk=70 ahum=0 aorg=35 amod=2 amdp=25 amrt=4 asnd=65",
    // GABBA: stabs at BREAKCORE, a chug on every kick, the industrial pound at the far end
    "agan=100 aton=30 atun=5 abrk=1 achg=100 atrm=0 arif=5 adrn=0 ahum=40 asnd=25",
    // GRIND: tremolo picking and chugs from BREAKCORE on, sludge at the far end
    "agan=90 aton=50 atun=4 abrk=2 achg=90 atrm=60 aprt=1 arif=1 adrn=0 ahum=30 asnd=20",
    // BAROQUE: harpsichord arpeggios in harmonic minor and stabs; an organ at DRONE
    "agan=70 ascl=3 ahrp=70 aorg=70 abrk=1 achg=70 atrm=30 arif=1 adrn=2 afbk=30 asnd=40",
    // WIZARD: the fuzz wide open, wah, slow riffs to the end, drenched
    "agan=100 aton=40 amid=10 atun=2 amod=1 amdp=75 amrt=1 arif=2 adrn=0 afbk=40 ahum=40 asnd=70",
    // EARTH: clean, twang, tremolo, slow riffs
    "agan=15 aton=70 amid=70 atun=1 asub=20 amod=2 amdp=60 amrt=3 arif=4 adrn=0 ahum=20 asnd=50",
    // DRONE: one chord forever, fuzz, feedback, hum
    "agan=95 aton=35 amid=20 atun=5 asub=70 arif=3 adrn=2 afbk=85 ahum=70 aswl=6 asnd=50",
    // THUNDER: a low drone under the thunder
    "alvl=55 agan=80 atun=6 arif=5 adrn=2 afbk=20 ahum=30 aswl=4 asnd=80",
};

// ---------------------------------------------------------------- the riffs
// 16 steps of the machine. Each note is a power chord `off` semitones from the chord's root (snapped into the amp's
// scale unless RF_PS), `len` steps long.
enum { RF_PM = 1, RF_SL = 2, RF_B1 = 4, RF_B2 = 8, RF_VB = 16, RF_PS = 32, RF_AC = 64 };
// RF_PM palm muted, RF_SL slid into from the note before, RF_B1 / RF_B2 bent up a semitone / a tone half way through,
// RF_VB vibrato, RF_PS a passing note (the flat 5th: not snapped), RF_AC accented
typedef struct { uint8_t step; int8_t off; uint8_t len; uint8_t fl; } rnote_t;
typedef struct { uint8_t n; rnote_t note[10]; } riff_t;
static const riff_t riffs[NRIFFS] = {
    { 4, { { 0, 0, 4, RF_VB | RF_AC }, { 4, 12, 2, 0 }, { 6, 6, 8, RF_PS | RF_VB }, { 14, 3, 2, RF_SL } } },
    { 9, { { 0, 0, 2, RF_AC }, { 2, 0, 1, RF_PM }, { 3, 1, 1, RF_PS }, { 4, 0, 2, 0 }, { 6, 3, 2, RF_SL },
           { 8, 5, 3, RF_B1 }, { 11, 3, 1, 0 }, { 12, 1, 2, RF_PS }, { 14, 0, 2, RF_VB } } },
    { 7, { { 0, 0, 4, RF_AC | RF_VB }, { 4, 0, 1, RF_PM }, { 5, 0, 1, RF_PM }, { 6, 3, 2, RF_SL },
           { 8, 5, 4, RF_B2 | RF_VB }, { 12, 3, 2, RF_SL }, { 14, 0, 2, RF_SL } } },
    { 4, { { 0, 0, 6, RF_VB | RF_AC }, { 6, 10, 2, 0 }, { 8, 5, 4, RF_B2 }, { 12, 3, 4, RF_VB } } },
    { 6, { { 0, 0, 3, RF_AC }, { 3, 7, 1, 0 }, { 4, 12, 4, RF_VB }, { 8, 10, 2, RF_SL }, { 10, 7, 2, RF_SL },
           { 12, 5, 4, RF_VB | RF_SL } } },
    { 0, { { 0, 0, 0, 0 } } },                                         // POUND: a heavy hit on every kick
};
// tremolo picking moves every 4 steps: the root, the flat 2nd, the flat 5th (a passing note), the 4th, the minor 3rd
static int8_t trem_off[8] = { 0, 0, 1, 0, 6, 5, 3, 1 };
// the harpsichord's broken chord: scale steps above the chord's root
static int8_t arp_deg[8] = { 0, 2, 4, 7, 9, 7, 4, 2 };

// ---------------------------------------------------------------- shared with the UI task
static volatile float    v_level, v_fb;
static volatile int      v_note = -1, v_mode;
static volatile float    hit_s;
static volatile uint32_t hit_n;

#ifndef ESP_PLATFORM
// tools/sim/test_amp.c: the amp's own output, and what it started when (not on the device)
float   *amp_tap; int amp_tap_cap, amp_tap_n;
typedef struct { uint32_t frame; int kind, note; uint32_t kicks; int32_t step; } amp_ev_t;
amp_ev_t amp_evlog[1024]; int amp_evn;
float    amp_pre_peak;                     // the loudest sample before the safety clip
float    amp_fb_hz, amp_fb_lvl;            // the feedback partial now (Hz) and its level
static void ev_log(uint32_t frame, int kind, int note, uint32_t kicks, int32_t step)
{ amp_ev_t *e = &amp_evlog[amp_evn++ & 1023]; e->frame = frame; e->kind = kind; e->note = note; e->kicks = kicks; e->step = step; }
static void tap_block(const float *out, int n)
{ if (amp_tap && amp_tap_n + n <= amp_tap_cap) { memcpy(amp_tap + amp_tap_n, out, (size_t)n * sizeof(float)); amp_tap_n += n; } }
#else
#define ev_log(a, b, c, d, e) do { } while (0)
#define tap_block(o, n) do { } while (0)
#endif

// ---------------------------------------------------------------- the voice (audio task)
#define SR ((float)AUDIO_SR)
enum { NT_CHUG, NT_PICK, NT_STAB, NT_RIFF, NT_DRONE, NT_HIT };
enum { MD_SILENT, MD_STAB, MD_HARD, MD_RIFF, MD_DRONE };
typedef struct {
    float pitch, target, glide;            // MIDI; glide: one-pole per sample (1: a jump)
    float bend, bend_to, bend_step; int32_t bend_wait;   // semitones; it starts after bend_wait samples (-1: none)
    float vib, vib_to, vph;                // vibrato depth (semitones) and phase
    float pluck;                           // the pick's sharp start, semitones
    float env, peak, att, dec, sus, rel;   // into the fuzz; att per sample (linear), dec / rel one-pole
    int   stage;                           // 0 off, 1 attack, 2 decay / sustain, 3 release
    int32_t gate;                          // samples until the release, -1: held
    float gk;                              // the gate after the fuzz: open while env * gk >= 1
    float pm, pm_s;                        // palm mute
    float swell, swell_to, swell_k;        // after the fuzz (the drone's swell)
    float dipg, dip_step; int dip_n;       // a pick's short dip (repeated notes through a fuzz still speak)
    int32_t swell_hold;                    // samples the swell stays down before it rises (entering the drone)
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
static float k_open, k_mute, k_prehp, k1hp, k2lp, k_dchp, k_tlp, k_thp, k_bass, k_hlp;
static float k_pluck, k_dive, k_vib, k_pm, k_dipup, k_accp, k_g, k_cab;
#define VIB_INC (5.2f / SR)

// filter states
static float pmz, prehp, z1hp, z2lp, dchp, tlp, thp, cabz, blp1, blp2;
static biquad_t cab[3];
static svf_t wah;
static float g1 = 1.0f, g2 = 1.0f, g1_t = 1.0f, g2_t = 1.0f, dive_s, dive_t;
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
static ramp_t r_lvl, r_gtr, r_bass, r_sub, r_harp, r_org, r_hum, r_send, r_mk, r_tone, r_wah, r_trem, r_fb;
static float mids_s = -1.0f, mids_set = -100.0f;

// feedback
static int   fb_h = 3, fb_next;            // the partial (3: the 12th); climb to the next one
static float fb_wob;
static int32_t pinch_left;
static int   fb_partials[4] = { 3, 4, 6, 4 };
static uint32_t fb_since;                  // the frame the partial began (the strike, or its last climb)

// the harpsichord / organ
#define NH 4
typedef struct { float ph, inc, env, amp, att, ldk, w; int stage; bool organ; } hv_t;   // ldk: log2 of the decay per sample
static hv_t HV[NH];
static int  h_next;
static float harp_lp, harp_k, harp_kd, harp_kend, org_lp;   // the harpsichord's brightness (opens at a pluck), the organ's tone

// what it plays
static int   mode = MD_SILENT, view_mode;
static int   h_root_pc = 9;                // the chord's root (pitch class), in the amp's scale
static uint16_t h_mask = 0x0FFF;
static int8_t h_iv[12]; static int h_n = 7;
static int32_t bar_window;                 // frames after a bar line in which a late chord is still taken
static bool  trem_bar;
static int32_t last_tick = -1;
static int   quiet_n;                      // samples since the voice was last heard (the tails, then sleep)
static bool  was_running = true;
static uint32_t hit_seen;
static int   tun_was = -1;

static inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
static inline float hash01(uint32_t x) { return (float)(hash32(x) >> 8) * (1.0f / 16777216.0f); }
static inline float wrapf(float p) { p -= (float)(int)p; return p < 0.0f ? p + 1.0f : p; }

// ---------------------------------------------------------------- the harmony
static uint16_t amp_mask(void)
{
    int s = (int)P(A_SCALE);
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
static int chord_pc_at(uint16_t m, uint32_t frame)   // the same, exact at a frame (a bar line)
{
    int pc = ((scale_note(host_degree_at(frame), 3) % 12) + 12) % 12;
    return ((snap(pc + 24, m) % 12) + 12) % 12;
}
static void latch_at(uint32_t frame)       // the harmony for the bar starting at frame
{
    h_mask = amp_mask();
    h_root_pc = chord_pc_at(h_mask, frame);
    int n = 0;
    for (int i = 0; i < 12; i++) if (h_mask & (1u << ((h_root_pc + i) % 12))) h_iv[n++] = (int8_t)i;
    if (n < 3) { static const int8_t pw[3] = { 0, 7, 12 }; n = 3; memcpy(h_iv, pw, 3); }
    h_n = n;
}
static void latch(void)                    // the harmony for this bar
{
    h_mask = amp_mask();
    h_root_pc = chord_pc(h_mask);
    int n = 0;
    for (int i = 0; i < 12; i++) if (h_mask & (1u << ((h_root_pc + i) % 12))) h_iv[n++] = (int8_t)i;
    if (n < 3) { static const int8_t pw[3] = { 0, 7, 12 }; n = 3; memcpy(h_iv, pw, 3); }
    h_n = n;
}
static int floor_note(void) { return tuning_floor[clampi((int)P(A_TUNING), 0, 6)]; }
static int root_in(int fl) { return fl + ((h_root_pc - fl) % 12 + 12) % 12; }
static int root_note(void) { return root_in(floor_note()); }
static int scale_up(int k) { return 12 * (k / h_n) + h_iv[k % h_n]; }      // k scale steps above the root (k >= 0)
static int riff_note(int off, int fl)
{
    int n = root_note() + off;
    if (!(fl & RF_PS)) n = snap(n, h_mask);
    while (n < floor_note()) n += 12;
    return n;
}

// ---------------------------------------------------------------- notes
static void release(float tau)
{
    if (V.stage == 1 || V.stage == 2) { V.stage = 3; V.rel = onepole_s(tau); }
}

static void play(int note, int kind, float acc, int32_t len, int fl, uint32_t frame)
{
    gvoice_t *v = &V;
    bool ringing = v->stage != 0 && v->env > 0.03f;
    bool legato = (fl & RF_SL) && ringing && v->stage != 3;
    v->pitch += v->bend; v->bend = 0; v->bend_to = 0; v->bend_wait = -1;   // a bend goes into the pitch: nothing jumps
    v->target = (float)note;
    float glide = legato ? 0.10f : (kind == NT_DRONE && ringing) ? 0.45f : 0.0f;
    if (glide > 0.0f) v->glide = onepole_s(glide * 0.33f);
    else { v->glide = 1.0f; v->pitch = v->target; }
    if (fl & (RF_B1 | RF_B2)) {
        v->bend_to = (fl & RF_B2) ? 2.0f : 1.0f;
        v->bend_wait = len > 0 ? len / 2 : (int32_t)(0.3f * SR);
        v->bend_step = v->bend_to / (0.22f * SR);
    }
    v->vib_to = (fl & RF_VB) ? 0.20f : 0.0f;
    float pm = (fl & RF_PM) ? 0.85f : 0.0f, att = 0.003f, dec = 1.2f, sus = 0.55f, rel = 0.08f, gk = 3.0f, dip = 0.35f;
    switch (kind) {
    case NT_CHUG:  pm = 0.85f; att = 0.001f; dec = 0.05f; sus = 0.12f; rel = 0.010f; gk = 7.0f; dip = 0.5f; break;
    case NT_PICK:  pm = 0.3f; att = 0.0008f; dec = 0.03f; sus = 0.45f; rel = 0.006f; gk = 6.0f; dip = 0.65f; break;
    case NT_STAB:  att = 0.001f; dec = 0.12f; sus = 0.3f; rel = 0.05f; gk = 5.0f; dip = 0.4f; break;
    case NT_DRONE: att = 0.4f; dec = 0.0f; sus = 1.0f; rel = 1.2f; gk = 3.0f; dip = 0.0f; break;
    case NT_HIT:   att = 0.001f; dec = 2.5f; sus = 0.5f; rel = 0.3f; gk = 3.0f; dip = 0.5f; break;
    default: if (pm > 0.0f) { dec = 0.08f; sus = 0.15f; rel = 0.015f; gk = 6.0f; } break;
    }
    if (!legato) {
        v->peak = acc;
        v->att = acc / (att * SR);
        v->stage = 1;
        v->dec = dec > 0.0f ? onepole_s(dec) : 0.0f;
        v->sus = sus * acc;
        v->pluck = (kind == NT_DRONE ? 0.0f : 0.12f) * acc;
        if (dip > 0.0f && ringing) { v->dip_n = 24; v->dip_step = (v->dipg - (1.0f - dip)) / 24.0f; }
        v->start = frame;
    }
    v->rel = onepole_s(rel);
    v->gk = gk;
    v->pm = pm;
    v->gate = len;
    v->kind = kind;
    if (kind == NT_DRONE) {
        if (!ringing) v->swell = 0.0f;
        v->swell_to = 1.0f; v->swell_k = onepole_s(P(A_SWELL) * 0.33f);
    } else { v->swell_to = 1.0f; v->swell_k = onepole_s(0.004f); }
    v->swell_hold = 0;
    if (kind == NT_HIT) { v->accp = 1.0f + 0.7f * acc; v->swell_k = onepole_s(0.003f); }
    quiet_n = 0;
}

static void harp_note(int note, float acc)  // a plucked note of the arpeggio (the oldest voice goes)
{
    hv_t *h = &HV[h_next++ % NH];
    h->inc = 0.01375f * fast_exp2(((float)note - 69.0f) * (1.0f / 12.0f));
    h->organ = false;
    h->stage = 1;
    h->amp = acc;
    h->att = 1.0f / (0.0006f * SR);
    h->ldk = -1.442695f / (0.25f * SR);                            // the pluck dies away (0.25 s)
    h->w = 0.14f;                                                  // a narrow pulse: plucked near the end
    harp_k = onepole_hz(9000.0f);                                  // the brightness opens, then closes
}
static void organ_chord(void)              // the organ holds root, fifth, octave, tenth (in the amp's scale)
{
    int base = 48 + ((h_root_pc - 48) % 12 + 12) % 12;
    int notes[4] = { base, base + scale_up(4), base + 12, base + 12 + scale_up(2) };
    for (int k = 0; k < NH; k++) {
        hv_t *h = &HV[k];
        bool held = h->organ && (h->stage == 1 || h->stage == 2);
        h->inc = 0.01375f * fast_exp2(((float)notes[k] - 69.0f) * (1.0f / 12.0f));
        if (!held) { h->organ = true; h->stage = 1; h->amp = 1.0f; h->att = 1.0f / (0.6f * SR); h->ldk = 0.0f; h->w = 0.5f; }
    }
}
static void organ_off(void) { for (int k = 0; k < NH; k++) if (HV[k].organ && HV[k].stage) { HV[k].stage = 3; HV[k].ldk = -1.442695f / (0.5f * SR); } }

// ---------------------------------------------------------------- the sequencer: one tick (an eighth of a step)
static int want_mode(void)
{
    int z = perf.zone, d = (int)P(A_DRONE);
    if (d == D_ON || (d == D_AUTO && z == Z_DRONE)) return MD_DRONE;
    switch (z) {
    case Z_BREAKCORE: { int b = (int)P(A_BREAK); return b == B_STABS ? MD_STAB : b == B_CHUGS ? MD_HARD : MD_SILENT; }
    case Z_HARDCORE: return MD_HARD;
    default: return MD_RIFF;
    }
}
static void drone_on(uint32_t frame, bool enter)
{
    play(root_note(), NT_DRONE, 1.0f, -1, 0, frame);
    fb_since = frame;
    if (enter && pinch_left <= 0) { fb_h = 3; fb_next = 0; }       // a new drone: the feedback starts at the 12th
    if (enter && V.swell > 0.15f) {                                // a note rings: it dips, then the chord swells in
        V.swell_to = 0.1f; V.swell_k = onepole_s(0.02f); V.swell_hold = (int32_t)(0.08f * SR);
    }
    if (P(A_ORGAN) > 0.5f) organ_chord();
    ev_log(frame, 300, root_note(), 0, 0);
}

static void on_tick(int32_t T, const grid_t *g, uint32_t frame)
{
    int32_t step = T >> 3; int sub = T & 7;
    int spb = wr_steps_per_beat(g->feel_q);
    int bar_steps = spb * (song.bar_beats > 0 ? song.bar_beats : 4);
    if (bar_steps < 16) bar_steps = 16;
    float step_f = (float)g->fpb / (float)spb;
    bool run = perf.running;
    uint32_t kicks = beat_kicks();
    bool kick = (kicks >> (step & 31)) & 1u;
    float chaos = perf.chaos;
    if (sub == 0) {
        bool bar = step % bar_steps == 0;
        int was_root = root_note();
        if (!bar && amp_mask() != h_mask) {                        // a new key or scale: on this step, not the bar
            latch();
            if (V.stage) { V.target += (float)(root_note() - was_root); V.glide = onepole_s(0.03f); }
            if (mode == MD_DRONE && P(A_ORGAN) > 0.5f) organ_chord();
            was_root = root_note();
        }
        if (bar) {
            latch_at(frame); bar_window = 800;
            trem_bar = hash01((uint32_t)(step / bar_steps) & 3u) * 100.0f < P(A_TREM) + 40.0f * chaos;
        }
        int m = run ? want_mode() : MD_SILENT;
        if (m != mode) {
            if (mode == MD_DRONE) organ_off();
            if (m == MD_SILENT) release(0.3f);
            mode = m;
            if (m == MD_DRONE) drone_on(frame, true);                 // the drone comes in now, not on the bar
        } else if (m == MD_DRONE) {
            bool moved = bar && root_note() != was_root;
            if (moved && V.stage) fb_next = 1;                         // a new chord: the feedback lets go
            if (V.stage == 0 || V.stage == 3 || V.kind != NT_DRONE || moved) drone_on(frame, V.kind != NT_DRONE);
        }
        if (m == MD_DRONE && bar && (int32_t)(frame - fb_since) > (int32_t)(12.0f * SR)) fb_next = 1;   // sung a while: it climbs a partial
        if (m == MD_DRONE && run) {                                    // ORGAN turned up or down while it drones
            bool held = false;
            for (int k = 0; k < NH; k++) if (HV[k].organ && (HV[k].stage == 1 || HV[k].stage == 2)) held = true;
            if (P(A_ORGAN) > 0.5f && !held) organ_chord();
            else if (P(A_ORGAN) <= 0.5f && held) organ_off();
        }
        if (!run) { release(0.5f); organ_off(); view_mode = 0; return; }
        int32_t sl = (int32_t)(step_f * 0.7f);
        if (sl > (int32_t)(0.15f * SR)) sl = (int32_t)(0.15f * SR);
        switch (mode) {
        case MD_STAB:
            if (step % bar_steps == 0 || (kick && hash01((uint32_t)(step & 31) * 7u + 1u) < 0.25f + 0.5f * chaos)) {
                int n = root_note() + (chaos > 0.5f && (hash32((uint32_t)step) & 1u) ? 12 : 0);
                play(n, NT_STAB, 1.0f, (int32_t)fminf(3.0f * step_f, 0.3f * SR), 0, frame);
                ev_log(frame, NT_STAB, n, kicks, step);
            }
            break;
        case MD_HARD:
            if (!trem_bar) {
                if (kick && hash01((uint32_t)(step & 31) * 13u + 5u) * 100.0f < P(A_CHUGS)) {
                    play(root_note(), NT_CHUG, (step & 3) == 0 ? 1.0f : 0.85f, sl, 0, frame);
                    ev_log(frame, NT_CHUG, root_note(), kicks, step);
                } else if (!kick && chaos > 0.05f && rng_uni(&seed) < 0.45f * chaos) {   // chaos adds notes
                    static const int8_t xo[4] = { 1, 6, 12, 0 };
                    int n = riff_note(xo[rng_next(&seed) & 3u], RF_PS);
                    play(n, NT_CHUG, 0.8f, sl, 0, frame);
                    ev_log(frame, 10 + NT_CHUG, n, kicks, step);
                }
                if (chaos > 0.05f && rng_uni(&seed) < 0.12f * chaos) { pinch_left = (int32_t)(0.22f * SR); fb_h = 5 + (int)(rng_next(&seed) % 4u); }
            }
            break;
        case MD_RIFF: {
            int r = clampi((int)P(A_RIFF), 0, NRIFFS - 1);
            if (r == RIFF_POUND) {
                if (kicks ? kick : (step & 7) == 0) {
                    play(root_note(), NT_RIFF, 1.0f, (int32_t)(3.0f * step_f), RF_AC, frame);
                    ev_log(frame, NT_RIFF, root_note(), kicks, step);
                }
            } else {
                const riff_t *R = &riffs[r];
                int s16 = (int)(step & 15);
                for (int k = 0; k < R->n; k++) if (R->note[k].step == s16) {
                    const rnote_t *nt = &R->note[k];
                    int nn = riff_note(nt->off, nt->fl);
                    play(nn, NT_RIFF, (nt->fl & RF_AC) ? 1.0f : 0.85f, (int32_t)((float)nt->len * step_f * 0.96f), nt->fl, frame);
                    ev_log(frame, NT_RIFF, nn, kicks, step);
                }
            }
            break;
        }
        default: break;
        }
        // the harpsichord: a broken chord on every step (every other one at SLUDGE)
        if (P(A_HARPSI) > 0.5f && mode != MD_DRONE) {
            int every = perf.zone >= Z_SLUDGE ? 2 : 1;
            if (step % every == 0) {
                int base = 57 + ((h_root_pc - 57) % 12 + 12) % 12;
                int k = (int)((step / every) & 7);
                harp_note(base + scale_up(arp_deg[k]), k == 0 ? 1.0f : 0.8f);
            }
        }
        view_mode = mode == MD_STAB ? 1 : mode == MD_HARD ? (trem_bar ? 2 : 1) : mode == MD_RIFF ? 3 : mode == MD_DRONE ? 4 : 0;
    }
    // tremolo picking, on the ticks
    if (run && mode == MD_HARD && trem_bar) {
        int picks = 1 << clampi((int)P(A_PICK), 0, 2);            // per step: 1, 2, 4
        if (g->feel_q > 0) picks = picks > 1 ? picks >> 1 : 1;    // twice the tempo: half as many
        int every = 8 / picks;
        if (sub % every == 0) {
            int off = trem_off[(step >> 2) & 7];
            int nn = riff_note(off, off == 6 ? RF_PS : 0);
            float acc = sub == 0 ? (kick ? 1.0f : 0.9f) : ((sub / every) & 1 ? 0.75f : 0.85f);
            play(nn, NT_PICK, acc, (int32_t)(step_f / (float)picks * 0.85f), 0, frame);
            ev_log(frame, NT_PICK, nn, kicks, step);
        }
    }
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
static inline float pulse(float ph, float w, float dt)
{
    float y = (ph < w ? 1.0f : -1.0f) - (2.0f * w - 1.0f);
    if (fminf(fminf(ph, 1.0f - ph), fabsf(ph - w)) < dt) {       // near an edge: band-limit it
        float p2 = ph - w; if (p2 < 0.0f) p2 += 1.0f;
        y += poly_blep(ph, dt) - poly_blep(p2, dt);
    }
    return y;
}

typedef struct { bool str, cab, bass, harp, wah, trem, fb, hum; float fb_ratio, dr, lfo_inc; } rflags_t;

// One segment of a block, in passes (each a short loop the CPU can run many samples of at once): the voice's
// controls, the strings, into the fuzz and up to 64 kHz, the fuzz, down and the tone, the cab, the bass and the
// sub, the harpsichord and the output.
static IRAM_ATTR __attribute__((noinline)) void render(float *restrict out, float *restrict send, int n, const rflags_t *restrict F, float *restrict pk)
{
    // the work arrays (static: the audio task's stack stays small; render is never re-entered)
    static float inc[AUDIO_BLOCK], env[AUDIO_BLOCK], post[AUDIO_BLOCK], pmk[AUDIO_BLOCK], a[AUDIO_BLOCK], up[2 * AUDIO_BLOCK], fbs[AUDIO_BLOCK];
    gvoice_t *v = &V;
    const float fn = (float)n;
    // ---- 1: the voice's controls: the pitch (glide, bend, the pick, vibrato, the dive), the envelope into the fuzz,
    //      the gate and the swell after it, the palm mute
    {
        float pitch = v->pitch, target = v->target, glide = v->glide, bend = v->bend, pluck = v->pluck, ds = dive_s, dt_ = dive_t;
        float vib = v->vib, vib_to = v->vib_to, vph = v->vph, e = v->env, sw = v->swell, swt = v->swell_to, swk = v->swell_k;
        float dip = v->dipg, accp = v->accp, pms = v->pm_s, pm = v->pm, gk = v->gk;
        int stage = v->stage; int32_t bw = v->bend_wait, gate_n = v->gate; int dipn = v->dip_n;
        const float bto = v->bend_to, bst = v->bend_step, att = v->att, peak = v->peak, dec = v->dec, sus = v->sus, rel = v->rel, dst = v->dip_step;
        float sref = pitch + bend + pluck - 12.0f * ds, iref = 0.01375f * fast_exp2((sref - 69.0f) * (1.0f / 12.0f));
        for (int i = 0; i < n; i++) {
            pitch += glide * (target - pitch);
            if (bw > 0) bw--;
            else if (bw == 0) bend += fminf(fmaxf(bto - bend, -bst), bst);
            pluck *= k_pluck;
            ds += k_dive * (dt_ - ds);
            float semi = pitch + bend + pluck - 12.0f * ds;
            vib += k_vib * (vib_to - vib);
            if (vib > 0.0005f) { vph += VIB_INC; if (vph >= 1.0f) vph -= 1.0f; semi += vib * sin01(vph); }
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
        if (vib_to == 0.0f && vib < 1e-6f) vib = 0.0f;
        if (fabsf(ds - dt_) < 1e-6f) ds = dt_;
        {   // the palm mute: across the segment, a third of the way there (about 3 ms)
            float p1 = pms + (pm - pms) * fminf(1.0f, fn * (1.0f / 96.0f)), k0 = k_open + (k_mute - k_open) * pms, k1 = k_open + (k_mute - k_open) * p1;
            for (int i = 0; i < n; i++) pmk[i] = k0 + (k1 - k0) * (float)(i + 1) * (1.0f / fn);
            pms = fabsf(p1 - pm) < 1e-6f ? pm : p1;
        }
        v->pitch = pitch; v->bend = bend; v->pluck = pluck; dive_s = ds; v->vib = vib; v->vph = vph; v->env = e; v->stage = stage;
        v->bend_wait = bw; v->gate = gate_n; v->dip_n = dipn;
        v->swell = sw; v->dipg = dip; v->accp = accp; v->pm_s = pms;
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
        // ---- 3: into the fuzz: the level, the palm mute (darker), the feedback (a partial of the string), the input's
        //      high pass, the gain; up to 64 kHz
        {
            float z = pmz, h = prehp, fb = r_fb.v, fbd = r_fb.d, phf = v->phf, gg = g1, gd = (g1_t - g1) * 0.25f / fn;
            const float hk = k_prehp, hm = 1.0f - k_prehp, c0 = HBC[0], c1 = HBC[1], c2 = HBC[2];
            float ux[3], uy[3]; memcpy(ux, up_f.x, sizeof ux); memcpy(uy, up_f.y, sizeof uy);
            for (int i = 0; i < n; i++) {
                float x = a[i] * env[i];
                z = z * (1.0f - pmk[i]) + pmk[i] * x; x = z;
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
    // ---- 8: the level, the harpsichord and the organ, a safety net, out
    for (int i = 0; i < n; i++) a[i] *= lvl0 + lvld * (float)(i + 1);
    if (F->harp) {
        // each voice's envelope across the segment is a ramp (exact for the attack, linear between the decay's ends)
        static float hsum[AUDIO_BLOCK], osum[AUDIO_BLOCK];
        float ph[NH], d[NH], e0[NH], sl[NH], cap[NH], w[NH];
        bool org = false, harp = false;
        for (int k = 0; k < NH; k++) {
            hv_t *h = &HV[k];
            ph[k] = h->ph; d[k] = h->stage ? h->inc * F->dr : 0.0f; w[k] = h->w;
            if (!h->stage) { e0[k] = sl[k] = 0.0f; cap[k] = 0.0f; continue; }
            if (h->organ) org = true; else harp = true;
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
            float o0 = HV[0].organ ? 1.0f : 0.0f, o1 = HV[1].organ ? 1.0f : 0.0f, o2 = HV[2].organ ? 1.0f : 0.0f, o3 = HV[3].organ ? 1.0f : 0.0f;
            float p0 = ph[0], p1 = ph[1], p2 = ph[2], p3 = ph[3];
            for (int i = 0; i < n; i++) {
                float fi = (float)(i + 1), sum = 0.0f;
#define TRI(p, k, o) do { p += d[k]; if (p >= 1.0f) p -= 1.0f; sum += o * (4.0f * fabsf(p - 0.5f) - 1.0f) * fminf(cap[k], e0[k] + sl[k] * fi); } while (0)
                TRI(p0, 0, o0); TRI(p1, 1, o1); TRI(p2, 2, o2); TRI(p3, 3, o3);
#undef TRI
                osum[i] = sum;
            }
            if (!harp) { ph[0] = p0; ph[1] = p1; ph[2] = p2; ph[3] = p3; }
            else {
                if (HV[0].organ) { ph[0] = p0; }
                if (HV[1].organ) { ph[1] = p1; }
                if (HV[2].organ) { ph[2] = p2; }
                if (HV[3].organ) { ph[3] = p3; }
            }
        }
        if (harp) {                                                    // the harpsichord: narrow pulses
            float o0 = HV[0].organ ? 0.0f : 1.0f, o1 = HV[1].organ ? 0.0f : 1.0f, o2 = HV[2].organ ? 0.0f : 1.0f, o3 = HV[3].organ ? 0.0f : 1.0f;
            float p0 = ph[0], p1 = ph[1], p2 = ph[2], p3 = ph[3];
            for (int i = 0; i < n; i++) {
                float fi = (float)(i + 1), sum = 0.0f;
#define PLS(p, k, o) do { p += d[k]; if (p >= 1.0f) p -= 1.0f; sum += o * pulse(p, w[k], d[k] + 1e-9f) * fminf(cap[k], e0[k] + sl[k] * fi); } while (0)
                PLS(p0, 0, o0); PLS(p1, 1, o1); PLS(p2, 2, o2); PLS(p3, 3, o3);
#undef PLS
                hsum[i] = sum;
            }
            if (!HV[0].organ) { ph[0] = p0; }
            if (!HV[1].organ) { ph[1] = p1; }
            if (!HV[2].organ) { ph[2] = p2; }
            if (!HV[3].organ) { ph[3] = p3; }
        }
        for (int k = 0; k < NH; k++) HV[k].ph = ph[k];
        if (!org) for (int i = 0; i < n; i++) osum[i] = 0.0f;
        if (!harp) for (int i = 0; i < n; i++) hsum[i] = 0.0f;
        float hl = harp_lp, hk = harp_k, ol = org_lp, gh = 0.13f * r_harp.v, go = 0.06f * r_org.v;
        for (int i = 0; i < n; i++) {
            hl = hl * (1.0f - hk) + hk * hsum[i];
            hk = hk * harp_kd + harp_kend * (1.0f - harp_kd);
            float o = gh * hl;
            if (org) { ol = ol * (1.0f - k_hlp) + k_hlp * osum[i]; o += go * ol; }
            a[i] += o * (lvl0 + lvld * (float)(i + 1));
        }
        harp_lp = hl; harp_k = hk; org_lp = ol;
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
    if (F->harp) { r_harp.v += r_harp.d * fn; r_org.v += r_org.d * fn; }
}

// the LFO's phase on the machine's grid (cycles of cyc_steps steps), and its increment per frame
static float grid_phase(const grid_t *g, int32_t pos, int cyc_steps, float *inc)
{
    int spb = wr_steps_per_beat(g->feel_q);
    int64_t len = (int64_t)g->fpb * cyc_steps;                    // frames per cycle, times spb
    int64_t num = ((int64_t)pos * spb) % len;
    *inc = (float)spb / (float)len;
    return (float)num / (float)len;
}

static void flush_states(void)             // recursive states that decay: never denormal
{
    float *z[] = { &pmz, &prehp, &z1hp, &z2lp, &dchp, &tlp, &thp, &cabz, &blp1, &blp2, &harp_lp, &org_lp,
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
    F->harp = r_harp.d != 0.0f || r_org.d != 0.0f;
    for (int k = 0; k < NH; k++) if (HV[k].stage) F->harp = true;
    F->wah = F->str && (r_wah.v > 1e-4f || r_wah.d != 0.0f);
    F->trem = F->cab && (r_trem.v > 1e-4f || r_trem.d != 0.0f);
}

// Once a block: in flash (CORE.md). The per-sample work is render(), in IRAM.
void amp_process(float *out, float *send, int n, uint32_t f0)
{
    grid_t g; wr_grid_get(&g);
    if (!g.fpb) g.fpb = 19200;
    bool run = perf.running;
    // ---- a knock: a chord hit (at SLUDGE and DRONE)
    uint32_t hn = hit_n;
    if (hn != hit_seen) {
        hit_seen = hn;
        if (run && perf.zone >= Z_SLUDGE) {
            float s = clampf(hit_s, 0.0f, 1.0f);
            if (mode == MD_DRONE && V.stage == 2 && V.kind == NT_DRONE) {   // the drone is struck again, hard
                V.accp = 1.0f + 0.8f * s; V.dip_n = 24; V.dip_step = (V.dipg - 0.5f) / 24.0f; V.swell_k = onepole_s(0.01f); V.pluck = 0.15f;
            } else play(root_note(), NT_HIT, 0.7f + 0.3f * s, (int32_t)(1.5f * SR), 0, f0);
            ev_log(f0, NT_HIT, root_note(), 0, 0);
        }
    }
    // ---- the drone's dip is over: swell
    if (V.swell_hold > 0 && (V.swell_hold -= n) <= 0) { V.swell_to = 1.0f; V.swell_k = onepole_s(P(A_SWELL) * 0.33f); }
    // ---- stopped: what rings fades
    if (!run && was_running) { release(0.4f); organ_off(); }
    was_running = run;
    // ---- a chord that came late for its bar line (the song moves in the UI task): take it while the note is young
    if (bar_window > 0) {
        bar_window -= n;
        int pc = chord_pc(amp_mask());
        if (pc != h_root_pc) {
            int was = root_note();
            latch();
            int d = root_note() - was;
            if (V.stage) { V.target += (float)d; V.pitch += (float)d; }
            if (mode == MD_DRONE && P(A_ORGAN) > 0.5f) organ_chord();
        }
    }
    // ---- the tuning moved: slide there
    int tun = clampi((int)P(A_TUNING), 0, 6);
    if (tun != tun_was) {
        if (tun_was >= 0 && V.stage) { V.target += (float)(root_note() - root_in(tuning_floor[tun_was])); V.glide = onepole_s(0.03f); }
        tun_was = tun;
    }
    // ---- the ticks that start in this block
    int spb = wr_steps_per_beat(g.feel_q);
    int64_t TS = (int64_t)spb * 8, WT = TS * SONG_WRAP_BEATS;
    int32_t pos0 = wr_pos(&g, f0);
    int64_t Tc = ((int64_t)pos0 * TS) / (int64_t)g.fpb;
    int32_t ev_tick[4]; int ev_at[4], nev = 0;
    for (int64_t T = Tc; nev < 4; T++) {
        int64_t s = (T * (int64_t)g.fpb + TS - 1) / TS;
        int64_t off = s - pos0;
        if (off >= n) break;
        int32_t tk = (int32_t)(T % WT);
        if (tk == last_tick) continue;
        if (off < 0) {                                             // it began before this block: play it only if the
            if (last_tick < 0 || tk != (int32_t)((last_tick + 1) % WT) || off < -64) continue;   // grid jumped a hair over it
            off = 0;
        }
        ev_tick[nev] = tk; ev_at[nev] = (int)off; nev++;
        last_tick = tk;
    }
    // ---- block-rate settings
    float gain01 = P(A_GAIN) * 0.01f;
    float gdb = 48.0f * gain01 + 15.0f * perf.drive;
    g1_t = exp2f(gdb * 0.16609640f * 0.55f);
    g2_t = exp2f(gdb * 0.16609640f * 0.45f);
    dive_t = perf.dive;
    int voice = (int)P(A_VOICE);
    const float kb = 0.12f;                                        // block one-pole: about 30 ms
    ramp(&r_lvl, P(A_LEVEL) * 0.01f, kb, n);
    ramp(&r_gtr, voice != V_BASS ? 1.0f : 0.0f, kb, n);
    ramp(&r_bass, voice != V_GUITAR ? 1.0f : 0.0f, kb, n);
    ramp(&r_sub, P(A_SUB) * 0.01f, kb, n);
    ramp(&r_harp, P(A_HARPSI) * 0.01f, kb, n);
    ramp(&r_org, P(A_ORGAN) * 0.01f, kb, n);
    ramp(&r_send, clampf(P(A_SEND) * 0.01f * (0.5f + 0.7f * perf.axis), 0.0f, 1.0f), kb, n);
    { float u = (1.0f - gain01) * (1.0f - gain01); ramp(&r_mk, 1.0f + 3.0f * u * u, kb, n); }   // a clean amp as loud as a fuzzed one
    ramp(&r_tone, 0.12f + 0.76f * P(A_TONE) * 0.01f, kb, n);
    float hum_zone = clampf((perf.axis - 0.55f) * 5.0f, 0.0f, 1.0f);
    ramp(&r_hum, run ? P(A_HUM) * 0.01f * hum_zone : 0.0f, 0.03f, n);
    float m_t = P(A_MIDS) * 0.01f;
    mids_s = mids_s < 0.0f ? m_t : mids_s + 0.15f * (m_t - mids_s);
    int mod = (int)P(A_MOD);
    float depth = P(A_MDEPTH) * 0.01f;
    ramp(&r_wah, mod == MOD_WAH ? depth : 0.0f, kb, n);
    ramp(&r_trem, mod == MOD_TREM ? 0.9f * depth : 0.0f, kb, n);
    // the LFO, locked to the grid (it slews to the grid's phase: a new rate never jumps)
    float lfo_inc, ph_t = grid_phase(&g, pos0, mrate_steps[clampi((int)P(A_MRATE), 0, 4)], &lfo_inc);
    { float e = ph_t - phl; e -= (float)(int)(e + (e >= 0.0f ? 0.5f : -0.5f)); phl = wrapf(phl + 0.08f * e); }
    // ---- the feedback: it grows over the drone, climbs a partial now and then; a pinch squeals
    float fb_t = 0.0f;
    if (mode == MD_DRONE && V.stage && V.kind == NT_DRONE && run) {
        float age = (float)(f0 - V.start) / SR;
        fb_t = P(A_FEEDBACK) * 0.01f * clampf((age - 1.0f) / 5.0f, 0.0f, 1.0f) * (0.7f + 0.3f * clampf((perf.axis - 0.75f) * 4.0f, 0.0f, 1.0f));
        fb_wob = wrapf(fb_wob + (float)n * (0.13f / SR));
        fb_t *= 0.85f + 0.15f * sin_turns(fb_wob);
        if (fb_next) {
            fb_t = 0.0f;
            if (r_fb.v < 0.02f) { int k = 0; while (k < 4 && fb_partials[k] != fb_h) k++; fb_h = fb_partials[(k + 1) & 3]; fb_next = 0; fb_since = f0; }
        }
    }
    if (pinch_left > 0) { pinch_left -= n; fb_t = 0.9f; }
    else if (fb_h > 6 || fb_h == 5) { if (r_fb.v < 0.01f) fb_h = 3; else fb_t = 0.0f; }
    ramp(&r_fb, fb_t, fb_t > r_fb.v ? (pinch_left > 0 ? 0.3f : 0.012f) : 0.04f, n);
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
    if (!F.cab && !F.bass && !F.harp && nev == 0) {
        if (quiet_n < 20000) {
            pmz = prehp = z1hp = z2lp = dchp = tlp = thp = cabz = blp1 = blp2 = 0.0f;
            memset(&up_f, 0, sizeof up_f); memset(&dn_f, 0, sizeof dn_f);
            for (int k = 0; k < 3; k++) cab[k].z1 = cab[k].z2 = 0.0f;
            wah.ic1 = wah.ic2 = 0.0f;
            quiet_n = 20000;
        }
        ramp_skip(&r_lvl, n); ramp_skip(&r_gtr, n); ramp_skip(&r_bass, n); ramp_skip(&r_sub, n); ramp_skip(&r_hum, n);
        ramp_skip(&r_send, n); ramp_skip(&r_mk, n); ramp_skip(&r_tone, n); ramp_skip(&r_harp, n); ramp_skip(&r_org, n);
        ramp_skip(&r_wah, n); ramp_skip(&r_trem, n); ramp_skip(&r_fb, n);
        phl = wrap1(phl + lfo_inc * (float)n);
        dive_s = dive_t;
        float lv = v_level * 0.97f; v_level = lv < 1e-4f ? 0.0f : lv;
        v_note = -1; v_fb = 0.0f; v_mode = view_mode;
        tap_block(out, n);
        return;
    }
    flush_states();
    // ---- render, a segment between ticks at a time
    bool wah0 = F.wah, trem0 = F.trem, harp0 = F.harp;
    float pk = 0.0f;
    int i = 0;
    for (int e = 0; e <= nev; e++) {
        int end = e < nev ? ev_at[e] : n;
        if (end > i) { render(out + i, send + i, end - i, &F, &pk); i = end; }
        if (e < nev) {
            on_tick(ev_tick[e], &g, f0 + (uint32_t)ev_at[e]);
            if (V.stage) quiet_n = 0;
            bool w = F.wah, t = F.trem, h = F.harp;
            flags(&F);
            F.wah = F.wah && w; F.trem = F.trem && t;                // these two start on the next block (their ramps)
            F.harp = F.harp || h;
        }
    }
    if (!wah0 && !F.wah) ramp_skip(&r_wah, n);
    if (!trem0 && !F.trem) { ramp_skip(&r_trem, n); phl = wrap1(phl + lfo_inc * (float)n); }
    if (!harp0 && !F.harp) { ramp_skip(&r_harp, n); ramp_skip(&r_org, n); }
#ifndef ESP_PLATFORM
    if (pk > amp_pre_peak) amp_pre_peak = pk;
    amp_fb_hz = 440.0f * exp2f((V.pitch + V.bend - 12.0f * dive_s - 69.0f) / 12.0f) * (float)fb_h;
    amp_fb_lvl = r_fb.v;
#endif
    tap_block(out, n);
    float lv = v_level * 0.97f;
    v_level = pk > lv ? pk : lv;
    v_note = V.stage == 1 || V.stage == 2 ? (int)(V.target + 0.5f) : -1;
    v_fb = clampf(r_fb.v * 1.2f, 0.0f, 1.0f);
    v_mode = view_mode;
}

// ---------------------------------------------------------------- the UI side
void amp_hit(float strength) { hit_s = strength; hit_n = hit_n + 1u; }

void amp_view(amp_view_t *v)
{
    v->level = v_level;
    v->note = v_note;
    v->mode = v_mode;
    v->feedback = v_fb;
}

static void init(void)
{
    const float sr2 = 2.0f * SR;
    k_open = onepole_hz(6000.0f); k_mute = onepole_hz(420.0f);
    k_prehp = onepole_hz(35.0f);
    k1hp = 1.0f - expf(-TWO_PI * 30.0f / sr2);
    k2lp = 1.0f - expf(-TWO_PI * 6500.0f / sr2);
    k_dchp = onepole_hz(15.0f);
    k_tlp = onepole_hz(650.0f); k_thp = onepole_hz(1500.0f);
    k_bass = onepole_hz(750.0f);
    k_hlp = onepole_hz(2600.0f);
    harp_kend = onepole_hz(1800.0f); harp_kd = 1.0f - onepole_s(0.12f); harp_k = harp_kend;
    k_pluck = 1.0f - onepole_s(0.025f);
    k_dive = onepole_s(0.02f);
    k_vib = onepole_s(0.35f);
    k_pm = onepole_s(0.003f);
    k_dipup = onepole_s(0.004f);
    k_accp = onepole_s(0.6f);
    k_g = onepole_s(0.01f);
    k_cab = onepole_hz(7000.0f);
    biquad_set(&cab[0], BQ_HP, 70.0f, 1.2f, 0.0f);                // the cab: a high pass with the low resonance on it,
    biquad_set(&cab[1], BQ_PEAK, 1000.0f, 1.1f, -4.0f);           // the honk cut (MIDS moves it),
    biquad_set(&cab[2], BQ_LP, 4300.0f, 1.2f, 0.0f);              // the top rolled off, a little presence before it
    svf_set(&wah, 800.0f, 4.5f);
    for (int k = 0; k < 6; k++) V.ph[k] = 0.137f * (float)k + 0.05f;
    V.pitch = V.target = 45.0f; V.glide = 1.0f; V.bend_wait = -1; V.swell = 1.0f; V.swell_to = 1.0f; V.swell_k = onepole_s(0.004f);
    V.dipg = 1.0f; V.accp = 1.0f;
    r_lvl.v = 0.7f; r_gtr.v = 1.0f; r_bass.v = 1.0f; r_sub.v = 0.5f; r_mk.v = 1.0f; r_tone.v = 0.45f; r_send.v = 0.2f;
    quiet_n = 1 << 20;
    latch();
}
static void changed(int p) { (void)p; }

const module_t mod_amp = { .name = "AMP", .letter = 'a', .pages = pages, .npages = 2, .params = params, .nparams = A_N,
                           .sounds = sounds, .init = init, .changed = changed };
