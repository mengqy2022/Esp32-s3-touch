#!/usr/bin/env bash
set -euo pipefail
export PYTHONUTF8=1
export PYTHONIOENCODING=utf-8
PORT="${1:-/dev/ttyUSB0}"
idf.py set-target esp32
idf.py build
idf.py -p "$PORT" flash monitor
