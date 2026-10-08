---
name: pcb-design
description: Playbook for the generated KiCad boards in hardware/gen (the Samplooler stack and Mini) and for any new board built the same way. Use when changing placement, nets, breakouts, routing, ground pours or silkscreen, when chasing ERC/DRC findings, when adding a board, or when making renders and the showcase video. Covers the build loop, the order to work in, the rules that avoid a long fix-and-rebuild loop, KiCad/Freerouting gotchas, debugging tools and the video pipeline.
---

# Generated PCB design (hardware/gen)

Every KiCad file under `hardware/` is generated. Change the Python in `hardware/gen/`, never the
KiCad files. One build does:

1. library, then schematic with ERC, then netlist
2. PCB placement and the fixed SuperMini escape
3. Freerouting, trying several settings
4. pruning of dangling stubs
5. GND pours and stitching
6. GND rescue on the real fill
7. DRC with schematic parity
8. Gerbers, BOM, CPL

`hardware/README.md` describes the boards. This file describes how to work on them.

## The loop

```sh
python3 hardware/gen/build.py --check             # mechanical model only: seconds
python3 hardware/gen/build.py mini --no-route     # placement only, to look at
python3 hardware/gen/build.py mini                # one design, routed: ~2-5 min
python3 hardware/gen/build.py stack               # stack-bottom + stack-top
.claude/skills/pcb-design/tools/report.sh         # DRC counts + what the router left, per board
python3 hardware/gen/panel.py [stack|mini]        # one-piece panels: stack 94 x 94; Mini + Filter 82 x 94
python3 hardware/gen/render.py                    # hardware/images/*.png
```

**Done means:**
- ERC 0
- DRC 0 at every severity, with schematic parity
- every connection routed
- 0 collisions in the mechanical model
- on the stack, J1 mates pin for pin

The build prints all of this. The reports are `hardware/<board>/*-drc.rpt` and `*-erc.rpt`.
Close the projects in KiCad first: the build refuses to run while a `~*.lck` file exists.

## Work in this order

1. **Topology on paper, before placing anything.** For each pin group, note which side of its part
   it leaves from and where it has to go. Put connectors where their signals already head.
   Freerouting fails in two predictable places:
   - **dense escapes**, like pads under a module: design those by hand (`kit.supermini_escape`)
   - **buses that cross**: fix those with pin assignment

   Anything whose pin order is free (breakouts, the board-to-board link) takes its order from where
   its lines arrive. `kit.supermini_exits()` says where each escape line ends; `stack.py` and
   `mini.py` sort their breakout pins by it.
2. **Placement.** Run `build.py --check`, then `--no-route`, and look at the board with
   `tools/view.sh`. Keep courtyards apart. The mechanical model checks heights, the openings in the
   top board, and the solder order on two-sided boards.
3. **Route one design to 100%.** The router tries `kit.ROUTE_VARIANTS` (layer directions, via cost)
   and keeps the best. If every variant leaves connections, the topology is wrong: go back to step 1.
   Nudging a part 0.5 mm and re-rolling does not fix topology.
4. **Freeze.** Once a board routes fully, record `md5sum hardware/<board>/route.ses`. From here on,
   change only what Freerouting never sees:
   - silkscreen text
   - label offsets inside footprints
   - hidden references
   - post-route GND ties

   Check that the md5 is unchanged after each rebuild. Moving a part, changing a pad, or changing a
   net re-rolls the routing: even a net set to None changes the router's input.
5. **Ground.** Pours on both layers, stitching, then `kit.rescue_gnd` on the real fill. If the DRC
   still reports a GND pad or pocket, first ask whether that pad needs GND at all. Mounting lugs and
   strap options nobody uses do not: make them no-net. Only then write code.
6. **Silkscreen.**
   - Pin labels go outside pads and courtyards. On a module with pads underneath, put them outside
     the pin columns (`label_side`, `label_off` in `modules.py`).
   - Breakout labels can be skipped per row (`skip_rows`) and placed as board text instead.
   - Hide a reference rather than move a part.
7. **Finish.**
   - `render.py`
   - the README, with pinout tables from `tools/pinout.py`
   - the showcase video (below)
   - the commit. If the user has staged work of their own, commit through a temporary
     `GIT_INDEX_FILE`, then run `git reset -q -- hardware CLAUDE.md`.

## Rules that save hours

- **One design per build.** A full build of all three boards takes 10-15 min. Never rebuild
  everything for a local fix.
- **Time-box.** If a sub-problem survives two attempts, stop and question the requirement or the
  topology instead of writing a third algorithm.
- **kicad-cli DRC is the only judge.** Read its JSON (`kicad-cli pcb drc --format json`). pcbnew's
  Python connectivity disagrees with it: it counted 45° thermal spokes that the DRC did not.
- **Don't route again from the previous result.** Rerunning Freerouting on its own output made
  results worse. Vary the settings from a clean start instead: that's what `ROUTE_VARIANTS` does.
- **Test helper scripts before trusting them.** Especially wait loops and image compositing (see
  below).

## Gotchas

- **Zone fill:** pcbnew's Python `ZONE_FILLER` ignores edge and NPTH clearances. The build refills
  with `kicad-cli pcb drc --refill-zones --save-board`, and that fill is what gets plotted.
