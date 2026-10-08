#!/bin/bash
# cards.sh <work dir>: title card, end card and caption overlays (transparent PNGs), 1920x1080.
# The texts state facts about the boards: keep them true (ERC/DRC results, sizes, parts).
set -e
C=$1/cards; mkdir -p "$C"
F=/usr/share/fonts/TTF
BOLD=$F/Roboto-Bold.ttf; REG=$F/Roboto-Regular.ttf; LIGHT=$F/Roboto-Light.ttf; MONO=$F/JetBrainsMonoNerdFont-Regular.ttf
BG='#030304'; W=1920; H=1080

magick -size ${W}x${H} xc:"$BG" \
  -font $BOLD -pointsize 150 -fill '#f2f2f2' -gravity center -annotate +0-70 'SAMPLOOLER' \
  -font $LIGHT -pointsize 46 -fill '#9aa3ad' -annotate +0+50 'a tilt-controlled looper  ·  ESP32-S3 hardware, rev B' \
  -font $MONO -pointsize 28 -fill '#5f6b76' -annotate +0+130 'two designs: the Stack and the Mini' \
  "$C/title.png"

magick -size ${W}x${H} xc:"$BG" \
  -font $BOLD -pointsize 64 -fill '#f2f2f2' -gravity center -annotate +0-170 'Generated, routed and checked in Python' \
  -font $MONO -pointsize 34 -fill '#9aa3ad' -annotate +0-60 'KiCad 10  ·  Freerouting  ·  3 boards  ·  2 layers' \
  -font $MONO -pointsize 34 -fill '#7fd08a' -annotate +0+10 'ERC 0   DRC 0   every net routed   mechanical model: 0 collisions' \
  -font $REG -pointsize 34 -fill '#9aa3ad' -annotate +0+90 'pogo pins to the 20 underside pads  ·  every spare GPIO broken out' \
  -font $REG -pointsize 34 -fill '#9aa3ad' -annotate +0+145 'Qwiic  ·  amp header  ·  JLCPCB-ready Gerbers, BOM, CPL' \
  -font $LIGHT -pointsize 30 -fill '#5f6b76' -annotate +0+260 'willbearfruits / samplooler' \
  "$C/end.png"

cap() {   # name, headline, detail: text on a soft dark band at the bottom
  magick -size ${W}x${H} xc:none \
    \( -size ${W}x230 gradient:'rgba(3,3,4,0)'-'rgba(3,3,4,0.85)' \) -gravity south -composite \
    -font $BOLD -pointsize 52 -fill '#f2f2f2' -gravity southwest -annotate +90+110 "$2" \
    -font $REG -pointsize 32 -fill '#b8c0c8' -annotate +92+60 "$3" \
    "$C/$1.png"
}
cap stack1 'The Stack' 'two 94 × 46 mm boards, 11 mm apart on M3 standoffs, joined by a 2×4 socket'
cap stack2 'Bottom board: every module' 'SuperMini on pogo pins, PCM5102 DAC, MPU-6050, joystick, encoder, amp header, breakouts'
cap mini1 'The Mini' 'one 82 × 56 mm board, no joystick: OLED, mic, encoder and a record button on the front'
cap mini2 'Mini, back: the modules' 'SuperMini with pogo pins, DAC, IMU upside down, amp header; solder order worked out by the build'
cap plan1 'Every net routed' 'fixed escape under the SuperMini, the breakouts ordered to match, GND pours stitched on both layers'
