// The editor (pixel.h): the knob held opens it, held again closes it. One bar on the screen, sixteen steps across;
// five rows to work in, top to bottom: SONG (the bars in order), LEAD, CHORDS, BASS, DRUMS.
//
//   the stick       left / right: the cursor along the steps (on into the next bar); up / down: the row
//   turn the knob   LEAD, BASS: the note under the cursor higher or lower (an empty step gets a note). On a note's
//                   tail: the note glides to that pitch from there. CHORDS: which chord (OFF below I).
//                   DRUMS: which drum is in hand. SONG: which pattern (A..P) this bar plays
//   press           LEAD, BASS, DRUMS: nothing -> a note -> an accented note -> nothing (BAND's grid does the same);
//                   on a note's tail: the note ends there. CHORDS: put the chord, or take it away.
//                   SONG: this bar once more, after itself
//   press and turn  LEAD, BASS: the note longer or shorter. CHORDS: its kind (plain, seventh, sus, fifth).
//                   SONG: right: this bar gets a pattern of its own (a copy, to change); left: the bar leaves the song
//   REC             tap: play this bar round and round (on the SONG row: the song from here), tap again to stop.
//                   held a second: the row's part is emptied in this bar
//
// Without a stick (a board that has none): turning walks the cursor step by step and on into the next row; press
// and turn is then the pitch, the chord, the drum or the pattern.
//
// On rev E the left encoder takes the stick's place (ed_in below is where it will come in).
#include "pixel.h"
#include "ui.h"
#include "params.h"
#include <stdio.h>
#include <string.h>

enum { ROW_SONG, ROW_LEAD, ROW_CHORDS, ROW_BASS, ROW_DRUMS, ROW_N };
static const char *const row_name[ROW_N] = { "SONG", "LEAD", "CHORDS", "BASS", "DRUMS" };
static const char *const drum_name[PX_DRUMS] = { "KICK", "SNARE", "HAT", "OPEN HAT", "TOM" };

static bool    open_;
static int     row = ROW_LEAD, slot, step, drum;
static int     cur_row[PART_N] = { 0, 9, 0, 16 };   // BASS and LEAD: the row a new note gets (set when the editor opens)
static uint8_t last_chord = 1;
static int     win_lo[PART_N] = { 0, 4, 0, 12 };
static bool    homed;
static px_pattern_t clip;
static bool    clip_ok;
static int     idle_ms, rec_ms, play_asked = -1, drag_used, blink;
static bool    rec_was, rec_cleared, rec_ready;
static int     learned;                              // bit 0 moved, 1 turned, 2 pressed (the editor's own little coach)

// what the hands did this tick, whichever controls the board has
typedef struct { int dx, dy, turn, drag; bool tap, play; } ed_in_t;

bool edit_active(void) { return open_; }
#ifndef ESP_PLATFORM
void edit_test_state(int *r, int *sl, int *st) { *r = row; *sl = slot; *st = step; }   // tools/sim
void edit_test_home(void) { row = ROW_LEAD; slot = 0; step = 0; }
void edit_test_row(int r, int sl) { row = r; slot = sl; homed = true; }
#endif
static int  cur_pat(void) { if (slot >= px_song.order_n) slot = px_song.order_n - 1; if (slot < 0) slot = 0; return px_song.order[slot] & (PX_PATTERNS - 1); }
int  edit_pattern(void) { return open_ || homed ? cur_pat() : px_pos.pattern; }
static px_pattern_t *pat(void) { return &px_song.pat[cur_pat()]; }
static int  scale_n(void) { return PX_SCALES[px_scale()].n; }
static int  row_midi(int part, int r) { return px_row_note(part, r, px_key(), px_scale()); }
static void edited(void) { px_notes_edited(); idle_ms = 0; }

// ---------------------------------------------------------------- notes in a row of cells
static uint8_t *cells_of(int r) { px_pattern_t *p = pat(); return r == ROW_LEAD ? p->lead : r == ROW_BASS ? p->bass : r == ROW_CHORDS ? p->chords : p->drums; }
static bool is_tie(const uint8_t *c, int i) { return i > 0 && (c[i] & NC_TIE) && (c[i] & NC_ROW) && (c[i - 1] & NC_ROW); }
static int  note_start(const uint8_t *c, int i) { while (is_tie(c, i)) i--; return i; }
static int  note_end(const uint8_t *c, int i) { while (i < PX_STEPS - 1 && is_tie(c, i + 1)) i++; return i; }

static void audition(int part, int r, int ms)
{
    px_audition(part, row_midi(part, r) + 12 * (int)pv[param_by_tag(part == PART_BASS ? "boct" : "loct")], ms);
}

