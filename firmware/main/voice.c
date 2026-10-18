// The voice on the Samplooler's input (voice.h): SING's voices, driven as SING's host drives them.
//
// Each voice is one of SING's sounds (instruments/sing, instrument.c: the same tags, the same values), applied over
// SING's defaults and turned into the engines' settings the same way. The song is the Key and Scale set here and the
// key's first chord; the echo's time is a beat of the loop (a loop taken as one bar), or of 100 BPM without one.
// Audio per block, in place: the input stage (gate, compressor, de-esser, the speaker guard), the voices (the tuned
// lead, harmonies, choir), the vocoder, mixed as SING mixes them, then the echo and the reverb. OFF skips it all;
// moving between OFF and a voice crossfades over 30 ms.
#include "voice.h"
#include "voice_engines.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#else
#define IRAM_ATTR
#endif

// ---------------------------------------------------------------- the platform for SING's engines
void *app_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
#else
    return calloc(1, n);
#endif
}
void *app_alloc_fast(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
#else
    return calloc(1, n);
#endif
}
void app_free(void *p) { free(p); }
void app_log(const char *fmt, ...)
{
    char b[160];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
#ifdef ESP_PLATFORM
    ESP_LOGW("voice", "%s", b);
#else
    fprintf(stderr, "voice: %s\n", b);
#endif
}

// ---------------------------------------------------------------- SING's settings that shape a voice
enum {
    V_TUNE, V_SPEED, V_VIB, V_SHIFT, V_FORMANT, V_LEAD, V_DRY,
    V_HMODE, V_H1, V_H2, V_H3, V_HLVL,
    V_CHOIR, V_CTYPE, V_SPREAD, V_CTIME, V_HUMAN,
    V_VOC, V_VCAR, V_VOCT, V_VBANDS, V_VSHARP, V_VREL, V_VSIB, V_VBRIGHT, V_VDET,
    V_ECHO, V_ETIME, V_EFB, V_ETONE, V_EDUCK, V_REV, V_RTIME, V_RTONE, V_SHIM,
    V_TUNETO, V_GATE, V_COMP, V_DEESS, V_GUARD,
    V_NP
};
typedef struct { const char *tag; float def; } vparam_t;
static const vparam_t vparams[V_NP] = {         // SING's tags and defaults
    [V_TUNE] = { "vtun", 0 }, [V_SPEED] = { "vspd", 80 }, [V_VIB] = { "vvib", 60 }, [V_SHIFT] = { "vshf", 0 },
    [V_FORMANT] = { "vfmt", 0 }, [V_LEAD] = { "vlvl", 0 }, [V_DRY] = { "vdry", 100 },
    [V_HMODE] = { "hmod", 0 }, [V_H1] = { "hiv1", 1 }, [V_H2] = { "hiv2", 0 }, [V_H3] = { "hiv3", 0 }, [V_HLVL] = { "hlvl", 70 },
    [V_CHOIR] = { "cmix", 0 }, [V_CTYPE] = { "ctyp", 0 }, [V_SPREAD] = { "cspr", 14 }, [V_CTIME] = { "ctim", 20 }, [V_HUMAN] = { "chum", 40 },
    [V_VOC] = { "olvl", 0 }, [V_VCAR] = { "ocar", 0 }, [V_VOCT] = { "ooct", 0 }, [V_VBANDS] = { "oban", 16 }, [V_VSHARP] = { "osha", 50 },
    [V_VREL] = { "orel", 40 }, [V_VSIB] = { "osib", 50 }, [V_VBRIGHT] = { "obri", 50 }, [V_VDET] = { "odet", 8 },
    [V_ECHO] = { "fech", 15 }, [V_ETIME] = { "fetm", 3 }, [V_EFB] = { "fefb", 35 }, [V_ETONE] = { "feto", 40 }, [V_EDUCK] = { "fedk", 40 },
    [V_REV] = { "frev", 20 }, [V_RTIME] = { "frtm", 1.8f }, [V_RTONE] = { "frto", 50 }, [V_SHIM] = { "fshm", 0 },
    [V_TUNETO] = { "stto", 0 }, [V_GATE] = { "igat", 30 }, [V_COMP] = { "icmp", 40 }, [V_DEESS] = { "ides", 30 }, [V_GUARD] = { "igrd", 60 },
};
// SING's 18 voices (instruments/sing/firmware/main/instrument.c, presets[]), OFF first
typedef struct { const char *name; const char *set; } vpreset_t;
static const vpreset_t voices[VOICE_N] = {
    { "OFF",            NULL },
    { "CLEAN",          "" },
    { "NATURAL TUNE",   "vtun=60 vspd=140 vvib=80 vlvl=100 vdry=0 frev=25" },
    { "HARD TUNE",      "vtun=100 vspd=0 vvib=0 vlvl=100 vdry=0 fech=20 fetm=2 frev=20" },
    { "CHOIR",          "vtun=50 vspd=90 vlvl=80 vdry=30 cmix=80 ctyp=2 cspr=18 ctim=26 chum=50 frev=55 frtm=3.5" },
    { "BARBERSHOP",     "vtun=60 vspd=60 vlvl=100 vdry=0 hmod=2 hiv1=1 hiv2=3 hiv3=10 hlvl=70 frev=30" },
    { "THIRDS",         "vtun=40 vspd=100 vlvl=100 vdry=0 hmod=1 hiv1=1 hlvl=80 frev=25 fech=20" },
    { "OCTAVE DOWN",    "vlvl=100 vdry=0 hmod=3 hiv1=10 hlvl=90 icmp=60" },
    { "ROBOT",          "vtun=100 vspd=0 vvib=0 vlvl=40 vdry=0 olvl=90 ocar=1 obri=75 odet=4 osib=40 stto=1 frev=15" },
    { "VOCODER",        "vlvl=0 vdry=0 olvl=100 ocar=0 oban=16 osha=55 osib=55 obri=55 odet=10 frev=30 fech=15" },
    { "TALKBOX",        "vtun=80 vspd=20 vlvl=20 vdry=0 olvl=100 ocar=2 oban=20 osha=70 obri=85 odet=6 icmp=70" },
    { "WHISPER",        "vlvl=0 vdry=0 olvl=100 ocar=3 oban=18 osib=80 frev=50 frtm=2.5" },
    { "CHIPMUNK",       "vlvl=100 vdry=0 vshf=7 vfmt=7" },
    { "MONSTER",        "vlvl=100 vdry=0 vshf=-12 vfmt=-6 frev=40 frto=30" },
    { "BIG THROAT",     "vlvl=100 vdry=0 vfmt=-5 frev=20" },
    { "SPACE ECHO",     "fech=70 fetm=4 fefb=65 feto=25 fedk=50 frev=30" },
    { "CATHEDRAL",      "vtun=40 vspd=150 vlvl=60 vdry=60 cmix=40 ctyp=1 frev=80 frtm=8 frto=40" },
    { "SHIMMER",        "frev=70 frtm=6 fshm=70 fech=30 fetm=3" },
    { "DUB VOICE",      "fech=85 fetm=2 fefb=80 feto=15 fedk=30 frev=40 icmp=60" },
};
static const char *const key_names[VOICE_KEYS] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const scale_names[VOICE_SCALES] = { "MAJOR", "MINOR", "DORIAN", "MIXOLYDIAN", "PHRYGIAN", "HARM MINOR" };
static const int8_t scales[VOICE_SCALES][7] = { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 },
                                                { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };
