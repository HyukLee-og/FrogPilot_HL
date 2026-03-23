#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"
PYTHON_BIN="/usr/local/venv/bin/python"

if [ ! -x "$PYTHON_BIN" ]; then
  PYTHON_BIN="python3"
fi

mkdir -p /data/media/0/codex_logs /data/media/0/apn_bridge
cd "$SCRIPT_DIR"

exec "$PYTHON_BIN" apn_bridge.py >>/data/media/0/codex_logs/apn_bridge.out 2>&1
