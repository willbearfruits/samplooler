# Instruments

Other instruments for the [Samplooler](../README.md) Stack. Each one is its own firmware. Flash one, play it,
and flash the Samplooler back the same way. Anyone can add an instrument.

<!-- list -->
| | What | Status |
| - | - | - |
| [BAND](band) | Six players on one board: drums you knock or draw on a grid, bass, lead, keys, ocarina and tuned feedback, in one key and tempo. Syncs with a SING. | Runs on a board |
| [CHIRP](chirp) | The Bluetooth devices around you, played as birds. Phones, earbuds and watches sing on three wires. | Simulator only |
| [ETHER](ether) | SNIFF and CHIRP at once: the Wi-Fi on the ground, the Bluetooth in the air, in one key and one tempo. | Runs on a board |
| [HOWL](howl) | Feedback in tune. Only the notes of a chord can build up into a howl. Tip the box to bring it in, lean to pick the note. | Simulator only |
| [HUFF](huff) | Blow at the mic and tilt the box for the notes. Ocarina, flute, pan pipe, shakuhachi, clarinet, tin whistle and a blown bottle; links to the other boards. | Simulator only |
| [JAM](jam) | Drums, bass, breath and howl, looped on top of each other in one key and one tempo. Turn the knob to pick the one you play. | Runs on a board |
| [KNOCK](knock) | Knock on the box to play drums. Each side is a different drum. Hold REC while you knock and it loops. | Simulator only |
| [MANTRA](mantra) | A formant voice and a seated Buddha who sings it: monks chanting OM, overtone singing, Daisy Bell, and a wall of amps behind him. Tilt for the vowels, turn it for the overtones. | Runs on a board |
| [SING](sing) | Sing and it tunes you, sings harmonies and a choir with you, or turns you into a vocoder. Hold REC to loop it; tilt to play the loop faster or backwards. Syncs with a BAND. | Runs on a board |
| [SNIFF](sniff) | The Wi-Fi around you as sound. Every device gets a note and plays it each time it sends something. HUNT finds one by ear. | Simulator only |
| [WRECK](wreck) | A breakcore machine that turns into a doom machine: one knob from a chopped, rolled, crushed break at twice the tempo down to one chord held under a break slowed into thunder. | Simulator only |
<!-- /list -->

"Runs on a board" means it has started and kept up on real hardware but has not been played much yet.
"Simulator only" means it has been checked in its simulator on a computer and not yet tried on a board.

## Flashing

- **From the [page](https://willbearfruits.github.io/samplooler/):** Chrome or Edge on a computer. Hold BOOT on the board while you plug it in, then press
  Flash under the instrument.
- **From a folder:** with ESP-IDF 5.5.1, `cd instruments/<name>/firmware && idf.py build`, then
  `tools/flash.sh` from the instrument's folder. It finds a board that runs the Samplooler or one of these
  over USB and flashes it; for anything else, hold BOOT while plugging the board in first.

Both write only the bootloader, the partition table and the program. The Samplooler's sample slots and
settings, and the sounds saved in each instrument, stay on the board.

## Adding one

[CONTRIBUTING.md](../CONTRIBUTING.md). In short: a folder in `instruments/` with an `entry.json`, as a pull
request or through an issue. GitHub checks it, builds it and runs its simulator; once it is merged it is on
the page.

## What is where

| | |
| - | - |
| `instruments/<name>/` | one instrument: a complete ESP-IDF project that builds on its own, with its README, manual, pictures and simulator |
| [`core/`](../core/) | the shared core as it is now: the menu, sounds, looper, MIDI, drivers and the simulator's frame ([core/docs/CORE.md](../core/docs/CORE.md)) |
| [`tools/library.py`](../tools/library.py) | checks the entries and compares them with the core |

## The core

Every instrument carries its own copy of the core, so each folder builds and ships on its own. `core/` is the
current version and `tools/core_files.txt` lists its files. `python3 tools/library.py core` shows which
entries have it unchanged, and `python3 tools/library.py core-copy <name>` brings one up to date. A change to
the core goes into `core/` first, then into the entries, each one rebuilt with its simulator run.

## Licence

Code: GPL-3.0 ([LICENSE](LICENSE)), as the Samplooler's. Pictures and text: CC BY-SA 4.0.
