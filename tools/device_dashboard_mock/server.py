#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import Any
from urllib.parse import unquote, urlparse


REPO_ROOT = Path(__file__).resolve().parents[2]
WEB_ROOT = REPO_ROOT / "tools" / "device_dashboard_mock"
PARAMS_HEADER = REPO_ROOT / "common" / "params_keys.h"
DEFAULT_PORT = 8123
KEY_LINE_RE = re.compile(r'^\s*\{"([^"]+)",\s*\{(.*)\}\},?\s*$')
APN_STATE_PATH = Path("/data/media/0/apn_bridge/bridge_state.json") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "apn_bridge" / "bridge_state.json"
APN_HTTP_PATH = Path("/data/media/0/apn_bridge/latest_carrot_http.json") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "apn_bridge" / "latest_carrot_http.json"
APN_LABELS_PATH = Path("/data/media/0/apn_bridge/sdi_labels.json") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "apn_bridge" / "sdi_labels.json"
CAN_LABELS_PATH = Path("/data/media/0/button_sniff/can_labels.json") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "button_sniff" / "can_labels.json"
OFFROAD_MEDIA_ROOT = Path("/data/media/0/device_dashboard") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "device_dashboard"
OFFROAD_SNAPSHOT_DIR = OFFROAD_MEDIA_ROOT / "snapshots"
OFFROAD_LIVE_DIR = OFFROAD_MEDIA_ROOT / "live"

if str(REPO_ROOT) not in sys.path:
  sys.path.insert(0, str(REPO_ROOT))

MESSAGING_IMPORT_ERROR = ""
try:
  from cereal import messaging
except Exception as exc:
  messaging = None
  MESSAGING_IMPORT_ERROR = str(exc)

PARAMS_IMPORT_ERROR = ""
try:
  from openpilot.common.params import Params
except Exception as exc:
  Params = None
  PARAMS_IMPORT_ERROR = str(exc)

CAN_IMPORT_ERROR = ""
try:
  from opendbc.can.parser import CANParser
except Exception as exc:
  CANParser = None
  CAN_IMPORT_ERROR = str(exc)


def memory_params():
  if Params is None:
    return None
  try:
    return Params(memory=True)
  except Exception:
    return None


@dataclass
class ParamMeta:
  key: str
  scope: str
  flags: str
  ptype: str
  default_raw: str | None
  stock_raw: str | None
  tuning_level: int | None


class LiveStateReader:
  SERVICES = ["carState", "selfdriveState", "deviceState", "pandaStates", "peripheralState", "gpsLocation", "gpsLocationExternal"]

  def __init__(self) -> None:
    self.available = messaging is not None
    self.error = MESSAGING_IMPORT_ERROR
    self._lock = threading.Lock()
    self._sm = None
    if self.available:
      try:
        self._sm = messaging.SubMaster(self.SERVICES, ignore_avg_freq=["deviceState", "pandaStates", "peripheralState"])
      except Exception as exc:
        self.available = False
        self.error = str(exc)
        self._sm = None

  def snapshot(self) -> dict[str, Any]:
    if not self.available or self._sm is None:
      return {"available": False, "error": self.error}

    with self._lock:
      try:
        self._sm.update(0)
        return {
          "available": True,
          "seen": dict(self._sm.seen),
          "carState": self._sm["carState"],
          "selfdriveState": self._sm["selfdriveState"],
          "deviceState": self._sm["deviceState"],
          "pandaStates": list(self._sm["pandaStates"]),
          "peripheralState": self._sm["peripheralState"],
          "gpsLocation": self._sm["gpsLocation"],
          "gpsLocationExternal": self._sm["gpsLocationExternal"],
        }
      except Exception as exc:
        self.available = False
        self.error = str(exc)
        return {"available": False, "error": self.error}


LIVE_READER = LiveStateReader()

DBC_NAME = "gm_global_a_powertrain_generated"
CAN_SIGNAL_CANDIDATES = [
  {"id": "decoded:0:608:ClusterSpeed", "src": 0, "address": 608, "message": "SPEED_RELATED", "signal": "ClusterSpeed", "title": "608 ClusterSpeed", "digits": 2},
  {"id": "decoded:0:977:CruiseSetSpeed", "src": 0, "address": 977, "message": "ECMCruiseControl", "signal": "CruiseSetSpeed", "title": "977 CruiseSetSpeed", "digits": 4},
  {"id": "decoded:2:880:ACCSpeedSetpoint", "src": 2, "address": 880, "message": "ASCMActiveCruiseControlStatus", "signal": "ACCSpeedSetpoint", "title": "880 ACCSpeedSetpoint", "digits": 4},
]
CAN_RAW_CANDIDATE_ADDRESSES = {608, 880, 977}


class CanSignalReader:
  SERVICES = ["carState", "can"]

  def __init__(self) -> None:
    self.available = messaging is not None and CANParser is not None
    self.error = MESSAGING_IMPORT_ERROR or CAN_IMPORT_ERROR
    self._lock = threading.Lock()
    self._sm = None
    self._pt_parser = None
    self._cam_parser = None
    if self.available:
      try:
        self._sm = messaging.SubMaster(self.SERVICES)
        self._pt_parser = CANParser(DBC_NAME, [("SPEED_RELATED", float("nan")), ("ECMCruiseControl", float("nan"))], 0)
        self._cam_parser = CANParser(DBC_NAME, [("ASCMActiveCruiseControlStatus", float("nan"))], 2)
      except Exception as exc:
        self.available = False
        self.error = str(exc)
        self._sm = None
        self._pt_parser = None
        self._cam_parser = None

  def snapshot(self) -> dict[str, Any]:
    if not self.available or self._sm is None or self._pt_parser is None or self._cam_parser is None:
      return {"available": False, "error": self.error, "entries": []}

    with self._lock:
      try:
        self._sm.update(0)
        can_msgs = list(self._sm["can"]) if self._sm.updated["can"] else []
        if can_msgs:
          parser_frames = [(int(msg.address), bytes(msg.dat), int(msg.src)) for msg in can_msgs]
          parser_input = [(time.monotonic_ns(), parser_frames)]
          self._pt_parser.update(parser_input)
          self._cam_parser.update(parser_input)

        raw_by_key: dict[str, str] = {}
        for msg in can_msgs:
          src = int(msg.src)
          address = int(msg.address)
          if address in CAN_RAW_CANDIDATE_ADDRESSES:
            raw_by_key[f"raw:{src}:{address}"] = msg.dat.hex()

        entries = []
        for spec in CAN_SIGNAL_CANDIDATES:
          parser = self._pt_parser if spec["src"] == 0 else self._cam_parser
          raw_value = parser.vl[spec["message"]][spec["signal"]]
          rounded = round(float(raw_value), spec["digits"])
          entries.append({
            "id": spec["id"],
            "kind": "decoded",
            "title": spec["title"],
            "src": spec["src"],
            "address": spec["address"],
            "message": spec["message"],
            "signal": spec["signal"],
            "value": rounded,
          })

        for raw_id, dat in sorted(raw_by_key.items()):
          _, src_text, address_text = raw_id.split(":", 2)
          entries.append({
            "id": raw_id,
            "kind": "raw",
            "title": f"{address_text} RAW",
            "src": int(src_text),
            "address": int(address_text),
            "message": "RAW CAN",
            "signal": "dat",
            "value": dat,
          })

        return {
          "available": True,
          "error": "",
          "updatedAt": time.time(),
          "entries": entries,
        }
      except Exception as exc:
        self.available = False
        self.error = str(exc)
        return {"available": False, "error": self.error, "entries": []}


CAN_READER = CanSignalReader()


def summarize_button_events(events: Any) -> str:
  parts: list[str] = []
  try:
    for ev in list(events):
      parts.append(f"{str(ev.type)}:{'1' if bool(ev.pressed) else '0'}")
  except Exception:
    return "-"
  return ", ".join(parts) if parts else "-"


def build_vehicle_status_label_entries(car_state: Any, is_metric: bool) -> list[dict[str, Any]]:
  if car_state is None:
    return []

  cruise_state = getattr(car_state, "cruiseState", None)
  speed_unit = "km/h" if is_metric else "mph"
  speed_factor = 3.6 if is_metric else 2.236936

  def speed_entry(entry_id: str, title: str, value_ms: Any) -> dict[str, Any]:
    try:
      value = round(float(value_ms) * speed_factor, 3)
    except Exception:
      value = "-"
    return {
      "id": entry_id,
      "kind": "status",
      "title": title,
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": speed_unit,
      "value": value,
    }

  entries = [
    speed_entry("status:carState:vEgo", "차량 속도", getattr(car_state, "vEgo", 0.0)),
    speed_entry("status:carState:vEgoCluster", "클러스터 속도", getattr(car_state, "vEgoCluster", 0.0)),
    speed_entry("status:carState:vCruiseCluster", "vCruiseCluster", getattr(car_state, "vCruiseCluster", 0.0)),
    speed_entry("status:cruiseState:speed", "ACC 속도", getattr(cruise_state, "speed", 0.0) if cruise_state is not None else 0.0),
    speed_entry("status:cruiseState:speedCluster", "ACC 클러스터 속도", getattr(cruise_state, "speedCluster", 0.0) if cruise_state is not None else 0.0),
    {
      "id": "status:cruiseState:available",
      "kind": "status",
      "title": "ACC Available",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(cruise_state, "available", False)) if cruise_state is not None else False,
    },
    {
      "id": "status:cruiseState:enabled",
      "kind": "status",
      "title": "ACC Enabled",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(cruise_state, "enabled", False)) if cruise_state is not None else False,
    },
    {
      "id": "status:carState:standstill",
      "kind": "status",
      "title": "차량 정지",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(car_state, "standstill", False)),
    },
    {
      "id": "status:carState:gasPressed",
      "kind": "status",
      "title": "가속 페달",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(car_state, "gasPressed", False)),
    },
    {
      "id": "status:carState:brakePressed",
      "kind": "status",
      "title": "브레이크",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(car_state, "brakePressed", False)),
    },
    {
      "id": "status:carState:leftBlinker",
      "kind": "status",
      "title": "좌측 방향지시등",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(car_state, "leftBlinker", False)),
    },
    {
      "id": "status:carState:rightBlinker",
      "kind": "status",
      "title": "우측 방향지시등",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "bool",
      "value": bool(getattr(car_state, "rightBlinker", False)),
    },
    {
      "id": "status:carState:buttonEvents",
      "kind": "status",
      "title": "버튼 이벤트",
      "src": -1,
      "address": -1,
      "message": "carState",
      "signal": "events",
      "value": summarize_button_events(getattr(car_state, "buttonEvents", [])),
    },
  ]
  return entries

PARAMS_CACHE_TTL = 1.0
META_CACHE_TTL = 15.0
STATUS_CACHE_TTL = 1.0
CAN_DEBUG_CACHE_TTL = 2.0
CAN_DEBUG_STALE_RETENTION_SEC = 600.0
DEBUG_CACHE_TTL = 2.0
OFFROAD_AUTO_SNAPSHOT_STALE_SEC = 60 * 60
OFFROAD_AUTO_WEB_REFRESH_COOLDOWN_SEC = 60 * 60
OFFROAD_AUTO_EVENT_COOLDOWN_SEC = 12.0

GIT_BRANCH = ""
GIT_COMMIT = ""

_PARAMS_CACHE: dict[str, Any] = {"expires_at": 0.0, "items": None, "mapping": None}
_META_CACHE: dict[str, Any] = {"expires_at": 0.0, "value": None}
_STATUS_CACHE: dict[str, Any] = {
  "expires_at": 0.0,
  "value": None,
  "compact_expires_at": 0.0,
  "compact_value": None,
}
_CAN_DEBUG_CACHE: dict[str, Any] = {"expires_at": 0.0, "value": None}
_CAN_DEBUG_OBSERVED: dict[str, dict[str, Any]] = {}
_DEBUG_CACHE: dict[str, Any] = {"expires_at": 0.0, "value": None}
_OFFROAD_CAMERA_LOCK = threading.Lock()
_OFFROAD_AUTO_STATE: dict[str, Any] = {
  "last_ignition": None,
  "last_onroad": None,
  "last_auto_snapshot_at": 0.0,
  "last_auto_reason": "",
  "last_web_refresh_at": 0.0,
  "worker": None,
}


