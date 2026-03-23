#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
  sys.path.insert(0, str(REPO_ROOT))

from tools.apn_bridge.apn_compat import refresh_apn_compat_files


def main() -> int:
  refresh_apn_compat_files()
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