static void note_turn(int part, uint8_t *c, int d)
{
    int i = step;
    if (!(c[i] & NC_ROW)) {                                      // an empty step: a note at the row last used
        int r = clampi(cur_row[part] + (d > 0 ? d - 1 : d + 1), 0, PX_ROWS - 1);
        c[i] = (uint8_t)(r + 1); cur_row[part] = r;
        audition(part, r, 220); edited();
        return;
    }
    int from = is_tie(c, i) ? i : note_start(c, i), e = note_end(c, i);
    int old = (c[i] & NC_ROW) - 1, nw = clampi(old + d, 0, PX_ROWS - 1);
    if (nw == old) return;
    // the start: the whole note moves by that much. The tail: from here on it is at the new pitch (a glide to it)
    for (int k = from; k <= e; k++) {
        int r = (c[k] & NC_ROW) - 1;
        r = from == note_start(c, i) && !is_tie(c, i) ? clampi(r + (nw - old), 0, PX_ROWS - 1) : (r == old ? nw : r);
        c[k] = (uint8_t)((c[k] & ~NC_ROW) | (r + 1));
    }
    cur_row[part] = nw;
    audition(part, nw, 220); edited();
}

static void note_tap(int part, uint8_t *c)
{
    int i = step;
    if (!(c[i] & NC_ROW)) { c[i] = (uint8_t)(cur_row[part] + 1); audition(part, cur_row[part], 220); }
    else if (is_tie(c, i)) { for (int k = note_end(c, i); k >= i; k--) c[k] = 0; }          // the note ends before here
    else if (!(c[i] & NC_ACCENT)) { int e = note_end(c, i); for (int k = i; k <= e; k++) c[k] |= NC_ACCENT; audition(part, (c[i] & NC_ROW) - 1, 220); }
    else { for (int k = note_end(c, i); k >= i; k--) c[k] = 0; }
    edited();
}

static void note_drag(uint8_t *c, int d)
{
    int i = step;
    if (!(c[i] & NC_ROW)) return;
    int e = note_end(c, i);
    if (d > 0) {
        if (e >= PX_STEPS - 1 || (c[e + 1] & NC_ROW)) return;    // the bar's end, or another note in the way
        c[e + 1] = (uint8_t)((c[e] & (NC_ROW | NC_ACCENT)) | NC_TIE);
    } else {
        if (!is_tie(c, e)) return;                               // one step long already
        c[e] = 0;
        if (step > e - 1) step = e - 1;
    }
    edited();
}

// ---------------------------------------------------------------- the song row
static void song_insert(int at, int p)
{
    if (px_song.order_n >= PX_ORDER_MAX) { toast("THE SONG IS FULL", 1200); return; }
    for (int k = px_song.order_n; k > at; k--) px_song.order[k] = px_song.order[k - 1];
    px_song.order[at] = (uint8_t)p; px_song.order_n++;
    edited();
}
static void song_delete(int at)
{
    if (px_song.order_n <= 1) { toast("A SONG HAS ONE BAR AT LEAST", 1300); return; }
    for (int k = at; k < px_song.order_n - 1; k++) px_song.order[k] = px_song.order[k + 1];
    px_song.order_n--;
    if (slot >= px_song.order_n) slot = px_song.order_n - 1;
    edited();
}
static void song_unique(int at)
{
    uint16_t used = 0;
    for (int k = 0; k < px_song.order_n; k++) used |= (uint16_t)(1u << (px_song.order[k] & 15));
    int me = px_song.order[at] & 15, others = 0;
    for (int k = 0; k < px_song.order_n; k++) if (k != at && (px_song.order[k] & 15) == me) others++;
    if (!others) { toast("IT IS THE ONLY ONE ALREADY", 1300); return; }
    for (int p = 0; p < PX_PATTERNS; p++) if (!((used >> p) & 1)) {
        px_song.pat[p] = px_song.pat[me];
        px_song.order[at] = (uint8_t)p;
        char b[32]; snprintf(b, sizeof b, "A COPY: PATTERN %c", 'A' + p);
        toast(b, 1200);
        edited();
        return;
    }
    toast("ALL 16 PATTERNS ARE IN USE", 1300);
}

// ---------------------------------------------------------------- the hands
typedef struct { int dir, ms; } rep_t;
static int axis(float v, rep_t *r, int first, int again)        // -1 / 0 / +1, repeating while held
{
    int d = v > 0.6f ? 1 : v < -0.6f ? -1 : (fabsf(v) < 0.3f ? 0 : r->dir);
    if (d != r->dir) { r->dir = d; r->ms = 0; return d; }
    if (!d) return 0;
    r->ms += 10;
    if (r->ms >= first) { r->ms -= again; return d; }
    return 0;
}