def ensure_offroad_dirs() -> None:
  OFFROAD_SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)
  OFFROAD_LIVE_DIR.mkdir(parents=True, exist_ok=True)


class OffroadLivePreviewManager:
  CAMERA_TO_STREAM = {
    "wide": "wideRoadCameraState",
    "driver": "driverCameraState",
  }

  def __init__(self) -> None:
    self._lock = threading.Lock()
    self._stop_event = threading.Event()
    self._thread: threading.Thread | None = None
    self._state = {
      "active": False,
      "camera": "",
      "startedAt": 0.0,
      "updatedAt": 0.0,
      "error": "",
      "url": "",
    }

  def status(self) -> dict[str, Any]:
    with self._lock:
      return dict(self._state)

  def start(self, camera: str) -> dict[str, Any]:
    normalized = str(camera or "").strip().lower()
    if normalized not in self.CAMERA_TO_STREAM:
      raise ValueError("camera must be one of: wide, driver")

    self.stop()
    ensure_offroad_dirs()

    with self._lock:
      self._stop_event = threading.Event()
      self._state = {
        "active": True,
        "camera": normalized,
        "startedAt": time.time(),
        "updatedAt": 0.0,
        "error": "",
        "url": f"/api/offroad/media/live/{normalized}.jpg",
      }
      self._thread = threading.Thread(target=self._run, args=(normalized, self._stop_event), daemon=True)
      self._thread.start()
      return dict(self._state)

  def stop(self) -> None:
    thread: threading.Thread | None = None
    with self._lock:
      if self._thread is None:
        self._state["active"] = False
        return
      self._stop_event.set()
      thread = self._thread
      self._thread = None

    if thread is not None:
      thread.join(timeout=2.0)

    with self._lock:
      self._state["active"] = False

  def _set_error(self, camera: str, error: str) -> None:
    with self._lock:
      self._state.update({
        "active": False,
        "camera": camera,
        "error": error,
      })
      self._thread = None

  def _set_frame_timestamp(self) -> None:
    with self._lock:
      self._state["updatedAt"] = time.time()

  def _run(self, camera: str, stop_event: threading.Event) -> None:
    camerad_started = False
    try:
      from openpilot.system.manager.process_config import managed_processes
      from openpilot.system.camerad.snapshot import extract_image
      from msgq.visionipc import VisionIpcClient, VisionStreamType
      from PIL import Image
    except Exception as exc:
      self._set_error(camera, f"live preview import failed: {exc}")
      return

    stream_type = {
      "wide": VisionStreamType.VISION_STREAM_WIDE_ROAD,
      "driver": VisionStreamType.VISION_STREAM_DRIVER,
    }[camera]
    output_path = OFFROAD_LIVE_DIR / f"{camera}.jpg"

    with _OFFROAD_CAMERA_LOCK:
      try:
        camerad_running = subprocess.run(["pgrep", "-x", "camerad"], capture_output=True, check=False).returncode == 0
        if not camerad_running:
          managed_processes["camerad"].start()
          camerad_started = True
          time.sleep(2.0)

        client = VisionIpcClient("camerad", stream_type, True)
        if not client.connect(True):
          raise RuntimeError("visionipc connect failed")

        last_write = 0.0
        while not stop_event.is_set():
          frame = client.recv()
          if frame is None:
            time.sleep(0.05)
            continue

          now = time.monotonic()
          if now - last_write < 0.35:
            continue

          image = Image.fromarray(extract_image(frame))
          image.save(output_path, "JPEG", quality=84)
          last_write = now
          self._set_frame_timestamp()
      except Exception as exc:
        self._set_error(camera, str(exc))
      finally:
        if camerad_started:
          try:
            managed_processes["camerad"].stop()
          except Exception:
            pass


OFFROAD_LIVE_PREVIEW = OffroadLivePreviewManager()


def split_top_level(text: str) -> list[str]:
  parts: list[str] = []
  current: list[str] = []
  in_string = False
  escape = False
  depth = 0

  for ch in text:
    if in_string:
      current.append(ch)
      if escape:
        escape = False
      elif ch == "\\":
        escape = True
      elif ch == '"':
        in_string = False
      continue

    if ch == '"':
      in_string = True
      current.append(ch)
      continue

    if ch in "([{":
      depth += 1
    elif ch in ")]}":
      depth = max(0, depth - 1)

    if ch == "," and depth == 0:
      parts.append("".join(current).strip())
      current = []
      continue

    current.append(ch)

  if current:
    parts.append("".join(current).strip())
  return parts


def normalize_literal(token: str | None) -> str | None:
  if token is None:
    return None
  token = token.strip()
  if len(token) >= 2 and token[0] == '"' and token[-1] == '"':
    return token[1:-1]
  return token


def parse_params_header() -> dict[str, ParamMeta]:
  metas: dict[str, ParamMeta] = {}
  scope = "core"

  for raw_line in PARAMS_HEADER.read_text().splitlines():
    if raw_line.strip().startswith("// FrogPilot variables"):
      scope = "frogpilot"
      continue

    match = KEY_LINE_RE.match(raw_line)
    if not match:
      continue

    key, inner = match.groups()
    tokens = split_top_level(inner)
    if len(tokens) < 2:
      continue

    tuning_level = None
    if len(tokens) >= 5:
      try:
        tuning_level = int(tokens[4])
      except ValueError:
        tuning_level = None

    metas[key] = ParamMeta(
      key=key,
      scope=scope,
      flags=tokens[0],
      ptype=tokens[1],
      default_raw=normalize_literal(tokens[2]) if len(tokens) >= 3 else None,
      stock_raw=normalize_literal(tokens[3]) if len(tokens) >= 4 else None,
      tuning_level=tuning_level,
    )

  return metas


PARAMS_META = parse_params_header()

NETWORK_TYPES = {
  0: "none",
  1: "wifi",
  2: "cell2G",
  3: "cell3G",
  4: "cell4G",
  5: "cell5G",
  6: "ethernet",
}

THERMAL_STATUS = {
  0: "green",
  1: "yellow",
  2: "red",
  3: "danger",
}

OPENPILOT_STATES = {
  0: "DISABLED",
  1: "PRE-ENABLED",
  2: "ENABLED",
  3: "SOFT DISABLING",
  4: "OVERRIDING",
}

PERSONALITY = {
  0: "Aggressive",
  1: "Standard",
  2: "Relaxed",
}

GEAR = {
  0: "unknown",
  1: "park",
  2: "drive",
  3: "neutral",
  4: "reverse",
  5: "sport",
  6: "low",
  7: "brake",
  8: "eco",
  9: "manumatic",
}


def maybe_raw(value: Any) -> Any:
  return getattr(value, "raw", value)


def format_speed(value_ms: Any, is_metric: bool) -> str:
  try:
    speed = float(value_ms)
  except Exception:
    return "-"
  if is_metric:
    return f"{speed * 3.6:.0f} km/h"
  return f"{speed * 2.236936:.0f} mph"


def format_voltage(value_mv: Any) -> str:
  try:
    voltage = float(value_mv) / 1000.0
  except Exception:
    return "-"
  return f"{voltage:.2f} V"


def format_percent(value: Any) -> str:
  try:
    return f"{float(value):.0f}%"
  except Exception:
    return "-"


def format_apn_distance(meters: Any) -> str:
  try:
    value = float(meters)
  except Exception:
    return "-"
  if value <= 0:
    return "-"
  if value >= 1000:
    return f"{value / 1000.0:.1f} km"
  return f"{int(round(value))} m"


def format_apn_duration(seconds: Any) -> str:
  try:
    total = int(round(float(seconds)))
  except Exception:
    return "-"
  if total <= 0:
    return "-"
  hours, rem = divmod(total, 3600)
  minutes, seconds = divmod(rem, 60)
  if hours > 0:
    return f"{hours}시간 {minutes}분"
  if minutes > 0:
    return f"{minutes}분 {seconds}초" if seconds else f"{minutes}분"
  return f"{seconds}초"


def format_apn_turn(turn_type: Any, main_text: Any) -> str:
  text = str(main_text or "").strip()
  try:
    code = int(round(float(turn_type)))
  except Exception:
    code = 0
  if text and code > 0:
    return f"{text} ({code})"
  if text:
    return text
  if code > 0:
    return f"Turn {code}"
  return "-"


def max_or_default(values: Any, default: float = 0.0) -> float:
  try:
    seq = list(values)
    return max(seq) if seq else default
  except Exception:
    return default


def run_text_command(command: list[str]) -> tuple[bool, str]:
  try:
    result = subprocess.run(
      command,
      cwd=REPO_ROOT,
      capture_output=True,
      text=True,
      timeout=5,
      check=False,
    )
  except Exception as exc:
    return False, str(exc)

  if result.returncode != 0:
    detail = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
    return False, detail
  return True, result.stdout.rstrip()


LOG_SOURCES: dict[str, dict[str, Any]] = {
  "tmux": {
    "title": "TMUX",
    "subtitle": "현재 tmux pane 출력",
    "command": ["tmux", "capture-pane", "-p", "-S", "-250", "-t", "0"],
  },
  "comma": {
    "title": "COMMA SERVICE",
    "subtitle": "comma.service journal",
    "command": ["journalctl", "-u", "comma.service", "-n", "250", "--no-pager"],
  },
  "dashboard": {
    "title": "DASHBOARD SERVER",
    "subtitle": "device dashboard server log",
    "command": ["tail", "-n", "250", "/data/media/0/codex_logs/device_dashboard_server.out"],
  },
  "apn": {
    "title": "APN BRIDGE",
    "subtitle": "carrotnavi bridge log",
    "command": ["tail", "-n", "250", "/data/media/0/codex_logs/apn_bridge.out"],
  },
  "system": {
    "title": "SYSTEM JOURNAL",
    "subtitle": "최근 시스템 journal",
    "command": ["journalctl", "-n", "250", "--no-pager"],
  },
}

RANDOM_EVENT_LABELS = {
  "accel30": "UwU",
  "accel35": "네스호 괴물 조우",
  "accel40": "1955년 방문",
  "dejaVuCurve": "데자뷔 순간",
  "firefoxSteerSaturated": "Internet Explorer Weeeeeee",
  "hal9000": "HAL 9000 거부",
  "openpilotCrashedRandomEvent": "openpilot 크래시",
  "thisIsFineSteerSaturated": "This Is Fine 순간",
  "toBeContinued": "To Be Continued 순간",
  "vCruise69": "Noice",
  "yourFrogTriedToKillMe": "개구리 암살 시도",
  "youveGotMail": "받은 메일",
}

WEATHER_LABELS = {
  "clear": "맑음",
  "rain": "비",
  "rain_storm": "폭우/폭풍우",
  "snow": "눈",
  "low_visibility": "저시야",
  "unknown": "알 수 없음",
}

PERSONALITY_LABELS = {
  "Aggressive": "공격적",
  "Standard": "표준",
  "Relaxed": "여유형",
  "Unknown": "알 수 없음",
}


def build_logs(source: str) -> dict[str, Any]:
  selected = LOG_SOURCES.get(source) or LOG_SOURCES["tmux"]
  ok, text = run_text_command(selected["command"])
  content = text if text else "(로그 없음)"
  return {
    "source": source if source in LOG_SOURCES else "tmux",
    "title": selected["title"],
    "subtitle": selected["subtitle"],
    "ok": ok,
    "status": "LIVE" if ok else "ERROR",
    "lineCount": len(content.splitlines()) if content else 0,
    "content": content,
    "updatedAt": time.time(),
    "sources": [{"id": key, "title": value["title"]} for key, value in LOG_SOURCES.items()],
  }


def read_frogpilot_stats() -> dict[str, Any]:
  path = params_dir() / "FrogPilotStats"
  if not path.exists():
    return {}
  try:
    return json.loads(path.read_text())
  except Exception:
    return {}


