# DWELL

A drone you live inside.

DWELL holds a chord of whole-number ratios on one low note, the way La Monte Young tunes the Dream House. Tilt the
box and you walk a lattice of those ratios: away from you up the fifths (3/2, 9/8, 27/16), towards you down them
(4/3, 16/9), roll it for the sevenths (7/4, 7/6, 21/16). The tones you pass stay behind you, up to six, the oldest
giving way. Nothing happens fast: a change takes twenty seconds to arrive (DRIFT), by a swell or a slide, as in
Eliane Radigue's music. Turn the knob and each tone becomes a cluster of copies a few cents apart, beating slowly,
louder and brighter, until it is a wall: Phill Niblock. Knock on the box and every tone is strummed. Sing one note
into it and the box stacks your own voice. Run a ROOM PASS and it plays itself through the speaker into the room,
records that, plays the recording, records again, until only the room's own resonances are left, which is what Alvin
Lucier did with his voice in 1969.

Eight sources: sines, a harmonium reed, a bowed string, a tanpura whose curved bridge buzzes so its overtones bloom
after each pluck, a shruti box with its bellows, a hurdy-gurdy with the wheel's rumble, your own tone, and the room.

Firmware for the Samplooler Stack (ESP32-S3). Version 0.1, built 2026-10-24, checked in the host simulator
(`tools/sim`, 156 checks). Not on a board yet. Its busiest case (six tones of eight copies each, the hall, the
difference tone) takes 11.7 microseconds per audio block on a computer, which should be about 40 % of the board's
processor. That is still to be measured on a board.

> VIDEO: DWELL on a table, tilted one step every half minute, the room pass.

![walking the lattice](images/dwell-walk.gif)

## Playing

1. Switch it on. Two tones: 1/1 and the same an octave under it (the root). The screen shows the lattice with 1/1 lit
   and a figure lying on the floor, listening.
2. Tip the box away from you, about ten degrees, and put it down. After a moment 3/2 lights, and over twenty seconds
   it swells in. 1/1 stays. Tip it further: 9/8. Roll it right: 21/16. The screen prints the ratio, how far it is
   from the piano's note in cents, and the beat rate.
3. Turn the knob up. The tones thicken into clusters, beating; at the top it is a wall.
4. Knock on the box: a strum through every tone, ringing out.
5. Hold REC and sing one steady note. Let go. The source turns to WAVE: that note, stacked on the lattice.
6. Press the knob, ROOM > ROOM PASS. Let it run (twelve passes of four seconds). The room sings back through the
   lattice afterwards (the source turns to ROOM). It needs the speaker on and a room; on headphones it stops by itself.
7. Tap REC: the drone rests. Tap again: it returns.

## Controls

| | |
| - | - |
| tip away / towards you | up / down the fifths |
| roll right / left | up / down the sevenths (the fifths' thirds with LIMIT 5; nothing with LIMIT 3) |
| turn the knob | DENSE: pure sines to a beating wall |
| press the knob | the menu: SOUND, DRONE, LATTICE, CLUSTER, SOURCE, STRUM, ROOM, SPACE, SONG, SYSTEM |
| press the knob twice | the knob turns the volume for a moment |
| hold the knob | the tilt stays where it is (a padlock shows); hold again to let go |
| knock on the box | a strum |
| hold REC | sing one note: it becomes the WAVE source |
| tap REC | the drone rests; tap again, it returns (and it stops a room pass) |
| stick up / down | the lead an octave up / down |
| stick left / right | the chord louder / the lead louder |

## Sounds

DREAM HOUSE (pure sines, 7-limit), NIBLOCK (a bowed wall, eight copies), ADNOS (a slow glide on a reed), IN A ROOM
(the room source: run ROOM PASS first), TANPURA, SHRUTI BOX, HURDY GURDY, STRUMMING (long strums on sines),
CONRAD (a bow in 5-limit), DAVACHI (a six-voice organ that slides), DEEP LISTENING (a reed in a long hall),
TEMPO BEATS (the clusters beat at the flock's tempo), YOUR TONE (the WAVE source), TARTINI (two tones and their
difference tone), PYTHAGORAS (3-limit). Eight more places for your own, under SOUND > SAVE AS.

## With other boards

On the radio link DWELL shares the tempo and the key with a BAND, a SING, the Samplooler or another DWELL. SONG >
FOLLOW: a chord played live on a linked board moves 1/1 to the nearest ratio of the lattice, at DRIFT. SONG > SPREAD:
with another DWELL in the room, each board sounds one tone of its own chord, so a few boards around a room are one
chord you can walk through. CLUSTER > BEAT SYNC: the copies beat at the tempo (120 BPM is 2 Hz).

![every screen](images/screens.png)

## Build

ESP-IDF v5.5.1:

```sh
. ~/esp/esp-idf/export.sh
cd firmware && idf.py build
../tools/flash.sh          # finds the board by name, never touches a TWOFOLD
```

`tools/sim/run.sh` runs the simulator and writes screens, animations and sound to `tools/sim/out/`.
[docs/MANUAL.md](docs/MANUAL.md) has every setting.
