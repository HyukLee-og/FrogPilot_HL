#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PYTHON_BIN="${PYTHON_BIN:-/home/hyuklee/openpilot/.venv/bin/python}"
DELAY="${1:-3}"

export PYTHONPATH="$ROOT"

set_preview() {
  local value="$1"
  "$PYTHON_BIN" - <<'PY' "$value"
import os
import sys
from openpilot.common.params import Params

value = sys.argv[1]
params = Params()
path = params.get_param_path("UiAlertPreview")
if value:
  with open(path, "w", encoding="utf-8") as f:
    f.write(value)
else:
  try:
    os.remove(path)
  except FileNotFoundError:
    pass
PY
}

echo "Cycling real alert previews with ${DELAY}s delay..."
for preview in changing_lanes blindspot steer_saturated fcw traffic_mode; do
  echo "Showing: ${preview}"
  set_preview "${preview}"
  sleep "${DELAY}"
done

echo "Clearing preview alert"
set_preview ""