def read_json_file(path: Path) -> dict[str, Any]:
  if not path.exists():
    return {}
  try:
    return json.loads(path.read_text())
  except Exception:
    return {}


def parse_last_gps_position(params: dict[str, dict[str, Any]]) -> dict[str, Any]:
  raw = param_value(params, "LastGPSPosition", "{}")
  if not isinstance(raw, str):
    return {}
  try:
    value = json.loads(raw)
  except Exception:
    return {}
  return value if isinstance(value, dict) else {}


def choose_gps_payload(live: dict[str, Any], params: dict[str, dict[str, Any]]) -> dict[str, Any]:
  seen = live.get("seen", {})
  external_seen = bool(seen.get("gpsLocationExternal"))
  internal_seen = bool(seen.get("gpsLocation"))

  gps = live.get("gpsLocationExternal") if external_seen else live.get("gpsLocation") if internal_seen else None
  latitude = float(getattr(gps, "latitude", 0.0) or 0.0) if gps is not None else 0.0
  longitude = float(getattr(gps, "longitude", 0.0) or 0.0) if gps is not None else 0.0
  bearing = float(getattr(gps, "bearingDeg", 0.0) or 0.0) if gps is not None else 0.0
  speed = float(getattr(gps, "speed", 0.0) or 0.0) if gps is not None else 0.0
  accuracy = float(getattr(gps, "horizontalAccuracy", 0.0) or 0.0) if gps is not None else 0.0

  if latitude == 0.0 and longitude == 0.0:
    last = parse_last_gps_position(params)
    latitude = float(last.get("latitude", 0.0) or 0.0)
    longitude = float(last.get("longitude", 0.0) or 0.0)
    bearing = float(last.get("bearing", 0.0) or 0.0)

  has_fix = latitude != 0.0 or longitude != 0.0
  map_url = f"https://maps.google.com/?q={latitude:.6f},{longitude:.6f}" if has_fix else ""
  return {
    "hasFix": has_fix,
    "latitude": round(latitude, 6) if has_fix else 0.0,
    "longitude": round(longitude, 6) if has_fix else 0.0,
    "bearing": round(bearing, 1) if has_fix else 0.0,
    "speedKph": round(speed * 3.6, 1) if speed else 0.0,
    "accuracyM": round(accuracy, 1) if accuracy else 0.0,
    "mapUrl": map_url,
    "label": f"{latitude:.6f}, {longitude:.6f}" if has_fix else "위치 정보 없음",
  }


def offroad_media_url(kind: str, name: str) -> str:
  return f"/api/offroad/media/{kind}/{name}"


def snapshot_metadata(camera: str) -> dict[str, Any]:
  jpg_path = OFFROAD_SNAPSHOT_DIR / f"{camera}.jpg"
  svg_path = OFFROAD_SNAPSHOT_DIR / f"{camera}.svg"
  path = jpg_path if jpg_path.exists() else svg_path if svg_path.exists() else jpg_path
  if not path.exists():
    return {
      "camera": camera,
      "available": False,
      "url": "",
      "updatedAt": 0.0,
      "label": "캡처 없음",
    }
  updated_at = path.stat().st_mtime
  return {
    "camera": camera,
    "available": True,
    "url": offroad_media_url("snapshots", path.name),
    "updatedAt": updated_at,
    "label": time.strftime("%H:%M:%S", time.localtime(updated_at)),
  }


def snapshots_stale(max_age_sec: float) -> bool:
  newest = 0.0
  for camera in ("wide", "driver"):
    meta = snapshot_metadata(camera)
    if not meta.get("available"):
      return True
    newest = max(newest, float(meta.get("updatedAt") or 0.0))
  if newest <= 0.0:
    return True
  return (time.time() - newest) >= max_age_sec


def build_placeholder_snapshot(camera: str) -> None:
  ensure_offroad_dirs()
  jpg_path = OFFROAD_SNAPSHOT_DIR / f"{camera}.jpg"
  svg_path = OFFROAD_SNAPSHOT_DIR / f"{camera}.svg"
  if jpg_path.exists() or svg_path.exists():
    return

  try:
    from PIL import Image, ImageDraw
  except Exception:
    label = "WIDE SNAPSHOT" if camera == "wide" else "DM SNAPSHOT"
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 1280 720">
<defs>
  <linearGradient id="bg" x1="0" x2="1" y1="0" y2="1">
    <stop offset="0%" stop-color="#08121d"/>
    <stop offset="100%" stop-color="#112638"/>
  </linearGradient>
