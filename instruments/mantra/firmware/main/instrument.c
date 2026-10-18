// MANTRA's host (mantra.h): the param table, the sounds, the song and the link, the hands, the mix and the FX.
//
//   the knob      turn: the next sound; press: the menu; hold: freeze the hands (the core's motion hold)
//   REC           the core's looper: hold to loop what it sings (or add to it), tap to stop / play
//   the tilt      forward and back: the jaw, through OO OH AH EH EE; roll: the pitch (HANDS > ROLL: or the overtone)
//   yaw           the box turned flat on the table, like a prayer wheel: the overtone walks up the harmonics
//   pumping       the box pumped up and down: the breath (louder, more air), each stroke an accent
//   the stick     left and right bend, up the vibrato, down shuts the mouth to a hum
//   a knock       strikes the singing bowl (and hits the amps' chord, when the sound says so)
//   the mic       through the same mouth (MIC > EFFECT), or the voice follows your hum (HUM ALONG)
//
// The song (tempo, key, scale, chords) is SING's, BAND's and WRECK's, shared both ways on the link.
#include "mantra.h"
#include "ui.h"
#include "params.h"
#include "front.h"
#include "link.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#define CYCLES() esp_cpu_get_cycle_count()
#else
#include <time.h>
static inline uint32_t CYCLES(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 250000000ULL + (uint64_t)t.tv_nsec / 4); }   // wraps consistently
#endif
// What each part of the audio costs, CPU cycles summed over blocks (the log shows it): the voices, the bowl, the
// effect (the mic's), the amps, the FX bus. A block is 2 ms: 480000 cycles at 240 MHz (on the host: us x 0.96).
enum { PR_VOICE, PR_BOWL, PR_EFFECT, PR_DRONE, PR_FX, PR_N };
static volatile uint32_t prof_cyc[PR_N], prof_blocks;
void mn_prof(float *us, int n)
{
    uint32_t b = prof_blocks ? prof_blocks : 1;
    for (int i = 0; i < n && i < PR_N; i++) us[i] = (float)prof_cyc[i] / (float)b / 240.0f;
    for (int i = 0; i < PR_N; i++) prof_cyc[i] = 0;
    prof_blocks = 0;
}

// ---------------------------------------------------------------- the param table
enum {
    H_BLEND, H_ROLL, H_RANGE, H_YAW, H_PUMP, H_KNOCK, H_KSENSE,
    H_EFFECT, H_GATE,
    H_ECHO, H_ETIME, H_EFB, H_REVERB, H_RTIME, H_SHIM,
    H_VOICES, H_BOWL, H_DRONE,
    H_TEMPO, H_KEY, H_SCALE, H_PROG, H_HOLD, H_BARS,
    H_LINK, H_GROUP, H_RANGE_L, H_PEERS,
    H_N
};
#define HB (M_N * MOD_PARAMS_MAX)
#define NPARAMS (HB + H_N)
#define H(i) pv[HB + (i)]
// the menu's pages: VOICE, VOICE 2, CHANT, CHANT 2, DRONE, DRONE 2, HANDS, MIC, FX, MIX, SONG, LINK
enum { G_MOD0, G_HANDS = G_MOD0 + 2 * M_N, G_MIC, G_FX, G_MIX, G_SONG, G_LINK, G_N };
static const module_t *const mods[M_N] = { &mod_voice, &mod_score, &mod_drone };
int mod_base[M_N] = { 0, MOD_PARAMS_MAX, 2 * MOD_PARAMS_MAX };

