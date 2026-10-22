// The link on the host: several boards in one process, each with its own link state and a crystal that runs
// at its own rate, talking through a pretend air (queueing before the air, a receive delay, losses). Checks
// that the boards agree on the clock and the session, and how closely: beat 0 must fall at the same true moment
// on every board.
//
//   tools/linktest/run.sh       builds firmware/main/link.c with this file and runs every scenario
#include "../../firmware/main/link.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- pretend boards
#define NB 4
typedef struct {
    link_state_t *st;
    uint8_t mac[6];
    double off_us, ppm, epoch;        // its clock: off + true_us * (1 + ppm 1e-6); its audio frame 0 at clock = epoch
    bool on;
} board_t;
static board_t B[NB];
static int cur = -1;                  // the board whose calls are running
static double T;                      // true time, us
static double loss = 0.0, q_max = 2500, rx_max = 400;   // queueing before the air, receive delay (us)
static uint32_t seed = 12345;
static double rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (double)(seed % 1000000u) / 1e6; }

static double clk(int b, double t) { return B[b].off_us + t * (1.0 + B[b].ppm * 1e-6); }
static double frame_at(int b, double t) { return (clk(b, t) - B[b].epoch) / 31.25; }           // its audio frame at true t

typedef struct { double t; int to; uint8_t from[6]; int n; uint8_t b[256]; } air_t;
static air_t air[4096];
static int nair;

void app_log(const char *fmt, ...) { (void)fmt; }
bool plat_link_start(int power) { (void)power; B[cur].on = true; return true; }
void plat_link_stop(void) { B[cur].on = false; }
bool plat_link_send(const void *b, int n)
{
    double ta = T + rnd() * q_max;                                  // waits for the air
    for (int k = 0; k < NB; k++) {
        if (k == cur || !B[k].st || !B[k].on) continue;
        if (rnd() < loss) continue;
        if (nair >= 4096) break;
        air_t *a = &air[nair++];
        a->t = ta + 150 + rnd() * rx_max; a->to = k; memcpy(a->from, B[cur].mac, 6); a->n = n; memcpy(a->b, b, (size_t)n);
    }
    return true;
}
int64_t plat_link_us(void) { return (int64_t)clk(cur, T); }
int64_t plat_link_epoch(void) { return (int64_t)B[cur].epoch; }
void plat_link_mac(uint8_t mac[6]) { memcpy(mac, B[cur].mac, 6); }

static void use(int b) { cur = b; link_use(B[b].st); }

static void boot(int b, const char *name, int prio, double off_us, double ppm, int group)
{
    B[b].st = link_new_state();
    for (int i = 0; i < 6; i++) B[b].mac[i] = (uint8_t)(0x30 + b * 7 + i);
    B[b].off_us = off_us; B[b].ppm = ppm;
    B[b].epoch = clk(b, T) + 1.3e6;                                 // the audio starts a moment after power-on
    use(b);
    link_init(name, prio);
    link_boot_time(19200, (uint32_t)frame_at(b, T), 4);              // 100 BPM, beat 0 now
    link_boot_harm(0, 0, 1, 0, 0);
    link_enable(true, group, 1);
}

static void step(double ms)
{
    for (double end = T + ms * 1000.0; T < end; T += 10000.0) {
        // deliver what has landed (stamped with the receiver's clock at landing)
        for (int i = 0; i < nair;) {
            if (air[i].t <= T + 10000.0) {
                int k = air[i].to; double tl = air[i].t;
                if (B[k].st && B[k].on) { use(k); link_rx(air[i].from, air[i].b, air[i].n, (int64_t)clk(k, tl), -40); }
                air[i] = air[--nair];
            } else i++;
        }
        for (int b = 0; b < NB; b++) if (B[b].st) { use(b); link_tick(0.01f); }
    }
}

// where in its beat board b is at true time t (beats since beat 0, fractional)
static double beats_at(int b, double t)
{
    use(b);
    uint32_t o; float f; link_origin(&o, &f);
    double fr = frame_at(b, t);
    double rel = fmod(fr - (double)o - (double)f, 4294967296.0);    // the frame counter wraps at 2^32
    if (rel < 0) rel += 4294967296.0;
    if (rel > 2147483648.0) rel -= 4294967296.0;
    return rel / (double)link_time()->fpb;
}
// how far apart the beats on two boards are now, in ms (modulo a beat)
static double beat_gap_ms(int a, int b)
{
    double d = beats_at(b, T) - beats_at(a, T);
    d -= floor(d + 0.5);
    use(a);
    return d * link_time()->fpb * 31.25 / 1000.0;
}

