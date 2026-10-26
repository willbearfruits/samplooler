#!/bin/sh
# Install the Samplooler Rig for this user: the Python side (uv), the twin, the viewer, two launchers in
# ~/.local/bin (slrig, slrig-view), a menu entry, and the MCP server for Claude Code (user scope).
#   rig/tools/install.sh              install (again: it updates)
#   rig/tools/install.sh --uninstall  remove the launchers, the menu entry and the MCP registration
set -e
RIG=$(cd "$(dirname "$0")/.." && pwd)
BIN="$HOME/.local/bin"
APPS="$HOME/.local/share/applications"
if [ "$1" = "--uninstall" ]; then
    rm -f "$BIN/slrig" "$BIN/slrig-view" "$APPS/slrig-view.desktop"
    command -v claude >/dev/null && claude mcp remove --scope user slrig 2>/dev/null || true
    echo "removed"; exit 0
fi
cd "$RIG"
uv sync -q
sh twin/build.sh > /dev/null
if [ -f view/CMakeLists.txt ]; then
    cmake -S view -B view/build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
    cmake --build view/build > /dev/null
fi
mkdir -p "$BIN" "$APPS"
printf '#!/bin/sh\nexec "%s/.venv/bin/slrig" "$@"\n' "$RIG" > "$BIN/slrig"
printf '#!/bin/sh\nexec "%s/.venv/bin/slrig" view "$@"\n' "$RIG" > "$BIN/slrig-view"
chmod +x "$BIN/slrig" "$BIN/slrig-view"
cat > "$APPS/slrig-view.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Samplooler Rig
Comment=The Samplooler on USB: every input, every gesture, tuned live
Exec=$BIN/slrig-view
Icon=audio-card
Categories=AudioVideo;Audio;Development;
Terminal=false
DESK
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null || true
if command -v claude >/dev/null; then
    claude mcp list 2>/dev/null | grep -q '^slrig:' || claude mcp add --scope user slrig -- "$BIN/slrig" mcp
fi
echo "installed: slrig, slrig-view, the menu entry, the MCP server 'slrig'"
