# AIR

Drum on nothing. Hold the box like a stick and swing it: forward is the hi-hat, left the kick, right the snare,
back the open hat. The motion sensor sees each stroke, which way it went and how fast, and the drum sounds as the
stroke peaks, on the beat. There is always a beat, and every hit lands on it. Tap REC and make a sound: the sound
is a pad from then on, the next one round each time. Hold REC and play: a loop of whole bars; hold again for
another on top, four of them, each switched on and off. The drums are [WRECK](../wreck)'s kit, played and printed
like a record, or the four pads are the four quarters of a break (the Amen, the Think, Apache and five more), so
you swing the break together yourself. Next to another board on the radio link (a [BAND](../band), a
[MANTRA](../mantra), a [HUFF](../huff), a [WRECK 2](../wreck2), the Samplooler, another AIR in the other hand)
everything sits on the shared beat.

Firmware for the Samplooler Stack (ESP32-S3). Built on 2026-10-21 and swung on a board on 2026-10-22, which is
where the stroke detector got its reset rule and its thresholds. Reworked for beginners on 2026-10-23 (the beat
that is always there, the loops in bars, the tap that samples, CONFIRM, SWING SETUP), checked in the host
simulator (`tools/sim`, 109 checks) and not yet swung on a board in that form.

## Playing

1. Switch it on and swing the box like a drumstick. Forward: hi-hat. Left: kick. Right: snare. Back: open hat.
   Swing harder for louder. Every hit lands on the nearest sixteenth of the beat (LOOPS > TEMPO; the drummer on
   the screen nods on it). The compass on the right shows the stroke it saw.
2. Turn the knob for another kit. AMEN, THINK, APACHE, FUNKY, BOOM BAP BREAK, DOOM and GABBA put the four
   quarters of a break on the four pads: swing them in order and you have the break, out of order and you have
   your own. BREAK > PRINT makes it tape, vinyl, a 12-bit sampler or dust.
3. Tap REC and make a sound at the mic (say it, clap it, hum it, bang something). It is the FRONT pad from then
   on, and after a power cycle too. Tap again for the RIGHT pad, then BACK, then LEFT: four taps, your own kit.
4. Hold REC and play. The loop starts on the next bar and ends on the bar you let go in. Hold again and play:
   another loop on top. Up to four; push the stick towards one to switch it off and on; hold the knob to stop
   them all and to start them again. LOOPS > ERASE LAST or ERASE ALL to start over.
5. FEEL > SWING SETUP: swing forward four times and it learns how hard you swing.
6. Tip the bottom edge down and the drums tune up. FX has drive, crush, echo, room and grain.
7. Two boards, one in each hand: nothing to set. They find each other, share the beat, and with a break each
   hand holds a bar of it.

A swing in one direction is one hit: before the same pad sounds again the box has to come back the other way (10
degrees) or rest a moment. Another pad needs no reset, so a roll between two pads is free. A hit needs a jolt as
well as a turn (FEEL > CONFIRM), so carrying or tilting the box plays nothing. If strokes go to the wrong pad,
FEEL > SIDES swaps left and right and FEEL > FORWARD swaps front and back.

## Controls

| | |
| - | - |
| swing forward / left / right / back | hi-hat / kick / snare / open hat |
| swing harder | louder |
| tap REC, make a sound | the next pad is that sound |
| hold REC and play | a new loop, whole bars, on top of the others |
| hold the knob | every loop stops / goes |
| stick up / right / down / left | loop 1 / 2 / 3 / 4 on or off |
| turn the knob | next or previous kit |
| press the knob | the menu |
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| tilt (bottom edge down / up) | the drums tuned up / down |

## The loops

A loop is what you played while REC was held, cut to whole bars of the beat (one to eight) and played round. Each
hold is a new loop on top of the ones playing, four at most (a fifth records over the newest); each can be
switched off and on; the newest can be taken away. The loops are in beats, so LOOPS > TEMPO speeds them all up and
down together, and on the link they sit on the flock's beat. SWING makes the offbeats late, MUTATE drops and swaps
hits at random.

## The breaks

There is no recording of anyone else's break in AIR (the famous ones are under copyright). The breaks are WRECK's:
a synthesized kit playing the classic patterns, printed through a room and onto tape, vinyl or a sampler. Each is
cut into four equal parts, one per pad; BREAK > STRETCH plays the parts at the beat's speed, so a break recorded
at 136 BPM fits a beat of 120. AIR KIT is the same kit printing one drum per part. A new break or print is
rendered in the background for a few seconds (PRINT shows how far) while the old one plays on.

## Samples

A pad set to SAMPLE plays its own sound. The take starts at the first sound the mic hears and stops when the
sound has died away (or at PADS > LONGEST, 1 s at most). While it listens the drums are quiet, so the mic hears
only you. Tilt and TUNE play a sample faster or slower. PADS > FORGET ALL deletes the four samples and puts the
drums back. The samples live on the same flash partition as the Samplooler's slots, so the two firmwares keep
each other's files.

## Kits

AIR KIT (the printed drums), AMEN, THINK, APACHE, FUNKY, BOOM BAP BREAK, DOOM, GABBA (the breaks in four), TAPE
DRUMS, GRAIN STORM, SYNTH KIT (KNOCK's drum synth), 808 PARTY, TECHNO, HAND DRUM, COWBELL FEVER, ZAP GUN, TRASH
CAN, DUB, MY SAMPLES (all four pads are your samples), HALF MINE (hats from the kit, kick and snare yours). Eight
more places for your own, under SOUND > SAVE AS.

## MIDI (USB)

| Out | |
| - | - |
| notes on channel 10 | every hit, General MIDI drum notes (36 kick, 38 snare, 42 hat, 46 open hat, ...) with its velocity |
| CC 20 and up | every setting, when you change it (the link's switches excepted) |
| program change | the kit |

| In | |
| - | - |
| notes | play the pads (by their drum's GM note), on the grid |
| CC 20 and up, program change | settings, kits |
| CC 64 (sustain pedal) | REC: short samples, long loops |

## Building

ESP-IDF v5.5.1, as for the Samplooler (the core's files are copied in from `core/`, 2026-10-21):

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name over USB and flashes only that one
../tools/sim/run.sh        # the simulator: every check, screens, GIFs and sound in tools/sim/out/
```

The full manual is [docs/MANUAL.md](docs/MANUAL.md). All the screens: [images/screens.png](images/screens.png).
