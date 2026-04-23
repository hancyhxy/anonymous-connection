#!/bin/bash
#
# Stage 2 collective wall — one-shot launcher.
#
# Double-click in Finder OR run from Terminal:  ./start.sh
#
# What it does:
#   1. Make sure we're in the script's own folder (so relative paths work
#      no matter where you call it from).
#   2. Create the .venv on first run; reuse it after.
#   3. Make sure flask + qrcode are installed.
#   4. Free port 8000 if a previous run is still squatting on it.
#   5. Start server.py in the foreground so the QR code shows in your
#      Terminal. Ctrl+C to stop.

set -e

cd "$(dirname "$0")"

if [ ! -d ".venv" ]; then
  echo "[start] first run — creating .venv (one-time, ~30s)"
  python3 -m venv .venv
fi

echo "[start] checking dependencies"
./.venv/bin/pip install -q -r requirements.txt

# Free port 8000 if something's still on it from a previous launch.
PORT_PIDS=$(lsof -ti:8000 2>/dev/null || true)
if [ -n "$PORT_PIDS" ]; then
  echo "[start] port 8000 was busy → freeing"
  echo "$PORT_PIDS" | xargs kill -9 2>/dev/null || true
  sleep 1
fi

echo "[start] launching server — Ctrl+C to stop"
exec ./.venv/bin/python3 server.py
