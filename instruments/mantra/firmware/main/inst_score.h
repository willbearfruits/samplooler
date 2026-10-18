// SCORE (inst_score.c, inst_bowl.c): what the voices sing, and the singing bowl. Owned by the SCORE builder; refine
// it, but report every change.
//
// It runs in the UI task: it pushes phonemes (mv_push, a little ahead, with the audio frame each starts at, on the
// song's beat) and sets the free voices (the drones, the monks) in the mv_ctl_t the host passes it each tick. The host
// fills the global controls (jaw, hum, bend ...) from the hands first; the score may override what its mode needs (the
// melody's overtone, the hum at the end of an OM), mixing in the hands where the mode says so.
//
// Timing: everything the score sings is placed in song beats from an anchor (the beat the mode started on), so it
// follows the song's tempo and the link wherever they move the beat. Each phoneme is pushed about 150 ms before it
// starts, with its audio frame; consonants start before the beat so that the vowel lands on it.
//
// The knob's blend (mn_blend) from the sound's chant to Daisy Bell, in four zones: 0..0.25 the chant alone; 0.25..0.5
// the chant's voices hum Daisy's melody on M, OW, AH over its drone (THROAT and KARGYRAA: the whistle plays it on the
// drone's harmonics); 0.5..0.75 they sing her words over the drone; 0.75..1 Daisy alone, one voice and the hummed
// waltz. Daisy runs from one anchor all along; a new zone takes over at her next bar line with no note held across
// it; the drones and the voices' controls cross over that bar. The DAISY sounds are Daisy all the way: four singers
// at 0, one at 1, the waltz throughout.
//
// Voices: the chant: OM 0, 1 (an octave below), 4, 2, 3; OM MANI 0 and 1 (a free drone); THROAT 0 (free: the drone
// and its whistle); KARGYRAA 0; VOWELS and HUM 0 (free). Daisy: the lead 0 (OM, OM MANI, DAISY) or 1 (THROAT,
// KARGYRAA, VOWELS, HUM, over the drone on 0); the waltz 2 and 3; OM's drone under Daisy 5; the DAISY choir 0, 1, 4, 5.
#pragma once
#include "core.h"
#include "inst_voice.h"

enum { MODE_OM, MODE_MANI, MODE_THROAT, MODE_KARGYRAA, MODE_DAISY, MODE_VOWELS, MODE_HUM, MODE_N };
// MODE_OM: monks chanting OM (OW to M) on a deep drone, a breath between; MODE_MANI: OM MANI PADME HUM recited on one
// tone, falling at the phrase's end; MODE_THROAT: a drone and the whistle's melody on the harmonics (the hands'
// yaw and roll take it over); MODE_KARGYRAA: the undertone growl, vowels moving; MODE_DAISY: the chorus of Daisy Bell
// sung syllable by syllable, a hummed waltz under it; MODE_VOWELS: free: the jaw is the vowel, roll the note, pumping
// the breath; MODE_HUM: the lead follows the mic's pitch (mv_track) with the jaw's vowel
void  sc_init(void);
void  sc_tick(const ctl_t *c, mv_ctl_t *m);      // UI task, 100 Hz: the mode's notes and phonemes; fills m->v[] and may
                                                 // set m's overtone, sharp, hum, jaw, sub, choir, rough, tract, c64,
                                                 // breath, level (the host calls mv_set(m) after)
int   sc_mode(void);                             // the mode now (a SCORE param, set by the sound)
// the singing bowl: struck by a knock; rings for half a minute, beating
void  bowl_strike(float strength);               // UI task: now
void  bowl_strike_at(float strength, uint32_t frame);   // UI task: at that audio frame (the score's strikes on the beat)
void  bowl_set(float hz, float t60, float hard); // UI task: its pitch, its ring (s) and the mallet (0 soft .. 1 hard),
                                                 // taken at the next strike (the score sets them every tick)
void  bowl_process(float *out, float *send, int n);   // audio: ADD
float bowl_level(void);                          // 0..1 how loud it rings now (any task)
// for the screen (UI task)
typedef struct {
    int   mode;
    const char *line;          // the lyric line now whenever words are sung (Daisy Bell, the chant, the vowels), NULL: none
                               // (Daisy hummed has no words)
    int   word_at, word_len;   // the syllable sung now, as a span of `line` (characters); word_len 0: none
    float breath;              // 0..1 the breath cycle (0: lungs empty .. 1 full): the Buddha's chest
    int   harmonic;            // the whistle's harmonic now (THROAT, or the yaw's whistle anywhere), 0 none
    float bowl;                // 0..1 how loud the bowl rings
    int   monks;               // voices singing now, drones and the waltz included (the choir drawn around the Buddha)
} sc_view_t;
void  sc_view(sc_view_t *v);