static void move_step(int d)
{
    if (row == ROW_SONG) { slot = clampi(slot + d, 0, px_song.order_n < PX_ORDER_MAX ? px_song.order_n : px_song.order_n - 1); return; }
    step += d;
    if (step >= PX_STEPS) { if (slot < px_song.order_n - 1) { slot++; step = 0; } else step = 0; }
    if (step < 0) { if (slot > 0) { slot--; step = PX_STEPS - 1; } else step = PX_STEPS - 1; }
    if (row == ROW_LEAD || row == ROW_BASS) {                    // the new-note row follows the tune
        int part = row == ROW_LEAD ? PART_LEAD : PART_BASS;
        const uint8_t *c = cells_of(row);
        if (c[step] & NC_ROW) { cur_row[part] = (c[step] & NC_ROW) - 1; if (!px_pos.running && !is_tie(c, step)) audition(part, cur_row[part], 130); }
    } else if (row == ROW_CHORDS) {
        uint8_t cc = pat()->chords[step];
        if ((cc & CH_DEGREE) && !px_pos.running) px_audition_chord(cc, 240);
    }
}

static void value(int d)                                         // the knob's turn on the cursor
{
    switch (row) {
    case ROW_LEAD: note_turn(PART_LEAD, pat()->lead, d); break;
    case ROW_BASS: note_turn(PART_BASS, pat()->bass, d); break;
    case ROW_CHORDS: {
        uint8_t *c = &pat()->chords[step];
        int deg = *c & CH_OFF ? 0 : *c & CH_DEGREE ? (*c & CH_DEGREE) : (d > 0 ? (last_chord & CH_DEGREE) - 1 : (last_chord & CH_DEGREE) + 1);
        deg = clampi(deg + (d > 0 ? 1 : -1), 0, 7);
        *c = deg ? (uint8_t)(deg | (*c & CH_OFF ? 0 : *c & CH_KIND)) : CH_OFF;
        if (deg) { last_chord = *c; px_audition_chord(*c, 260); }
        edited();
        break;
    }
    case ROW_DRUMS:
        drum = clampi(drum + (d > 0 ? 1 : -1), 0, PX_DRUMS - 1);
        px_audition_drums((uint8_t)(1u << drum));
        break;
    case ROW_SONG:
        if (slot >= px_song.order_n) { song_insert(px_song.order_n, px_song.order[px_song.order_n - 1]); break; }
        px_song.order[slot] = (uint8_t)clampi((px_song.order[slot] & 15) + (d > 0 ? 1 : -1), 0, PX_PATTERNS - 1);
        edited();
        break;
    }
}

static void press(void)
{
    switch (row) {
    case ROW_LEAD: note_tap(PART_LEAD, pat()->lead); break;
    case ROW_BASS: note_tap(PART_BASS, pat()->bass); break;
    case ROW_CHORDS: {
        uint8_t *c = &pat()->chords[step];
        if (*c) *c = 0; else { *c = last_chord; px_audition_chord(*c, 260); }
        edited();
        break;
    }
    case ROW_DRUMS: {
        uint8_t *c = &pat()->drums[step], bit = (uint8_t)(1u << drum);
        if (!(*c & bit)) { *c |= bit; px_audition_drums(bit); }
        else if (!(*c & DR_ACCENT)) *c |= DR_ACCENT;
        else { *c &= (uint8_t)~bit; *c &= (uint8_t)~DR_ACCENT; }
        if (!(*c & 0x1F)) *c = 0;
        edited();
        break;
    }
    case ROW_SONG:
        if (slot >= px_song.order_n) song_insert(px_song.order_n, px_song.order[px_song.order_n - 1]);
        else { song_insert(slot + 1, px_song.order[slot]); if (slot < px_song.order_n - 1) slot++; }
        break;
    }
}

static void drag(int d)                                          // the knob pressed and turned
{
    switch (row) {
    case ROW_LEAD: note_drag(pat()->lead, d); break;
    case ROW_BASS: note_drag(pat()->bass, d); break;
    case ROW_CHORDS: {
        uint8_t *c = &pat()->chords[step];
        if (!(*c & CH_DEGREE) || (*c & CH_OFF)) break;
        int k = clampi(((*c & CH_KIND) >> 3) + (d > 0 ? 1 : -1), 0, 3);
        *c = (uint8_t)((*c & CH_DEGREE) | (k << 3));
        last_chord = *c; px_audition_chord(*c, 260);
        edited();
        break;
    }
    case ROW_SONG:
        if (drag_used || slot >= px_song.order_n) break;         // once per press
        drag_used = 1;
        if (d > 0) song_unique(slot); else song_delete(slot);
        break;
    default: break;
    }
}

