#!/usr/bin/env python3
import bisect
import math
import os
from enum import IntEnum
from collections.abc import Callable
from types import SimpleNamespace

from cereal import log, car, custom
import cereal.messaging as messaging
from openpilot.common.constants import CV
from openpilot.common.git import get_short_branch
from openpilot.common.params import Params
from openpilot.common.realtime import DT_CTRL
from openpilot.selfdrive.controls.lib.desire_helper import LaneChangeDirection
from openpilot.selfdrive.locationd.calibrationd import MIN_SPEED_FILTER
from openpilot.system.micd import SAMPLE_RATE, SAMPLE_BUFFER
from openpilot.selfdrive.ui.feedback.feedbackd import FEEDBACK_MAX_DURATION
from openpilot.system.hardware import HARDWARE

AlertSize = log.SelfdriveState.AlertSize
AlertStatus = log.SelfdriveState.AlertStatus
VisualAlert = car.CarControl.HUDControl.VisualAlert
AudibleAlert = car.CarControl.HUDControl.AudibleAlert
EventName = log.OnroadEvent.EventName

# FrogPilot variables
FrogPilotAlertStatus = custom.FrogPilotSelfdriveState.AlertStatus
FrogPilotAudibleAlert = custom.FrogPilotCarControl.HUDControl.AudibleAlert
FrogPilotEventName = custom.FrogPilotOnroadEvent.EventName


# Alert priorities
class Priority(IntEnum):
  LOWEST = 0
  LOWER = 1
  LOW = 2
  MID = 3
  HIGH = 4
  HIGHEST = 5


# Event types
class ET:
  ENABLE = 'enable'
  PRE_ENABLE = 'preEnable'
  OVERRIDE_LATERAL = 'overrideLateral'
  OVERRIDE_LONGITUDINAL = 'overrideLongitudinal'
  NO_ENTRY = 'noEntry'
  WARNING = 'warning'
  USER_DISABLE = 'userDisable'
  SOFT_DISABLE = 'softDisable'
  IMMEDIATE_DISABLE = 'immediateDisable'
  PERMANENT = 'permanent'


# get event name from enum
EVENT_NAME = {v: k for k, v in EventName.schema.enumerants.items()}

# FrogPilot variables
FROGPILOT_EVENT_NAME = {v: k for k, v in FrogPilotEventName.schema.enumerants.items()}


class Events:
  def __init__(self, frogpilot=False):
    self.events: list[int] = []
    self.static_events: list[int] = []
    self.event_counters = dict.fromkeys((FROGPILOT_EVENTS if frogpilot else EVENTS).keys(), 0)

    # FrogPilot variables
    self.frogpilot = frogpilot

  @property
  def names(self) -> list[int]:
    return self.events

  def __len__(self) -> int:
    return len(self.events)

  def add(self, event_name: int, static: bool=False) -> None:
    if static:
      bisect.insort(self.static_events, event_name)
    bisect.insort(self.events, event_name)

  def remove(self, event_name: int) -> None:
    self.events = [e for e in self.events if e != event_name]

  def remove_many(self, event_names: list[int] | tuple[int, ...] | set[int]) -> None:
    to_remove = set(event_names)
    self.events = [e for e in self.events if e not in to_remove]

  def clear(self) -> None:
    self.event_counters = {k: (v + 1 if k in self.events else 0) for k, v in self.event_counters.items()}
    self.events = self.static_events.copy()

  def contains(self, event_type: str) -> bool:
    return any(event_type in (FROGPILOT_EVENTS if self.frogpilot else EVENTS).get(e, {}) for e in self.events)

  def create_alerts(self, event_types: list[str], callback_args=None):
    if callback_args is None:
      callback_args = []

    ret = []
    for e in self.events:
      types = (FROGPILOT_EVENTS if self.frogpilot else EVENTS)[e].keys()
      for et in event_types:
        if et in types:
          alert = (FROGPILOT_EVENTS if self.frogpilot else EVENTS)[e][et]
          if not isinstance(alert, Alert):
            alert = alert(*callback_args)

          if DT_CTRL * (self.event_counters[e] + 1) >= alert.creation_delay:
            alert.alert_type = f"{(FROGPILOT_EVENT_NAME if self.frogpilot else EVENT_NAME)[e]}/{et}"
            alert.event_type = et
            ret.append(alert)
    return ret

  def add_from_msg(self, events):
    for e in events:
      bisect.insort(self.events, e.name.raw)

  def to_msg(self):
    ret = []
    for event_name in self.events:
      event = (custom.FrogPilotOnroadEvent if self.frogpilot else log.OnroadEvent).new_message()
      event.name = event_name
      for event_type in (FROGPILOT_EVENTS if self.frogpilot else EVENTS).get(event_name, {}):
        setattr(event, event_type, True)
      ret.append(event)
    return ret


class Alert:
  def __init__(self,
               alert_text_1: str,
               alert_text_2: str,
               alert_status: log.SelfdriveState.AlertStatus,
               alert_size: log.SelfdriveState.AlertSize,
               priority: Priority,
               visual_alert: car.CarControl.HUDControl.VisualAlert,
               audible_alert: car.CarControl.HUDControl.AudibleAlert,
               duration: float,
               creation_delay: float = 0.):

    self.alert_text_1 = alert_text_1
    self.alert_text_2 = alert_text_2
    self.alert_status = alert_status
    self.alert_size = alert_size
    self.priority = priority
    self.visual_alert = visual_alert
    self.audible_alert = audible_alert

    self.duration = int(duration / DT_CTRL)

    self.creation_delay = creation_delay

    self.alert_type = ""
    self.event_type: str | None = None

  def __str__(self) -> str:
    return f"{self.alert_text_1}/{self.alert_text_2} {self.priority} {self.visual_alert} {self.audible_alert}"

  def __gt__(self, alert2) -> bool:
    if not isinstance(alert2, Alert):
      return False
    return self.priority > alert2.priority

EmptyAlert = Alert("" , "", AlertStatus.normal, AlertSize.none, Priority.LOWEST,
                   VisualAlert.none, AudibleAlert.none, 0)

class NoEntryAlert(Alert):
  def __init__(self, alert_text_2: str,
               alert_text_1: str = "오픈파일럿 사용 불가",
               visual_alert: car.CarControl.HUDControl.VisualAlert=VisualAlert.none):
    if HARDWARE.get_device_type() == 'mici':
      alert_text_1, alert_text_2 = alert_text_2, alert_text_1
    super().__init__(alert_text_1, alert_text_2, AlertStatus.normal,
                     AlertSize.mid, Priority.LOW, visual_alert,
                     AudibleAlert.refuse, 3.)


