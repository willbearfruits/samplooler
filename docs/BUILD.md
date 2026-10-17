# Build a Samplooler

Hold a button and talk. Let go and it loops. Tilt the box nose down and the loop
plays forward, faster the further you tilt; nose up plays it backwards.

This guide covers both versions: parts, wiring, the case, flashing and a first
check. The pin tables here come from `firmware/main/pins.h`; the pin map page
[pinmap.html](pinmap.html) shows the same thing drawn out.

## 1. Pick a version

| | S3 | C3 mini |
| - | - | - |
| Board | ESP32-S3 SuperMini | ESP32-C3 SuperMini with the 0.42" OLED (01Space style), or the plain C3 SuperMini |
| Take length | 20 s at 32 kHz, 16-bit | about 5 to 7 s at 16 kHz (the log says how much fits) |
| Modes | TAPE and GRAIN | TAPE only |
| Roll effects | left = delay, right = stutter | none |
| Screen | 0.96" 128 x 64 OLED | the board's 0.42" 72 x 40 OLED |
| Controls | record button, EC11 encoder, BOOT | BOOT, optional EC11 encoder |
| USB | MIDI port, sound card, plugin | none (the C3 has no USB OTG) |
| Sample slots | 8, on flash | 8, on flash |
| Case | the kit's case | `enclosure/samplooler.scad` |

The S3 version uses the same parts and pins as the
[ESP32-S3 SuperMini kit](https://github.com/willbearfruits/esp32-s3-supermini-kit).
If you have built that kit, flash this firmware and you are done: no rewiring.

Screens: [S3](../images/screens-s3.png), [C3](../images/screens-c3.png).

## 2. Parts

Prices are rough single-piece AliExpress ranges in USD, September 2026, before
shipping. Expect them to move.

### S3

| Qty | Part | Rough price |
| -: | - | - |
| 1 | ESP32-S3 SuperMini (ESP32-S3FH4R2, 2 MB PSRAM) | $3.5 to 7 |
| 1 | INMP441 I2S microphone, round 6-pin board | $1.5 to 3 |
| 1 | PCM5102A I2S DAC board with 3.5 mm jack (GY-PCM5102, purple) | $2.5 to 5 |
| 1 | MPU6050 GY-521 breakout | $1 to 2.5 |
| 1 | SSD1306 0.96" 128 x 64 OLED, 4-pin I2C | about $2 |
| 1 | EC11 rotary encoder with push switch | $0.8 to 2 |
| 1 | 12 mm tactile button (record) | a few cents |
| 0 or 1 | MAX98357A amp + small 4 or 8 ohm speaker (optional) | a few dollars |
| | Hookup wire, heat-shrink, 3D-printed case | |

The OLED price is a guess; it was not part of the price research.

### C3 mini

| Qty | Part | Rough price |
| -: | - | - |
| 1 | ESP32-C3 SuperMini with 0.42" OLED | about $3 to 5 |
| 1 | INMP441 I2S microphone, round 6-pin board | $1.5 to 3 |
| 1 | PCM5102A I2S DAC board with 3.5 mm jack | $2.5 to 5 |
| 1 | MPU6050 GY-521 breakout | $1 to 2.5 |
| 0 or 1 | EC11 rotary encoder with push switch (optional) | $0.8 to 2 |
| 4 | M2 x 8 self-tapping screws | |
| | Hookup wire, hot glue, 3D-printed shell, lid and plunger | |

A plain C3 SuperMini (no screen) also works, at $1.8 to 3.5. The firmware runs
without a screen, but the case is drawn for the OLED board.

## 3. Tools, time, skill

| | |
| - | - |
| Tools | soldering iron and solder, wire strippers, flush cutters, multimeter, small screwdriver, hot glue gun, a 3D printer (or a friend with one) |
| Time | 2 to 4 hours for the electronics on a first build, plus printing (a few hours) and flashing (10 minutes) |
| Skill | basic through-hole soldering: wires onto pads and headers, one solder bridge on the DAC. No surface-mount work. |

If you have never soldered, practise on something cheap first or ask someone who
has to sit with you. Unplug USB before you solder or move a wire.

## 4. Wiring

Every GND goes to the board's GND. The PCM5102A runs from 5V (its own regulator
makes 3.3 V); everything else runs from 3V3. All GPIO logic is 3.3 V.

