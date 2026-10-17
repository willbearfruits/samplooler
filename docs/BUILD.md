# Build a Samplooler Stack

The Stack is two 94 x 46 mm boards, 11 mm apart on standoffs. The bottom board carries the ESP32-S3 SuperMini,
the DAC, the tilt sensor, the joystick and the encoder; the top board carries the screen and the microphone, hung
underneath it. The modules are the usual breakouts on straight headers, and the boards do the wiring.

Rev C is the current board: it was made and runs the firmware, and the screen, microphone, tilt, line out and USB
MIDI/audio work. Its files are at the git tag `checkpoint/boards-rev-c`; it has the KY-023 joystick on its own
little board, DAC strap jumpers, and the OLED and the mic on top of the top board. The board files in
[hardware/](../hardware/) are rev D, which is being tested now (October 2026). This guide follows rev D.

How to play it is in [MANUAL.md](MANUAL.md).

![The stack, rendered](../hardware/images/stack-3d.png)

## What it takes

| | |
| - | - |
| Time | An evening for the soldering, ten minutes for the flashing |
| Skill | Through-hole soldering, plus six 0805 capacitors (three are optional), which are easy by hand or can be fitted by JLCPCB |
| Tools | Soldering iron, solder and flux, flush cutters, a small file, a multimeter, tweezers, a screwdriver for M3, a toothpick for BOOT |
| Computer | Chrome or Edge for the browser flasher, or ESP-IDF 5.5.1 to build from source |

If you have never soldered, practise on something cheap first or ask someone who has to sit with you. Unplug USB
before you solder or move anything.

## Ordering the boards

