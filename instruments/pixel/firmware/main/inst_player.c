// The player (pixel.h): once a driver tick it turns the pattern's cells and the settings into a px_frame_t, the same
// for every console. A step's cells are read on the step's first tick; everything else (the slide, the vibrato, the
// arpeggio, a note's end inside its last step, the echo) moves on ticks, as an old sound driver did it.
//
// Audio task only (px_player_tick). No allocation after px_player_new(), no libm.
#include "pixel.h"
#include <string.h>

#define ECHO_RING 128                      // ticks of the lead kept for the echo (4 steps of 16 ticks, and room)

typedef struct {
    bool     on, acc;
    bool     seq_on;                       // the song's own note sounds (a live note may sit on top of it)
    uint16_t age;
    float    target, cur, slide_step;      // MIDI, before the octave, the bend and the vibrato
    int      off_tick;                     // the tick in this step at which the note ends; -1: not in this step
    int      live;                         // the note played by hand that sounds now, -1: none
    float    vib_ph;
} mono_t;

struct px_player {
    uint32_t tick, rng;
    bool     restart;
    mono_t   lead, bass;
    struct {
        int      n, notes[4];
        bool     on;
        uint16_t age;
        int      gate;                     // ticks the stab still sounds (RHY_HELD: unused)
        int      arp_ctr, arp_pos;
        int      rnd;
    } ch;
    struct { float note; uint8_t on, trig, accent; } hist[ECHO_RING];
    int      hw;
    uint16_t echo_age;
    bool     stutter;                      // a glitch: the lead retriggers on every tick of this step
    float    jump;                         // a glitch: semitones added to the lead for this step
};

px_player_t *px_player_new(void)
{
    px_player_t *p = app_alloc_fast(sizeof *p);
    if (p) px_player_reset(p);
    return p;
}

void px_player_reset(px_player_t *p)
{
    uint32_t t = p->tick;
    memset(p, 0, sizeof *p);
    p->tick = t; p->rng = 0x2545F491u ^ (t * 2654435761u);
    if (!p->rng) p->rng = 1;
    p->lead.live = p->bass.live = -1;
    p->lead.off_tick = p->bass.off_tick = -1;
    p->lead.cur = p->lead.target = 72; p->bass.cur = p->bass.target = 36;
}

void px_player_restart(px_player_t *p) { p->restart = true; }

static inline float dice(px_player_t *p) { return rng_uni(&p->rng); }

// a step's cell for one of the two melodic parts; `next` is the cell that follows (to know where the note ends)
static void mono_step(mono_t *m, int part, uint8_t cell, uint8_t next, bool muted, int tps, const px_ctl_t *c, bool *trig)
{
    int row = cell & NC_ROW, len = part == PART_LEAD ? c->len_lead : c->len_bass;
    m->off_tick = -1;
    if (muted || !row) { m->seq_on = false; return; }
    float note = (float)px_row_note(part, row - 1, c->key, c->scale);
    if ((cell & NC_TIE) && m->seq_on) {                        // carried on: held, or a slide to another row
        if (note != m->target) {
            float d = note - m->cur;
            int   ticks = (c->slide * tps + 50) / 100;
            m->slide_step = ticks <= 0 ? 0.0f : (d < 0 ? -d : d) / (float)ticks;
            m->target = note;
            if (ticks <= 0) m->cur = note;
        }
    } else {
        m->seq_on = true; m->acc = (cell & NC_ACCENT) != 0;
        m->target = m->cur = note; m->slide_step = 0;
        *trig = true;
    }
    if (!((next & NC_TIE) && (next & NC_ROW))) {               // its last step: it ends inside it (LENGTH)
        int t = (len * tps + 50) / 100;
        m->off_tick = t < 1 ? 1 : t;                           // >= tps: it lasts to the step's end
    }
}

// every tick: the live note, the end of a note, the slide; fills the voice
static void mono_tick(mono_t *m, int part, int tick_in_step, bool trig, const px_ctl_t *c, float vib, px_voice_t *v)
{
    int live = c->live[part];
    if (live != m->live) {
        m->live = live;
        if (live >= 0) { m->target = m->cur = (float)live; m->acc = false; trig = true; }
        else if (m->seq_on) { trig = true; }                   // back to the song's note: it starts again
    }
    if (m->seq_on && m->off_tick >= 0 && tick_in_step >= m->off_tick) m->seq_on = false;
    bool on = m->live >= 0 || m->seq_on;
    if (m->cur != m->target) {
        if (m->slide_step <= 0.0f) m->cur = m->target;
        else if (m->cur < m->target) { m->cur += m->slide_step; if (m->cur > m->target) m->cur = m->target; }
        else { m->cur -= m->slide_step; if (m->cur < m->target) m->cur = m->target; }
    }
    if (trig && on) { m->age = 0; m->vib_ph = 0; }
    else if (m->age < 65535) m->age++;
    m->on = on;
    v->on = on; v->trig = trig && on; v->accent = m->acc; v->sound = c->sound[part] & 3;
    v->age = m->age;
    v->note = m->cur + 12.0f * (float)c->oct[part] + c->bend + vib;
    v->level = c->level[part];
}

