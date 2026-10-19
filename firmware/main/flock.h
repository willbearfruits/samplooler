// The Samplooler in the flock (link.h: the instruments' shared clock and session over ESP-NOW). What it adds to
// the link, in the UI task:
//
//   the loop and the tempo   Alone, or first in the flock (nobody has set a tempo and nobody has a loop): a take
//                            sets the tempo. Its length is split into n beats, n in 1 2 4 8 16, so the tempo lands
//                            in 60..180 BPM (4 or 8 beats when they fit), the loop is trimmed to n whole beats, and
//                            beat 0 is the loop's start: WRECK and MANTRA follow it. With a tempo already set, or
//                            a loop elsewhere (SING's, BAND's): the take closes on whole beats of that tempo (the
//                            nearest, at least one; let go early it records on to the beat) and plays with its
//                            start on the beat it began on; the tempo and beat 0 stay where they were.
//   the head on the beat     With boards around, while the tilt's speed sits on 1x, 2x, 4x, 8x, 1/2x or 1/4x the
//                            head is kept on the grid (the link's slow slew, the speed's own slew); with Snap on, a
//                            head scrubbed far away is crossfaded back to the beat when the speed settles on a ratio.
//                            At other speeds the loop drifts: that is the instrument. Alone, nothing changes.
//   the key and the scale    The Voice's Key and Scale are the flock's harmony: a change here is sent, a change
//                            elsewhere lands in the settings. The numbering is SING's (key 0..11 from C; MAJOR,
//                            MINOR, DORIAN, MIXOLYDIAN, PHRYGIAN, HARM MINOR).
#pragma once
#include <stdbool.h>
#include <stdint.h>

void  flock_init(int key, int scale);                  // at boot, before the first tick (link_init, the boot session)
void  flock_enable(bool on, int group, int range);     // the Link, Group and Range settings (any change: call again)
void  flock_tick(float dt, bool snap);                 // every 10 ms: the link, the grid to the recorder, the news; snap: the Snap setting
void  flock_loop_made(int len, uint32_t start);        // a loop has begun playing: its length (frames) and the frame of its position 0
void  flock_loop_gone(void);                           // the loop is gone (a new take, a tap, a load)
int   flock_snap_len(int take);                        // a loaded take: the loop length to play on the flock's beats (0: as it is)
bool  flock_snapping(void);                            // takes close on the flock's beats (boards around with a tempo or a loop)
bool  flock_harm_news(int *key, int *scale);           // the flock changed the key or the scale (once per change)
void  flock_harm_set(int key, int scale);              // the user changed them here
int   flock_peers(void);                               // boards heard lately
float flock_bpm(void);                                 // the session's tempo
int   flock_beat_flash(void);                          // > 0 for a few ticks after each beat (3 on the bar), for the screen
const char *flock_news(void);                          // a line for the screen when a board comes or goes (once), else NULL
void  flock_who(char *b, int n);                       // the "Who is there" text
