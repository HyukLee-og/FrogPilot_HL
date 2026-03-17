# UI / Runtime Work History

Last updated: 2026-03-17

Repository baseline:
- Branch: `testing-v1`
- Base commit when this work started: `61c139a`
- Local repo path: `/Users/ijonghyeog/Desktop/frogpilot-testing-v1`
- UTM repo path used during development: `/home/hyuklee/frogpilot-testing-v1`
- Comma device repo path used during deployment: `/data/openpilot`

## Purpose of this file

This file is a handoff/history document for anyone continuing work on this branch.

It records:
- what was changed
- why it was changed
- what is already verified
- what is still unstable or incomplete
- how to keep using the current UTM/comma workflow without repeating earlier mistakes

## Current agreed UI state

These are the latest states the user wanted to keep:

- Onroad outer status border removed.
- Screen recorder button hidden.
- `MAX` style set-speed box changed to `SET`.
- The later "larger readability-first SET card" experiment was reverted.
- Speed limit widget under the `SET` area was removed because it overlapped and looked wrong.
- When openpilot is onroad but disengaged:
  - camera image becomes darker and grayscale
  - path becomes gray-toned
- These latest disengaged visual changes are currently implemented in the repo and verified in UTM preview.
- These latest disengaged visual changes were not yet pushed to the comma device at the time of this handoff.

## High-level summary

This work ended up covering three areas:

1. UI customization for onroad.
2. UTM preview / restart tooling so UI could be viewed without a full stable simulator.
3. Runtime fixes needed so UTM preview and comma device deployment would keep working.

## Work log

### 1. Repository and UI source verification

- Confirmed this branch contains editable Qt/C++ UI source and is not just a prebuilt UI binary dump.
- Main UI work is in:
  - `selfdrive/ui/qt/onroad/...`
  - `frogpilot/ui/qt/onroad/...`
- There are also Python fallback/onroad render paths that needed matching changes:
  - `selfdrive/ui/onroad/...`
  - `selfdrive/ui/mici/onroad/...`

### 2. Build / preview strategy that actually worked

- Native comma-device UI builds were attempted first.
- Device-local `scons selfdrive/ui/ui` was unstable and caused reboots / process death during build.
- Because of that, the practical workflow became:
  - edit locally
  - sync to UTM
  - build in UTM
  - use UTM for preview
  - use UTM/device-compatible build artifacts for comma deployment
- UTM preview and comma deployment should be treated as separate targets.

### 3. UTM onroad preview strategy

- An unsafe `pandaStates` spoof path was intentionally not used.
- A safer UTM-only preview flow was created instead.
- New helper script added:
  - `tools/utm/force_onroad_preview.sh`
- What this script does:
  - copies persistent `CarParams` into active params
  - restarts openpilot manager and UI only
  - clears stale msgq state
  - sets `ForceOnroad=True` after manager starts
  - launches `ui.utm`
- Important behavior discovered:
  - `ForceOnroad` is a `CLEAR_ON_MANAGER_START` param
  - if it is set before manager starts, it gets wiped
  - therefore it must be written after manager startup
- The user later clarified that "UTM reboot" should mean "restart openpilot inside UTM", not reboot the whole VM.
- From this point onward, "UTM reboot" should be interpreted as:
  - stop manager/UI/bridge as needed
  - clear runtime state
  - restart openpilot only

### 4. Onroad border removal

- Removed the visible outer status border from the Qt onroad path.
- Matching Python fallback path was also updated so behavior stays consistent.
- Files:
  - `selfdrive/ui/qt/onroad/onroad_home.cc`
  - `selfdrive/ui/onroad/augmented_road_view.py`

### 5. Alert area redesign and onroad state handling

- Alert rendering was refactored so custom chips and alert layout work more reliably.
- Added chip-style alert helper drawing.
- Added state tracking for engageable/enabled state.
- Added a `ForceOnroad` fallback in alert logic so UTM preview would not trip the "waiting for start" / missing selfdriveState behavior.
- Files:
  - `selfdrive/ui/qt/onroad/alerts.cc`
  - `selfdrive/ui/qt/onroad/alerts.h`

### 6. SET speed HUD work