- **Freerouting:**
  - It's deterministic for the same DSN but chaotic for small changes.
  - `autoroute_settings` must go inside `(structure …)` after the layer definitions.
  - GND is stripped from the DSN (the pours handle it), and clearances get a +10 µm margin.
    Locked GND copper stays in as net-less obstacles, which is how pre-laid GND ties survive.
  - It can report "0 unrouted" for a session with two tracks crossing. `kit.autoroute` checks
    each such session for touching tracks of different nets (`session_shorts`) before accepting
    it. Import a session into a scratch copy only: after `ImportSpecctraSES`, `Tracks()` on that
    board breaks under Python 3.14 until it is saved and reloaded.
- **Split ground:** a bundle of lines running on both layers can cut the pours in two, and
  whether it does changes from one router run to the next. On the stack's bottom board a
  locked GND spine along the free bottom edge keeps the front pour whole: nothing lives below
  it, so no line has to cross it. `tools/islands.py` says `main planes joined: False` when
  this happens.
- **Panels (`gen/panel.py`):** copies the built boards (with their saved fill) onto one board,
  redraws the outer edges with gaps for 5 mm tabs, and drills 0.5 mm mouse bites at 0.8 mm on
  both edges of each tab. It never refills zones. DRC on the panel reports the bite holes as
  hole-to-hole and the stack's link nets as unconnected: both expected. Keep tabs off parts at
  the facing edges. `Duplicate()` needs `.Cast()`, and takes `addToParentGroup` on some item
  types only. Boards from another project (the Filter) are aligned by their own outline, on the
  line centres: `GetBoardEdgesBoundingBox` includes the 0.1 mm line width. `bite_in` moves the
  holes into the slot for a board whose copper runs 0.3 mm from its edge. DRC on the Mini panel
  reports the Filter's 0.3 mm pours against the Mini's 0.5 mm rule: expected.
- **Island removal ("always"):** it keeps any island that touches a pad of the net. So a GND pad
  walled in by other nets keeps its own pocket, and the DRC reports it unconnected.
- **pcbnew Python quirks:**
  - `ExportSpecctraDSN` segfaults if zones were removed from a board loaded from disk. Exporting the
    loaded board untouched works.
  - The SWIG iterators over `board.Tracks()` break on Python 3.14. Use `kit.items()`.
- **Back side:** placement is a flip, then an orientation. Local (x, y) maps to R(θ)·(x, −y).
- **Project file:** `write_project` writes the `.kicad_pro` (net classes, rules). pcbnew never saves
  it: always call `SaveBoard(path, b, True)`.
- **Shell and image tools:**
  - `pgrep -f "build.py"` matches its own command line, so a loop waiting on it never ends. Wait
    on the task or its output file instead.
  - In ImageMagick 7, `-compose minus` subtracts the first image from the second.

## Tools

The Python tools import `hardware/gen` and print positions in the board frame (mm from the top left).

| Tool | What |
| - | - |
| `tools/report.sh [board…]` | DRC finding counts and the last routing run's unrouted nets |
| `tools/view.sh <board> <out.png> [WxH+X+Y]` | copper on both layers without the pours, optionally cropped (mm) |
| `tools/pads.py <board> [ref…]` | pad positions, layers, nets |
| `tools/tracks.py <board> <net…>` | tracks and vias of the nets, with lock state |
| `tools/islands.py <board>` | GND pour islands not joined to the main planes |
| `tools/pinout.py` | breakout pinout tables (markdown) straight from `stack.py` / `mini.py` |

## Renders and the showcase videos

The video tooling lives in `tools/showcase/`. Everything lands in `hardware/images/`.

| Command | Output | Time |
| - | - | - |
| `python3 hardware/gen/render.py` | the stills | ~1.5 min |
| `tools/showcase/make_hardware_video.sh` | `samplooler-showcase.mp4`: the boards only, no sound, ~31 s | ~8 min |
| `tools/showcase/make_showcase.sh` | `samplooler-showcase-full.mp4`: the whole product, with sound, ~89 s | ~25 min |

**The full showcase's soundtrack is the device itself.** `twin.c` compiles the firmware's
`recorder_f32.c`, `fx.c`, `ui.c` and `oled.c` on the host and drives them the way `main.c` does:
10 ms ticks, the button debounce, the 50 Hz tilt curve. The input is a scripted performance
(`score.py`): the riff played into the mic, the button, the tilt and roll angles, mode switches.

The same score drives:
- the audio
- the OLED frames
- the angles that Blender tilts the stack by (`perf.py`)

So the sound, the screen and the motion stay in sync.

Stages can be rerun: `STAGE=n WORK=<dir>`. The sections and captions live in `score.py` and
`compose.py`.

**Timing notes:**
- EEVEE on the RTX 3060 runs at about 0.8 s per frame; Cycles at about 8 s.
- `QUICK=1 make_hardware_video.sh` is a one-minute smoke test.

The cards state facts (ERC 0, DRC 0, board sizes): update them whenever those change. The user
likes screenshots during long work, and the stills and videos saved in `hardware/images/`.
