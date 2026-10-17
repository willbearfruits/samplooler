# SNIFF manual

SNIFF listens to the 2.4 GHz Wi-Fi around it and plays it. This is every page of the menu and what
each setting does. The controls and the sounds are in the [README](../README.md); the menu, the
looper and SYSTEM work the same on all the Samplooler instruments ([CORE.md](CORE.md)).

## What it hears

- **Frames.** Everything a Wi-Fi device sends is a frame. Routers send a beacon about ten times a
  second ("I am here"). Phones send probe requests when they look for networks. Data frames carry
  whatever people are doing. After almost every frame the receiver sends a short ACK. Each sender
  gets its own note from a number made from its address (new every time SNIFF starts).
- **The room.** For many frames the radio also measures how the signal arrived on each of its 52
  subcarriers (channel state information, CSI). Walls and furniture shape it; a person moving
  between the sender and SNIFF changes it. SNIFF follows the sender with the most such frames on the
  channel (usually your router) and plays that shape as a chord whose notes swell where it changes.
- **Interference.** Frames that arrive broken, and the radio's noise floor. A microwave oven (channels
  5 to 11 especially), a wireless camera or a packed room raise both.

The radio hears one channel at a time. AUTO (the default) listens to all 13 at boot, picks the busiest
and looks again if it goes quiet for 15 s or after 5 minutes.

## EARS

| Setting | Does |
| - | - |
| MODE | LISTEN plays everything. HUNT follows one device: a beep that gets faster and higher as its signal gets stronger, the other layers turned down, the radio fixed on its channel. |
| PACKETS | Level of the notes from frames (with the ticks and the rain). |
| ROOM | Level of the chord from the room's shape. A quiet bed even when nothing moves. |
| ROOM SENS | How small a movement the chord answers. Turn up in a big room, down if it never settles. |
| STORM | Level of the interference: crackles for broken frames, a rumble for a raised noise floor, thunder when it jumps. |
| ACK TICKS | Level of the clicks for ACKs (at most six a tick, so a busy channel does not become a buzz). |
| REACH | Devices whose signal is weaker than this are left out. -92 dBm hears nearly everything; -60 only the next room. |
| DENSITY | The most notes a second. Frames over the budget become raindrops instead, so a crowd stays musical. |
| NEXT TARGET | In HUNT: follow the next device, loudest first. The joystick does the same with a flick left or right. |

## TONE

| Setting | Does |
| - | - |
| VOICE | AUTO gives each kind of frame its own sound: data frames pluck, beacons ring as bells an octave up, probe requests chirp, other management frames are kalimba tines. Or one voice for all: PLUCK, BELL, KALIMBA, DROP, CHIRP, BEEP (as long as the frame), STATIC. |
| SCALE | The notes devices may take. FREE: any pitch at all. |
| KEY | The scale's root. A MIDI note in sets it too. |
| REGISTER | LOW, MID, HIGH, AIR: where the devices' notes start. |
| SPREAD | How many octaves the devices' notes cover. |
| TRANSPOSE | Everything up or down, in semitones. |
| RING | How long a note rings (for BEEP: how long it lasts, with BY LENGTH). |
| BY LENGTH | How much a frame's length stretches its ring: long frames ring longer. |
| BRIGHT | Dark to sharp: the strike and a filter on the notes. |

## RHYTHM

| Setting | Does |
| - | - |
| GRID | FREE plays each frame when it arrived (20 ms late, keeping their own spacing). The others snap notes to a beat: 1/4 to 1/32, and triplets. |
| TEMPO | For the grid and the echo. |
| SWING | Every other step a little late. |
| PER STEP | The most notes on one step of the grid; the rest become rain. |

## SPACE

| Setting | Does |
| - | - |
| REVERB | How much hall. |
| SIZE | A small room to a huge hall. |
| DISTANCE | How much further away weak devices sound (more reverb, less direct sound). |
| ECHO | A repeat in time with the tempo. |
| ECHO TIME | From 1/16 of a bar to half a bar at the tempo (never longer than a second). |
| ECHO REPEAT | How many times it comes back. |

## RADIO

These belong to the device, not to the sound: changing sounds leaves them as they are.

| Setting | Does |
| - | - |
| CHANNEL | AUTO (the busiest), HOP (all of them in turn), or one of 1 to 13. HUNT overrides it while it follows a device. |
| HOP TIME | How long HOP stays on each channel. |
| BEACONS | Whether routers' beacons sound. Off for a calmer sound in a block of flats. |
| BROKEN | Whether broken frames are listened to (they feed the storm). |

## MOTION

| Setting | Does |
| - | - |
| TILT | Bottom edge down: RING (notes up to about six times longer; the other way, shorter), PITCH (an octave each way), REACH (hear only what is near), or OFF. |
| LEAN | Left and right: SCALE WALK (the notes move up and down the scale), BRIGHT, ECHO (lean right), or OFF. |
| STICK X | Joystick left and right: BRIGHT, SCALE WALK, REVERB, or OFF. In HUNT a flick picks the next device. |
| STICK Y | Joystick up and down: REACH (up: only what is near), DENSITY, ROOM, or OFF. |

Holding the knob holds the tilt where it is (a padlock in the header), so you can set the box down.

## MIDI CC

Every setting both ways, on the MIDI channel set in SYSTEM:

| CC | | CC | | CC | |
| - | - | - | - | - | - |
| 20 | MODE | 32 | REGISTER | 44 | DISTANCE |
| 21 | PACKETS | 33 | SPREAD | 45 | ECHO |
| 22 | ROOM | 34 | TRANSPOSE | 46 | ECHO TIME |
| 23 | ROOM SENS | 35 | RING | 47 | ECHO REPEAT |
| 24 | STORM | 36 | BY LENGTH | 48 | CHANNEL |
| 25 | ACK TICKS | 37 | BRIGHT | 49 | HOP TIME |
| 26 | REACH | 38 | GRID | 50 | BEACONS |
| 27 | DENSITY | 39 | TEMPO | 51 | BROKEN |
| 29 | VOICE | 40 | SWING | 52 | TILT |
| 30 | SCALE | 41 | PER STEP | 53 | LEAN |
| 31 | KEY | 42 | REVERB | 54 | STICK X |
| | | 43 | SIZE | 55 | STICK Y |

Out only: notes (each note that sounds, 80 ms long), CC 3 frames a second (log scale), CC 4 room
movement, CC 5 storm, CC 6 HUNT nearness; CC 12/13 tilt, 14/15 joystick (SYSTEM > MIDI CC OUT).

## Tips

- In a quiet house, try HOP or a lower REACH; in a busy office, raise REACH or lower DENSITY.
- GHOSTS with the box near the router and someone walking across the room is the clearest way to
  hear the room move. Movement close to the line between SNIFF and the router changes it most.
- HUNT looks for the loudest device first. A flick of the joystick (or EARS > NEXT TARGET) moves to
  the next one. Hold the box at arm's length and turn slowly: your body blocks the signal too.
- The LED: blue for beacons, green for data, yellow for searching phones, red flashes in a storm, a
  slow blue glow when the room moves; in HUNT it blinks with the beeps.
