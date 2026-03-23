#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
PARAMS_HEADER = REPO_ROOT / "common" / "params_keys.h"
CARROT_SETTINGS_PATH = REPO_ROOT / "selfdrive" / "carrot_settings.json"
BACKUP_PARAMS_PATH = Path("/data/backup_params.json") if Path("/data").exists() else REPO_ROOT / ".codex_tmp" / "backup_params.json"

if str(REPO_ROOT) not in sys.path:
  sys.path.insert(0, str(REPO_ROOT))

try:
  from common.params import Params as NativeParams
except Exception:
  NativeParams = None


class FallbackParams:
  def get(
    self,
    key: str,
    block: bool = False,
    encoding: str | None = None,
    return_default: bool = False,
  ) -> bytes | str | None:
    del block
    del return_default
    value = None
    if value is None:
      return None
    if encoding:
      return value
    return value.encode("utf-8")


Params = NativeParams or FallbackParams


KEY_LINE_RE = re.compile(r'^\s*\{"([^"]+)",\s*\{(.*)\}\},?\s*$')


@dataclass(frozen=True)
class ParamMeta:
  key: str
  scope: str
  flags: str
  ptype: str
  default_raw: str | None
  stock_raw: str | None


SAFE_CORE_PARAMS = {
  "AlwaysOnDM",
  "DisengageOnAccelerator",
  "ExperimentalMode",
  "IsLdwEnabled",
  "IsMetric",
  "LongitudinalPersonality",
  "OpenpilotEnabledToggle",
  "RecordAudio",
  "RecordFront",
  "UseAPN",
}


def _split_top_level(text: str) -> list[str]:
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


def _normalize_literal(token: str | None) -> str | None:
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
    tokens = _split_top_level(inner)
    if len(tokens) < 2:
      continue

    metas[key] = ParamMeta(
      key=key,
      scope=scope,
      flags=tokens[0],
      ptype=tokens[1],
      default_raw=_normalize_literal(tokens[2]) if len(tokens) >= 3 else None,
      stock_raw=_normalize_literal(tokens[3]) if len(tokens) >= 4 else None,
    )

  return metas


PARAMS_META = parse_params_header()


def humanize_key(key: str) -> str:
  text = key.replace("_", " ")
  text = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", text)
  return " ".join(part for part in text.split() if part)


def int_value(meta: ParamMeta, value: Any) -> int:
  if value is None:
    value = meta.default_raw or meta.stock_raw or "0"

  if meta.ptype == "BOOL":
    if isinstance(value, bool):
      return 1 if value else 0
    if isinstance(value, (int, float)):
      return 1 if int(value) else 0
    return 1 if str(value).strip().lower() in {"1", "true", "yes", "on"} else 0

  if isinstance(value, bool):
    return int(value)
  if isinstance(value, int):
    return value
  if isinstance(value, float):
    return int(round(value))

  try:
    return int(str(value).strip())
  except ValueError:
    try:
      return int(float(str(value).strip()))
    except ValueError:
      return 0


def param_bounds(meta: ParamMeta, default_value: int) -> tuple[int, int]:
  if meta.ptype == "BOOL":
    return 0, 1

  span = max(abs(default_value), abs(int_value(meta, meta.stock_raw)), 10)
  if default_value >= 0:
    return 0, max(span * 2, 10)
  return min(default_value * 2, -10), max(span * 2, 10)


def include_param(meta: ParamMeta) -> bool:
  if meta.ptype not in {"BOOL", "INT"}:
    return False
  if "PERSISTENT" not in meta.flags or "DONT_LOG" in meta.flags:
    return False

  if meta.scope == "core":
    return meta.key in SAFE_CORE_PARAMS

  prefixes = (
    "ApiCache_",
    "Calibration",
    "Cancel",
    "Car",
    "Current",
    "Download",
    "FrogPilot",
    "Git",
    "Last",
    "Live",
    "MapTarget",
    "Updater",
  )
  if meta.key.startswith(prefixes):
    return False

  blocked = {
    "DisableForcedPowerLogic",
    "DisableOpenpilotLongitudinal",
    "ForceOnroad",
    "ForceOffroad",
    "ModelDownloadProgress",
    "ModelToDownload",
    "ScreenRecorder",
    "TuningLevel",
    "TuningLevelConfirmed",
  }
  return meta.key not in blocked


def selected_param_metas() -> list[ParamMeta]:
  return sorted((meta for meta in PARAMS_META.values() if include_param(meta)), key=lambda item: (item.scope, item.key.lower()))


def build_carrot_settings() -> dict[str, Any]:
  params_payload: list[dict[str, Any]] = []

  for meta in selected_param_metas():
    default_value = int_value(meta, meta.default_raw)
    min_value, max_value = param_bounds(meta, default_value)
    group = "FrogPilot" if meta.scope == "frogpilot" else "Openpilot"
    title = humanize_key(meta.key)
    descr = f"{title} setting"
    params_payload.append({
      "group": group,
      "name": meta.key,
      "title": title,
      "descr": descr,
      "egroup": group,
      "etitle": title,
      "edescr": descr,
      "min": min_value,
      "max": max_value,
      "default": default_value,
      "unit": 0,
    })

  return {
    "apilot": 1,
    "params": params_payload,
  }


def write_carrot_settings_json(path: Path | None = None) -> Path:
  output_path = path or CARROT_SETTINGS_PATH
  output_path.parent.mkdir(parents=True, exist_ok=True)
  output_path.write_text(json.dumps(build_carrot_settings(), ensure_ascii=False, indent=2) + "\n")
  return output_path


def write_backup_params_json(path: Path | None = None) -> Path:
  output_path = path or BACKUP_PARAMS_PATH
  output_path.parent.mkdir(parents=True, exist_ok=True)

  params = Params()
  values = []
  for meta in selected_param_metas():
    values.append({
      "filename": meta.key,
      "content": str(int_value(meta, params.get(meta.key, return_default=True))),
    })

  output_path.write_text(json.dumps(values, ensure_ascii=False, indent=2) + "\n")
  return output_path


def refresh_apn_compat_files() -> tuple[Path, Path]:
  return write_carrot_settings_json(), write_backup_params_json()


if __name__ == "__main__":
  config_path, backup_path = refresh_apn_compat_files()
  print(json.dumps({
    "carrot_settings": str(config_path),
    "backup_params": str(backup_path),
    "count": len(selected_param_metas()),
  }, ensure_ascii=False))
