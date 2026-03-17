#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENV_PY="${OPENPILOT_VENV_PY:-$HOME/openpilot/.venv/bin/python}"
USER_ID="$(id -u)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$USER_ID}"
DISPLAY_NAME="${DISPLAY:-:0}"
XAUTH_FILE="${XAUTHORITY:-}"
UI_BIN="$REPO_ROOT/selfdrive/ui/ui.utm"

if [[ -z "$XAUTH_FILE" ]]; then
  XAUTH_FILE="$(ls -1 "$RUNTIME_DIR"/.mutter-Xwaylandauth.* 2>/dev/null | head -n 1 || true)"
fi

if [[ -z "$XAUTH_FILE" && -f "$HOME/.Xauthority" ]]; then
  XAUTH_FILE="$HOME/.Xauthority"
fi

if [[ -z "$XAUTH_FILE" && -f "$RUNTIME_DIR/ICEauthority" ]]; then
  XAUTH_FILE="$RUNTIME_DIR/ICEauthority"
fi

if [[ ! -x "$UI_BIN" ]]; then
  UI_BIN="$REPO_ROOT/selfdrive/ui/ui"
fi

if [[ -x "$REPO_ROOT/selfdrive/ui/ui" && "$UI_BIN" == "$REPO_ROOT/selfdrive/ui/ui.utm" ]]; then
  cp -f "$REPO_ROOT/selfdrive/ui/ui" "$UI_BIN"
fi

if [[ ! -x "$VENV_PY" ]]; then
  echo "Missing Python venv: $VENV_PY" >&2
  exit 1
fi

export PYTHONPATH="$REPO_ROOT"
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
pkill -9 -f "$REPO_ROOT/selfdrive/ui/ui.utm" || true
pkill -9 -f "$REPO_ROOT/selfdrive/ui/ui" || true
rm -f /dev/shm/msgq* /dev/shm/sem.*msgq* 2>/dev/null || true

export PASSIVE=0
export NOBOARD=1
export SIMULATION=1
export SKIP_FW_QUERY=1
export BLOCK="camerad,loggerd,encoderd,micd,logmessaged,ui"
export PYTHONPATH="$REPO_ROOT/frogpilot/third_party:$REPO_ROOT"

nohup "$VENV_PY" "$REPO_ROOT/system/manager/manager.py" >/tmp/op_manager.log 2>&1 &
sleep 5

# ForceOnroad is cleared on manager start, so it must be applied after manager is up.
export PYTHONPATH="$REPO_ROOT"
"$VENV_PY" - <<'PY'
from openpilot.common.params import Params

p = Params()
pm = Params(memory=True)

p.put_bool("ForceOffroad", False)
p.put_bool("ForceOnroad", True)
pm.put_bool("FrogPilotTogglesUpdated", True)

print("ForceOnroad", p.get_bool("ForceOnroad"))
print("ForceOffroad", p.get_bool("ForceOffroad"))
PY

sleep 2

UI_ENV=(
  DISPLAY="$DISPLAY_NAME"
  XDG_RUNTIME_DIR="$RUNTIME_DIR"
  DBUS_SESSION_BUS_ADDRESS="unix:path=$RUNTIME_DIR/bus"
  QT_QPA_PLATFORM=xcb
)

if [[ -n "$XAUTH_FILE" ]]; then
  UI_ENV+=(XAUTHORITY="$XAUTH_FILE")
fi

nohup env "${UI_ENV[@]}" "$UI_BIN" >/tmp/ui_utm.log 2>&1 &

sleep 2

echo "UI binary: $UI_BIN"
pgrep -af "$REPO_ROOT/system/manager/manager.py|$UI_BIN" || true

export PYTHONPATH="$REPO_ROOT"
"$VENV_PY" - <<'PY'
import json
import cereal.messaging as messaging

sm = messaging.SubMaster(["frogpilotPlan"])
for _ in range(25):
  sm.update(200)

toggles = {}
if sm.updated["frogpilotPlan"]:
  raw = sm["frogpilotPlan"].frogpilotToggles
  toggles = json.loads(raw) if raw else {}

print("frogpilotPlan.force_onroad", toggles.get("force_onroad"))
print("frogpilotPlan.force_offroad", toggles.get("force_offroad"))
PY
