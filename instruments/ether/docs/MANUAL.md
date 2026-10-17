# ETHER manual

ETHER listens to the Wi-Fi and the Bluetooth around it at the same time and plays both. This is every
page of the menu and what each setting does. The controls and the sounds are in the
[README](../README.md); the menu, the looper and SYSTEM work the same on all the Samplooler instruments
([CORE.md](CORE.md)).

## What it hears

**Wi-Fi (the ground).** Everything a Wi-Fi device sends is a frame. Routers send a beacon about ten times
a second; phones send probe requests when they look for networks; data frames carry what people are
doing; after almost every frame the receiver sends a short ACK. For many frames the radio also measures
how the signal arrived on each of its subcarriers (channel state information): the room's radio shape,
which changes when someone moves between the router and the box. Broken frames and a raised noise
floor mean interference: a microwave oven, a wireless camera, a crowded band. The Wi-Fi radio hears one
channel at a time; AUTO finds the busiest.

**Bluetooth (the air).** Phones, earbuds, watches, trackers, beacons, laptops and speakers advertise a few
times a second to say they are there. Each one becomes a bird. What kind of bird comes from what the
advertisement says the device is (a watch trills, a tracker pips, a beacon rings); how near it sounds
comes from its signal strength. Bluetooth advertises on three channels: the three wires.

**Both at once.** There is one antenna. ESP-IDF's coexistence takes turns between the two radios; BT TIME
sets how much of the time the Bluetooth scan gets. Each sender, on either radio, is known only by a
number made from its address and a random value that changes every time ETHER starts.

## MIX

| Setting | Does |
| - | - |
| MODE | LISTEN plays everything. HUNT follows one device from either radio: a beep that gets faster and higher as its signal gets stronger, the rest turned down; a Wi-Fi target fixes the Wi-Fi channel on its own. |
| BALANCE | Minus towards the Wi-Fi, plus towards the Bluetooth. At either end the other world is silent and greyed out. The joystick (left / right) adds to it. |
| PACKETS | Level of the Wi-Fi notes (with the ACK ticks and the rain). |
| ROOM | Level of the chord from the room's shape. |
| ROOM SENS | How small a movement the chord answers. |
| STORM | Level of the interference: crackles for broken frames, a rumble for a raised noise floor, thunder when it jumps. |
| BIRDS | Level of the Bluetooth birds. |
| DENSITY | The most notes a second, both worlds together. BALANCE shares it between them; what one side does not use, the other may. Wi-Fi frames over the budget become raindrops. |
| REACH | Devices on either radio fainter than this are left out. |
| NEXT TARGET | In HUNT: follow the next device, loudest first, whichever radio it is on. |

## TONE

| Setting | Does |
| - | - |
| SCALE | One scale for both worlds. |
| KEY | Its root. A MIDI note in sets it too. |
| REGISTER | Where the Wi-Fi notes start; the birds sing an octave above (trackers and watches higher still, laptops and speakers lower). |
| SPREAD | How many octaves the devices' notes cover. |
| TRANSPOSE | Everything up or down, in semitones. |
| WI-FI VOICE | AUTO gives each kind of frame its own sound: data plucks, beacons ring as bells, probe requests drop, other management frames are kalimba tines. Or one for all: PLUCK, BELL, KALIMBA, DROP, BEEP (as long as the frame), STATIC. |
| BIRD VOICE | BY KIND gives each kind of device its own call: phones chirp, earbuds whistle, watches trill, trackers pip, beacons ring, laptops warble, speakers coo. Or one call for all, or MIXED. |
| RING | How long a Wi-Fi note rings. |
| BRIGHT | Dark to sharp, for both. |

## BIRDS

| Setting | Does |
| - | - |
| FLOCK | CHORUS: birds sing in short phrases and rest, as birds do. CLOCKS: each bird pulses at its own advertising interval (slowed down), a polyrhythm; on the GRID when it is not FREE. SWARM: every advert is a short grain, the crowd as a flutter (on screen, a murmuration). |
| CALL LENGTH | Longer or shorter calls. |
| REST | The pause between phrases (CHORUS). |
| PHRASE | The most calls in a row (CHORUS). |
| SLOW DOWN | CLOCKS: how much slower than its real advertising interval each bird pulses. |
| MAX BIRDS | Only the nearest this many sit on the wires. |

