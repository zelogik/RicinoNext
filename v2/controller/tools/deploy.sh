#!/usr/bin/env bash
# Install / update RicinoNext on an ESP32 over USB (first install; afterwards the OTA page is enough).
# Needs: MicroPython on the board (ESP32_GENERIC-OTA variant for firmware OTA), and mpremote
#        (installed, or run through uvx when only uv is available)
# Usage: tools/deploy.sh [port]        e.g. tools/deploy.sh /dev/ttyUSB0   (default: auto)
set -euo pipefail
PORT=${1:-auto}
cd "$(dirname "$0")/.."
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# web pages are stored gzipped (served as is, ~3x less flash and transfer)
for f in data/*.html; do gzip -9 -n -c "$f" > "$BUILD/$(basename "$f").gz"; done
# modules precompiled to .mpy: compiling the .py on the board takes so much RAM that the Wi-Fi
# driver may not get its buffers ("WiFi Out of Memory"). mpy-cross must match the board's MicroPython.
MPY_CROSS=${MPY_CROSS:-"uvx --from mpy-cross==1.29.* mpy-cross"}
for f in ricino/*.py; do
  [ "$f" = ricino/pc.py ] && continue           # PC only (argparse, settings file)
  $MPY_CROSS -o "$BUILD/$(basename "$f" .py).mpy" "$f"
done

if command -v mpremote >/dev/null; then MP=(mpremote connect "$PORT"); else MP=(uvx mpremote connect "$PORT"); fi
"${MP[@]}" exec "
import os
for d in ('ricino', 'data'):
    try: os.mkdir(d)
    except OSError: pass
for f in ('data/index.html', 'data/system.html'):   # plain copies would be shadowed by .gz anyway
    try: os.remove(f)
    except OSError: pass
for f in os.listdir('ricino'):                       # .py modules are replaced by .mpy
    if f.endswith('.py'): os.remove('ricino/' + f)
"
"${MP[@]}" cp main.py :main.py
for f in "$BUILD"/*.mpy; do "${MP[@]}" cp "$f" ":ricino/$(basename "$f")"; done
for f in "$BUILD"/*.gz; do "${MP[@]}" cp "$f" ":data/$(basename "$f")"; done
"${MP[@]}" reset
echo "Done. Without Wi-Fi settings the board opens the access point RicinoNext-XXXX: join it, then http://192.168.4.1/system.html"
