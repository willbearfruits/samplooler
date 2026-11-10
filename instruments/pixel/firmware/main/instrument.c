// PIXEL's host (pixel.h): the params, the songs as the core's sounds, the clock and the link, the transport, the
// hands, the console that sounds.
//
//   the knob      turn: the CONSOLE, from the one-bit beeper to the Mega Drive (it is always the knob's); press: the
//                 menu; hold: the editor (inst_edit.c), hold again to leave it
//   REC           tap: play the song from the start, tap again to stop (in the editor: the bar under the cursor)
//   the stick     a push mutes or brings back a part: up LEAD, right CHORDS, down BASS, left DRUMS
//   the tilt      forward: the cartridge tilts and the tune glitches; rolled left or right: everything bends
//
// The clock runs in the audio task from the flock's beat (link.h): a bar is 16 steps, a step a whole number of driver
// ticks (pixel.h). Blocks are split at tick boundaries, so the player and the console run sample exact.
#include "pixel.h"
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
static inline uint32_t CYCLES(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)((uint64_t)t.tv_sec * 250000000ULL + (uint64_t)t.tv_nsec / 4); }
#endif

// ---------------------------------------------------------------- the params
enum {
    P_CONSOLE, P_FRAME,
    P_TEMPO, P_SWING, P_KEY, P_SCALE,
    P_LSND, P_LLVL, P_LOCT, P_LLEN, P_LVIB, P_LECH, P_LSLD,
    P_CSND, P_CLVL, P_COCT, P_CARP, P_CSHP, P_CRHY,
    P_BSND, P_BLVL, P_BOCT, P_BLEN,
    P_DKIT, P_DLVL,
    P_GLITCH,
    P_ECOPY, P_EPASTE, P_ECLEAR,
    P_TILT, P_BEND, P_STICK,
    P_LINK, P_GROUP, P_RANGE, P_PEERS,
    P_NREV,
    P_N
};
enum { G_TUNE, G_CONSOLE, G_LEAD, G_CHORDS, G_BASS, G_DRUMS, G_BAR, G_HANDS, G_LINK, G_N };
static const char *const groups[G_N] = { "TUNE", "CONSOLE", "LEAD", "CHORDS", "BASS", "DRUMS", "BAR", "HANDS", "LINK" };