static const int8_t iv_steps[] = { 0, 2, 3, 4, 5, 7, -2, -3, -4, -5, -7 };        // scale steps
static const int8_t iv_tones[] = { 0, 1, 1, 2, 2, 3, -1, -1, -2, -2, -3 };        // chord tones
static const int8_t iv_semis[] = { 0, 4, 5, 7, 9, 12, -3, -5, -7, -8, -12 };      // fixed
static const float etime_beats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f };

const char *vox_name(int k) { return k >= 0 && k < VOICE_N ? voices[k].name : "?"; }
const char *vox_key_name(int k) { return key_names[((k % 12) + 12) % 12]; }
const char *vox_scale_name(int s) { return scale_names[s < 0 ? 0 : s >= VOICE_SCALES ? VOICE_SCALES - 1 : s]; }

// ---------------------------------------------------------------- the UI side
static float P[V_NP];
static int   cur = 0, key = 0, scale = 0;
static float beat_frames = 19200.0f;             // a beat of the loop (100 BPM without one)
static volatile int   on_v;                       // the audio side: a voice is selected
static volatile float t_dry = 1.0f, t_voc;        // levels for the mix
static bool  ready;

static void load_preset(int k)
{
    for (int i = 0; i < V_NP; i++) P[i] = vparams[i].def;
    const char *s = voices[k].set;
    while (s && *s) {
        while (*s == ' ') s++;
        char tag[5] = { 0 };
        int i = 0;
        while (*s && *s != '=' && i < 4) tag[i++] = *s++;
        if (*s != '=') break;
        s++;
        float v = strtof(s, (char **)&s);
        for (int p = 0; p < V_NP; p++) if (!strncmp(vparams[p].tag, tag, 4)) { P[p] = v; break; }
    }
}