- `MAX` label was replaced with `SET`.
- Qt HUD path was updated first.
- Python fallback HUD renderers were also updated so the same label appears there too.
- A larger, more readable `SET` card experiment was later tried.
- The user did not like the larger card, so that specific redesign was reverted.
- Current state:
  - `SET` remains
  - compact layout remains
  - large-card redesign is reverted
- Files:
  - `selfdrive/ui/qt/onroad/hud.cc`
  - `selfdrive/ui/onroad/hud_renderer.py`
  - `selfdrive/ui/mici/onroad/hud_renderer.py`

### 7. Speed limit widget under SET was removed

- The speed limit widget looked like it was stacking or overlapping under the `SET` area.
- Instead of trying to keep that widget in a broken layout, it was fully disabled in the FrogPilot onroad overlay.
- Current effect:
  - no speed limit sign under the `SET` area
  - no pending speed limit widget
  - no speed limit sources widget
- File:
  - `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`

### 8. Screen recorder button hidden

- The user wanted the onroad recording button removed from the UI.
- It was hard-hidden in the Qt path.
- At one point it was also temporarily hidden on device by param, but the code path is now also forcing it hidden.
- File:
  - `selfdrive/ui/qt/onroad/annotated_camera.cc`

### 9. Experimental / steering wheel button styling

- Button background ellipse was removed from the wheel button rendering.
- Added tint support for icon rendering.
- If stock wheel image is used and openpilot is enabled, the wheel icon gets a green tint.
- Files:
  - `selfdrive/ui/qt/onroad/buttons.cc`
  - `selfdrive/ui/qt/onroad/buttons.h`

### 10. Driver monitoring widget restyle

- The original face-keypoint style DM widget was replaced with an asset-based layered style.
- Added separate assets/tints for disengaged, engageable, and enabled states.
- Orientation handling was simplified into a cone rotation model.
- Files:
  - `selfdrive/ui/qt/onroad/driver_monitoring.cc`
  - `selfdrive/ui/qt/onroad/driver_monitoring.h`

### 11. Disengaged camera effect

- Added shader-driven camera post-processing for onroad disengaged state.
- Current effect when `scene.started` is true and status is `STATUS_DISENGAGED`:
  - camera frame becomes grayscale
  - camera frame brightness is multiplied down to `0.62`
- This is intentionally only the camera frame, not the whole UI.
- Files:
  - `selfdrive/ui/qt/widgets/cameraview.cc`
  - `selfdrive/ui/qt/widgets/cameraview.h`
  - `selfdrive/ui/qt/onroad/annotated_camera.cc`

### 12. Disengaged path gray tone

- The user wanted only the path to turn gray, not the whole overlay.
- Implemented a gradient gray-toning pass only when UI status is `STATUS_DISENGAGED`.
- This leaves the rest of the HUD alone.
- File:
  - `selfdrive/ui/qt/onroad/model.cc`

### 13. UTM modeld / tinygrad compatibility fixes

- UTM is `aarch64 Linux`, which initially made the branch try to use QCOM tinygrad kernels.
- That caused crashes because UTM does not have `/dev/kgsl-3d0`.
- Fix:
  - only use `DEV=QCOM` when `/TICI` actually exists
  - otherwise use CPU tinygrad on UTM/Linux arm64
- Files:
  - `selfdrive/modeld/SConscript`
- A second UTM issue appeared after regenerating CPU models:
  - some policy models expected `desire`
  - some runtime code still used `desire_pulse`
- Compatibility mapping was added so both cases work.
- File:
  - `selfdrive/modeld/modeld.py`

### 14. UTM full-stack bring-up support

- Added `selfdrive/test/helpers.py` to seed a valid calibration / setup path for simulator flows.
- This made UTM / simulated flows easier to start without going through setup UI every time.
- File:
  - `selfdrive/test/helpers.py`

### 15. MetaDrive helper changes

- `tools/sim/bridge/metadrive/metadrive_bridge.py` was adjusted so MetaDrive rendering can be enabled for visible simulator runs.
- This was used while trying to get a richer onroad preview in UTM.
- File:
  - `tools/sim/bridge/metadrive/metadrive_bridge.py`

### 16. Comma device runtime fixes

