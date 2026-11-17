#!/bin/sh
# The instruments' rev E clips for the page: each instrument's simulator (instruments/<id>/tools/sim/run.sh) writes
# out/<id>-<clip>.gif and a .wav of the same stretch; this joins each pair into out/inst/<id>-<clip>.mp4 (the screen
# with its sound) and copies the simulator's sheet of every screen. Run the simulators first. Needs ffmpeg.
set -e
cd "$(dirname "$0")"
I=../../instruments
mkdir -p out/inst
for pair in huff:rev-e wreck:rev-e mantra:rev-e band:rev-e pixel:rev-e-write pixel:breakdown sing:rev-e; do
    id=${pair%%:*}; clip=${pair#*:}
    g=$I/$id/tools/sim/out/$id-$clip.gif; w=$I/$id/tools/sim/out/$id-$clip.wav
    if [ ! -f "$g" ] || [ ! -f "$w" ]; then echo "$id-$clip: no clip yet (run instruments/$id/tools/sim/run.sh)"; continue; fi
    s=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$w")
    # the GIF keeps one frame for a run of equal ones, with its time: fps=25 puts them back; the last one is held to the sound's end
    ffmpeg -loglevel error -y -i "$g" -i "$w" -vf "fps=25,tpad=stop_mode=clone:stop_duration=2,scale=768:384:flags=neighbor" -t "$s" \
        -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 128k -movflags +faststart out/inst/$id-$clip.mp4
    cp $I/$id/tools/sim/out/$id.png out/inst/$id-screens.png
    echo "out/inst/$id-$clip.mp4  $s s"
done