class SoftDisableAlert(Alert):
  def __init__(self, alert_text_2: str):
    super().__init__("직접 운전을 진행해주세요", alert_text_2,
                     AlertStatus.userPrompt, AlertSize.full,
                     Priority.MID, VisualAlert.steerRequired,
                     AudibleAlert.warningSoft, 2.),


# less harsh version of SoftDisable, where the condition is user-triggered
class UserSoftDisableAlert(SoftDisableAlert):
  def __init__(self, alert_text_2: str):
    super().__init__(alert_text_2),
    self.alert_text_1 = "오픈파일럿이 곧 비활성화됩니다"


class ImmediateDisableAlert(Alert):
  def __init__(self, alert_text_2: str):
    super().__init__("직접 운전을 진행해주세요", alert_text_2,
                     AlertStatus.critical, AlertSize.full,
                     Priority.HIGHEST, VisualAlert.steerRequired,
                     AudibleAlert.warningImmediate, 4.),


class EngagementAlert(Alert):
  def __init__(self, audible_alert: car.CarControl.HUDControl.AudibleAlert):
    super().__init__("", "",
                     AlertStatus.normal, AlertSize.none,
                     Priority.MID, VisualAlert.none,
                     audible_alert, .2),


class NormalPermanentAlert(Alert):
  def __init__(self, alert_text_1: str, alert_text_2: str = "", duration: float = 0.2, priority: Priority = Priority.LOWER, creation_delay: float = 0.):
    super().__init__(alert_text_1, alert_text_2,
                     AlertStatus.normal, AlertSize.mid if len(alert_text_2) else AlertSize.small,
                     priority, VisualAlert.none, AudibleAlert.none, duration, creation_delay=creation_delay),


class StartupAlert(Alert):
  def __init__(self, alert_text_1: str, alert_text_2: str = "항시 전방을 주시하고 교통 상황에 유의하세요", alert_status=AlertStatus.normal):
    alert_size = AlertSize.mid
    if HARDWARE.get_device_type() == 'mici':
      if alert_text_2 == "항시 전방을 주시하고 교통 상황에 유의하세요":
        alert_text_2 = ""
      alert_size = AlertSize.small
    super().__init__(alert_text_1, alert_text_2,
                     alert_status, alert_size,
                     Priority.LOWER, VisualAlert.none, AudibleAlert.none, 5.),



# ********** helper functions **********
def get_display_speed(speed_ms: float, metric: bool) -> str:
  speed = int(round(speed_ms * (CV.MS_TO_KPH if metric else CV.MS_TO_MPH)))
  unit = 'km/h' if metric else 'mph'
  return f"{speed} {unit}"


# ********** alert callback functions **********

AlertCallbackType = Callable[[car.CarParams, car.CarState, messaging.SubMaster, bool, int, log.ControlsState], Alert]