### S3

| Signal | S3 GPIO | Goes to |
| - | -: | - |
| BCLK | 7 | INMP441 SCK, PCM5102A BCK |
| WS | 8 | INMP441 WS, PCM5102A LCK |
| DOUT | 9 | PCM5102A DIN |
| DIN | 10 | INMP441 SD |
| SDA | 12 | MPU6050 SDA, OLED SDA |
| SCL | 13 | MPU6050 SCL, OLED SCL |
| ENC A | 4 | encoder CLK |
| ENC B | 5 | encoder DT |
| ENC SW | 6 | encoder push |
| REC | 3 | record button, other leg to GND (a 12 mm tactile button, or any momentary switch) |
| AMP SD | 11 | MAX98357A SD (optional; the firmware drives it high so the speaker plays) |
| BOOT | 0 | button on the board |

With the optional amp, its BCLK, LRC and DIN share GPIO 7, 8 and 9 with the DAC.
Power it from a supply that can deliver the speaker current, not from the small
3V3 regulator. See the kit's [pin map](https://github.com/willbearfruits/esp32-s3-supermini-kit/blob/main/docs/pinmap.md).

### C3 mini

| Signal | C3 GPIO | Goes to |
| - | -: | - |
| BCLK | 4 | INMP441 SCK, PCM5102A BCK |
| WS | 3 | INMP441 WS, PCM5102A LCK |
| DOUT | 10 | PCM5102A DIN |
| DIN | 7 | INMP441 SD |
| SDA | 5 | MPU6050 SDA (the OLED is already on it) |
| SCL | 6 | MPU6050 SCL (the OLED is already on it) |
| ENC A | 0 | encoder CLK (optional) |
| ENC B | 1 | encoder DT (optional) |
| ENC SW | 20 | encoder push (optional) |
| BOOT | 9 | button on the board |

Leave GPIO 8 (LED, strapping), 2 (strapping) and 18/19 (USB) alone.

### Module power and straps

| Module | Pin | Connect to |
| - | - | - |
| INMP441 | VDD | 3V3 |
| INMP441 | L/R | GND (VDD also works; the firmware finds the slot) |
| PCM5102A | VIN | 5V |
| PCM5102A | SCK | GND (most boards already do this) |
| PCM5102A | XSMT bridge | H, or the DAC stays muted |
| PCM5102A | FLT, DEMP, FMT bridges | L. A floating FMT flips the data format when touched: loud distortion |
| MPU6050 | VCC | 3V3 (most breakouts also accept 5V) |
| MPU6050 | AD0 | GND (address 0x68) |
| Encoder | + | 3V3, if the module has the pin. The GPIOs use internal pull-ups. |

The OLED sits at I2C address 0x3C and the MPU6050 at 0x68, so they share the bus.
Most breakouts have pull-ups; if none of yours do, add 4.7 k from SDA and SCL to 3V3.

The PCM5102A output is line level. Use powered speakers, an audio interface or a
headphone amp; plain headphones will be quiet.

## 5. Assembly

### S3

The S3 version goes in the kit's case. Its OpenSCAD source, STLs and notes are
in the kit repo's [enclosure/](https://github.com/willbearfruits/esp32-s3-supermini-kit/tree/main/enclosure),
and the kit's [assembly guide](https://github.com/willbearfruits/esp32-s3-supermini-kit/blob/main/docs/assembly/README.md)
walks through the wiring step by step. BOOT sits inside that case, so the encoder
push is the record button.

### C3 mini

Two printed parts plus a button plunger, held with M2 self-tapping screws into
printed posts. Outer size 74 x 60 x 16 mm. Run `enclosure/render.sh` for
`out/shell.stl`, `out/lid.stl` and `out/plunger.stl`.

This case has not been printed yet. Several numbers come from photos and seller
listings, not calipers. Measure your parts first, and expect one round of fixes on
the jack and button positions.

| Check before printing | Parameter source |
| - | - |
| OLED position on the board | photo estimate |
| BOOT / RESET switch centres | photo estimate |
| PCM5102A board size and height | one seller listing |
| Jack centre along the short edge | none |
| INMP441 board diameter | kit assumption, no drawing found |