static const char *const roll_l[] = { "PITCH", "OVERTONE", "OFF" };
static const char *const yaw_l[] = { "OVERTONE", "PITCH", "OFF" };
static const char *const pump_l[] = { "BREATH", "OFF" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const etime_l[] = { "1/8", "1/8 DOT", "1/4", "1/4 DOT", "1/2", "1 BAR" };
static const float etime_beats[] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_l[] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };
static const int8_t scales[][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
#define NSCALES 6
typedef struct { int n; int8_t d[12]; } prog_t;     // as BAND's, SING's and WRECK's: the link's numbers agree
static const prog_t progs[] = {
    { 1, { 0 } }, { 4, { 0, 4, 5, 3 } }, { 4, { 5, 3, 0, 4 } }, { 4, { 0, 5, 3, 4 } }, { 2, { 0, 3 } },
    { 4, { 1, 4, 0, 0 } }, { 4, { 0, 6, 5, 4 } }, { 2, { 0, 3 } }, { 12, { 0, 0, 0, 0, 3, 3, 0, 0, 4, 3, 0, 4 } },
};
#define NPROGS ((int)(sizeof progs / sizeof progs[0]))
static const char *const prog_l[NPROGS] = { "HOLD", "I V vi IV", "vi IV I V", "I vi IV V", "I IV", "ii V I", "i VII VI V", "i iv", "12 BAR BLUES" };
static const char *const chord_l[] = { "I", "II", "III", "IV", "V", "VI", "VII" };
static const char *const bars_l[] = { "1 BAR", "2 BARS", "HALF BAR" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };

#define SYS PF_SYS
static const param_t host_params[H_N] = {
    [H_BLEND]   = { "BLEND", "hbld", G_HANDS, PK_NUM, PF_INT | SYS, 0, 100, 2, 0, "", NULL, "0 THE CHANT, 100 DAISY" },
    [H_ROLL]    = { "ROLL", "hrol", G_HANDS, PK_ENUM, 0, 0, 2, 1, 0, "", roll_l, "WHAT ROLLING IT DOES" },
    [H_RANGE]   = { "PITCH RANGE", "hrng", G_HANDS, PK_NUM, PF_INT, 1, 24, 1, 12, "st", NULL, "HOW FAR A FULL ROLL GOES" },
    [H_YAW]     = { "YAW", "hyaw", G_HANDS, PK_ENUM, 0, 0, 2, 1, 0, "", yaw_l, "THE BOX TURNED FLAT" },
    [H_PUMP]    = { "PUMP", "hpmp", G_HANDS, PK_ENUM, SYS, 0, 1, 1, 0, "", pump_l, "PUMPING IT UP AND DOWN" },
    [H_KNOCK]   = { "KNOCKS", "hkno", G_HANDS, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "A KNOCK STRIKES THE BOWL" },
    [H_KSENSE]  = { "KNOCK SENSE", "hkse", G_HANDS, PK_NUM, PF_INT | SYS, 0, 100, 5, 50, "%", NULL, "MORE: SOFTER KNOCKS COUNT" },
    [H_EFFECT]  = { "EFFECT", "eamt", G_MIC, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE MIC THROUGH THE MOUTH" },
    [H_GATE]    = { "MIC GATE", "egat", G_MIC, PK_NUM, PF_INT | SYS, 0, 100, 5, 40, "%", NULL, "SILENCE BETWEEN SOUNDS" },
    [H_ECHO]    = { "ECHO", "fech", G_FX, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "THE CHANT COMES BACK" },
    [H_ETIME]   = { "ECHO TIME", "fetm", G_FX, PK_ENUM, 0, 0, 5, 1, 3, "", etime_l, "IN TIME WITH THE SONG" },
    [H_EFB]     = { "REPEATS", "fefb", G_FX, PK_NUM, PF_INT, 0, 95, 5, 45, "%", NULL, "HOW LONG IT GOES ON" },
    [H_REVERB]  = { "REVERB", "frev", G_FX, PK_NUM, PF_INT, 0, 100, 5, 45, "%", NULL, "HOW LOUD THE HALL IS" },
    [H_RTIME]   = { "HALL SIZE", "frtm", G_FX, PK_NUM, PF_LOG, 0.3f, 12.0f, 1.12f, 4.0f, "s", NULL, "HOW LONG IT RINGS" },
    [H_SHIM]    = { "SHIMMER", "fshm", G_FX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "AN OCTAVE UP IN THE HALL" },
    [H_VOICES]  = { "VOICES", "mvoi", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 85, "%", NULL, "THE SINGERS' LEVEL" },
    [H_BOWL]    = { "BOWL", "mbow", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "THE SINGING BOWL'S LEVEL" },
    [H_DRONE]   = { "DRONE", "mdrn", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "THE WALL OF AMPS' LEVEL" },
    [H_TEMPO]   = { "TEMPO", "stmp", G_SONG, PK_NUM, PF_INT | SYS, 40, 180, 1, 72, "bpm", NULL, "SHARED WITH THE FLOCK" },
    [H_KEY]     = { "KEY", "skey", G_SONG, PK_ENUM, SYS, 0, 11, 1, 7, "", key_l, "THE KEY THEY SING IN" },
    [H_SCALE]   = { "SCALE", "sscl", G_SONG, PK_ENUM, SYS, 0, NSCALES - 1, 1, 0, "", scale_l, "THE NOTES OF THE KEY" },
    [H_PROG]    = { "CHORDS", "sprg", G_SONG, PK_ENUM, SYS, 0, NPROGS - 1, 1, 0, "", prog_l, "HOW THE CHORDS MOVE BY BAR" },
    [H_HOLD]    = { "HOLD CHORD", "schd", G_SONG, PK_ENUM, SYS, 0, 6, 1, 0, "", chord_l, "THE CHORD FOR CHORDS: HOLD" },
    [H_BARS]    = { "CHORD BARS", "sbar", G_SONG, PK_ENUM, SYS, 0, 2, 1, 0, "", bars_l, "HOW LONG EACH CHORD LASTS" },
    [H_LINK]    = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [H_GROUP]   = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_NOCC | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [H_RANGE_L] = { "RANGE", "npow", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [H_PEERS]   = { "WHO IS THERE", "nwho", G_LINK, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
};
static UI_RAM param_t table[NPARAMS];
static UI_RAM char placeholder_tag[M_N * MOD_PARAMS_MAX][5];
static const char *groups[G_N];

// ---------------------------------------------------------------- the sounds: the host's part and each module's
static const char *const sound_names[MN_NSOUNDS] = { "OM", "MONKS", "OM MANI", "KHOOMEI", "SYGYT", "KARGYRAA", "DAISY", "DAISY 8-BIT", "VOWELS", "HUM ALONG",
                                                     "SUNN", "DOOM DAISY", "ORGAN" };
static const char *const host_sounds[MN_NSOUNDS] = {
    "frev=55 frtm=6 fech=20 fetm=5 fefb=40",                     // OM: a temple
    "frev=65 frtm=8 fech=15 fetm=5 fefb=35",                     // MONKS
    "frev=50 frtm=5 fech=35 fetm=3 fefb=50",                     // OM MANI: the Delay Lama's echoing chant
    "frev=40 frtm=3 fech=25 fetm=2 fefb=40 hyaw=0 hrol=1",       // KHOOMEI: roll and yaw on the overtone
    "frev=45 frtm=4 fech=20 fetm=2 fefb=35 hyaw=0 hrol=1",       // SYGYT
    "frev=40 frtm=3 fech=15 fetm=3 fefb=30",                     // KARGYRAA
    "frev=25 frtm=1.2 fech=0",                                   // DAISY: a small room
    "frev=15 frtm=0.8 fech=0",                                   // DAISY 8-BIT
    "frev=40 frtm=3 fech=30 fetm=3 fefb=45",                     // VOWELS: the Delay Lama's way to play
    "frev=35 frtm=2.5 fech=20 fetm=3 eamt=60",                   // HUM ALONG: the mic through the mouth too
    "frev=60 frtm=10 fech=15 fetm=5 fefb=40 mdrn=85 mvoi=80",    // SUNN: the monks over the wall of amps
    "frev=50 frtm=8 fech=25 fetm=4 fefb=50 mdrn=75 mvoi=85",     // DOOM DAISY: Daisy slow over the drone, no waltz
    "frev=55 frtm=8 fech=10 fetm=5 fefb=30 mdrn=65 mvoi=85",     // ORGAN: the chant over an organ's held chord
};
#define SOUND_TEXT 1024
static UI_RAM char sound_text[MN_NSOUNDS][SOUND_TEXT];
static preset_t presets[MN_NSOUNDS];

__attribute__((constructor)) static void build_table(void)
{
    groups[G_HANDS] = "HANDS"; groups[G_MIC] = "MIC"; groups[G_FX] = "FX"; groups[G_MIX] = "MIX"; groups[G_SONG] = "SONG"; groups[G_LINK] = "LINK";
    for (int m = 0; m < M_N; m++) {
        const module_t *M = mods[m];
        int g0 = G_MOD0 + 2 * m;
        groups[g0] = M->pages[0];
        groups[g0 + 1] = M->npages > 1 ? M->pages[1] : M->pages[0];
        for (int i = 0; i < MOD_PARAMS_MAX; i++) {
            param_t *d = &table[m * MOD_PARAMS_MAX + i];
            if (i < M->nparams) { *d = M->params[i]; d->group = (uint8_t)(g0 + (M->params[i].group ? 1 : 0)); }
            else {
                snprintf(placeholder_tag[m * MOD_PARAMS_MAX + i], 5, "~%c%02d", M->letter, i);
                *d = (param_t){ "-", placeholder_tag[m * MOD_PARAMS_MAX + i], 0, PK_NUM, PF_HIDE | PF_NOCC | PF_SYS, 0, 1, 1, 0, "", NULL, NULL };
            }
        }
    }
    for (int i = 0; i < H_N; i++) table[HB + i] = host_params[i];
    for (int k = 0; k < MN_NSOUNDS; k++) {
        int w = snprintf(sound_text[k], SOUND_TEXT, "%s", host_sounds[k]);
        for (int m = 0; m < M_N; m++) {
            const char *s = mods[m]->sounds ? mods[m]->sounds[k] : "";
            if (s && s[0] && w < SOUND_TEXT - 2) w += snprintf(sound_text[k] + w, (size_t)(SOUND_TEXT - w), "%s%s", w ? " " : "", s);
        }
        presets[k].name = sound_names[k]; presets[k].set = sound_text[k];
    }
}

// ---------------------------------------------------------------- the song and the link (as SING's)
song_t song = { .bpm = 72, .fpb = 26667, .bar_beats = 4, .scale = scales[0], .scale_n = 7, .key = 7, .chord = { 55, 59, 62 }, .chord_n = 3 };
volatile hands_t hands = { .jaw = 0.5f, .running = true };
volatile float mn_blend;
static int chord_live = -1;
static bool link_up, applying;
static int  peers_was;
static volatile float out_env_v;
float host_out_env(void) { return out_env_v; }
const char *host_sound_name(void) { const front_status_t *st = front_status(); return st && st->sound ? st->sound : ""; }

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static uint16_t rot(uint16_t m, int k) { k = ((k % 12) + 12) % 12; return (uint16_t)(((m << k) | (m >> (12 - k))) & 0x0FFF); }

// the chord's degree at a frame (the progression, HOLD, or a chord played live on a linked board): exact on a bar
// line, where song.degree may still be the last bar's for up to a tick (the drone takes its chord with it). Any task.
static const float bar_beats[3] = { 4.0f, 8.0f, 2.0f };            // CHORD BARS: 1 BAR, 2 BARS, HALF BAR
int host_degree_at(uint32_t frame)
{
    int live = chord_live;
    if (live >= 0) return live;
    if ((int)H(H_PROG) == 0) return (int)H(H_HOLD);
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    int32_t span = song.loop_beats ? (int32_t)(fpb * (uint32_t)song.loop_beats) : (int32_t)(fpb * SONG_WRAP_BEATS);
    float beat = (float)mn_mod((int32_t)(frame - song.origin), span) / (float)fpb;
    uint32_t bar = (uint32_t)(beat / bar_beats[(int)H(H_BARS)] + 1e-4f);
    const prog_t *p = &progs[(int)H(H_PROG)];
    return p->d[bar % (uint32_t)p->n];
}

static void set_song(uint32_t frame)
{
    int s = (int)H(H_SCALE);
    song.key = (int)H(H_KEY); song.scale = scales[s]; song.scale_n = 7;
    uint16_t m = 0;
    for (int i = 0; i < 7; i++) m |= (uint16_t)(1u << song.scale[i]);
    song.scale_mask = rot(m, song.key);
    int d = host_degree_at(frame);
    song.degree = d;
    int root = scale_note(d, 3), pc = ((root % 12) + 12) % 12;
    song.chord_n = 3;
    song.chord[0] = 48 + pc;
    song.chord[1] = song.chord[0] + ((scale_note(d + 2, 3) - root) % 12 + 12) % 12;
    song.chord[2] = song.chord[0] + ((scale_note(d + 4, 3) - root) % 12 + 12) % 12;
    uint16_t cm = 0;
    for (int i = 0; i < song.chord_n; i++) cm |= (uint16_t)(1u << (song.chord[i] % 12));
    song.chord_mask = cm;
}

static void link_start(void)
{
    link_init("MANTRA", 1);
    link_boot_time(tempo_fpb(H(H_TEMPO)), song.origin, song.bar_beats);
    link_boot_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_HOLD), (int)H(H_BARS));
    link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE_L));
    link_up = true;
}
static void tempo_set_here(void)
{
    uint32_t fpb = tempo_fpb(H(H_TEMPO));
    if (fpb == song.fpb) return;
    if (link_flags_any() & LINKF_LOOP) {
        applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false;
        toast("A LOOP KEEPS THE TEMPO", 1200);
        return;
    }
    uint32_t now = audio_frames();
    float beat = song_beats_at(now);
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song.fpb = fpb; song.origin = origin; song.origin_frac = 0;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    link_set_time(fpb, origin, song.bar_beats, 0);
}
static void harm_set_here(void) { link_set_harm((int)H(H_KEY), (int)H(H_SCALE), (int)H(H_PROG), (int)H(H_HOLD), (int)H(H_BARS), -1); }
static void link_follow(void)
{
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb) {
            bool tempo = fpb != song.fpb;
            song.fpb = fpb; song.origin = o; song.origin_frac = f;
            song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
            if (tempo && (int)(song.bpm + 0.5f) != (int)H(H_TEMPO)) { applying = true; param_set(HB + H_TEMPO, (float)(int)(song.bpm + 0.5f)); applying = false; }
        }
    }
    song.loop_beats = link_time()->beats;
    link_time_news();
    if (link_harm_news()) {
        const link_harm_t *h = link_harm();
        applying = true;
        param_set(HB + H_KEY, h->key); param_set(HB + H_SCALE, h->scale); param_set(HB + H_PROG, h->prog);
        param_set(HB + H_HOLD, h->hold); param_set(HB + H_BARS, h->bars);
        applying = false;
        chord_live = h->live;
    }
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (np != peers_was) {
        char b[40];
        if (np > peers_was && np > 0) snprintf(b, sizeof b, "LINKED: %s", pr[np - 1].name);
        else snprintf(b, sizeof b, np ? "A BOARD LEFT" : "LINK: ALONE");
        toast(b, 1500);
        peers_was = np;
    }
}

