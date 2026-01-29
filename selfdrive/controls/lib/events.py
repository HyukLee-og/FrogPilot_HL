#!/usr/bin/env python3
import bisect
import math
import os
from enum import IntEnum
from collections.abc import Callable
from types import SimpleNamespace

from cereal import log, car
import cereal.messaging as messaging
from openpilot.common.conversions import Conversions as CV
from openpilot.common.git import get_short_branch
from openpilot.common.realtime import DT_CTRL
from openpilot.selfdrive.locationd.calibrationd import MIN_SPEED_FILTER

from openpilot.selfdrive.frogpilot.frogpilot_variables import params

AlertSize = log.ControlsState.AlertSize
AlertStatus = log.ControlsState.AlertStatus
VisualAlert = car.CarControl.HUDControl.VisualAlert
AudibleAlert = car.CarControl.HUDControl.AudibleAlert
EventName = car.CarEvent.EventName


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


class Events:
  def __init__(self):
    self.events: list[int] = []
    self.static_events: list[int] = []
    self.event_counters = dict.fromkeys(EVENTS.keys(), 0)

  @property
  def names(self) -> list[int]:
    return self.events

  def __len__(self) -> int:
    return len(self.events)

  def add(self, event_name: int, static: bool=False) -> None:
    if static:
      bisect.insort(self.static_events, event_name)
    bisect.insort(self.events, event_name)

  def clear(self) -> None:
    self.event_counters = {k: (v + 1 if k in self.events else 0) for k, v in self.event_counters.items()}
    self.events = self.static_events.copy()

  def contains(self, event_type: str) -> bool:
    return any(event_type in EVENTS.get(e, {}) for e in self.events)

  def create_alerts(self, event_types: list[str], callback_args=None):
    if callback_args is None:
      callback_args = []

    ret = []
    for e in self.events:
      types = EVENTS[e].keys()
      for et in event_types:
        if et in types:
          alert = EVENTS[e][et]
          if not isinstance(alert, Alert):
            alert = alert(*callback_args)

          if DT_CTRL * (self.event_counters[e] + 1) >= alert.creation_delay:
            alert.alert_type = f"{EVENT_NAME[e]}/{et}"
            alert.event_type = et
            ret.append(alert)
    return ret

  def add_from_msg(self, events):
    for e in events:
      bisect.insort(self.events, e.name.raw)

  def to_msg(self):
    ret = []
    for event_name in self.events:
      event = car.CarEvent.new_message()
      event.name = event_name
      for event_type in EVENTS.get(event_name, {}):
        setattr(event, event_type, True)
      ret.append(event)
    return ret


class Alert:
  def __init__(self,
               alert_text_1: str,
               alert_text_2: str,
               alert_status: log.ControlsState.AlertStatus,
               alert_size: log.ControlsState.AlertSize,
               priority: Priority,
               visual_alert: car.CarControl.HUDControl.VisualAlert,
               audible_alert: car.CarControl.HUDControl.AudibleAlert,
               duration: float,
               alert_rate: float = 0.,
               creation_delay: float = 0.):

    self.alert_text_1 = alert_text_1
    self.alert_text_2 = alert_text_2
    self.alert_status = alert_status
    self.alert_size = alert_size
    self.priority = priority
    self.visual_alert = visual_alert
    self.audible_alert = audible_alert

    self.duration = int(duration / DT_CTRL)

    self.alert_rate = alert_rate
    self.creation_delay = creation_delay

    self.alert_type = ""
    self.event_type: str | None = None

  def __str__(self) -> str:
    return f"{self.alert_text_1}/{self.alert_text_2} {self.priority} {self.visual_alert} {self.audible_alert}"

  def __gt__(self, alert2) -> bool:
    if not isinstance(alert2, Alert):
      return False
    return self.priority > alert2.priority


class NoEntryAlert(Alert):
  def __init__(self, alert_text_2: str,
               alert_text_1: str = "오픈파일럿 사용 불가",
               visual_alert: car.CarControl.HUDControl.VisualAlert=VisualAlert.none):
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
    super().__init__(alert_text_1, alert_text_2,
                     alert_status, AlertSize.mid,
                     Priority.LOWER, VisualAlert.none, AudibleAlert.none, 5.),