static const char *con_l[CON_N], *scale_l[PX_SC_N];
static const char *const frame_l[] = { "60 HZ", "50 HZ" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const snd_l[] = { "1", "2", "3", "4" };
static const char *const arp_l[] = { "OFF", "SLOW", "MID", "FAST" };
static const char *const shape_l[] = { "UP", "DOWN", "UP DOWN", "OCTAVES", "RANDOM" };
static const char *const rhy_l[] = { "HELD", "EIGHTHS", "OFFBEAT", "GALLOP", "FOURS" };
static const char *const echo_l[] = { "OFF", "SHORT", "MID", "LONG" };
static const char *const off_on[] = { "OFF", "ON" };
static const char *const stick_l[] = { "OFF", "MUTES" };
static const char *const range_l[] = { "NEAR", "ROOM", "FAR" };

#define SYS PF_SYS
#define ACT PK_ACT, PF_NOCC
static const param_t params[P_N] = {
    [P_CONSOLE] = { "CONSOLE", "cons", G_CONSOLE, PK_ENUM, 0, 0, CON_N - 1, 1, CON_NES, "", con_l, "THE CHIP THAT PLAYS IT" },
    [P_FRAME]   = { "FRAME", "fram", G_CONSOLE, PK_ENUM, 0, 0, 1, 1, 0, "", frame_l, "50: SLOWER ARPS, AS ON PAL" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_TUNE, PK_NUM, PF_INT, 60, 240, 1, 150, "bpm", NULL, "THE SONG'S TEMPO" },
    [P_SWING]   = { "SWING", "swng", G_TUNE, PK_NUM, PF_INT, 0, 50, 2, 0, "%", NULL, "EVERY OTHER STEP IS LATE" },
    [P_KEY]     = { "KEY", "key ", G_TUNE, PK_ENUM, 0, 0, 11, 1, 9, "", key_l, "MOVES THE WHOLE SONG UP" },
    [P_SCALE]   = { "SCALE", "scal", G_TUNE, PK_ENUM, 0, 0, PX_SC_N - 1, 1, PX_SC_MINOR, "", scale_l, "THE NOTES OF THE ROWS" },
    [P_LSND]    = { "SOUND", "lsnd", G_LEAD, PK_ENUM, 0, 0, 3, 1, 0, "", snd_l, "THE CONSOLE'S LEADS, 1 TO 4" },
    [P_LLVL]    = { "LEVEL", "llvl", G_LEAD, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "HOW LOUD THE LEAD IS" },
    [P_LOCT]    = { "OCTAVE", "loct", G_LEAD, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "AN OCTAVE UP OR DOWN" },
    [P_LLEN]    = { "LENGTH", "llen", G_LEAD, PK_NUM, PF_INT, 10, 100, 5, 85, "%", NULL, "SHORT NOTES OR JOINED ONES" },
    [P_LVIB]    = { "VIBRATO", "lvib", G_LEAD, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "LONG NOTES WOBBLE" },
    [P_LECH]    = { "ECHO", "lech", G_LEAD, PK_ENUM, 0, 0, 3, 1, 2, "", echo_l, "THE LEAD AGAIN, LATER" },
    [P_LSLD]    = { "SLIDE", "lsld", G_LEAD, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "HOW LONG A GLIDE TAKES" },
    [P_CSND]    = { "SOUND", "csnd", G_CHORDS, PK_ENUM, 0, 0, 3, 1, 0, "", snd_l, "THE CONSOLE'S CHORD SOUNDS" },
    [P_CLVL]    = { "LEVEL", "clvl", G_CHORDS, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "HOW LOUD THE CHORDS ARE" },
    [P_COCT]    = { "OCTAVE", "coct", G_CHORDS, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "AN OCTAVE UP OR DOWN" },
    [P_CARP]    = { "ARP", "carp", G_CHORDS, PK_ENUM, 0, 0, 3, 1, ARP_FAST, "", arp_l, "HOW FAST A CHORD IS BROKEN" },
    [P_CSHP]    = { "SHAPE", "cshp", G_CHORDS, PK_ENUM, 0, 0, SHAPE_N - 1, 1, SHAPE_UP, "", shape_l, "THE ORDER OF ITS NOTES" },
    [P_CRHY]    = { "RHYTHM", "crhy", G_CHORDS, PK_ENUM, 0, 0, RHY_N - 1, 1, RHY_HELD, "", rhy_l, "HELD, OR STABS IN A PATTERN" },
    [P_BSND]    = { "SOUND", "bsnd", G_BASS, PK_ENUM, 0, 0, 3, 1, 0, "", snd_l, "THE CONSOLE'S BASSES, 1 TO 4" },
    [P_BLVL]    = { "LEVEL", "blvl", G_BASS, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "HOW LOUD THE BASS IS" },
    [P_BOCT]    = { "OCTAVE", "boct", G_BASS, PK_NUM, PF_INT | PF_SIGNED, -2, 2, 1, 0, "", NULL, "AN OCTAVE UP OR DOWN" },
    [P_BLEN]    = { "LENGTH", "blen", G_BASS, PK_NUM, PF_INT, 10, 100, 5, 80, "%", NULL, "SHORT NOTES OR JOINED ONES" },
    [P_DKIT]    = { "KIT", "dkit", G_DRUMS, PK_ENUM, 0, 0, 3, 1, 0, "", snd_l, "THE CONSOLE'S DRUM KITS" },
    [P_DLVL]    = { "LEVEL", "dlvl", G_DRUMS, PK_NUM, PF_INT, 0, 100, 5, 100, "%", NULL, "HOW LOUD THE DRUMS ARE" },
    [P_GLITCH]  = { "GLITCH", "glit", G_CONSOLE, PK_NUM, PF_INT, 0, 100, 5, 0, "%", NULL, "A BADLY SEATED CARTRIDGE" },
    [P_ECOPY]   = { "COPY", "ecpy", G_BAR, ACT, 0, 0, 0, 0, "", NULL, "REMEMBER THIS BAR" },
    [P_EPASTE]  = { "PASTE", "epst", G_BAR, ACT, 0, 0, 0, 0, "", NULL, "PUT THE COPIED BAR HERE" },
    [P_ECLEAR]  = { "CLEAR", "eclr", G_BAR, ACT, 0, 0, 0, 0, "", NULL, "EMPTY THIS BAR" },
    [P_TILT]    = { "TILT", "htlt", G_HANDS, PK_ENUM, SYS, 0, 1, 1, 1, "", off_on, "FORWARD GLITCH, ROLL BEND" },
    [P_BEND]    = { "BEND", "hbnd", G_HANDS, PK_NUM, PF_INT | SYS, 1, 12, 1, 2, "st", NULL, "HOW FAR A ROLL BENDS" },
    [P_STICK]   = { "STICK", "hstk", G_HANDS, PK_ENUM, SYS, 0, 1, 1, 1, "", stick_l, "A PUSH MUTES A PART" },
    [P_LINK]    = { "LINK", "nlnk", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 1, 1, 1, "", off_on, "SYNC WITH BOARDS NEARBY" },
    [P_GROUP]   = { "GROUP", "ngrp", G_LINK, PK_NUM, SYS | PF_NOCC | PF_INT, 1, 8, 1, 1, "", NULL, "SAME GROUP: THEY SYNC" },
    [P_RANGE]   = { "RANGE", "npow", G_LINK, PK_ENUM, SYS | PF_NOCC, 0, 2, 1, 1, "", range_l, "HOW FAR THE RADIO REACHES" },
    [P_PEERS]   = { "WHO IS THERE", "nwho", G_LINK, ACT, 0, 0, 0, 0, "", NULL, "THE BOARDS LINKED NOW" },
    [P_NREV]    = { "NOTES", "nrev", G_TUNE, PK_NUM, PF_INT | PF_HIDE | PF_NOCC, 0, 16777215, 1, 0, "", NULL, NULL },
};
int px_param_nrev(void) { return P_NREV; }

__attribute__((constructor)) static void build_labels(void)
{
    for (int i = 0; i < CON_N; i++) con_l[i] = PX_CONSOLES[i]->name;
    for (int i = 0; i < PX_SC_N; i++) scale_l[i] = PX_SCALES[i].name;
}

// ---------------------------------------------------------------- the clock
// A bar is 4 beats of the flock's tempo, 16 steps; pairs of steps share an eighth, the second one later with SWING.
// Positions are frames since the session's beat 0, wrapping after WRAP_BEATS beats (a whole number of bars).
#define WRAP_BEATS 12288
typedef struct { uint32_t fpb, origin; uint16_t swing; uint8_t hz; } grid_t;
static volatile grid_t   grid = { 12800, 0, 0, 60 };
static volatile uint32_t grid_seq;                // odd while the UI task writes it
static inline IRAM_ATTR void grid_get(grid_t *g)
{
    for (;;) {
        uint32_t s = grid_seq;
        if (s & 1u) continue;
        g->fpb = grid.fpb; g->origin = grid.origin; g->swing = grid.swing; g->hz = grid.hz;
        if (s == grid_seq) return;
    }
}
static inline int32_t imod(int32_t a, int32_t m) { int32_t r = a % m; return r < 0 ? r + m : r; }
static inline int32_t g_pos(const grid_t *g, uint32_t frame) { return imod((int32_t)(frame - g->origin), (int32_t)(g->fpb * WRAP_BEATS)); }
static inline int32_t g_pair_start(const grid_t *g, int32_t p) { return (int32_t)(((int64_t)p * g->fpb + 1) / 2); }
static inline IRAM_ATTR int32_t g_step_start(const grid_t *g, int32_t s)
{
    int32_t p = s >> 1, a = g_pair_start(g, p);
    if (!(s & 1)) return a;
    return a + (int32_t)(((int64_t)(g_pair_start(g, p + 1) - a) * (100 + g->swing)) / 200);
}
static inline int g_tps(const grid_t *g)          // ticks in a step: the nearest whole number to the FRAME rate
{
    int t = (int)(((uint64_t)g->hz * g->fpb + 2u * AUDIO_SR) / (4u * AUDIO_SR));
    return t < 2 ? 2 : t > 16 ? 16 : t;
}
// the step and the tick a position falls in, the step's length, and the frames from there to the next tick (>= 1)
static IRAM_ATTR void g_locate(const grid_t *g, int32_t pos, int tps, int32_t *step, int *tick, int32_t *len, int32_t *to_next)
{
    int32_t p = (int32_t)(((int64_t)pos * 2) / g->fpb);
    if (g_pair_start(g, p) > pos) p--;
    else if (g_pair_start(g, p + 1) <= pos) p++;
    int32_t s = 2 * p;
    if (pos >= g_step_start(g, s + 1)) s++;
    int32_t a = g_step_start(g, s), l = g_step_start(g, s + 1) - a, rel = pos - a;
    if (l < 1) l = 1;
    int t = (int)(((int64_t)rel * tps) / l);
    if (t > tps - 1) t = tps - 1;
#define TICK_AT(k) ((int32_t)(((int64_t)l * (k) + tps - 1) / tps))
    while (t + 1 < tps && TICK_AT(t + 1) <= rel) t++;
    while (t > 0 && TICK_AT(t) > rel) t--;
    int32_t nx = (t + 1 < tps ? TICK_AT(t + 1) : l) - rel;
#undef TICK_AT
    *step = s; *tick = t; *len = l; *to_next = nx < 1 ? 1 : nx;
}

// ---------------------------------------------------------------- the song's time and the link (as WRECK's)
static struct { float bpm; uint32_t fpb, origin; float origin_frac; int loop_beats; } song = { 150, 12800, 0, 0, 0 };
static bool link_up;
static int  peers_was, eff_key = 9, eff_scale = PX_SC_MINOR, chord_sent = -2;
int px_key(void) { return eff_key; }
int px_scale(void) { return eff_scale; }
int px_peers(void) { return peers_was; }

static uint32_t tempo_fpb(float bpm) { return (uint32_t)(60.0f * (float)AUDIO_SR / (bpm < 20.0f ? 20.0f : bpm) + 0.5f); }
static void grid_set(void)
{
    __atomic_add_fetch(&grid_seq, 1u, __ATOMIC_ACQ_REL);
    grid.fpb = song.fpb ? song.fpb : 12800; grid.origin = song.origin;
    grid.swing = (uint16_t)pv[P_SWING]; grid.hz = pv[P_FRAME] >= 1.0f ? 50 : 60;
    __atomic_add_fetch(&grid_seq, 1u, __ATOMIC_ACQ_REL);
}
static void link_start(void)
{
    link_init("PIXEL", 1);
    link_boot_time(tempo_fpb(pv[P_TEMPO]), song.origin, 4);
    link_boot_harm((int)pv[P_KEY], PX_SCALES[(int)pv[P_SCALE]].parent, 0, 0, 0);
    link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
    link_up = true;
}
// the song's own tempo becomes the session's (a loop on another board keeps its tempo: then the song follows it)
static void tempo_set_here(bool say)
{
    uint32_t fpb = tempo_fpb(pv[P_TEMPO]);
    if (fpb == song.fpb) return;
    if (link_flags_any() & LINKF_LOOP) { if (say) toast("A LOOP KEEPS THE TEMPO", 1200); return; }
    uint32_t now = audio_frames();
    int32_t span = (int32_t)(song.fpb * WRAP_BEATS);
    float beat = (float)imod((int32_t)(now - song.origin), span) / (float)song.fpb;
    uint32_t origin = now - (uint32_t)(beat * (float)fpb);
    song.fpb = fpb; song.origin = origin; song.origin_frac = 0;
    song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
    link_set_time(fpb, origin, 4, 0);
    grid_set();
}
static void harm_set_here(int live) { link_set_harm(eff_key, PX_SCALES[eff_scale].parent, 0, live >= 0 ? live : 0, 0, live); }

static void link_follow(void)
{
    if (link_synced()) {
        uint32_t o; float f; link_origin(&o, &f);
        uint32_t fpb = link_fpb();
        if (fpb && (fpb != song.fpb || o != song.origin)) {
            song.fpb = fpb; song.origin = o; song.origin_frac = f;
            song.bpm = 60.0f * (float)AUDIO_SR / (float)fpb;
            grid_set();
        }
    }
    song.loop_beats = link_time()->beats;
    link_time_news();
    if (link_harm_news()) {                                   // another board changed the key or the scale: play in it
        const link_harm_t *h = link_harm();
        eff_key = h->key % 12;
        if (h->scale < PX_SC_N && PX_SCALES[eff_scale].parent != h->scale) eff_scale = h->scale;
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

// ---------------------------------------------------------------- the transport (UI task asks, the audio task does)
enum { CMD_NONE, CMD_STOP, CMD_SONG, CMD_PATTERN };
static volatile uint32_t cmd_seq;
static volatile uint8_t  cmd_kind, cmd_arg;
volatile px_pos_t px_pos = { .tps = 6, .tick_hz = 60, .bpm = 150, .console = CON_NES };
volatile uint8_t  px_mute;
static volatile int8_t  live_note[PART_N] = { -1, -1, -1, -1 };
static volatile uint8_t live_drums;
static volatile float   hand_bend, hand_glitch;
float px_glitch_now(void) { float g = pv[P_GLITCH] * 0.01f; return g > hand_glitch ? g : hand_glitch; }
float px_bend_now(void) { return hand_bend; }

static void cmd(int kind, int arg)
{
    cmd_kind = (uint8_t)kind; cmd_arg = (uint8_t)arg;
    __atomic_add_fetch(&cmd_seq, 1u, __ATOMIC_ACQ_REL);
}
// Alone, a start is at once: beat 0 moves to a moment from now and the bar begins there. With boards linked the bar
// line is the flock's, and the start waits for it.
static void start_now(void)
{
    if (px_pos.running || px_pos.armed || peers_was > 0 || (link_flags_any() & LINKF_LOOP)) return;
    uint32_t origin = audio_frames() + 4 * AUDIO_BLOCK;
    song.origin = origin; song.origin_frac = 0;
    if (link_up) link_set_time(song.fpb, origin, 4, 0);
    grid_set();
}
void px_play_song(int slot) { start_now(); cmd(CMD_SONG, slot < 0 ? 0 : slot); }
void px_play_pattern(int pat) { start_now(); cmd(CMD_PATTERN, pat & (PX_PATTERNS - 1)); }
void px_stop(void) { cmd(CMD_STOP, 0); }

// ---------------------------------------------------------------- audio
#define OUT_GAIN   0.7f
#define FADE_STEP  (1.0f / 96.0f)                  // a console's fade out and in: 3 ms each
static px_player_t *player;
static struct {
    bool     running, armed, pending;
    uint8_t  mode, want_mode, slot, pat, want;
    uint32_t bars;
} T;
static uint32_t cmd_seen;
static int32_t  last_lin = -1;
static int      last_tps;
static const px_console_t *con_cur;
static int      con_idx = CON_NES;
static float    fade = 0.0f, fade_to = 1.0f;
static volatile uint32_t prof_render, prof_tick, prof_blocks;
static volatile float out_env_v;
const px_console_t *px_console(void) { return PX_CONSOLES[px_pos.console < CON_N ? px_pos.console : CON_NES]; }

static IRAM_ATTR void do_tick(int32_t gstep, int tick, int tps, float tick_hz)
{
    int step = (int)(gstep & (PX_STEPS - 1));
    bool bar_line = step == 0 && tick == 0;
    uint32_t cs = cmd_seq;
    if (cs != cmd_seen) {
        cmd_seen = cs;
        int k = cmd_kind;
        if (k == CMD_STOP) { T.running = T.armed = T.pending = false; }
        else if (k == CMD_SONG || k == CMD_PATTERN) {
            T.want_mode = k == CMD_SONG ? PM_SONG : PM_PATTERN; T.want = cmd_arg;
            if (T.running) T.pending = true; else T.armed = true;
        }
    }
    int on = px_song.order_n ? px_song.order_n : 1;
    if (on > PX_ORDER_MAX) on = PX_ORDER_MAX;
    if (bar_line) {
        if (T.armed || T.pending) {
            if (T.armed) T.bars = 0; else T.bars++;
            T.armed = T.pending = false; T.running = true; T.mode = T.want_mode;
            if (T.mode == PM_SONG) T.slot = (uint8_t)(T.want % on); else T.pat = T.want & (PX_PATTERNS - 1);
        } else if (T.running) {
            T.bars++;
            if (T.mode == PM_SONG) T.slot = (uint8_t)((T.slot + 1) % on);
        }
    }
    if (T.slot >= on) T.slot = 0;
    int pi = T.mode == PM_SONG ? px_song.order[T.slot] & (PX_PATTERNS - 1) : T.pat;
    int ni = T.mode == PM_SONG ? px_song.order[(T.slot + 1) % on] & (PX_PATTERNS - 1) : T.pat;

    px_ctl_t c;
    c.key = (uint8_t)eff_key; c.scale = (uint8_t)eff_scale;
    c.oct[PART_DRUMS] = 0; c.oct[PART_BASS] = (int8_t)pv[P_BOCT]; c.oct[PART_CHORDS] = (int8_t)pv[P_COCT]; c.oct[PART_LEAD] = (int8_t)pv[P_LOCT];
    c.level[PART_DRUMS] = pv[P_DLVL] * 0.01f; c.level[PART_BASS] = pv[P_BLVL] * 0.01f;
    c.level[PART_CHORDS] = pv[P_CLVL] * 0.01f; c.level[PART_LEAD] = pv[P_LLVL] * 0.01f;
    c.sound[PART_DRUMS] = (uint8_t)pv[P_DKIT]; c.sound[PART_BASS] = (uint8_t)pv[P_BSND];
    c.sound[PART_CHORDS] = (uint8_t)pv[P_CSND]; c.sound[PART_LEAD] = (uint8_t)pv[P_LSND];
    c.mute = px_mute;
    c.len_bass = (uint8_t)pv[P_BLEN]; c.len_lead = (uint8_t)pv[P_LLEN];
    c.arp = (uint8_t)pv[P_CARP]; c.shape = (uint8_t)pv[P_CSHP]; c.rhythm = (uint8_t)pv[P_CRHY];
    c.vibrato = (uint8_t)pv[P_LVIB]; c.echo = (uint8_t)pv[P_LECH]; c.slide = (uint8_t)pv[P_LSLD];
    c.bend = hand_bend;
    float g = pv[P_GLITCH] * 0.01f, hg = hand_glitch;
    c.glitch = g > hg ? g : hg;
    for (int i = 0; i < PART_N; i++) c.live[i] = live_note[i];
    c.live_drums = __atomic_exchange_n(&live_drums, 0, __ATOMIC_ACQ_REL);

    // the knob's console: the old one fades out, then the new one starts on a tick
    int want = (int)pv[P_CONSOLE];
    if (want < 0 || want >= CON_N) want = CON_NES;
    if (want != con_idx) {
        fade_to = 0.0f;
        if (fade <= 0.0f) { con_idx = want; con_cur = PX_CONSOLES[want]; con_cur->reset(); px_player_restart(player); fade_to = 1.0f; }
    }
    px_frame_t f;
    px_player_tick(player, T.running ? &px_song.pat[pi] : NULL, T.running ? &px_song.pat[ni] : NULL, step, tick, tps, tick_hz, &c, &f);
    con_cur->tick(&f);

    px_pos.running = T.running; px_pos.armed = T.armed; px_pos.mode = T.mode; px_pos.slot = T.slot;
    px_pos.pattern = (uint8_t)pi; px_pos.step = (uint8_t)step; px_pos.tick = (uint8_t)tick; px_pos.tps = (uint8_t)tps;
    px_pos.console = (uint8_t)con_idx; px_pos.tick_hz = tick_hz; px_pos.bars = T.bars;
    if (bar_line || !T.running || f.chord.trig) {
        int d = 0;
        if (T.running && f.chord.on) for (int s = step; s >= 0 && !d; s--) { uint8_t cc = px_song.pat[pi].chords[s]; if (cc & CH_OFF) break; d = cc & CH_DEGREE; }
        if (d || !f.chord.on) px_pos.degree = (uint8_t)d;
    }
}

static void init(void)
{
    player = px_player_new();
    for (int i = 0; i < CON_N; i++) PX_CONSOLES[i]->init();
    con_idx = CON_NES; con_cur = PX_CONSOLES[con_idx]; con_cur->reset();
    px_song_clear(&px_song);
    lib_init();
}

static IRAM_ATTR void audio(const float *in, float *out, int n)
{
    (void)in;
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    uint32_t f0 = audio_frames(), c_tick = 0, c0 = CYCLES();
    grid_t g; grid_get(&g);
    int tps = g_tps(&g);
    if (tps != last_tps) { last_tps = tps; last_lin = -1; }
    for (int i = 0; i < n; ) {
        int32_t step, len, to_next; int tick;
        g_locate(&g, g_pos(&g, f0 + (uint32_t)i), tps, &step, &tick, &len, &to_next);
        int32_t lin = step * tps + tick, d = lin - last_lin;
        if (last_lin < 0 || d > 0 || d < -4 * tps) {             // a new tick (a little way back is the link's clock
            last_lin = lin;                                      // settling: the same tick again, not a new one)
            uint32_t t0 = CYCLES();
            do_tick(step, tick, tps, (float)AUDIO_SR * (float)tps / (float)len);
            c_tick += CYCLES() - t0;
        }
        int k = n - i < to_next ? n - i : (int)to_next;
        con_cur->render(out + i, k);
        i += k;
    }
    float pk = 0;
    for (int i = 0; i < n; i++) {
        if (fade != fade_to) { fade += fade < fade_to ? FADE_STEP : -FADE_STEP; if (fade > 1.0f) fade = 1.0f; if (fade < 0.0f) fade = 0.0f; }
        float x = out[i] * fade * OUT_GAIN;
        out[i] = x;
        float a = fabsf(x); if (a > pk) pk = a;
    }
    out_env_v = pk > out_env_v ? pk : out_env_v * 0.95f;
    prof_render += CYCLES() - c0 - c_tick; prof_tick += c_tick; prof_blocks++;
}
void px_prof(float *render_us, float *tick_us)          // per block since the last call
{
    uint32_t b = prof_blocks ? prof_blocks : 1;
    *render_us = (float)prof_render / (float)b / 240.0f; *tick_us = (float)prof_tick / (float)b / 240.0f;
    prof_render = prof_tick = 0; prof_blocks = 0;
}

// ---------------------------------------------------------------- the UI side
static int  aud_ms[PART_N], aud_seq[4], aud_seq_n, aud_seq_i, aud_seq_ms;
static int8_t midi_note_on = -1;
static bool rec_was, rec_ignore = true;
static int  rec_ms, rec_up_ms, stick_dir, beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;

static void live_set(void)
{
    live_note[PART_BASS] = (int8_t)(aud_ms[PART_BASS] > 0 ? aud_seq[0] : -1);
    live_note[PART_LEAD] = (int8_t)(aud_ms[PART_LEAD] > 0 ? aud_seq[1] : midi_note_on);
}
void px_audition(int part, int note, int ms)
{
    if (part != PART_BASS && part != PART_LEAD) return;
    aud_seq_n = 0;
    aud_seq[part == PART_BASS ? 0 : 1] = note;
    aud_ms[part] = note < 0 ? 0 : ms;
    live_set();
}
void px_audition_drums(uint8_t bits) { __atomic_or_fetch(&live_drums, bits & 0x1F, __ATOMIC_ACQ_REL); }
void px_audition_chord(uint8_t cell, int ms)
{
    int notes[4], n = px_chord_notes(cell, eff_key, eff_scale, notes);
    if (!n) return;
    aud_seq_n = n; aud_seq_i = 0; aud_seq_ms = ms / n;
    static int seq[4];
    for (int i = 0; i < n; i++) seq[i] = notes[i] + 12 * (int)pv[P_COCT] + 12;
    aud_seq[1] = seq[0]; aud_seq[2] = seq[1]; aud_seq[3] = n > 2 ? seq[2] : seq[1];
    aud_ms[PART_LEAD] = ms;
    live_set();
}
static void aud_tick(void)
{
    for (int p = 0; p < PART_N; p++) if (aud_ms[p] > 0) {
        aud_ms[p] -= 10;
        if (p == PART_LEAD && aud_seq_n > 1 && aud_seq_ms > 0) {     // a chord: its notes one after the other
            int i = aud_seq_i; aud_seq_i += 10;
            int a = i / aud_seq_ms, b = aud_seq_i / aud_seq_ms;
            if (b != a && b < aud_seq_n && b < 3) aud_seq[1] = aud_seq[1 + b];
        }
        if (aud_ms[p] <= 0) { aud_ms[p] = 0; if (p == PART_LEAD) aud_seq_n = 0; }
    }
    live_set();
}

static float lean_amount(float deg, float dead, float full)
{
    float a = fabsf(deg);
    return a < dead ? 0.0f : clampf((a - dead) / (full - dead), 0.0f, 1.0f);
}
static void hands(const ctl_t *c)
{
    bool tilt = pv[P_TILT] >= 1.0f && c->has_imu && !edit_active();
    float gl = tilt && c->lean_y > 0.0f ? lean_amount(c->lean_y, 14.0f, 50.0f) : 0.0f;
    float bd = tilt ? (c->lean_x < 0.0f ? -1.0f : 1.0f) * lean_amount(c->lean_x, 12.0f, 50.0f) * pv[P_BEND] : 0.0f;
    hand_glitch += 0.25f * (gl - hand_glitch); if (hand_glitch < 0.003f && gl == 0.0f) hand_glitch = 0.0f;
    hand_bend += 0.35f * (bd - hand_bend); if (fabsf(hand_bend) < 0.003f && bd == 0.0f) hand_bend = 0.0f;
    if (gl > 0.3f || fabsf(bd) > 0.5f) coach_done(LEARN_TILT);
    // the stick: a push toggles a part's mute (it has to come back to the middle before the next)
    if (c->has_joy && pv[P_STICK] >= 1.0f && !edit_active()) {
        float ax = fabsf(c->jx), ay = fabsf(c->jy);
        int dir = ax < 0.35f && ay < 0.35f ? 0 : ax > 0.65f && ax > ay ? (c->jx > 0 ? 2 : 4) : ay > 0.65f && ay > ax ? (c->jy > 0 ? 1 : 3) : stick_dir;
        if (dir != stick_dir) {
            stick_dir = dir;
            static const uint8_t part_of[5] = { 0, PART_LEAD, PART_CHORDS, PART_BASS, PART_DRUMS };
            static const char *const nm[PART_N] = { "DRUMS", "BASS", "CHORDS", "LEAD" };
            if (dir) {
                int p = part_of[dir];
                px_mute ^= (uint8_t)(1u << p);
                char b[24]; snprintf(b, sizeof b, "%s %s", nm[p], (px_mute >> p) & 1 ? "OFF" : "ON");
                toast(b, 700);
                coach_done(LEARN_JOY);
            }
        }
    } else stick_dir = 0;
}

// REC: a tap starts the song, or stops it. A REC that is down when PIXEL starts counts for nothing until it has been
// up 0.1 s (a board here has had its REC pin stuck low). In the editor REC is the editor's.
static void rec_logic(const ctl_t *c)
{
    if (rec_ignore) {
        rec_up_ms = c->rec ? 0 : rec_up_ms + 10;
        if (rec_up_ms >= 100) rec_ignore = false;
        rec_was = false; return;
    }
    if (edit_active()) { rec_was = c->rec; return; }
    if (c->rec && !rec_was) rec_ms = 0;
    if (c->rec) rec_ms += 10;
    if (!c->rec && rec_was && rec_ms < 600) {
        if (px_pos.running || px_pos.armed) { px_stop(); toast("STOP", 500); }
        else { px_play_song(0); toast(peers_was > 0 ? "ON THE NEXT BAR" : "PLAY", 500); }
        coach_done(LEARN_INST + 0);
    }
    rec_was = c->rec;
}

static const uint8_t con_rgb[CON_N][3] = { { 255, 255, 255 }, { 255, 120, 0 }, { 0, 200, 255 }, { 255, 30, 30 },
                                           { 120, 255, 40 }, { 150, 110, 255 }, { 255, 60, 200 }, { 40, 90, 255 } };
static void tick(const ctl_t *c)
{
    if ((c->ticks % 1000 == 0 || (c->ticks < 3000 && c->ticks % 100 == 0)) && prof_blocks) {
        float r, t; px_prof(&r, &t);
        app_log("cpu us/block: %s render %.0f tick %.0f (of 2000)", px_console()->name, (double)r, (double)t);
    }
    if (meta.quick[0] && memcmp(meta.quick, "cons", 4)) memset(meta.quick, 0, sizeof meta.quick);   // the knob stays the console
    if (!link_up) { link_start(); grid_set(); }
    link_tick(c->dt);
    link_follow();
    px_pos.bpm = song.bpm;
    hands(c);
    rec_logic(c);
    aud_tick();
    edit_tick(c);
    lib_tick(c);
    // the chord that sounds, for the flock: boards that follow chords follow the song's
    int live = px_pos.running && px_pos.degree ? px_pos.degree - 1 : -1;
    if (live != chord_sent && link_up) { chord_sent = live; harm_set_here(live); }
    // the beat, for the header and the LED
    uint32_t fpb = song.fpb ? song.fpb : 12800;
    uint32_t b = (uint32_t)imod((int32_t)(c->frames - song.origin), (int32_t)(fpb * WRAP_BEATS)) / fpb;
    if (b != last_beat) { last_beat = b; beat_flash = (b % 4 == 0) ? 3 : 2; }
    else if (beat_flash > 0 && c->ticks % 4 == 0) beat_flash--;
    const uint8_t *k = con_rgb[px_pos.console < CON_N ? px_pos.console : 0];
    float lv = px_pos.running ? clampf(out_env_v * 2.0f, 0.0f, 1.0f) * 0.5f + (beat_flash ? 0.5f : 0.1f) : 0.04f;
    led_rgb((uint8_t)((float)k[0] * lv), (uint8_t)((float)k[1] * lv), (uint8_t)((float)k[2] * lv));
}
int  px_beat_flash(void) { return beat_flash; }
float px_out_env(void) { return out_env_v; }

static bool applying;
static void changed(int p)
{
    if (applying || p == P_NREV) return;
    if (p < 0) {                                                   // a song loaded, or everything reset
        px_mute = 0;
        eff_key = (int)pv[P_KEY]; eff_scale = (int)pv[P_SCALE];
        lib_loaded();
        grid_set();
        if (link_up) {
            tempo_set_here(false);
            harm_set_here(-1); chord_sent = -1;
            link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
        }
        return;
    }
    switch (p) {
    case P_TEMPO: if (link_up) tempo_set_here(true); break;
    case P_SWING: case P_FRAME: grid_set(); break;
    case P_KEY: case P_SCALE:
        eff_key = (int)pv[P_KEY]; eff_scale = (int)pv[P_SCALE];
        if (link_up) harm_set_here(chord_sent);
        break;
    case P_LINK: case P_GROUP: case P_RANGE:
        if (!link_up) break;
        link_enable(false, 1, 1);
        link_enable(pv[P_LINK] >= 1.0f, (int)pv[P_GROUP], (int)pv[P_RANGE]);
        peers_was = 0;
        break;
    }
}

static void action(int p)
{
    if (p == P_ECOPY) edit_action(EA_COPY);
    else if (p == P_EPASTE) edit_action(EA_PASTE);
    else if (p == P_ECLEAR) edit_action(EA_CLEAR);
    else if (p == P_PEERS) {
        link_peer_t pr[LINK_PEERS_MAX];
        int np = link_peers(pr, LINK_PEERS_MAX);
        char b[48];
        if (!link_radio()) snprintf(b, sizeof b, "LINK IS OFF");
        else if (!np) snprintf(b, sizeof b, "NO BOARD IN GROUP %d YET", (int)pv[P_GROUP]);
        else {
            int k = snprintf(b, sizeof b, "%s", pr[0].name);
            for (int i = 1; i < np && k < (int)sizeof b - 10; i++) k += snprintf(b + k, sizeof b - (size_t)k, " + %s", pr[i].name);
        }
        toast(b, 2500);
    }
}

// notes from a keyboard on USB play the lead (the newest key; letting it go ends it)
static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint8_t k = st & 0xF0;
    if (k == 0x90 && d2 > 0) midi_note_on = (int8_t)(d1 > 119 ? 119 : d1 < 12 ? 12 : d1);
    else if ((k == 0x80 || k == 0x90) && d1 == (uint8_t)midi_note_on) midi_note_on = -1;
    live_set();
}

static bool hold(void) { if (edit_hold()) { coach_done(LEARN_INST + 1); return true; } return false; }

// LEARN_INST + 0: started or stopped with REC; + 1: opened the editor. The editor's own start at + 8.
static const hint_t hints[] = {
    { "TAP REC TO PLAY THE SONG, AGAIN TO STOP", LEARN_INST + 0 },
    { "TURN THE KNOB: EIGHT CHIPS PLAY IT", LEARN_TURN },
    { "HOLD THE KNOB TO WRITE NOTES", LEARN_INST + 1 },
    { "PUSH THE STICK TO MUTE A PART", LEARN_JOY },
    { "TIP IT FORWARD: THE CARTRIDGE GLITCHES", LEARN_TILT },
    { "PRESS THE KNOB: THE MENU, THE SONGS", LEARN_MENU },
};

static void draw(const view_t *v) { if (edit_active()) edit_draw(v); else scene_draw(v); }
static bool splash(int f) { return scene_splash(f); }
static void saver(uint32_t f) { scene_saver(f); }

const instrument_t INST = {
    .name = "PIXEL", .tagline = "A CHIPTUNE MACHINE",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = G_N,
    .presets = PX_PRESETS, .npresets = PX_NSONGS,
    .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .burst = false, .looper = false, .knob = "cons", .midi_notes = true,
    .init = init, .audio = audio, .tick = tick, .draw = draw, .splash = splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = saver, .hold = hold,
};