bool edit_hold(void)
{
    open_ = !open_;
    knob_grab(open_);
    if (open_) {
        if (!homed) {
            homed = true;
            int n = scale_n(), l = px_home_octave(PART_LEAD, px_key()) * n, b = px_home_octave(PART_BASS, px_key()) * n;
            cur_row[PART_LEAD] = l; cur_row[PART_BASS] = b; win_lo[PART_LEAD] = l - 2; win_lo[PART_BASS] = b - 2;
        }
        if (px_pos.running && px_pos.mode == PM_SONG) slot = px_pos.slot;
        if (slot >= px_song.order_n) slot = 0;
        idle_ms = 0; play_asked = -1; rec_was = false; rec_ready = false;   // (a REC already down is not a tap)
        px_mute = 0;                                             // every part sounds while you write
        toast("WRITE", 600);
    } else {
        px_audition(PART_LEAD, -1, 0); px_audition(PART_BASS, -1, 0);
        if (px_pos.running && px_pos.mode == PM_PATTERN) px_play_song(slot);   // back to the song, from this bar
        toast("PLAY", 600);
    }
    return true;
}

void edit_tick(const ctl_t *c)
{
    static rep_t rx, ry;
    if (!open_) return;
    if (c->knob_hold) { edit_hold(); return; }                   // held again: back to playing
    ui_awake();
    ed_in_t in = { 0 };
    bool stick = c->has_joy;
    if (stick) {
        in.dx = axis(c->jx, &rx, 320, 90);
        in.dy = -axis(c->jy, &ry, 420, 260);                     // up on the stick is up on the screen
        if (c->knob_down && c->knob_turned) in.drag = c->knob; else in.turn = c->knob;
    } else {                                                     // no stick: the knob walks, pressed it changes
        if (c->knob_down && c->knob_turned) in.turn = c->knob;
        else if (c->knob) {
            int pos = (row == ROW_SONG ? 0 : (row * PX_STEPS + step)) + c->knob;
            if (row == ROW_SONG) { slot += c->knob; if (slot > px_song.order_n) { row = ROW_LEAD; step = 0; slot = px_song.order_n - 1; } else if (slot < 0) slot = 0; }
            else if (pos < PX_STEPS) { row = ROW_SONG; }
            else if (pos >= ROW_N * PX_STEPS) { row = ROW_DRUMS; step = PX_STEPS - 1; }
            else { row = pos / PX_STEPS; step = pos % PX_STEPS; }
            learned |= 1; idle_ms = 0;
        }
    }
    in.tap = c->knob_tap;
    if (!c->knob_down) drag_used = 0;
    // REC: a tap plays or stops; a second held empties the row's part in this bar
    if (!c->rec && !rec_was) rec_ready = true;                   // it has been up since the editor opened
    if (rec_ready) {
        if (c->rec) {
            rec_ms = rec_was ? rec_ms + 10 : 0;
            if (rec_ms >= 1000 && !rec_cleared && row != ROW_SONG) {
                memset(cells_of(row), 0, PX_STEPS);
                rec_cleared = true;
                char b[32]; snprintf(b, sizeof b, "%s EMPTIED IN THIS BAR", row_name[row]);
                toast(b, 1200); edited();
            }
        } else {
            if (rec_was && rec_ms < 600 && !rec_cleared) in.play = true;
            rec_cleared = false;
        }
    }
    rec_was = c->rec;

    if (in.dy) { row = clampi(row + in.dy, 0, ROW_N - 1); learned |= 1; idle_ms = 0; if (row == ROW_DRUMS && !px_pos.running) px_audition_drums((uint8_t)(1u << drum)); }
    if (in.dx) { move_step(in.dx); learned |= 1; idle_ms = 0; }
    if (in.turn) { value(in.turn); learned |= 2; coach_done(LEARN_INST + 8); }
    if (in.drag) { drag(in.drag); idle_ms = 0; }
    if (in.tap) { press(); learned |= 4; coach_done(LEARN_INST + 9); }
    if (in.play) {
        if (px_pos.running || px_pos.armed) { px_stop(); play_asked = -1; }
        else if (row == ROW_SONG) { px_play_song(slot < px_song.order_n ? slot : 0); play_asked = -1; }
        else { play_asked = cur_pat(); px_play_pattern(play_asked); }
        idle_ms = 0;
    }
    // a bar going round follows the cursor into another bar
    if (px_pos.running && px_pos.mode == PM_PATTERN && play_asked >= 0 && row != ROW_SONG && cur_pat() != play_asked) { play_asked = cur_pat(); px_play_pattern(play_asked); }
    // keep the cursor's pitch in the window
    for (int p = PART_BASS; p <= PART_LEAD; p += 2) {
        if (cur_row[p] < win_lo[p] + 1) win_lo[p] = cur_row[p] - 1;
        if (cur_row[p] > win_lo[p] + 6) win_lo[p] = cur_row[p] - 6;
        win_lo[p] = clampi(win_lo[p], 0, PX_ROWS - 8);
    }
    idle_ms += 10;
}

