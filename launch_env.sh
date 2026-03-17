#!/usr/bin/env bash

export OMP_NUM_THREADS=1
export MKL_NUM_THREADS=1
export NUMEXPR_NUM_THREADS=1
export OPENBLAS_NUM_THREADS=1
export VECLIB_MAXIMUM_THREADS=1

# models get lower priority than ui
# - ui is ~5ms
# - modeld is 20ms
# - DM is 10ms
# in order to run ui at 60fps (16.67ms), we need to allow
# it to preempt the model workloads. we have enough
# headroom for this until ui is moved to the CPU.
export QCOM_PRIORITY=12

OPENPILOT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_LIBYUV_DIR="$OPENPILOT_ROOT/third_party/libyuv/larch64/lib"
if [ -d "$LOCAL_LIBYUV_DIR" ]; then
  export LD_LIBRARY_PATH="${LOCAL_LIBYUV_DIR}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

if [ -z "$AGNOS_VERSION" ]; then
  export AGNOS_VERSION="12.8"
fi

export STAGING_ROOT="/data/safe_staging"

# TICI runs weston out of /var/tmp/weston. Force Qt to the same socket so the
# checked-in UI binary doesn't inherit an SSH/empty display environment.
if [ -S /var/tmp/weston/wayland-0 ]; then
  export XDG_RUNTIME_DIR="/var/tmp/weston"
  export WAYLAND_DISPLAY="wayland-0"
  export QT_QPA_PLATFORM="wayland-egl"
fi

# FrogPilot variables
eval "$(/data/openpilot/frogpilot/system/environment_variables)"
