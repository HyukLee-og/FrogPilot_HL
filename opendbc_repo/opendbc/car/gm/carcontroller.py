import numpy as np
import time
import json
from openpilot.common.params import Params
from opendbc.can import CANPacker
from opendbc.car import Bus, DT_CTRL, create_gas_interceptor_command, structs
from opendbc.car.lateral import apply_driver_steer_torque_limits
from opendbc.car.gm import gmcan
from opendbc.car.common.conversions import Conversions as CV
from opendbc.car.gm.cluster_speed import gm_cluster_cruise_speed_from_raw_ms, gm_raw_cruise_speed_from_cluster_ms
from opendbc.car.gm.values import CC_ONLY_CAR, DBC, CanBus, CarControllerParams, CruiseButtons, GMFlags
from opendbc.car.interfaces import CarControllerBase

VisualAlert = structs.CarControl.HUDControl.VisualAlert
NetworkLocation = structs.CarParams.NetworkLocation
LongCtrlState = structs.CarControl.Actuators.LongControlState
ButtonType = structs.CarState.ButtonEvent.Type

# Camera cancels up to 0.1s after brake is pressed, ECM allows 0.5s
CAMERA_CANCEL_DELAY_FRAMES = 10
# Enforce a minimum interval between steering messages to avoid a fault
MIN_STEER_MSG_INTERVAL_MS = 15

FAKE_LONG_BUTTON_INTERVAL_FRAMES = max(1, int(round(0.6 / DT_CTRL)))
FAKE_LONG_PAUSE_FRAMES = max(1, int(round(2.0 / DT_CTRL)))
FAKE_LONG_IGNORE_ECHO_FRAMES = max(1, int(round(0.2 / DT_CTRL)))
FAKE_LONG_MIN_SET_SPEED_MS = 25 * CV.KPH_TO_MS
FAKE_LONG_MIN_SEND_SPEED_MS = 10 * CV.KPH_TO_MS
FAKE_LONG_PLANNER_HEADROOM_KPH = 2.0
FAKE_LONG_PLANNER_HEADROOM_MPH = 1.0
FAKE_LONG_FAST_INTERVAL_FRAMES = max(1, int(round(0.18 / DT_CTRL)))
FAKE_LONG_MEDIUM_INTERVAL_FRAMES = max(1, int(round(0.24 / DT_CTRL)))
FAKE_LONG_SLOW_INTERVAL_FRAMES = max(1, int(round(0.32 / DT_CTRL)))
FAKE_LONG_PRESS_REPEAT_COUNT = 1
FAKE_LONG_RELEASE_DELAY_FRAMES = max(1, int(round(0.03 / DT_CTRL)))
FAKE_LONG_DECEL_TRACK_FACTOR = 1.0
FAKE_LONG_DECEL_MIN_STEP_MS = 0.25
FAKE_LONG_FOLLOW_HEADROOM_KPH = 10.0
FAKE_LONG_FOLLOW_HEADROOM_MPH = 6.0
FAKE_LONG_RECOVERY_MARGIN_KPH = 10.0
FAKE_LONG_RECOVERY_MARGIN_MPH = 6.0
FAKE_LONG_TEST_BUTTON_MAX_AGE_S = 2.0
FAKE_LONG_SMOOTH_UP_KPH_PER_S = 8.0
FAKE_LONG_SMOOTH_UP_MPH_PER_S = 5.0
FAKE_LONG_SMOOTH_DOWN_KPH_PER_S = 14.0
FAKE_LONG_SMOOTH_DOWN_MPH_PER_S = 9.0
FAKE_LONG_TARGET_HYST_KPH = 1.0
FAKE_LONG_TARGET_HYST_MPH = 1.0
APN_FAKE_LONG_SHORT_ALERT_DISTANCE_M = 500.0
APN_FAKE_LONG_LONG_ALERT_DISTANCE_M = 1000.0