def soft_disable_alert(alert_text_2: str) -> AlertCallbackType:
  def func(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
    if soft_disable_time < int(0.5 / DT_CTRL):
      return ImmediateDisableAlert(alert_text_2)
    return SoftDisableAlert(alert_text_2)
  return func

def user_soft_disable_alert(alert_text_2: str) -> AlertCallbackType:
  def func(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
    if soft_disable_time < int(0.5 / DT_CTRL):
      return ImmediateDisableAlert(alert_text_2)
    return UserSoftDisableAlert(alert_text_2)
  return func

def startup_master_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  branch = get_short_branch()  # Ensure get_short_branch is cached to avoid lags on startup
  if "REPLAY" in os.environ:
    branch = "replay"

  return StartupAlert("WARNING: This branch is not tested", branch, alert_status=AlertStatus.userPrompt)

def below_engage_speed_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NoEntryAlert(f"Drive above {get_display_speed(CP.minEnableSpeed, metric)} to engage")


def below_steer_speed_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  return Alert(
    "조향 보조 비활성화됨",
    f"{get_display_speed(CP.minSteerSpeed, metric)} 이상으로 주행하면 다시 활성화됩니다",
    AlertStatus.normal, AlertSize.full,
    Priority.LOW, VisualAlert.steerRequired, AudibleAlert.none, 0.4)


def calibration_incomplete_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  first_word = "재보정 진행중" if sm['liveCalibration'].calStatus == log.LiveCalibrationData.Status.recalibrating else "캘리브레이션이 진행중입니다"
  return Alert(
    f"{first_word}: {sm['liveCalibration'].calPerc:.0f}%",
    f"{get_display_speed(MIN_SPEED_FILTER, metric)} 이상으로 주행하세요",
    AlertStatus.normal, AlertSize.mid,
    Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2)


def audio_feedback_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  duration = FEEDBACK_MAX_DURATION - ((sm['audioFeedback'].blockNum + 1) * SAMPLE_BUFFER / SAMPLE_RATE)
  return NormalPermanentAlert(
    "Recording Audio Feedback",
    f"{round(duration)} second{'s' if round(duration) != 1 else ''} remaining. Press again to save early.",
    priority=Priority.LOW)


# *** debug alerts ***

def out_of_space_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  full_perc = round(100. - sm['deviceState'].freeSpacePercent)
  return NormalPermanentAlert("용량 부족", f"{full_perc}%")


def posenet_invalid_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  mdl = sm['modelV2'].velocity.x[0] if len(sm['modelV2'].velocity.x) else math.nan
  err = CS.vEgo - mdl
  msg = f"Speed Error: {err:.1f} m/s"
  return NoEntryAlert(msg, alert_text_1="Posenet Speed Invalid")


def process_not_running_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  not_running = [p.name for p in sm['managerState'].processes if not p.running and p.shouldBeRunning]
  msg = ', '.join(not_running)
  return NoEntryAlert(msg, alert_text_1="외부 프로그램 비정상")


def comm_issue_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  ignored = set(getattr(sm, 'ignore_alive', [])) | set(getattr(sm, 'ignore_valid', [])) | set(getattr(sm, 'ignore_average_freq', []))
  bs = [s for s in sm.data.keys() if s not in ignored and not sm.all_checks([s, ])]
  msg = ', '.join(bs[:4])  # can't fit too many on one line
  return NoEntryAlert(msg, alert_text_1="차량과의 통신 오류")


def camera_malfunction_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  all_cams = ('roadCameraState', 'driverCameraState', 'wideRoadCameraState')
  bad_cams = [s.replace('State', '') for s in all_cams if s in sm.data.keys() and not sm.all_checks([s, ])]
  return NormalPermanentAlert("카메라 이상", ', '.join(bad_cams))


def calibration_invalid_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  rpy = sm['liveCalibration'].rpyCalib
  yaw = math.degrees(rpy[2] if len(rpy) == 3 else math.nan)
  pitch = math.degrees(rpy[1] if len(rpy) == 3 else math.nan)
  angles = f"기기의 각도를 조정해주세요 (Pitch: {pitch:.1f}°, Yaw: {yaw:.1f}°)"
  return NormalPermanentAlert("캘리브레이션이 필요합니다", angles)


def paramsd_invalid_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  if not sm['liveParameters'].angleOffsetValid:
    angle_offset_deg = sm['liveParameters'].angleOffsetDeg
    title = "Steering misalignment detected"
    text = f"Angle offset too high (Offset: {angle_offset_deg:.1f}°)"
  elif not sm['liveParameters'].steerRatioValid:
    steer_ratio = sm['liveParameters'].steerRatio
    title = "Steer ratio mismatch"
    text = f"Steering rack geometry may be off (Ratio: {steer_ratio:.1f})"
  elif not sm['liveParameters'].stiffnessFactorValid:
    stiffness_factor = sm['liveParameters'].stiffnessFactor
    title = "Abnormal tire stiffness"
    text = f"Check tires, pressure, or alignment (Factor: {stiffness_factor:.1f})"
  else:
    return NoEntryAlert("paramsd Temporary Error")

  return NoEntryAlert(alert_text_1=title, alert_text_2=text)

def overheat_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  cpu = max(sm['deviceState'].cpuTempC, default=0.)
  gpu = max(sm['deviceState'].gpuTempC, default=0.)
  temp = max((cpu, gpu, sm['deviceState'].memoryTempC))
  return NormalPermanentAlert("기기의 온도가 높습니다", f"{temp:.0f} °C")


def low_memory_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NormalPermanentAlert("메모리 부족", f"{sm['deviceState'].memoryUsagePercent}% 사용됨")


def high_cpu_usage_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  x = max(sm['deviceState'].cpuUsagePercent, default=0.)
  return NormalPermanentAlert("CPU 사용량이 높습니다", f"{x}% 사용됨")


def modeld_lagging_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NormalPermanentAlert("오픈파일럿이 불안정합니다", f"{sm['modelV2'].frameDropPerc:.1f}% 프레임 하락")


def wrong_car_mode_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  if frogpilot_toggles.has_cc_long:
    text = "Enable Cruise Control to Engage"
  elif CP.brand == "honda":
    text = "Enable Main Switch to Engage"
  else:
    text = "Enable Adaptive Cruise to Engage"
  return NoEntryAlert(text)


def joystick_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  gb = sm['carControl'].actuators.accel / 4.
  steer = sm['carControl'].actuators.torque
  vals = f"Gas: {round(gb * 100.)}%, Steer: {round(steer * 100.)}%"
  return NormalPermanentAlert("Joystick Mode", vals)


def longitudinal_maneuver_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  ad = sm['alertDebug']
  audible_alert = AudibleAlert.prompt if 'Active' in ad.alertText1 else AudibleAlert.none
  alert_status = AlertStatus.userPrompt if 'Active' in ad.alertText1 else AlertStatus.normal
  alert_size = AlertSize.mid if ad.alertText2 else AlertSize.small
  return Alert(ad.alertText1, ad.alertText2,
               alert_status, alert_size,
               Priority.LOW, VisualAlert.none, audible_alert, 0.2)


def personality_changed_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  personality = str(personality).title()
  return NormalPermanentAlert(f"Driving Personality: {personality}", duration=1.5)


def invalid_lkas_setting_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  text = "Toggle stock LKAS on or off to engage"
  if CP.brand == "tesla":
    text = "Switch to Traffic-Aware Cruise Control to engage"
  elif CP.brand == "mazda":
    text = "Enable your car's LKAS to engage"
  elif CP.brand == "nissan":
    text = "Disable your car's stock LKAS to engage"
  return NormalPermanentAlert("Invalid LKAS setting", text)


# FrogPilot variables
def custom_startup_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  return StartupAlert(frogpilot_toggles.startup_alert_top, frogpilot_toggles.startup_alert_bottom, alert_status=AlertStatus.normal)


def forcing_stop_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  model_length = sm["frogpilotPlan"].forcingStopLength
  model_length_msg = f"{model_length:.1f} meters" if metric else f"{model_length * CV.METER_TO_FOOT:.1f} feet"

  return Alert(
    f"Forcing the car to stop in {model_length_msg}",
    "Press the gas pedal or 'Resume' button to override",
    FrogPilotAlertStatus.frogpilot, AlertSize.mid,
    Priority.MID, VisualAlert.none, AudibleAlert.prompt, 1.)


def holiday_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  holiday_messages = {
    "new_years": "Happy New Year! 🎉",
    "valentines": "Happy Valentine's Day! ❤️",
    "st_patricks": "Happy St. Patrick's Day! 🍀",
    "world_frog_day": "Happy World Frog Day! 🐸",
    "april_fools": "Happy April Fool's Day! 🤡",
    "easter_week": "Happy Easter! 🐰",
    "may_the_fourth": "May the 4th be with you! 🚀",
    "cinco_de_mayo": "¡Feliz Cinco de Mayo! 🌮",
    "stitch_day": "Happy Stitch Day! 💙",
    "fourth_of_july": "Happy Fourth of July! 🎆",
    "halloween_week": "Happy Halloween! 🎃",
    "thanksgiving_week": "Happy Thanksgiving! 🦃",
    "christmas_week": "Merry Christmas! 🎄",
  }

  return Alert(
    holiday_messages.get(frogpilot_toggles.current_holiday_theme, ""),
    "",
    AlertStatus.normal, AlertSize.small,
    Priority.LOWEST, VisualAlert.none, FrogPilotAudibleAlert.startup, 5.)


def nnff_loaded_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  model_name = Params().get("NNFFModelName")
  if model_name is None:
    return Alert(
      "NNFF 토크 컨트롤러 사용 불가",
      "주행 로그를 기부하면 차량 지원에 도움이 됩니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 10.0)
  else:
    return Alert(
      "NNFF 토크 컨트롤러 로드됨",
      "인공 신경망 기반 모델이 차량을 제어합니다",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.engage, 5.0)


def no_lane_available_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, personality, frogpilot_toggles: SimpleNamespace) -> Alert:
  lane_width = sm["frogpilotPlan"].laneWidthLeft if sm["modelV2"].meta.laneChangeDirection == LaneChangeDirection.left else sm["frogpilotPlan"].laneWidthRight
  lane_width_msg = f"{lane_width:.1f} 미터" if metric else f"{lane_width * CV.METER_TO_FOOT:.1f} 피트"

  return Alert(
    "차선 변경 불가",
    f"변경할 차선의 공간이 부족합니다 - {lane_width_msg}",
    AlertStatus.normal, AlertSize.mid,
    Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2)



EVENTS: dict[int, dict[str, Alert | AlertCallbackType]] = {
  # ********** events with no alerts **********

  EventName.stockFcw: {},
  EventName.actuatorsApiUnavailable: {},

  # ********** events only containing alerts displayed in all states **********

  EventName.joystickDebug: {
    ET.WARNING: joystick_alert,
    ET.PERMANENT: NormalPermanentAlert("Joystick Mode"),
  },

  EventName.longitudinalManeuver: {
    ET.WARNING: longitudinal_maneuver_alert,
    ET.PERMANENT: NormalPermanentAlert("Longitudinal Maneuver Mode",
                                       "Ensure road ahead is clear"),
  },

  EventName.selfdriveInitializing: {
    ET.NO_ENTRY: NoEntryAlert("시스템 시작중"),
  },

  EventName.startup: {
    ET.PERMANENT: StartupAlert("Openpilot 활성화 가능", "항시 전방을 주시하고 교통 상황에 유의하세요")
  },

  EventName.startupMaster: {
    ET.PERMANENT: startup_master_alert,
  },

  EventName.startupNoControl: {
    ET.PERMANENT: StartupAlert("Dashcam mode"),
    ET.NO_ENTRY: NoEntryAlert("Dashcam mode"),
  },

  EventName.startupNoCar: {
    ET.PERMANENT: StartupAlert("Dashcam mode for unsupported car"),
  },

  EventName.startupNoSecOcKey: {
    ET.PERMANENT: NormalPermanentAlert("Dashcam Mode",
                                       "Security Key Not Available",
                                       priority=Priority.HIGH),
  },

  EventName.dashcamMode: {
    ET.PERMANENT: NormalPermanentAlert("Dashcam Mode",
                                       priority=Priority.LOWEST),
  },

  EventName.invalidLkasSetting: {
    ET.PERMANENT: invalid_lkas_setting_alert,
    ET.NO_ENTRY: NoEntryAlert("Invalid LKAS setting"),
  },

  EventName.cruiseMismatch: {
    #ET.PERMANENT: ImmediateDisableAlert("openpilot failed to cancel cruise"),
  },

  # openpilot doesn't recognize the car. This switches openpilot into a
  # read-only mode. This can be solved by adding your fingerprint.
  # See https://github.com/commaai/openpilot/wiki/Fingerprinting for more information
  EventName.carUnrecognized: {
    ET.PERMANENT: NormalPermanentAlert("Dashcam Mode",
                                       "Car Unrecognized",
                                       priority=Priority.LOWEST),
  },

  EventName.aeb: {
    ET.PERMANENT: Alert(
      "BRAKE!",
      "Emergency Braking: Risk of Collision",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGHEST, VisualAlert.fcw, AudibleAlert.none, 2.),
    ET.NO_ENTRY: NoEntryAlert("AEB: Risk of Collision"),
  },

  EventName.stockAeb: {
    ET.PERMANENT: Alert(
      "AEB 작동",
      "충돌의 위험이 있어 차량의 순정 AEB가 작동했습니다",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGHEST, VisualAlert.fcw, AudibleAlert.none, 2.),
    ET.NO_ENTRY: NoEntryAlert("충돌의 위험이 있어 차량의 순정 AEB가 작동했습니다"),
  },

  EventName.fcw: {
    ET.PERMANENT: Alert(
      "전방 추돌 주의",
      "전방 차량과 추돌 위험이 있습니다",
      AlertStatus.critical, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.fcw, AudibleAlert.prompt, 3.),
  },

  EventName.ldw: {
    ET.PERMANENT: Alert(
      "차선 이탈 감지됨",
      "운전에 주의하세요",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.ldw, AudibleAlert.prompt, 3.),
  },

  # ********** events only containing alerts that display while engaged **********

  EventName.steerTempUnavailableSilent: {
    ET.WARNING: Alert(
      "조향 제어 불안정",
      "",
      AlertStatus.userPrompt, AlertSize.small,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.prompt, 1.8),
  },

  EventName.preDriverDistracted: {
    ET.PERMANENT: Alert(
      "운전에 집중하세요",
      "운전자 부주의 감지됨",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.promptDriverDistracted: {
    ET.PERMANENT: Alert(
      "운전에 집중하세요",
      "미응답시 오픈파일럿이 비활성화됩니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.MID, VisualAlert.steerRequired, AudibleAlert.promptDistracted, .1),
  },

  EventName.driverDistracted: {
    ET.PERMANENT: Alert(
      "오픈파일럿 비활성화",
      "운전자 부주의 감지됨",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGH, VisualAlert.steerRequired, AudibleAlert.warningImmediate, .1),
  },

  EventName.preDriverUnresponsive: {
    ET.PERMANENT: Alert(
      "스티어링 휠을 잡아주세요 (운전자 감지 안됨)",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.none, .1),
  },

  EventName.promptDriverUnresponsive: {
    ET.PERMANENT: Alert(
      "스티어링 휠을 잡아주세요",
      "운전자 응답 없음",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.MID, VisualAlert.steerRequired, AudibleAlert.promptDistracted, .1),
  },

  EventName.driverUnresponsive: {
    ET.PERMANENT: Alert(
      "오픈파일럿 비활성화",
      "운전자 응답 없음",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGH, VisualAlert.steerRequired, AudibleAlert.warningImmediate, .1),
  },

  EventName.manualRestart: {
    ET.WARNING: Alert(
      "수동 운전 요구됨",
      "직접 운전을 진행하세요",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .2),
  },

  EventName.resumeRequired: {
    ET.WARNING: Alert(
      "오토 홀드",
      "해제하려면 악셀을 밟거나 RES버튼을 누르세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .2),
  },

  EventName.belowSteerSpeed: {
    ET.WARNING: below_steer_speed_alert,
  },

  EventName.preLaneChangeLeft: {
    ET.WARNING: Alert(
      "좌측 차선 변경 승인 대기중",
      "핸들을 돌려 차선 변경을 승인해주세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.preLaneChangeRight: {
    ET.WARNING: Alert(
      "우측 차선 변경 승인 대기중",
      "핸들을 돌려 차선 변경을 승인해주세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.laneChangeBlocked: {
    ET.WARNING: Alert(
      "차선 변경 대기중",
      "사각지대에 차량이 감지되었습니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, .1),
  },

  EventName.laneChange: {
    ET.WARNING: Alert(
      "차선 변경 중",
      "전후방 및 측면에 유의하세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.steerSaturated: {
    ET.WARNING: Alert(
      "핸들을 조작해주세요",
      "조향각 한계에 도달했습니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.warningSoft, 2.),
  },

  # Thrown when the fan is driven at >50% but is not rotating
  EventName.fanMalfunction: {
    ET.PERMANENT: NormalPermanentAlert("쿨링팬 이상 감지됨", "하드웨어를 점검해주세요"),
  },

  # Camera is not outputting frames
  EventName.cameraMalfunction: {
    ET.PERMANENT: camera_malfunction_alert,
    ET.SOFT_DISABLE: soft_disable_alert("카메라 이상 감지됨"),
    ET.NO_ENTRY: NoEntryAlert("기기를 재부팅해주세요"),
  },
  # Camera framerate too low
  EventName.cameraFrameRate: {
    ET.PERMANENT: NormalPermanentAlert("카메라의 프레임이 낮습니다", "기기를 재부팅하세요"),
    ET.SOFT_DISABLE: soft_disable_alert("카메라의 프레임이 낮습니다"),
    ET.NO_ENTRY: NoEntryAlert("카메라의 프레임이 낮습니다: 기기를 재부팅하세요"),
  },

  # Unused

  EventName.locationdTemporaryError: {
    ET.NO_ENTRY: NoEntryAlert("locationd Temporary Error"),
    ET.SOFT_DISABLE: soft_disable_alert("locationd Temporary Error"),
  },

  EventName.locationdPermanentError: {
    ET.NO_ENTRY: NoEntryAlert("locationd Permanent Error"),
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("locationd Permanent Error"),
    ET.PERMANENT: NormalPermanentAlert("locationd Permanent Error"),
  },

  # openpilot tries to learn certain parameters about your car by observing
  # how the car behaves to steering inputs from both human and openpilot driving.
  # This includes:
  # - steer ratio: gear ratio of the steering rack. Steering angle divided by tire angle
  # - tire stiffness: how much grip your tires have
  # - angle offset: most steering angle sensors are offset and measure a non zero angle when driving straight
  # This alert is thrown when any of these values exceed a sanity check. This can be caused by
  # bad alignment or bad sensor data. If this happens consistently consider creating an issue on GitHub
  EventName.paramsdTemporaryError: {
    ET.NO_ENTRY: paramsd_invalid_alert,
    ET.SOFT_DISABLE: soft_disable_alert("paramsd Temporary Error"),
  },

  EventName.paramsdPermanentError: {
    ET.NO_ENTRY: NoEntryAlert("paramsd Permanent Error"),
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("paramsd Permanent Error"),
    ET.PERMANENT: NormalPermanentAlert("paramsd Permanent Error"),
  },

  # ********** events that affect controls state transitions **********

  EventName.pcmEnable: {
    ET.ENABLE: EngagementAlert(AudibleAlert.engage),
  },

  EventName.buttonEnable: {
    ET.ENABLE: EngagementAlert(AudibleAlert.engage),
  },

  EventName.pcmDisable: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
  },

  EventName.buttonCancel: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Cancel Pressed"),
  },

  EventName.brakeHold: {
    ET.WARNING: Alert(
      "Press Resume to Exit Brake Hold",
      "",
      AlertStatus.userPrompt, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .2),
  },

  EventName.parkBrake: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Parking Brake Engaged"),
  },

  EventName.pedalPressed: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Pedal Pressed",
                              visual_alert=VisualAlert.brakePressed),
  },

  EventName.steerDisengage: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Steering Pressed"),
  },

  EventName.preEnableStandstill: {
    ET.PRE_ENABLE: Alert(
      "오토홀드 대기중",
      "브레이크에서 발을 떼면 활성화됩니다",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1, creation_delay=1.),
  },

  EventName.gasPressedOverride: {
    ET.OVERRIDE_LONGITUDINAL: Alert(
      "",
      "",
      AlertStatus.normal, AlertSize.none,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.steerOverride: {
    ET.OVERRIDE_LATERAL: Alert(
      "",
      "",
      AlertStatus.normal, AlertSize.none,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.wrongCarMode: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: wrong_car_mode_alert,
  },

  EventName.resumeBlocked: {
    ET.NO_ENTRY: NoEntryAlert("Press Set to Engage"),
  },

  EventName.wrongCruiseMode: {
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Adaptive Cruise Disabled"),
  },

  EventName.steerTempUnavailable: {
    ET.SOFT_DISABLE: soft_disable_alert("Steering Assist Temporarily Unavailable"),
    ET.NO_ENTRY: NoEntryAlert("Steering Temporarily Unavailable"),
  },

  EventName.steerTimeLimit: {
    ET.SOFT_DISABLE: soft_disable_alert("Vehicle Steering Time Limit"),
    ET.NO_ENTRY: NoEntryAlert("Vehicle Steering Time Limit"),
  },

  EventName.outOfSpace: {
    ET.PERMANENT: out_of_space_alert,
    ET.NO_ENTRY: NoEntryAlert("Out of Storage"),
  },

  EventName.belowEngageSpeed: {
    ET.NO_ENTRY: below_engage_speed_alert,
  },

  EventName.sensorDataInvalid: {
    ET.PERMANENT: Alert(
      "Sensor Data Invalid",
      "Possible Hardware Issue",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOWER, VisualAlert.none, AudibleAlert.none, .2, creation_delay=1.),
    ET.NO_ENTRY: NoEntryAlert("Sensor Data Invalid"),
    ET.SOFT_DISABLE: soft_disable_alert("Sensor Data Invalid"),
  },

  EventName.noGps: {
  },

  EventName.tooDistracted: {
    ET.NO_ENTRY: NoEntryAlert("운전자 부주의 레벨이 너무 높습니다"),
  },

  EventName.excessiveActuation: {
    ET.SOFT_DISABLE: soft_disable_alert("Excessive Actuation"),
    ET.NO_ENTRY: NoEntryAlert("Excessive Actuation"),
  },

  EventName.overheat: {
    ET.PERMANENT: overheat_alert,
    ET.SOFT_DISABLE: soft_disable_alert("시스템 과열됨"),
    ET.NO_ENTRY: NoEntryAlert("시스템 과열됨"),
  },

  EventName.wrongGear: {
    ET.SOFT_DISABLE: user_soft_disable_alert("기어를 D에 위치하세요"),
    ET.NO_ENTRY: NoEntryAlert("기어를 D에 위치하세요"),
  },

  # This alert is thrown when the calibration angles are outside of the acceptable range.
  # For example if the device is pointed too much to the left or the right.
  # Usually this can only be solved by removing the mount from the windshield completely,
  # and attaching while making sure the device is pointed straight forward and is level.
  # See https://comma.ai/setup for more information
  EventName.calibrationInvalid: {
    ET.PERMANENT: calibration_invalid_alert,
    ET.SOFT_DISABLE: soft_disable_alert("캘리브레이션 틀어짐: 기기의 각도를 조정해주세요"),
    ET.NO_ENTRY: NoEntryAlert("캘리브레이션 틀어짐: 기기의 각도를 조정해주세요"),
  },

  EventName.calibrationIncomplete: {
    ET.PERMANENT: calibration_incomplete_alert,
    ET.SOFT_DISABLE: soft_disable_alert("캘리브레이션 미완료"),
    ET.NO_ENTRY: NoEntryAlert("캘리브레이션이 진행중입니다"),
  },

  EventName.calibrationRecalibrating: {
    ET.PERMANENT: calibration_incomplete_alert,
    ET.SOFT_DISABLE: soft_disable_alert("기기 각도 변화 감지됨: 재보정 진행중"),
    ET.NO_ENTRY: NoEntryAlert("기기 각도 변화 감지됨: 재보정 진행중"),
  },

  EventName.doorOpen: {
    ET.SOFT_DISABLE: user_soft_disable_alert("문이 열려있습니다"),
    ET.NO_ENTRY: NoEntryAlert("문이 열려있습니다"),
  },

  EventName.seatbeltNotLatched: {
    ET.SOFT_DISABLE: user_soft_disable_alert("안전벨트를 착용하세요"),
    ET.NO_ENTRY: NoEntryAlert("안전벨트를 착용하세요"),
  },

  EventName.espDisabled: {
    ET.SOFT_DISABLE: soft_disable_alert("ESP 비활성화됨"),
    ET.NO_ENTRY: NoEntryAlert("ESP 비활성화됨"),
  },

  EventName.lowBattery: {
    ET.SOFT_DISABLE: soft_disable_alert("배터리 부족"),
    ET.NO_ENTRY: NoEntryAlert("배터리 부족"),
  },

  # Different openpilot services communicate between each other at a certain
  # interval. If communication does not follow the regular schedule this alert
  # is thrown. This can mean a service crashed, did not broadcast a message for
  # ten times the regular interval, or the average interval is more than 10% too high.
  EventName.commIssue: {
    ET.SOFT_DISABLE: soft_disable_alert("차량과의 통신에 오류가 발생했습니다"),
    ET.NO_ENTRY: comm_issue_alert,
  },
  EventName.commIssueAvgFreq: {
    ET.SOFT_DISABLE: soft_disable_alert("차량과의 통신 빈도 낮음"),
    ET.NO_ENTRY: NoEntryAlert("차량과의 통신 빈도 낮음"),
  },

  EventName.selfdrivedLagging: {
    ET.SOFT_DISABLE: soft_disable_alert("System Lagging"),
    ET.NO_ENTRY: NoEntryAlert("Selfdrive Process Lagging: Reboot Your Device"),
  },

  # Thrown when manager detects a service exited unexpectedly while driving
  EventName.processNotRunning: {
    ET.NO_ENTRY: process_not_running_alert,
    ET.SOFT_DISABLE: soft_disable_alert("보조 서비스가 종료되었습니다"),
  },

  EventName.radarFault: {
    ET.SOFT_DISABLE: soft_disable_alert("Radar Error: Restart the Car"),
    ET.NO_ENTRY: NoEntryAlert("Radar Error: Restart the Car"),
  },

  EventName.radarTempUnavailable: {
    ET.SOFT_DISABLE: soft_disable_alert("Radar Temporarily Unavailable"),
    ET.NO_ENTRY: NoEntryAlert("Radar Temporarily Unavailable"),
  },

  # Every frame from the camera should be processed by the model. If modeld
  # is not processing frames fast enough they have to be dropped. This alert is
  # thrown when over 20% of frames are dropped.
  EventName.modeldLagging: {
    ET.SOFT_DISABLE: soft_disable_alert("오픈파일럿이 불안정합니다"),
    ET.NO_ENTRY: NoEntryAlert("오픈파일럿이 불안정합니다"),
    ET.PERMANENT: modeld_lagging_alert,
  },

  # Besides predicting the path, lane lines and lead car data the model also
  # predicts the current velocity and rotation speed of the car. If the model is
  # very uncertain about the current velocity while the car is moving, this
  # usually means the model has trouble understanding the scene. This is used
  # as a heuristic to warn the driver.
  EventName.posenetInvalid: {
    ET.SOFT_DISABLE: soft_disable_alert("Posenet Speed Invalid"),
    ET.NO_ENTRY: posenet_invalid_alert,
  },

  # When the localizer detects an acceleration of more than 40 m/s^2 (~4G) we
  # alert the driver the device might have fallen from the windshield.
  EventName.deviceFalling: {
    ET.SOFT_DISABLE: soft_disable_alert("Device Fell Off Mount"),
    ET.NO_ENTRY: NoEntryAlert("Device Fell Off Mount"),
  },

  EventName.lowMemory: {
    ET.SOFT_DISABLE: soft_disable_alert("메모리 부족: 기기를 재부팅하세요"),
    ET.PERMANENT: low_memory_alert,
    ET.NO_ENTRY: NoEntryAlert("메모리 부족: 기기를 재부팅하세요"),
  },

  EventName.accFaulted: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("크루즈 오류: 차량을 재시작하세요"),
    ET.PERMANENT: NormalPermanentAlert("크루즈 오류: 차량을 재시작하세요"),
    ET.NO_ENTRY: NoEntryAlert("크루즈 오류: 차량을 재시작하세요"),
  },

  EventName.espActive: {
    ET.SOFT_DISABLE: soft_disable_alert("Electronic Stability Control Active"),
    ET.NO_ENTRY: NoEntryAlert("Electronic Stability Control Active"),
  },

  EventName.controlsMismatch: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("제어 명령과 차량 반응 불일치"),
    ET.NO_ENTRY: NoEntryAlert("제어 명령과 차량 반응 불일치"),
  },

  # Sometimes the USB stack on the device can get into a bad state
  # causing the connection to the panda to be lost
  EventName.usbError: {
    ET.SOFT_DISABLE: soft_disable_alert("USB Error: Reboot Your Device"),
    ET.PERMANENT: NormalPermanentAlert("USB Error: Reboot Your Device"),
    ET.NO_ENTRY: NoEntryAlert("USB Error: Reboot Your Device"),
  },

  # This alert can be thrown for the following reasons:
  # - No CAN data received at all
  # - CAN data is received, but some message are not received at the right frequency
  # If you're not writing a new car port, this is usually cause by faulty wiring
  EventName.canError: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("Unknown Vehicle Variant"),
    ET.PERMANENT: Alert(
      "Unknown Vehicle Variant",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, 1., creation_delay=1.),
    ET.NO_ENTRY: NoEntryAlert("Unknown Vehicle Variant"),
  },

  EventName.canBusMissing: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("CAN Bus 연결 해제됨"),
    ET.PERMANENT: Alert(
      "CAN Bus 연결 해제됨: 케이블을 확인해주세요",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, 1., creation_delay=1.),
    ET.NO_ENTRY: NoEntryAlert("CAN Bus 연결 해제됨: 케이블을 확인해주세요"),
  },

  EventName.steerUnavailable: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("LKAS 오류: 차량을 완전히 재시작하세요"),
    ET.PERMANENT: NormalPermanentAlert("LKAS 오류: 차량을 완전히 재시작하세요"),
    ET.NO_ENTRY: NoEntryAlert("LKAS 오류: 차량을 완전히 재시작하세요"),
  },

  EventName.reverseGear: {
    ET.PERMANENT: Alert(
      "기어 R",
      "",
      AlertStatus.normal, AlertSize.full,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2, creation_delay=0.5),
    ET.USER_DISABLE: ImmediateDisableAlert("기어 R"),
    ET.NO_ENTRY: NoEntryAlert("기어 R"),
  },

  # On cars that use stock ACC the car can decide to cancel ACC for various reasons.
  # When this happens we can no long control the car so the user needs to be warned immediately.
  EventName.cruiseDisabled: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("Cruise Is Off"),
  },

  # When the relay in the harness box opens the CAN bus between the LKAS camera
  # and the rest of the car is separated. When messages from the LKAS camera
  # are received on the car side this usually means the relay hasn't opened correctly
  # and this alert is thrown.
  EventName.relayMalfunction: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("Harness Relay Malfunction"),
    ET.PERMANENT: NormalPermanentAlert("Harness Relay Malfunction", "Check Hardware"),
    ET.NO_ENTRY: NoEntryAlert("Harness Relay Malfunction"),
  },

  EventName.speedTooLow: {
    ET.IMMEDIATE_DISABLE: Alert(
      "openpilot Canceled",
      "Speed too low",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGH, VisualAlert.none, AudibleAlert.disengage, 3.),
  },

  # When the car is driving faster than most cars in the training data, the model outputs can be unpredictable.
  EventName.speedTooHigh: {
    ET.WARNING: Alert(
      "속도가 너무 높습니다",
      "주행 모델이 불안정합니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.HIGH, VisualAlert.steerRequired, AudibleAlert.promptRepeat, 4.),
    ET.NO_ENTRY: NoEntryAlert("감속하여 활성화하세요"),
  },

  EventName.vehicleSensorsInvalid: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("Vehicle Sensors Invalid"),
    ET.PERMANENT: NormalPermanentAlert("Vehicle Sensors Calibrating", "Drive to Calibrate"),
    ET.NO_ENTRY: NoEntryAlert("Vehicle Sensors Calibrating"),
  },

  EventName.personalityChanged: {
    ET.WARNING: personality_changed_alert,
  },

  EventName.userBookmark: {
    ET.PERMANENT: NormalPermanentAlert("Bookmark Saved", duration=1.5),
  },

  EventName.audioFeedback: {
    ET.PERMANENT: audio_feedback_alert,
  },
}

# FrogPilot variables
FROGPILOT_EVENTS: dict[int, dict[str, Alert | AlertCallbackType]] = {
  FrogPilotEventName.blockUser: {
    ET.PERMANENT: Alert(
      "Don't use the 'Development' branch!",
      "Forcing you into 'Dashcam Mode' for your safety...",
      AlertStatus.critical, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.warningImmediate, 1.),
  },

  FrogPilotEventName.customStartupAlert: {
    ET.PERMANENT: custom_startup_alert,
  },

  FrogPilotEventName.forcingStop: {
    ET.WARNING: forcing_stop_alert,
  },

  FrogPilotEventName.goatSteerSaturated: {
    ET.WARNING: Alert(
      "JESUS TAKE THE WHEEL!!",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, FrogPilotAudibleAlert.goat, 2.),
  },

  FrogPilotEventName.greenLight: {
    ET.PERMANENT: Alert(
      "신호가 변경되었습니다",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.MID, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  FrogPilotEventName.holidayActive: {
    ET.PERMANENT: holiday_alert,
  },

  FrogPilotEventName.laneChangeBlockedLoud: {
    ET.WARNING: Alert(
      "차선 변경 대기중",
      "사각지대에 차량이 감지되었습니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.warningSoft, .1),
  },

  FrogPilotEventName.leadDeparting: {
    ET.PERMANENT: Alert(
      "",
      "선행 차량이 출발하였습니다",
      FrogPilotAlertStatus.normal, AlertSize.full,
      Priority.MID, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  FrogPilotEventName.nnffLoaded: {
    ET.PERMANENT: nnff_loaded_alert,
  },

  FrogPilotEventName.noLaneAvailable: {
    ET.WARNING: no_lane_available_alert,
  },

  FrogPilotEventName.openpilotCrashed: {
    ET.IMMEDIATE_DISABLE: Alert(
      "오픈파일럿 시스템 오류",
      "디스코드에 에러 로그를 제보해주세요",
      AlertStatus.critical, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.prompt, .1),

    ET.NO_ENTRY: Alert(
      "오픈파일럿 시스템 오류",
      "디스코드에 에러 로그를 제보해주세요",
      AlertStatus.critical, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.prompt, .1),
  },

  FrogPilotEventName.speedLimitChanged: {
    ET.PERMANENT: Alert(
      "Speed limit changed",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  FrogPilotEventName.trafficModeActive: {
    ET.WARNING: Alert(
      "Traffic Mode enabled",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  FrogPilotEventName.trafficModeInactive: {
    ET.WARNING: Alert(
      "Traffic Mode Disabled",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  FrogPilotEventName.turningLeft: {
    ET.WARNING: Alert(
      "좌회전 진행 중",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1),
  },

  FrogPilotEventName.turningRight: {
    ET.WARNING: Alert(
      "우회전 진행 중",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1),
  },

  # Random Events
  FrogPilotEventName.accel30: {
    ET.WARNING: Alert(
      "UwU u went a bit fast there!",
      "(⁄ ⁄•⁄ω⁄•⁄ ⁄)",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.uwu, 4.),
  },

  FrogPilotEventName.accel35: {
    ET.WARNING: Alert(
      "I ain't giving you no tree-fiddy",
      "You damn Loch Ness Monsta!",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.nessie, 4.),
  },

  FrogPilotEventName.accel40: {
    ET.WARNING: Alert(
      "Great Scott!",
      "🚗💨",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.doc, 4.),
  },

  FrogPilotEventName.dejaVuCurve: {
    ET.PERMANENT: Alert(
      "♬♪ Deja vu! ᕕ(⌐■_■)ᕗ ♪♬",
      "🏎️",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.dejaVu, 4.),
  },

  FrogPilotEventName.firefoxSteerSaturated: {
    ET.WARNING: Alert(
      "IE Has Stopped Responding...",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, FrogPilotAudibleAlert.firefox, 4.),
  },

  FrogPilotEventName.hal9000: {
    ET.WARNING: Alert(
      "I'm sorry Dave",
      "I'm afraid I can't do that...",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGH, VisualAlert.none, FrogPilotAudibleAlert.hal9000, 4.),
  },

  FrogPilotEventName.openpilotCrashedRandomEvent: {
    ET.IMMEDIATE_DISABLE: Alert(
      "오픈파일럿 시스템 오류",
      "디스코드에 에러 로그를 제보해주세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, FrogPilotAudibleAlert.fart, 10.),

    ET.NO_ENTRY: Alert(
      "오픈파일럿 시스템 오류",
      "디스코드에 에러 로그를 제보해주세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, FrogPilotAudibleAlert.fart, 10.),
  },

  FrogPilotEventName.thisIsFineSteerSaturated: {
    ET.WARNING: Alert(
      "This is fine ☕",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, FrogPilotAudibleAlert.thisIsFine, 2.),
  },

  FrogPilotEventName.toBeContinued: {
    ET.PERMANENT: Alert(
      "To be continued...",
      "⬅️",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.MID, VisualAlert.none, FrogPilotAudibleAlert.continued, 7.),
  },

  FrogPilotEventName.vCruise69: {
    ET.WARNING: Alert(
      "Lol 69",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.noice, 2.),
  },

  FrogPilotEventName.yourFrogTriedToKillMe: {
    ET.PERMANENT: Alert(
      "Your Frog tried to kill me...",
      "👺",
      FrogPilotAlertStatus.frogpilot, AlertSize.mid,
      Priority.MID, VisualAlert.none, FrogPilotAudibleAlert.angry, 5.),
  },

  FrogPilotEventName.youveGotMail: {
    ET.WARNING: Alert(
      "You've got mail! 📧",
      "",
      FrogPilotAlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, FrogPilotAudibleAlert.mail, 3.),
  },
}


if HARDWARE.get_device_type() == 'mici':
  EVENTS.update({
    EventName.preDriverDistracted: {
      ET.PERMANENT: Alert(
        "운전에 집중하세요",
        "운전자 부주의 감지됨",
        AlertStatus.normal, AlertSize.mid,
        Priority.LOW, VisualAlert.none, AudibleAlert.none, 2),
    },
    EventName.promptDriverDistracted: {
      ET.PERMANENT: Alert(
        "운전에 집중하세요",
        "미응답시 오픈파일럿이 비활성화됩니다",
        AlertStatus.userPrompt, AlertSize.mid,
        Priority.MID, VisualAlert.steerRequired, AudibleAlert.promptDistracted, 1),
    },
    EventName.resumeRequired: {
      ET.WARNING: Alert(
        "오토 홀드",
        "해제하려면 악셀을 밟거나 RES버튼을 누르세요",
        AlertStatus.normal, AlertSize.mid,
        Priority.LOW, VisualAlert.none, AudibleAlert.none, .2),
    },
    EventName.preLaneChangeLeft: {
      ET.WARNING: Alert(
        "좌측 차선 변경 승인 대기중",
        "핸들을 돌려 차선 변경을 승인해주세요",
        AlertStatus.normal, AlertSize.mid,
        Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
    },
    EventName.preLaneChangeRight: {
      ET.WARNING: Alert(
        "우측 차선 변경 승인 대기중",
        "핸들을 돌려 차선 변경을 승인해주세요",
        AlertStatus.normal, AlertSize.mid,
        Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
    },
    EventName.laneChangeBlocked: {
      ET.WARNING: Alert(
        "차선 변경 대기중",
        "사각지대에 차량이 감지되었습니다",
        AlertStatus.userPrompt, AlertSize.mid,
        Priority.LOW, VisualAlert.none, AudibleAlert.prompt, .1),
    },
    EventName.steerSaturated: {
      ET.WARNING: Alert(
        "핸들을 조작해주세요",
        "조향각 한계에 도달했습니다",
        AlertStatus.userPrompt, AlertSize.mid,
        Priority.LOW, VisualAlert.steerRequired, AudibleAlert.warningSoft, 2.),
    },
    EventName.calibrationIncomplete: {
      ET.PERMANENT: calibration_incomplete_alert,
      ET.SOFT_DISABLE: soft_disable_alert("캘리브레이션 미완료"),
      ET.NO_ENTRY: NoEntryAlert("캘리브레이션이 진행중입니다"),
    },
    EventName.reverseGear: {
      ET.PERMANENT: Alert(
        "기어 R",
        "",
        AlertStatus.normal, AlertSize.full,
        Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2, creation_delay=0.5),
      ET.USER_DISABLE: ImmediateDisableAlert("기어 R"),
      ET.NO_ENTRY: NoEntryAlert("기어 R"),
    },
  })


if __name__ == '__main__':
  # print all alerts by type and priority
  from cereal.services import SERVICE_LIST
  from collections import defaultdict

  event_names = {v: k for k, v in EventName.schema.enumerants.items()}
  alerts_by_type: dict[str, dict[Priority, list[str]]] = defaultdict(lambda: defaultdict(list))

  CP = car.CarParams.new_message()
  CS = car.CarState.new_message()
  sm = messaging.SubMaster(list(SERVICE_LIST.keys()))

  for i, alerts in EVENTS.items():
    for et, alert in alerts.items():
      if callable(alert):
        alert = alert(CP, CS, sm, False, 1, log.LongitudinalPersonality.standard)
      alerts_by_type[et][alert.priority].append(event_names[i])

  all_alerts: dict[str, list[tuple[Priority, list[str]]]] = {}
  for et, priority_alerts in alerts_by_type.items():
    all_alerts[et] = sorted(priority_alerts.items(), key=lambda x: x[0], reverse=True)

  for status, evs in sorted(all_alerts.items(), key=lambda x: x[0]):
    print(f"**** {status} ****")
    for p, alert_list in evs:
      print(f"  {repr(p)}:")
      print("   ", ', '.join(alert_list), "\n")
