# Enclosure

OpenSCAD, two printed parts plus a button plunger, M2 self-tapping screws into
printed posts. `enclosure/render.sh` writes `out/shell.stl`, `out/lid.stl`,
`out/plunger.stl` and two preview PNGs. Every module dimension is a Customizer
parameter at the top of `enclosure/samplooler.scad`.

Outer size 74 x 60 x 16 mm (cavity 70 x 56 x 12, 2 mm walls, 2 mm floor and lid).
The IMU lies flat on the floor with its long edge along the box, so "nose up"
is rotation about the box's short axis, which is what the firmware reads as pitch.

## Where the numbers come from

| Part | Value used | Source | Confidence |
| - | - | - | - |
| ESP32-C3 SuperMini, OLED variant | 25.0 x 20.5 mm PCB, header rows 17.78 mm apart, OLED on GPIO 5/6 at 0x3C, LED GPIO 8 | michiel.vanderwulp.be module page; atomic14 (SDA 5, SCL 6) | good |
| ESP32-C3 SuperMini, plain | 22.5 x 18 mm, rows 15.24 mm, PCB 1.0 | mischianti.org; lamina part library | good |
| USB-C on either | body 9 x 3.2 mm, overhangs the edge 1.3 mm | lamina part library | good |
| 0.42" OLED panel | glass 12.0 x 11.0 x 1.2 mm, active area 9.196 x 5.180 mm | displaymodule.com (DM-OLED042-647p) | good |
| OLED position on the board | 2 mm from the end opposite the USB, centred | photo estimate | **measure** |
| BOOT / RESET switch centres | 4.5 mm from the USB edge, 3.5 mm from each side | photo estimate | **measure** |
| GY-521 IMU | 21 x 16 mm, two 3 mm holes at the corners opposite the 8-pin header | protosupplies (21 x 16), grobotronics (21.2 x 16.4), AZ-Delivery manual photo for the holes | good; hole positions estimated at 2.5 mm from the edges |
| GY-PCM5102 DAC | 31.8 x 23.7 mm, 6.35 mm tall with the jack | one seller listing; the kit's own enclosure assumed 32 x 17 | **measure** |
| Jack centre along the short edge | 6.0 mm from the long edge | none | **measure** |
| INMP441 round module | 14.5 mm diameter, 1.2 mm thick, pads on 2.0 mm pitch | the kit's assumption; no manufacturer drawing found anywhere | **measure** |
| Mic port | through the PCB under the MEMS can, about 1 mm | TDK datasheet recommends 0.5 to 1 mm | good |

Everything marked **measure** is a single parameter. Change it, rerun `render.sh`.
The cradles have 0.3 mm of clearance per side, so a 0.5 mm error in a board
outline still fits; a wrong jack or button position does not, which is why those
two are the ones to check first.

## Layout, cavity coordinates in mm (x along the length, jack end at 0)

| Module | x | y | Notes |
| - | - | - | - |
| DAC | 0.5 to 32.3 | 7.5 to 31.2 | jack through the x = 0 wall, hole 8.8 mm |
| IMU | 39.0 to 60.0 | 7.5 to 23.5 | flat on the floor, header edge toward y = 0 |
| ESP32-C3 OLED | 46.1 to 71.1 | 27.5 to 48.0 | USB-C through the x = 70 wall; OLED window and BOOT plunger in the lid |
| Mic | centre (18.0, 47.25) | | drops into a collar under the lid, port up against the 7-hole grille. Fix it with a drop of hot glue. |

## Assembly

1. Print the shell and the lid face down, 0.4 nozzle, 5 perimeters, no supports.
2. Solder wires to the modules (no headers; the cradles are sized for bare boards).
3. Drop the DAC, IMU and ESP into their cradles. The USB-C and the jack should
   sit in their wall openings without force.
4. Seat the mic in the lid collar, port side toward the grille, and glue it.
5. Drop the plunger into its lid guide, flange inside, then screw the lid on with
   four M2 x 8 self-tapping screws.

## Not verified on a printer yet

The model renders and the parts do not intersect in the preview. It has not
been printed. Expect one iteration on the jack and button positions.

## Stack bottom cover (`enclosure/stack-cover.scad`)

A tray that clicks onto the underside of the stack's bottom board (rev D, 94 x 46 mm) so the
solder joints are covered. `enclosure/render-stack-cover.sh` writes the STL, runs the collision
check and renders the previews. Nothing printed yet.

- **Drawn for a 1.2 mm nozzle at 0.6 mm layers**: the wall is one line (1.2 mm), the floor three
  layers (1.8 mm), each clip tooth one line deep. Print floor down, no supports. The tooth's 45°
  lead-in is two layers, the upper one a 1.8 mm wall: slicers with variable-width perimeters
  (Arachne: PrusaSlicer 2.6+, OrcaSlicer, Cura 5+) print it; a classic slicer drops that layer
  and the lead-in becomes a step, which still clicks but takes a harder push.
- **How it holds**: the board drops in with 0.3 mm a side; the long walls bow out over four
  teeth and spring back over the board's top face. Teeth sit only where the top face is empty
  within 1.5 mm of the edge: under the DAC and the IMU modules at the nose (both 2.5 mm up on
  their headers) and between the joystick and the encoder at the tail. The short walls locate.
- **Depth**: 2.5 mm under the board, for pin tails and the joystick's legs trimmed to 2 mm. The
  four standoff screws sit in 7 mm holes in the floor, so they stay reachable and do not set the
  depth; the board can be 1.6 to 1.8 mm thick.
- **Notches** drop the wall to 0.3 mm below the board's top face where a plug comes in: the
  USB-C (x 67–83), the 3.5 mm jack (x 9–21) and the Qwiic socket on the right edge (y 11–21).
  The USB-C overmold clears the notched wall by about 0.8 mm: the first thing to check on a print.
- **Checked**: OpenSCAD intersects the cover with the board's keep-outs (standoffs, screw heads,
  pin tails, header plastic, the module boards, encoder, Qwiic socket, both plugs and a Qwiic
  cable) and the result is empty. Blender finds the STL closed, on the bed, with four 1.2 mm
  bridges (the tooth undersides). Its thin-wall check flags the teeth at 0.4–0.6 mm: that is the
  wedge of the lead-in measured across, on a 1.2 mm wall, reviewed and accepted.
- Every board number is copied from `hardware/gen/stack.py` and `modules.py`; move the teeth
  (`teeth_nose`, `teeth_tail`) if the board's edge layout changes.