// ---------------------------------------------------------------- audio
static fx_in_params_t fxin;
static fx_bus_params_t fxbus;
static volatile mv_fx_t mfx;
static smooth_t g_voices, g_bowl;

static void init(void)
{
    for (int m = 0; m < M_N; m++) mods[m]->init();
    mv_init();
    sc_init();
    fx_init();
    smooth_init(&g_voices, 0.85f, 0.01f); smooth_init(&g_bowl, 0.7f, 0.01f);
}

static void audio(const float *in, float *out, int n)
{
    static float voice[AUDIO_BLOCK], vsend[AUDIO_BLOCK], bowl[AUDIO_BLOCK], send[AUDIO_BLOCK], mic[AUDIO_BLOCK];
    static float drone[AUDIO_BLOCK], dsend[AUDIO_BLOCK];
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    memset(voice, 0, sizeof voice); memset(vsend, 0, sizeof vsend); memset(bowl, 0, sizeof bowl); memset(send, 0, sizeof send);
    memset(drone, 0, sizeof drone); memset(dsend, 0, sizeof dsend);
    uint32_t c0 = CYCLES();
    mv_process(voice, vsend, n);
    uint32_t c1 = CYCLES();
    bowl_process(bowl, send, n);                                    // (the bowl scales its own send by MIX > BOWL)
    uint32_t c2 = CYCLES();
    mv_fx_t fx = mfx;
    if (fx.amount > 0.0f || sc_mode() == MODE_HUM) {               // the mic through the mouth (HUM ALONG needs its pitch
                                                                    // tracked even with EFFECT at 0)
        memcpy(mic, in, (size_t)n * sizeof(float));
        fx_input(mic, n, out_env_v);
        mv_effect(mic, voice, vsend, n, &fx);
    }
    uint32_t c3 = CYCLES();
    dr_process(drone, dsend, n);                                    // the amps (MIX > DRONE is applied there, smoothed)
    uint32_t c4 = CYCLES();
    float tv = H(H_VOICES) * 0.01f, tb = H(H_BOWL) * 0.01f;
    for (int i = 0; i < n; i++) {
        float gv = smooth_to(&g_voices, tv);
        out[i] = voice[i] * gv + bowl[i] * smooth_to(&g_bowl, tb) + drone[i];
        send[i] += vsend[i] * gv + dsend[i];
    }
    fx_bus(send, out, n);                                           // the echo and the hall
    uint32_t c5 = CYCLES();
    prof_cyc[PR_VOICE] += c1 - c0; prof_cyc[PR_BOWL] += c2 - c1; prof_cyc[PR_EFFECT] += c3 - c2; prof_cyc[PR_DRONE] += c4 - c3; prof_cyc[PR_FX] += c5 - c4;
    prof_blocks++;
    float pk = 0;
    for (int i = 0; i < n; i++) { float a = fabsf(out[i]); if (a > pk) pk = a; }
    out_env_v = pk > out_env_v ? pk : out_env_v * 0.95f;
}