static uint16_t rot(uint16_t m, int k) { k = ((k % 12) + 12) % 12; return (uint16_t)(((m << k) | (m >> (12 - k))) & 0x0FFF); }
static int note_of(int degree, int octave)       // MIDI note of a scale degree above the key
{
    int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7), step = degree - oct * 7;
    return 12 * (octave + 1 + oct) + key + scales[scale][step];
}

static void apply(void)
{
    if (!ready || cur == 0) return;
    // the song: the key, its scale, its first chord
    uint16_t sm = 0;
    for (int i = 0; i < 7; i++) sm |= (uint16_t)(1u << scales[scale][i]);
    uint16_t scale_mask = rot(sm, key);
    int chord[3] = { note_of(0, 3), note_of(2, 3), note_of(4, 3) };
    uint16_t chord_mask = 0;
    for (int i = 0; i < 3; i++) chord_mask |= (uint16_t)(1u << (chord[i] % 12));
    bool minor = scales[scale][2] == 3;
    uint16_t tm;
    switch ((int)P[V_TUNETO]) {
    case 1: tm = chord_mask; break;
    case 2: tm = 0; break;
    case 3: {
        static const int maj[] = { 0, 1, 2, 4, 5 }, min[] = { 0, 2, 3, 4, 6 };
        uint16_t m = 0;
        for (int i = 0; i < 5; i++) m |= (uint16_t)(1u << scales[scale][minor ? min[i] : maj[i]]);
        tm = rot(m, key);
        break;
    }
    case 4: tm = rot((uint16_t)((1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10)), minor ? key : key + 9); break;
    default: tm = scale_mask; break;
    }
    // as SING's engines()
    voice_params_t vp = { 0 };
    vp.scale_mask = tm;
    vp.tune = P[V_TUNE] * 0.01f;
    vp.speed_ms = P[V_SPEED];
    vp.keep_vib = P[V_VIB] * 0.01f;
    vp.shift = P[V_SHIFT];
    vp.formant = clampf(P[V_FORMANT], -12.0f, 12.0f);
    vp.lead_gain = P[V_LEAD] * 0.01f;
    int hm = (int)P[V_HMODE];
    vp.harm_mode = hm == 0 ? HARM_OFF : hm == 1 ? HARM_SCALE : hm == 2 ? HARM_CHORD : HARM_SEMIS;
    for (int k = 0; k < 3; k++) {
        int iv = clampi((int)P[V_H1 + k], 0, 10);
        vp.harm[k] = hm == 1 ? iv_steps[iv] : hm == 2 ? iv_tones[iv] : iv_semis[iv];
        vp.harm_gain[k] = iv && hm ? P[V_HLVL] * 0.01f : 0.0f;
    }
    vp.chord_mask = chord_mask;
    vp.choir = P[V_CHOIR] * 0.01f;
    vp.choir_mode = (int)P[V_CTYPE] == 0 ? CHOIR_UNISON : (int)P[V_CTYPE] == 1 ? CHOIR_OCTAVES : CHOIR_CHORD;
    vp.choir_spread = P[V_SPREAD];
    vp.choir_time_ms = P[V_CTIME];
    vp.humanize = P[V_HUMAN] * 0.01f;
    voice_set(&vp);

    voice_state_t vs; voice_state(&vs);
    voc_params_t op = { 0 };
    int car = (int)P[V_VCAR];
    op.carrier = car == 0 ? VOC_CHORD : car == 1 ? VOC_NOTE : car == 2 ? VOC_CHORD_NOTE : VOC_NOISE;
    op.chord_n = 3;
    for (int i = 0; i < 3; i++) op.chord[i] = chord[i];
    op.note = vs.voiced ? vs.out_note : 0.0f;
    op.octave = (int)P[V_VOCT];
    op.bands = P[V_VBANDS];
    op.sharp = P[V_VSHARP] * 0.01f;
    op.release_ms = P[V_VREL];                 // milliseconds
    op.sibilance = P[V_VSIB] * 0.01f;
    op.bright = P[V_VBRIGHT] * 0.01f;
    op.detune = P[V_VDET];
    vocoder_set(&op);

    fx_in_params_t ip = { P[V_GATE] * 0.01f, P[V_COMP] * 0.01f, P[V_GUARD] * 0.01f, P[V_DEESS] * 0.01f };
    vfx_set_input(&ip);
    fx_bus_params_t bp = { 0 };
    bp.echo = P[V_ECHO] * 0.01f;
    bp.echo_frames = etime_beats[clampi((int)P[V_ETIME], 0, 6)] * beat_frames;
    bp.echo_fb = P[V_EFB] * 0.01f;
    bp.echo_tone = P[V_ETONE] * 0.01f;
    bp.echo_duck = P[V_EDUCK] * 0.01f;
    bp.reverb = P[V_REV] * 0.01f;
    bp.reverb_s = P[V_RTIME];
    bp.reverb_tone = P[V_RTONE] * 0.01f;
    bp.shimmer = P[V_SHIM] * 0.01f;
    vfx_set_bus(&bp);
    t_dry = P[V_DRY] * 0.01f; t_voc = P[V_VOC] * 0.01f;
}

