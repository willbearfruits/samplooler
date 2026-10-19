// See flock.h. Host-testable: tools/flocksim builds it with link.c and recorder_f32.c against a pretend air.
#include "flock.h"
#include "link.h"
#include "recorder.h"
#include <stdio.h>
#include <string.h>

#define SR     REC_SAMPLE_RATE
#define BAR    4
static bool  up;
static int   peers_was, peers_now, beat_flash;
static uint32_t last_beat = 0xFFFFFFFFu;
static char  news[40];
static bool  have_news;
static bool  snap_now;
static int   key_here, scale_here;
static uint32_t fpb_mine;                               // the tempo this board's own take set (0: the flock's tempo is someone else's)

static uint32_t fpb_now(void) { uint32_t f = link_fpb(); return f ? f : link_time()->fpb; }

void flock_init(int key, int scale)
{
    key_here = key; scale_here = scale;
    link_init("SAMPLOOL", 1);
    link_boot_time(19200, 0, BAR);                      // 100 BPM, beat 0 at frame 0: gives way to any board that set one
    link_boot_harm(key, scale, 0, 0, 0);
    up = true;
}

void flock_enable(bool on, int group, int range)
{
    if (!up) return;
    link_enable(false, 1, 1);                           // off and on again: a new group or range takes at once
    link_enable(on, group, range);
    peers_was = 0;
}

// Takes close on the flock's beats when there are boards around and another of them set the tempo (a change, not a
// boot default), or a loop is playing anywhere. Alone, with boards that only booted, or when the tempo is this
// board's own last take's, a take is free and sets the tempo.
static bool snapping_now(void)
{
    if (peers_now <= 0) return false;
    const link_time_t *t = link_time();
    return t->beats > 0 || (t->ver > 0 && fpb_now() != fpb_mine) || (link_flags_any() & LINKF_LOOP);
}
bool flock_snapping(void) { return snap_now; }

void flock_tick(float dt, bool snap)
{
    if (!up) return;
    link_tick(dt);
    link_peer_t pr[LINK_PEERS_MAX];
    peers_now = link_peers(pr, LINK_PEERS_MAX);
    snap_now = snapping_now();
    // the grid to the recorder: beat 0 in our frames and the frames per beat; what to do with it
    int mode = 0;
    if (peers_now > 0) mode = GRID_LOCK | (snap ? GRID_JUMP : 0) | (snap_now ? GRID_SNAP_END : 0);
    uint32_t o = 0; float f = 0;
    if (link_synced()) link_origin(&o, &f); else mode = 0;
    uint32_t fpb = fpb_now();
    recorder_set_grid(fpb, o, f, mode);
    // the beat, for the screen
    uint32_t b = (recorder_frames() - o) / fpb;
    if (b != last_beat) { last_beat = b; beat_flash = (b % BAR == 0) ? 3 : 2; }
    else if (beat_flash > 0) beat_flash--;
    link_time_news();
    if (peers_now != peers_was) {
        if (peers_now > peers_was && peers_now > 0) snprintf(news, sizeof news, "LINKED: %s", pr[peers_now - 1].name);
        else snprintf(news, sizeof news, peers_now ? "A BOARD LEFT" : "LINK: ALONE");
        have_news = true;
        peers_was = peers_now;
    }
}

// n beats for a free take so the tempo lands in 60..180 BPM: 4 or 8 when they fit, then 2, 16, 1; outside the
// range, the nearest end of it
static int beats_for(int len)
{
    static const int pref[] = { 4, 8, 2, 16, 1 };
    float best_d = 1e9f; int best = 4;
    for (int i = 0; i < 5; i++) {
        float bpm = 60.0f * (float)SR * (float)pref[i] / (float)len;
        if (bpm >= 60.0f && bpm <= 180.0f) return pref[i];
        float d = bpm < 60.0f ? 60.0f - bpm : bpm - 180.0f;
        if (d < best_d) { best_d = d; best = pref[i]; }
    }
    return best;
}

void flock_loop_made(int len, uint32_t start)
{
    if (!up || len <= 0) return;
    if (!snap_now) {
        // alone or first: the take is the tempo. n whole beats of len / n frames, the loop trimmed to them; beat 0 at
        // the loop's start, bars of four
        int n = beats_for(len);
        uint32_t fpb = (uint32_t)(len / n);
        if (fpb < 3200) { n = 1; fpb = (uint32_t)len; }
        if (fpb > 96000) fpb = 96000;
        recorder_set_loop((int)(fpb * (uint32_t)n), start, start);
        link_set_time(fpb, start, BAR, n);
        link_set_flags(LINKF_LOOP);
        fpb_mine = fpb;
        return;
    }
    // the flock's tempo: the loop is whole beats of it (the recorder closed it so; a loaded loop is trimmed here)
    uint32_t fpb = fpb_now();
    int n = (int)((uint32_t)len / fpb);
    if (n < 1) n = 1;
    if ((int)(fpb * (uint32_t)n) < len) recorder_set_loop((int)(fpb * (uint32_t)n), start, start);
    const link_time_t *t = link_time();
    if (!t->beats) {                                    // the session's loop: ours, the first; beat 0 stays where it is
        uint32_t o; float f; link_origin(&o, &f);       // (in our frames: link_set_time maps it back to the network's)
        link_set_time(t->fpb, o, t->bar ? t->bar : BAR, n);
    }
    link_set_flags(LINKF_LOOP);
}

void flock_loop_gone(void) { if (up) link_set_flags(0); }

int flock_snap_len(int take)
{
    if (!up || !snap_now || take <= 0) return 0;
    int fpb = (int)fpb_now();
    int n = (take + fpb / 2) / fpb;
    if (n < 1) n = 1;
    return n * fpb;
}

bool flock_harm_news(int *key, int *scale)
{
    if (!up || !link_harm_news()) return false;
    const link_harm_t *h = link_harm();
    *key = h->key % 12; *scale = h->scale;
    key_here = *key; scale_here = *scale;
    return true;
}
void flock_harm_set(int key, int scale)
{
    if (!up) return;
    key_here = key; scale_here = scale;
    const link_harm_t *h = link_harm();
    link_set_harm(key, scale, h->prog, h->hold, h->bars, -1);
}

int   flock_peers(void) { return peers_now; }
float flock_bpm(void)   { return 60.0f * (float)SR / (float)fpb_now(); }
int   flock_beat_flash(void) { return beat_flash; }
const char *flock_news(void) { if (!have_news) return NULL; have_news = false; return news; }
void  flock_who(char *b, int n)
{
    link_peer_t pr[LINK_PEERS_MAX];
    int np = link_peers(pr, LINK_PEERS_MAX);
    if (!link_radio()) snprintf(b, (size_t)n, "LINK IS OFF");
    else if (!np) snprintf(b, (size_t)n, "NO BOARD HEARD YET");
    else {
        int k = snprintf(b, (size_t)n, "%s", pr[0].name);
        for (int i = 1; i < np && k < n - 10; i++) k += snprintf(b + k, (size_t)(n - k), " + %s", pr[i].name);
    }
}