static int run, failed;
static void check(bool ok, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("%s ", ok ? "  ok  " : "  FAIL"); vprintf(fmt, ap); printf("\n");
    va_end(ap);
    run++; if (!ok) failed++;
}

static void reset(void)
{
    for (int b = 0; b < NB; b++) { if (B[b].st) free(B[b].st); memset(&B[b], 0, sizeof B[b]); }
    nair = 0; T = 5e6; loss = 0; q_max = 2500; rx_max = 400;
}

int main(void)
{
    printf("== two boards, 35 ppm apart, booted 3 hours apart\n");
    reset();
    boot(0, "BAND", 2, 0, +20, 1);
    step(400);
    boot(1, "SING", 1, 3.0 * 3600e6, -15, 1);
    step(1500);
    use(1);
    check(!link_is_clock() && link_synced(), "SING takes BAND as the clock and is synced after 1.5 s");
    link_peer_t pr[4]; int np = link_peers(pr, 4);
    check(np == 1 && !strcmp(pr[0].name, "BAND") && pr[0].clock, "SING hears BAND (%d peer%s)", np, np == 1 ? "" : "s");
    use(0); check(link_is_clock(), "BAND is the clock");
    step(5000);
    double g = beat_gap_ms(0, 1);
    check(fabs(g) < 0.5, "after 6 s the beats are %.3f ms apart (< 0.5)", g);
    use(1); check(link_time()->fpb == 19200, "SING runs BAND's tempo");
    double worst = 0;
    for (int i = 0; i < 60; i++) { step(1000); double x = fabs(beat_gap_ms(0, 1)); if (x > worst) worst = x; }
    check(worst < 0.5, "over the next minute the worst gap is %.3f ms (< 0.5; a block is 2 ms)", worst);

    printf("== a tempo change on SING reaches BAND, beat continuous\n");
    use(1);
    {
        uint32_t o; float f; link_origin(&o, &f);
        double now = frame_at(1, T);
        // keep the beat now where it is: the new origin is the beat now at the new tempo
        uint32_t fpb = 16000;                                  // 120 BPM
        double beats = (now - ((double)o + (double)f)) / 19200.0;
        link_set_time(fpb, (uint32_t)(now - beats * fpb), 4, 0);
    }
    step(500);
    use(0); check(link_time()->fpb == 16000, "BAND took 120 BPM from SING (fpb %u)", (unsigned)link_time()->fpb);
    check(link_time_news(), "BAND was told");
    step(3000);
    g = beat_gap_ms(0, 1);
    check(fabs(g) < 0.5, "the beats are %.3f ms apart", g);

    printf("== a loop on BAND locks the tempo\n");
    use(0); link_set_time(16000, link_time()->origin, 4, 8); link_set_flags(LINKF_LOOP);
    step(300);
    use(1); check(link_flags_any() & LINKF_LOOP, "SING knows a board has a loop");
    check(link_time()->beats == 8, "and the session's loop is 8 beats (%d)", link_time()->beats);
    link_set_time(24000, 0, 4, 0);
    step(600);
    use(1); check(link_time()->fpb == 16000, "SING's tempo change lost to BAND's loop (fpb %u)", (unsigned)link_time()->fpb);
    use(0); check(link_time()->fpb == 16000, "BAND kept its loop's tempo");
    use(0); link_set_flags(0);
    step(300);
    use(1); link_set_time(24000, 0, 4, 0);
    step(600);
    use(0); check(link_time()->fpb == 24000, "with the loop gone, the tempo is free again (BAND now %u)", (unsigned)link_time()->fpb);

    printf("== the harmony\n");
    use(1); link_set_harm(7, 1, 3, 0, 0, -1);
    step(300);
    use(0); const link_harm_t *h = link_harm();
    check(link_harm_news() && h->key == 7 && h->scale == 1 && h->prog == 3, "BAND took G minor, progression 3 from SING");
    use(0); link_set_harm(7, 1, 3, 0, 0, 4);
    step(200);
    use(1); check(link_harm()->live == 4, "a chord played live on BAND reaches SING");

    printf("== a late board joins the session as it is\n");
    boot(2, "THIRD", 0, 7e6, +5, 1);
    step(2000);
    use(2); check(link_time()->fpb == 24000 && link_harm()->key == 7, "it took the tempo and the key");
    check(fabs(beat_gap_ms(0, 2)) < 0.5, "its beat is %.3f ms from BAND's", beat_gap_ms(0, 2));
    printf("== another group is not heard\n");
    boot(3, "OTHER", 5, 9e6, 0, 2);
    step(1500);
    use(1); np = link_peers(pr, 4);
    check(np == 2, "SING hears 2 boards, not the one in group 2 (%d)", np);
    use(3); check(link_is_clock() && link_peers(pr, 4) == 0, "the group-2 board is alone");

    printf("== the clock board leaves\n");
    double before = beat_gap_ms(1, 2);
    use(0); link_enable(false, 1, 1);
    step(2500);
    use(1); check(link_is_clock(), "SING is the clock now (the highest priority left)");
    double after = beat_gap_ms(1, 2);
    check(fabs(after) < 0.5, "SING and THIRD still agree (%.3f ms, before %.3f)", after, before);
    {
        // and SING's own beat did not move when it became the clock
        use(1);
        check(link_time()->fpb == 24000, "the tempo stayed");
    }

    printf("== a busy room: 25 %% of packets lost, 8 ms queues\n");
    reset();
    loss = 0.25; q_max = 8000; rx_max = 1500;
    boot(0, "BAND", 2, 0, +25, 1);
    boot(1, "SING", 1, 1e9, -20, 1);
    step(3000);
    use(1); check(link_synced(), "synced within 3 s");
    step(10000);
    worst = 0;
    for (int i = 0; i < 30; i++) { step(1000); double x = fabs(beat_gap_ms(0, 1)); if (x > worst) worst = x; }
    check(worst < 1.5, "worst gap over 30 s: %.3f ms (< 1.5)", worst);

    printf("== two loops at different tempos\n");
    reset();
    boot(0, "BAND", 2, 0, 0, 1);
    boot(1, "SING", 1, 2e6, 0, 1);
    use(0); link_enable(false, 1, 1); use(1); link_enable(false, 1, 1);
    use(0); link_set_time(16000, 0, 4, 8); link_set_flags(LINKF_LOOP);
    use(1); link_set_time(20000, 0, 4, 4); link_set_flags(LINKF_LOOP);
    use(0); link_enable(true, 1, 1); use(1); link_enable(true, 1, 1);
    step(2000);
    use(0); bool c0 = link_tempo_clash(); use(1); bool c1 = link_tempo_clash();
    use(0); uint32_t f0 = link_time()->fpb; use(1); uint32_t f1 = link_time()->fpb;
    check(f0 == 16000 && f1 == 20000, "each keeps its own loop's tempo (%u, %u)", (unsigned)f0, (unsigned)f1);
    check(c0 || c1, "and one of them says the tempos clash");

    printf("== two boards that ran apart meet: the clock board's session is the one\n");
    reset();
    boot(0, "BAND", 2, 0, 0, 1);
    boot(1, "SING", 1, 3e6, 0, 1);
    use(0); link_enable(false, 1, 1); use(1); link_enable(false, 1, 1);
    use(0); link_set_time(16000, 0, 4, 0); link_set_harm(9, 1, 6, 0, 0, -1);
    use(1); for (int i = 0; i < 5; i++) link_set_time(20000 + (uint32_t)i, 0, 4, 0);   // many changes made alone
    link_set_harm(2, 0, 1, 0, 0, -1);
    use(0); link_enable(true, 1, 1); use(1); link_enable(true, 1, 1);
    step(2000);
    use(1); check(link_time()->fpb == 16000 && link_harm()->key == 9, "SING joined the BAND's 120 BPM and A minor (fpb %u, key %d)", (unsigned)link_time()->fpb, link_harm()->key);
    use(0); check(link_time()->fpb == 16000 && link_harm()->key == 9, "the BAND kept them");
    printf("== ... unless the other has a loop and the clock board has none\n");
    reset();
    boot(0, "BAND", 2, 0, 0, 1);
    boot(1, "SING", 1, 3e6, 0, 1);
    use(0); link_enable(false, 1, 1); use(1); link_enable(false, 1, 1);
    use(0); link_set_time(16000, 0, 4, 0);
    use(1); link_set_time(24806, 0, 4, 4); link_set_flags(LINKF_LOOP);
    use(0); link_enable(true, 1, 1); use(1); link_enable(true, 1, 1);
    step(2000);
    use(0); check(link_time()->fpb == 24806, "the BAND took SING's looped tempo (fpb %u)", (unsigned)link_time()->fpb);
    use(1); check(link_time()->fpb == 24806 && !link_tempo_clash(), "SING kept it");

    printf("\n%d checks, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