// ---------------------------------------------------------------- the dice: an idea for one row of this bar
static uint32_t dice_seed = 0x9E3779B9u;
static int roll(int n) { return (int)(rng_next(&dice_seed) % (uint32_t)(n < 1 ? 1 : n)); }
// the chord that holds at a step of this bar (the last chord cell at or before it; I if the bar has none)
static uint8_t chord_at(const px_pattern_t *p, int s)
{
    for (; s >= 0; s--) { if (p->chords[s] & CH_OFF) break; if (p->chords[s] & CH_DEGREE) return p->chords[s]; }
    return 1;
}
// the row of a part nearest to one of a chord's notes (k: which note, octaves: rows of the scale above the home octave)
static int chord_row(int part, uint8_t chord, int k, int oct)
{
    int notes[4], n = px_chord_notes(chord, px_key(), px_scale(), notes);
    if (n < 1) { notes[0] = 48 + px_key(); n = 1; }
    int home = px_row_note(part, px_home_octave(part, px_key()) * scale_n(), px_key(), px_scale());
    int m = notes[k % n] % 12 + 12 * (home / 12) + 12 * oct;
    if (m < home - 5) m += 12;
    return px_note_row(part, m, px_key(), px_scale());
}

static void dice_drums(px_pattern_t *p)
{
    static const char *const K[] = { "x...x...x...x...", "x.....x.x.....x.", "x..x..x.x..x..x.", "x.......x.x.....", "x...x..xx...x...", "x.x...x.x.x...x." };
    static const char *const S[] = { "....x.......x...", "....x.......x..x", "....x..x....x...", "........x.......", "....x.......x.xx" };
    static const char *const H[] = { "x.x.x.x.x.x.x.x.", "xxxxxxxxxxxxxxxx", "..x...x...x...x.", "x.xxx.xxx.xxx.xx", "x...x...x...x..." };
    const char *k = K[roll(6)], *s = S[roll(5)], *h = H[roll(5)];
    for (int i = 0; i < PX_STEPS; i++) p->drums[i] = (uint8_t)((k[i] == 'x' ? DR_KICK : 0) | (s[i] == 'x' ? DR_SNARE : 0) | (h[i] == 'x' ? DR_HAT : 0));
    if (roll(3) == 0) { p->drums[14] &= (uint8_t)~DR_HAT; p->drums[14] |= DR_OPEN; }
    if (roll(4) == 0) { p->drums[12] |= DR_TOM | DR_ACCENT; p->drums[14] |= DR_TOM; }
    p->drums[0] |= DR_ACCENT;
}
static void dice_bass(px_pattern_t *p)
{
    // a beat's worth each: r the root, R the root an octave up, f the fifth, t the third, - held, . nothing
    static const char *const T[] = { "r.R.", "r..r", "rrrr", "r---", "r.f.", "r.rR", "r-.r" };
    const char *t = T[roll(7)];
    for (int i = 0; i < PX_STEPS; i++) {
        char c = i >= 12 && roll(3) == 0 ? "r.tf"[i & 3] : t[i & 3];
        uint8_t ch = chord_at(p, i & ~3);
        int r = c == 'r' ? chord_row(PART_BASS, ch, 0, 0) : c == 'R' ? chord_row(PART_BASS, ch, 0, 1) : c == 'f' ? chord_row(PART_BASS, ch, 2, 0)
              : c == 't' ? chord_row(PART_BASS, ch, 1, 0) : -1;
        p->bass[i] = c == '-' && i && (p->bass[i - 1] & NC_ROW) ? (uint8_t)((p->bass[i - 1] & NC_ROW) | NC_TIE) : r >= 0 ? (uint8_t)(r + 1) : 0;
    }
}
static void dice_lead(px_pattern_t *p)
{
    // rhythms: x a note, - held, . nothing. Notes on the beats are the chord's; between them the tune walks the scale
    static const char *const R[] = { "x.x.x-x.x.x.x---", "x-..x.x.x-..x.x.", "x.xxx.x.x.xxx-..", "x---x-x-x---x-x-", "xx.xx.x.xx.xx-..",
                                     "x-x-x.x.x-x-x---", "..x.x.x-..x.x.x-", "x..x..x.x..x..x-" };
    const char *t = R[roll(8)];
    int n = scale_n(), home = px_home_octave(PART_LEAD, px_key()) * n, lo = home - 2, hi = home + n + 4;
    int cur = chord_row(PART_LEAD, chord_at(p, 0), roll(3), 0);
    for (int i = 0; i < PX_STEPS; i++) {
        if (t[i] == '.') { p->lead[i] = 0; continue; }
        if (t[i] == '-') { p->lead[i] = i && (p->lead[i - 1] & NC_ROW) ? (uint8_t)((p->lead[i - 1] & NC_ROW) | NC_TIE) : 0; continue; }
        if ((i & 3) == 0 || i == 14) {                           // a strong step: the chord's note nearest to where the tune is
            uint8_t ch = chord_at(p, i);
            int best = cur, bd = 99;
            for (int k = 0; k < 3; k++) for (int o = 0; o < 2; o++) { int r = chord_row(PART_LEAD, ch, k, o), d = r > cur ? r - cur : cur - r; if (d < bd && (d || roll(2))) { bd = d; best = r; } }
            cur = best;
        } else cur += "\x01\x01\x02\x03\x00"[roll(4)] * (roll(2) ? 1 : -1);
        cur = clampi(cur, lo, hi);
        p->lead[i] = (uint8_t)((cur + 1) | ((i & 7) == 0 && roll(3) == 0 ? NC_ACCENT : 0));
    }
}
static void dice_chords(px_pattern_t *p)
{
    static const uint8_t pick[] = { 1, 1, 4, 5, 6, 6, 4, 5, 2, 3 };
    uint8_t before = chord_at(p, 0) & CH_DEGREE, d;
    do d = pick[roll(10)]; while (d == before);
    memset(p->chords, 0, PX_STEPS);
    p->chords[0] = d;
    if (roll(3) == 0) { uint8_t e; do e = pick[roll(10)]; while (e == d); p->chords[8] = e; }
}