</defs>
<rect width="1280" height="720" fill="url(#bg)"/>
<rect x="40" y="40" width="1200" height="640" rx="28" fill="#10212f" stroke="#587c9c" stroke-width="4"/>
<text x="120" y="360" fill="#eef4fb" font-size="64" font-family="Arial, Helvetica, sans-serif">{label}</text>
</svg>
"""
    write_atomic(svg_path, svg.encode("utf-8"))
    return

  image = Image.new("RGB", (1280, 720), (10, 22, 34))
  draw = ImageDraw.Draw(image)
  draw.rounded_rectangle((40, 40, 1240, 680), radius=28, fill=(18, 36, 52), outline=(88, 124, 156), width=4)
  label = "WIDE SNAPSHOT" if camera == "wide" else "DM SNAPSHOT"
  draw.text((120, 320), label, fill=(236, 244, 251))
  image.save(jpg_path, "JPEG", quality=84)


def offroad_camera_allowed() -> tuple[bool, str]:
  live = LIVE_READER.snapshot()
  if not live.get("available"):
    return (not Path("/data/params").exists(), "live messaging unavailable")

  device_state = live.get("deviceState")
  panda_states = live.get("pandaStates", [])
  ignition = any(bool(p.ignitionLine or p.ignitionCan) for p in panda_states) if panda_states else False
  onroad = bool(getattr(device_state, "started", False)) if device_state is not None else False
  if ignition or onroad:
    return False, "시동이 켜져 있거나 onroad 상태에서는 offroad 카메라 조회를 시작할 수 없습니다."
  return True, ""


def capture_dashboard_snapshots(camera: str) -> tuple[Any, Any]:
  normalized = str(camera or "").strip().lower()
  if normalized not in {"wide", "driver", "both"}:
    raise ValueError("camera must be one of: wide, driver, both")

  try:
    from openpilot.system.camerad.snapshot import get_snapshots
    from openpilot.system.manager.process_config import managed_processes
    from openpilot.selfdrive.selfdrived.alertmanager import set_offroad_alert
  except Exception as exc:
    raise RuntimeError(f"snapshot import failed: {exc}") from exc

  if Params is None:
    raise RuntimeError("params unavailable")
  params = Params()

  if (not params.get_bool("IsOffroad")) or params.get_bool("IsTakingSnapshot"):
    raise RuntimeError("이미 스냅샷을 찍고 있거나 offroad 상태가 아닙니다.")

  frame = "wideRoadCameraState" if normalized in {"wide", "both"} else None
  front_frame = "driverCameraState" if normalized in {"driver", "both"} else None
  if frame is None and front_frame is None:
    raise RuntimeError("no camera selected")

  camerad_started = False
  params.put_bool("IsTakingSnapshot", True)
  set_offroad_alert("Offroad_IsTakingSnapshot", True)
  time.sleep(2.0)

  try:
    camerad_running = subprocess.run(["pgrep", "camerad"], capture_output=True, check=False).returncode == 0
    if not camerad_running:
      managed_processes["camerad"].start()
      camerad_started = True

    rear, front = get_snapshots(frame, front_frame)
    return rear, front
  finally:
    if camerad_started:
      managed_processes["camerad"].stop()
    params.put_bool("IsTakingSnapshot", False)
    set_offroad_alert("Offroad_IsTakingSnapshot", False)


def take_offroad_snapshot(camera: str) -> dict[str, Any]:
  normalized = str(camera or "").strip().lower()
  if normalized not in {"wide", "driver", "both"}:
    raise ValueError("camera must be one of: wide, driver, both")

  ensure_offroad_dirs()
  OFFROAD_LIVE_PREVIEW.stop()

  if not Path("/data/params").exists():
    targets = ["wide", "driver"] if normalized == "both" else [normalized]
    for target in targets:
      build_placeholder_snapshot(target)
    return {
      "ok": True,
      "mode": "mock",
      "snapshots": {
        "wide": snapshot_metadata("wide"),
        "driver": snapshot_metadata("driver"),
      },
    }

  allowed, reason = offroad_camera_allowed()
  if not allowed:
    raise RuntimeError(reason)

  try:
    from openpilot.system.camerad.snapshot import jpeg_write
  except Exception as exc:
    raise RuntimeError(f"snapshot import failed: {exc}") from exc

  with _OFFROAD_CAMERA_LOCK:
    rear, front = capture_dashboard_snapshots(normalized)
    if normalized in {"wide", "both"} and rear is not None:
      jpeg_write(str(OFFROAD_SNAPSHOT_DIR / "wide.jpg"), rear)
    if normalized in {"driver", "both"} and front is not None:
      jpeg_write(str(OFFROAD_SNAPSHOT_DIR / "driver.jpg"), front)

  return {
    "ok": True,
    "mode": "device",
    "snapshots": {
      "wide": snapshot_metadata("wide"),
      "driver": snapshot_metadata("driver"),
    },
  }


def _auto_snapshot_worker(reason: str) -> None:
  try:
    take_offroad_snapshot("both")
  except Exception:
    pass
  finally:
    _OFFROAD_AUTO_STATE["last_auto_snapshot_at"] = time.time()
    _OFFROAD_AUTO_STATE["last_auto_reason"] = reason
    _OFFROAD_AUTO_STATE["worker"] = None
    invalidate_runtime_caches()


def schedule_offroad_auto_snapshot(reason: str, cooldown_sec: float) -> bool:
  now = time.time()
  worker = _OFFROAD_AUTO_STATE.get("worker")
  if isinstance(worker, threading.Thread) and worker.is_alive():
    return False
  if now - float(_OFFROAD_AUTO_STATE.get("last_auto_snapshot_at") or 0.0) < cooldown_sec:
    return False
  if OFFROAD_LIVE_PREVIEW.status().get("active"):
    return False

  thread = threading.Thread(target=_auto_snapshot_worker, args=(reason,), daemon=True)
  _OFFROAD_AUTO_STATE["worker"] = thread
  thread.start()
  return True


def maybe_schedule_offroad_snapshot(ignition: bool, is_onroad: bool) -> None:
  previous_ignition = _OFFROAD_AUTO_STATE.get("last_ignition")
  previous_onroad = _OFFROAD_AUTO_STATE.get("last_onroad")
  _OFFROAD_AUTO_STATE["last_ignition"] = ignition
  _OFFROAD_AUTO_STATE["last_onroad"] = is_onroad

  if ignition or is_onroad:
    return

  just_turned_off = (previous_ignition is True or previous_onroad is True) and (not ignition and not is_onroad)
  if just_turned_off:
    schedule_offroad_auto_snapshot("ignition_off", 0.0)
    return

  if snapshots_stale(OFFROAD_AUTO_SNAPSHOT_STALE_SEC):
    last_web_refresh_at = float(_OFFROAD_AUTO_STATE.get("last_web_refresh_at") or 0.0)
    if (time.time() - last_web_refresh_at) >= OFFROAD_AUTO_WEB_REFRESH_COOLDOWN_SEC:
      if schedule_offroad_auto_snapshot("web_stale_refresh", OFFROAD_AUTO_WEB_REFRESH_COOLDOWN_SEC):
        _OFFROAD_AUTO_STATE["last_web_refresh_at"] = time.time()


def build_offroad_console(params: dict[str, dict[str, Any]], live: dict[str, Any], car_state: Any, device_state: Any, panda_states: list[Any]) -> dict[str, Any]:
  panda_seen = len(panda_states) > 0
  ignition = any(bool(p.ignitionLine or p.ignitionCan) for p in panda_states) if panda_seen else False
  is_onroad = bool(getattr(device_state, "started", False)) if device_state is not None else bool(param_value(params, "IsOnroad", False))
  maybe_schedule_offroad_snapshot(ignition, is_onroad)

  location = choose_gps_payload(live, params)
  if not Path("/data/params").exists():
    build_placeholder_snapshot("wide")
    build_placeholder_snapshot("driver")

  return {
    "visible": not ignition and not is_onroad,
    "ignition": ignition,
    "onroad": is_onroad,
    "vehicle": {
      "displayName": (param_value(params, "CarModelName", "-") or param_value(params, "CarModel", "-") or "-"),
      "gear": GEAR.get(maybe_raw(getattr(car_state, "gearShifter", 0)), "-") if car_state is not None else "-",
      "hasLiveCarState": car_state is not None,
      "standstill": bool(getattr(car_state, "standstill", False)) if car_state is not None else None,
      "parkingBrake": bool(getattr(car_state, "parkingBrake", False)) if car_state is not None else None,
      "doorOpen": bool(getattr(car_state, "doorOpen", False)) if car_state is not None else None,
      "seatbeltUnlatched": bool(getattr(car_state, "seatbeltUnlatched", False)) if car_state is not None else None,
      "carVoltage": format_voltage(getattr(live.get("peripheralState"), "voltage", 0)) if live.get("peripheralState") is not None else "-",
      "updatedAt": time.time(),
    },
    "location": location,
    "snapshots": {
      "wide": snapshot_metadata("wide"),
      "driver": snapshot_metadata("driver"),
    },
    "livePreview": OFFROAD_LIVE_PREVIEW.status(),
  }


def as_int(value: Any, default: int = 0) -> int:
  try:
    return int(round(float(value)))
  except Exception:
    return default


def as_bool(value: Any) -> bool:
  if isinstance(value, bool):
    return value
  if isinstance(value, (int, float)):
    return value != 0
  if isinstance(value, str):
    return value.strip().lower() in {"1", "true", "yes", "on"}
  return False


def build_apn_signature_fields(packet_payload: dict[str, Any]) -> dict[str, Any]:
  return {
    "sdiType": as_int(packet_payload.get("nSdiType", packet_payload.get("nsdiType", 0))),
    "sdiSection": as_int(packet_payload.get("nSdiSection", packet_payload.get("nsdiSection", 0))),
    "sdiPlusType": as_int(packet_payload.get("nSdiPlusType", packet_payload.get("nsdiPlusType", 0))),
    "sdiBlockType": as_int(packet_payload.get("nSdiBlockType", packet_payload.get("nsdiBlockType", 0))),
    "blockSection": as_bool(packet_payload.get("bSdiBlockSection", packet_payload.get("bsdiBlockSection", False))),
    "changeableSpeed": as_bool(packet_payload.get("bIsChangeableSpeedType", packet_payload.get("bisChangeableSpeedType", False))),
    "limitSignChanged": as_bool(packet_payload.get("bIsLimitSpeedSignChanged", packet_payload.get("bisLimitSpeedSignChanged", False))),
  }


def build_apn_signature(fields: dict[str, Any]) -> str:
  if not any(bool(value) for value in fields.values()):
    return ""
  return "|".join([
    f"T{int(fields.get('sdiType', 0) or 0)}",
    f"S{int(fields.get('sdiSection', 0) or 0)}",
    f"P{int(fields.get('sdiPlusType', 0) or 0)}",
    f"B{int(fields.get('sdiBlockType', 0) or 0)}",
    f"BS{1 if fields.get('blockSection') else 0}",
    f"C{1 if fields.get('changeableSpeed') else 0}",
    f"L{1 if fields.get('limitSignChanged') else 0}",
  ])


def summarize_apn_signature(fields: dict[str, Any]) -> str:
  parts = [
    f"Type {int(fields.get('sdiType', 0) or 0)}",
    f"Section {int(fields.get('sdiSection', 0) or 0)}",
    f"Plus {int(fields.get('sdiPlusType', 0) or 0)}",
    f"Block {int(fields.get('sdiBlockType', 0) or 0)}",
    f"BlockSection {'ON' if fields.get('blockSection') else 'OFF'}",
  ]
  if fields.get("changeableSpeed"):
    parts.append("가변속도")
  if fields.get("limitSignChanged"):
    parts.append("표지변경")
  return " · ".join(parts)


def read_apn_labels() -> dict[str, dict[str, Any]]:
  data = read_json_file(APN_LABELS_PATH)
  return data if isinstance(data, dict) else {}


def list_apn_labels() -> list[dict[str, Any]]:
  labels = read_apn_labels()
  items: list[dict[str, Any]] = []
  for signature, entry in labels.items():
    if not isinstance(entry, dict):
      continue
    fields = entry.get("fields") if isinstance(entry.get("fields"), dict) else {}
    updated_at = float(entry.get("updatedAt", 0.0) or 0.0)
    items.append({
      "signature": signature,
      "label": str(entry.get("label", "")).strip(),
      "fields": fields,
      "summary": summarize_apn_signature(fields),
      "updatedAt": updated_at,
    })
  items.sort(key=lambda item: item["updatedAt"], reverse=True)
  return items


def build_apn_status(params: dict[str, dict[str, Any]]) -> dict[str, Any]:
  local_preview_mode = not Path("/data/media/0").exists()
  use_apn = bool(param_value(params, "UseAPN", False))
  bridge_state = read_json_file(APN_STATE_PATH)
  latest_http = read_json_file(APN_HTTP_PATH)
  bridge_enabled = bool(bridge_state.get("enabled", False))

  if local_preview_mode and not use_apn and (bridge_enabled or latest_http):
    use_apn = True

  last_http = bridge_state.get("lastCarrotHttp") if isinstance(bridge_state.get("lastCarrotHttp"), dict) else {}
  if not last_http and latest_http:
    last_http = latest_http

  last_payload = last_http.get("payload") if isinstance(last_http.get("payload"), dict) else {}
  packet_payload = last_payload.get("rgdata") if isinstance(last_payload.get("rgdata"), dict) else last_payload
  last_received_at = last_http.get("receivedAt") or bridge_state.get("updatedAt") or 0.0
  try:
    last_received_at = float(last_received_at)
  except Exception:
    last_received_at = 0.0

  age_sec = max(0.0, time.time() - last_received_at) if last_received_at else None
  connected = bool(last_http) and age_sec is not None and age_sec < 10.0
  if local_preview_mode and bridge_enabled and last_http:
    connected = True
    age_sec = 0.0
    last_received_at = time.time()

  broadcast = bridge_state.get("broadcast") if isinstance(bridge_state.get("broadcast"), dict) else {}
  http_server = bridge_state.get("httpServer") if isinstance(bridge_state.get("httpServer"), dict) else {}

  road_name = packet_payload.get("szPosRoadName") or packet_payload.get("roadName") or "-"
  sdi_type = packet_payload.get("nSdiType", packet_payload.get("nsdiType", 0))
  sdi_section = packet_payload.get("nSdiSection", packet_payload.get("nsdiSection", 0))
  sdi_plus_type = packet_payload.get("nSdiPlusType", packet_payload.get("nsdiPlusType", 0))
  sdi_block_type = packet_payload.get("nSdiBlockType", packet_payload.get("nsdiBlockType", 0))
  sdi_block_section = as_bool(packet_payload.get("bSdiBlockSection", packet_payload.get("bsdiBlockSection", False)))
  changeable_speed = as_bool(packet_payload.get("bIsChangeableSpeedType", packet_payload.get("bisChangeableSpeedType", False)))
  limit_sign_changed = as_bool(packet_payload.get("bIsLimitSpeedSignChanged", packet_payload.get("bisLimitSpeedSignChanged", False)))
  sdi_dist = packet_payload.get("nSdiDist", packet_payload.get("nsdiDist", 0))
  go_pos_dist = packet_payload.get("nGoPosDist", packet_payload.get("ngoPosDist", 0))
  go_pos_time = packet_payload.get("nGoPosTime", packet_payload.get("ngoPosTime", 0))
  tbt_dist = packet_payload.get("nTBTDist", packet_payload.get("ntbtdist", 0))
  tbt_turn_type = packet_payload.get("nTBTTurnType", packet_payload.get("ntbtturnType", 0))
  tbt_main_text = packet_payload.get("szTBTMainText", packet_payload.get("tbtMainText", ""))
  route_active = bool(
    broadcast.get("CarrotRouteActive", False) or
    float(go_pos_dist or 0) > 0 or
    float(go_pos_time or 0) > 0 or
    float(tbt_dist or 0) > 0 or
    float(tbt_turn_type or 0) > 0 or
    bool(str(tbt_main_text or "").strip())
  )
  road_limit = (
    packet_payload.get("nSdiPlusSpeedLimit", packet_payload.get("nsdiPlusSpeedLimit")) or
    packet_payload.get("nSdiSpeedLimit", packet_payload.get("nsdiSpeedLimit")) or
    packet_payload.get("nRoadLimitSpeed", packet_payload.get("nroadLimitSpeed", 0))
  )
  if isinstance(road_limit, (int, float)) and road_limit > 200:
    road_limit = road_limit / 10.0
  camera_only_active = bool(float(sdi_dist or 0) > 0 and (int(sdi_type or 0) > 0 or int(sdi_section or 0) > 0))
  if not route_active and not camera_only_active:
    road_name = "-"
    road_limit = 0
    sdi_type = 0
    sdi_section = 0
    sdi_dist = 0
  elif not route_active and camera_only_active:
    road_name = "-"
    road_limit = (
      packet_payload.get("nSdiPlusSpeedLimit", packet_payload.get("nsdiPlusSpeedLimit")) or
      packet_payload.get("nSdiSpeedLimit", packet_payload.get("nsdiSpeedLimit")) or
      0
    )
    if isinstance(road_limit, (int, float)) and road_limit > 200:
      road_limit = road_limit / 10.0

  status_label = "CONNECTED" if connected else "WAITING" if use_apn else "OFF"
  message = bridge_state.get("message", "")
  if connected and route_active and not message:
    source = last_http.get("from", "-")
    message = f"Receiving {last_http.get('kind', 'packet')} from {source}"
  elif connected and camera_only_active and not message:
    message = "안전운전 모드 - 과속카메라 정보 수신 중"
  elif connected and not route_active and not message:
    message = "안전운전 모드 감지됨 - 경로안내 데이터 대기 중"
  elif use_apn and not connected and not message:
    message = "Waiting for CarrotNavi packets"

  signature_fields = build_apn_signature_fields(packet_payload)
  current_signature = build_apn_signature(signature_fields)
  current_signature_summary = summarize_apn_signature(signature_fields) if current_signature else "-"
  saved_labels = list_apn_labels()
  current_label = next((item["label"] for item in saved_labels if item["signature"] == current_signature), "")

  debug_payload = {
    "useApn": use_apn,
    "bridgeEnabled": bridge_enabled,
    "connected": connected,
    "routeActive": route_active,
    "cameraOnlyActive": camera_only_active,
    "deviceIp": broadcast.get("ip", "-"),
    "listenPort": broadcast.get("port", 0),
    "httpPort": http_server.get("port", 0),
    "httpPath": http_server.get("path", "-"),
    "broadcastTargets": bridge_state.get("broadcastTargets", []),
    "lastPacketKind": last_http.get("kind", "-"),
    "lastPacketFrom": last_http.get("from", "-"),
    "lastPacketSize": last_http.get("size", 0),
    "lastPacketAgeSec": round(age_sec, 1) if age_sec is not None else None,
    "roadName": road_name,
    "roadLimitKph": road_limit,
    "sdiType": sdi_type,
    "sdiSection": sdi_section,
    "sdiPlusType": sdi_plus_type,
    "sdiBlockType": sdi_block_type,
    "bSdiBlockSection": sdi_block_section,
    "bIsChangeableSpeedType": changeable_speed,
    "bIsLimitSpeedSignChanged": limit_sign_changed,
    "currentSignature": current_signature,
    "currentSignatureSummary": current_signature_summary,
    "currentLabel": current_label or None,
    "sdiDistanceM": sdi_dist,
    "remainingDistanceM": go_pos_dist,
    "remainingDistanceLabel": format_apn_distance(go_pos_dist),
    "remainingTimeSec": go_pos_time,
    "remainingTimeLabel": format_apn_duration(go_pos_time),
    "nextTurnDistanceM": tbt_dist,
    "nextTurnDistanceLabel": format_apn_distance(tbt_dist),
    "nextTurnType": tbt_turn_type,
    "nextTurnMainText": tbt_main_text,
    "nextTurnLabel": format_apn_turn(tbt_turn_type, tbt_main_text),
    "updatedAt": bridge_state.get("updatedAt", 0),
  }

  return {
    "useApn": use_apn,
    "bridgeEnabled": bridge_enabled,
    "connected": connected,
    "statusLabel": status_label,
    "routeActive": route_active,
    "cameraOnlyActive": camera_only_active,
    "deviceIp": broadcast.get("ip", "-"),
    "listenPort": broadcast.get("port", 0),
    "httpPort": http_server.get("port", 0),
    "httpPath": http_server.get("path", "-"),
    "broadcastTargets": bridge_state.get("broadcastTargets", []),
    "lastPacketKind": last_http.get("kind", "-"),
    "lastPacketFrom": last_http.get("from", "-"),
    "lastPacketSize": last_http.get("size", 0),
    "lastPacketAt": last_received_at,
    "lastPacketAgeSec": round(age_sec, 1) if age_sec is not None else None,
    "roadName": road_name,
    "roadLimitKph": road_limit if road_limit else "-",
    "sdiType": sdi_type if sdi_type else "-",
    "sdiSection": sdi_section if sdi_section else "-",
    "sdiPlusType": sdi_plus_type if sdi_plus_type else "-",
    "sdiBlockType": sdi_block_type if sdi_block_type else "-",
    "sdiBlockSection": sdi_block_section,
    "changeableSpeed": changeable_speed,
    "limitSignChanged": limit_sign_changed,
    "currentSignature": current_signature,
    "currentSignatureSummary": current_signature_summary,
    "currentSignatureFields": signature_fields,
    "currentLabel": current_label or "-",
    "savedLabels": saved_labels,
    "sdiDistanceM": sdi_dist if sdi_dist else "-",
    "remainingDistanceLabel": format_apn_distance(go_pos_dist),
    "remainingTimeLabel": format_apn_duration(go_pos_time),
    "nextTurnDistanceLabel": format_apn_distance(tbt_dist),
    "nextTurnLabel": format_apn_turn(tbt_turn_type, tbt_main_text),
    "message": message,
    "debugJson": json.dumps(debug_payload, ensure_ascii=False, indent=2),
  }


def format_time_compact(seconds: Any) -> str:
  try:
    total = int(round(float(seconds)))
  except Exception:
    return "-"
  if total <= 0:
    return "0분"
  days, rem = divmod(total, 86400)
  hours, rem = divmod(rem, 3600)
  minutes, _ = divmod(rem, 60)
  parts = []
  if days:
    parts.append(f"{days}일")
  if hours:
    parts.append(f"{hours}시간")
  if minutes or not parts:
    parts.append(f"{minutes}분")
  return " ".join(parts)


def format_time_percent(seconds: Any, tracked_time: float) -> str:
  try:
    sec = float(seconds)
  except Exception:
    return "-"
  percent = (sec * 100.0 / tracked_time) if tracked_time > 0 else 0.0
  return f"{format_time_compact(sec)} ({percent:.0f}%)"


def format_distance_value(meters: Any, is_metric: bool) -> str:
  try:
    value = float(meters)
  except Exception:
    return "-"
  if is_metric:
    return f"{round(value / 1000.0):,} km"
  return f"{round(value * 0.000621371):,} mi"


def format_speed_value(speed_ms: Any, is_metric: bool) -> str:
  try:
    speed = float(speed_ms)
  except Exception:
    return "-"
  if is_metric:
    return f"{round(speed * 3.6):,} km/h"
  return f"{round(speed * 2.236936):,} mph"


def top_cruise_speed(cruise_speed_times: dict[str, Any], is_metric: bool) -> str:
  if not cruise_speed_times:
    return "-"
  best_speed, best_time = max(cruise_speed_times.items(), key=lambda item: float(item[1]))
  return f"{format_speed_value(best_speed, is_metric)} · {format_time_compact(best_time)}"


def make_stat(label: str, value: str, tone: str = "neutral") -> dict[str, str]:
  return {"label": label, "value": value, "tone": tone}


def build_stats() -> dict[str, Any]:
  params = param_map()
  stats = read_frogpilot_stats()
  is_metric = bool(param_value(params, "IsMetric", False))
  tracked_time = float(stats.get("TrackedTime", 0.0) or 0.0)

  total_events = stats.get("TotalEvents", {}) or {}
  aeb_events = int(total_events.get("stockAeb", 0) or 0) + int(total_events.get("fcw", 0) or 0)

  model_times = stats.get("ModelTimes", {}) or {}
  cruise_speed_times = stats.get("CruiseSpeedTimes", {}) or {}

  model_items = [
    make_stat(name.replace("(Default)", "").strip(), format_time_compact(seconds))
    for name, seconds in sorted(model_times.items(), key=lambda item: float(item[1]), reverse=True)
  ] or [make_stat("기록 없음", "-")]

  summary = [
    make_stat("긴급 제동 경고", f"{aeb_events:,}회", "warning"),
    make_stat("이번 달 주행 거리", format_distance_value(stats.get("CurrentMonthsMeters", 0), is_metric)),
    make_stat("즐겨찾는 설정 속도", top_cruise_speed(cruise_speed_times, is_metric)),
    make_stat("개입 없이 최장 거리", format_distance_value(stats.get("LongestDistanceWithoutOverride", 0), is_metric), "accent"),
  ]

  pulse = {
    "primaryLabel": "총 활성화",
    "primaryValue": f"{int(stats.get('Engages', 0)):,}회",
    "secondaryLabel": "긴급 제동 경고",
    "secondaryValue": f"{aeb_events:,}회",
    "noteLabel": "Overview",
    "noteText": "제어 사용, 개입 기록, 주행 모델 통계를 아래 카드에서 확인합니다.",
  }

  sections = [
    {
      "id": "control",
      "title": "제어 사용",
      "items": [
        make_stat("조향 제어 사용", format_time_percent(stats.get("LateralTime", 0), tracked_time), "success"),
        make_stat("종방향 제어 사용", format_time_percent(stats.get("LongitudinalTime", 0), tracked_time), "success"),
        make_stat("항상 켜진 조향", format_time_percent(stats.get("AOLTime", 0), tracked_time)),
        make_stat("실험 모드", format_time_percent(stats.get("ExperimentalModeTime", 0), tracked_time)),
        make_stat("주간 주행", format_time_percent(stats.get("DayTime", 0), tracked_time)),
        make_stat("야간 주행", format_time_percent(stats.get("NightTime", 0), tracked_time)),
      ],
    },
    {
      "id": "interventions",
      "title": "개입 / 정차",
      "items": [
        make_stat("총 활성화 횟수", f"{int(stats.get('Engages', 0)):,}회", "warning"),
        make_stat("총 해제 횟수", f"{int(stats.get('Disengages', 0)):,}회", "warning"),
        make_stat("총 수동 개입", f"{int(stats.get('Overrides', 0)):,}회", "warning"),
        make_stat("수동 개입 시간", format_time_percent(stats.get("OverrideTime", 0), tracked_time)),
        make_stat("개입 없이 최장 거리", format_distance_value(stats.get("LongestDistanceWithoutOverride", 0), is_metric)),
        make_stat("정차 시간", format_time_percent(stats.get("StandstillTime", 0), tracked_time)),
        make_stat("신호 대기 시간", format_time_percent(stats.get("StopLightTime", 0), tracked_time)),
        make_stat("최고 가속도", f"{float(stats.get('MaxAcceleration', 0) or 0):.2f} m/s²"),
      ],
    },
    {
      "id": "models",
      "title": "주행 모델",
      "items": model_items,
    },
  ]

  return {
    "available": bool(stats),
    "updatedAt": time.time(),
    "trackedTime": format_time_compact(tracked_time),
    "summary": {
      "drives": f"{int(stats.get('FrogPilotDrives', 0)):,}회",
      "distance": format_distance_value(stats.get("FrogPilotMeters", 0), is_metric),
      "time": format_time_compact(stats.get("FrogPilotSeconds", 0)),
      "cards": summary,
    },
    "pulse": pulse,
    "sections": sections,
  }


def current_prefix() -> str:
  return os.environ.get("OPENPILOT_PREFIX", "") or "d"


def params_base_dir() -> Path:
  if os.environ.get("PARAMS_ROOT"):
    return Path(os.environ["PARAMS_ROOT"])
  if Path("/data/params").exists():
    return Path("/data/params")
  suffix = os.environ.get("OPENPILOT_PREFIX", "")
  return Path.home() / f".comma{suffix}" / "params"


def params_dir() -> Path:
  suffix = os.environ.get("OPENPILOT_PREFIX", "")
  base = params_base_dir()
  if base.name == "params" and base.parent.name.startswith(".comma"):
    directory = base / current_prefix()
  else:
    directory = base / current_prefix()
  directory.mkdir(parents=True, exist_ok=True)
  return directory


def git_output(args: list[str], default: str = "-") -> str:
  try:
    result = subprocess.run(
      ["git", *args],
      cwd=REPO_ROOT,
      capture_output=True,
      text=True,
      check=True,
    )
    return result.stdout.strip() or default
  except Exception:
    return default


def init_static_repo_info() -> None:
  global GIT_BRANCH, GIT_COMMIT
  GIT_BRANCH = git_output(["branch", "--show-current"])
  GIT_COMMIT = git_output(["rev-parse", "--short", "HEAD"])


def invalidate_runtime_caches() -> None:
  _PARAMS_CACHE["expires_at"] = 0.0
  _PARAMS_CACHE["items"] = None
  _PARAMS_CACHE["mapping"] = None
  _META_CACHE["expires_at"] = 0.0
  _META_CACHE["value"] = None
  _STATUS_CACHE["expires_at"] = 0.0
  _STATUS_CACHE["value"] = None
  _STATUS_CACHE["compact_expires_at"] = 0.0
  _STATUS_CACHE["compact_value"] = None
  _CAN_DEBUG_CACHE["expires_at"] = 0.0
  _CAN_DEBUG_CACHE["value"] = None
  _DEBUG_CACHE["expires_at"] = 0.0
  _DEBUG_CACHE["value"] = None
  _CAN_DEBUG_OBSERVED.clear()


def write_atomic(path: Path, data: bytes) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  with NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
    tmp.write(data)
    tmp.flush()
    os.fsync(tmp.fileno())
    temp_name = tmp.name
  os.replace(temp_name, path)


def save_apn_label(signature: str, label: str, fields: dict[str, Any]) -> None:
  labels = read_apn_labels()
  labels[signature] = {
    "label": label.strip(),
    "fields": fields,
    "updatedAt": time.time(),
  }
  write_atomic(APN_LABELS_PATH, (json.dumps(labels, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def delete_apn_label(signature: str) -> None:
  labels = read_apn_labels()
  if signature in labels:
    labels.pop(signature, None)
    write_atomic(APN_LABELS_PATH, (json.dumps(labels, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def read_can_labels() -> dict[str, dict[str, Any]]:
  data = read_json_file(CAN_LABELS_PATH)
  return data if isinstance(data, dict) else {}


def list_can_labels() -> list[dict[str, Any]]:
  labels = read_can_labels()
  items: list[dict[str, Any]] = []
  for signal_id, entry in labels.items():
    if not isinstance(entry, dict):
      continue
    updated_at = float(entry.get("updatedAt", 0.0) or 0.0)
    items.append({
      "id": signal_id,
      "label": str(entry.get("label", "")).strip(),
      "meta": entry.get("meta", {}) if isinstance(entry.get("meta"), dict) else {},
      "updatedAt": updated_at,
    })
  items.sort(key=lambda item: item["updatedAt"], reverse=True)
  return items


def save_can_label(signal_id: str, label: str, meta: dict[str, Any]) -> None:
  labels = read_can_labels()
  labels[signal_id] = {
    "label": label.strip(),
    "meta": meta,
    "updatedAt": time.time(),
  }
  write_atomic(CAN_LABELS_PATH, (json.dumps(labels, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def delete_can_label(signal_id: str) -> None:
  labels = read_can_labels()
  if signal_id in labels:
    labels.pop(signal_id, None)
    write_atomic(CAN_LABELS_PATH, (json.dumps(labels, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def safe_utf8(data: bytes) -> tuple[bool, str]:
  try:
    text = data.decode("utf-8")
  except UnicodeDecodeError:
    return False, data.hex()

  if not text:
    return True, ""
  printable = sum(1 for ch in text if ch.isprintable() or ch in "\r\n\t")
  if printable / len(text) < 0.92:
    return False, data.hex()
  return True, text


def payload_for(meta: ParamMeta, raw: bytes | None) -> dict[str, Any]:
  present = raw is not None
  raw = raw or b""
  editor_mode = "text"
  input_kind = "text"
  bool_value = None
  effective_text = meta.default_raw or ""

  if meta.ptype == "BOOL":
    raw_text = raw.decode("utf-8", "ignore") if present else (meta.default_raw or "0")
    bool_value = raw_text == "1"
    effective_text = "1" if bool_value else "0"
    editor_mode = "bool"
    input_kind = "toggle"
  elif present:
    if meta.ptype == "BYTES":
      is_utf8, converted = safe_utf8(raw)
      effective_text = converted
      editor_mode = "text" if is_utf8 else "hex"
      input_kind = "textarea"
    else:
      effective_text = raw.decode("utf-8", "replace")
      if meta.ptype == "JSON":
        try:
          effective_text = json.dumps(json.loads(effective_text), ensure_ascii=False, indent=2)
        except Exception:
          pass
        editor_mode = "json"
        input_kind = "textarea"
      elif meta.ptype in ("INT", "FLOAT"):
        editor_mode = "number"
      else:
        editor_mode = "text"
      input_kind = "textarea" if len(effective_text) > 80 or meta.ptype in ("JSON", "BYTES") else "text"
  else:
    if meta.ptype == "JSON" and effective_text:
      try:
        effective_text = json.dumps(json.loads(effective_text), ensure_ascii=False, indent=2)
      except Exception:
        pass
      editor_mode = "json"
      input_kind = "textarea"
    elif meta.ptype == "BYTES":
      editor_mode = "hex"
      input_kind = "textarea"
    elif meta.ptype in ("INT", "FLOAT"):
      editor_mode = "number"
      input_kind = "text"
    else:
      editor_mode = "text"
      input_kind = "textarea" if len(effective_text) > 80 else "text"

  default_compare = meta.default_raw if meta.default_raw is not None else ""
  is_default = effective_text == default_compare

  return {
    "key": meta.key,
    "scope": meta.scope,
    "flags": meta.flags,
    "type": meta.ptype,
    "defaultValue": meta.default_raw,
    "stockValue": meta.stock_raw,
    "tuningLevel": meta.tuning_level,
    "present": present,
    "rawLength": len(raw),
    "valueText": effective_text,
    "boolValue": bool_value,
    "editorMode": editor_mode,
    "inputKind": input_kind,
    "isDefault": is_default,
  }


def read_all_params() -> list[dict[str, Any]]:
  now = time.monotonic()
  if _PARAMS_CACHE["items"] is not None and now < _PARAMS_CACHE["expires_at"]:
    return _PARAMS_CACHE["items"]

  directory = params_dir()
  payloads = []
  for key in sorted(PARAMS_META):
    path = directory / key
    payloads.append(payload_for(PARAMS_META[key], path.read_bytes() if path.exists() else None))
  _PARAMS_CACHE["items"] = payloads
  _PARAMS_CACHE["mapping"] = {item["key"]: item for item in payloads}
  _PARAMS_CACHE["expires_at"] = now + PARAMS_CACHE_TTL
  return payloads


def current_value(param: dict[str, Any]) -> Any:
  if param["type"] == "BOOL":
    return bool(param["boolValue"])
  if param["type"] == "INT":
    try:
      return int(param["valueText"])
    except Exception:
      return param["valueText"]
  if param["type"] == "FLOAT":
    try:
      return float(param["valueText"])
    except Exception:
      return param["valueText"]
  return param["valueText"]


def param_map() -> dict[str, dict[str, Any]]:
  read_all_params()
  return _PARAMS_CACHE["mapping"] or {}


def param_value(params: dict[str, dict[str, Any]], key: str, fallback: Any = "-") -> Any:
  item = params.get(key)
  if item is None:
    return fallback
  value = current_value(item)
  if value in ("", None):
    return fallback
  return value


def read_param_bytes(key: str) -> bytes | None:
  path = params_dir() / key
  if not path.exists():
    return None
  try:
    return path.read_bytes()
  except Exception:
    return None


def read_memory_param_bytes(key: str) -> bytes | None:
  params = memory_params()
  if params is None:
    return None
  try:
    raw = params.get(key)
    if isinstance(raw, str):
      return raw.encode("utf-8")
    return raw
  except Exception:
    return None


def read_param_text(key: str) -> str:
  raw = read_param_bytes(key)
  if not raw:
    return ""
  try:
    return raw.decode("utf-8", errors="ignore").strip()
  except Exception:
    return ""


def read_memory_param_text(key: str) -> str:
  params = memory_params()
  if params is None:
    return ""
  try:
    raw = params.get(key)
  except Exception:
    return ""
  if not raw:
    return ""
  if isinstance(raw, str):
    return raw.strip()
  try:
    return raw.decode("utf-8", errors="ignore").strip()
  except Exception:
    return ""


def write_memory_param_text(key: str, value: str) -> bool:
  params = memory_params()
  if params is None:
    return False
  try:
    params.put(key, value)
    return True
  except Exception:
    return False


def parse_json_text(raw: str) -> dict[str, Any]:
  text = str(raw or "").strip()
  if not text:
    return {}
  try:
    value = json.loads(text)
  except Exception:
    return {}
  return value if isinstance(value, dict) else {}


def normalize_fake_long_test_body(body: dict[str, Any]) -> dict[str, Any]:
  button = str(body.get("button", "")).strip().lower()
  if button not in {"main", "cancel", "res", "set", "unpress"}:
    raise ValueError("button must be one of: main, cancel, res, set, unpress")

  bus = "camera"

  mode = str(body.get("mode", "tap")).strip().lower()
  mode_aliases = {
    "tap": "tap",
    "press": "press",
    "hold": "press",
    "release": "release",
    "unpress": "release",
  }
  mode = mode_aliases.get(mode, mode)
  if mode not in {"tap", "press", "release"}:
    raise ValueError("mode must be one of: tap, press, release")

  repeats = max(1, min(12, int(body.get("repeats", 1) or 1)))
  hold_frames = max(0, min(40, int(body.get("holdFrames", 0) or 0)))

  return {
    "source": "web-debug",
    "button": button,
    "bus": bus,
    "mode": mode,
    "repeats": repeats,
    "holdFrames": hold_frames,
    "sentAtMs": int(time.time() * 1000),
    "note": str(body.get("note", "") or "").strip(),
  }


def build_meta() -> dict[str, Any]:
  now = time.monotonic()
  if _META_CACHE["value"] is not None and now < _META_CACHE["expires_at"]:
    return _META_CACHE["value"]

  params = read_all_params()
  type_counts: dict[str, int] = {}
  for item in params:
    type_counts[item["type"]] = type_counts.get(item["type"], 0) + 1

  modified_lines = git_output(["status", "--porcelain"], "").splitlines()
  is_device_backend = str(params_dir()).startswith("/data/params/")
  mode_label = "Device Params + Messaging Backend" if is_device_backend and LIVE_READER.available else \
               "Device Params Backend" if is_device_backend else "Local Params Backend"
  payload = {
    "mode": "local-filesystem",
    "modeLabel": mode_label,
    "branch": GIT_BRANCH,
    "commit": GIT_COMMIT,
    "modifiedCount": len([line for line in modified_lines if line.strip()]),
    "paramsRoot": str(params_dir()),
    "prefix": current_prefix(),
    "totalKeys": len(params),
    "presentKeys": sum(1 for item in params if item["present"]),
    "frogpilotKeys": sum(1 for item in params if item["scope"] == "frogpilot"),
    "coreKeys": sum(1 for item in params if item["scope"] == "core"),
    "typeCounts": type_counts,
    "liveMessaging": LIVE_READER.available,
    "liveMessagingError": LIVE_READER.error,
  }
  _META_CACHE["value"] = payload
  _META_CACHE["expires_at"] = now + META_CACHE_TTL
  return payload


def build_status(compact: bool = False) -> dict[str, Any]:
  now = time.monotonic()
  cache_key = "compact_value" if compact else "value"
  cache_expires_key = "compact_expires_at" if compact else "expires_at"
  if _STATUS_CACHE[cache_key] is not None and now < _STATUS_CACHE[cache_expires_key]:
    return _STATUS_CACHE[cache_key]

  params = param_map()
  live = LIVE_READER.snapshot()

  car_make = param_value(params, "CarMake", "-")
  car_model = param_value(params, "CarModel", "-")
  car_model_name = param_value(params, "CarModelName", "-")
  display_name = car_model_name if car_model_name != "-" else f"{car_make} {car_model}".strip()
  is_metric = bool(param_value(params, "IsMetric", False))
  openpilot_enabled_toggle = bool(param_value(params, "OpenpilotEnabledToggle", False))

  car_state = live.get("carState")
  selfdrive_state = live.get("selfdriveState")
  device_state = live.get("deviceState")
  peripheral_state = live.get("peripheralState")
  panda_states = live.get("pandaStates", [])
  seen = live.get("seen", {})

  car_seen = bool(seen.get("carState"))
  selfdrive_seen = bool(seen.get("selfdriveState"))
  device_seen = bool(seen.get("deviceState"))
  peripheral_seen = bool(seen.get("peripheralState"))
  panda_seen = bool(seen.get("pandaStates")) and len(panda_states) > 0

  ignition = any(bool(p.ignitionLine or p.ignitionCan) for p in panda_states) if panda_seen else False
  controls_allowed = any(bool(p.controlsAllowed) for p in panda_states) if panda_seen else False
  safety_param = next((int(p.safetyParam) for p in panda_states), 0) if panda_seen else 0

  is_onroad = bool(getattr(device_state, "started", False)) if device_seen else bool(param_value(params, "IsOnroad", False))
  is_engaged = bool(getattr(selfdrive_state, "active", False)) if selfdrive_seen else bool(param_value(params, "IsEngaged", False))
  selfdrive_enabled = bool(getattr(selfdrive_state, "enabled", False)) if selfdrive_seen else False
  selfdrive_engageable = bool(getattr(selfdrive_state, "engageable", False)) if selfdrive_seen else False
  openpilot_state = OPENPILOT_STATES.get(maybe_raw(getattr(selfdrive_state, "state", 0)), "DISABLED") if selfdrive_seen else \
                    ("ENGAGED" if is_engaged else "ENABLED" if openpilot_enabled_toggle else "DISABLED")

  vehicle_speed = format_speed(getattr(car_state, "vEgoCluster", getattr(car_state, "vEgo", 0.0)), is_metric) if car_seen else "-"
  acc_speed = format_speed(getattr(getattr(car_state, "cruiseState", None), "speedCluster", 0.0), is_metric) if car_seen and bool(getattr(car_state.cruiseState, "available", False)) else "-"
  car_voltage_raw = getattr(peripheral_state, "voltage", 0) if peripheral_seen else 0
  car_voltage = format_voltage(car_voltage_raw) if car_voltage_raw else "-"
  status_bits = [openpilot_state]
  status_bits.append("ONROAD" if is_onroad else "OFFROAD")
  if panda_seen:
    status_bits.append("IGNITION ON" if ignition else "IGNITION OFF")
  if car_seen:
    status_bits.append("CRUISE READY" if bool(car_state.cruiseState.available) else "CRUISE OFF")
  subtitle = " · ".join(status_bits)
  apn = build_apn_status(params)

  payload = {
    "runtime": {
      "vehicleDisplayName": display_name if display_name else "-",
      "subtitle": subtitle,
      "carVoltage": car_voltage,
      "vehicleSpeed": vehicle_speed,
      "accSpeed": acc_speed,
      "openpilotState": openpilot_state,
      "isOnroad": is_onroad,
      "isEngaged": is_engaged,
      "enabledToggle": openpilot_enabled_toggle,
      "updatedAt": time.time(),
    },
    "apn": apn,
    "offroadConsole": build_offroad_console(params, live, car_state if car_seen else None, device_state if device_seen else None, panda_states),
    "device": {
      "branch": GIT_BRANCH,
      "commit": GIT_COMMIT,
      "paramsRoot": str(params_dir()),
      "prefix": current_prefix(),
      "dongleId": param_value(params, "DongleId"),
      "hardwareSerial": param_value(params, "HardwareSerial"),
      "bootCount": param_value(params, "BootCount", 0),
      "language": param_value(params, "LanguageSetting", "main_en"),
      "isMetric": param_value(params, "IsMetric", False),
      "adbEnabled": param_value(params, "AdbEnabled", False),
      "sshEnabled": param_value(params, "SshEnabled", False),
      "recordFront": param_value(params, "RecordFront", False),
      "recordAudio": param_value(params, "RecordAudio", False),
      "disableUpdates": param_value(params, "DisableUpdates", False),
      "deviceType": str(getattr(device_state, "deviceType", "-")) if device_seen else "-",
      "started": bool(getattr(device_state, "started", False)) if device_seen else is_onroad,
      "ignition": ignition,
      "networkType": NETWORK_TYPES.get(maybe_raw(getattr(device_state, "networkType", 0)), "-") if device_seen else "-",
      "thermalStatus": THERMAL_STATUS.get(maybe_raw(getattr(device_state, "thermalStatus", 0)), "-") if device_seen else "-",
      "freeSpacePercent": format_percent(getattr(device_state, "freeSpacePercent", None)) if device_seen else "-",
      "memoryUsagePercent": format_percent(getattr(device_state, "memoryUsagePercent", None)) if device_seen else "-",
      "screenBrightnessPercent": format_percent(getattr(device_state, "screenBrightnessPercent", None)) if device_seen else "-",
      "fanSpeedPercentDesired": format_percent(getattr(device_state, "fanSpeedPercentDesired", None)) if device_seen else "-",
      "maxCpuTempC": f"{max_or_default(getattr(device_state, 'cpuTempC', [])):.0f} C" if device_seen else "-",
      "maxGpuTempC": f"{max_or_default(getattr(device_state, 'gpuTempC', [])):.0f} C" if device_seen else "-",
      "memoryTempC": f"{float(getattr(device_state, 'memoryTempC', 0.0)):.0f} C" if device_seen else "-",
      "controlsAllowed": controls_allowed,
      "safetyParam": safety_param,
    },
    "openpilot": {
      "enabledToggle": openpilot_enabled_toggle,
      "state": openpilot_state,
      "enabled": selfdrive_enabled,
      "active": is_engaged,
      "engageable": selfdrive_engageable,
      "experimentalMode": bool(getattr(selfdrive_state, "experimentalMode", False)) if selfdrive_seen else param_value(params, "ExperimentalMode", False),
      "isOnroad": is_onroad,
      "isOffroad": param_value(params, "IsOffroad", False),
      "isEngaged": is_engaged,
      "isLdwEnabled": param_value(params, "IsLdwEnabled", False),
      "disengageOnAccelerator": param_value(params, "DisengageOnAccelerator", False),
      "longitudinalPersonality": PERSONALITY.get(maybe_raw(getattr(selfdrive_state, "personality", -1)), param_value(params, "LongitudinalPersonality", "-")) if selfdrive_seen else param_value(params, "LongitudinalPersonality", "-"),
      "alertText1": str(getattr(selfdrive_state, "alertText1", "")) if selfdrive_seen else "",
      "alertText2": str(getattr(selfdrive_state, "alertText2", "")) if selfdrive_seen else "",
      "alwaysOnDM": param_value(params, "AlwaysOnDM", False),
      "alwaysOnLateral": param_value(params, "AlwaysOnLateral", False),
      "fakeLong": param_value(params, "FakeLong", False),
      "fakeLongTestUI": param_value(params, "FakeLongTestUI", False),
      "drivingModelName": param_value(params, "DrivingModelName", "-"),
    },
    "vehicle": {
      "carMake": car_make,
      "carModel": car_model,
      "carModelName": car_model_name,
      "carVoltage": car_voltage,
      "canValid": bool(getattr(car_state, "canValid", False)) if car_seen else False,
      "canTimeout": bool(getattr(car_state, "canTimeout", False)) if car_seen else False,
      "accFaulted": bool(getattr(car_state, "accFaulted", False)) if car_seen else False,
      "gear": GEAR.get(maybe_raw(getattr(car_state, "gearShifter", 0)), "-") if car_seen else "-",
      "standstill": bool(getattr(car_state, "standstill", False)) if car_seen else False,
      "vehicleSpeed": vehicle_speed,
      "vehicleSpeedCluster": format_speed(getattr(car_state, "vEgoCluster", 0.0), is_metric) if car_seen else "-",
      "accSpeed": acc_speed,
      "accAvailable": bool(getattr(getattr(car_state, "cruiseState", None), "available", False)) if car_seen else False,
      "accEnabled": bool(getattr(getattr(car_state, "cruiseState", None), "enabled", False)) if car_seen else False,
      "gasPressed": bool(getattr(car_state, "gasPressed", False)) if car_seen else False,
      "brakePressed": bool(getattr(car_state, "brakePressed", False)) if car_seen else False,
      "steeringPressed": bool(getattr(car_state, "steeringPressed", False)) if car_seen else False,
      "leftBlinker": bool(getattr(car_state, "leftBlinker", False)) if car_seen else False,
      "rightBlinker": bool(getattr(car_state, "rightBlinker", False)) if car_seen else False,
      "forceFingerprint": param_value(params, "ForceFingerprint", False),
      "disableOpenpilotLongitudinal": param_value(params, "DisableOpenpilotLongitudinal", False),
      "distanceButtonControl": param_value(params, "DistanceButtonControl", "-"),
      "clusterOffset": param_value(params, "ClusterOffset", "-"),
      "drivingModel": param_value(params, "DrivingModel", "-"),
      "drivingModelVersion": param_value(params, "DrivingModelVersion", "-"),
    },
    "canDebug": {
      "available": False,
      "error": "CAN 디버그는 별도 요청으로 갱신됩니다.",
      "updatedAt": time.time(),
      "entries": [],
      "savedLabels": [],
    },
  }
  if compact:
    payload = {
      "runtime": payload["runtime"],
      "apn": payload["apn"],
      "offroadConsole": payload["offroadConsole"],
      "device": {},
      "openpilot": {},
      "vehicle": {},
      "canDebug": payload["canDebug"],
    }
  _STATUS_CACHE[cache_key] = payload
  _STATUS_CACHE[cache_expires_key] = now + STATUS_CACHE_TTL
  return payload


def build_can_debug() -> dict[str, Any]:
  now = time.monotonic()
  if _CAN_DEBUG_CACHE["value"] is not None and now < _CAN_DEBUG_CACHE["expires_at"]:
    return _CAN_DEBUG_CACHE["value"]

  params = param_map()
  live = LIVE_READER.snapshot()
  is_metric = bool(param_value(params, "IsMetric", False))
  car_state = live.get("carState")
  seen = live.get("seen", {})
  car_seen = bool(seen.get("carState"))

  vehicle_status_entries = build_vehicle_status_label_entries(car_state if car_seen else None, is_metric)
  can_snapshot = CAN_READER.snapshot()
  can_labels = {item["id"]: item for item in list_can_labels()}
  observed_now = time.time()

  current_entries = vehicle_status_entries + list(can_snapshot.get("entries", []))
  current_ids = set()
  for entry in current_entries:
    entry_id = entry["id"]
    current_ids.add(entry_id)
    _CAN_DEBUG_OBSERVED[entry_id] = {
      **entry,
      "live": True,
      "lastSeenAt": observed_now,
    }

  stale_cutoff = observed_now - CAN_DEBUG_STALE_RETENTION_SEC
  expired_ids = [
    entry_id for entry_id, cached in _CAN_DEBUG_OBSERVED.items()
    if float(cached.get("lastSeenAt", 0.0) or 0.0) < stale_cutoff
  ]
  for entry_id in expired_ids:
    _CAN_DEBUG_OBSERVED.pop(entry_id, None)

  entries = []
  for entry_id, entry in _CAN_DEBUG_OBSERVED.items():
    live_now = entry_id in current_ids
    saved = can_labels.get(entry_id, {})
    last_seen_at = float(entry.get("lastSeenAt", 0.0) or 0.0)
    entries.append({
      **entry,
      "live": live_now,
      "lastSeenAt": last_seen_at,
      "lastSeenAgeSec": max(0.0, observed_now - last_seen_at) if last_seen_at > 0.0 else None,
      "label": saved.get("label", "-"),
      "updatedAt": saved.get("updatedAt", 0.0),
    })

  entries.sort(key=lambda item: (
    0 if item.get("live") else 1,
    0 if item.get("kind") == "status" else 1,
    str(item.get("title", "")),
  ))

  payload = {
    "available": bool(entries),
    "error": "" if entries else can_snapshot.get("error", LIVE_READER.error),
    "updatedAt": can_snapshot.get("updatedAt", time.time()),
    "entries": entries,
    "savedLabels": list(can_labels.values()),
  }
  _CAN_DEBUG_CACHE["value"] = payload
  _CAN_DEBUG_CACHE["expires_at"] = now + CAN_DEBUG_CACHE_TTL
  return payload


def build_debug() -> dict[str, Any]:
  now = time.monotonic()
  if _DEBUG_CACHE["value"] is not None and now < _DEBUG_CACHE["expires_at"]:
    return _DEBUG_CACHE["value"]

  params = param_map()
  live = LIVE_READER.snapshot()
  is_metric = bool(param_value(params, "IsMetric", False))

  car_state = live.get("carState")
  selfdrive_state = live.get("selfdriveState")
  panda_states = live.get("pandaStates", [])
  seen = live.get("seen", {})

  car_seen = bool(seen.get("carState"))
  selfdrive_seen = bool(seen.get("selfdriveState"))
  panda_seen = bool(seen.get("pandaStates")) and len(panda_states) > 0

  fake_long_debug_raw = read_memory_param_text("FakeLongDebug")
  fake_long_debug = parse_json_text(fake_long_debug_raw)
  pending_test_raw = read_memory_param_text("FakeLongTestButton")
  pending_test = parse_json_text(pending_test_raw)

  cruise_available_debug = bool(fake_long_debug.get("cruiseAvailable", False))
  cruise_enabled_debug = bool(fake_long_debug.get("cruiseEnabled", False))

  safety_param = next((int(p.safetyParam) for p in panda_states), 0) if panda_seen else 0
  controls_allowed = any(bool(p.controlsAllowed) for p in panda_states) if panda_seen else False
  ignition = any(bool(p.ignitionLine or p.ignitionCan) for p in panda_states) if panda_seen else False

  if not bool(param_value(params, "FakeLongTestUI", False)):
    safety_hint = "FakeLongTestUI가 꺼져 있으면 웹 디버그 버튼이 차량으로 전송되지 않습니다."
    safety_hint_level = "warn"
  elif not bool(getattr(live.get("deviceState"), "started", False)) if live.get("available") else True:
    safety_hint = "오프로드 상태에서는 차량 반응을 확인할 수 없습니다."
    safety_hint_level = "neutral"
  elif not cruise_available_debug:
    safety_hint = "현재 ACC AVAILABLE 상태가 아니어서 차량이 SET/RES/MAIN 버튼을 무시할 수 있습니다."
    safety_hint_level = "warn"
  elif not cruise_enabled_debug:
    safety_hint = "GM safety상 SET/RES/CANCEL/UNPRESS는 ACC가 이미 engaged 상태일 때만 통과합니다. 먼저 실차 핸들로 ACC를 활성화한 뒤 시험하세요."
    safety_hint_level = "warn"
  else:
    safety_hint = "현재 조건상 웹 명령은 carcontroller까지 들어갑니다. 여기서도 반응이 없으면 차량이 synthetic 버튼 프레임을 거부하는 단계입니다."
    safety_hint_level = "success"

  payload = {
    "updatedAt": time.time(),
    "runtime": {
      "onroad": bool(getattr(live.get("deviceState"), "started", False)) if live.get("available") else False,
      "engaged": bool(getattr(selfdrive_state, "active", False)) if selfdrive_seen else False,
      "enabled": bool(getattr(selfdrive_state, "enabled", False)) if selfdrive_seen else False,
      "controlsAllowed": controls_allowed,
      "ignition": ignition,
      "safetyParam": safety_param,
      "vehicleSpeed": format_speed(getattr(car_state, "vEgoCluster", getattr(car_state, "vEgo", 0.0)), is_metric) if car_seen else "-",
      "accSpeed": format_speed(getattr(getattr(car_state, "cruiseState", None), "speedCluster", 0.0), is_metric) if car_seen and bool(getattr(car_state.cruiseState, "available", False)) else "-",
      "accAvailable": bool(getattr(getattr(car_state, "cruiseState", None), "available", False)) if car_seen else False,
      "accEnabled": bool(getattr(getattr(car_state, "cruiseState", None), "enabled", False)) if car_seen else False,
      "fakeLong": bool(param_value(params, "FakeLong", False)),
      "fakeLongTestUI": bool(param_value(params, "FakeLongTestUI", False)),
      "apnFakeLong": bool(param_value(params, "APNFakeLong", False)),
    },
    "safetyHint": {
      "message": safety_hint,
      "level": safety_hint_level,
      "cruiseAvailable": cruise_available_debug,
      "cruiseEnabled": cruise_enabled_debug,
    },
    "fakeLongDebug": fake_long_debug,
    "fakeLongDebugRaw": fake_long_debug_raw or "{}",
    "pendingTest": pending_test,
    "pendingTestRaw": pending_test_raw,
  }

  _DEBUG_CACHE["value"] = payload
  _DEBUG_CACHE["expires_at"] = now + DEBUG_CACHE_TTL
  return payload


def encode_value(meta: ParamMeta, body: dict[str, Any]) -> bytes:
  value = body.get("value")
  mode = body.get("mode", "text")

  if meta.ptype == "BOOL":
    if isinstance(value, str):
      value = value.lower() in ("1", "true", "on", "yes")
    return b"1" if bool(value) else b"0"

  if value is None:
    raise ValueError("missing value")

  if meta.ptype == "INT":
    return str(int(value)).encode("utf-8")
  if meta.ptype == "FLOAT":
    return str(float(value)).encode("utf-8")
  if meta.ptype == "JSON":
    normalized = json.dumps(json.loads(str(value)), ensure_ascii=False, indent=2)
    return normalized.encode("utf-8")
  if meta.ptype == "BYTES":
    if mode == "hex":
      cleaned = re.sub(r"\s+", "", str(value))
      return bytes.fromhex(cleaned) if cleaned else b""
    return str(value).encode("utf-8")
  return str(value).encode("utf-8")


class DashboardHandler(SimpleHTTPRequestHandler):
  def __init__(self, *args: Any, **kwargs: Any):
    super().__init__(*args, directory=str(WEB_ROOT), **kwargs)

  def log_message(self, format: str, *args: Any) -> None:
    print("[dashboard]", format % args)

  def send_json(self, payload: Any, status: int = 200) -> None:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    self.send_response(status)
    self.send_header("Content-Type", "application/json; charset=utf-8")
    self.send_header("Content-Length", str(len(body)))
    self.send_header("Cache-Control", "no-store")
    self.end_headers()
    self.wfile.write(body)

  def send_binary(self, path: Path, content_type: str) -> None:
    if not path.exists():
      self.send_json({"error": "not found"}, status=404)
      return
    body = path.read_bytes()
    self.send_response(200)
    self.send_header("Content-Type", content_type)
    self.send_header("Content-Length", str(len(body)))
    self.send_header("Cache-Control", "no-store")
    self.end_headers()
    self.wfile.write(body)

  def read_json(self) -> dict[str, Any]:
    length = int(self.headers.get("Content-Length", "0"))
    raw = self.rfile.read(length) if length else b"{}"
    return json.loads(raw.decode("utf-8"))

  def do_GET(self) -> None:
    parsed = urlparse(self.path)
    query: dict[str, str] = {}
    if parsed.query:
      for chunk in parsed.query.split("&"):
        if "=" in chunk:
          key, value = chunk.split("=", 1)
          query[key] = value
    if parsed.path == "/api/meta":
      self.send_json(build_meta())
      return
    if parsed.path == "/api/apn-labels":
      self.send_json({"labels": list_apn_labels()})
      return
    if parsed.path == "/api/can-labels":
      self.send_json({"labels": list_can_labels()})
      return
    if parsed.path == "/api/status":
      detail = unquote(query.get("detail", "full")).lower()
      self.send_json(build_status(compact=detail != "full"))
      return
    if parsed.path == "/api/can-debug":
      self.send_json(build_can_debug())
      return
    if parsed.path == "/api/debug":
      self.send_json(build_debug())
      return
    if parsed.path == "/api/offroad/live/status":
      self.send_json({"livePreview": OFFROAD_LIVE_PREVIEW.status()})
      return
    if parsed.path.startswith("/api/offroad/media/"):
      suffix = parsed.path.removeprefix("/api/offroad/media/").strip("/")
      if suffix.startswith("snapshots/"):
        target = OFFROAD_SNAPSHOT_DIR / suffix.removeprefix("snapshots/")
      elif suffix.startswith("live/"):
        target = OFFROAD_LIVE_DIR / suffix.removeprefix("live/")
      else:
        self.send_json({"error": "unsupported media path"}, status=404)
        return
      content_type = "image/svg+xml" if target.suffix.lower() == ".svg" else "image/jpeg"
      self.send_binary(target, content_type)
      return
    if parsed.path == "/api/stats":
      self.send_json(build_stats())
      return
    if parsed.path == "/api/logs":
      self.send_json(build_logs(unquote(query.get("source", "tmux"))))
      return
    if parsed.path == "/api/params":
      self.send_json({"params": read_all_params()})
      return
    if parsed.path == "/":
      self.path = "/index.html"
    super().do_GET()

  def do_POST(self) -> None:
    parsed = urlparse(self.path)
    if parsed.path == "/api/apn-labels":
      try:
        payload = self.read_json()
        signature = str(payload.get("signature", "")).strip()
        label = str(payload.get("label", "")).strip()
        fields = payload.get("fields") if isinstance(payload.get("fields"), dict) else {}
        if not signature:
          self.send_json({"error": "signature is required"}, status=400)
          return
        if not label:
          self.send_json({"error": "label is required"}, status=400)
          return
        save_apn_label(signature, label, fields)
        self.send_json({"ok": True, "labels": list_apn_labels()})
      except Exception as exc:
        self.send_json({"error": str(exc)}, status=400)
      return

    if parsed.path == "/api/can-labels":
      try:
        payload = self.read_json()
        signal_id = str(payload.get("id", "")).strip()
        label = str(payload.get("label", "")).strip()
        meta = payload.get("meta") if isinstance(payload.get("meta"), dict) else {}
        if not signal_id:
          self.send_json({"error": "id is required"}, status=400)
          return
        if not label:
          self.send_json({"error": "label is required"}, status=400)
          return
        save_can_label(signal_id, label, meta)
        self.send_json({"ok": True, "labels": list_can_labels()})
      except Exception as exc:
        self.send_json({"error": str(exc)}, status=400)
      return

    if parsed.path == "/api/debug/fake-long-test":
      try:
        payload = normalize_fake_long_test_body(self.read_json())
        encoded = json.dumps(payload, ensure_ascii=False, separators=(",", ":"))
        if not write_memory_param_text("FakeLongTestButton", encoded):
          path = params_dir() / "FakeLongTestButton"
          write_atomic(path, encoded.encode("utf-8"))
        invalidate_runtime_caches()
        self.send_json({"ok": True, "command": payload, "debug": build_debug()})
      except Exception as exc:
        self.send_json({"error": str(exc)}, status=400)
      return

    if parsed.path == "/api/offroad/snapshot":
      try:
        payload = self.read_json()
        self.send_json(take_offroad_snapshot(str(payload.get("camera", "both") or "both")))
        invalidate_runtime_caches()
      except Exception as exc:
        self.send_json({"error": str(exc)}, status=400)
      return

    if parsed.path == "/api/offroad/live/start":
      try:
        if not Path("/data/params").exists():
          raise RuntimeError("실시간 카메라 조회는 기기에서만 지원합니다.")
        payload = self.read_json()
        camera = str(payload.get("camera", "wide") or "wide")
        allowed, reason = offroad_camera_allowed()
        if not allowed:
          raise RuntimeError(reason)
        self.send_json({"ok": True, "livePreview": OFFROAD_LIVE_PREVIEW.start(camera)})
        invalidate_runtime_caches()
      except Exception as exc:
        self.send_json({"error": str(exc)}, status=400)
      return

    if parsed.path == "/api/offroad/live/stop":
      OFFROAD_LIVE_PREVIEW.stop()
      invalidate_runtime_caches()
      self.send_json({"ok": True, "livePreview": OFFROAD_LIVE_PREVIEW.status()})
      return

    if not parsed.path.startswith("/api/params/"):
      self.send_json({"error": "unsupported endpoint"}, status=404)
      return

    key = unquote(parsed.path.rsplit("/", 1)[-1])
    meta = PARAMS_META.get(key)
    if meta is None:
      self.send_json({"error": f"unknown key: {key}"}, status=404)
      return

    try:
      encoded = encode_value(meta, self.read_json())
      path = params_dir() / key
      write_atomic(path, encoded)
      invalidate_runtime_caches()
      self.send_json({"ok": True, "param": payload_for(meta, path.read_bytes())})
    except Exception as exc:
      self.send_json({"error": str(exc)}, status=400)

  def do_DELETE(self) -> None:
    parsed = urlparse(self.path)
    if parsed.path.startswith("/api/apn-labels/"):
      signature = unquote(parsed.path.rsplit("/", 1)[-1]).strip()
      if not signature:
        self.send_json({"error": "signature is required"}, status=400)
        return
      delete_apn_label(signature)
      self.send_json({"ok": True, "labels": list_apn_labels()})
      return

    if parsed.path.startswith("/api/can-labels/"):
      signal_id = unquote(parsed.path.rsplit("/", 1)[-1]).strip()
      if not signal_id:
        self.send_json({"error": "id is required"}, status=400)
        return
      delete_can_label(signal_id)
      self.send_json({"ok": True, "labels": list_can_labels()})
      return

    if not parsed.path.startswith("/api/params/"):
      self.send_json({"error": "unsupported endpoint"}, status=404)
      return

    key = unquote(parsed.path.rsplit("/", 1)[-1])
    meta = PARAMS_META.get(key)
    if meta is None:
      self.send_json({"error": f"unknown key: {key}"}, status=404)
      return

    path = params_dir() / key
    if path.exists():
      path.unlink()
    invalidate_runtime_caches()
    self.send_json({"ok": True, "param": payload_for(meta, None)})


def main() -> None:
  init_static_repo_info()
  port = int(os.environ.get("DEVICE_DASHBOARD_PORT", str(DEFAULT_PORT)))
  default_host = "0.0.0.0" if Path("/data/params").exists() else "127.0.0.1"
  host = os.environ.get("DEVICE_DASHBOARD_HOST", default_host)
  server = ThreadingHTTPServer((host, port), DashboardHandler)
  print(f"Serving dashboard at http://{host}:{port}")
  server.serve_forever()


if __name__ == "__main__":
  main()
