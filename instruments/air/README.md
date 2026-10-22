# AIR

Drum on nothing. Hold the box like a stick and swing it: forward is the hi-hat, left the kick, right the snare,
back the open hat. The motion sensor sees each stroke, which way it went and how fast, and the drum sounds as the
stroke peaks, pulled to the beat. The drums are [WRECK](../wreck)'s kit, played and printed like a record; or the
four pads are the four quarters of a break (the Amen, the Think, Apache and five more), so you swing the break
together yourself; or a pad is a sample of your own, made from the mic in a second and kept in flash. Hold REC
while you play and it loops. Next to another board on the radio link (a [BAND](../band), a [MANTRA](../mantra), a
[HUFF](../huff), a [WRECK 2](../wreck2), the Samplooler, another AIR in the other hand) the loop and the hits sit
on the shared beat.

Firmware for the Samplooler Stack (ESP32-S3). Built on 2026-10-21, checked in the host simulator (`tools/sim`,
93 checks) and swung on a board on 2026-10-22, which is where the stroke detector got its reset rule and its
thresholds. The breaks and the effects came after that and have not been on a board yet.

## Playing

1. Switch it on and swing the box like a drumstick. Forward: hi-hat. Left: kick. Right: snare. Back: open hat.
   Swing harder for louder. Hits land on the next sixteenth of the beat (FEEL > QUANTIZE). The drummer on the
   screen plays what you play; the compass on the right shows the stroke it saw.
2. Turn the knob for another kit. AMEN, THINK, APACHE, FUNKY, BOOM BAP BREAK, DOOM and GABBA put the four
   quarters of a break on the four pads: swing them in order and you have the break, out of order and you have
   your own. BREAK > PRINT makes it tape, vinyl, a 12-bit sampler or dust.
3. Hold REC and play. Let go: it loops. Hold REC again to add, tap REC to stop and to play again.
4. Hold the knob to sample a pad: hit the air for the pad you want, then make a sound at the mic (say it, clap
   it, hum it, bang something). The pad is that sound from then on, and after a power cycle too.
5. The stick: up a roll of the last drum, down a beat repeat, left and right a filter. Tip the bottom edge down and
   the drums tune up. BUS has drive, crush, echo, room and grain.
6. Two boards, one in each hand: LINK on both, LINK > HANDS LEFT on one and RIGHT on the other. They share the
   beat, and with a break each hand holds a bar of it.

A swing in one direction is one hit: before the same pad sounds again the box has to come back the other way (10
degrees) or rest a moment. Another pad needs no reset, so a roll between two pads is free. If strokes go to the
wrong pad, FEEL > SIDES swaps left and right and FEEL > FORWARD swaps front and back (which way the sensor reads a
swing depends on how the box sits in your hand). If small movements count as hits, lower FEEL > SENSE.

## Controls

| | |
| - | - |
| swing forward / left / right / back | hi-hat / kick / snare / open hat |
| swing harder | louder |
| turn the knob | next or previous kit |
| press the knob | the menu |
| press the knob twice | the knob turns the volume, until it rests 3 s or you press it again |
| hold the knob | sample a pad: hit the air for it, then make a sound |
| REC | hold: record or add to the loop; tap: stop or play it |
| stick up / down | a roll of the last drum / a repeat of the last slice |
| stick left / right | a filter: darker / thinner |
| tilt (bottom edge down / up) | the drums tuned up / down |

## The loop

What you play while REC is held loops when you let go, for as long as you held it. With a board on the link the
loop is cut to whole beats of the flock's tempo and starts on a beat; alone, your loop sets the tempo the others
follow. GRID pulls the hits towards a grid as they play, without changing what you played. MUTATE drops and
swaps hits at random. LOOP > UNDO LAST takes the last layer away; CLEAR LOOP erases it.

## The breaks

There is no recording of anyone else's break in AIR (the famous ones are under copyright). The breaks are WRECK's:
a synthesized kit playing the classic patterns, printed through a room and onto tape, vinyl or a sampler. Each is
cut into four equal parts, one per pad; BREAK > STRETCH plays the parts at the beat's speed (the loop's, the
flock's, or FEEL > TEMPO alone), so a break recorded at 136 BPM fits a 120 BPM flock. AIR KIT is the same kit
printing one drum per part. A new break or print is rendered in the background for a few seconds (PRINT shows how
far) while the old one plays on.

## Samples

A pad set to SAMPLE plays its own sound. The take starts at the first sound the mic hears and stops when the
sound has died away (or at SAMPLE > LONGEST, 2 s at most). While it listens the drums are quiet, so the mic hears
only you. Tilt and TUNE play a sample faster or slower. SAMPLE > FORGET ALL deletes the four samples and puts the
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
| notes | play the pads (by their drum's GM note) |
| CC 20 and up, program change | settings, kits |
| CC 64 (sustain pedal) | REC |

## Building

ESP-IDF v5.5.1, as for the Samplooler (the core's files are copied in from `core/`, 2026-10-21):

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name over USB and flashes only that one
../tools/sim/run.sh        # the simulator: every check, screens, GIFs and sound in tools/sim/out/
```

The full manual is [docs/MANUAL.md](docs/MANUAL.md). All the screens: [images/screens.png](images/screens.png).