void edit_action(int what)
{
    int p = edit_pattern();
    char b[32];
    if (what == EA_DICE) {
        int r = row == ROW_SONG ? ROW_DRUMS : row;
        dice_seed ^= audio_frames() * 2654435761u; if (!dice_seed) dice_seed = 1;
        px_pattern_t *pt = &px_song.pat[p];
        if (r == ROW_DRUMS) dice_drums(pt); else if (r == ROW_BASS) dice_bass(pt); else if (r == ROW_LEAD) dice_lead(pt); else dice_chords(pt);
        px_notes_edited();
        snprintf(b, sizeof b, "NEW %s IN BAR %c", row_name[r], 'A' + p);
        toast(b, 1200);
        return;
    }
    if (what == EA_COPY) { clip = px_song.pat[p]; clip_ok = true; snprintf(b, sizeof b, "BAR %c COPIED", 'A' + p); }
    else if (what == EA_PASTE) {
        if (!clip_ok) { toast("NOTHING COPIED YET", 1200); return; }
        px_song.pat[p] = clip; px_notes_edited(); snprintf(b, sizeof b, "PASTED INTO BAR %c", 'A' + p);
    } else { memset(&px_song.pat[p], 0, sizeof px_song.pat[p]); px_notes_edited(); snprintf(b, sizeof b, "BAR %c EMPTIED", 'A' + p); }
    toast(b, 1200);
}

// ---------------------------------------------------------------- drawing
#define GX 15                              // the grid's left edge; 16 columns of 7
#define CW 7
#define GY 26                              // the grid's top

// a letter in a box: white on black, or (on = the cursor's) black on a filled box
static void boxed(int x, int y, int w, int h, const char *t, bool on)
{
    oled_rect(x, y, w, h, false);
    oled_ftext_ex(F_TINY, x + (w - oled_ftext_w(F_TINY, t, 0) + 1) / 2, y + (h - 5) / 2, t, 0);
    if (on) oled_invert(x, y, w, h);
}

static void draw_song_strip(int y)
{
    // the bars in order: a letter each, the cursor's filled, a line under the one playing
    int n = px_song.order_n, per = 14, top = slot >= per ? slot - per + 1 : 0, w = 8;
    if (top > 0) oled_ftext_ex(F_TINY, 0, y + 1, S_LEFT, 0);
    for (int k = 0; k <= per; k++) {
        int i = top + k, x = 6 + k * w;
        if (i > n || (i == n && n >= PX_ORDER_MAX) || x > OLED_W - w) break;
        char t[2] = { (char)(i < n ? 'A' + (px_song.order[i] & 15) : '+'), 0 };
        boxed(x, y, w - 1, 7, t, i == slot);
        if (px_pos.running && px_pos.mode == PM_SONG && i == px_pos.slot) oled_rect(x + 1, y + 8, w - 3, 1, true);
    }
}