class CarController(CarControllerBase):
  def __init__(self, dbc_names, CP):
    super().__init__(dbc_names, CP)
    self.start_time = 0.
    self.apply_torque_last = 0
    self.apply_gas = 0
    self.apply_brake = 0
    self.last_steer_frame = 0
    self.last_button_frame = 0
    self.cancel_counter = 0

    self.lka_steering_cmd_counter = 0
    self.lka_icon_status_last = (False, False)

    self.params = CarControllerParams(self.CP)
    self.params_memory = Params(memory=True)

    self.packer_pt = CANPacker(DBC[self.CP.carFingerprint][Bus.pt])
    self.packer_obj = CANPacker(DBC[self.CP.carFingerprint][Bus.radar])
    self.packer_ch = CANPacker(DBC[self.CP.carFingerprint][Bus.chassis])

    # OPGM variables
    self.prev_op_enabled = False

    self.apply_speed = 0
    self.pedal_steady = 0
    self.fake_long_target_speed = 0.0
    self.fake_long_pause_until = 0
    self.fake_long_ignore_button_until = 0
    self.fake_long_sync_target_until = 0
    self.fake_long_commanded_speed = 0.0
    self.fake_long_user_set_speed = 0.0
    self.fake_long_prev_cruise_enabled = False
    self.fake_long_prev_v_ego = 0.0
    self.fake_long_session_armed = False
    self.fake_long_prev_session_cruise_enabled = False
    self.fake_long_last_button = ""
    self.fake_long_debug_cache = ""
    self.fake_long_pending_release = False
    self.fake_long_pending_press_button = CruiseButtons.INIT
    self.fake_long_pending_press_repeats = 0
    self.fake_long_pending_bus_mode = "camera"
    self.fake_long_pending_hold_until = 0
    self.fake_long_release_frame = 0
    self.fake_long_release_idx = 0
    self.fake_long_apn_decel_started = False
    self.fake_long_apn_recovery_started = False
    self.fake_long_apn_restore_pending = False
    self.fake_long_test_bus_mode = "camera"
    self.fake_long_test_mode = "tap"

  def clear_fake_long_pending_buttons(self) -> None:
    self.fake_long_pending_release = False
    self.fake_long_pending_press_button = CruiseButtons.INIT
    self.fake_long_pending_press_repeats = 0
    self.fake_long_pending_bus_mode = "camera"
    self.fake_long_pending_hold_until = 0
    self.fake_long_release_frame = 0
    self.fake_long_release_idx = 0

  def reset_fake_long(self, clear_user_set: bool = True) -> None:
    self.fake_long_target_speed = 0.0
    self.fake_long_pause_until = 0
    self.fake_long_ignore_button_until = 0
    self.fake_long_sync_target_until = 0
    self.fake_long_commanded_speed = 0.0
    self.fake_long_prev_v_ego = 0.0
    self.clear_fake_long_pending_buttons()
    self.fake_long_apn_decel_started = False
    self.fake_long_apn_recovery_started = False
    self.fake_long_apn_restore_pending = False
    if clear_user_set:
      self.fake_long_user_set_speed = 0.0

  @staticmethod
  def _param_float(value, default: float = 0.0) -> float:
    try:
      return float(value)
    except (TypeError, ValueError):
      return default

  @staticmethod
  def _display_set_speed(CS) -> float:
    speed_cluster = float(CS.out.cruiseState.speedCluster)
    if speed_cluster > 0.0:
      return speed_cluster

    raw_speed = float(CS.out.cruiseState.speed)
    if raw_speed > 0.0:
      return gm_cluster_cruise_speed_from_raw_ms(raw_speed)
    return raw_speed

  def update_fake_long_debug(self, *, active, test_ui, cruise_enabled, cruise_available, current_set_speed, current_v_ego,
                             raw_current_set_speed: float | None = None,
                             apn_enabled: bool = False, apn_override: bool = False, apn_target_speed: float = 0.0,
                             apn_hazard_distance: float = 0.0, apn_hazard: str = "",
                             apn_control_active: bool = False, apn_recovery_active: bool = False):
    current_set_speed = float(current_set_speed)
    raw_current_set_speed = float(current_set_speed if raw_current_set_speed is None else raw_current_set_speed)
    payload = {
      "active": bool(active),
      "testUI": bool(test_ui),
      "armed": bool(self.fake_long_session_armed),
      "paused": bool(self.frame < self.fake_long_pause_until),
      "cruiseEnabled": bool(cruise_enabled),
      "cruiseAvailable": bool(cruise_available),
      "target": round(float(self.fake_long_target_speed), 3),
      "commanded": round(float(self.fake_long_commanded_speed), 3),
      "set": round(current_set_speed, 3),
      "userSet": round(float(self.fake_long_user_set_speed), 3),
      "vEgo": round(float(current_v_ego), 3),
      "last": self.fake_long_last_button,
      "apnEnabled": bool(apn_enabled),
      "apnOverride": bool(apn_control_active),
      "apnOverrideDetected": bool(apn_override),
      "apnControlActive": bool(apn_control_active),
      "apnRecoveryActive": bool(apn_recovery_active),
      "apnTarget": round(float(apn_target_speed), 3),
      "apnDistance": round(float(apn_hazard_distance), 3),
      "apnHazard": str(apn_hazard),
      "rawTarget": round(float(gm_raw_cruise_speed_from_cluster_ms(self.fake_long_target_speed)), 3),
      "rawCommanded": round(float(gm_raw_cruise_speed_from_cluster_ms(self.fake_long_commanded_speed)), 3),
      "rawSet": round(raw_current_set_speed, 3),
      "rawUserSet": round(float(gm_raw_cruise_speed_from_cluster_ms(self.fake_long_user_set_speed)), 3),
      "testBus": self.fake_long_test_bus_mode,
      "testMode": self.fake_long_test_mode,
    }
    encoded = json.dumps(payload, separators=(",", ":"))
    if encoded != self.fake_long_debug_cache:
      self.params_memory.put("FakeLongDebug", encoded)
      self.fake_long_debug_cache = encoded

  def update_fake_long_session_state(self, CS) -> None:
    cruise_enabled = CS.out.cruiseState.enabled
    cruise_available = CS.out.cruiseState.available
    current_v_ego = float(CS.out.vEgo)

    # Require one manual stock ACC engagement after each ignition/offline cycle
    # before fake-long is allowed to emit any synthetic cruise buttons.
    if not cruise_available and current_v_ego < 1.0:
      self.fake_long_session_armed = False
      self.reset_fake_long(clear_user_set=True)
    elif cruise_enabled and not self.fake_long_prev_session_cruise_enabled:
      self.fake_long_session_armed = True

    self.fake_long_prev_session_cruise_enabled = cruise_enabled

  @staticmethod
  def get_fake_long_interval_frames(diff_units: float) -> int:
    if diff_units >= 8.0:
      return FAKE_LONG_FAST_INTERVAL_FRAMES
    if diff_units >= 4.0:
      return FAKE_LONG_MEDIUM_INTERVAL_FRAMES
    return FAKE_LONG_SLOW_INTERVAL_FRAMES

  @staticmethod
  def _normalize_fake_long_bus_mode(bus_mode: str | None) -> str:
    return "camera"

  def create_fake_long_button_commands(self, idx, button, bus_mode: str = "camera"):
    return [gmcan.create_buttons(self.packer_pt, CanBus.CAMERA, idx, button)]

  def create_fake_long_press_command(self, CS, button, bus_mode: str = "camera",
                                     repeats: int = FAKE_LONG_PRESS_REPEAT_COUNT,
                                     release: bool = True, hold_frames: int = 0):
    press_idx = CS.buttons_counter % 4
    normalized_bus = self._normalize_fake_long_bus_mode(bus_mode)
    self.fake_long_pending_press_button = button
    self.fake_long_pending_press_repeats = max(0, int(repeats) - 1)
    self.fake_long_pending_release = bool(release)
    self.fake_long_pending_bus_mode = normalized_bus
    self.fake_long_pending_hold_until = self.frame + max(0, int(hold_frames))
    self.fake_long_release_frame = self.frame + max(FAKE_LONG_RELEASE_DELAY_FRAMES, max(0, int(hold_frames)))
    self.fake_long_release_idx = press_idx
    self.fake_long_test_bus_mode = normalized_bus
    self.fake_long_test_mode = "press" if not release else "tap"
    return self.create_fake_long_button_commands(press_idx, button, normalized_bus)

  def consume_fake_long_press_repeat(self, CS):
    if self.fake_long_pending_press_repeats <= 0 or self.fake_long_pending_press_button == CruiseButtons.INIT:
      return None

    self.fake_long_pending_press_repeats -= 1
    self.last_button_frame = self.frame
    repeat_idx = CS.buttons_counter % 4
    self.fake_long_release_frame = self.frame + max(FAKE_LONG_RELEASE_DELAY_FRAMES, 0)
    self.fake_long_release_idx = repeat_idx
    return self.create_fake_long_button_commands(repeat_idx, self.fake_long_pending_press_button, self.fake_long_pending_bus_mode)

  def consume_fake_long_release(self, CS):
    if not self.fake_long_pending_release or self.fake_long_pending_press_repeats > 0:
      return None

    if self.frame < max(self.fake_long_pending_hold_until, self.fake_long_release_frame):
      return None

    self.fake_long_pending_release = False
    self.fake_long_pending_press_button = CruiseButtons.INIT
    self.fake_long_pending_hold_until = 0
    self.fake_long_release_frame = 0
    self.last_button_frame = self.frame
    return self.create_fake_long_button_commands(self.fake_long_release_idx, CruiseButtons.UNPRESS, self.fake_long_pending_bus_mode)

  def create_fake_long_command(self, CS, actuators, frogpilot_toggles, op_enabled: bool):
    apn_fake_long_enabled = bool(getattr(frogpilot_toggles, "apn_fake_long", False))
    apn_fake_long_offset = float(getattr(frogpilot_toggles, "apn_fake_long_offset", 0.0) or 0.0)
    fake_long_enabled = bool(getattr(frogpilot_toggles, "fake_long", False))
    test_ui_enabled = bool(getattr(frogpilot_toggles, "fake_long_test_ui", False))
    stock_acc_path = self.CP.pcmCruise and not self.CP.openpilotLongitudinalControl and self.CP.networkLocation == NetworkLocation.fwdCamera
    # Keep the web/onroad button experiment path isolated from the main
    # fake-long/APN controller so test presses don't inherit controller state.
    fake_long_controller_enabled = fake_long_enabled or apn_fake_long_enabled

    if not (fake_long_controller_enabled and stock_acc_path):
      self.reset_fake_long(clear_user_set=True)
      self.update_fake_long_debug(active=False, test_ui=test_ui_enabled, cruise_enabled=CS.out.cruiseState.enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=self._display_set_speed(CS),
                                  current_v_ego=float(CS.out.vEgo), raw_current_set_speed=float(CS.out.cruiseState.speed))
      return None

    cruise_enabled = CS.out.cruiseState.enabled
    current_set_speed_raw = float(CS.out.cruiseState.speed)
    current_set_speed = self._display_set_speed(CS)
    current_v_ego = float(CS.out.vEgo)
    apn_override_active = False
    apn_hazard_distance = 0.0
    apn_hazard_kind = ""
    apn_target_speed = 0.0
    apn_control_active = False
    apn_recovery_active = False

    if not op_enabled:
      self.fake_long_prev_cruise_enabled = False
      self.reset_fake_long(clear_user_set=False)
      if current_set_speed > 0.0:
        self.fake_long_user_set_speed = current_set_speed
      self.apply_speed = current_set_speed
      self.update_fake_long_debug(active=False, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled)
      return None

    if not cruise_enabled:
      self.fake_long_prev_cruise_enabled = False
      self.reset_fake_long(clear_user_set=False)
      self.apply_speed = current_set_speed
      self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled)
      return None

    if current_v_ego < FAKE_LONG_MIN_SEND_SPEED_MS:
      self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled)
      return None

    if not self.fake_long_session_armed:
      self.reset_fake_long(clear_user_set=False)
      self.apply_speed = current_set_speed
      self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled)
      return None

    is_metric = bool(getattr(frogpilot_toggles, "is_metric", True))
    speed_unit_to_ms = CV.KPH_TO_MS if is_metric else CV.MPH_TO_MS
    planner_headroom = (FAKE_LONG_PLANNER_HEADROOM_KPH if is_metric else FAKE_LONG_PLANNER_HEADROOM_MPH) * speed_unit_to_ms
    follow_headroom = (FAKE_LONG_FOLLOW_HEADROOM_KPH if is_metric else FAKE_LONG_FOLLOW_HEADROOM_MPH) * speed_unit_to_ms
    recovery_margin = (FAKE_LONG_RECOVERY_MARGIN_KPH if is_metric else FAKE_LONG_RECOVERY_MARGIN_MPH) * speed_unit_to_ms
    speed_hysteresis = 0.5 * speed_unit_to_ms
    target_hysteresis = (FAKE_LONG_TARGET_HYST_KPH if is_metric else FAKE_LONG_TARGET_HYST_MPH) * speed_unit_to_ms
    smooth_up_step = (FAKE_LONG_SMOOTH_UP_KPH_PER_S if is_metric else FAKE_LONG_SMOOTH_UP_MPH_PER_S) * speed_unit_to_ms * DT_CTRL
    smooth_down_step = (FAKE_LONG_SMOOTH_DOWN_KPH_PER_S if is_metric else FAKE_LONG_SMOOTH_DOWN_MPH_PER_S) * speed_unit_to_ms * DT_CTRL
    planner_speed = float(getattr(actuators, "speed", 0.0) or 0.0)

    user_set_button_event = any(be.type in (ButtonType.accelCruise, ButtonType.decelCruise) for be in CS.out.buttonEvents)
    user_other_button_event = any(be.type in (ButtonType.cancel, ButtonType.mainCruise) for be in CS.out.buttonEvents)
    manual_override = CS.out.gasPressed or CS.out.brakePressed or user_set_button_event or user_other_button_event

    if not self.fake_long_prev_cruise_enabled:
      if self.fake_long_user_set_speed <= 0.0:
        self.fake_long_user_set_speed = current_set_speed
      initial_fake_speed = max(FAKE_LONG_MIN_SET_SPEED_MS, min(current_set_speed, current_v_ego))
      self.fake_long_target_speed = initial_fake_speed
      self.fake_long_commanded_speed = initial_fake_speed
      self.fake_long_prev_v_ego = current_v_ego
    elif self.fake_long_user_set_speed <= 0.0:
      self.fake_long_user_set_speed = current_set_speed
      self.fake_long_target_speed = current_set_speed
      self.fake_long_commanded_speed = current_set_speed
      self.fake_long_prev_v_ego = current_v_ego

    # Only direct RES/SET changes should update the stored user ACC target.
    if user_set_button_event and self.frame > self.fake_long_ignore_button_until:
      self.fake_long_user_set_speed = current_set_speed

    if manual_override and self.frame > self.fake_long_ignore_button_until:
      self.fake_long_pause_until = self.frame + FAKE_LONG_PAUSE_FRAMES
      self.fake_long_apn_decel_started = False
      self.fake_long_apn_recovery_started = False
      self.fake_long_apn_restore_pending = False

    self.fake_long_prev_cruise_enabled = True

    stored_user_set_speed = max(FAKE_LONG_MIN_SET_SPEED_MS, self.fake_long_user_set_speed if self.fake_long_user_set_speed > 0.0 else current_set_speed)

    if apn_fake_long_enabled and self.params_memory.get_bool("APNDataActive"):
      apn_hazard_kind = str(self.params_memory.get("APNNextHazard", return_default=True) or "")
      apn_hazard_distance = self._param_float(self.params_memory.get("APNNextHazardDistance", return_default=True))
      apn_target_speed = self._param_float(self.params_memory.get("APNNextSpeedLimit", return_default=True))
      if apn_target_speed <= 0.1:
        apn_target_speed = self._param_float(self.params_memory.get("APNSpeedLimit", return_default=True))
      if apn_target_speed > 0.1:
        apn_target_speed = max(FAKE_LONG_MIN_SET_SPEED_MS, apn_target_speed + apn_fake_long_offset)

      apn_camera_alert_distance = APN_FAKE_LONG_LONG_ALERT_DISTANCE_M if (apn_target_speed * CV.MS_TO_KPH) >= 80.0 else APN_FAKE_LONG_SHORT_ALERT_DISTANCE_M
      apn_override_active = "camera" in apn_hazard_kind and apn_hazard_distance > 0.0 and apn_hazard_distance <= apn_camera_alert_distance and apn_target_speed > 0.1
      apn_override_active &= stored_user_set_speed > (apn_target_speed + speed_hysteresis)

    if not apn_override_active:
      self.fake_long_apn_decel_started = False
    else:
      self.fake_long_apn_recovery_started = False

    if fake_long_enabled:
      if planner_speed > 0.0:
        raw_desired_set_speed = max(FAKE_LONG_MIN_SET_SPEED_MS, min(stored_user_set_speed, planner_speed + planner_headroom))
      else:
        raw_desired_set_speed = stored_user_set_speed

      if current_v_ego + speed_hysteresis < stored_user_set_speed:
        raw_desired_set_speed = min(raw_desired_set_speed, max(FAKE_LONG_MIN_SET_SPEED_MS, current_v_ego))
      elif raw_desired_set_speed < current_set_speed:
        raw_desired_set_speed = min(raw_desired_set_speed, max(FAKE_LONG_MIN_SET_SPEED_MS, current_v_ego + follow_headroom))
    else:
      raw_desired_set_speed = stored_user_set_speed

    if apn_override_active:
      raw_desired_set_speed = min(raw_desired_set_speed, apn_target_speed)

    actual_speed_drop = max(0.0, self.fake_long_prev_v_ego - current_v_ego)
    self.fake_long_prev_v_ego = current_v_ego

    if self.fake_long_commanded_speed <= 0.0:
      self.fake_long_commanded_speed = raw_desired_set_speed

    if raw_desired_set_speed < self.fake_long_commanded_speed - target_hysteresis:
      decel_step = max(actual_speed_drop * FAKE_LONG_DECEL_TRACK_FACTOR, FAKE_LONG_DECEL_MIN_STEP_MS, smooth_down_step)
      self.fake_long_commanded_speed = max(raw_desired_set_speed, self.fake_long_commanded_speed - decel_step)
    elif raw_desired_set_speed > self.fake_long_commanded_speed + target_hysteresis:
      self.fake_long_commanded_speed = min(raw_desired_set_speed, self.fake_long_commanded_speed + smooth_up_step)

    self.fake_long_target_speed = raw_desired_set_speed
    desired_set_speed = self.fake_long_commanded_speed

    self.apply_speed = desired_set_speed
    apn_recovery_active = apn_fake_long_enabled and self.fake_long_apn_recovery_started and self.fake_long_apn_restore_pending and not apn_override_active
    apn_control_active = apn_fake_long_enabled and self.fake_long_apn_decel_started and apn_override_active

    if self.frame < self.fake_long_pause_until:
      self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled, apn_override=apn_override_active,
                                  apn_target_speed=apn_target_speed, apn_hazard_distance=apn_hazard_distance, apn_hazard=apn_hazard_kind,
                                  apn_control_active=apn_control_active, apn_recovery_active=apn_recovery_active)
      return None

    button = CruiseButtons.INIT
    if current_set_speed > desired_set_speed + speed_hysteresis:
      button = CruiseButtons.DECEL_SET
    elif (current_set_speed + speed_hysteresis) < desired_set_speed:
      button = CruiseButtons.RES_ACCEL

    diff_units = abs(current_set_speed - desired_set_speed) / speed_unit_to_ms
    interval_frames = self.get_fake_long_interval_frames(diff_units)

    if button != CruiseButtons.INIT and (self.frame - self.last_button_frame) >= interval_frames:
      self.last_button_frame = self.frame
      self.fake_long_ignore_button_until = self.frame + FAKE_LONG_IGNORE_ECHO_FRAMES
      self.fake_long_last_button = "set" if button == CruiseButtons.DECEL_SET else "res"
      if apn_override_active and button == CruiseButtons.DECEL_SET:
        self.fake_long_apn_decel_started = True
        self.fake_long_apn_recovery_started = False
        self.fake_long_apn_restore_pending = True
      elif apn_fake_long_enabled and self.fake_long_apn_restore_pending and not apn_override_active and button == CruiseButtons.RES_ACCEL:
        self.fake_long_apn_recovery_started = True

      apn_control_active = apn_fake_long_enabled and self.fake_long_apn_decel_started and apn_override_active
      apn_recovery_active = apn_fake_long_enabled and self.fake_long_apn_recovery_started and self.fake_long_apn_restore_pending and not apn_override_active
      self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                  cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                  current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                  apn_enabled=apn_fake_long_enabled, apn_override=apn_override_active,
                                  apn_target_speed=apn_target_speed, apn_hazard_distance=apn_hazard_distance, apn_hazard=apn_hazard_kind,
                                  apn_control_active=apn_control_active, apn_recovery_active=apn_recovery_active)
      return self.create_fake_long_press_command(CS, button)

    if apn_fake_long_enabled and apn_recovery_active and current_set_speed >= stored_user_set_speed - speed_hysteresis:
      self.fake_long_apn_recovery_started = False
      self.fake_long_apn_restore_pending = False
      apn_recovery_active = False

    self.update_fake_long_debug(active=True, test_ui=test_ui_enabled, cruise_enabled=cruise_enabled,
                                cruise_available=CS.out.cruiseState.available, current_set_speed=current_set_speed,
                                current_v_ego=current_v_ego, raw_current_set_speed=current_set_speed_raw,
                                apn_enabled=apn_fake_long_enabled, apn_override=apn_override_active,
                                apn_target_speed=apn_target_speed, apn_hazard_distance=apn_hazard_distance, apn_hazard=apn_hazard_kind,
                                apn_control_active=apn_control_active, apn_recovery_active=apn_recovery_active)
    return None

  def consume_fake_long_test_button(self, CS, frogpilot_toggles):
    test_ui_enabled = bool(getattr(frogpilot_toggles, "fake_long_test_ui", False))
    stock_acc_path = self.CP.pcmCruise and not self.CP.openpilotLongitudinalControl and self.CP.networkLocation == NetworkLocation.fwdCamera
    if not (test_ui_enabled and stock_acc_path):
      return None

    raw_value = self.params_memory.get("FakeLongTestButton")
    if not raw_value:
      return None

    raw_text = raw_value if isinstance(raw_value, str) else raw_value.decode("utf-8", errors="ignore")
    raw_text = raw_text.strip()
    if not raw_text:
      return None

    payload = None
    try:
      parsed = json.loads(raw_text)
      if isinstance(parsed, dict):
        payload = parsed
    except Exception:
      payload = None

    legacy_button_key = ""
    legacy_timestamp_ms = 0.0
    if payload is None:
      button_key, sep, button_ts = raw_text.partition(":")
      if not sep:
        return None
      legacy_button_key = button_key.strip().lower()
      legacy_timestamp_ms = self._param_float(button_ts, 0.0)

    self.params_memory.remove("FakeLongTestButton")

    if payload is not None:
      sent_at_ms = self._param_float(payload.get("sentAtMs"), 0.0)
      button_key = str(payload.get("button", "")).strip().lower()
      mode = str(payload.get("mode", "tap")).strip().lower()
      repeats = int(max(1, min(12, self._param_float(payload.get("repeats"), FAKE_LONG_PRESS_REPEAT_COUNT))))
      hold_frames = int(max(0, min(40, self._param_float(payload.get("holdFrames"), 0))))
    else:
      sent_at_ms = legacy_timestamp_ms
      button_key = legacy_button_key
      mode = "tap"
      repeats = FAKE_LONG_PRESS_REPEAT_COUNT
      hold_frames = 0

    if sent_at_ms > 0.0 and (time.time() - (sent_at_ms / 1000.0)) > FAKE_LONG_TEST_BUTTON_MAX_AGE_S:
      return None

    if float(CS.out.vEgo) < FAKE_LONG_MIN_SEND_SPEED_MS:
      return None

    bus_mode = self._normalize_fake_long_bus_mode("camera")

    button_map = {
      "main": CruiseButtons.MAIN,
      "cancel": CruiseButtons.CANCEL,
      "res": CruiseButtons.RES_ACCEL,
      "set": CruiseButtons.DECEL_SET,
      "unpress": CruiseButtons.UNPRESS,
    }
    button = button_map.get(button_key, CruiseButtons.INIT)
    if button == CruiseButtons.INIT:
      return None

    # Treat test buttons as their own isolated path. Clear any pending repeat
    # or release from previous test/controller commands before queueing a new one.
    self.clear_fake_long_pending_buttons()
    self.fake_long_pause_until = self.frame + FAKE_LONG_PAUSE_FRAMES
    self.fake_long_ignore_button_until = self.frame + FAKE_LONG_IGNORE_ECHO_FRAMES
    self.fake_long_sync_target_until = self.frame + FAKE_LONG_PAUSE_FRAMES
    self.fake_long_test_bus_mode = bus_mode
    self.fake_long_test_mode = mode
    self.fake_long_last_button = button_key
    self.last_button_frame = self.frame

    if button == CruiseButtons.UNPRESS or mode == "release":
      return self.create_fake_long_button_commands((CS.buttons_counter + 1) % 4, CruiseButtons.UNPRESS, bus_mode)

    return self.create_fake_long_press_command(CS, button, bus_mode=bus_mode,
                                               repeats=repeats, release=(mode != "press"),
                                               hold_frames=hold_frames)

  # OPGM variables
  @staticmethod
  def calc_pedal_command(accel: float, long_active: bool, v_ego: float) -> float:
    if not long_active:
      return 0.

    if accel < -0.5:
      pedal_gas = 0
    else:
      pedaloffset = np.interp(v_ego, [0., 3, 6, 30], [0.10, 0.175, 0.240, 0.240])
      pedal_gas = np.clip((pedaloffset + accel * 0.6), 0.0, 1.0)

    return pedal_gas

  def update(self, CC, CS, now_nanos, frogpilot_toggles):
    actuators = CC.actuators
    hud_control = CC.hudControl
    hud_alert = hud_control.visualAlert
    hud_v_cruise = hud_control.setSpeed
    if hud_v_cruise > 70:
      hud_v_cruise = 0

    # Send CAN commands.
    can_sends = []

    # Steering (Active: 50Hz, inactive: 10Hz)
    steer_step = self.params.STEER_STEP if CC.latActive else self.params.INACTIVE_STEER_STEP

    if self.CP.networkLocation == NetworkLocation.fwdCamera:
      # Also send at 50Hz:
      # - on startup, first few msgs are blocked
      # - until we're in sync with camera so counters align when relay closes, preventing a fault.
      #   openpilot can subtly drift, so this is activated throughout a drive to stay synced
      out_of_sync = self.lka_steering_cmd_counter % 4 != (CS.cam_lka_steering_cmd_counter + 1) % 4
      if CS.loopback_lka_steering_cmd_ts_nanos == 0 or out_of_sync:
        steer_step = self.params.STEER_STEP

    self.lka_steering_cmd_counter += 1 if CS.loopback_lka_steering_cmd_updated else 0

    # Avoid GM EPS faults when transmitting messages too close together: skip this transmit if we
    # received the ASCMLKASteeringCmd loopback confirmation too recently
    last_lka_steer_msg_ms = (now_nanos - CS.loopback_lka_steering_cmd_ts_nanos) * 1e-6
    if (self.frame - self.last_steer_frame) >= steer_step and last_lka_steer_msg_ms > MIN_STEER_MSG_INTERVAL_MS:
      # Initialize ASCMLKASteeringCmd counter using the camera until we get a msg on the bus
      if CS.loopback_lka_steering_cmd_ts_nanos == 0:
        self.lka_steering_cmd_counter = CS.pt_lka_steering_cmd_counter + 1

      if CC.latActive:
        new_torque = int(round(actuators.torque * self.params.STEER_MAX))
        apply_torque = apply_driver_steer_torque_limits(new_torque, self.apply_torque_last, CS.out.steeringTorque, self.params)
      else:
        apply_torque = 0

      self.last_steer_frame = self.frame
      self.apply_torque_last = apply_torque
      idx = self.lka_steering_cmd_counter % 4
      can_sends.append(gmcan.create_steering_control(self.packer_pt, CanBus.POWERTRAIN, apply_torque, idx, CC.latActive))

    if self.CP.openpilotLongitudinalControl:
      # Gas/regen, brakes, and UI commands - all at 25Hz
      if self.frame % 4 == 0:
        stopping = actuators.longControlState == LongCtrlState.stopping
        interceptor_gas_cmd = 0
        if not CC.longActive:
          # ASCM sends max regen when not enabled
          self.apply_gas = self.params.INACTIVE_REGEN
          self.apply_brake = 0
        else:
          self.apply_gas = float(np.interp(actuators.accel, self.params.GAS_LOOKUP_BP, self.params.GAS_LOOKUP_V))
          self.apply_brake = int(round(np.interp(actuators.accel, self.params.BRAKE_LOOKUP_BP, self.params.BRAKE_LOOKUP_V)))
          # Don't allow any gas above inactive regen while stopping
          # FIXME: brakes aren't applied immediately when enabling at a stop
          if stopping:
            self.apply_gas = self.params.INACTIVE_REGEN

          # OPGM variables
          if self.CP.carFingerprint in CC_ONLY_CAR:
            # gas interceptor only used for full long control on cars without ACC
            interceptor_gas_cmd = self.calc_pedal_command(actuators.accel, CC.longActive, CS.out.vEgo)

        idx = (self.frame // 4) % 4

        at_full_stop = CC.longActive and CS.out.standstill
        near_stop = CC.longActive and (abs(CS.out.vEgo) < self.params.NEAR_STOP_BRAKE_PHASE)
        if self.CP.flags & GMFlags.CC_LONG.value:
          if CC.longActive and CS.out.vEgo > self.CP.minEnableSpeed:
            # Using extend instead of append since the message is only sent intermittently
            can_sends.extend(gmcan.create_gm_cc_spam_command(self.packer_pt, self, CS, actuators))
        if self.CP.enableGasInterceptorDEPRECATED:
          can_sends.append(create_gas_interceptor_command(self.packer_pt, interceptor_gas_cmd, idx))
        if self.CP.carFingerprint not in CC_ONLY_CAR:
          friction_brake_bus = CanBus.CHASSIS
          # GM Camera exceptions
          # TODO: can we always check the longControlState?
          if self.CP.networkLocation == NetworkLocation.fwdCamera and self.CP.carFingerprint not in CC_ONLY_CAR:
            at_full_stop = at_full_stop and stopping
            friction_brake_bus = CanBus.POWERTRAIN

          # FrogPilot variables
          if CC.cruiseControl.resume and CS.out.cruiseState.standstill and frogpilot_toggles.volt_sng:
            acc_engaged = False
          else:
            acc_engaged = CC.enabled

          # GasRegenCmdActive needs to be 1 to avoid cruise faults. It describes the ACC state, not actuation
          can_sends.append(gmcan.create_gas_regen_command(self.packer_pt, CanBus.POWERTRAIN, self.apply_gas, idx, acc_engaged, at_full_stop))
          can_sends.append(gmcan.create_friction_brake_command(self.packer_ch, friction_brake_bus, self.apply_brake,
                                                             idx, CC.enabled, near_stop, at_full_stop, self.CP))

          # Send dashboard UI commands (ACC status)
          send_fcw = hud_alert == VisualAlert.fcw
          can_sends.append(gmcan.create_acc_dashboard_command(self.packer_pt, CanBus.POWERTRAIN, CC.enabled,
                                                              hud_v_cruise * CV.MS_TO_KPH, hud_control, send_fcw))

      # Radar needs to know current speed and yaw rate (50hz),
      # and that ADAS is alive (10hz)
      if not self.CP.radarUnavailable:
        tt = self.frame * DT_CTRL
        time_and_headlights_step = 10
        if self.frame % time_and_headlights_step == 0:
          idx = (self.frame // time_and_headlights_step) % 4
          can_sends.append(gmcan.create_adas_time_status(CanBus.OBSTACLE, int((tt - self.start_time) * 60), idx))
          can_sends.append(gmcan.create_adas_headlights_status(self.packer_obj, CanBus.OBSTACLE))

        speed_and_accelerometer_step = 2
        if self.frame % speed_and_accelerometer_step == 0:
          idx = (self.frame // speed_and_accelerometer_step) % 4
          can_sends.append(gmcan.create_adas_steering_status(CanBus.OBSTACLE, idx))
          can_sends.append(gmcan.create_adas_accelerometer_speed_status(CanBus.OBSTACLE, abs(CS.out.vEgo), idx))

      if self.CP.networkLocation == NetworkLocation.gateway and self.frame % self.params.ADAS_KEEPALIVE_STEP == 0:
        can_sends += gmcan.create_adas_keepalive(CanBus.POWERTRAIN)

      # OPGM variables
      # Pedal interceptor: always send CANCEL when cruise is on
      if (self.CP.flags & GMFlags.PEDAL_LONG.value) and CS.out.cruiseState.enabled:
        if (self.frame - self.last_button_frame) * DT_CTRL > 0.04:
          self.last_button_frame = self.frame
          can_sends.append(gmcan.create_buttons(self.packer_pt, CanBus.POWERTRAIN, (CS.buttons_counter + 1) % 4, CruiseButtons.CANCEL))
      # CC_LONG: only send CANCEL on OP disengage when cruise is still on
      elif ((self.CP.flags & GMFlags.CC_LONG.value) and self.prev_op_enabled and not CC.enabled and CS.out.cruiseState.enabled):
        if (self.frame - self.last_button_frame) * DT_CTRL > 0.04:
          self.last_button_frame = self.frame
          can_sends.append(gmcan.create_buttons(self.packer_pt, CanBus.POWERTRAIN, (CS.buttons_counter + 1) % 4, CruiseButtons.CANCEL))

    else:
      # While car is braking, cancel button causes ECM to enter a soft disable state with a fault status.
      # A delayed cancellation allows camera to cancel and avoids a fault when user depresses brake quickly
      self.cancel_counter = self.cancel_counter + 1 if CC.cruiseControl.cancel else 0

      # Stock longitudinal, integrated at camera
      if (self.frame - self.last_button_frame) * DT_CTRL > 0.04:
        self.update_fake_long_session_state(CS)
        fake_long_press_repeat_send = self.consume_fake_long_press_repeat(CS)
        if fake_long_press_repeat_send is not None:
          can_sends.extend(fake_long_press_repeat_send)
        else:
          fake_long_release_send = self.consume_fake_long_release(CS)
          if fake_long_release_send is not None:
            can_sends.extend(fake_long_release_send)
          else:
            if self.cancel_counter > CAMERA_CANCEL_DELAY_FRAMES:
              self.last_button_frame = self.frame
              can_sends.append(gmcan.create_buttons(self.packer_pt, CanBus.CAMERA, CS.buttons_counter, CruiseButtons.CANCEL))
            else:
              fake_long_test_send = self.consume_fake_long_test_button(CS, frogpilot_toggles)
              if fake_long_test_send is not None:
                can_sends.extend(fake_long_test_send)
              else:
                fake_long_send = self.create_fake_long_command(CS, actuators, frogpilot_toggles, CC.enabled)
                if fake_long_send is not None:
                  can_sends.extend(fake_long_send)

    if self.CP.networkLocation == NetworkLocation.fwdCamera:
      # Silence "Take Steering" alert sent by camera, forward PSCMStatus with HandsOffSWlDetectionStatus=1
      if self.frame % 10 == 0:
        can_sends.append(gmcan.create_pscm_status(self.packer_pt, CanBus.CAMERA, CS.pscm_status))

    new_actuators = actuators.as_builder()
    new_actuators.torque = self.apply_torque_last / self.params.STEER_MAX
    new_actuators.torqueOutputCan = self.apply_torque_last
    new_actuators.gas = self.apply_gas
    new_actuators.brake = self.apply_brake

    self.frame += 1

    # OPGM variables
    new_actuators.speed = self.apply_speed

    self.prev_op_enabled = CC.enabled

    return new_actuators, can_sends
