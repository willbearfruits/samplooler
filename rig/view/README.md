# slrig-view

The Samplooler Rig's viewer. It shows what the rig's daemon (slrigd) knows about a Samplooler: the board in 3D, its
screen, its signals, the tilt and roll curves, the tuning table, the gestures, the settings and the sessions. Every
change goes back to the daemon as a command, the same commands the CLI and the MCP use. The viewer never talks to the
board itself.

Status (2026-10-27): built and checked through offscreen shots, against the mock daemon and against slrigd running
the twin. The live window has not been opened yet, and no board has been connected.

## Build

From the repository root:

```sh
cmake -S rig/view -B rig/view/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build rig/view/build
```

It needs GLFW 3.3 or newer, EGL, nlohmann-json 3.11 or newer and a GL 3.3 driver. glad, NanoVG and stb are in
`external/`.

## Run

```sh
rig/view/build/slrig-view                  # connects to $XDG_RUNTIME_DIR/slrig/daemon.sock
rig/view/build/slrig-view --socket PATH
```

It tries again every second, so it can start before the daemon, and it carries on when the daemon restarts.

To work on it without the daemon, start the mock and point the viewer at it:

```sh
python3 rig/view/tools/mock_daemon.py      # $XDG_RUNTIME_DIR/slrig-mock/daemon.sock, made-up data
rig/view/build/slrig-view --socket $XDG_RUNTIME_DIR/slrig-mock/daemon.sock
```

`--pose flat` makes the mock's board lie still, screen up.

## Shots, with no window

```sh
slrig-view --shot out.png [--size 1600x1000] [--scale 2] [--wait-ms 1500] [--tab tune|gestures|settings|session]
           [--camera YAW,ELEV[,ZOOM]] [--click X,Y] [--wheel X,Y,N] [--bench N]
```

`--shot` draws through EGL with no display server. It collects data for `--wait-ms`, draws one frame and writes the
PNG. With no daemon it draws that state. `--scale` sets device pixels per unit, to check HiDPI. `--click` and
`--wheel` act on the frame first, in the order given. `--bench N` draws N frames and prints the frame times.

## What it shows

- **The Stack** (rev C) turned by the board's quaternion. The OLED plays on its glass. The arrows are the device
  axes: x to the right edge (red), y to the top edge, the nose (green), z out of the screen (blue). G is the gravity
  the accelerometer measures, turned into the world. The thin
  outline is where the host's own fusion puts the board, drawn when the two differ by more than a degree. Drag to
  turn the view, use the wheel to zoom, and double-click to reset it.
- **SCREEN**: the OLED, 4 screen pixels to a board pixel (3 when the window is small). GRID shows the gaps between
  pixels. The button beside it switches the panel's colours: yellow and blue, blue, or white.
- **TILT → SPEED** and **ROLL → FX**: the curves from the firmware's `gesture.c` with the current tuning (the `curve`
  command, asked again after every tuning or settings change). The dot is where the board is now, with its last
  second behind it. Grey bands are dead zones. Detents are yellow: a tick for the speed, a bar for the angles where
  it holds.
- **SIGNALS**: accel, gyro, angles, speed asked and speed reached, tilt_u with its dead zone, and the in and out
  levels, over 2, 5, 10 or 30 s. FREEZE holds them.
- **TUNE**: every tuning value, by group. A dot and yellow mark a value that differs from v1. KEEP AS A (or B)
  stores the whole set; A and B recall it. SAVE TO BOARD writes it to the board's NVS, and RESET goes back to v1.
  The box at the bottom explains the value under the mouse.
- **GESTURES**: each gesture with its rule, its counter and the tuning values behind it. Click one of those to jump
  to it in TUNE. A row flashes when its gesture fires. The timeline below shows the last 20 s.
- **SETTINGS**: the board's own settings, stepped with - and +. An action the daemon marks with `confirm` asks
  first.
- **SESSION**: record, the guided tests, the sessions, and the analysis of the one selected. APPLY sets the
  tuning values an analysis suggests.
- **During a guided test**, a card over the 3D view shows the step, its words and a countdown.
- **The strip at the bottom**: the commands anyone ran (view, cli, osc; mcp in pink, which is Claude) and the
  board's log.
- **The status bar**: the source (board, twin or none), the board, the stream rates, drops, the board's CPU, and
  the viewer's own frame time and how long a state message waits before it is drawn.

## Keys

| | |
|---|---|
| 1 to 4, Tab | the tabs |
| up, down | choose a tuning value |
| left, right | step it (shift: ten steps) |
| R | reset the 3D view |
| A | axes on or off |
| G | the OLED's pixel grid |
| P | the OLED's colours |
| F | freeze the plots |
| [ and ] | a shorter or longer plot window |
| ctrl+Q | quit |

## Frame time

`--bench 120` at 1600 x 1000 on the RTX 3060 (the GPU at its idle clock): about 2.0 ms of CPU and 2.4 ms of GPU per
frame on average, 2.7 ms with both and a `glFinish`, 4.3 ms at the 95th percentile. At 1280 x 800, 1.5 ms. The 3D
pass costs 0.1 ms of CPU; the 2D layer costs the rest.

## Files

| | |
|---|---|
| `src/main.cpp` | options, the window, `--shot` through EGL |
| `src/App.cpp`, `Panels.cpp`, `Plots.cpp` | the layout, the tabs, the plots, curves and OLED |
| `src/Scene3D.cpp` | the 3D view (GL 3.3, multisampled, a soft shadow from above) |
| `src/Net.cpp`, `Model.cpp` | the socket thread and the data the daemon sent |
| `src/Ui.cpp`, `PxFont.cpp` | the widgets, and lettering in the firmware's pixel faces |
| `assets/stack.mesh` | the board, made by `tools/build_mesh.py` from KiCad's VRML of rev C (tag `checkpoint/boards-rev-c`); run it again after a board change (it needs `kicad-cli`) |
| `src/PxFontData.inc` | made by `tools/gen_pxfont.py` from `firmware/main/fonts/` |
| `tools/mock_daemon.py` | a stand-in daemon with made-up data, standard library only |
| `assets/fonts/` | Atkinson Hyperlegible and JetBrains Mono, with their OFL licences |
| `external/` | glad (CC0 / Apache-2.0), NanoVG (zlib), stb (public domain) |

## Not done

- The live window is written but has not been opened: every check so far was offscreen.
- There is no text entry, so session names are made by the daemon.
- The presets show as buttons only when `tune.list` names them (the mock does; slrigd does not yet).