// ---------------------------------------------------------------- the hands
static float pump_lp = 1.0f, pump_env, knock_prev = 1.0f, knock_ref;
static int   pump_quiet_ms, knock_quiet_ms, knock_count, pump_count;
int mn_knocks(void) { return knock_count; }                       // for tools/sim
int mn_pumps(void) { return pump_count; }

static float lean_amt(float deg, float dead, float full)
{
    float a = fabsf(deg);
    float t = a < dead ? 0.0f : clampf((a - dead) / (full - dead), 0.0f, 1.0f);
    return deg < 0.0f ? -t : t;
}

static void read_hands(const ctl_t *c)
{
    float bl = H(H_BLEND) * 0.01f;                                 // the knob: chant .. Daisy, gliding 0.2 s
    mn_blend += (bl - mn_blend) * 0.05f;
    if (fabsf(bl - mn_blend) < 0.001f) mn_blend = bl;
    bool imu = c->has_imu;
    hands.jaw = imu ? clampf(0.5f + 0.5f * lean_amt(c->lean_y, 3.0f, 40.0f), 0.0f, 1.0f) : 0.5f;
    hands.roll = imu ? lean_amt(c->lean_x, 4.0f, 45.0f) : 0.0f;
    // yaw: the turn in the screen's plane (gyro z), in turns; slow drift is not a turn
    float rate = imu ? c->gyro.z / 360.0f : 0.0f;
    if (fabsf(rate) < 0.02f) rate = 0.0f;
    hands.yaw_rate = rate;
    hands.yaw = clampf(hands.yaw + rate * c->dt, -2.0f, 2.0f);
    // pumping: the box's weight swinging (|acc| - 1 g, smoothed over a few strokes), each upward stroke an accent;
    // a knock is a jolt (its jerk), a pump is a swing
    float mag = imu ? sqrtf(c->acc.x * c->acc.x + c->acc.y * c->acc.y + c->acc.z * c->acc.z) : 1.0f;
    float dev = mag - pump_lp;
    pump_lp += 0.02f * (mag - pump_lp);
    pump_env += (fabsf(dev) > pump_env ? 0.3f : 0.02f) * (fabsf(dev) - pump_env);
    hands.pump = H(H_PUMP) < 1.0f ? clampf((pump_env - 0.06f) / 0.5f, 0.0f, 1.0f) : 0.0f;
    hands.pumped = false;
    if (pump_quiet_ms > 0) pump_quiet_ms -= 10;
    else if (H(H_PUMP) < 1.0f && dev > 0.25f) { hands.pumped = true; pump_quiet_ms = 180; pump_count++; }
    // the stick
    hands.jx = c->has_joy ? c->jx : 0.0f; hands.jy = c->has_joy ? c->jy : 0.0f;
    // a knock: a sharp jolt in the 1 kHz accelerometer stream, well over what the hands do
    hands.knocked = false;
    if (knock_quiet_ms > 0) knock_quiet_ms -= 10;
    if (H(H_KNOCK) >= 1.0f && c->burst && c->nburst > 0) {
        float pk = 0;
        for (int i = 0; i < c->nburst; i++) {
            const vec3_t *a = &c->burst[i];
            float m2 = sqrtf(a->x * a->x + a->y * a->y + a->z * a->z), j = fabsf(m2 - knock_prev);
            knock_prev = m2;
            if (j > pk) pk = j;
        }
        knock_ref += 0.02f * (fminf(pk, 0.5f) - knock_ref);
        float thr = 0.35f + (1.0f - H(H_KSENSE) * 0.01f) * 1.2f + 2.0f * knock_ref;
        if (knock_quiet_ms <= 0 && pk > thr) {
            hands.knocked = true; hands.knock = clampf((pk - thr) / (thr * 2.0f), 0.0f, 1.0f);
            knock_quiet_ms = 150; knock_count++;
            bowl_strike(hands.knock);
            coach_done(LEARN_INST + 0);
            ui_awake();
        }
    }
    if (fabsf(hands.roll) > 0.3f || fabsf(hands.jaw - 0.5f) > 0.15f) coach_done(LEARN_TILT);
    if (fabsf(hands.yaw_rate) > 0.2f) coach_done(LEARN_INST + 1);
    if (hands.pumped) coach_done(LEARN_INST + 2);
}

