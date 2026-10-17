# CHIRP manual

## What you see

- **The header** (the yellow band): CHIRP, or the looper's state (REC, LOOP, ADD, STOP) with its
  length; the sound's name (a * once you have changed something); a padlock while the tilt is held;
  the output level.
- **Three wires**, one per Bluetooth advertising channel. A bird for each device heard, bigger the
  nearer it is. When it calls it opens its beak, a note floats up and the wire wobbles under it.
  New devices fly in; silent ones fly off.
- **The bottom line**: how many birds, how many calls a second, the mode.
- **CLOCKS**: a small bar over each bird fills up to its next pulse.
- **SWARM**: the birds leave the wires and fly as a flock that grows with the traffic.
- **HUNT**: one big bird (the one you picked with the stick, or else the nearest), what kind of device
  it is, its signal in dBm, NEARER / FARTHER as it changes, and a radar whose rings light up as you get
  closer.
- With nothing to hear: LISTENING FOR DEVICES, then NO DEVICES NEAR YET. If Bluetooth did not start:
  BLUETOOTH WOULD NOT START.

## Controls

| | Playing | In the menu |
| - | - | - |
| turn | the next or previous sound; after you edit a setting in the menu, the knob turns that setting instead (choose SOUND in the menu to go back to sounds) | move, or change the value |
| press | open the menu | open a page, edit, run; press again to go back up |
| hold the knob | hold the tilt (padlock) | back to playing |
| REC | hold: record a loop (or add to it); tap: stop / play | the same |
| tilt, bottom edge down | longer calls (TILT DOES: LENGTH) or faster birds (SPEED) | |
| lean left / right | birds up or down the scale (LEAN SHIFT steps at a full lean) | |
| stick left / right | spotlight the next bird to the left or right | |
| stick up / down | brighter / darker (STICK Y: TONE) or more / fewer calls (DENSITY) | |

REC held from power-on for 3 s: everything back to new (the tilt setup stays).

## The menu

### SOUND
- **SOUND**: choose a sound; the knob plays them as you turn.
- **SAVE AS**: keep the current settings as USER 1-8 (named after the sound they started from).
- **REVERT**: back to the sound as it was saved.

### BIRDS: what they sing
- **MODE**: CHORUS, CLOCKS, SWARM or HUNT (see the README).
- **SCALE**, **KEY**: the notes the birds sing. Every device gets a note from its hash, so a device
  keeps its note while it stays.
- **OCTAVE**: the register of the lowest bird. Small things (watches, trackers) sing an octave higher,
  big things (laptops, speakers) an octave lower.
- **SPREAD**: how many octaves the birds spread over.
- **VOICES**: BY KIND gives each kind of device its own call (the table in the README); or one call for
  all (CHIRP, WHISTLE, TRILL, WARBLE, PIP, COO, BELL, DROP); MIXED picks one per device at random.
- **CALL LENGTH**: longer or shorter calls (x0.25 to x4).
- **SWEEP**: how far the calls glide. 0 % makes plain notes.
- **BRIGHT**: thin and pure, or reedy.

### CROWD: how devices become calls
- **CALLS/SEC**: the most calls a second, all birds together. Busy rooms are thinned to this.
- **REST**: a bird's pause after a phrase.
- **PHRASE**: the most calls a bird makes in a row before it rests.
- **NEAR AT**, **FAR AT**: the signal strengths (dBm) that count as right here and as far away. Between
  them, birds get quieter, duller and more distant. Set them to the room: a phone in your hand reads
  around -40, one across a house around -90.
- **FORGET**: a device silent this long flies off.
- **HELLO/BYE**: how loud the arrival and goodbye calls are (0: none).
- **MAX BIRDS**: only the nearest this many sing (up to 24).

### CLOCKS: the rhythm machine
- **SLOW DOWN**: each bird pulses at its device's interval times this. Devices send every 20 ms to a few
  seconds; at x4 a phone at 200 ms becomes a pulse every 0.8 s.
- **QUANTIZE**: ON snaps every pulse to the tempo grid: each bird takes the nearest of 1, 2, 3, 4, 6, 8,
  12... steps, so the polyrhythm locks into a groove.
- **TEMPO**, **GRID** (1/32 to 1/4), **SWING** (late offbeats).
- **PULSE LEN**: a pulse is this share of a full call.

### SPACE
- **REVERB**, **ROOM** (how long it rings), **DISTANCE** (how much quieter, duller and wetter far
  birds are), **AIR** (a wind that grows with the traffic), **TONE** (a low-pass filter on everything).

### CONTROL
- **TILT DOES**: LENGTH, SPEED (CLOCKS faster, CHORUS rests shorter) or OFF.
- **LEAN SHIFT**: scale steps at a full lean (0: off).
- **STICK Y**: TONE, DENSITY or OFF.
- **SPOTLIGHT**: how much louder the chosen bird is.
- **NEXT BIRD**: spotlight the next bird (for boards without a stick).
- **MIDI SPLIT**: ONE CHANNEL, or BY KIND (a channel per kind of device).
- **MIDI KEYS**: notes coming in play calls.

### LOOP and SYSTEM
The same on every instrument of the family: loop level and fade, clear; volume, mic gain (unused by
CHIRP), tilt setup, motion on/off, the joystick and its orientation, MIDI channel and CC out, the LED,
hints, the screen saver, ABOUT (version, CPU, memory) and RESET ALL.

## Tips

- In HUNT, pick the bird with the stick first, then walk. Bluetooth signals bounce off walls and bodies,
  so move slowly and turn around: the trend settles in a second or two.
- CLOCKS with QUANTIZE on, a slow tempo and BELL or DROP voices makes a steady piece out of a busy
  office.
- Record a minute of CHORUS with REC, then switch to SWARM and play over it.
- The LED flashes in the colour of the kind of device that called: yellow phone, cyan earbuds, green
  watch, magenta tracker, blue beacon, white laptop, orange speaker.
