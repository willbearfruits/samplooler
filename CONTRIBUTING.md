# Adding an instrument

Anything that runs on the Samplooler's ESP32-S3 boards (the Stack, the Mini, the breadboard kit) and builds
with ESP-IDF 5.5.1 can go in: an instrument, a game, a tool.

## Two ways to send it

1. **A pull request** with one new folder, `instruments/<name>/`. GitHub checks it, builds it and runs its
   simulator. Once it is merged, it is on the page with a Flash button.
2. **An issue**, if you don't use git: [Add an instrument](https://github.com/willbearfruits/samplooler-library/issues/new?template=add-instrument.yml),
   with a link to your code or a zip. It gets added for you.

## Start from the one closest to your idea

| Copy | If yours |
| - | - |
| [KNOCK](instruments/knock) | reacts to hits on the box, loops events (notes), not audio |
| [HUFF](instruments/huff) | uses the mic as a sensor (breath), plays a physical model |
| [HOWL](instruments/howl) | runs the mic through filters back out of the speaker |
| [SNIFF](instruments/sniff) | listens to the Wi-Fi radio |
| [CHIRP](instruments/chirp) | listens to Bluetooth LE |
| [ETHER](instruments/ether) | uses both radios at once |
| [JAM](instruments/jam) | has several instruments as layers sharing a tempo and key |
| [BAND](instruments/band) | plays with other boards (the link), or needs an editor of its own on the knob (the drum grid) |
| [SING](instruments/sing) | processes the voice (pitch, harmonies, vocoder) or loops on the song's bars |

They share a core that already does the menu, the link between boards (tempo, beat, loop length and chords over
ESP-NOW, for instruments that call it), the sounds (presets, user sounds, MY EDIT), the looper on REC,
the tilt setup, MIDI in and out, the master limiter, the screen's header band and hints, crash notes and a
simulator. Read `docs/CORE.md` in your copy: it says what the core does and what the instrument provides.

Then:

1. Rename it: the folder (lower case), `project()` in `firmware/CMakeLists.txt`, and in
   `firmware/main/CMakeLists.txt` the `INSTRUMENT_NAME` (capitals, at most 12 characters), a new
   `INSTRUMENT_PID` (`python3 tools/library.py check` prints the next free one) and a two-letter
   `INSTRUMENT_SERIAL`. Rename `firmware/main/<old>_iram.lf` too.
2. Write the instrument in `firmware/main/instrument.c` and any `inst_*.c` next to it. List the files your
   audio runs through in the `_iram.lf` fragment: on the board, audio code run from flash costs several times
   more CPU (see "The audio rules" in `docs/CORE.md`).
3. Keep the simulator going: `tools/sim/run.sh` lists your files, `tools/sim/test_inst.c` holds your own
   checks. `tools/sim/run.sh` runs the core's checks on your instrument as well (every setting for clicks,
   levels, the menu) and writes the screens as GIFs and the sound as WAVs in `tools/sim/out/`.
4. Put the GIFs you like in `images/`, write `README.md` for players (and `docs/MANUAL.md` if there are many
   settings), and `entry.json`.

## entry.json

```json
{
  "name": "KNOCK",
  "summary": "Knock on the box to play drums. Each side is a different drum.",
  "author": "your name or GitHub name",
  "licence": "GPL-3.0",
  "version": "0.1.0",
  "chip": "esp32s3",
  "uses": ["mic", "tilt"],
  "hardware_note": "optional: what it needs beyond the basic board",
  "status": "simulator",
  "status_note": "optional: one line about where it has run",
  "image": "images/play.gif",
  "more": ["images/splash.gif"],
  "added": "2026-10-14"
}
```

| Field | |
| - | - |
| `name` | as on the screen and on USB; the same as `INSTRUMENT_NAME` |
| `summary` | one or two plain sentences, at most 200 characters |
| `uses` | any of `mic`, `tilt`, `joystick`, `speaker`, `wifi`, `bluetooth` |
| `status` | `simulator` (only checked on a computer), `board` (starts and keeps up on a board), `played` (played on a board) |
| `image`, `more` | GIFs or pictures in your folder, 2 MB each at most; the first is the one on the page |

## Rules

- **Keep the partition table** (`firmware/partitions.csv`, the same as the core's). The Samplooler keeps its
  sample slots there, so flashing your instrument leaves them alone. The check fails otherwise.
- **The audio rules** in `docs/CORE.md`: nothing that can wait or allocate in the audio task, every control
  smoothed, no clicks, peaks around -12 dBFS, the master limiter left in.
- **Say where it has run.** `status` is what you have seen, not what you expect.
- **Plain words** in the README: what it does, how to play it, what to try first.
- **Leave the core files alone** if you can. If the core needs a change, say so in the pull request: it goes
  into `core/` and every instrument, not only yours.
- **Licence:** the code is GPL-3.0 (the core is), pictures and text CC BY-SA 4.0. Send only what you wrote or
  may share.

## What GitHub runs, and how to run it yourself

```sh
python3 tools/library.py check                        # the entries: fields, names, USB IDs, pictures, partition table
cd instruments/<name>/firmware && idf.py build        # ESP-IDF 5.5.1
instruments/<name>/tools/sim/run.sh                   # the simulator (gcc, Python 3 with Pillow)
python3 tools/library.py site --local                 # the page, with your local builds, in _site/
```

USB product IDs so far (all with Espressif's vendor ID 0x303A, picked here, not yet registered at
[espressif/usb-pids](https://github.com/espressif/usb-pids)):

| PID | |
| - | - |
| 0x4020 | Samplooler |
| 0x4021 | KNOCK |
| 0x4022 | HUFF |
| 0x4023 | HOWL |
| 0x4024 | SNIFF |
| 0x4025 | CHIRP |
| 0x4026 | ETHER |
| 0x4027 | JAM |
| 0x4028 | BAND |
| 0x4029 | SING |