// the voices' controls from the hands and the settings; the score then sets the voices and what its mode needs
static mv_ctl_t mc;
static void controls(const ctl_t *c)
{
    memset(&mc, 0, sizeof mc);
    mc.jaw = hands.jaw;
    mc.hum = hands.jy < -0.2f ? clampf((-hands.jy - 0.2f) / 0.6f, 0.0f, 1.0f) : 0.0f;
    mc.vib_depth = hands.jy > 0.2f ? 0.6f * clampf((hands.jy - 0.2f) / 0.6f, 0.0f, 1.0f) : 0.0f;
    mc.vib_rate = 5.5f;
    float range = H(H_RANGE);
    mc.bend = hands.jx * 2.0f + ((int)H(H_ROLL) == 0 ? hands.roll * range : 0.0f) + ((int)H(H_YAW) == 1 ? hands.yaw * 12.0f : 0.0f);
    mc.breath = 0.15f + 0.5f * hands.pump;
    mc.level = 0.8f + 0.2f * hands.pump;
    mc.send = 0.6f;
    sc_tick(c, &mc);
    mv_set(&mc);
    float stick_hum = hands.jy < -0.2f ? clampf((-hands.jy - 0.2f) / 0.6f, 0.0f, 1.0f) : 0.0f;   // (not the score's M)
    dr_ctl_t dc = { .swell = hands.pump, .bend = hands.jx * 2.0f, .dive = stick_hum, .hit = hands.knocked, .hit_s = hands.knock,
                    .running = hands.running, .level = H(H_DRONE) * 0.01f, .send = 0.5f };
    dr_set(&dc);
    mfx = (mv_fx_t){ H(H_EFFECT) * 0.01f, mc.jaw, mc.hum, mc.tract, mc.sub, mc.overtone, mc.sharp };
    fxin = (fx_in_params_t){ H(H_GATE) * 0.01f, 0.4f, 0.6f, 0.3f };
    fx_set_input(&fxin);
    fxbus.echo = H(H_ECHO) * 0.01f;
    fxbus.echo_frames = etime_beats[(int)H(H_ETIME)] * (float)(song.fpb ? song.fpb : 19200);
    fxbus.echo_fb = H(H_EFB) * 0.01f;
    fxbus.echo_tone = 0.35f;
    fxbus.echo_duck = 0.3f;
    fxbus.reverb = H(H_REVERB) * 0.01f;
    fxbus.reverb_s = H(H_RTIME);
    fxbus.reverb_tone = 0.45f;
    fxbus.shimmer = H(H_SHIM) * 0.01f;
    fxbus.throw_ = 0.0f;
    fx_set_bus(&fxbus);
}

