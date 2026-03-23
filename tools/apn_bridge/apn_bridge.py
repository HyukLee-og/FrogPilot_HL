#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
APN_LOG_ROOT = Path("/data/media/0/apn_bridge") if Path("/data/media/0").exists() else REPO_ROOT / ".codex_tmp" / "apn_bridge"
APN_LOG_ROOT.mkdir(parents=True, exist_ok=True)
LATEST_CARROT_DATA_PATH = APN_LOG_ROOT / "latest_carrot_data.json"
LATEST_CARROT_HTTP_PATH = APN_LOG_ROOT / "latest_carrot_http.json"
BRIDGE_STATE_PATH = APN_LOG_ROOT / "bridge_state.json"
BROADCAST_PORT = int(os.getenv("APN_BROADCAST_PORT", "7705"))
LISTEN_PORT = int(os.getenv("APN_LISTEN_PORT", "7706"))
HTTP_PORT = int(os.getenv("APN_HTTP_PORT", "7713"))
HTTP_API_PATH = os.getenv("APN_HTTP_API_PATH", "/api/navi/10.18.1.3503")
BROADCAST_INTERVAL = 1.0
COMPAT_REFRESH_INTERVAL = 10.0
DISCOVERY_REFRESH_INTERVAL = 15.0

if str(REPO_ROOT) not in sys.path:
  sys.path.insert(0, str(REPO_ROOT))

from tools.apn_bridge.apn_compat import refresh_apn_compat_files

try:
  from common.params import Params as NativeParams
except Exception:
  NativeParams = None


class FallbackParams:
  _store: dict[str, str] = {}

  def __init__(self, *args: Any, **kwargs: Any) -> None:
    del args, kwargs

  def get_bool(self, key: str) -> bool:
    if key in self._store:
      return self._store[key].strip().lower() in {"1", "true", "yes", "on"}
    env_key = f"APN_PARAM_{key}".upper()
    return os.getenv(env_key, "").strip().lower() in {"1", "true", "yes", "on"}

  def get(self, key: str, block: bool = False, encoding: str | None = None) -> bytes | str | None:
    del block
    value = self._store.get(key)
    if value is None:
      value = os.getenv(f"APN_PARAM_{key}".upper())
    if value is None:
      return None
    if encoding:
      return value
    return value.encode("utf-8")

  def put(self, key: str, value: str) -> None:
    self._store[key] = value

  def put_bool(self, key: str, value: bool) -> None:
    self.put(key, "1" if value else "0")

  def remove(self, key: str) -> None:
    self._store.pop(key, None)


def make_params(memory: bool = False):
  if NativeParams is not None:
    return NativeParams(memory=memory)
  return FallbackParams()

MESSAGING_IMPORT_ERROR = ""
try:
  from cereal import messaging
except Exception as exc:
  messaging = None
  MESSAGING_IMPORT_ERROR = str(exc)


def safe_write_json(path: Path, payload: dict[str, Any]) -> None:
  path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")


def parse_broadcast_targets() -> list[tuple[str, int]]:
  raw = os.getenv("APN_BROADCAST_TARGETS", "").strip()
  if not raw:
    return [("255.255.255.255", BROADCAST_PORT)]

  targets: list[tuple[str, int]] = []
  for chunk in raw.split(","):
    item = chunk.strip()
    if not item:
      continue

    host, _, port_text = item.partition(":")
    port = BROADCAST_PORT
    if port_text:
      try:
        port = int(port_text)
      except ValueError:
        continue
    targets.append((host.strip(), port))

  return targets or [("255.255.255.255", BROADCAST_PORT)]


def decode_carrot_payload(raw: bytes) -> tuple[dict[str, Any] | list[Any] | None, str]:
  text = raw.decode("utf-8", errors="replace").strip()
  if not text:
    return None, ""

  try:
    return json.loads(text), text
  except json.JSONDecodeError:
    pass

  start = text.find("{")
  end = text.rfind("}")
  if start != -1 and end != -1 and end >= start:
    candidate = text[start : end + 1]
    try:
      return json.loads(candidate), candidate
    except json.JSONDecodeError:
      pass

  return None, text


