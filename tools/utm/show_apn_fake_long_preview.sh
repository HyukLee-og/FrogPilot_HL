#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENV_PY="${OPENPILOT_VENV_PY:-$HOME/openpilot/.venv/bin/python}"
USER_ID="$(id -u)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$USER_ID}"
XAUTH_FILE="$(ls -1 "$RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -n 1 || true)"

if [[ ! -x "$VENV_PY" ]]; then
  echo "Missing Python venv: $VENV_PY" >&2
  exit 1
fi

export PASSIVE=0
export NOBOARD=1
export SIMULATION=1
export SKIP_FW_QUERY=1
export BLOCK="camerad,loggerd,encoderd,micd,logmessaged,ui"
export PYTHONPATH="$REPO_ROOT/frogpilot/third_party:$REPO_ROOT"
export LIBGL_ALWAYS_SOFTWARE=1
export MESA_LOADER_DRIVER_OVERRIDE=llvmpipe

APN_HAZARD_PREVIEW="${FROGPILOT_APN_HAZARD_PREVIEW:-${APN_HAZARD_PREVIEW:-camera:1}}"
APN_SPEED_LIMIT_KPH="${FROGPILOT_APN_SPEED_LIMIT_KPH:-${APN_SPEED_LIMIT_KPH:-80}}"
APN_HAZARD_DISTANCE_M="${FROGPILOT_APN_HAZARD_DISTANCE_M:-${APN_HAZARD_DISTANCE_M:-175}}"
APN_SECTION_ACTIVE_PREVIEW="${FROGPILOT_APN_SECTION_ACTIVE_PREVIEW:-${APN_SECTION_ACTIVE_PREVIEW:-0}}"
APN_VARIABLE_SECTION_PREVIEW="${FROGPILOT_APN_VARIABLE_SECTION_PREVIEW:-${APN_VARIABLE_SECTION_PREVIEW:-0}}"

"$VENV_PY" - <<'PY'
from openpilot.common.params import Params

p = Params()
for src, dst in (
  ("CarParamsPersistent", "CarParams"),
  ("FrogPilotCarParamsPersistent", "FrogPilotCarParams"),
):
  value = p.get(src)
  if value is not None:
    p.put(dst, value)
PY

pkill -9 -f "$REPO_ROOT/system/manager/manager.py" || true
pkill -9 -f "$REPO_ROOT/selfdrive/ui/ui" || true
rm -f /dev/shm/msgq* /dev/shm/sem.*msgq* 2>/dev/null || true

nohup "$VENV_PY" "$REPO_ROOT/system/manager/manager.py" >/tmp/op_manager.log 2>&1 &
sleep 5

export PYTHONPATH="$REPO_ROOT"
"$VENV_PY" - <<'PY'
from openpilot.common.params import Params
import json
import os
import time
import re

p = Params()
pm = Params(memory=True)

hazard_preview = os.environ.get("APN_HAZARD_PREVIEW", "camera:1")
speed_limit_kph = float(os.environ.get("APN_SPEED_LIMIT_KPH", "80"))
hazard_distance_m = float(os.environ.get("APN_HAZARD_DISTANCE_M", "175"))
section_active = os.environ.get("APN_SECTION_ACTIVE_PREVIEW", "0").strip().lower() in ("1", "true", "yes", "on")
variable_section = os.environ.get("APN_VARIABLE_SECTION_PREVIEW", "0").strip().lower() in ("1", "true", "yes", "on")
match = re.search(r":(\d+)$", hazard_preview.strip())
hazard_type = int(match.group(1)) if match else 0

p.put_bool("ForceOffroad", False)
p.put_bool("ForceOnroad", True)

pm.put_bool("APNDataActive", True)
pm.put("APNDataTimestamp", time.time())
pm.put("APNNextHazard", hazard_preview)
pm.put("APNNextHazardDistance", hazard_distance_m)
pm.put("APNNextSpeedLimit", speed_limit_kph / 3.6)
pm.put("APNSpeedLimit", speed_limit_kph / 3.6)
pm.put("APNRoadName", "영동고속도로")
pm.put("APNLastRGData", json.dumps({
  "rgdata": {
    "nSdiType": hazard_type,
    "nSdiPlusType": 0,
    "nSdiDist": hazard_distance_m,
    "nSdiSpeedLimit": speed_limit_kph,
    "nSdiSection": 0,
    "nSdiBlockType": 1 if section_active else 0,
    "nSdiBlockDist": hazard_distance_m if section_active else 0,
    "bSdiBlockSection": section_active,
    "bIsChangeableSpeedType": variable_section or hazard_type in (84, 85),
  }
}))
pm.put("FakeLongDebug", json.dumps({
  "apnEnabled": True,
  "apnOverride": True,
  "apnTarget": speed_limit_kph / 3.6,
  "userSet": 120.0 / 3.6,
  "set": 118.0 / 3.6,
  "commanded": speed_limit_kph / 3.6,
  "target": speed_limit_kph / 3.6,
  "armed": True,
  "paused": False,
  "last": "set",
}))

print("APN fake-long preview ready")
print("ForceOnroad", p.get_bool("ForceOnroad"))
print(pm.get("FakeLongDebug"))
PY

(
  export PYTHONPATH="$REPO_ROOT"
  for _ in $(seq 1 20); do
    "$VENV_PY" - <<'PY'
from openpilot.common.params import Params

p = Params()
p.put_bool("ForceOffroad", False)
p.put_bool("ForceOnroad", True)
PY
    sleep 0.5
  done
) >/tmp/force_onroad_refresh.log 2>&1 &

nohup /usr/bin/env \
  LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  XDG_RUNTIME_DIR="$RUNTIME_DIR" \
  DISPLAY=":0" \
  XAUTHORITY="$XAUTH_FILE" \
  QT_QPA_PLATFORM="xcb" \
  LIBGL_ALWAYS_SOFTWARE="1" \
  GALLIUM_DRIVER="llvmpipe" \
  FROGPILOT_APN_CAMERA_PREVIEW="1" \
  "$REPO_ROOT/selfdrive/ui/ui" >/tmp/apn_fake_long_ui.log 2>&1 &

sleep 2
pgrep -af "$REPO_ROOT/selfdrive/ui/ui|$REPO_ROOT/system/manager/manager.py" || true
tail -n 40 /tmp/apn_fake_long_ui.log 2>/dev/null || true