- The comma device showed `process not running mapd`, which blocked engagement.
- Root cause found:
  - process manager could keep a stale `proc` handle after a process died
  - manager then failed to restart that process cleanly
- Fix:
  - clean up stale `proc` references in process handling path
- File:
  - `system/manager/process.py`
- Also discovered the device launch path was not consistently using the correct Python runtime.
- Fix:
  - `launch_chffrplus.sh` now prefers `/usr/local/venv/bin/python`
  - falls back to `python3` if unavailable
- File:
  - `launch_chffrplus.sh`
- This fix was applied in the repo and also synced to the actual device during debugging.

### 17. Device runtime library path fix

- While getting UTM-built binaries to run on comma, a `libyuv` runtime path issue appeared.
- Added conditional `LD_LIBRARY_PATH` setup for local `third_party/libyuv/larch64/lib`.
- File:
  - `launch_env.sh`

### 18. ForceOnroad fallback in core UI state

- UTM did not reliably have `frogpilotPlan` alive.
- Because of that, the UI could not always see the normal FrogPilot `force_onroad` toggle flow.
- Added direct `Params()` fallback reads for:
  - `ForceOnroad`
  - `ForceOffroad`
- File:
  - `selfdrive/ui/ui.cc`
- Alert logic was also updated to use the same fallback.

## Current file-by-file change map

This is the current local diff footprint and why each file matters.

- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
  - removed speed-limit/pending-limit/source drawing under `SET`
  - includes `QPainterPath` fix that was needed for one build path
- `launch_chffrplus.sh`
  - use correct Python runtime on comma device
- `launch_env.sh`
  - add local `libyuv` runtime path for compatible binaries
- `selfdrive/modeld/SConscript`
  - do not build QCOM tinygrad kernels on non-TICI arm64 UTM
- `selfdrive/modeld/modeld.py`
  - `desire` / `desire_pulse` compatibility and input-name mapping
- `selfdrive/ui/mici/onroad/hud_renderer.py`
  - Python fallback `MAX` -> `SET`
- `selfdrive/ui/onroad/augmented_road_view.py`
  - remove onroad border in Python renderer
- `selfdrive/ui/onroad/hud_renderer.py`
  - Python fallback `MAX` -> `SET`
- `selfdrive/ui/qt/onroad/alerts.cc`
  - alert redraw behavior, chips, `ForceOnroad` fallback
- `selfdrive/ui/qt/onroad/alerts.h`
  - alert state storage support for the redraw changes
- `selfdrive/ui/qt/onroad/annotated_camera.cc`
  - hide screen recorder
  - set disengaged grayscale/dim camera filter
- `selfdrive/ui/qt/onroad/buttons.cc`
  - wheel icon styling/tint changes
- `selfdrive/ui/qt/onroad/buttons.h`
  - button state support
- `selfdrive/ui/qt/onroad/driver_monitoring.cc`
  - full DM widget redesign
- `selfdrive/ui/qt/onroad/driver_monitoring.h`
  - DM renderer state changes
- `selfdrive/ui/qt/onroad/hud.cc`
  - `MAX` -> `SET`
  - compact `SET` layout preserved
  - large redesign reverted
- `selfdrive/ui/qt/onroad/model.cc`
  - gray-tone path in disengaged state
- `selfdrive/ui/qt/onroad/onroad_home.cc`
  - remove Qt onroad outer border/margins
- `selfdrive/ui/qt/widgets/cameraview.cc`
  - camera fragment shader grayscale + dimming support
- `selfdrive/ui/qt/widgets/cameraview.h`
  - frame filter controls for grayscale/brightness
- `selfdrive/ui/ui.cc`
  - `ForceOnroad` / `ForceOffroad` param fallback
- `system/manager/process.py`
  - stale-process restart fix for manager
- `tools/sim/bridge/metadrive/metadrive_bridge.py`
  - MetaDrive render support for UTM testing
- `selfdrive/test/__init__.py`
  - test package support for helper import
- `selfdrive/test/helpers.py`
  - seed params/calibration for simulator-style bring-up
- `tools/utm/force_onroad_preview.sh`
  - UTM-only onroad preview launcher

## Current recommended workflow

### Local edit -> UTM preview

Use this when the goal is to visually inspect UI changes.