Each one is a single parameter at the top of `enclosure/samplooler.scad`. Change it
and rerun `render.sh`. Details and sources are in [ENCLOSURE.md](ENCLOSURE.md).

1. Print the shell and the lid face down, 0.4 nozzle, 5 perimeters, no supports.
2. Solder wires to the modules. No headers: the cradles are sized for bare boards.
3. Drop the DAC, IMU and ESP32 into their cradles. The USB-C and the jack should
   sit in their wall openings without force.
4. Seat the mic in the lid collar, port side toward the grille, and glue it.
5. Drop the plunger into its lid guide, flange inside, then screw the lid on with
   four M2 x 8 self-tapping screws.

The IMU lies flat on the floor with its long edge along the box. The firmware
reads rotation about the box's short axis as pitch (nose up or down).

The case has no hole for the optional encoder. Without it the C3 records with
BOOT and always has tilt on.

## 6. Flash

Pick one of three ways.

### In the browser

Open <https://willbearfruits.github.io/samplooler/> in Chrome or Edge on a
computer (phones do not work) and press Install. The page picks the right image
for your chip.

| Chip | How to connect |
| - | - |
| S3 | Hold BOOT while you plug in USB, then press Install. The running firmware turns the USB port into a MIDI device and sound card, so there is no serial port until the chip is in download mode. After flashing the page may say the device was lost. That is expected: unplug and plug in again. |
| C3 | Plug in and press Install. It usually needs no BOOT. If it does not connect, hold BOOT while you plug in. |

### A release image with esptool

Each release has one merged image per chip that goes at offset 0x0:

```sh
esptool.py --chip esp32s3 write_flash 0x0 samplooler-esp32s3.bin
esptool.py --chip esp32c3 write_flash 0x0 samplooler-esp32c3.bin
```

On the S3, hold BOOT while plugging in first, as above.

### From source

ESP-IDF v5.5 at `~/esp/esp-idf`:

```sh
. ~/esp/esp-idf/export.sh
cd firmware

# S3
idf.py set-target esp32s3      # once
idf.py build flash monitor

# C3, in its own build directory so both stay built
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 set-target esp32c3   # once
idf.py -B build-c3 -DSDKCONFIG=sdkconfig.c3 build flash monitor
```

The first build downloads the USB components. An S3 that is already running the
USB firmware can be reflashed with `tools/flash.sh`, which asks it over MIDI to
reboot into download mode. Updating keeps your saved slots and settings as long as
the flash is not erased: the browser flasher asks before erasing, and `tools/flash.sh`
never does.

## 7. First power-on

To watch the serial log on the S3, hold the encoder push while plugging in. That
keeps the plain serial console and skips USB MIDI and audio. The C3 always has it.

1. The log names the firmware version, the reset reason, the chip, the take length and
   whether the MPU6050 answered.
2. Hold record, speak, release. Flat on the desk it loops slowly forward.
3. Tilt nose down: faster. Nose up: backwards, faster with angle.
4. Wave it about: no zipper noise, no click at the loop seam.
5. Hold record for longer than the take length: recording stops at the limit,
   playback still works.
6. Without the MPU6050 it plays forward at 1x and says so in the log. Nothing crashes.
7. Change a setting, wait five seconds, power-cycle: the setting is still there.
8. Save the loop to a slot (menu, Save): the sound fades out, the screen shows `SAVING n`
   with a bar for some seconds, then `SAVED n`, and the loop fades back in. Load it back.
9. S3 in USB mode: `python3 tools/sl_midi.py status` prints a status line. After a minute
   of playing, its audio counters (rx ovf, tx ovf, read err) should still be 0; flash stalls
   count the save you just did.

If the device ever restarts on its own, the screen says why for a few seconds (`CRASH: …`,
`BROWNOUT`, `WATCHDOG`) and `sl_midi.py status` prints the crash report with a backtrace.

**Safe mode.** Three crashes in a row, none of them followed by ten seconds of normal running,
and the next boot starts from the default settings and keeps them; the screen says
`SAFE MODE: 3 crashes`. A power cycle or a requested reboot starts the count over. The task
watchdog covers the control loop and the audio task, so a hang resets the device within five
seconds instead of leaving it playing with dead controls.

