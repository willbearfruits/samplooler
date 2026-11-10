// The reference console: no real chip, the plainest thing that plays a frame. It shows how a console reads the
// contract (pixel.h), and a console that is not built yet can stand in with it (px_ref_* below).
//
//   voice 0  LEAD    a square (SOUND picks the width: 50, 25, 12.5 %, 50 with a duty step)
//   voice 1  CHORDS  a square playing the arpeggio's note; the lead's ECHO when no chord sounds
//   voice 2  BASS    a triangle
//   voice 3  DRUMS   noise shaped by a volume table per drum; the kick borrows the triangle for a pitch drop
//
// A console is two things in one file. The DRIVER half (tick(), 60 times a second) decides what each chip voice
// does for this frame: which part it takes, its pitch, its volume shape by the note's age. The CHIP half (render())
// makes the sound of those settings until the next tick.
#include "pixel.h"
#include <string.h>

typedef struct { float hz, vol, duty, ph; uint8_t part; } sq_t;
static struct {
    sq_t     lead, chord;
    float    tri_hz, tri_vol, tri_ph, tri_cur;
    float    nz_vol, nz_hz, nz_ph, nz_out;
    uint32_t lfsr;
    int      drum, drum_age;          // the drum sounding (0..4, -1 none) and its age in ticks
    float    drum_level;
    int      kick_age;                // ticks since a kick took the triangle, -1: not
    float    rel_lead, rel_chord, rel_bass;   // release: the volume a voice fades from after its note ends
    dcb_t    dc;
} S;

static void reset(void)
{
    memset(&S, 0, sizeof S);
    S.lfsr = 1; S.drum = -1; S.kick_age = -1;
    S.lead.part = S.chord.part = 0xFF;
}
static void init(void) { reset(); }

// a note's volume by its age in ticks: a quick attack, a decay to a sustain
static float shape(int age, bool accent) { float a = age == 0 ? 1.0f : age < 6 ? 1.0f - 0.06f * (float)age : 0.64f; return accent ? a : a * 0.8f; }

static void tick(const px_frame_t *f)
{
    // LEAD on voice 0
    if (f->lead.on) {
        static const float duty[4] = { 0.5f, 0.25f, 0.125f, 0.5f };
        S.lead.hz = px_hz(f->lead.note);
        S.lead.duty = f->lead.sound == 3 && f->lead.age < 2 ? 0.125f : duty[f->lead.sound & 3];
        S.lead.vol = S.rel_lead = shape(f->lead.age, f->lead.accent) * f->lead.level;
        S.lead.part = PART_LEAD;
    } else { S.rel_lead *= 0.6f; S.lead.vol = S.rel_lead < 0.01f ? 0.0f : S.rel_lead; if (S.lead.vol == 0.0f) S.lead.part = 0xFF; }
    // CHORDS on voice 1 (the arpeggio's one note), else the ECHO
    if (f->chord.on) {
        S.chord.hz = px_hz(f->chord.note);
        S.chord.duty = f->chord.sound & 1 ? 0.25f : 0.5f;
        S.chord.vol = S.rel_chord = shape(f->chord.age, false) * f->chord.level * 0.7f;
        S.chord.part = PART_CHORDS;
    } else if (f->echo.on) {
        S.chord.hz = px_hz(f->echo.note); S.chord.duty = 0.25f;
        S.chord.vol = S.rel_chord = shape(f->echo.age, false) * f->echo.level;
        S.chord.part = PART_LEAD;
    } else { S.rel_chord *= 0.6f; S.chord.vol = S.rel_chord < 0.01f ? 0.0f : S.rel_chord; if (S.chord.vol == 0.0f) S.chord.part = 0xFF; }
    // the drums: the last hit named wins the noise voice; a kick also takes the triangle for four ticks
    if (f->drums) {
        S.drum = f->drums & DR_SNARE ? 1 : f->drums & DR_OPEN ? 3 : f->drums & DR_HAT ? 2 : f->drums & DR_TOM ? 4 : f->drums & DR_KICK ? 0 : -1;
        S.drum_age = 0; S.drum_level = f->drum_level * (f->drum_accent ? 1.0f : 0.8f);
        if (f->drums & DR_KICK) S.kick_age = 0;
    }
    static const float nz_hz[5] = { 900, 5000, 14000, 14000, 2500 };
    static const uint8_t nz_len[5] = { 3, 7, 2, 12, 6 };
    if (S.drum >= 0 && S.drum_age < nz_len[S.drum]) {
        S.nz_hz = nz_hz[S.drum] * (S.drum == 4 ? 1.0f - 0.12f * (float)S.drum_age : 1.0f);
        S.nz_vol = S.drum_level * (1.0f - (float)S.drum_age / (float)nz_len[S.drum]) * (S.drum == 0 ? 0.5f : 0.8f);
        S.drum_age++;
    } else { S.nz_vol = 0; S.drum = -1; }
    // BASS on the triangle, unless a kick has it
    if (S.kick_age >= 0 && S.kick_age < 4) {
        S.tri_hz = 220.0f * fast_exp2(-(float)S.kick_age * 0.7f); S.tri_vol = S.drum_level;
        S.kick_age++;
    } else {
        S.kick_age = -1;
        if (f->bass.on) { S.tri_hz = px_hz(f->bass.note); S.tri_vol = S.rel_bass = f->bass.level * (f->bass.accent ? 1.0f : 0.85f); }
        else { S.rel_bass *= 0.5f; S.tri_vol = S.rel_bass < 0.01f ? 0.0f : S.rel_bass; }
    }
}