// ---------------------------------------------------------------- the UI side
static int beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;
int mn_beat_flash(void) { return beat_flash; }
int mn_peers(void) { return peers_was; }

static void tick(const ctl_t *c)
{
    if ((c->ticks % 1000 == 0 || (c->ticks < 3000 && c->ticks % 100 == 0)) && prof_blocks) {   // every second for the
        float us[PR_N]; mn_prof(us, PR_N);                                                      // first 30 s, then 10 s
        app_log("cpu us/block: voices %.0f bowl %.0f effect %.0f drone %.0f fx %.0f (of 2000)", (double)us[PR_VOICE], (double)us[PR_BOWL], (double)us[PR_EFFECT], (double)us[PR_DRONE], (double)us[PR_FX]);
    }
    if (meta.quick[0] && memcmp(meta.quick, "hbld", 4)) memset(meta.quick, 0, sizeof meta.quick);   // the knob stays the blend
    if (!link_up) link_start();
    link_tick(c->dt);
    link_follow();
    set_song(c->frames);
    read_hands(c);
    for (int m = 0; m < M_N; m++) if (mods[m]->tick) mods[m]->tick(c);
    controls(c);
    uint32_t fpb = song.fpb ? song.fpb : 19200;
    uint32_t b = (uint32_t)mn_mod((int32_t)(c->frames - song.origin), (int32_t)(fpb * SONG_WRAP_BEATS)) / fpb;
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    // the LED: saffron, breathing with the voices
    mv_view_t vv; mv_view(&vv);
    float lv = 0.0f;
    for (int i = 0; i < MV_N; i++) if (vv.level[i] > lv) lv = vv.level[i];
    lv = clampf(lv * 1.5f, 0.0f, 1.0f) * 0.8f + 0.06f;
    dr_view_t dv; dr_view(&dv);
    float dl = clampf(dv.level * 1.5f, 0.0f, 1.0f) * H(H_DRONE) * 0.01f;   // the amps: a deep red under the saffron
    led_rgb((uint8_t)(255.0f * fmaxf(lv, dl)), (uint8_t)(120.0f * lv), (uint8_t)(10.0f * lv));
}