**Factory reset.** Hold the record button while plugging in and keep it down: the screen counts
`RESET? hold REC 3 … 2 … 1`, then `RESET defaults restored`. Let go earlier and nothing happens.
The saved loops in the slots stay. (On the C3 the record button is BOOT, which at power-on means
download mode instead, so the C3 has no reset gesture: reflash with "erase" ticked.)

**Recovery.** Whatever the firmware does, the chip's own download mode is always reachable:
hold BOOT, tap RESET (or plug in with BOOT held), release BOOT, and the flasher sees the board.
The firmware cannot lock itself out.

### When something is wrong

| Symptom | Fix |
| - | - |
| No sound at all | PCM5102A XSMT bridge not set to H, or SCK not on GND. Check VIN is on 5V. |
| The sound distorts while you touch the DAC's pins | FMT (or FLT, DEMP) is floating: set its bridge on the DAC's back to L, or wire the pin to GND |
| Nose down plays backwards | Flip the sign of `TILT_SIGN` in `firmware/main/main.c` (it is `-1` for the stack boards, `+1` for the kit wiring) |
| Tilt reacts to the wrong axis | The sensor sits sideways: set `TILT_USE_ROLL` to 1 in `firmware/main/main.c` |
| No MIDI port after flashing, `lsusb` shows `303a:0009` | The S3 stayed in download mode. Unplug and plug in again. |
| No MIDI port, serial console instead | The encoder push was held at power-on. Plug in again without holding it. |
| Block at the right end of the input meter | The mic clipped. Turn Mic gain down. |
| Screen blank | Check SDA/SCL and 3V3 to the OLED. The rest works without it, and the screen comes back by itself once it answers again. |
| Screen says `NO PSRAM` | The S3 module has no PSRAM (or it is not the FH4R2). The firmware needs it for the take. |
| Screen says `BROWNOUT` after boot | The supply dipped: a weak USB port or cable, or the speaker amp drawing from the 3V3 pin. |
| Settings lost after a power cycle | They are written three seconds after the last change, never while recording. Wait, then unplug. |
| Board does not enumerate for flashing | Hold BOOT, tap RESET, release BOOT, then flash. |

## 8. Controls

The same on both; the S3 has a record button, the C3 records with BOOT.

| Control | Play | Menu: choosing | Menu: editing |
| - | - | - | - |
| Record button (or BOOT), hold | record | record | record |
| Encoder tap | open the menu | edit the setting, or run Save / Load | back to the list |
| Encoder turn | adjust the last setting (a toast shows it) | choose a setting | adjust it |
| Encoder hold (0.5 s) | freeze or release the tilt: speed, direction and the roll effects stay where they are (`HOLD` on the screen) | leave the menu | leave the menu |

On a kit that still has the joystick, build with `HAS_JOY 1` in `pins.h`: its Y axis sets the
speed when Tilt is off and its X axis the pitch in GRAIN mode.

### Settings

| Setting | Range | Default | Notes |
| - | - | - | - |
| Mic gain | 0 to 36 dB | 12 dB | |
| Mode | TAPE, GRAIN | TAPE | GRAIN on the S3 only |
| Pitch | semitones | 0 | GRAIN only |
| Tilt | on, off | on | off: plays at 1x (or at the joystick's Y with `HAS_JOY`) |
| Speed max | 0.25 to 8x | 2x | speed at 45 degrees of tilt; above 2x the recording aliases audibly |
| Speed min | 0 to 1x | 0.05x | the crawl when flat |
| Snap | on, off | off | speeds quantised to octaves and fifths |
| Grain | 20 to 200 ms | 50 ms | GRAIN only |
| Volume | 0 to 150 % | 100 % | |
| Input | mic, USB | mic | USB is S3 only: record what the computer plays |
| Slot, Save, Load | 8 slots | slot 1 | takes survive power off |

TAPE is varispeed: speed and pitch move together. GRAIN keeps them apart, so you
can scrub backwards at half speed and still hear words at their own pitch, or
freeze the sound at speed 0.
