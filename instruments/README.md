# Instruments

Other instruments for the [Samplooler](../README.md) Stack. Each one is its own firmware. Flash one, play it,
and flash the Samplooler back the same way. Anyone can add an instrument.

<!-- list -->
| | What | Status |
| - | - | - |
| [HUFF](huff) | Blow at the mic and tilt the box for the notes. Ocarina, flute, pan pipe, shakuhachi, clarinet, tin whistle and a blown bottle; links to the other boards. | Runs on a board |
| [MANTRA](mantra) | A formant voice and a seated Buddha who sings it: monks chanting OM, overtone singing, Daisy Bell, and a wall of amps behind him. Tilt for the vowels, turn it for the overtones. | Runs on a board |
| [WRECK](wreck) | A breakcore machine that turns into a doom machine: one knob from a chopped, rolled, crushed break at twice the tempo down to one chord held under a break slowed into thunder. | Runs on a board |

Also in this folder, still in progress and not on the list: [BAND](band), [CHIRP](chirp), [ETHER](ether), [HOWL](howl), [JAM](jam), [KNOCK](knock), [SING](sing), [SNIFF](sniff) ([ROADMAP.md](../ROADMAP.md)).
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