static void changed(int p)
{
    if (p < 0) {
        for (int m = 0; m < M_N; m++) mods[m]->changed(-1);
        if (link_up) { tempo_set_here(); harm_set_here(); link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE_L)); }
        return;
    }
    if (p < HB) { int m = p / MOD_PARAMS_MAX, i = p % MOD_PARAMS_MAX; if (i < mods[m]->nparams) mods[m]->changed(i); return; }
    if (!link_up || applying) return;
    switch (p - HB) {
    case H_TEMPO: tempo_set_here(); break;
    case H_KEY: case H_SCALE: case H_PROG: case H_HOLD: case H_BARS: harm_set_here(); break;
    case H_LINK: case H_GROUP: case H_RANGE_L:
        link_enable(false, 1, 1);
        link_enable(H(H_LINK) >= 1.0f, (int)H(H_GROUP), (int)H(H_RANGE_L));
        peers_was = 0;
        break;
    }
}

static void action(int p)
{
    if (p < HB) { int m = p / MOD_PARAMS_MAX; if (mods[m]->action) mods[m]->action(p % MOD_PARAMS_MAX); return; }
    if (p - HB == H_PEERS) {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[48];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)H(H_GROUP));
        else {
            int k = snprintf(b, sizeof b, "%s", pr[0].name);
            for (int i = 1; i < np && k < (int)sizeof b - 10; i++) k += snprintf(b + k, sizeof b - (size_t)k, " + %s", pr[i].name);
        }
        toast(b, 2500);
    }
}