static void draw_ruler(int ph)
{
    for (int b = 0; b <= 4; b++) { int x = GX + b * 4 * CW - 1; if (x < OLED_W) for (int y = GY; y < 58; y += 3) oled_pixel(x, y, true); }
    if (ph >= 0) { int x = GX + ph * CW; oled_rect(x, GY - 2, CW - 1, 1, true); }
}

// the other parts as thin lanes under the grid: where they have something
static void draw_lanes(int skip_row)
{
    const px_pattern_t *p = pat();
    static const int rows[3][3] = { { ROW_CHORDS, ROW_BASS, ROW_DRUMS }, { ROW_LEAD, ROW_BASS, ROW_DRUMS }, { ROW_LEAD, ROW_CHORDS, ROW_DRUMS } };
    const int *r = skip_row == ROW_LEAD ? rows[0] : skip_row == ROW_CHORDS ? rows[1] : rows[2];
    int lane_rows[3] = { r[0], r[1], r[2] };
    if (skip_row == ROW_DRUMS) { lane_rows[0] = ROW_LEAD; lane_rows[1] = ROW_CHORDS; lane_rows[2] = ROW_BASS; }
    for (int k = 0; k < 3; k++) {
        const uint8_t *c = lane_rows[k] == ROW_LEAD ? p->lead : lane_rows[k] == ROW_BASS ? p->bass : lane_rows[k] == ROW_CHORDS ? p->chords : p->drums;
        int y = 59 + k * 2;
        oled_pixel(GX - 4, y, true);
        for (int s = 0; s < PX_STEPS; s++) if (c[s]) oled_rect(GX + s * CW, y, lane_rows[k] == ROW_DRUMS ? 3 : CW - 2, 1, true);
    }
}

static void draw_roll(int part, const uint8_t *c)
{
    int n = scale_n(), lo = win_lo[part];
    // the octave marks: a tick where a row is the key's own note
    for (int k = 0; k < 8; k++) {
        int r = lo + k, y = GY + (7 - k) * 4;
        if (r % n == 0) { oled_rect(GX - 5, y + 1, 3, 1, true); char b[4]; snprintf(b, sizeof b, "%d", (row_midi(part, r) / 12) - 1); oled_ftext_ex(F_TINY, 0, y - 1, b, 0); }
        else oled_pixel(GX - 3, y + 1, true);
    }
    for (int s = 0; s < PX_STEPS; s++) {
        if (!(c[s] & NC_ROW)) continue;
        int r = (c[s] & NC_ROW) - 1, k = r - lo, x = GX + s * CW;
        if (k < 0) { oled_pixel(x + 2, GY + 31, true); continue; }                 // off the window: a dot at the edge
        if (k > 7) { oled_pixel(x + 2, GY, true); continue; }
        int y = GY + (7 - k) * 4;
        if (is_tie(c, s)) {
            int pr = (c[s - 1] & NC_ROW) - 1 - lo;
            if (pr != k && pr >= 0 && pr <= 7) oled_line(x - 2, GY + (7 - pr) * 4 + 1, x, y + 1, true);   // a glide
            oled_rect(x - 1, y + 1, CW, 2, true);
        } else {
            oled_rect(x, y, CW - 1, 3, true);
            if (c[s] & NC_ACCENT) oled_rect(x, y - 1, 2, 1, true);
        }
    }
    // the cursor: a frame at the cursor's step and pitch
    int k = clampi(((c[step] & NC_ROW) ? (c[step] & NC_ROW) - 1 : cur_row[part]) - lo, 0, 7);
    if (blink < 9) oled_frame(GX + step * CW - 1, GY + (7 - k) * 4 - 1, CW + 1, 5);
}

static void draw_chords(const uint8_t *c)
{
    char b[12];
    int y = GY + 6;
    oled_hline_pat(GX, y + 9, 16 * CW, 0x11, true);
    for (int s = 0, last_x = -100; s < PX_STEPS; s++) {
        if (!c[s]) continue;
        int x = GX + s * CW;
        px_chord_name(c[s], px_scale(), b, sizeof b);
        int yy = x < last_x ? y + 12 : y;                         // a name that would run into the one before goes lower
        oled_rect(x, y + 7, 1, 5, true);
        last_x = oled_ftext_ex(F_SMALL, x + 2, yy, b, 1) + 2;
    }
    // the cursor: a box on the line under the names (not over them), and a mark above
    if (blink < 9) { int x = GX + step * CW; oled_frame(x - 1, y + 6, CW + 1, 7); oled_rect(x + 1, y - 4, 3, 1, true); oled_pixel(x + 2, y - 3, true); }
}

