// The song (pixel.h): scales, rows and chords; the notes' text form, in which the factory songs are written.
//
// The text: one line per pattern and part, tokens separated by spaces, a '|' anywhere is ignored.
//
//   AL: 5 . 1' . 3' 2' 1' . 5 . 1' . 7 - - .      pattern A, the LEAD: sixteen steps
//   AB: 1 . 1' . 1 . 1' . 1 . 1' . 1 . 1' 1       the BASS
//   AC: I . . . . . . . VI . . . . . . .          the CHORDS
//   AK: x...x...x...x...                          the drums, a line each: K kick, S snare, H hat, O open hat, T tom
//   ORDER: A A B A                                the song: the patterns in order
//
// A note is a degree of the scale, 1..7 (a b c for 10 11 12 in longer scales), in the part's home octave (the LEAD's
// starts two octaves above its lowest row, the BASS's one): ' after it is an octave up, , an octave down, ! an
// accent. "-" holds the note before; "~" in front slides to this note without a new attack; "." is nothing.
// A chord is I..VII (its kind comes from the scale), with 7 (a seventh), s (sus4) or 5 (root and fifth) after it;
// "x" stops the chords. A drum line is sixteen characters: x a hit, X an accented one, anything else none.
#include "pixel.h"
#include <stdio.h>
#include <string.h>

px_song_t px_song;

const px_scale_t PX_SCALES[PX_SC_N] = {
    [PX_SC_MAJOR]     = { "MAJOR",      7,  { 0, 2, 4, 5, 7, 9, 11 }, PX_SC_MAJOR },
    [PX_SC_MINOR]     = { "MINOR",      7,  { 0, 2, 3, 5, 7, 8, 10 }, PX_SC_MINOR },
    [PX_SC_DORIAN]    = { "DORIAN",     7,  { 0, 2, 3, 5, 7, 9, 10 }, PX_SC_DORIAN },
    [PX_SC_MIXO]      = { "MIXOLYDIAN", 7,  { 0, 2, 4, 5, 7, 9, 10 }, PX_SC_MIXO },
    [PX_SC_PHRYGIAN]  = { "PHRYGIAN",   7,  { 0, 1, 3, 5, 7, 8, 10 }, PX_SC_PHRYGIAN },
    [PX_SC_HARMONIC]  = { "HARM MINOR", 7,  { 0, 2, 3, 5, 7, 8, 11 }, PX_SC_HARMONIC },
    [PX_SC_PENTA_MAJ] = { "PENTA MAJOR", 5, { 0, 2, 4, 7, 9 }, PX_SC_MAJOR },
    [PX_SC_PENTA_MIN] = { "PENTA MINOR", 5, { 0, 3, 5, 7, 10 }, PX_SC_MINOR },
    [PX_SC_BLUES]     = { "BLUES",      6,  { 0, 3, 5, 6, 7, 10 }, PX_SC_MINOR },
    [PX_SC_CHROMATIC] = { "ALL NOTES",  12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, PX_SC_MAJOR },
};
const uint8_t PX_PART_BASE[PART_N] = { 0, 24, 48, 48 };

int px_note_row(int part, int midi, int key, int scale)
{
    int best = 0, bd = 1000;
    for (int r = 0; r < PX_ROWS; r++) {
        int d = px_row_note(part, r, key, scale) - midi;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = r; }
    }
    return best;
}

int px_chord_notes(uint8_t cell, int key, int scale, int notes[4])
{
    int d = (cell & CH_DEGREE) - 1;
    if (d < 0 || (cell & CH_OFF)) return 0;
    const px_scale_t *s = &PX_SCALES[PX_SCALES[scale < 0 || scale >= PX_SC_N ? 0 : scale].parent];
#define ABOVE(k) ((s->st[(d + (k)) % 7] - s->st[d] + 12) % 12)
    int root = 48 + (key + s->st[d]) % 12, third = ABOVE(2), fifth = ABOVE(4), n = 3;
    notes[0] = root;
    switch (cell & CH_KIND) {
    default:
    case CHK_TRIAD:   notes[1] = root + third; notes[2] = root + fifth; break;
    case CHK_SEVENTH: notes[1] = root + third; notes[2] = root + fifth; notes[3] = root + ABOVE(6); n = 4; break;
    case CHK_SUS:     notes[1] = root + ABOVE(3); notes[2] = root + fifth; break;
    case CHK_POWER:   notes[1] = root + fifth; notes[2] = root + 12; break;
    }
#undef ABOVE
    return n;
}

