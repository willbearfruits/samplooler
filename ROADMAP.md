# Roadmap

Where the Samplooler is in October 2026, and what comes next. Nothing here has a date.

## Now

- **The Stack.** Rev C boards are made and run the firmware. Rev D, the files in [hardware/](hardware/), is being
  tested.
- **The Samplooler firmware.** The looper, 18 voices on the input, 8 saved loops, USB MIDI and a USB sound card,
  and a radio link to the other boards: a take sets their tempo or lands on their beat, and key and scale are
  shared.
- **Three more instruments on the list:** [WRECK](instruments/wreck), [MANTRA](instruments/mantra) and
  [HUFF](instruments/huff). Each has run on a board. They install from the
  [page](https://willbearfruits.github.io/samplooler/#instruments) or the desktop app.
- **The desktop app** ([app/](app/)): the list, one-click install onto a board, the README and manual of each.
  Linux, built from source.
- **The plugin** ([plugin/](plugin/)): VST3 and standalone.

## Next

1. **Two boards together.** Play the radio link on real boards, the Samplooler with WRECK or MANTRA, and fix what
   the simulators could not show.
2. **Played, not just run.** WRECK's latest build and MANTRA's wall of amps have only run in the simulator. Play
   them on a board.
3. **The app for everyone.** Ready-made downloads (AppImage and deb first, then macOS and Windows). Its board view,
   a board's settings shown and changed from the app, is built and needs trying on real boards.
4. **Rev D.** Test it. Then it becomes the board in the build guide.
5. **Pictures.** Photos and a filmed video where the manual and the build guide have placeholders.

## Back on the list

These are in [instruments/](instruments/) and build, but are not offered for install yet. Each one comes back
after it runs on a board with the current core.

| | Where it is |
| - | - |
| SING, BAND | ran on a board before the latest core |
| JAM, ETHER | ran on a board on an older core |
| KNOCK, HOWL, SNIFF, CHIRP | simulator only |

## Later

- A bought Stack comes loaded with the program you pick.
- More instruments, from anyone: [CONTRIBUTING.md](CONTRIBUTING.md).
