#!/bin/sh
# The live window, tested without a screen: a private X server (unablenot's headless Xvfb, no GLX), Mesa's EGL in
# software, the viewer for a few seconds, then its last swapped frame and the X server's own picture of the window.
#   rig/view/tools/live-test.sh SOCKET OUT_DIR [SECONDS]
set -e
VIEW=$(cd "$(dirname "$0")/.." && pwd)
SOCK=$1; OUT=$2; SECS=${3:-6}
XVFB=${SLRIG_XVFB:-$HOME/Projects/audio/unablenot/third_party/_downloads/xvfb/bin/Xvfb}
[ -x "$XVFB" ] || { echo "no Xvfb at $XVFB" >&2; exit 2; }
for n in $(seq 90 140); do [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] || { DISP=$n; break; }; done
"$XVFB" ":$DISP" -screen 0 1600x1000x24 -nolisten tcp -dpi 96 >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null' EXIT
for _ in $(seq 1 50); do [ -e "/tmp/.X11-unix/X$DISP" ] && break; sleep 0.1; done
mkdir -p "$OUT"
env -u WAYLAND_DISPLAY DISPLAY=":$DISP" SLRIG_VIEW_EGL=1 LIBGL_ALWAYS_SOFTWARE=1 \
    __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
    SLRIG_VIEW_SECONDS="$SECS" SLRIG_VIEW_LIVE_SHOT="$OUT/live-frame.png" \
    "$VIEW/build/slrig-view" --socket "$SOCK" --size 1600x1000 &
VPID=$!
sleep $((SECS - 1))
import -display ":$DISP" -window root "$OUT/live-xserver.png" 2>/dev/null || true
wait $VPID