static inline IRAM_ATTR float sq_run(sq_t *v)
{
    float dt = v->hz * (1.0f / (float)AUDIO_SR);
    if (dt > 0.45f) dt = 0.45f;
    v->ph += dt; if (v->ph >= 1.0f) v->ph -= 1.0f;
    float p2 = v->ph + 1.0f - v->duty; if (p2 >= 1.0f) p2 -= 1.0f;
    // a pulse centred on zero: +1 for `duty` of the cycle, band-limited at both edges
    return ((v->ph < v->duty ? 1.0f : -1.0f) + poly_blep(v->ph, dt) - poly_blep(p2, dt) + (1.0f - 2.0f * v->duty)) * v->vol;
}

static IRAM_ATTR void render(float *out, int n)
{
    for (int i = 0; i < n; i++) {
        float x = 0.22f * sq_run(&S.lead) + 0.22f * sq_run(&S.chord);
        float dt = S.tri_hz * (1.0f / (float)AUDIO_SR);
        S.tri_ph += dt; if (S.tri_ph >= 1.0f) S.tri_ph -= 1.0f;
        S.tri_cur += 0.05f * (S.tri_vol - S.tri_cur);                 // the triangle has no volume steps: a short glide
        x += 0.45f * S.tri_cur * (4.0f * fabsf(S.tri_ph - 0.5f) - 1.0f);
        S.nz_ph += S.nz_hz * (1.0f / (float)AUDIO_SR);
        while (S.nz_ph >= 1.0f) { S.nz_ph -= 1.0f; S.lfsr = (S.lfsr >> 1) ^ (-(S.lfsr & 1u) & 0xB400u); S.nz_out = (S.lfsr & 1u) ? 1.0f : -1.0f; }
        x += 0.25f * S.nz_vol * S.nz_out;
        out[i] = dcb_run(&S.dc, x * 0.62f);
    }
    S.dc.y1 = flush_denormal(S.dc.y1);
}

static int view(px_vview_t *v, int max)
{
    if (max < 4) return 0;
    v[0] = (px_vview_t){ S.lead.vol > 0 ? VW_PULSE : VW_OFF, S.lead.part, S.lead.hz, S.lead.vol, S.lead.duty };
    v[1] = (px_vview_t){ S.chord.vol > 0 ? VW_PULSE : VW_OFF, S.chord.part, S.chord.hz, S.chord.vol, S.chord.duty };
    v[2] = (px_vview_t){ S.tri_vol > 0 ? VW_TRI : VW_OFF, S.kick_age >= 0 ? PART_DRUMS : PART_BASS, S.tri_hz, S.tri_vol, 0 };
    v[3] = (px_vview_t){ S.nz_vol > 0 ? VW_NOISE : VW_OFF, PART_DRUMS, 0, S.nz_vol, 0 };
    return 4;
}

const px_console_t con_ref = { "REF", "NONE", "2026", 4, init, reset, tick, render, view, NULL };

// For a console that is not built yet: its file can stand in with these (see pixel.h).
void px_ref_init(void) { init(); }
void px_ref_reset(void) { reset(); }
void px_ref_tick(const px_frame_t *f) { tick(f); }
IRAM_ATTR void px_ref_render(float *out, int n) { render(out, n); }
int  px_ref_view(px_vview_t *v, int max) { return view(v, max); }

// The consoles in the knob's order, thin to fat. Each is defined in its own file, inst_con_<name>.c. (They must be
// plain definitions, not weak ones overridden: the board's firmware is linked from a library, and the linker takes a
// weak symbol it already has rather than fetch the real one.)
const px_console_t *const PX_CONSOLES[CON_N] = { &con_beeper, &con_atari, &con_ay, &con_nes, &con_gameboy, &con_c64, &con_amiga, &con_mega };