1. Edit files locally in this repo.
2. Sync the changed files to the UTM repo.
3. In UTM:
   - build `selfdrive/ui/ui`
   - copy it to `selfdrive/ui/ui.utm`
4. Run:
   - `tools/utm/force_onroad_preview.sh`
5. This should restart openpilot inside UTM and relaunch the preview UI.

Notes:
- This is an openpilot restart, not a VM reboot.
- `frogpilotPlan` is still unreliable in UTM, so the preview depends on the direct `ForceOnroad` fallback.
- This preview is best for UI layout checks, not for validating every live signal.

### Local edit -> Comma device deployment

Use this when the goal is to see the change on the actual device.

Recommended approach:
- do not rely on comma-device native compilation unless absolutely necessary
- building directly on comma was unstable and caused reboots
- instead, continue using the previously established UTM/device-compatible build flow
- after producing the device-compatible `selfdrive/ui/ui`, copy it to:
  - `/data/openpilot/selfdrive/ui/ui`
- keep a backup of the previous device binary before replacing it

Notes:
- the repo alone does not encode every external environment tweak used during ABI-aligned device builds
- `launch_env.sh` now expects `third_party/libyuv/larch64/lib` to be available when needed

## Current UTM notes

- UTM preview is currently best treated as a visual preview environment, not a full-fidelity driving environment.
- A stable visual preview path exists even when simulator pieces are shaky.
- Current preferred preview assumptions:
  - use Honda Civic 2022 for simulator-style compatibility
  - keep openpilot longitudinal disabled when needed for preview consistency
- Forcing Chevrolet Traverse in UTM preview was not a real simulator success path because the simulator CAN path is effectively Honda-oriented.

## Known problems and unfinished items

### 1. `frogpilotPlan` is still unreliable in UTM

- The UI preview is working around this with direct `Params()` fallback.
- This should be fixed properly later if full FrogPilot message-driven preview is needed.

### 2. Headless MetaDrive / OpenCL path in UTM is still unstable

- During bridge testing, `pyopencl/pocl` kernel build issues appeared.
- This means simulator-backed preview can still stall or be incomplete.
- If someone later wants a richer UTM environment, this needs a dedicated fix.

### 3. Latest disengaged visuals are only verified in UTM

- Camera grayscale/dim and gray path were implemented and previewed in UTM.
- They were not yet synced and deployed to the comma device when this file was written.
- If the user wants those on the comma device too, that is still a follow-up task.

### 4. Speed limit widget was removed, not repositioned

- The current choice was to remove the speed-limit block entirely from that area.
- If someone later wants it back, it needs a fresh layout pass instead of simply re-enabling old paint calls.

### 5. The larger `SET` redesign was rejected

- Do not resurrect the large-card version unless the user explicitly asks again.
- The current accepted state is the smaller compact `SET` design.

### 6. The screen recorder is hard-hidden

- This is not a temporary param-only hide anymore in the Qt path.
- If the recorder needs to return, it should be redesigned intentionally instead of just flipped back on.

### 7. Local working tree contains generated junk files

- Current local status also includes things that are not part of the real handoff:
  - `.DS_Store`
  - `__pycache__`
- These were not cleaned in this pass.
- If someone prepares a PR or commit later, they should clean or ignore those first.

## Suggested next tasks

If another engineer picks this up, the most logical next steps are:

1. Decide whether the latest disengaged visuals should also be deployed to the comma device.
2. If yes, run the device-compatible UTM build/deploy flow and verify on actual hardware.
3. If the user wants richer UTM preview, fix the `frogpilotPlan` and/or `pyopencl/pocl` simulator issues.
4. If speed limit data needs to come back, redesign the `SET` cluster and lower-left/upper HUD layout first.
5. Clean the working tree (`.DS_Store`, `__pycache__`) before any formal commit.

## Final caution

There are both UI changes and runtime/process changes in this working tree.

That means this is not "just a skinning branch" anymore.

Anyone continuing should review these files carefully before committing or cherry-picking:
- `launch_chffrplus.sh`
- `launch_env.sh`
- `selfdrive/modeld/SConscript`
- `selfdrive/modeld/modeld.py`
- `system/manager/process.py`

Those files affect runtime behavior beyond pure UI appearance.
