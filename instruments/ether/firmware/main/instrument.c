// ETHER: the Wi-Fi and the Bluetooth around you, at the same time, as one piece of music. On the ground,
// the Wi-Fi (SNIFF's): every frame a device sends strikes its own note, the routers' beacons ring, the room's
// radio shape (CSI) swells when people move, interference is a storm. In the air, the Bluetooth (CHIRP's):
// every phone, watch, earbud and tracker that advertises is a bird on one of three wires, singing in
// phrases (CHORUS), pulsing at its own advertising interval (CLOCKS) or as a flutter (SWARM). One key and
// scale, one density budget, one reverb; BALANCE (the joystick's left and right) leans the music towards
// one world or the other. HUNT follows one device from either radio like a Geiger counter.
//
// This file: the param table, the sounds, the hints, both radios' events -> the audio engine
// (inst_sound.c) and the screen (inst_scene.c).
//
// Privacy: from either radio only a salted hash of the sender (a new salt every boot), the signal strength
// and the kind of frame or advert. No address, name, network or content is kept.
#include "inst.h"
#include "params.h"
#include "ui.h"
#include "spsc.h"
#include <stdio.h>
#include <string.h>

ether_t E;

// ---------------------------------------------------------------- the table
enum { G_MIX, G_TONE, G_BIRDS, G_RHYTHM, G_SPACE, G_CONTROL };
static const char *const groups[] = { "MIX", "TONE", "BIRDS", "RHYTHM", "SPACE", "CONTROL" };
static const char *const mode_l[] = { "LISTEN", "HUNT" };
static const char *const scale_l[] = { "MAJ PENTA", "MIN PENTA", "MAJOR", "MINOR", "DORIAN", "LYDIAN", "HIRAJOSHI", "PELOG", "WHOLE TONE", "BLUES", "CHROMATIC" };
static const char *const key_l[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const reg_l[] = { "LOW", "MID", "HIGH", "AIR" };
static const char *const wvoice_l[] = { "AUTO", "PLUCK", "BELL", "KALIMBA", "DROP", "BEEP", "STATIC" };
static const char *const bvoice_l[] = { "BY KIND", "CHIRP", "WHISTLE", "TRILL", "WARBLE", "PIP", "COO", "BELL", "DROP", "MIXED" };
static const char *const flock_l[] = { "CHORUS", "CLOCKS", "SWARM" };
static const char *const grid_l[] = { "FREE", "1/4", "1/8", "1/16", "1/32", "1/8 TRIP", "1/16 TRIP" };
static const char *const etime_l[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };
static const char *const chan_l[] = { "AUTO", "HOP", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13" };
static const char *const share_l[] = { "25%", "50%", "75%" };
static const char *const onoff_l[] = { "OFF", "ON" };
static const char *const tilt_l[] = { "OFF", "RING", "PITCH", "REACH" };
static const char *const lean_l[] = { "OFF", "SCALE WALK", "BRIGHT", "ECHO" };
static const char *const stx_l[] = { "OFF", "BALANCE", "BRIGHT", "SCALE WALK" };
static const char *const sty_l[] = { "OFF", "REACH", "DENSITY", "ROOM" };

static const param_t params[P_N] = {
    [P_MODE]    = { "MODE", "mode", G_MIX, PK_ENUM, 0, 0, 1, 1, 0, "", mode_l, "HUNT: FIND A DEVICE BY EAR" },
    [P_BAL]     = { "BALANCE", "bal ", G_MIX, PK_NUM, PF_INT | PF_SIGNED, -100, 100, 5, 0, "%", NULL, "- WI-FI    + BLUETOOTH" },
    [P_PKT]     = { "PACKETS", "pkts", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 70, "%", NULL, "WI-FI: EVERY FRAME A NOTE" },
    [P_ROOM]    = { "ROOM", "room", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "WI-FI: PEOPLE MOVING" },
    [P_SENS]    = { "ROOM SENS", "sens", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "HOW SMALL A MOVE IT HEARS" },
    [P_STORM]   = { "STORM", "strm", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 30, "%", NULL, "WI-FI: INTERFERENCE" },
    [P_BIRDS]   = { "BIRDS", "brds", G_MIX, PK_NUM, PF_INT, 0, 100, 5, 80, "%", NULL, "BLUETOOTH: A BIRD EACH" },
    [P_DENS]    = { "DENSITY", "dens", G_MIX, PK_NUM, PF_LOG, 4, 400, 1.12f, 40, "/s", NULL, "NOTES A SECOND, IN ALL" },
    [P_REACH]   = { "REACH", "rech", G_MIX, PK_NUM, PF_INT, -100, -40, 2, -92, "dBm", NULL, "FAINTER ONES ARE LEFT OUT" },
    [P_NEXT]    = { "NEXT TARGET", "next", G_MIX, PK_ACT, PF_NOCC, 0, 0, 0, 0, "", NULL, "HUNT THE NEXT DEVICE" },
    [P_SCALE]   = { "SCALE", "scal", G_TONE, PK_ENUM, 0, 0, 10, 1, 0, "", scale_l, "ONE SCALE FOR BOTH WORLDS" },
    [P_KEY]     = { "KEY", "key ", G_TONE, PK_ENUM, 0, 0, 11, 1, 2, "", key_l, "MIDI NOTES IN SET IT TOO" },
    [P_REG]     = { "REGISTER", "reg ", G_TONE, PK_ENUM, 0, 0, 3, 1, 1, "", reg_l, "BIRDS SING AN OCTAVE ABOVE" },
    [P_SPREAD]  = { "SPREAD", "sprd", G_TONE, PK_NUM, PF_INT, 1, 4, 1, 2, "oct", NULL, "OCTAVES THE DEVICES SPAN" },
    [P_TRANS]   = { "TRANSPOSE", "trns", G_TONE, PK_NUM, PF_INT | PF_SIGNED, -12, 12, 1, 0, "st", NULL, "EVERYTHING UP OR DOWN" },
    [P_WVOICE]  = { "WI-FI VOICE", "wvoc", G_TONE, PK_ENUM, 0, 0, WV_N - 1, 1, WV_AUTO, "", wvoice_l, "AUTO: A SOUND PER KIND" },
    [P_BVOICE]  = { "BIRD VOICE", "bvoc", G_TONE, PK_ENUM, 0, 0, 9, 1, 0, "", bvoice_l, "BY KIND: A CALL PER KIND" },
    [P_RING]    = { "RING", "ring", G_TONE, PK_NUM, PF_LOG, 0.03f, 8, 1.15f, 0.7f, "s", NULL, "HOW LONG WI-FI NOTES RING" },
    [P_BRIGHT]  = { "BRIGHT", "brit", G_TONE, PK_NUM, PF_INT, 0, 100, 5, 50, "%", NULL, "DARK TO SHARP" },
    [P_FLOCK]   = { "FLOCK", "flok", G_BIRDS, PK_ENUM, 0, 0, 2, 1, FL_CHORUS, "", flock_l, "HOW THE BIRDS SING" },
    [P_CLEN]    = { "CALL LENGTH", "clen", G_BIRDS, PK_NUM, PF_LOG, 0.25f, 4.0f, 1.12f, 1.0f, "x", NULL, "LONGER OR SHORTER CALLS" },
    [P_REST]    = { "REST", "rest", G_BIRDS, PK_NUM, PF_LOG, 0.05f, 5.0f, 1.12f, 0.4f, "s", NULL, "PAUSE BETWEEN PHRASES" },
    [P_PHRASE]  = { "PHRASE", "phrs", G_BIRDS, PK_NUM, PF_INT, 1, 8, 1, 3, "", NULL, "MOST CALLS IN A ROW" },
    [P_SLOW]    = { "SLOW DOWN", "slow", G_BIRDS, PK_NUM, PF_LOG, 1, 32, 1.12f, 4, "x", NULL, "CLOCKS: SLOWS EACH PULSE" },
    [P_MAXB]    = { "MAX BIRDS", "maxb", G_BIRDS, PK_NUM, PF_INT, 4, MAXB, 1, 16, "", NULL, "ONLY THE NEAREST SING" },
    [P_GRID]    = { "GRID", "grid", G_RHYTHM, PK_ENUM, 0, 0, 6, 1, 0, "", grid_l, "FREE, OR SNAPPED TO A BEAT" },
    [P_TEMPO]   = { "TEMPO", "tmpo", G_RHYTHM, PK_NUM, PF_INT, 40, 240, 1, 100, "BPM", NULL, "FOR THE GRID AND THE ECHO" },
    [P_SWING]   = { "SWING", "swng", G_RHYTHM, PK_NUM, PF_INT, 0, 75, 5, 0, "%", NULL, "EVERY OTHER STEP A BIT LATE" },
    [P_PERSTEP] = { "PER STEP", "pstp", G_RHYTHM, PK_NUM, PF_INT, 1, 8, 1, 2, "", NULL, "MOST NOTES ON ONE STEP" },
    [P_VERB]    = { "REVERB", "verb", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 40, "%", NULL, "HOW MUCH ROOM AROUND IT" },
    [P_SIZE]    = { "SIZE", "size", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 55, "%", NULL, "SMALL ROOM TO HUGE HALL" },
    [P_DIST]    = { "DISTANCE", "dist", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 60, "%", NULL, "FAR DEVICES SOUND FARTHER" },
    [P_ECHO]    = { "ECHO", "echo", G_SPACE, PK_NUM, PF_INT, 0, 100, 5, 12, "%", NULL, "REPEATS IN TIME WITH IT" },
    [P_ETIME]   = { "ECHO TIME", "etim", G_SPACE, PK_ENUM, 0, 0, 5, 1, 2, "", etime_l, "IN BEATS OF THE TEMPO" },
    [P_EFB]     = { "ECHO REPEAT", "efb ", G_SPACE, PK_NUM, PF_INT, 0, 90, 5, 40, "%", NULL, "HOW MANY TIMES IT RETURNS" },
    [P_CHAN]    = { "WI-FI CHAN", "chan", G_CONTROL, PK_ENUM, PF_SYS, 0, 14, 1, CH_AUTO, "", chan_l, "AUTO: THE BUSIEST CHANNEL" },
    [P_DWELL]   = { "HOP TIME", "dwel", G_CONTROL, PK_NUM, PF_LOG | PF_SYS, 0.1f, 3, 1.15f, 0.4f, "s", NULL, "HOW LONG ON EACH CHANNEL" },
    [P_BTTIME]  = { "BT TIME", "btt ", G_CONTROL, PK_ENUM, PF_SYS, 0, 2, 1, 1, "", share_l, "AIR TIME BLUETOOTH GETS" },
    [P_BEACON]  = { "BEACONS", "bcon", G_CONTROL, PK_ENUM, 0, 0, 1, 1, 1, "", onoff_l, "ROUTERS SAYING: I AM HERE" },
    [P_TILT]    = { "TILT", "tilt", G_CONTROL, PK_ENUM, 0, 0, 3, 1, 1, "", tilt_l, "BOTTOM EDGE DOWN DOES" },
    [P_LEAN]    = { "LEAN", "lean", G_CONTROL, PK_ENUM, 0, 0, 3, 1, 1, "", lean_l, "LEFT AND RIGHT DO THIS" },
    [P_STX]     = { "STICK X", "stkx", G_CONTROL, PK_ENUM, 0, 0, 3, 1, 1, "", stx_l, "JOYSTICK LEFT AND RIGHT" },
    [P_STY]     = { "STICK Y", "stky", G_CONTROL, PK_ENUM, 0, 0, 3, 1, 1, "", sty_l, "JOYSTICK UP AND DOWN" },
};

static const preset_t presets[] = {
    { "BOTH WORLDS",  "" },
    { "MUSIC BOX",    "wvoc=3 bvoc=7 scal=0 key=0 reg=2 grid=3 tmpo=96 ring=0.9 flok=1 slow=4 verb=40 echo=20 etim=1 room=25" },
    { "DAWN",         "bal=40 scal=5 key=7 reg=2 bvoc=0 brds=90 pkts=45 wvoc=4 ring=0.15 rest=0.5 verb=50 size=70 room=30" },
    { "RAIN & BIRDS", "wvoc=4 reg=3 sprd=2 ring=0.12 dens=120 strm=45 brds=70 flok=0 verb=45 room=20" },
    { "CLOCK SHOP",   "flok=1 slow=4 bvoc=5 clen=0.5 wvoc=1 ring=0.2 grid=3 tmpo=110 verb=20 size=30 pkts=40" },
    { "GAMELAN",      "flok=1 grid=2 tmpo=72 slow=3 scal=7 key=0 reg=0 wvoc=2 bvoc=7 ring=3 clen=1.6 verb=50 size=75 dens=24" },
    { "GHOSTS",       "pkts=0 brds=35 room=100 sens=70 strm=10 verb=65 size=85 scal=1 key=9 reg=0 bvoc=2 rest=1.5" },
    { "STARLINGS",    "flok=2 brds=90 pkts=40 wvoc=4 dens=90 verb=40 reg=3 sprd=3" },
    { "NIGHT",        "bal=-30 scal=1 key=9 reg=0 bvoc=6 clen=2 rest=1.6 dens=10 wvoc=2 ring=4 verb=70 size=90 brit=15 room=60" },
    { "STORM",        "pkts=25 brds=40 strm=100 wvoc=6 bvoc=3 verb=30" },
    { "HUNT",         "mode=1 verb=15 echo=0" },
    { "ONLY WI-FI",   "bal=-100 brds=0 pkts=85 room=50" },
    { "ONLY BIRDS",   "bal=100 pkts=0 room=0 strm=0 brds=90 verb=45" },
    { "GLITCH",       "wvoc=6 bvoc=1 scal=10 sprd=4 reg=2 ring=0.04 clen=0.3 grid=4 tmpo=140 pstp=3 verb=0 echo=40 etim=0 efb=60 brit=90" },
};
#define NPRESETS ((int)(sizeof presets / sizeof presets[0]))

enum { LEARN_HUNT = LEARN_INST, LEARN_GHOST, LEARN_LISTEN };
static const hint_t hints[] = {
    { "BIRDS ARE BLUETOOTH, THE GROUND IS WI-FI", LEARN_LISTEN },
    { "TURN THE KNOB: NEW SOUNDS", LEARN_TURN },
    { "STICK LEFT: WI-FI  RIGHT: BLUETOOTH", LEARN_JOY },
    { "WALK PAST THE ROUTER AND LISTEN", LEARN_GHOST },
    { "TILT ME: THE NOTES RING LONGER", LEARN_TILT },
    { "FIND A DEVICE: TRY THE SOUND HUNT", LEARN_HUNT },
    { "HOLD REC TO LOOP THE AIR", LEARN_REC },
    { "PRESS THE KNOB FOR THE MENU", LEARN_MENU },
};

// ---------------------------------------------------------------- scales and notes
typedef struct { int n; int8_t st[12]; } scale_t;
static const scale_t scales[] = {
    { 5, { 0, 2, 4, 7, 9 } }, { 5, { 0, 3, 5, 7, 10 } }, { 7, { 0, 2, 4, 5, 7, 9, 11 } }, { 7, { 0, 2, 3, 5, 7, 8, 10 } },
    { 7, { 0, 2, 3, 5, 7, 9, 10 } }, { 7, { 0, 2, 4, 6, 7, 9, 11 } }, { 5, { 0, 2, 3, 7, 8 } }, { 5, { 0, 1, 3, 7, 8 } },
    { 6, { 0, 2, 4, 6, 8, 10 } }, { 6, { 0, 3, 5, 6, 7, 10 } }, { 12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
};
static const int reg_base[4] = { 36, 48, 60, 72 };
static int sn(void) { return scales[(int)pv[P_SCALE]].n; }
static float degree_note(int deg)               // any degree, in the scale, key and register
{
    const scale_t *s = &scales[(int)pv[P_SCALE]];
    int oct = deg >= 0 ? deg / s->n : -((-deg + s->n - 1) / s->n), step = deg - oct * s->n;
    return (float)(reg_base[(int)pv[P_REG]] + (int)pv[P_KEY] + (int)pv[P_TRANS] + 12 * oct + s->st[step]);
}

// ---------------------------------------------------------------- shared state
uint32_t ether_dbg_at[64], ether_dbg_n;          // the last notes' start frames, both worlds (tools/sim checks the budget and grid)
uint32_t ether_dbg_wifi, ether_dbg_bt;           // notes so far from each world (tools/sim)
static uint32_t ticks;
static float now_s;
static uint32_t frames_now;
static float tok_w, tok_b, w_wifi = 1, w_bt = 1;
static float walk_deg, ring_mul = 1.0f, pitch_bend, reach_add, bright_add, echo_add, dens_mul = 1.0f, room_add, bal_stick;
static uint32_t step_idx[8]; static int step_cnt[8];
static float led_r, led_g, led_b;
static uint32_t rs = 0x2545f491u;
static float frand(void) { return (float)(rng_next(&rs) >> 8) * (1.0f / 16777216.0f); }
typedef struct { float t; uint8_t ch, note; } noff_t;
static noff_t noff[32]; static int nnoff;

static void midi_out(int ch_off, int note, float near, float len_s)
{
    if (note < 0 || note > 127) return;
    int ch = (((int)pv[CP(CP_MIDI_CH)] - 1 + ch_off) & 15) + 1;
    if (nnoff >= 32) { midi_note_ch(noff[0].ch, noff[0].note, 0); memmove(noff, noff + 1, sizeof noff[0] * 31); nnoff--; }
    midi_note_ch(ch, note, 25 + (int)(near * 102.0f));
    noff[nnoff++] = (noff_t){ now_s + fmaxf(len_s, 0.04f), (uint8_t)ch, (uint8_t)note };
}

// The grid, for both worlds: the next step at or after `at`; 0 if that step already has PER STEP notes.
static float grid_unit(void)
{
    static const float div[7] = { 0, 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f };
    int g = (int)pv[P_GRID];
    return g ? 60.0f / pv[P_TEMPO] * div[g] * (float)AUDIO_SR : 0.0f;
}
static uint32_t grid_at(uint32_t at)
{
    float step = grid_unit();
    if (step <= 0) return at;
    uint32_t k = (uint32_t)((float)at / step) + 1u;
    uint32_t t = (uint32_t)((float)k * step);
    if (k & 1u) t += (uint32_t)(pv[P_SWING] * 0.01f * 0.5f * step);
    int slot = (int)(k & 7u);
    if (step_idx[slot] != k) { step_idx[slot] = k; step_cnt[slot] = 0; }
    if (step_cnt[slot] >= (int)pv[P_PERSTEP]) return 0;
    step_cnt[slot]++;
    return t ? t : 1;
}
static void noted(uint32_t at, int world)
{
    ether_dbg_at[ether_dbg_n++ & 63] = at;
    if (world == W_WIFI) ether_dbg_wifi++; else ether_dbg_bt++;
}

// ================================================================ Wi-Fi (SNIFF's)
static int  cur_chan = 1, dwell_ticks, scan_i, quiet_ticks, scan_age;
static uint32_t chan_hits;
static uint32_t room_src; static int csi_ticks_since;
static float base_shape[CSI_BANDS], dev_shape[CSI_BANDS], m_floor = 0.02f, m_raw;
static bool base_ok;
static float bad_fast, bad_slow, nf_s, nf_base; static int bad_tick, thunder_cool; static bool nf_ok;
static float overflow_n, overflow_rate;

static int dev_find(uint32_t who, bool add)
{
    int free_i = -1, old_i = 0;
    for (int i = 0; i < NDEV; i++) {
        if (E.dev[i].who == who) return i;
        if (!E.dev[i].who) { if (free_i < 0) free_i = i; }
        else if (E.dev[i].last < E.dev[old_i].last || !E.dev[old_i].who) old_i = i;
    }
    if (!add) return -1;
    int i = free_i >= 0 ? free_i : old_i;
    if (E.tw == W_WIFI && i == E.target) E.target = -1;
    E.dev[i] = (wdev_t){ .who = who, .rssi = -90, .last = ticks };
    return i;
}

static void set_chan(int c) { if (c < 1) c = 1; if (c > 13) c = 13; if (c != cur_chan) { cur_chan = c; radio_set_channel(c); } E.chan = c; }

static void channels_tick(void)
{
    int mode = (int)pv[P_CHAN];
    if (E.mode == MODE_HUNT && E.tw == W_WIFI && E.target >= 0 && E.dev[E.target].chan) { E.scanning = false; set_chan(E.dev[E.target].chan); chan_hits = 0; return; }
    if (mode >= 2) { E.scanning = false; set_chan(mode - 1); E.hist[cur_chan] += 0.01f * ((float)chan_hits * 100.0f - E.hist[cur_chan]); chan_hits = 0; return; }
    int dwell = mode == CH_HOP ? (int)(pv[P_DWELL] * 100.0f) : 25;
    if (mode == CH_AUTO && !E.scanning) {
        quiet_ticks = E.hist[cur_chan] < 2.0f ? quiet_ticks + 1 : 0;
        if (++scan_age > 30000 || quiet_ticks > 1500) { E.scanning = true; scan_i = 0; scan_age = 0; quiet_ticks = 0; dwell_ticks = 0; set_chan(1); chan_hits = 0; }
        else { E.hist[cur_chan] += 0.01f * ((float)chan_hits * 100.0f - E.hist[cur_chan]); chan_hits = 0; return; }
    }
    if (mode == CH_HOP) E.scanning = false;
    if (++dwell_ticks < dwell) return;
    float r = (float)chan_hits * 100.0f / (float)dwell_ticks;
    E.hist[cur_chan] = E.hist[cur_chan] > 0 ? 0.5f * (E.hist[cur_chan] + r) : r;
    chan_hits = 0; dwell_ticks = 0;
    if (mode == CH_AUTO) {
        if (++scan_i >= 13) {
            int best = 1;
            for (int c = 2; c <= 13; c++) if (E.hist[c] > E.hist[best]) best = c;
            E.scanning = false; scan_age = 0;
            set_chan(best);
            app_log("busiest Wi-Fi channel: %d (%.0f frames/s)", best, (double)E.hist[best]);
            return;
        }
        set_chan(scan_i + 1);
    } else set_chan(cur_chan % 13 + 1);
}

static void wifi_frame(const radio_pkt_t *p, int di, float prox)
{
    wdev_t *d = &E.dev[di];
    int voice = (int)pv[P_WVOICE];
    bool beacon = p->kind == PKT_MGMT && (p->sub == 8 || p->sub == 5), probe = p->kind == PKT_MGMT && p->sub == 4;
    strike_t s = { .bright = clampf(pv[P_BRIGHT] * 0.01f + bright_add, 0, 1) };
    float note = degree_note((int)(d->who % (uint32_t)(sn() * (int)pv[P_SPREAD])) + (int)lroundf(walk_deg)) + pitch_bend;
    float len_k = 1.0f + 0.5f * ((float)p->len / 1500.0f * 2.0f - 0.5f);
    s.ring = pv[P_RING] * ring_mul * clampf(len_k, 0.3f, 2.5f);
    s.amp = 0.03f + 0.13f * prox * prox + 0.04f * prox;
    s.wet = clampf(0.06f + pv[P_DIST] * 0.01f * (1.0f - prox) * 0.9f, 0, 0.95f);
    if (E.mode == MODE_HUNT) { s.timbre = T_DROP; s.ring = 0.08f; s.amp *= 0.6f; note += 12.0f; }
    else if (voice == WV_AUTO) {
        if (beacon) { s.timbre = T_BELL; note += 12.0f; s.amp *= 0.45f; s.ring *= 1.4f; }
        else if (probe) { s.timbre = T_DROP; note += 12.0f; s.ring = fminf(s.ring, 0.2f); }
        else if (p->kind == PKT_MGMT) s.timbre = T_KALIMBA;
        else s.timbre = T_PLUCK;
    } else {
        static const uint8_t tm[WV_N] = { T_PLUCK, T_PLUCK, T_BELL, T_KALIMBA, T_DROP, T_BEEP, T_STATIC };
        s.timbre = tm[voice];
        if (beacon) { note += 12.0f; s.amp *= 0.5f; }
        if (s.timbre == T_BEEP) s.ring = 0.03f + pv[P_RING] * ring_mul * clampf(len_k, 0.2f, 3.0f) * 0.25f;
    }
    s.f = midi_hz(clampf(note, 12.0f, 115.0f));
    if (E.mode != MODE_HUNT) {
        if (frand() > w_wifi) return;                                // BALANCE leans the other way
        if (tok_w < 1.0f) { overflow_n += 1.0f; return; }           // over the budget: a raindrop
    }
    uint32_t at = grid_at(p->frame + 640u);                         // 20 ms behind, keeping the frames' spacing
    if (!at) { overflow_n += 1.0f; return; }
    s.at = at;
    if (!snd_strike(&s)) return;
    if (E.mode != MODE_HUNT) tok_w -= 1.0f;
    noted(at, W_WIFI);
    E.sniff = 1.0f; d->pulse = 1.0f;
    scene_heard(di);
    midi_out(0, (int)lroundf(note), prox, 0.08f);
    if (beacon) { led_b += 60; led_g += 25; } else if (probe) { led_r += 70; led_g += 55; } else { led_g += 50; led_r += 10; }
}

static void room_frame(const radio_csi_t *c)
{
    float tot = 0;
    for (int b = 0; b < CSI_BANDS; b++) tot += (float)c->amp[b];
    if (tot < 13.0f) return;
    csi_ticks_since = 0;
    float sh[CSI_BANDS], dsum = 0;
    for (int b = 0; b < CSI_BANDS; b++) sh[b] = (float)c->amp[b] * (float)CSI_BANDS / tot;
    if (!base_ok) { memcpy(base_shape, sh, sizeof sh); base_ok = true; }
    for (int b = 0; b < CSI_BANDS; b++) {
        float dv = fabsf(sh[b] - base_shape[b]);
        dev_shape[b] += 0.3f * (dv - dev_shape[b]);
        dsum += dv;
        base_shape[b] += 0.01f * (sh[b] - base_shape[b]);
        E.shape[b] += 0.25f * (sh[b] - E.shape[b]);
    }
    float m = dsum / (float)CSI_BANDS;
    m_raw += (m > m_raw ? 0.3f : 0.03f) * (m - m_raw);
    if (m_raw < m_floor) m_floor += 0.05f * (m_raw - m_floor); else m_floor += 0.0005f * (m_raw - m_floor);
}

static void room_tick(void)
{
    if (csi_ticks_since < 100000) csi_ticks_since++;
    E.room_live = csi_ticks_since < 200;
    float k = 4.0f + 36.0f * pv[P_SENS] * 0.01f;
    float target = E.room_live ? clampf((m_raw - m_floor * 1.3f) * k, 0, 1) : 0.0f;
    E.motion += (target > E.motion ? 0.2f : 0.02f) * (target - E.motion);
    if (!E.room_live) for (int b = 0; b < CSI_BANDS; b++) { E.shape[b] += 0.01f * (1.0f - E.shape[b]); dev_shape[b] *= 0.98f; }
    if (E.motion > 0.5f) coach_done(LEARN_GHOST);
    if (ticks % 100 == 0) {                                     // follow the sender with the most CSI on this channel
        int best = -1; float bs = 0;
        for (int i = 0; i < NDEV; i++) {
            wdev_t *d = &E.dev[i];
            if (!d->who || d->chan != cur_chan) { d->csi = 0; continue; }
            float s_ = (float)d->csi * (1.0f + (d->rssi + 100.0f) / 40.0f);
            if (d->csi >= 3 && s_ > bs) { bs = s_; best = i; }
            d->csi = 0;
        }
        uint32_t w = best >= 0 ? E.dev[best].who : 0;
        if (w != room_src && (best >= 0 || csi_ticks_since > 300)) { room_src = w; radio_follow(w); base_ok = false; }
    }
    int n = sn();
    static const int8_t deg5[PAD_N] = { 0, 2, 4, 5, 7, 9, 10, 12 };
    for (int k2 = 0; k2 < PAD_N; k2++) {
        int deg = n >= 7 ? (k2 / 3) * n + (k2 % 3) * 2 : deg5[k2];
        if (n == 12) deg = (k2 / 3) * 12 + (k2 % 3) * 4;
        sc.pad_f[k2] = midi_hz(degree_note(deg) - 12.0f);
        int b = k2 * (CSI_BANDS - 1) / (PAD_N - 1);
        sc.pad_g[k2] = clampf(0.3f + 0.5f * E.shape[b] + 10.0f * dev_shape[b], 0, 2.0f) * (k2 < 3 ? 1.0f : k2 < 6 ? 0.7f : 0.45f);
    }
    sc.motion = E.motion;
}

static void storm_tick(void)
{
    float br = (float)bad_tick * 100.0f;
    bad_tick = 0;
    bad_fast += 0.05f * (br - bad_fast);
    bad_slow += 0.001f * (br - bad_slow);
    float elev = nf_s - nf_base; if (elev < 0) elev = 0;
    E.storm = clampf(log10f(1.0f + bad_fast / 5.0f) * 0.5f + elev / 12.0f, 0, 1);
    sc.crackle = fminf(bad_fast * 1.5f + elev * 4.0f, 1500.0f);
    sc.rumble = clampf(elev / 10.0f + E.storm * 0.3f, 0, 1);
    if (thunder_cool > 0) thunder_cool--;
    else if (bad_fast > 3.0f * bad_slow + 25.0f && sc.storm > 0.01f) { sc.thunder++; E.thunder = 1.0f; thunder_cool = 250; led_r += 255; led_g += 200; led_b += 255; }
}

static void wifi_tick(float reach)
{
    radio_pkt_t p;
    int k = 0, acks = 0;
    while (k < 400 && radio_pkt(&p)) {
        k++;
        chan_hits++;
        if (p.noise && !nf_ok) { nf_s = nf_base = (float)p.noise; nf_ok = true; }
        if (p.noise) { nf_s += 0.02f * ((float)p.noise - nf_s); if ((float)p.noise < nf_base) nf_base += 0.2f * ((float)p.noise - nf_base); else nf_base += 0.0003f * ((float)p.noise - nf_base); }
        if (p.kind == PKT_BAD) { bad_tick++; continue; }
        if (p.kind == PKT_CTRL || !p.who) {
            if (p.rssi < reach || E.mode == MODE_HUNT || ++acks > 6 || frand() > w_wifi) continue;
            float pr = clampf(((float)p.rssi - reach) / (-35.0f - reach), 0, 1);
            strike_t t = { .at = p.frame + 640u, .amp = 0.25f + 0.75f * pr, .timbre = T_TICK };
            snd_strike(&t);
            continue;
        }
        int di = dev_find(p.who, true);
        wdev_t *d = &E.dev[di];
        d->rssi += 0.3f * ((float)p.rssi - d->rssi);
        d->last = ticks; d->hits++; d->chan = p.chan ? p.chan : (uint8_t)cur_chan;
        if (p.kind == PKT_MGMT && p.sub == 8) d->ap = 1;
        if (E.mode == MODE_HUNT) {
            if (E.tw != W_WIFI || di != E.target) continue;
            E.target_rssi += 0.35f * ((float)p.rssi - E.target_rssi);
        } else {
            if (p.rssi < reach) continue;
            if (p.kind == PKT_MGMT && p.sub == 8 && pv[P_BEACON] < 1.0f) continue;
        }
        wifi_frame(&p, di, clampf(((float)p.rssi - reach) / (-35.0f - reach), 0, 1));
    }
    radio_csi_t cs;
    int kc = 0;
    while (kc < 64 && radio_csi(&cs)) {
        kc++;
        int di = dev_find(cs.who, false);
        if (di >= 0) E.dev[di].csi++;
        if (cs.who == room_src || !room_src) room_frame(&cs);
    }
    overflow_rate += 0.1f * (overflow_n * 100.0f - overflow_rate); overflow_n = 0;
    sc.rain = fminf(overflow_rate * w_wifi, 2000.0f);
    E.rate += 0.05f * ((float)k * 100.0f - E.rate);
    room_tick();
    storm_tick();
    channels_tick();
    if (ticks % 100 == 0) {
        int n = 0;
        for (int i = 0; i < NDEV; i++) {
            wdev_t *d = &E.dev[i];
            if (!d->who) continue;
            d->rate += 0.5f * ((float)d->hits - d->rate); d->hits = 0;
            uint32_t age = ticks - d->last;
            if (age > 6000) { if (E.tw == W_WIFI && i == E.target) E.target = -1; d->who = 0; continue; }
            if (age < 3000) n++;
        }
        E.ndev = n;
    }
    for (int i = 0; i < NDEV; i++) E.dev[i].pulse *= 0.93f;
}

// ================================================================ Bluetooth (CHIRP's)
SPSC(advq, ble_adv_t, 256)
static advq_t advq;
static void on_adv(const ble_adv_t *a) { advq_push(&advq, a); }    // the radio's task
typedef struct { float last_adv, rest_until; int phrase_left; uint32_t next_pulse; bool pulse_set; } bird_ui_t;
static bird_ui_t bu[MAXB];
static int  greet_q[8], ngreet, greet_wait;
static float calls_acc, adv_acc, stat_t, bird_calls;
static float len_mul = 1.0f;
static const uint8_t cat_rgb[CAT_N][3] = {
    { 255, 200, 40 }, { 40, 220, 255 }, { 60, 255, 90 }, { 255, 60, 200 }, { 60, 90, 255 }, { 230, 230, 230 }, { 255, 110, 30 }, { 150, 150, 150 },
};
static void flash_led(int cat, float near)
{
    float k = 0.3f + 0.7f * near;
    led_r = fmaxf(led_r, (float)cat_rgb[cat][0] * k); led_g = fmaxf(led_g, (float)cat_rgb[cat][1] * k); led_b = fmaxf(led_b, (float)cat_rgb[cat][2] * k);
}

static float near_of(float rssi, float reach) { return clampf((rssi - reach) / (-45.0f - reach), 0.0f, 1.0f); }

static int bird_midi(const bird_t *b, int extra)
{
    int n = sn(), span = n * (int)pv[P_SPREAD];
    int deg = (int)((b->who >> 8) % (uint32_t)span) + (int)lroundf(walk_deg) + extra + n;    // an octave above the ground
    if (b->cat == CAT_TRACKER || b->cat == CAT_WATCH) deg += n;                                 // small things sing high
    if (b->cat == CAT_SPEAKER || b->cat == CAT_LAPTOP) deg -= n;                                // big things low
    int m = (int)lroundf(degree_note(deg) + pitch_bend);
    while (m > 108) m -= 12;
    while (m < 36) m += 12;
    return m;
}

static uint8_t species_for(uint32_t who, uint8_t cat)
{
    static const uint8_t by_kind[CAT_N] = { SP_CHIRP, SP_WHISTLE, SP_TRILL, SP_PIP, SP_BELL, SP_WARBLE, SP_COO, SP_CHIRP };
    int v = (int)pv[P_BVOICE];
    if (v == 0) { if (cat == CAT_OTHER) { static const uint8_t o[3] = { SP_CHIRP, SP_WARBLE, SP_WHISTLE }; return o[(who >> 3) % 3]; } return by_kind[cat]; }
    if (v == 9) return (uint8_t)((who >> 5) % SP_N);
    return (uint8_t)(v - 1);
}

static bool call(int i, int kind, uint32_t at, float lenf, bool budget)
{
    bird_t *b = &E.birds[i];
    if (budget) {
        if (frand() > w_bt) return false;                          // BALANCE leans the other way
        if (tok_b < 1.0f) return false;
        if (pv[P_GRID] > 0) { at = grid_at(at); if (!at) return false; }
        tok_b -= 1.0f;
    }
    int note = bird_midi(b, 0);
    trig_t t = { .at = at, .hz = midi_hz((float)note), .near = b->near, .len = len_mul * pv[P_CLEN] * lenf, .species = b->species, .kind = (uint8_t)kind };
    if (!snd_call(&t)) return false;
    noted(at, W_BT);
    b->call_t = 0;
    scene_call(i);
    midi_out(1, note, b->near, 0.2f * t.len);
    calls_acc += 1.0f;
    flash_led(b->cat, b->near);
    if (b->near > 0.55f) { E.look = 1.0f; E.look_bird = i; }
    return true;
}

static int alive_count(void) { int n = 0; for (int i = 0; i < MAXB; i++) if (E.birds[i].alive && !E.birds[i].leaving) n++; return n; }
static bool slot_used(int w, int s) { for (int i = 0; i < MAXB; i++) if (E.birds[i].alive && E.birds[i].wire == w && E.birds[i].slot == s) return true; return false; }

static void depart(int i, bool sing)
{
    bird_t *b = &E.birds[i];
    if (!b->alive || b->leaving) return;
    b->leaving = true; b->leave = 0;
    if (sing && E.mode != MODE_HUNT) {
        trig_t t = { .at = frames_now, .hz = midi_hz((float)bird_midi(b, 0)), .near = b->near * 0.6f, .len = 1, .species = b->species, .kind = TR_BYE };
        snd_call(&t);
    }
}

static int new_bird(const ble_adv_t *a, float reach)
{
    float nr = near_of((float)a->rssi, reach);
    if (alive_count() >= (int)pv[P_MAXB]) {
        int f = -1; float fn = 2.0f;
        for (int i = 0; i < MAXB; i++) if (E.birds[i].alive && !E.birds[i].leaving && E.birds[i].near < fn) { fn = E.birds[i].near; f = i; }
        if (f < 0 || fn >= nr - 0.05f) return -1;
        depart(f, false);
    }
    int i = -1;
    for (int k = 0; k < MAXB; k++) if (!E.birds[k].alive) { i = k; break; }
    if (i < 0) return -1;
    int start = (int)(a->who % 3) * 8 + (int)((a->who >> 4) % 8), w = 0, s = 0;
    for (int k = 0; k < MAXB; k++) { int idx = (start + k) % MAXB; w = idx / 8; s = idx % 8; if (!slot_used(w, s)) break; }
    bird_t *b = &E.birds[i];
    memset(b, 0, sizeof *b);
    b->who = a->who; b->alive = true; b->cat = a->cat < CAT_N ? a->cat : CAT_OTHER;
    b->species = species_for(b->who, b->cat);
    b->wire = (uint8_t)w; b->slot = (uint8_t)s;
    b->rssi = (float)a->rssi; b->near = nr;
    bu[i] = (bird_ui_t){ .last_adv = now_s, .rest_until = now_s + 0.4f + frand(), .phrase_left = 1 + (int)(rng_next(&rs) % (uint32_t)pv[P_PHRASE]) };
    if (ngreet < 8) greet_q[ngreet++] = i;
    return i;
}

static void heard(const ble_adv_t *a, float reach)
{
    adv_acc += 1.0f;
    if ((float)a->rssi < reach - 6.0f) return;                    // beyond REACH (with a little room to stay)
    int i = -1;
    for (int k = 0; k < MAXB; k++) if (E.birds[k].alive && E.birds[k].who == a->who) { i = k; break; }
    if (i < 0) { i = new_bird(a, reach); if (i < 0) return; }
    bird_t *b = &E.birds[i];
    bird_ui_t *u = &bu[i];
    if (b->leaving) { if (alive_count() >= (int)pv[P_MAXB]) return; b->leaving = false; b->leave = 0; }
    float dt = now_s - u->last_adv;
    if (b->adverts > 0 && dt > 0.015f) {                          // one advert goes out on three channels: one event
        float est = b->interval, smp = dt;
        if (est > 0) { int k = (int)(dt / est + 0.5f); if (k >= 2 && fabsf(dt / (float)k - est) < 0.3f * est) smp = dt / (float)k; }
        b->interval = clampf(est > 0 ? est + 0.2f * (smp - est) : smp, 0.02f, 10.0f);
    }
    if (dt > 0.015f || !b->adverts) u->last_adv = now_s;
    b->adverts++; b->seen = 0;
    b->rssi = b->adverts == 1 ? (float)a->rssi : b->rssi + 0.25f * ((float)a->rssi - b->rssi);
    b->near = near_of(b->rssi, reach);
    if (E.mode == MODE_HUNT) {
        if (E.tw == W_BT && i == E.target) {
            E.target_rssi += 0.35f * ((float)a->rssi - E.target_rssi);
            if (now_s >= u->rest_until) { call(i, TR_CALL, frames_now, 0.6f, false); u->rest_until = now_s + 0.7f; }
        }
        return;
    }
    if (b->arrive < 1.0f) return;                                 // still flying in
    switch (E.flock) {
    case FL_CHORUS:
        if (now_s >= u->rest_until && tok_b >= 1.0f) {
            if (!call(i, TR_CALL, frames_now + 640u + (rng_next(&rs) % 160u), 1.0f, true)) break;
            float gap = pv[P_REST];
            if (--u->phrase_left > 0) u->rest_until = now_s + gap * (0.25f + 0.2f * frand());
            else { u->rest_until = now_s + gap * (1.5f + 2.5f * frand()); u->phrase_left = 1 + (int)(rng_next(&rs) % (uint32_t)pv[P_PHRASE]); }
        }
        break;
    case FL_SWARM:
        if (tok_b >= 0.25f && frand() <= w_bt) {
            tok_b -= 0.25f;
            int n = sn();
            int deg = (int)(rng_next(&rs) % (uint32_t)(n * ((int)pv[P_SPREAD] + 1))) + 2 * n + (int)lroundf(walk_deg);
            uint32_t at = frames_now + 640u + rng_next(&rs) % 320u;
            trig_t t = { .at = at, .hz = midi_hz(degree_note(deg)), .near = b->near, .len = len_mul * pv[P_CLEN], .kind = TR_GRAIN };
            if (snd_call(&t)) { noted(at, W_BT); b->call_t = 0; calls_acc += 1.0f; flash_led(b->cat, b->near * 0.5f); }
        }
        break;
    default: break;
    }
}

static const int mults[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
static void clocks(void)
{
    const uint32_t ahead = 640;
    float unit = grid_unit();
    bool quant = unit > 0;
    for (int i = 0; i < MAXB; i++) {
        bird_t *b = &E.birds[i];
        bird_ui_t *u = &bu[i];
        if (!b->alive || b->leaving || b->interval <= 0 || b->arrive < 1.0f) { u->pulse_set = false; continue; }
        float per = b->interval * pv[P_SLOW] * (float)AUDIO_SR;
        if (per < 1600.0f) per = 1600.0f;
        int m = 1;
        if (quant) {
            float best = 1e9f;
            for (int k = 0; k < (int)(sizeof mults / sizeof mults[0]); k++) { float d = fabsf(logf((float)mults[k] * unit / per)); if (d < best) { best = d; m = mults[k]; } }
            per = (float)m * unit;
        }
        if (!u->pulse_set || (int32_t)(u->next_pulse - frames_now) < -(int32_t)ahead || (int32_t)(u->next_pulse - frames_now) > (int32_t)(per * 2.0f)) {
            if (quant) {
                uint32_t off = (b->who >> 12) % (uint32_t)m;
                float k = ceilf(((float)frames_now + (float)ahead - (float)off * unit) / per);
                u->next_pulse = (uint32_t)(k * per + (float)off * unit);
            } else u->next_pulse = frames_now + ahead + (uint32_t)(frand() * per);
            u->pulse_set = true;
        }
        if ((int32_t)(u->next_pulse - frames_now) < (int32_t)ahead) {
            uint32_t at = u->next_pulse;
            if (quant && ((int)lroundf((float)at / unit) & 1)) at += (uint32_t)(pv[P_SWING] * 0.01f * unit * 0.5f);
            if (frand() <= w_bt && tok_b >= 1.0f) {                   // the pulse comes out of the same budget
                tok_b -= 1.0f;
                call(i, TR_PULSE, at, 0.45f, false);
            }
            u->next_pulse += (uint32_t)per;
        }
        b->phase = clampf(1.0f - (float)(int32_t)(u->next_pulse - frames_now) / per, 0.0f, 1.0f);
    }
}

static void bt_tick(float dt, float reach)
{
    ble_tick();
    ble_adv_t a;
    int k = 0;
    while (k++ < 128 && advq_pop(&advq, &a)) heard(&a, reach);
    int alive = 0;
    for (int i = 0; i < MAXB; i++) {
        bird_t *b = &E.birds[i];
        if (!b->alive) continue;
        b->seen += dt; b->call_t += dt;
        if (b->arrive < 1.0f) { b->arrive += dt / 0.9f; if (b->arrive > 1.0f) b->arrive = 1.0f; }
        b->near = near_of(b->rssi, reach);
        if (!b->leaving && b->seen > 10.0f) depart(i, true);
        if (b->leaving) { b->leave += dt / 1.2f; if (b->leave >= 1.0f) { b->alive = false; if (E.tw == W_BT && E.target == i) E.target = -1; continue; } }
        else alive++;
    }
    E.nbirds = alive;
    if (alive > (int)pv[P_MAXB]) {
        int f = -1; float fn = 2.0f;
        for (int i = 0; i < MAXB; i++) if (E.birds[i].alive && !E.birds[i].leaving && E.birds[i].near < fn) { fn = E.birds[i].near; f = i; }
        if (f >= 0) depart(f, false);
    }
    // one hello at a time: a crowd arriving at once comes in as a flock, not a burst
    if (greet_wait > 0) greet_wait--;
    else if (ngreet > 0) {
        int i = greet_q[0];
        memmove(greet_q, greet_q + 1, sizeof greet_q[0] * (size_t)(ngreet - 1)); ngreet--;
        if (E.birds[i].alive && !E.birds[i].leaving && E.mode != MODE_HUNT) {
            bird_t *b = &E.birds[i];
            trig_t t = { .at = frames_now, .hz = midi_hz((float)bird_midi(b, 0)), .near = b->near * 0.6f, .len = 1, .species = b->species, .kind = TR_HELLO };
            snd_call(&t);
            scene_call(i);
            flash_led(b->cat, b->near);
            greet_wait = 30;
        }
    }
    if (E.flock == FL_CLOCKS && E.mode != MODE_HUNT) clocks();
    sc.air = clampf(E.adverts / 150.0f, 0.0f, 1.0f);
    stat_t += dt;
    if (stat_t >= 0.5f) {
        bird_calls += 0.5f * (calls_acc / stat_t - bird_calls);
        E.adverts += 0.5f * (adv_acc / stat_t - E.adverts);
        calls_acc = adv_acc = 0; stat_t = 0;
    }
}

// ================================================================ HUNT: a device from either radio
static void hunt_pick(int dir)
{
    typedef struct { int w, i; float rssi; } cand_t;
    cand_t c[NDEV + MAXB];
    int n = 0;
    for (int i = 0; i < NDEV; i++) if (E.dev[i].who && ticks - E.dev[i].last < 1000) c[n++] = (cand_t){ W_WIFI, i, E.dev[i].rssi };
    for (int i = 0; i < MAXB; i++) if (E.birds[i].alive && !E.birds[i].leaving) c[n++] = (cand_t){ W_BT, i, E.birds[i].rssi };
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && c[b].rssi > c[b - 1].rssi; b--) { cand_t t = c[b]; c[b] = c[b - 1]; c[b - 1] = t; }
    if (!n) { E.target = -1; return; }
    int cur = -1;
    for (int i = 0; i < n; i++) if (c[i].w == E.tw && c[i].i == E.target) cur = i;
    int k = dir == 0 || cur < 0 ? 0 : ((cur + dir) % n + n) % n;
    E.tw = c[k].w; E.target = c[k].i; E.target_rssi = c[k].rssi;
    E.target_rank = k + 1; E.target_of = n; E.lost = false;
}

static float t_fast = -90, t_slow = -90, t_seen;
static int t_last_who_w = -1;
static void hunt_tick(float dt)
{
    sc.hunt = E.mode == MODE_HUNT ? 1.0f : 0.0f;
    if (E.mode != MODE_HUNT) return;
    coach_done(LEARN_HUNT);
    bool ok = E.target >= 0 && (E.tw == W_WIFI ? E.dev[E.target].who != 0 : E.birds[E.target].alive);
    if (!ok) hunt_pick(0);
    if (E.target < 0) { E.lost = true; sc.hunt_rate = 0.4f; sc.hunt_pitch = 220.0f; E.prox *= 0.98f; return; }
    int key = E.tw * 100 + E.target;
    if (key != t_last_who_w) { t_last_who_w = key; t_fast = t_slow = E.target_rssi; }
    float since = E.tw == W_WIFI ? (float)(ticks - E.dev[E.target].last) * 0.01f : E.birds[E.target].seen;
    t_seen = since;
    E.lost = since > 5.0f;
    t_fast += 0.08f * (E.target_rssi - t_fast);
    t_slow += 0.006f * (E.target_rssi - t_slow);
    E.trend = t_fast - t_slow > 1.5f ? 1 : t_fast - t_slow < -1.5f ? -1 : 0;
    float p = clampf((t_fast + 92.0f) / 57.0f, 0, 1);
    E.prox = E.lost ? E.prox * 0.99f : p;
    sc.hunt_rate = E.lost ? 0.5f : 1.0f + 15.0f * E.prox * E.prox;
    sc.hunt_pitch = E.lost ? 220.0f : 330.0f * exp2f(2.6f * E.prox);
    (void)dt;
}

// ================================================================ the instrument
static void apply(void)
{
    static const float et[6] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
    float bal = clampf(pv[P_BAL] * 0.01f + bal_stick, -1.0f, 1.0f);
    w_wifi = fminf(1.0f, 1.0f - bal); w_bt = fminf(1.0f, 1.0f + bal);
    float duck = E.mode == MODE_HUNT ? 0.3f : 1.0f;
    sc.wifi = w_wifi; sc.bt = w_bt;
    sc.pkt = pv[P_PKT] * 0.01f;
    sc.room = clampf(pv[P_ROOM] * 0.01f + room_add, 0, 1) * duck;
    sc.storm = pv[P_STORM] * 0.01f * duck;
    sc.birds = pv[P_BIRDS] * 0.01f;
    sc.bright = clampf(pv[P_BRIGHT] * 0.01f + bright_add, 0, 1);
    sc.sweep = 1.0f;
    sc.dist = pv[P_DIST] * 0.01f;
    sc.verb = pv[P_VERB] * 0.01f;
    sc.size = pv[P_SIZE] * 0.01f;
    sc.echo = clampf(pv[P_ECHO] * 0.01f + echo_add, 0, 1);
    sc.efb = pv[P_EFB] * 0.01f;
    float es = 60.0f / pv[P_TEMPO] * et[(int)pv[P_ETIME]];
    while (es > 1.0f) es *= 0.5f;
    sc.echo_s = es;
}

static void changed(int p)
{
    // plain copies and flags: safe before init()
    if (p == -1 || p == P_MODE) { int m = (int)pv[P_MODE]; if (m != E.mode) { E.mode = m; if (m == MODE_HUNT) hunt_pick(0); } }
    if (p == -1 || p == P_FLOCK) { E.flock = (int)pv[P_FLOCK]; for (int i = 0; i < MAXB; i++) bu[i].pulse_set = false; }
    if (p == -1 || p == P_BVOICE) for (int i = 0; i < MAXB; i++) if (E.birds[i].alive) E.birds[i].species = species_for(E.birds[i].who, E.birds[i].cat);
    if (p == -1 || p == P_BTTIME) { static const int sh[3] = { 25, 50, 75 }; ble_set_share(sh[(int)pv[P_BTTIME]]); }
    static int chan_mode = -1;                                   // a sound that leaves the channel as it was must not rescan
    if ((p == -1 || p == P_CHAN) && (int)pv[P_CHAN] != chan_mode) {
        chan_mode = (int)pv[P_CHAN]; dwell_ticks = 0; chan_hits = 0;
        if (chan_mode == CH_AUTO) { E.scanning = true; scan_i = 0; set_chan(1); }
    }
    apply();
}

static void hunt_toast(void)
{
    char b[40];
    if (E.target < 0) { toast("NOTHING TO HUNT YET", 1200); return; }
    if (E.tw == W_WIFI) snprintf(b, sizeof b, "WI-FI %s  %d/%d", E.dev[E.target].ap ? "ROUTER" : "DEVICE", E.target_rank, E.target_of);
    else snprintf(b, sizeof b, "BT %s  %d/%d", cat_name[E.birds[E.target].cat], E.target_rank, E.target_of);
    toast(b, 1100);
}

static void action(int p)
{
    if (p != P_NEXT) return;
    if (E.mode != MODE_HUNT) { toast("SET MODE TO HUNT FIRST", 1500); return; }
    hunt_pick(1); hunt_toast();
}

void radios_start(void)
{
    E.wifi_ok = radio_start();
    if (!E.wifi_ok) app_log("Wi-Fi would not start: the ground is quiet, the birds go on");
    E.bt_ok = ble_start(on_adv);
    if (!E.bt_ok) app_log("Bluetooth would not start: the sky is empty, the Wi-Fi goes on");
    E.scanning = (int)pv[P_CHAN] == CH_AUTO;                     // a fresh look at the channels
    scan_i = 0; dwell_ticks = 0; chan_hits = 0; memset(E.hist, 0, sizeof E.hist);
    cur_chan = 0; set_chan((int)pv[P_CHAN] >= 2 ? (int)pv[P_CHAN] - 1 : 1);
    radio_set_broken(true);
    static const int sh[3] = { 25, 50, 75 };
    ble_set_share(sh[(int)pv[P_BTTIME]]);
}

static void init(void)
{
    snd_init();
    E.target = -1; E.mode = (int)pv[P_MODE]; E.flock = (int)pv[P_FLOCK]; E.look_bird = -1;
    for (int b = 0; b < CSI_BANDS; b++) E.shape[b] = 1.0f;
    radios_start();
    apply();
}

static bool stick_armed = true;
static float last_bal = 0;
static void motion(const ctl_t *c)
{
    float ty = c->has_imu ? clampf(c->lean_y / 40.0f, -1, 1) : 0;
    float lx = c->has_imu ? clampf(c->lean_x / 40.0f, -1, 1) : 0;
    float jx = c->has_joy ? c->jx : 0, jy = c->has_joy ? c->jy : 0;
    ring_mul = 1; pitch_bend = 0; reach_add = 0; bright_add = 0; echo_add = 0; dens_mul = 1; room_add = 0; bal_stick = 0; len_mul = 1;
    float walk = 0;
    switch ((int)pv[P_TILT]) { case 1: ring_mul = exp2f(ty * 2.5f); len_mul = exp2f(ty * 1.2f); break; case 2: pitch_bend = ty * 12.0f; break; case 3: reach_add = ty * 30.0f; break; }
    switch ((int)pv[P_LEAN]) { case 1: walk += lx * 6.0f; break; case 2: bright_add += lx * 0.5f; break; case 3: echo_add += fmaxf(lx, 0) * 0.7f; break; }
    if (E.mode != MODE_HUNT)
        switch ((int)pv[P_STX]) { case 1: bal_stick = jx; break; case 2: bright_add += jx * 0.5f; break; case 3: walk += jx * 6.0f; break; }
    switch ((int)pv[P_STY]) { case 1: reach_add += jy * 45.0f; break; case 2: dens_mul = exp2f(jy * 2.5f); break; case 3: room_add = jy * 0.6f; break; }
    walk_deg += 0.25f * (walk - walk_deg);
    float bal = clampf(pv[P_BAL] * 0.01f + bal_stick, -1, 1);
    if (fabsf(bal - last_bal) > 0.02f) { E.bal_shown = 1.5f; last_bal = bal; }
    if (E.mode == MODE_HUNT && c->has_joy) {                      // a flick picks the next or the previous device
        if (stick_armed && fabsf(jx) > 0.7f) { hunt_pick(jx > 0 ? 1 : -1); stick_armed = false; hunt_toast(); }
        else if (fabsf(jx) < 0.3f) stick_armed = true;
    }
}

static void tick(const ctl_t *c)
{
    ticks++;
    float dt = c->dt;
    now_s += dt; frames_now = c->frames;
    motion(c);
    apply();
    hunt_tick(dt);
    float reach = clampf(pv[P_REACH] + reach_add, -100.0f, -30.0f);
    // one budget for both worlds, shared by BALANCE; what one side leaves, the other may use
    float dens = clampf(pv[P_DENS] * dens_mul, 1.0f, 1000.0f), ws = w_wifi + w_bt;
    float rw = dens * dt * w_wifi / ws, rb = dens * dt * w_bt / ws, cap = fmaxf(2.0f, dens * 0.125f);
    tok_w += rw; tok_b += rb;
    if (tok_w > cap) { tok_b += (tok_w - cap) * (w_bt > 0 ? 1.0f : 0.0f); tok_w = cap; }
    if (tok_b > cap) { tok_w += (tok_b - cap) * (w_wifi > 0 ? 1.0f : 0.0f); tok_b = cap; }
    if (tok_w > cap) tok_w = cap;
    wifi_tick(reach);
    bt_tick(dt, reach);
    // the screen's decays
    E.sniff *= 0.9f; E.thunder *= 0.9f; E.look *= 0.97f;
    if (E.bal_shown > 0) E.bal_shown -= dt;
    // MIDI: note-offs, and CCs (3 Wi-Fi frames a second, 4 room motion, 5 storm, 6 HUNT nearness, 8 birds, 9 bird calls a second)
    for (int i = 0; i < nnoff; ) {
        if (noff[i].t <= now_s) { midi_note_ch(noff[i].ch, noff[i].note, 0); noff[i] = noff[--nnoff]; } else i++;
    }
    if (ticks % 5 == 0) {
        static int last[6] = { -1, -1, -1, -1, -1, -1 };
        static const int ccn[6] = { 3, 4, 5, 6, 8, 9 };
        int v[6] = { (int)(log10f(1.0f + E.rate) / log10f(2001.0f) * 127.0f), (int)(E.motion * 127.0f), (int)(E.storm * 127.0f),
                     (int)(E.prox * 127.0f), E.nbirds * 5, (int)bird_calls };
        for (int i = 0; i < 6; i++) {
            int x = v[i] < 0 ? 0 : v[i] > 127 ? 127 : v[i], d = x - last[i];
            if (d >= 2 || d <= -2 || (x != last[i] && (x == 0 || x == 127))) { last[i] = x; midi_cc(ccn[i], x); }
        }
    }
    // the LED: Wi-Fi green/blue/yellow, each bird its kind's colour, red in a storm; HUNT blinks with its beeps
    static uint32_t beeps_seen;
    if (E.mode == MODE_HUNT && snd_beeps != beeps_seen) { beeps_seen = snd_beeps; led_g += 120.0f + 135.0f * E.prox; led_r += 120.0f * (1.0f - E.prox); }
    if (E.storm > 0.05f && ticks % 7 < 2) led_r += 25.0f * E.storm;
    led_b += 30.0f * E.motion * sc.room;
    led_r = fminf(led_r, 255.0f); led_g = fminf(led_g, 255.0f); led_b = fminf(led_b, 255.0f);
    led_rgb((uint8_t)led_r, (uint8_t)led_g, (uint8_t)led_b);
    led_r *= 0.84f; led_g *= 0.84f; led_b *= 0.86f;
    if (E.nbirds > 0 && E.ndev > 0 && now_s > 40.0f) coach_done(LEARN_LISTEN);
}

static void midi_in(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) == 0x90 && d2 > 0) param_set(P_KEY, (float)(d1 % 12));     // a note sets the key
}

const instrument_t INST = {
    .name = "ETHER", .tagline = "WI-FI AND BLUETOOTH AS ONE",
    .params = params, .nparams = P_N, .groups = groups, .ngroups = 6,
    .presets = presets, .npresets = NPRESETS, .hints = hints, .nhints = (int)(sizeof hints / sizeof hints[0]),
    .looper = true,
    .init = init, .audio = snd_audio, .tick = tick, .draw = scene_draw, .splash = scene_splash,
    .changed = changed, .action = action, .midi_in = midi_in, .saver = scene_saver,
};