# ********** helper functions **********
def get_display_speed(speed_ms: float, metric: bool) -> str:
  speed = int(round(speed_ms * (CV.MS_TO_KPH if metric else CV.MS_TO_MPH)))
  unit = 'km/h' if metric else 'mph'
  return f"{speed} {unit}"


# ********** alert callback functions **********

AlertCallbackType = Callable[[car.CarParams, car.CarState, messaging.SubMaster, bool, int], Alert]


def soft_disable_alert(alert_text_2: str) -> AlertCallbackType:
  def func(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
    if soft_disable_time < int(0.5 / DT_CTRL):
      return ImmediateDisableAlert(alert_text_2)
    return SoftDisableAlert(alert_text_2)
  return func

def user_soft_disable_alert(alert_text_2: str) -> AlertCallbackType:
  def func(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
    if soft_disable_time < int(0.5 / DT_CTRL):
      return ImmediateDisableAlert(alert_text_2)
    return UserSoftDisableAlert(alert_text_2)
  return func

def startup_master_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  branch = get_short_branch()  # Ensure get_short_branch is cached to avoid lags on startup
  if "REPLAY" in os.environ:
    branch = "replay"

  return StartupAlert("WARNING: This branch is not tested", branch, alert_status=AlertStatus.userPrompt)

def below_engage_speed_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NoEntryAlert(f"Drive above {get_display_speed(CP.minEnableSpeed, metric)} to engage")


def below_steer_speed_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  return Alert(
    f"조향 비활성화({get_display_speed(CP.minSteerSpeed, metric)}이하)",
    "",
    AlertStatus.userPrompt, AlertSize.small,
    Priority.LOW, VisualAlert.steerRequired, AudibleAlert.prompt, 0.4)


def calibration_incomplete_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  first_word = 'Recalibration' if sm['liveCalibration'].calStatus == log.LiveCalibrationData.Status.recalibrating else 'Calibration'
  return Alert(
    f"캘리브레이션이 진행중입니다:{sm['liveCalibration'].calPerc:.0f}%",
    f"{get_display_speed(MIN_SPEED_FILTER, metric)}이상으로 주행하세요",
    AlertStatus.normal, AlertSize.mid,
    Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2)


# *** debug alerts ***

def out_of_space_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  full_perc = round(100. - sm['deviceState'].freeSpacePercent)
  return NormalPermanentAlert("용량 부족", f"{full_perc}%")


def posenet_invalid_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  mdl = sm['modelV2'].velocity.x[0] if len(sm['modelV2'].velocity.x) else math.nan
  err = CS.vEgo - mdl
  msg = f"Speed Error: {err:.1f} m/s"
  return NoEntryAlert(msg, alert_text_1="Posenet Speed Invalid")


def process_not_running_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  not_running = [p.name for p in sm['managerState'].processes if not p.running and p.shouldBeRunning]
  msg = ', '.join(not_running)
  return NoEntryAlert(msg, alert_text_1="외부 프로그램 비정상")


def comm_issue_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  bs = [s for s in sm.data.keys() if not sm.all_checks([s, ])]
  msg = ', '.join(bs[:4])  # can't fit too many on one line
  return NoEntryAlert(msg, alert_text_1="차량과의 통신 오류")


def camera_malfunction_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  all_cams = ('roadCameraState', 'driverCameraState', 'wideRoadCameraState')
  bad_cams = [s.replace('State', '') for s in all_cams if s in sm.data.keys() and not sm.all_checks([s, ])]
  return NormalPermanentAlert("카메라 이상", ', '.join(bad_cams))


def calibration_invalid_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  rpy = sm['liveCalibration'].rpyCalib
  yaw = math.degrees(rpy[2] if len(rpy) == 3 else math.nan)
  pitch = math.degrees(rpy[1] if len(rpy) == 3 else math.nan)
  angles = f"기기의 각도를 조정해주세요 (Pitch: {pitch:.1f}°, Yaw: {yaw:.1f}°)"
  return NormalPermanentAlert("캘리브레이션이 필요합니다", angles)


def overheat_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  cpu = max(sm['deviceState'].cpuTempC, default=0.)
  gpu = max(sm['deviceState'].gpuTempC, default=0.)
  temp = max((cpu, gpu, sm['deviceState'].memoryTempC))
  return NormalPermanentAlert("기기의 온도가 높습니다", f"{temp:.0f} °C")


def low_memory_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NormalPermanentAlert("메모리 부족", f"{sm['deviceState'].memoryUsagePercent}% 사용됨")


def high_cpu_usage_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  x = max(sm['deviceState'].cpuUsagePercent, default=0.)
  return NormalPermanentAlert("CPU 사용량이 높습니다", f"{x}% used")


def modeld_lagging_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  return NormalPermanentAlert("오픈파일럿이 불안정합니다", f"{sm['modelV2'].frameDropPerc:.1f}% 프레임 하락")


def wrong_car_mode_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  text = "Enable Adaptive Cruise to Engage"
  if CP.carName == "honda":
    text = "Enable Main Switch to Engage"
  return NoEntryAlert(text)


def joystick_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  axes = sm['testJoystick'].axes
  gb, steer = list(axes)[:2] if len(axes) else (0., 0.)
  vals = f"Gas: {round(gb * 100.)}%, Steer: {round(steer * 100.)}%"
  return NormalPermanentAlert("Joystick Mode", vals)


# FrogPilot Alerts
def custom_startup_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  return StartupAlert(frogpilot_toggles.startup_alert_top, frogpilot_toggles.startup_alert_bottom, alert_status=AlertStatus.frogpilot)


def forcing_stop_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  model_length = sm['frogpilotPlan'].forcingStopLength
  model_length_msg = f"{model_length:.1f} meters" if metric else f"{model_length * CV.METER_TO_FOOT:.1f} feet"

  return Alert(
    f"Forcing the car to stop in {model_length_msg}",
    "Press the gas pedal or 'Resume' button to override",
    AlertStatus.frogpilot, AlertSize.mid,
    Priority.MID, VisualAlert.none, AudibleAlert.prompt, 1.)


def holiday_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
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
    holiday_messages.get(frogpilot_toggles.current_holiday_theme),
    "",
    AlertStatus.normal, AlertSize.small,
    Priority.LOWEST, VisualAlert.none, AudibleAlert.startup, 5.)


def no_lane_available_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  lane_width = sm['frogpilotPlan'].laneWidthLeft if CS.leftBlinker else sm['frogpilotPlan'].laneWidthRight
  lane_width_msg = f"{lane_width:.1f} 미터" if metric else f"{lane_width * CV.METER_TO_FOOT:.1f} 피트"

  return Alert(
    "차선 변경 불가",
    f"변경할 차선의 공간이 부족합니다 - {lane_width_msg}",
    AlertStatus.normal, AlertSize.mid,
    Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .2)


def torque_nn_load_alert(CP: car.CarParams, CS: car.CarState, sm: messaging.SubMaster, metric: bool, soft_disable_time: int, frogpilot_toggles: SimpleNamespace) -> Alert:
  model_name = params.get("NNFFModelName", encoding='utf-8')
  if model_name is None:
    return Alert(
      "NNFF Torque Controller not available",
      "Donate logs to Twilsonco to get your car supported!",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 10.0)
  else:
    return Alert(
      "NNFF 토크 컨트롤러 로드됨",
      "인공 신경망 기반 모델이 차량을 제어합니다",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.engage, 5.0)


EVENTS: dict[int, dict[str, Alert | AlertCallbackType]] = {
  # ********** events with no alerts **********

  EventName.stockFcw: {},
  EventName.actuatorsApiUnavailable: {},

  # ********** events only containing alerts displayed in all states **********

  EventName.joystickDebug: {
    ET.WARNING: joystick_alert,
    ET.PERMANENT: NormalPermanentAlert("Joystick Mode"),
  },

  EventName.controlsInitializing: {
    ET.NO_ENTRY: NoEntryAlert("시스템 시작중"),
  },

  EventName.startup: {
    ET.PERMANENT: StartupAlert("항시 전방을 주시하고 교통 상황에 유의하세요")
  },

  EventName.startupMaster: {
    ET.PERMANENT: startup_master_alert,
  },

  # Car is recognized, but marked as dashcam only
  EventName.startupNoControl: {
    ET.PERMANENT: StartupAlert("Dashcam mode"),
    ET.NO_ENTRY: NoEntryAlert("Dashcam mode"),
  },

  # Car is not recognized
  EventName.startupNoCar: {
    ET.PERMANENT: StartupAlert("Dashcam mode for unsupported car"),
  },

  EventName.startupNoFw: {
    ET.PERMANENT: StartupAlert("Car Unrecognized",
                               "Check comma power connections",
                               alert_status=AlertStatus.userPrompt),
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
    ET.PERMANENT: NormalPermanentAlert("Stock LKAS is on",
                                       "Turn off stock LKAS to engage"),
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
      "!BRAKE!",
      "충돌 위험",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGHEST, VisualAlert.fcw, AudibleAlert.warningSoft, 2.),
  },

  EventName.ldw: {
    ET.PERMANENT: Alert(
      "차선 이탈 감지됨",
      "",
      AlertStatus.userPrompt, AlertSize.small,
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
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.none, .1, alert_rate=0.75),
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
    ET.PERMANENT: Alert(
      "오토 홀드",
      "해제하려면 악셀을 밟거나 RES버튼을 누르세요",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.MID, VisualAlert.none, AudibleAlert.none, .2),
  },

  EventName.belowSteerSpeed: {
    ET.WARNING: below_steer_speed_alert,
  },

  EventName.preLaneChangeLeft: {
    ET.WARNING: Alert(
      "좌측 차선 변경 승인 대기중 (핸들 토크 요구됨)",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1, alert_rate=0.75),
  },

  EventName.preLaneChangeRight: {
    ET.WARNING: Alert(
      "우측 차선 변경 승인 대기중 (핸들 토크 요구됨)",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1, alert_rate=0.75),
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
      "차션 변경 중",
      "전후방 및 측면에 유의하세요",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, .1),
  },

  EventName.steerSaturated: {
    ET.WARNING: Alert(
      "핸들을 조작해주세요",
      "조향각 한계에 도달했습니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.promptRepeat, 2.),
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
    ET.NO_ENTRY: NoEntryAlert("paramsd Temporary Error"),
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
    ET.USER_DISABLE: EngagementAlert(AudibleAlert.disengage),
    ET.NO_ENTRY: NoEntryAlert("Brake Hold Active"),
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

  EventName.preEnableStandstill: {
    ET.PRE_ENABLE: Alert(
      "오토홀드 대기중",
      "브레이크에서 발을 떼면 활성화됩니다",
      AlertStatus.normal, AlertSize.mid,
      Priority.MID, VisualAlert.none, AudibleAlert.none, .1, creation_delay=1.),
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
    ET.SOFT_DISABLE: soft_disable_alert("Steering Temporarily Unavailable"),
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
    ET.PERMANENT: Alert(
      "GPS 수신 감도 나쁨",
      "야외에서도 해당 메세지가 계속될 경우 하드웨어 결함일 수 있습니다",
      AlertStatus.normal, AlertSize.mid,
      Priority.LOWER, VisualAlert.none, AudibleAlert.none, .2, creation_delay=600.)
  },

  EventName.soundsUnavailable: {
    ET.PERMANENT: NormalPermanentAlert("Speaker not found", "Reboot your Device"),
    ET.NO_ENTRY: NoEntryAlert("Speaker not found"),
  },

  EventName.tooDistracted: {
    ET.NO_ENTRY: NoEntryAlert("운전자 부주의 레벨이 너무 높습니다"),
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

  EventName.controlsdLagging: {
    ET.SOFT_DISABLE: soft_disable_alert("차량 통신 이상"),
    ET.NO_ENTRY: NoEntryAlert("차량과의 통신이 불안정합니다, 재부팅하세요"),
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

  EventName.highCpuUsage: {
    #ET.SOFT_DISABLE: soft_disable_alert("System Malfunction: Reboot Your Device"),
    #ET.PERMANENT: NormalPermanentAlert("System Malfunction", "Reboot your Device"),
    ET.NO_ENTRY: high_cpu_usage_alert,
  },

  EventName.accFaulted: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("크루즈 오류: 차량을 재시작하세요"),
    ET.PERMANENT: NormalPermanentAlert("크루즈 오류: 차량을 재시작하세요"),
    ET.NO_ENTRY: NoEntryAlert("크루즈 오류: 차량을 재시작하세요"),
  },

  EventName.controlsMismatch: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("제어 명령과 차량 반응 불일치"),
    ET.NO_ENTRY: NoEntryAlert("제어 명령과 차량 반응 불일치"),
  },

  EventName.roadCameraError: {
    ET.PERMANENT: NormalPermanentAlert("Camera CRC Error - Road",
                                       duration=1.,
                                       creation_delay=30.),
  },

  EventName.wideRoadCameraError: {
    ET.PERMANENT: NormalPermanentAlert("Camera CRC Error - Road Fisheye",
                                       duration=1.,
                                       creation_delay=30.),
  },

  EventName.driverCameraError: {
    ET.PERMANENT: NormalPermanentAlert("Camera CRC Error - Driver",
                                       duration=1.,
                                       creation_delay=30.),
  },

  # Sometimes the USB stack on the device can get into a bad state
  # causing the connection to the panda to be lost
  EventName.usbError: {
    ET.SOFT_DISABLE: soft_disable_alert("USB Error: Reboot Your Device"),
    ET.PERMANENT: NormalPermanentAlert("USB Error: Reboot Your Device", ""),
    ET.NO_ENTRY: NoEntryAlert("USB Error: Reboot Your Device"),
  },

  # This alert can be thrown for the following reasons:
  # - No CAN data received at all
  # - CAN data is received, but some message are not received at the right frequency
  # If you're not writing a new car port, this is usually cause by faulty wiring
  EventName.canError: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("CAN 에러"),
    ET.PERMANENT: Alert(
      "CAN 에러: 배선 연결 상태를 확인하세요",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.none, 1., creation_delay=1.),
    ET.NO_ENTRY: NoEntryAlert("CAN 에러: 배선 연결 상태를 확인하세요"),
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

  EventName.lowSpeedLockout: {
    ET.PERMANENT: NormalPermanentAlert("Cruise Fault: Restart the car to engage"),
    ET.NO_ENTRY: NoEntryAlert("Cruise Fault: Restart the Car"),
  },

  EventName.lkasDisabled: {
    ET.PERMANENT: NormalPermanentAlert("LKAS Disabled: Enable LKAS to engage"),
    ET.NO_ENTRY: NoEntryAlert("LKAS Disabled"),
  },

  EventName.vehicleSensorsInvalid: {
    ET.IMMEDIATE_DISABLE: ImmediateDisableAlert("Vehicle Sensors Invalid"),
    ET.PERMANENT: NormalPermanentAlert("Vehicle Sensors Calibrating", "Drive to Calibrate"),
    ET.NO_ENTRY: NoEntryAlert("Vehicle Sensors Calibrating"),
  },

  # FrogPilot Events
  EventName.blockUser: {
    ET.PERMANENT: Alert(
      "Don't use the 'Development' branch!",
      "Forcing you into 'Dashcam Mode' for your safety",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.none, 1.),
  },

  EventName.customStartupAlert: {
    ET.PERMANENT: custom_startup_alert,
  },

  EventName.forcingStop: {
    ET.WARNING: forcing_stop_alert,
  },

  EventName.goatSteerSaturated: {
    ET.WARNING: Alert(
      "JESUS TAKE THE WHEEL!!",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.goat, 2.),
  },

  EventName.greenLight: {
    ET.PERMANENT: Alert(
      "신호가 변경되었습니다",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.MID, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  EventName.holidayActive: {
    ET.PERMANENT: holiday_alert,
  },

  EventName.laneChangeBlockedLoud: {
    ET.WARNING: Alert(
      "차선 변경 대기중",
      "사각지대에 차량이 감지되었습니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.warningSoft, .1),
  },

  EventName.leadDeparting: {
    ET.PERMANENT: Alert(
      "선행 차량이 출발하였습니다",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.MID, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  EventName.noLaneAvailable: {
    ET.PERMANENT: no_lane_available_alert,
  },

  EventName.openpilotCrashed: {
    ET.IMMEDIATE_DISABLE: Alert(
      "openpilot crashed",
      "Please post the 'Error Log' in the FrogPilot Discord!",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.prompt, .1),

    ET.NO_ENTRY: Alert(
      "openpilot crashed",
      "Please post the 'Error Log' in the FrogPilot Discord!",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.prompt, .1),
  },

  EventName.pedalInterceptorNoBrake: {
    ET.WARNING: Alert(
      "Braking Unavailable",
      "Shift to L",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.HIGH, VisualAlert.wrongGear, AudibleAlert.promptRepeat, 4.),
  },

  EventName.speedLimitChanged: {
    ET.PERMANENT: Alert(
      "Speed limit changed",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  EventName.thisIsFineSteerSaturated: {
    ET.WARNING: Alert(
      "This is fine ☕",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.thisIsFine, 2.),
  },

  EventName.torqueNNLoad: {
    ET.PERMANENT: torque_nn_load_alert,
  },

  EventName.trafficModeActive: {
    ET.PERMANENT: Alert(
      "Traffic Mode enabled",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  EventName.trafficModeInactive: {
    ET.PERMANENT: Alert(
      "Traffic Mode Disabled",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.prompt, 3.),
  },

  EventName.turningLeft: {
    ET.WARNING: Alert(
      "좌회전 진행 중",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1, alert_rate=0.75),
  },

  EventName.turningRight: {
    ET.WARNING: Alert(
      "우회전 진행 중",
      "",
      AlertStatus.normal, AlertSize.small,
      Priority.LOWEST, VisualAlert.none, AudibleAlert.none, .1, alert_rate=0.75),
  },

  # Random Events
  EventName.accel30: {
    ET.PERMANENT: Alert(
      "전방 추돌 주의",
      "전방 차량과 추돌 위험이 있습니다",
      AlertStatus.critical, AlertSize.full,
      Priority.HIGHEST, VisualAlert.fcw, AudibleAlert.warningImmediate, 3.),
  },

  EventName.accel35: {
    ET.PERMANENT: Alert(
      "수동 운전으로 전환되었습니다",
      "핸들을 잡아주세요",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGH, VisualAlert.none, AudibleAlert.none, 5.),
  },

  EventName.accel40: {
    ET.WARNING: Alert(
      "Great Scott!",
      "🚗💨",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.doc, 4.),
  },

  EventName.dejaVuCurve: {
    ET.WARNING: Alert(
      "♬♪ Deja vu! ᕕ(⌐■_■)ᕗ ♪♬",
      "🏎️",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.LOW, VisualAlert.none, AudibleAlert.dejaVu, 4.),
  },

  EventName.firefoxSteerSaturated: {
    ET.WARNING: Alert(
      "IE Has Stopped Responding...",
      "Turn Exceeds Steering Limit",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.LOW, VisualAlert.steerRequired, AudibleAlert.firefox, 4.),
  },

  EventName.hal9000: {
    ET.PERMANENT: Alert(
      "2시간 이상의 장시간 운전 중입니다",
      "졸음 쉼터에서 휴식을 권장드립니다",
      AlertStatus.userPrompt, AlertSize.mid,
      Priority.MID, VisualAlert.none, AudibleAlert.prompt, 10.),
  },

  EventName.openpilotCrashedRandomEvent: {
    ET.IMMEDIATE_DISABLE: Alert(
      "openpilot crashed 💩",
      "Please post the 'Error Log' in the FrogPilot Discord!",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.fart, 10.),

    ET.NO_ENTRY: Alert(
      "openpilot crashed 💩",
      "Please post the 'Error Log' in the FrogPilot Discord!",
      AlertStatus.normal, AlertSize.mid,
      Priority.HIGHEST, VisualAlert.none, AudibleAlert.fart, 10.),
  },

  EventName.toBeContinued: {
    ET.PERMANENT: Alert(
      "To be continued...",
      "⬅️",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.MID, VisualAlert.none, AudibleAlert.continued, 7.),
  },

  EventName.vCruise69: {
    ET.PERMANENT: Alert(
      "Lol 69",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.noice, 2.),
  },

  EventName.yourFrogTriedToKillMe: {
    ET.PERMANENT: Alert(
      "Your Frog tried to kill me...",
      "👺",
      AlertStatus.frogpilot, AlertSize.mid,
      Priority.MID, VisualAlert.none, AudibleAlert.angry, 5.),
  },

  EventName.youveGotMail: {
    ET.PERMANENT: Alert(
      "You've got mail! 📧",
      "",
      AlertStatus.frogpilot, AlertSize.small,
      Priority.LOW, VisualAlert.none, AudibleAlert.mail, 3.),
  },
}


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
        alert = alert(CP, CS, sm, False, 1)
      alerts_by_type[et][alert.priority].append(event_names[i])

  all_alerts: dict[str, list[tuple[Priority, list[str]]]] = {}
  for et, priority_alerts in alerts_by_type.items():
    all_alerts[et] = sorted(priority_alerts.items(), key=lambda x: x[0], reverse=True)

  for status, evs in sorted(all_alerts.items(), key=lambda x: x[0]):
    print(f"**** {status} ****")
    for p, alert_list in evs:
      print(f"  {repr(p)}:")
      print("   ", ', '.join(alert_list), "\n")
