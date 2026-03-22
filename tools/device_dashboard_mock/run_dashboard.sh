#!/bin/bash
set -euo pipefail

cd /data/openpilot/tools/device_dashboard_mock
mkdir -p /data/media/0/codex_logs

exec /usr/local/venv/bin/python server.py >>/data/media/0/codex_logs/device_dashboard_server.out 2>&1
