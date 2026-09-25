#!/bin/bash
# assemble.sh <work dir>: title, the stack shot, the Mini shot, the copper, the end card, joined with
# crossfades -> <work dir>/samplooler-showcase.mp4
set -e
cd "$1"
mkdir -p seg
FPS=24
X264="-c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -r $FPS"
magick -size 1920x1080 radial-gradient:'#1b1f26'-'#050608' seg/bg.png
ffmpeg -y -v error -loop 1 -t 2.5 -i cards/title.png -vf "fps=$FPS,format=yuv420p" $X264 seg/1-title.mp4
shot() {   # frames-prefix out caption1 a1 b1 caption2 a2 b2 (seconds)
  ffmpeg -y -v error -loop 1 -i seg/bg.png -framerate $FPS -i frames/$1_%04d.png \
    -loop 1 -i cards/$3.png -loop 1 -i cards/$6.png -filter_complex "
      [0][1]overlay=shortest=1[v];
      [2]format=rgba,fade=in:st=$4:d=0.5:alpha=1,fade=out:st=$5:d=0.5:alpha=1[c1];
      [3]format=rgba,fade=in:st=$7:d=0.5:alpha=1,fade=out:st=$8:d=0.5:alpha=1[c2];
      [v][c1]overlay=shortest=1:enable='between(t,$4,$5+0.5)'[v1];
      [v1][c2]overlay=shortest=1:enable='between(t,$7,$8+0.5)',format=yuv420p[out]" \
    -map "[out]" $X264 seg/$2.mp4
}
shot stack 2-stack stack1 0.3 4.0 stack2 4.6 9.9
shot mini 3-mini mini1 0.3 3.6 mini2 4.2 9.0
for b in stack-bottom stack-top mini; do
  ffmpeg -y -v error -i copper/$b.png -vf "scale=-2:980,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=#06080b,
      zoompan=z='1+0.0016*on':d=53:x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':s=1920x1080:fps=$FPS,format=yuv420p" \
    $X264 seg/cu-$b.mp4
done
ffmpeg -y -v error -i seg/cu-stack-bottom.mp4 -i seg/cu-stack-top.mp4 -i seg/cu-mini.mp4 -loop 1 -i cards/plan1.png \
  -filter_complex "[0][1]xfade=transition=fade:duration=0.4:offset=1.8[a];[a][2]xfade=transition=fade:duration=0.4:offset=3.6[b];
                   [3]format=rgba,fade=in:st=0.2:d=0.5:alpha=1[c];[b][c]overlay=shortest=1,format=yuv420p[out]" \
  -map "[out]" $X264 seg/4-copper.mp4
ffmpeg -y -v error -loop 1 -t 3.5 -i cards/end.png -vf "fps=$FPS,format=yuv420p" $X264 seg/5-end.mp4
dur() { ffprobe -v error -show_entries format=duration -of csv=p=0 "$1"; }
T1=$(dur seg/1-title.mp4); T2=$(dur seg/2-stack.mp4); T3=$(dur seg/3-mini.mp4); T4=$(dur seg/4-copper.mp4)
X=0.5
read O1 O2 O3 O4 < <(python3 -c "
t = [$T1, $T2, $T3, $T4]; s = 0; o = []
for d in t:
    s += d - $X; o.append(round(s, 3))
print(*o)")
ffmpeg -y -v error -i seg/1-title.mp4 -i seg/2-stack.mp4 -i seg/3-mini.mp4 -i seg/4-copper.mp4 -i seg/5-end.mp4 -filter_complex "
  [0][1]xfade=transition=fade:duration=$X:offset=$O1[a];
  [a][2]xfade=transition=fade:duration=$X:offset=$O2[b];
  [b][3]xfade=transition=fade:duration=$X:offset=$O3[c];
  [c][4]xfade=transition=fade:duration=$X:offset=$O4,format=yuv420p[out]" \
  -map "[out]" $X264 -movflags +faststart samplooler-showcase.mp4
echo "$1/samplooler-showcase.mp4 $(dur samplooler-showcase.mp4) s"