class CarrotHttpServer(ThreadingHTTPServer):
  def __init__(self, server_address: tuple[str, int], bridge: "APNBridge") -> None:
    super().__init__(server_address, CarrotHttpRequestHandler)
    self.bridge = bridge


class CarrotHttpRequestHandler(BaseHTTPRequestHandler):
  server: CarrotHttpServer

  def log_message(self, format: str, *args: object) -> None:
    return

  def _write_json(self, code: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    self.send_response(code)
    self.send_header("Content-Type", "application/json; charset=utf-8")
    self.send_header("Content-Length", str(len(body)))
    self.end_headers()
    self.wfile.write(body)

  def do_GET(self) -> None:
    if self.path == "/healthz":
      self._write_json(200, {"ok": True, "path": self.path})
      return

    self._write_json(404, {"ok": False, "error": "not_found", "path": self.path})

  def do_POST(self) -> None:
    if self.path != HTTP_API_PATH:
      self._write_json(404, {"ok": False, "error": "not_found", "path": self.path})
      return

    length = int(self.headers.get("Content-Length", "0") or "0")
    raw = self.rfile.read(length) if length > 0 else b""
    payload, normalized = decode_carrot_payload(raw)
    self.server.bridge.handle_incoming_http(self.client_address[0], self.path, raw, payload, normalized)
    self._write_json(200, {"ok": True, "received": len(raw)})


class APNBridge:
  def __init__(self) -> None:
    self.params = make_params()
    self.params_memory = make_params(memory=True)
    self.last_carrot_payload: dict[str, Any] = {}
    self.last_carrot_http_payload: dict[str, Any] = {}
    self.last_rgdata_fields: dict[str, Any] = {}
    self.last_carrot_time = 0.0
    self.last_compat_refresh = 0.0
    self.last_broadcast = 0.0
    self.last_ip_refresh = 0.0
    self.last_idle_write = 0.0
    self.last_memory_payload: dict[str, Any] = {}
    self.device_ip = ""
    self.broadcast_targets = parse_broadcast_targets()

    self.broadcast_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    self.broadcast_socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    self.broadcast_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    self.listen_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    self.listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    self.listen_socket.bind(("", LISTEN_PORT))
    self.listen_socket.setblocking(False)

    self.submaster = None
    self.messaging_error = MESSAGING_IMPORT_ERROR
    if messaging is not None:
      try:
        self.submaster = messaging.SubMaster(
          ["deviceState", "carState", "selfdriveState", "pandaStates", "navInstruction", "mapdOut"],
          ignore_avg_freq=["deviceState", "pandaStates", "navInstruction", "mapdOut"],
        )
      except Exception as exc:
        self.messaging_error = str(exc)
        self.submaster = None

    self.http_server = CarrotHttpServer(("", HTTP_PORT), self)
    self.http_thread = threading.Thread(target=self.http_server.serve_forever, name="carrot-http", daemon=True)
    self.http_thread.start()

  def enabled(self) -> bool:
    if os.getenv("APN_FORCE_ENABLE", "").strip() in {"1", "true", "TRUE", "yes", "YES", "on", "ON"}:
      return True

    try:
      return self.params.get_bool("UseAPN")
    except Exception:
      return False

  @staticmethod
  def _first_value(payload: dict[str, Any], *keys: str, default: Any = None) -> Any:
    for key in keys:
      value = payload.get(key)
      if value not in (None, "", []):
        return value
    return default

  @staticmethod
  def _float_value(value: Any, default: float = 0.0) -> float:
    try:
      return float(value)
    except (TypeError, ValueError):
      return default

  @staticmethod
  def _normalize_speed_limit(value: Any) -> float:
    limit = APNBridge._float_value(value)
    if limit <= 0:
      return 0.0
    if limit > 200:
      limit /= 10.0
    return limit / 3.6

  @staticmethod
  def _describe_hazard(sdi_type: int, sdi_section: int) -> str:
    if sdi_type <= 0 and sdi_section <= 0:
      return ""
    if sdi_section > 0:
      return f"section_camera:{sdi_type or 0}"
    return f"camera:{sdi_type}"

  def _normalize_rgdata(self, payload: dict[str, Any], received_at: float) -> dict[str, Any]:
    rgdata = payload.get("rgdata") if isinstance(payload.get("rgdata"), dict) else payload

    road_name = str(self._first_value(rgdata, "szPosRoadName", default="") or "")
    latitude = self._float_value(self._first_value(rgdata, "vpPosPointLat", default=0.0))
    longitude = self._float_value(self._first_value(rgdata, "vpPosPointLon", default=0.0))

    speed_limit = self._normalize_speed_limit(self._first_value(rgdata, "nRoadLimitSpeed", "nroadLimitSpeed", default=0))
    next_speed_limit = self._normalize_speed_limit(self._first_value(rgdata, "nSdiPlusSpeedLimit", "nsdiPlusSpeedLimit", "nSdiSpeedLimit", "nsdiSpeedLimit", default=0))
    next_speed_distance = self._float_value(self._first_value(rgdata, "nSdiPlusDist", "nsdiPlusDist", "nSdiDist", "nsdiDist", default=0.0))

    sdi_type = int(round(self._float_value(self._first_value(rgdata, "nSdiType", "nsdiType", default=0))))
    sdi_section = int(round(self._float_value(self._first_value(rgdata, "nSdiSection", "nsdiSection", default=0))))
    next_hazard_distance = self._float_value(self._first_value(rgdata, "nSdiDist", "nsdiDist", default=0.0))

    timestamp_ms = self._float_value(payload.get("timestamp_ms"), default=0.0)
    timestamp = (timestamp_ms / 1000.0) if timestamp_ms > 0 else received_at

    return {
      "kind": "rgdata",
      "receivedAt": received_at,
      "timestamp": timestamp,
      "roadName": road_name,
      "latitude": latitude,
      "longitude": longitude,
      "speedLimit": speed_limit,
      "nextSpeedLimit": next_speed_limit,
      "nextSpeedLimitDistance": next_speed_distance if next_speed_limit > 0 else 0.0,
      "nextHazard": self._describe_hazard(sdi_type, sdi_section),
      "nextHazardDistance": next_hazard_distance,
      "raw": payload,
    }

  def _publish_memory_state(self) -> None:
    now = time.time()
    fresh = bool(self.last_rgdata_fields) and (now - self.last_rgdata_fields.get("receivedAt", 0.0) < 10.0)
    active_payload = self.last_rgdata_fields if fresh else {}

    memory_payload = {
      "APNDataActive": fresh,
      "APNDataKind": str(active_payload.get("kind", "")),
      "APNDataTimestamp": str(active_payload.get("timestamp", 0.0)),
      "APNLastRGData": json.dumps(active_payload.get("raw", {}), ensure_ascii=False) if active_payload else "",
      "APNLatitude": str(active_payload.get("latitude", 0.0)),
      "APNLongitude": str(active_payload.get("longitude", 0.0)),
      "APNNextHazard": str(active_payload.get("nextHazard", "")),
      "APNNextHazardDistance": str(active_payload.get("nextHazardDistance", 0.0)),
      "APNNextSpeedLimit": str(active_payload.get("nextSpeedLimit", 0.0)),
      "APNNextSpeedLimitDistance": str(active_payload.get("nextSpeedLimitDistance", 0.0)),
      "APNRoadName": str(active_payload.get("roadName", "")),
      "APNSpeedLimit": str(active_payload.get("speedLimit", 0.0)),
    }

    if memory_payload == self.last_memory_payload:
      return

    self.last_memory_payload = memory_payload
    try:
      self.params_memory.put_bool("APNDataActive", memory_payload["APNDataActive"])
      for key, value in memory_payload.items():
        if key == "APNDataActive":
          continue
        self.params_memory.put(key, value)
    except Exception:
      pass

  def refresh_device_ip(self, force: bool = False) -> str:
    override = os.getenv("APN_DEVICE_IP", "").strip()
    if override:
      self.device_ip = override
      return self.device_ip

    now = time.monotonic()
    if not force and self.device_ip and now - self.last_ip_refresh < DISCOVERY_REFRESH_INTERVAL:
      return self.device_ip

    self.last_ip_refresh = now
    commands = [
      "ip -4 route get 1.1.1.1 | awk '{for (i=1;i<=NF;i++) if ($i==\"src\") {print $(i+1); exit}}'",
      "hostname -I | awk '{print $1}'",
    ]
    for command in commands:
      try:
        value = subprocess.check_output(["/bin/sh", "-lc", command], text=True, timeout=2).strip()
      except Exception:
        continue
      if value and value != "127.0.0.1":
        self.device_ip = value
        return self.device_ip

    self.device_ip = ""
    return self.device_ip

  def update_messages(self) -> dict[str, Any]:
    state: dict[str, Any] = {
      "started": self.params.get_bool("IsOnroad"),
      "active": self.params.get_bool("IsEngaged"),
      "xState": 0,
      "vEgoKph": 0,
      "vCruiseKph": 0,
      "messagingAvailable": self.submaster is not None,
      "messagingError": self.messaging_error,
    }

    if self.submaster is None:
      return state

    try:
      self.submaster.update(0)
      device_state = self.submaster["deviceState"]
      car_state = self.submaster["carState"]
      selfdrive_state = self.submaster["selfdriveState"]

      state["started"] = bool(getattr(device_state, "started", state["started"]))
      state["active"] = bool(getattr(selfdrive_state, "active", state["active"]))
      state["xState"] = int(getattr(selfdrive_state, "state", 0))
      state["vEgoKph"] = int(round(float(getattr(car_state, "vEgo", 0.0)) * 3.6))

      cruise_state = getattr(car_state, "cruiseState", None)
      if cruise_state is not None:
        speed_value = getattr(cruise_state, "speedCluster", getattr(cruise_state, "speed", 0.0))
        state["vCruiseKph"] = int(round(float(speed_value) * 3.6))
    except Exception as exc:
      state["messagingAvailable"] = False
      state["messagingError"] = str(exc)

    return state

  def handle_incoming_packets(self) -> None:
    while True:
      try:
        data, address = self.listen_socket.recvfrom(65535)
      except BlockingIOError:
        break
      except OSError:
        break

      try:
        payload = json.loads(data.decode("utf-8"))
      except (UnicodeDecodeError, json.JSONDecodeError):
        continue

      now = time.time()
      self.last_carrot_payload = {
        "receivedAt": now,
        "from": address[0],
        "payload": payload,
      }
      if isinstance(payload, dict) and isinstance(payload.get("rgdata"), dict):
        self.last_rgdata_fields = self._normalize_rgdata(payload, now)
      self.last_carrot_time = now
      safe_write_json(LATEST_CARROT_DATA_PATH, self.last_carrot_payload)

  def handle_incoming_http(
    self,
    source_ip: str,
    path: str,
    raw: bytes,
    payload: dict[str, Any] | list[Any] | None,
    normalized: str,
  ) -> None:
    now = time.time()
    inferred_kind = "unknown"
    if isinstance(payload, dict):
      if payload.get("route") is not None:
        inferred_kind = "route"
      elif payload.get("vrtx") is not None:
        inferred_kind = "vrtx"
      elif payload.get("rgdata") is not None:
        inferred_kind = "rgdata"
      elif payload.get("sinf") is not None:
        inferred_kind = "sinf"
      elif payload.get("ssinf") is not None:
        inferred_kind = "ssinf"

    self.last_carrot_http_payload = {
      "receivedAt": now,
      "from": source_ip,
      "path": path,
      "size": len(raw),
      "kind": inferred_kind,
      "payload": payload,
      "raw": normalized,
    }
    if inferred_kind == "rgdata" and isinstance(payload, dict):
      self.last_rgdata_fields = self._normalize_rgdata(payload, now)
    self.last_carrot_time = now
    safe_write_json(LATEST_CARROT_HTTP_PATH, self.last_carrot_http_payload)
    safe_write_json(LATEST_CARROT_DATA_PATH, {
      "transport": "http",
      **self.last_carrot_http_payload,
    })

  def refresh_support_files(self, force: bool = False) -> None:
    now = time.monotonic()
    if not force and now - self.last_compat_refresh < COMPAT_REFRESH_INTERVAL:
      return
    self.last_compat_refresh = now
    refresh_apn_compat_files()

  def broadcast_state(self) -> None:
    now = time.monotonic()
    if now - self.last_broadcast < BROADCAST_INTERVAL:
      return

    ip = self.refresh_device_ip()
    if not ip:
      return

    live = self.update_messages()
    carrot_active = (time.time() - self.last_carrot_time) < 10.0
    sdi_distance = 0
    if carrot_active:
      sdi_distance = int(round(self.last_rgdata_fields.get("nextHazardDistance", 0.0)))
    payload = {
      "ip": ip,
      "navi_debug": 0,
      "port": LISTEN_PORT,
      "IsOnroad": bool(live["started"]),
      "CarrotRouteActive": carrot_active,
      "active": bool(live["active"]),
      "xState": int(live["xState"]),
      "trafficState": 0,
      "v_ego_kph": int(live["vEgoKph"]),
      "v_cruise_kph": int(live["vCruiseKph"]),
      "tbt_dist": 0,
      "sdi_dist": sdi_distance,
      "log_carrot": "FrogPilot APN bridge active",
      "Carrot2": "FrogPilot APN",
    }

    self.last_broadcast = now
    safe_write_json(BRIDGE_STATE_PATH, {
      "enabled": True,
      "httpServer": {
        "port": HTTP_PORT,
        "path": HTTP_API_PATH,
      },
      "broadcastTargets": [f"{host}:{port}" for host, port in self.broadcast_targets],
      "broadcast": payload,
      "lastCarrot": self.last_carrot_payload,
      "lastCarrotHttp": self.last_carrot_http_payload,
      "updatedAt": time.time(),
    })

    try:
      encoded = json.dumps(payload, ensure_ascii=False).encode("utf-8")
      for host, port in self.broadcast_targets:
        try:
          self.broadcast_socket.sendto(encoded, (host, port))
        except OSError:
          continue
    except OSError:
      pass

  def run(self) -> None:
    self.refresh_support_files(force=True)
    self._publish_memory_state()
    safe_write_json(BRIDGE_STATE_PATH, {
      "enabled": False,
      "updatedAt": time.time(),
      "message": "Bridge idle until UseAPN is enabled",
    })

    while True:
      self.handle_incoming_packets()
      self.refresh_support_files()
      self._publish_memory_state()

      if self.enabled():
        self.broadcast_state()
      else:
        now = time.monotonic()
        if now - self.last_idle_write >= 5.0:
          self.last_idle_write = now
          safe_write_json(BRIDGE_STATE_PATH, {
            "enabled": False,
            "httpServer": {
              "port": HTTP_PORT,
              "path": HTTP_API_PATH,
            },
            "broadcastTargets": [f"{host}:{port}" for host, port in self.broadcast_targets],
            "updatedAt": time.time(),
            "message": "Bridge idle until UseAPN is enabled",
            "lastCarrot": self.last_carrot_payload,
            "lastCarrotHttp": self.last_carrot_http_payload,
          })

      time.sleep(0.1)


def main() -> int:
  bridge = APNBridge()
  bridge.run()
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