static void draw_drums(const uint8_t *c)
{
    static const char *const L[PX_DRUMS] = { "K", "S", "H", "O", "T" };
    for (int d = 0; d < PX_DRUMS; d++) {
        int y = GY + d * 6;
        boxed(1, y, 9, 6, L[d], d == drum);                       // the drum in hand: its letter in a filled box
        for (int s = 0; s < PX_STEPS; s++) {
            int x = GX + s * CW;
            if (c[s] & (1u << d)) { oled_rect(x, y + 1, CW - 2, 4, true); if (c[s] & DR_ACCENT) oled_rect(x, y, CW - 2, 1, true); }
            else oled_pixel(x + 2, y + 3, true);
        }
    }
    if (blink < 9) oled_frame(GX + step * CW - 1, GY + drum * 6, CW, 6);
}

void edit_draw(const view_t *v)
{
    char b[24], val[16] = "";
    blink = (int)(v->frame % 13);
    px_pattern_t *p = pat();
    bool playing_here = px_pos.running && px_pos.pattern == cur_pat();
    // the header: the row, the bar, what is under the cursor
    int x = oled_pill(F_SMALL, 0, 2, row_name[row], true) + 4;
    snprintf(b, sizeof b, "%c %d/%d", 'A' + cur_pat(), slot + 1 > px_song.order_n ? px_song.order_n : slot + 1, px_song.order_n);
    oled_ftext_ex(F_SMALL, x, 5, b, 1);
    if (row == ROW_LEAD || row == ROW_BASS) {
        int part = row == ROW_LEAD ? PART_LEAD : PART_BASS;
        const uint8_t *c = cells_of(row);
        int r = (c[step] & NC_ROW) ? (c[step] & NC_ROW) - 1 : cur_row[part];
        ui_note_name(row_midi(part, r) + 12 * (int)pv[param_by_tag(part == PART_BASS ? "boct" : "loct")], val, sizeof val);
    } else if (row == ROW_CHORDS) { if (p->chords[step]) px_chord_name(p->chords[step], px_scale(), val, sizeof val); else px_chord_name(last_chord, px_scale(), val, sizeof val); }
    else if (row == ROW_DRUMS) snprintf(val, sizeof val, "%s", drum_name[drum]);
    else snprintf(val, sizeof val, "%d BARS", px_song.order_n);
    oled_ftext_r(F_SMALL, OLED_W - (px_pos.running ? 9 : 1), 5, val, 1);
    if (px_pos.running) { for (int k = 0; k < 4; k++) oled_rect(OLED_W - 6 + k, 5 + k, 1, 7 - 2 * k, true); }
    else if (px_pos.armed && (v->frame & 4)) oled_rect(OLED_W - 6, 6, 4, 5, true);

    draw_song_strip(16);
    if (row == ROW_SONG) {
        // the song row has the screen: under the strip, the bar under the cursor in small (all four parts)
        const int ly[4] = { 34, 41, 48, 55 };
        static const char *const L[4] = { "L", "C", "B", "D" };
        const uint8_t *cs[4] = { p->lead, p->chords, p->bass, p->drums };
        for (int k = 0; k < 4; k++) {
            oled_ftext_ex(F_TINY, 3, ly[k], L[k], 0);
            for (int s = 0; s < PX_STEPS; s++) { int xx = GX + s * CW; if (cs[k][s]) oled_rect(xx, ly[k] + 1, CW - 2, 3, true); else oled_pixel(xx + 2, ly[k] + 2, true); }
        }
        if (playing_here) oled_rect(GX + px_pos.step * CW, 31, CW - 1, 1, true);
        oled_ftext_ex(F_TINY, GX, 27, slot >= px_song.order_n ? "PRESS: ONE MORE BAR" : "", 1);
        return;
    }
    draw_ruler(playing_here ? px_pos.step : -1);
    if (row == ROW_LEAD) draw_roll(PART_LEAD, p->lead);
    else if (row == ROW_BASS) draw_roll(PART_BASS, p->bass);
    else if (row == ROW_CHORDS) draw_chords(p->chords);
    else draw_drums(p->drums);
    // the editor's own coach: one line at the bottom until each thing has been done once
    const char *hint = NULL;
    if (idle_ms > 2500) {
        if (!(learned & 1)) hint = "STICK: MOVE.  UP, DOWN: THE PART";
        else if (!(learned & 2)) hint = row == ROW_DRUMS ? "TURN: PICK A DRUM" : "TURN THE KNOB: THE NOTE";
        else if (!(learned & 4)) hint = "PRESS: PUT IT, PRESS AGAIN: ACCENT";
    }
    if (hint) { oled_rect(0, 57, OLED_W, 7, false); oled_ftext_c(F_TINY, 64, 58, hint, 1); }
    else draw_lanes(row);
}
