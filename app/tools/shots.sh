#!/bin/sh
# Screenshots of the window in its states, taken in headless Chromium with the stand-in (tools/mock.js):
#   tools/shots.sh [out-dir]       (default: app/screenshots/)
# Builds the library from this checkout first (tools/library.py site --local), so the pictures show the
# instruments as they are here.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
OUT="${1:-$HERE/screenshots}"
ROOT="$(mktemp -d)"
trap 'kill $SRV 2>/dev/null; rm -rf "$ROOT"' EXIT
python3 "$REPO/tools/library.py" site --local --out "$ROOT/site" >/dev/null
ln -s "$HERE" "$ROOT/app"
PORT=8793
python3 -m http.server $PORT --bind 127.0.0.1 -d "$ROOT" >/dev/null 2>&1 & SRV=$!
sleep 0.6
mkdir -p "$OUT"
shot() {   # name, query and hash, width, height
  chromium --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 \
    --window-size="${3:-1280},${4:-860}" --virtual-time-budget=6000 \
    --screenshot="$OUT/$1.png" "http://127.0.0.1:$PORT/app/ui/index.html?$2" >/dev/null 2>&1
  echo "$OUT/$1.png"
}
shot library        "scene=one#/"
shot library-update "scene=update#/"
shot instrument     "scene=one#/i/mantra"
shot manual         "scene=one#/i/sing/manual"
shot board          "scene=mantra#/board"
shot board-samplooler "scene=one#/board"
shot install        "scene=two&shot=pick#/"
shot installing     "scene=one&shot=progress#/"
shot installed      "scene=one&shot=done#/"
shot install-error  "scene=one&shot=error#/"
shot build          "scene=download#/build"
shot no-board       "scene=none#/board"
shot settings       "scene=one#/settings"