## RHYTHM

| Setting | Does |
| - | - |
| GRID | FREE plays each frame and call when it arrived (20 ms late, keeping their spacing). The others snap both worlds to a beat: 1/4 to 1/32, and triplets; CLOCKS pulses lock to whole steps. |
| TEMPO | For the grid and the echo. |
| SWING | Every other step a little late. |
| PER STEP | The most notes on one step, both worlds together. |

## SPACE

One hall and one echo for both worlds.

| Setting | Does |
| - | - |
| REVERB | How much hall. |
| SIZE | A small room to a huge hall. |
| DISTANCE | How much further away weak devices sound, on either radio. |
| ECHO | A repeat in time with the tempo. |
| ECHO TIME | From 1/16 of a bar to half a bar (never longer than a second). |
| ECHO REPEAT | How many times it comes back. |

## CONTROL

WI-FI CHAN, HOP TIME and BT TIME belong to the device, not to the sound: changing sounds leaves them.

| Setting | Does |
| - | - |
| WI-FI CHAN | AUTO (the busiest), HOP (all of them in turn), or one of 1 to 13. |
| HOP TIME | How long HOP stays on each channel. |
| BT TIME | How much of the radio's time the Bluetooth scan gets: 25 %, 50 % or 75 %. More hears more birds, and fewer Wi-Fi frames. |
| BEACONS | Whether routers' beacons sound. |
| TILT | Bottom edge down: RING (longer Wi-Fi notes and bird calls), PITCH (an octave each way), REACH (hear only what is near), or OFF. |
| LEAN | Left and right: SCALE WALK, BRIGHT, ECHO (lean right), or OFF. |
| STICK X | Joystick left and right: BALANCE, BRIGHT, SCALE WALK, or OFF. In HUNT a flick picks the next device. |
| STICK Y | Joystick up and down: REACH (up: only what is near), DENSITY, ROOM, or OFF. |

Holding the knob holds the tilt where it is (a padlock in the header).

## MIDI CC

Every setting both ways, on the MIDI channel set in SYSTEM:

| CC | | CC | | CC | |
| - | - | - | - | - | - |
| 20 | MODE | 35 | WI-FI VOICE | 49 | REVERB |
| 21 | BALANCE | 36 | BIRD VOICE | 50 | SIZE |
| 22 | PACKETS | 37 | RING | 51 | DISTANCE |
| 23 | ROOM | 38 | BRIGHT | 52 | ECHO |
| 24 | ROOM SENS | 39 | FLOCK | 53 | ECHO TIME |
| 25 | STORM | 40 | CALL LENGTH | 54 | ECHO REPEAT |
| 26 | BIRDS | 41 | REST | 55 | WI-FI CHAN |
| 27 | DENSITY | 42 | PHRASE | 56 | HOP TIME |
| 28 | REACH | 43 | SLOW DOWN | 57 | BT TIME |
| 30 | SCALE | 44 | MAX BIRDS | 58 | BEACONS |
| 31 | KEY | 45 | GRID | 59 | TILT |
| 32 | REGISTER | 46 | TEMPO | 60 | LEAN |
| 33 | SPREAD | 47 | SWING | 61 | STICK X |
| 34 | TRANSPOSE | 48 | PER STEP | 62 | STICK Y |

Out only: Wi-Fi notes on the MIDI channel, bird calls on the next one; CC 3 Wi-Fi frames a second (log
scale), 4 room movement, 5 storm, 6 HUNT nearness, 8 birds (x 5), 9 bird calls a second; CC 12/13 tilt,
14/15 joystick (SYSTEM > MIDI CC OUT).

## Tips

- Start with BALANCE in the middle and push the joystick slowly from side to side: you hear which sounds
  are which.
- In a quiet place, raise BT TIME for more birds; in a busy office, lower DENSITY or raise REACH.
- MUSIC BOX and CLOCK SHOP sound best with the box still: the birds' own timing makes the rhythm.
- HUNT starts with the loudest device of either radio. Bluetooth signals jump more than Wi-Fi's: walk
  slowly and wait for the needle.
- The LED: green and blue for Wi-Fi, each kind of bird its own colour, red flashes in a storm; in HUNT it
  blinks with the beeps.
