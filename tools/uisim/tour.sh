#!/bin/sh
# The screen tour for the README and the page: builds tour.c against the real ui.c, renders every frame
# tinted like the two-colour panel, and writes out/tour.mp4 and out/tour.gif. Needs ffmpeg and python3.
set -e
cd "$(dirname "$0")"
P=../../firmware/main
rm -rf out/tour out/tour-png && mkdir -p out/tour out/tour-png
gcc -std=gnu17 -O1 -I. -I$P -o tour tour.c stubs.c $P/ui.c $P/oled.c $P/font_data.c -lm
./tour
python3 tour_frames.py out/tour out/tour-png
ffmpeg -loglevel error -y -framerate 20 -i out/tour-png/%04d.png -c:v libx264 -pix_fmt yuv420p -crf 18 -movflags +faststart out/tour.mp4
ffmpeg -loglevel error -y -framerate 20 -i out/tour-png/%04d.png -vf "scale=iw/2:ih/2:flags=neighbor,split[a][b];[a]palettegen=max_colors=8[p];[b][p]paletteuse=dither=none" -loop 0 out/tour.gif
ls -la out/tour.mp4 out/tour.gif
