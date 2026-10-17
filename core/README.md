# The core

The part every instrument in `instruments/` shares: the menu, the sounds, the looper, MIDI, the drivers and
the simulator's frame. It is not a firmware by itself; each instrument carries a copy of these files next to
its own. [docs/CORE.md](docs/CORE.md) explains what it does and what an instrument provides.

`../tools/core_files.txt` lists the files. `python3 tools/library.py core` shows which instruments have this
version unchanged; `python3 tools/library.py core-copy <name>` copies it into one.