static const struct { uint16_t steps; uint8_t len4; } RHY[RHY_N] = {    // which steps of the bar start a stab; its
    [RHY_HELD]    = { 0x0000, 0 },                                      // length in quarter steps
    [RHY_EIGHTHS] = { 0x5555, 6 },
    [RHY_OFFBEAT] = { 0x4444, 6 },
    [RHY_GALLOP]  = { 0xDDDD, 4 },
    [RHY_FOURS]   = { 0x1111, 12 },
};

void px_player_tick(px_player_t *p, const px_pattern_t *pat, const px_pattern_t *next, int step, int tick_in_step,
                    int tps, float tick_hz, const px_ctl_t *c, px_frame_t *f)
{
    memset(f, 0, sizeof *f);
    if (tps < 1) tps = 1;
    step &= PX_STEPS - 1;
    float g = c->glitch < 0.0f ? 0.0f : c->glitch > 1.0f ? 1.0f : c->glitch;
    f->tick = p->tick++; f->tick_hz = tick_hz; f->tps = (uint8_t)tps; f->tick_in_step = (uint8_t)tick_in_step;
    f->step = (uint8_t)step; f->running = pat != NULL; f->restart = p->restart;
    f->glitch = g; f->rng = rng_next(&p->rng);
    f->kit = c->sound[PART_DRUMS] & 3; f->drum_level = c->level[PART_DRUMS];

    bool lead_trig = false, bass_trig = false, chord_trig = false;
    if (!pat) {                                                  // stopped: the song's notes end, tails ring out
        p->lead.seq_on = p->bass.seq_on = false;
        p->ch.n = 0; p->ch.on = false;
        p->stutter = false; p->jump = 0;
    } else if (tick_in_step == 0) {
        int rs = step;                                           // a tilted cartridge reads the wrong step now and then
        p->stutter = false; p->jump = 0;
        if (g > 0.0f) {
            if (dice(p) < g * 0.45f) rs = (int)(rng_next(&p->rng) & (PX_STEPS - 1));
            if (dice(p) < g * 0.20f) p->stutter = true;
            if (dice(p) < g * 0.25f) { static const float J[6] = { 12, -12, 7, 5, 1, -1 }; p->jump = J[rng_next(&p->rng) % 6]; }
        }
        const px_pattern_t *np = step < PX_STEPS - 1 ? pat : (next ? next : pat);
        int ns = (step + 1) & (PX_STEPS - 1);
        mono_step(&p->lead, PART_LEAD, pat->lead[rs], np->lead[ns], (c->mute >> PART_LEAD) & 1, tps, c, &lead_trig);
        mono_step(&p->bass, PART_BASS, pat->bass[rs], np->bass[ns], (c->mute >> PART_BASS) & 1, tps, c, &bass_trig);
        // the chords: a new chord's notes, then the rhythm's stabs
        uint8_t cc = pat->chords[rs];
        bool muted = (c->mute >> PART_CHORDS) & 1, changed = false;
        if (cc & CH_OFF) p->ch.n = 0;
        else if (cc & CH_DEGREE) { p->ch.n = px_chord_notes(cc, c->key, c->scale, p->ch.notes); changed = true; }
        int rhy = c->rhythm < RHY_N ? c->rhythm : RHY_HELD;
        if (muted || !p->ch.n) { p->ch.on = false; p->ch.gate = 0; }
        else if (rhy == RHY_HELD) { if (changed || !p->ch.on) chord_trig = true; p->ch.on = true; }
        else if ((RHY[rhy].steps >> step) & 1) { chord_trig = true; p->ch.on = true; p->ch.gate = (RHY[rhy].len4 * tps + 2) / 4; }
        if (chord_trig) { p->ch.age = 0; p->ch.arp_ctr = 0; p->ch.arp_pos = 0; }
        // the drums
        uint8_t d = pat->drums[rs];
        if (g > 0.0f && dice(p) < g * 0.30f) d |= (uint8_t)(1u << (rng_next(&p->rng) % PX_DRUMS));
        if (!((c->mute >> PART_DRUMS) & 1)) { f->drums = d & 0x1F; f->drum_accent = (d & DR_ACCENT) != 0; }
    }
    if (c->live_drums) f->drums |= c->live_drums & 0x1F;
    if (p->stutter && p->lead.seq_on) lead_trig = true;

    // the lead's vibrato: after a fifth of a second, growing over an eighth, six a second
    float vib = 0.0f;
    if (c->vibrato && p->lead.on && !lead_trig) {
        float t = (float)p->lead.age / tick_hz - 0.2f;
        if (t > 0.0f) {
            p->lead.vib_ph += 6.0f / tick_hz;
            if (p->lead.vib_ph >= 1.0f) p->lead.vib_ph -= 1.0f;
            vib = sin_turns(p->lead.vib_ph) * (float)c->vibrato * 0.005f * (t < 0.125f ? t * 8.0f : 1.0f);
        }
    }
    mono_tick(&p->lead, PART_LEAD, tick_in_step, lead_trig, c, vib + p->jump, &f->lead);
    mono_tick(&p->bass, PART_BASS, tick_in_step, bass_trig, c, 0.0f, &f->bass);

    // the chord and its arpeggio
    px_chord_t *ch = &f->chord;
    int rhy = c->rhythm < RHY_N ? c->rhythm : RHY_HELD;
    if (p->ch.on && rhy != RHY_HELD && !chord_trig) { if (p->ch.gate > 0) p->ch.gate--; if (p->ch.gate <= 0) p->ch.on = false; }
    if (!p->ch.n) p->ch.on = false;
    ch->sound = c->sound[PART_CHORDS] & 3; ch->arp = c->arp & 3; ch->level = c->level[PART_CHORDS];
    if (p->ch.on) {
        int n = p->ch.n, period = ch->arp == ARP_FAST ? 1 : ch->arp == ARP_MID ? 3 : tps, oct = 0, i = 0;
        if (ch->arp != ARP_OFF) {
            if (!chord_trig && ++p->ch.arp_ctr >= period) { p->ch.arp_ctr = 0; p->ch.arp_pos++; if (c->shape == SHAPE_RANDOM) p->ch.rnd = (int)(rng_next(&p->rng) % (uint32_t)n); }
            int k = p->ch.arp_pos;
            switch (c->shape) {
            default:
            case SHAPE_UP:      i = k % n; break;
            case SHAPE_DOWN:    i = n - 1 - k % n; break;
            case SHAPE_UPDOWN:  { int m = 2 * n - 2; int q = k % (m < 1 ? 1 : m); i = q < n ? q : m - q; break; }
            case SHAPE_OCTAVES: i = k % n; oct = (k / n) & 1; break;
            case SHAPE_RANDOM:  i = p->ch.arp_pos ? p->ch.rnd : 0; break;
            }
        }
        ch->on = 1; ch->trig = chord_trig; ch->n = (uint8_t)n; ch->arp_i = (uint8_t)i; ch->age = p->ch.age;
        float shift = 12.0f * (float)c->oct[PART_CHORDS] + c->bend;
        for (int k = 0; k < n; k++) ch->notes[k] = (float)p->ch.notes[k] + shift;
        ch->note = ch->notes[i] + 12.0f * (float)oct;
        if (p->ch.age < 65535) p->ch.age++;
    }

    // the echo: the lead as it was some steps ago, quieter
    p->hist[p->hw].note = f->lead.note; p->hist[p->hw].on = f->lead.on; p->hist[p->hw].trig = f->lead.trig; p->hist[p->hw].accent = f->lead.accent;
    if (c->echo) {
        int d = (c->echo + 1) * tps;
        if (d > ECHO_RING - 1) d = ECHO_RING - 1;
        int r = (p->hw - d) & (ECHO_RING - 1);
        px_voice_t *e = &f->echo;
        e->on = p->hist[r].on; e->trig = p->hist[r].trig; e->accent = 0; e->sound = f->lead.sound;
        e->note = p->hist[r].note; e->level = f->lead.level * 0.45f;
        if (e->trig) p->echo_age = 0; else if (p->echo_age < 65535) p->echo_age++;
        e->age = p->echo_age;
    }
    p->hw = (p->hw + 1) & (ECHO_RING - 1);

    if (p->restart) {                                            // the console is new: start what sounds again
        p->restart = false;
        if (f->lead.on) { f->lead.trig = 1; f->lead.age = 0; p->lead.age = 0; }
        if (f->bass.on) { f->bass.trig = 1; f->bass.age = 0; p->bass.age = 0; }
        if (f->echo.on) { f->echo.trig = 1; f->echo.age = 0; p->echo_age = 0; }
        if (ch->on) { ch->trig = 1; ch->age = 0; p->ch.age = 1; }
    }
}