// LEARN_INST + 0: knocked the bowl; + 1: turned it flat (yaw); + 2: pumped it. The modules use + 8 and up.
static const hint_t hints[] = {
    { "TURN THE KNOB: FROM THE CHANT TO DAISY", LEARN_TURN },
    { "TILT IT: THE MOUTH OPENS AND CLOSES", LEARN_TILT },
    { "TURN IT FLAT ON THE TABLE, LIKE A WHEEL", LEARN_INST + 1 },
    { "PUMP IT UP AND DOWN: THE BREATH", LEARN_INST + 2 },
    { "KNOCK ON THE BOX: THE BOWL, THE AMPS", LEARN_INST + 0 },
    { "HOLD REC TO LOOP WHAT IT SINGS", LEARN_REC },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

static void draw(const view_t *v) { scene_draw(v); }
static bool splash(int f) { return scene_splash(f); }
static void saver(uint32_t f) { scene_saver(f); }

const instrument_t INST = {
    .name = "MANTRA", .tagline = "IT CHANTS AND SINGS",
    .params = table, .nparams = NPARAMS, .groups = groups, .ngroups = G_N,
    .presets = presets, .npresets = MN_NSOUNDS,
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = true, .looper = true, .knob = "hbld", .midi_notes = false,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .saver = saver,
};