void vox_init(void)
{
    voice_init();
    vocoder_init();
    vfx_init();
    ready = true;
    load_preset(cur);
    apply();
}

void vox_select(int k, int kk, int sc)
{
    k = k < 0 ? 0 : k >= VOICE_N ? VOICE_N - 1 : k;
    bool again = k != cur;
    cur = k; key = ((kk % 12) + 12) % 12; scale = sc < 0 ? 0 : sc >= VOICE_SCALES ? VOICE_SCALES - 1 : sc;
    if (again) load_preset(k);
    apply();
    on_v = k != 0;
}

void vox_tick(float loop_s)
{
    float bf = 19200.0f;                                           // 100 BPM
    if (loop_s > 0.4f) {                                           // the loop as a bar of four
        bf = loop_s * (float)AUDIO_SR / 4.0f;
        while (bf > 38400.0f) bf *= 0.5f;                          // a beat between 0.3 and 1.2 s
        while (bf < 9600.0f) bf *= 2.0f;
    }
    if (fabsf(bf - beat_frames) > 32.0f) beat_frames = bf;
    apply();                                                       // the vocoder follows the sung note
}

bool  vox_on(void) { return on_v != 0; }
float vox_note(void) { if (!on_v) return 0.0f; voice_state_t vs; voice_state(&vs); return vs.voiced ? vs.out_note : 0.0f; }

// ---------------------------------------------------------------- the audio side
static float g_on, g_dry = 1.0f, g_voc, lim_env;
#define LIM_CEIL 0.89f                                             // -1 dBFS: harmonies and shimmer can stack up past it
#define LIM_REL  0.99961f                                          // 80 ms release
void IRAM_ATTR vox_run(float *io, int n, float out_env)
{
    static float x[AUDIO_BLOCK], lead[AUDIO_BLOCK], extra[AUDIO_BLOCK], voc[AUDIO_BLOCK], v[AUDIO_BLOCK];
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    float want = on_v ? 1.0f : 0.0f;
    if (want == 0.0f && g_on == 0.0f) return;                      // OFF: the input as it is
    memcpy(x, io, (size_t)n * sizeof(float));
    vfx_input(x, n, out_env);
    voice_process(x, lead, extra, n);
    const float td = t_dry, tv = t_voc;
    if (tv > 0.0f || g_voc > 1e-4f) vocoder_process(x, voc, n);
    else memset(voc, 0, (size_t)n * sizeof(float));
    for (int i = 0; i < n; i++) {
        g_dry += 0.002f * (td - g_dry); g_voc += 0.002f * (tv - g_voc);
        v[i] = x[i] * g_dry + lead[i] + extra[i] + voc[i] * g_voc;
    }
    memcpy(x, v, (size_t)n * sizeof(float));
    vfx_bus(x, v, n);                                              // the echo and the reverb, added
    for (int i = 0; i < n; i++) {                                  // a peak limiter (instant attack), then
        float a = fabsf(v[i]);                                     // 30 ms between OFF and the voice
        lim_env = a > lim_env ? a : lim_env * LIM_REL;
        if (lim_env > LIM_CEIL) v[i] *= LIM_CEIL / lim_env;
        g_on += (want - g_on) * (1.0f / 960.0f) * 4.0f;
        if (want == 0.0f && g_on < 1e-4f) g_on = 0.0f;
        io[i] = io[i] + (v[i] - io[i]) * g_on;
    }
}
