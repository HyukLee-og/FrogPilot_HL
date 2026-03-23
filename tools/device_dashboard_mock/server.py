#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
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

if str(REPO_ROOT) not in sys.path:
  sys.path.insert(0, str(REPO_ROOT))

MESSAGING_IMPORT_ERROR = ""
try:
  from cereal import messaging
except Exception as exc:
  messaging = None
  MESSAGING_IMPORT_ERROR = str(exc)


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
  SERVICES = ["carState", "selfdriveState", "deviceState", "pandaStates", "peripheralState"]

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
        }
      except Exception as exc:
        self.available = False
        self.error = str(exc)
        return {"available": False, "error": self.error}


LIVE_READER = LiveStateReader()

PARAMS_CACHE_TTL = 1.0
META_CACHE_TTL = 15.0
STATUS_CACHE_TTL = 0.5

GIT_BRANCH = ""
GIT_COMMIT = ""

_PARAMS_CACHE: dict[str, Any] = {"expires_at": 0.0, "items": None, "mapping": None}
_META_CACHE: dict[str, Any] = {"expires_at": 0.0, "value": None}
_STATUS_CACHE: dict[str, Any] = {"expires_at": 0.0, "value": None}


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


def build_apn_status(params: dict[str, dict[str, Any]]) -> dict[str, Any]:
  use_apn = bool(param_value(params, "UseAPN", False))
  bridge_state = read_json_file(APN_STATE_PATH)
  latest_http = read_json_file(APN_HTTP_PATH)

  last_http = bridge_state.get("lastCarrotHttp") if isinstance(bridge_state.get("lastCarrotHttp"), dict) else {}
  if not last_http and latest_http:
    last_http = latest_http

  last_payload = last_http.get("payload") if isinstance(last_http.get("payload"), dict) else {}
  last_received_at = last_http.get("receivedAt") or bridge_state.get("updatedAt") or 0.0
  try:
    last_received_at = float(last_received_at)
  except Exception:
    last_received_at = 0.0

  age_sec = max(0.0, time.time() - last_received_at) if last_received_at else None
  connected = bool(last_http) and age_sec is not None and age_sec < 10.0

  broadcast = bridge_state.get("broadcast") if isinstance(bridge_state.get("broadcast"), dict) else {}
  http_server = bridge_state.get("httpServer") if isinstance(bridge_state.get("httpServer"), dict) else {}

  road_name = last_payload.get("szPosRoadName") or last_payload.get("roadName") or "-"
  sdi_type = last_payload.get("nSdiType", 0)
  sdi_section = last_payload.get("nSdiSection", 0)
  sdi_dist = last_payload.get("nSdiDist", 0)
  road_limit = last_payload.get("nRoadLimitSpeed", 0)
  if isinstance(road_limit, (int, float)) and road_limit > 200:
    road_limit = road_limit / 10.0

  status_label = "CONNECTED" if connected else "WAITING" if use_apn else "OFF"
  debug_payload = {
    "useApn": use_apn,
    "bridgeEnabled": bool(bridge_state.get("enabled", False)),
    "connected": connected,
    "routeActive": bool(broadcast.get("CarrotRouteActive", False)),
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
    "sdiDistanceM": sdi_dist,
    "updatedAt": bridge_state.get("updatedAt", 0),
  }

  return {
    "useApn": use_apn,
    "bridgeEnabled": bool(bridge_state.get("enabled", False)),
    "connected": connected,
    "statusLabel": status_label,
    "routeActive": bool(broadcast.get("CarrotRouteActive", False)),
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
    "sdiDistanceM": sdi_dist if sdi_dist else "-",
    "message": bridge_state.get("message", ""),
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
  weather_times = stats.get("WeatherTimes", {}) or {}
  personality_times = stats.get("PersonalityTimes", {}) or {}
  random_events = stats.get("RandomEvents", {}) or {}
  cruise_speed_times = stats.get("CruiseSpeedTimes", {}) or {}

  model_items = [
    make_stat(name.replace("(Default)", "").strip(), format_time_compact(seconds))
    for name, seconds in sorted(model_times.items(), key=lambda item: float(item[1]), reverse=True)
  ] or [make_stat("기록 없음", "-")]

  weather_items = [
    make_stat(WEATHER_LABELS.get(name, name), format_time_compact(seconds))
    for name, seconds in sorted(weather_times.items(), key=lambda item: float(item[1]), reverse=True)
  ] or [make_stat("기록 없음", "-")]

  personality_items = [
    make_stat(PERSONALITY_LABELS.get(name, name), format_time_compact(seconds))
    for name, seconds in sorted(personality_times.items(), key=lambda item: float(item[1]), reverse=True)
    if name != "Unknown"
  ] or [make_stat("기록 없음", "-")]

  random_event_items = [
    make_stat(RANDOM_EVENT_LABELS.get(name, name), f"{int(count):,}회")
    for name, count in sorted(random_events.items(), key=lambda item: int(item[1]), reverse=True)
  ] or [make_stat("기록 없음", "-")]

  summary = [
    make_stat("긴급 제동 경고", f"{aeb_events:,}회", "warning"),
    make_stat("이번 달 주행 거리", format_distance_value(stats.get("CurrentMonthsMeters", 0), is_metric)),
    make_stat("즐겨찾는 설정 속도", top_cruise_speed(cruise_speed_times, is_metric)),
    make_stat("개입 없이 최장 거리", format_distance_value(stats.get("LongestDistanceWithoutOverride", 0), is_metric), "accent"),
  ]

  sections = [
    {
      "id": "overview",
      "title": "운행 개요",
      "items": [
        make_stat("총 주행 횟수", f"{int(stats.get('FrogPilotDrives', 0)):,}회", "accent"),
        make_stat("총 주행 거리", format_distance_value(stats.get("FrogPilotMeters", 0), is_metric), "accent"),
        make_stat("총 주행 시간", format_time_compact(stats.get("FrogPilotSeconds", 0)), "accent"),
        make_stat("이번 달 주행 거리", format_distance_value(stats.get("CurrentMonthsMeters", 0), is_metric)),
        make_stat("총 추적 시간", format_time_compact(tracked_time)),
        make_stat("즐겨찾는 설정 속도", top_cruise_speed(cruise_speed_times, is_metric)),
      ],
    },
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
    {
      "id": "personalities",
      "title": "주행 성향",
      "items": personality_items,
    },
    {
      "id": "weather",
      "title": "날씨별 주행",
      "items": weather_items,
    },
    {
      "id": "events",
      "title": "이벤트 / 개구리 통계",
      "items": [
        make_stat("긴급 제동 경고", f"{aeb_events:,}회", "warning"),
        make_stat("개구리 점프", f"{int(stats.get('FrogHops', 0)):,}회"),
        make_stat("개구리 짹짹", f"{int(stats.get('FrogChirps', 0)):,}회"),
        make_stat("개구리 끽끽", f"{int(stats.get('FrogSqueaks', 0)):,}회"),
        make_stat("염소 비명", f"{int(stats.get('GoatScreams', 0)):,}회"),
      ] + random_event_items,
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


def write_atomic(path: Path, data: bytes) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  with NamedTemporaryFile(dir=path.parent, delete=False) as tmp:
    tmp.write(data)
    tmp.flush()
    os.fsync(tmp.fileno())
    temp_name = tmp.name
  os.replace(temp_name, path)


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


def build_status() -> dict[str, Any]:
  now = time.monotonic()
  if _STATUS_CACHE["value"] is not None and now < _STATUS_CACHE["expires_at"]:
    return _STATUS_CACHE["value"]

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
  }
  _STATUS_CACHE["value"] = payload
  _STATUS_CACHE["expires_at"] = now + STATUS_CACHE_TTL
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

  def read_json(self) -> dict[str, Any]:
    length = int(self.headers.get("Content-Length", "0"))
    raw = self.rfile.read(length) if length else b"{}"
    return json.loads(raw.decode("utf-8"))

  def do_GET(self) -> None:
    parsed = urlparse(self.path)
    if parsed.path == "/api/meta":
      self.send_json(build_meta())
      return
    if parsed.path == "/api/status":
      self.send_json(build_status())
      return
    if parsed.path == "/api/stats":
      self.send_json(build_stats())
      return
    if parsed.path == "/api/logs":
      query = {}
      if parsed.query:
        for chunk in parsed.query.split("&"):
          if "=" in chunk:
            key, value = chunk.split("=", 1)
            query[key] = value
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