const char *px_chord_name(uint8_t cell, int scale, char *b, int n)
{
    static const char *const up[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const char *const lo[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };
    if (cell & CH_OFF) { snprintf(b, (size_t)n, "OFF"); return b; }
    int d = (cell & CH_DEGREE) - 1;
    if (d < 0) { snprintf(b, (size_t)n, "-"); return b; }
    const px_scale_t *s = &PX_SCALES[PX_SCALES[scale < 0 || scale >= PX_SC_N ? 0 : scale].parent];
    int third = (s->st[(d + 2) % 7] - s->st[d] + 12) % 12, kind = cell & CH_KIND;
    bool minor = third == 3 && kind != CHK_SUS && kind != CHK_POWER;
    snprintf(b, (size_t)n, "%s%s", minor ? lo[d] : up[d], kind == CHK_SEVENTH ? "7" : kind == CHK_SUS ? "sus" : kind == CHK_POWER ? "5" : "");
    return b;
}

void px_song_clear(px_song_t *s)
{
    memset(s, 0, sizeof *s);
    s->order_n = 1;
}

// ---------------------------------------------------------------- the text
static const char *skip_sp(const char *p) { while (*p == ' ' || *p == '\t' || *p == '|') p++; return p; }

static int note_token(const char **pp, int part, int scale_n, uint8_t prev_row, uint8_t *cell)
{
    const char *p = *pp;
    bool slide = false;
    *cell = 0;
    if (*p == '.') { *pp = p + 1; return 0; }
    if (*p == '-') { *cell = prev_row ? (uint8_t)(prev_row | NC_TIE) : 0; *pp = p + 1; return 0; }
    if (*p == '~') { slide = true; p++; }
    int deg;
    if (*p >= '1' && *p <= '9') deg = *p - '1';
    else if (*p >= 'a' && *p <= 'c') deg = 9 + (*p - 'a');
    else return -1;
    if (deg >= scale_n) return -1;
    p++;
    int oct = part == PART_LEAD ? 2 : 1;
    bool acc = false;
    for (;; p++) {
        if (*p == '\'') oct++;
        else if (*p == ',') oct--;
        else if (*p == '!') acc = true;
        else break;
    }
    int row = oct * scale_n + deg;
    if (row < 0 || row >= PX_ROWS) return -1;
    *cell = (uint8_t)((row + 1) | (slide ? NC_TIE : 0) | (acc ? NC_ACCENT : 0));
    *pp = p;
    return 0;
}

static int chord_token(const char **pp, uint8_t *cell)
{
    const char *p = *pp;
    *cell = 0;
    if (*p == '.') { *pp = p + 1; return 0; }
    if (*p == 'x') { *cell = CH_OFF; *pp = p + 1; return 0; }
    static const char *const rom[7] = { "VII", "VI", "IV", "V", "III", "II", "I" };
    static const uint8_t deg[7] = { 7, 6, 4, 5, 3, 2, 1 };
    for (int i = 0; i < 7; i++) {
        size_t n = strlen(rom[i]);
        if (strncmp(p, rom[i], n)) continue;
        p += n;
        uint8_t kind = CHK_TRIAD;
        if (*p == '7') { kind = CHK_SEVENTH; p++; }
        else if (*p == 's') { kind = CHK_SUS; p++; }
        else if (*p == '5') { kind = CHK_POWER; p++; }
        *cell = (uint8_t)(deg[i] | kind);
        *pp = p;
        return 0;
    }
    return -1;
}

bool px_song_parse(px_song_t *s, const char *text, int key, int scale, char *err, int errn)
{
    (void)key;
    px_song_clear(s);
    const px_scale_t *sc = &PX_SCALES[scale < 0 || scale >= PX_SC_N ? 0 : scale];
    int line = 0;
    bool have_order = false;
    for (const char *p = text; *p; ) {
        line++;
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char buf[160];
        if (len >= sizeof buf) { if (err) snprintf(err, (size_t)errn, "line %d too long", line); return false; }
        memcpy(buf, p, len); buf[len] = 0;
        p += len + (e ? 1 : 0);
        const char *q = skip_sp(buf);
        if (!*q || *q == '#') continue;
        if (!strncmp(q, "ORDER:", 6)) {
            q = skip_sp(q + 6);
            int n = 0;
            while (*q) {
                if (*q < 'A' || *q >= 'A' + PX_PATTERNS || n >= PX_ORDER_MAX) { if (err) snprintf(err, (size_t)errn, "line %d: order", line); return false; }
                s->order[n++] = (uint8_t)(*q - 'A');
                q = skip_sp(q + 1);
            }
            if (!n) { if (err) snprintf(err, (size_t)errn, "line %d: empty order", line); return false; }
            s->order_n = (uint8_t)n; have_order = true;
            continue;
        }
        if (q[0] < 'A' || q[0] >= 'A' + PX_PATTERNS || !q[1] || q[2] != ':') { if (err) snprintf(err, (size_t)errn, "line %d: what is '%.8s'", line, q); return false; }
        px_pattern_t *pt = &s->pat[q[0] - 'A'];
        char part = q[1];
        q = skip_sp(q + 3);
        if (part == 'L' || part == 'B') {
            uint8_t *row = part == 'L' ? pt->lead : pt->bass, prev = 0;
            for (int i = 0; i < PX_STEPS && *q; i++) {
                uint8_t c;
                if (note_token(&q, part == 'L' ? PART_LEAD : PART_BASS, sc->n, prev, &c)) { if (err) snprintf(err, (size_t)errn, "line %d step %d: note '%.6s'", line, i + 1, q); return false; }
                row[i] = c;
                prev = c & NC_ROW;
                q = skip_sp(q);
            }
        } else if (part == 'C') {
            for (int i = 0; i < PX_STEPS && *q; i++) {
                if (chord_token(&q, &pt->chords[i])) { if (err) snprintf(err, (size_t)errn, "line %d step %d: chord '%.6s'", line, i + 1, q); return false; }
                q = skip_sp(q);
            }
        } else {
            uint8_t bit = part == 'K' ? DR_KICK : part == 'S' ? DR_SNARE : part == 'H' ? DR_HAT : part == 'O' ? DR_OPEN : part == 'T' ? DR_TOM : 0;
            if (!bit) { if (err) snprintf(err, (size_t)errn, "line %d: part '%c'", line, part); return false; }
            for (int i = 0; i < PX_STEPS && *q; q++) {
                if (*q == ' ' || *q == '|') continue;
                if (*q == 'x' || *q == 'X') pt->drums[i] |= (uint8_t)(bit | (*q == 'X' ? DR_ACCENT : 0));
                i++;
            }
        }
    }
    if (!have_order) { s->order[0] = 0; s->order_n = 1; }
    return true;
}
