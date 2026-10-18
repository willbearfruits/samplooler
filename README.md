# Samplooler

[![build](https://github.com/willbearfruits/samplooler/actions/workflows/build.yml/badge.svg)](https://github.com/willbearfruits/samplooler/actions/workflows/build.yml)

A small looper you play by tilting it. Hold the button and make a sound; let go and it loops. Tip
the box forward and the loop runs faster, tip it back and it runs backwards, roll it sideways for a
tape delay or a stutter.

![The Samplooler screen: a tape between two reels with a stick-figure operator running on it, forwards, backwards, with delay echoes, stuttering, juggling grains and flung off its feet at 8x](media/tour.gif)

> PHOTO: the Stack in a hand, tipped forward, screen lit (media/photos/stack-hand.jpg)

**[Flash it from the browser](https://willbearfruits.github.io/samplooler/)** ·
[Manual](docs/MANUAL.md) · [Build one](docs/BUILD.md) ·
[Video](https://github.com/willbearfruits/samplooler/releases/download/media/samplooler-showcase-full.mp4) ·
[Get one](#get-one)

The loop is a tape between two reels, and a small operator runs on it, as fast as you tip the box. The
video is rendered, not filmed: the sound is the firmware's own playback engine running on a computer, and
the screen is the firmware's own drawing code.

> VIDEO: someone playing a Stack, filmed, with its sound, about 60 s

## What it does

| | |
| - | - |
| Record | Hold the button and make a sound, up to 20 s at 32 kHz. Let go and it loops. |
| Tilt | Forward plays faster, back plays backwards. Snap rounds the speed to octaves and fifths. |
| Roll | Left adds a tape delay, right a stutter. |
| Modes | TAPE (speed and pitch move together) and GRAIN (they move apart). |
| Voice | Optional: SING's 18 voices on the input before it records (autotune, harmonies, choir, vocoder, echo). |
| Speaker | Can be switched off, so only the line out plays, when the speaker feeds back into the mic. |
| Slots | 8 loops saved on the board, kept when you update it. |
| With a computer | USB MIDI and a USB sound card on one cable, and a VST3 plugin that shows and sets everything. |
| Other instruments | Eleven more firmwares for the same box, from a singing voice processor to a drum machine you knock on. |

## Eleven more instruments

The same Stack can be flashed with SING, BAND, WRECK, MANTRA, ETHER, JAM, KNOCK, HUFF, HOWL, SNIFF or CHIRP, and
flashed back. The list, what each one does and how far it is tested: [instruments/](instruments/).
Anyone can add one ([CONTRIBUTING.md](CONTRIBUTING.md)).

## Get one

- **Buy one**: built by hand in Haifa. Write and ask about price and shipping:
  [Dogme84@gmail.com](mailto:Dogme84@gmail.com?subject=Samplooler) ·
  [WhatsApp](https://wa.me/972539263808).
- **Build it in the workshop**: the [ESP32 workshop](https://willbearfruits.github.io/serious-shit/workshop-esp32.html),
  7 meetings at MEZO, Haifa, 2,200 ₪, kit included.
- **Build it yourself**: order the boards, solder the modules on, flash it: [docs/BUILD.md](docs/BUILD.md).

## Status

Early. The current Stack is rev C: it has been made and runs the firmware (screen, microphone, tilt,
line out and USB MIDI/audio work). The board files here are rev D, which is being tested now (October
2026); rev C's files are at the git tag `checkpoint/boards-rev-c`. Of the ten other instruments, four
have run on a board and six only in their simulators. Reports of builds, good or bad, are welcome in the
issues.

## What is where

| Path | What |
| - | - |
| [docs/](docs/) | the manual and the build guide |
| [firmware/](firmware/) | the Samplooler's ESP-IDF 5.5 project |
| [instruments/](instruments/) | the other instruments, each a complete ESP-IDF project |
| [core/](core/) | the code the instruments share |
| [hardware/](hardware/) | the Stack's schematics, boards and JLCPCB order files |
| [plugin/](plugin/) | the VST3 and standalone plugin (JUCE) |
| [media/](media/) | pictures and screen grabs |
| [site/](site/) | the project page and the browser flasher |
| [tools/](tools/) | `flash.sh`, `sl_midi.py` (log and MIDI from a shell), `release.sh`, `library.py` |

## Licence

- Firmware, instruments and tools: GPL-3.0 ([LICENSE](LICENSE)).
- Plugin: AGPL-3.0, as JUCE requires ([plugin/LICENSE](plugin/LICENSE)).
- Circuit boards, docs and pictures: CC BY-SA 4.0 ([hardware/LICENSE](hardware/LICENSE)).

---

Part of [Serious S.H.I.T.](https://willbearfruits.github.io/): instruments, tools and workshops made in Haifa by
Yaniv Schonfeld (Willbear). If Samplooler is useful to you, [support it on Patreon](https://www.patreon.com/Seriousshit).