Both boards come as one 94 x 94 mm JLCPCB panel. Upload `hardware/fab/samplooler-panel-stack-jlcpcb.zip`, set
Panel by Customer and Different Design = 2, and check the price before you pay. The other order settings and the
files are in [hardware/README.md](../hardware/README.md#ordering-from-jlcpcb).

## Parts

| Qty | Part | Goes where |
| - | - | - |
| 1 | Samplooler Stack panel from JLCPCB (above): 2 layers, 1.6 mm, HASL or ENIG | both boards |
| 1 | ESP32-S3 SuperMini, the ESP32-S3FH4R2 with 2 MB PSRAM | bottom U1 |
| 1 | GY-PCM5102 purple I2S DAC with the 3.5 mm jack | bottom U2 |
| 1 | GY-521 (MPU-6050) tilt sensor | bottom U4 |
| 1 | PS2-style thumb joystick, bare: the part on a KY-023, without its board | bottom U5 |
| 1 | EC11 encoder with push, vertical, 25 mm shaft, plus a knob | bottom SW1 |
| 1 | SSD1306 0.96" 128 x 64 I2C OLED, 4 pins | top U1, underneath |
| 1 | INMP441 round I2S microphone | top U2, underneath |
| 1 | 1 mm double-sided foam tape, about 5 x 20 mm | under the screen's glass |
| 1 + 1 | 2 x 4 female socket, 8.5 mm tall, and a 2 x 4 male header | bottom J1, top J1 from underneath |
| 4 + 8 | M3 x 11 mm female-female standoffs and M3 screws | between the boards |
| about 55 | straight 2.54 mm header pins (a strip or two) | under every module |
| 3 | 10 µF 0805 capacitors (LCSC C15850) | bottom C1 C2, top C1 |
| 3 | 100 nF 0805 capacitors (LCSC C49678), optional | bottom C3, top C2 C3 |
| 0 or 1 | JST-SH 4-pin horizontal (SM04B-SRSS-TB), if you want the Qwiic I2C port | bottom J3 |
| 0 or 1 | MAX98357A amp breakout (Adafruit 3006 pin order) and a small 4 or 8 ohm speaker | bottom J2 |

Not needed: the pogo pins under the SuperMini (they only serve the breakout rows), and R1 and R2 (the OLED and the
GY-521 carry their own I2C pull-ups).

## Don't forget

The things that cost an evening on the first board. Each one is a few seconds to check before you power up.

| Check | Why |
| - | - |
| DAC control pins | XSMT high, FMT low. The board ties XSMT to the DAC's own 3.3 V and FLT, DEMP and FMT to ground through the DAC's 9-pin row, so fit that row. An open XSMT leaves the PCM5102 muted with no other symptom; an open FMT flips the data format whenever a finger comes near, and the sound distorts. SCK on the DAC board goes to ground; most boards do that already. |
| OLED pin order | Most 4-pin OLEDs read GND VCC SCL SDA and the top board comes bridged for that. If yours reads VCC GND SCL SDA, cut the thin 1-2 bridges on the top board's JP1 and JP2 and solder 2-3 on both, before you solder the screen (photo below). Powering a screen backwards kills it. |
| Every joint | A module on headers has two joints per pin: the module to the header, and the header to the board. The first board's screen stayed dark over one SCL pin that was soldered on top but not wet through the hole. With flux, hold the iron until the solder pulls into the barrel. Then beep the I2C lines: the SuperMini's pin marked 13 is SCL and 12 is SDA, and both must reach the OLED and the GY-521. |
| Header plastic | The modules sit on their headers with the plastic between module and board. The bottom board's 2 x 4 socket points up; the top board's 2 x 4 header points down, soldered from the top face. Get one wrong and the boards won't mate. |
| Joystick direction | Its switch goes toward the player's edge, away from the USB-C. |
| Trim the tails | Trim every pin tail flush under the bottom board, to 2 mm at most. A long tail is what finds a metal desk. |
| 25 mm shaft | The encoder's shaft has to reach up through the top board, 11 mm higher. A 20 mm shaft ends below the knob. |
| Power | Everything runs from the SuperMini's USB-C: 5 V to the DAC, 3.3 V from the module's regulator to the rest. The optional speaker amp draws from 5 V, never from the 3.3 V pin. |

> PHOTO: the top board's JP1 and JP2 up close, as they come (1-2 bridged) and changed (1-2 cut, 2-3 bridged)

## Order of work

1. Snap the panel apart and file the tab nubs flush.

   > PHOTO: the panel as it arrives, and the two boards snapped apart with the nubs filed flush

2. SMD first. The 10 µF capacitors matter: bottom C1 on the 5 V rail and C2 on 3.3 V, top C1 beside the screen.
   The 100 nF ones are optional. Capacitors have no marking; go by the bag. (104 is 100 nF, 106 is 10 µF.)

   > PHOTO: the capacitors soldered, close up: bottom C1 to C3 and top C1 to C3

3. Joystick: the bare stick straight onto the bottom board, its switch toward the player's edge. Solder the four
   frame tabs too, then trim the legs underneath to 2 mm.

   > PHOTO: the bare stick on the bottom board with its switch toward the player's edge, and its legs trimmed underneath

4. Modules onto the bottom board, each on straight headers with the plastic between module and board. Fit the
   DAC's 9-pin row as well. Then the encoder and the 2 x 4 socket. Trim the tails flush underneath.

   > PHOTO: the bottom board with every module on, from the side, so the header plastic and the DAC's 9-pin row show

5. Top board: the OLED and the mic hang underneath it, facing up. Check the OLED's pin order first. Its header
   goes in from the glass side, plastic between the glass side and the board, long pins up through the board (most
   come with the header the other way round). A strip of foam tape on the glass's hidden lower end keeps it level.
   The mic sits flush, sound hole over the small port; trim the pin stubs below it. Then the 2 x 4 header from
   underneath, plastic below the board, soldered on top.

   > PHOTO: the underside of the top board: the OLED and the mic hung on it, the foam tape, the 2 x 4 header pointing down

6. Stack: four 11 mm standoffs on the bottom board, press the header into the socket, screw down. The joystick's
   stick and the encoder shaft come up through the top board.

   > PHOTO: the finished stack from the nose, standoffs in place, the stick and the knob up through the top board

| Bottom board, modules on (render) | Top board (render) |
| - | - |
| ![Bottom board](../hardware/images/stack-bottom-plan.png) | ![Top board](../hardware/images/stack-top-plan.png) |

## Flashing

### From the browser

1. Hold the SuperMini's BOOT button while you plug in the USB cable, then let go. Reach it through the hole in the
   top board with a toothpick.

   > PHOTO: a toothpick on BOOT through the top board's hole while the USB cable goes in

2. Open [willbearfruits.github.io/samplooler](https://willbearfruits.github.io/samplooler/) in Chrome or Edge on a
   computer (not a phone) and press Install. On a new board, say yes to erase.
3. After flashing the page may say the device was lost. That is expected: the chip is still in download mode.
   Unplug, plug in again without BOOT, and it comes up as the Samplooler.

### With esptool

The release image from the [releases page](https://github.com/willbearfruits/samplooler/releases), written at
offset 0, with the board in download mode as above:

```sh
esptool.py --chip esp32s3 write_flash 0x0 samplooler-esp32s3.bin
```

### From source

ESP-IDF 5.5.1 at `~/esp/esp-idf`:

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py set-target esp32s3     # once
idf.py build
idf.py flash                  # in download mode, as above
```

The first build downloads the USB components. A board that already runs the firmware can be reflashed without
BOOT: `tools/flash.sh` (Linux) asks it over MIDI to reboot into download mode, then writes the build. It never
erases the slots or the settings.

The firmware turns the USB port into a MIDI device and a sound card, so there is no serial console. To get one,
for `idf.py monitor` or to flash without BOOT, hold the encoder down while you plug in: the board keeps its serial
console and leaves USB MIDI and the sound card off. Otherwise read the log with `python3 tools/sl_midi.py log`
(Linux) or in the plugin.

## First power-on

Plug in. After a short intro the screen shows the tape and READY, and the computer sees a Samplooler MIDI port and
sound card. Then, in order:

1. Record. Hold the stick down, speak, let go. The tape fills while you hold and the loop plays when you release.
2. Tilt. Nose down: faster, the operator runs faster. Nose up: backwards. Flat on the desk: a slow crawl.
3. Roll. Left past 15 degrees: delay, echoes of the operator. Right: stutter.
4. Wave it about. No zipper noise, no click at the loop seam.
5. Hold record past 20 seconds. Recording stops at the limit, playback still works.
6. A setting. Change Volume, wait half a minute, unplug and plug in. It is still there.
7. A slot. Menu, Save. The sound fades, a bar runs under SAVING, then SAVED, and the loop fades back. Load it.
8. The status line, on Linux: `python3 tools/sl_midi.py status`. After a minute of playing, the audio counters (rx
   ovf, tx ovf, read err) should all be 0. Flash stalls count the save you just did.

If it restarts by itself, the screen says why for a few seconds: `CRASH: step`, `BROWNOUT` or `WATCHDOG`, and the
status command prints the crash report with a backtrace. BROWNOUT means the supply dipped: a weak USB port or
cable, or a speaker amp on the wrong rail.

## When something is wrong

| Symptom | Cause and fix |
| - | - |
| No sound at all | XSMT not high: the DAC's 9-pin row not soldered. SCK not on ground, or VIN not on 5 V. Check Volume is not 0. The line out needs powered speakers, an interface or a headphone amp. |
| The sound distorts while you touch the DAC | FMT (or FLT, DEMP) is floating: solder the DAC's 9-pin row. |
| Nose down plays backwards | The tilt sensor reads the other way round. Flip the sign of `TILT_SIGN` in `firmware/main/main.c`, build and flash. |
| Tilt does nothing | The GY-521 is not answering: the log says "no MPU6050" and the ball at the bottom left of the screen is crossed out. Check its SDA, SCL and 3V3 joints. Without it, it plays forward at Speed max. |
| Screen blank, everything else works | The OLED is not answering, or answers its address but not its commands (one line marginal). Check both I2C joints through the 2 x 4 link, the OLED's pin order, and the top board's 10 µF. The screen comes back by itself once it answers. |
| Screen says NO PSRAM | The SuperMini is not the FH4R2 variant. The firmware needs PSRAM for the take. |
| Screen says BROWNOUT | The supply dipped: a weak USB port or cable, or the speaker amp on the 3.3 V pin. |
| No MIDI port, `lsusb` shows `303a:1001` or `303a:0009` | The board is in download mode. Unplug and plug in again without BOOT. |
| No MIDI port, a serial port instead | The encoder push was held at power-on. Plug in again. If it happens every time, look for a bridge on the encoder's switch pins. |
| Board does not show up for flashing | Hold BOOT, tap RST, release BOOT. Use a data cable, not a charge-only one. |

More, for a working board, in [MANUAL.md](MANUAL.md#when-something-is-wrong). Still stuck? Open an
[issue](https://github.com/willbearfruits/samplooler/issues) with what the screen said, the output of
`python3 tools/sl_midi.py status` and which board you built.
