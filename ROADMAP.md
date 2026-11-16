# Roadmap

Where the Samplooler is in November 2026, and what comes next. Nothing here has a date.

## Now

- **The Stack.** Rev C boards are made and run the firmware. Rev D, the files in [hardware/](hardware/), has not been
  built.
- **The Samplooler firmware.** The looper, 18 voices on the input, 8 saved loops, USB MIDI and a USB sound card,
  and a radio link to the other boards: a take sets their tempo or lands on their beat, and key and scale are
  shared.
- **Firmware 0.2, not on a board yet.** One image for rev C and the next board (rev E, the Vessel: two knobs and a
  hand sensor). The left knob moves the tape by hand, a hand over the sensor closes a filter, the menu is a list,
  a take starts at the first sound, and the screen draws 30 frames a second without holding up the controls.
  Tested on a PC only: `tools/check.sh`.
- **Four more instruments on the list:** [HUFF](instruments/huff), [WRECK](instruments/wreck),
  [MANTRA](instruments/mantra) and [BAND](instruments/band). Each has run on a board. They install from the
  [page](https://willbearfruits.github.io/samplooler/#instruments) or the desktop app.
- **The desktop app** ([app/](app/)): the list, one-click install onto a board, the README and manual of each.
  Linux, built from source.
- **The plugin** ([plugin/](plugin/)): VST3 and standalone.

## Next

1. **Firmware 0.2 on a board.** First on a rev C (the menu, the takes, the screen), then on the first rev E (the left
   knob, the hand sensor). Measure the CPU and the screen's frame rate there, and tune the hand's range.
2. **Played, not just run.** HUFF's breath detector, WRECK's latest build, MANTRA's wall of amps and BAND on the
   current core. Play them on a board.
3. **Two boards together.** Play the radio link on real boards, the Samplooler with WRECK or MANTRA, and fix what
   the simulators could not show.
4. **SING.** A voice processor, in [instruments/](instruments/) but not on the list. The Samplooler already has its
   voices, so it is decided last whether it stays its own instrument.
5. **PIXEL.** A chiptune machine: four parts written on a grid, songs, eight old sound chips on the knob. In
   [instruments/](instruments/pixel) but not on the list yet. It ran on a board for the first time on 10 November.
   Next for it: playing it, and the link with a second board.
6. **Pictures.** Photos and a filmed video where the manual and the build guide have placeholders.
7. **The next board.** One is drawn, not made. It becomes the board in the build guide after one is built and tested.
8. **The app for everyone.** Ready-made downloads (AppImage and deb first, then macOS and Windows). Its board view,
   a board's settings shown and changed from the app, is built and needs trying on real boards.

## Later

- A bought Stack comes loaded with the program you pick.
- More instruments, from anyone: [CONTRIBUTING.md](CONTRIBUTING.md).
