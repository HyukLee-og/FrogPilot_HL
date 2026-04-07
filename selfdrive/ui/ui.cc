#include "selfdrive/ui/ui.h"

#include <algorithm>
#include <cmath>

#include <QtConcurrent>

#include "common/transformations/orientation.hpp"
#include "common/swaglog.h"
#include "common/timing.h"
#include "common/util.h"
#include "common/watchdog.h"
#include "system/hardware/hw.h"

#define BACKLIGHT_DT 0.05
#define BACKLIGHT_TS 10.00

constexpr float AUTO_BRIGHTNESS_MIN = 1.0f;
constexpr float AUTO_BRIGHTNESS_MAX = 100.0f;
constexpr float AUTO_BRIGHTNESS_EXPOSURE_MAX = 100.0f;
constexpr float AUTO_BRIGHTNESS_EXPOSURE_GAMMA = 0.8f;
constexpr float AUTO_BRIGHTNESS_DIM_FLOOR = 10.0f;
constexpr float AUTO_BRIGHTNESS_DARK_THRESHOLD = 2.0f;
constexpr double STARTED_FALL_DEBOUNCE_S = 5.0;

static void update_sockets(UIState *s) {
  s->sm->update(0);
}

static void update_state(UIState *s, FrogPilotUIState *fs) {
  SubMaster &sm = *(s->sm);
  UIScene &scene = s->scene;

  if (sm.updated("liveCalibration")) {
    auto list2rot = [](const capnp::List<float>::Reader &rpy_list) ->Eigen::Matrix3f {
      return euler2rot({rpy_list[0], rpy_list[1], rpy_list[2]}).cast<float>();
    };

    auto live_calib = sm["liveCalibration"].getLiveCalibration();
    if (live_calib.getCalStatus() == cereal::LiveCalibrationData::Status::CALIBRATED) {
      auto device_from_calib = list2rot(live_calib.getRpyCalib());
      auto wide_from_device = list2rot(live_calib.getWideFromDeviceEuler());
      s->scene.view_from_calib = VIEW_FROM_DEVICE * device_from_calib;
      s->scene.view_from_wide_calib = VIEW_FROM_DEVICE * wide_from_device * device_from_calib;
    } else {
      s->scene.view_from_calib = s->scene.view_from_wide_calib = VIEW_FROM_DEVICE;
    }
  }
  if (sm.updated("pandaStates")) {
    auto pandaStates = sm["pandaStates"].getPandaStates();
    if (pandaStates.size() > 0) {
      scene.pandaType = pandaStates[0].getPandaType();

      if (scene.pandaType != cereal::PandaState::PandaType::UNKNOWN) {
        scene.ignition = false;
        for (const auto& pandaState : pandaStates) {
          scene.ignition |= pandaState.getIgnitionLine() || pandaState.getIgnitionCan();
        }
      }
    }
  } else if ((s->sm->frame - s->sm->rcv_frame("pandaStates")) > 5*UI_FREQ) {
    scene.pandaType = cereal::PandaState::PandaType::UNKNOWN;
  }
  if (sm.updated("wideRoadCameraState")) {
    auto cam_state = sm["wideRoadCameraState"].getWideRoadCameraState();
    float exposure = std::clamp(cam_state.getExposureValPercent(), 0.0f, AUTO_BRIGHTNESS_EXPOSURE_MAX);
    float normalized_exposure = exposure / AUTO_BRIGHTNESS_EXPOSURE_MAX;
    float normalized_light = std::pow(1.0f - normalized_exposure, AUTO_BRIGHTNESS_EXPOSURE_GAMMA);
    scene.light_sensor = AUTO_BRIGHTNESS_MAX * normalized_light;
  } else if (!sm.allAliveAndValid({"wideRoadCameraState"})) {
    scene.light_sensor = -1;
  }
  bool raw_started = sm["deviceState"].getDeviceState().getStarted() && scene.ignition;
  if (raw_started) {
    scene.started = true;
    s->started_false_since = -1.0;
  } else if (scene.started) {
    double now = seconds_since_boot();
    if (s->started_false_since < 0.0) {
      s->started_false_since = now;
    }
    scene.started = (now - s->started_false_since) < STARTED_FALL_DEBOUNCE_S;
  } else {
    s->started_false_since = -1.0;
    scene.started = false;
  }

  auto params = Params();
  const bool force_onroad_param = params.getBool("ForceOnroad");
  const bool force_offroad_param = params.getBool("ForceOffroad");

  // FrogPilot variables
  FrogPilotUIScene &frogpilot_scene = fs->frogpilot_scene;

  if (sm.updated("carState")) {
    const cereal::CarState::Reader &carState = sm["carState"].getCarState();
    frogpilot_scene.parked = carState.getGearShifter() == cereal::CarState::GearShifter::PARK;
    frogpilot_scene.reverse = carState.getGearShifter() == cereal::CarState::GearShifter::REVERSE;
    frogpilot_scene.standstill = carState.getStandstill() && !frogpilot_scene.reverse;
  }

  if (scene.started) {
    frogpilot_scene.started_timer += 1;
  }
  scene.started |= force_onroad_param || frogpilot_scene.frogpilot_toggles.value("force_onroad").toBool();
  scene.started &= !(force_offroad_param || frogpilot_scene.frogpilot_toggles.value("force_offroad").toBool());
  scene.recording_audio = params.getBool("RecordAudio") && scene.started;
}

void ui_update_params(UIState *s) {
  auto params = Params();
  s->scene.is_metric = params.getBool("IsMetric");
}

void UIState::updateStatus(FrogPilotUIState *fs) {
  // FrogPilot variables
  FrogPilotUIScene &frogpilot_scene = fs->frogpilot_scene;
  QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  if (scene.started && sm->updated("selfdriveState")) {
    auto ss = (*sm)["selfdriveState"].getSelfdriveState();
    auto state = ss.getState();

    // FrogPilot variables
    const UIStatus previous_status = status;

    if (state == cereal::SelfdriveState::OpenpilotState::PRE_ENABLED || state == cereal::SelfdriveState::OpenpilotState::OVERRIDING) {
      status = STATUS_OVERRIDE;
    } else if (frogpilot_scene.always_on_lateral_active) {
      status = STATUS_ALWAYS_ON_LATERAL_ACTIVE;
    } else if (frogpilot_scene.traffic_mode_enabled && ss.getEnabled()) {
      status = STATUS_TRAFFIC_MODE_ENABLED;
    } else {
      status = ss.getEnabled() ? STATUS_ENGAGED : STATUS_DISENGAGED;
    }

    // FrogPilot variables
    frogpilot_scene.wake_up_screen = ss.getAlertStatus() != cereal::SelfdriveState::AlertStatus::NORMAL || (status != previous_status && status != STATUS_OVERRIDE);
  }

  if (engaged() != engaged_prev) {
    engaged_prev = engaged();
    emit engagedChanged(engaged());
  }

  // Handle onroad/offroad transition
  if (scene.started != started_prev || sm->frame == 1) {
    if (scene.started) {
      status = STATUS_DISENGAGED;
      scene.started_frame = sm->frame;
    }
    started_prev = scene.started;
    emit offroadTransition(!scene.started);

    // FrogPilot variables
    if (frogpilot_toggles.value("tethering_config").toInt() == 2) {
      fs->wifi->setTetheringEnabled(scene.started);
    }
  }
}

UIState::UIState(QObject *parent) : QObject(parent) {
  sm = std::make_unique<SubMaster>(std::vector<const char*>{
    "modelV2", "controlsState", "liveCalibration", "radarState", "deviceState",
    "pandaStates", "carParams", "driverMonitoringState", "carState", "driverStateV2",
    "carOutput",
    "wideRoadCameraState", "managerState", "selfdriveState", "longitudinalPlan",
  });
  prime_state = new PrimeState(this);
  language = QString::fromStdString(Params().get("LanguageSetting"));

  // update timer
  timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, &UIState::update);
  timer->start(1000 / UI_FREQ);
}

void UIState::update() {
  update_sockets(this);
  update_state(this, frogpilotUIState());
  updateStatus(frogpilotUIState());

  if (sm->frame % UI_FREQ == 0) {
    watchdog_kick(nanos_since_boot());
  }
  emit uiUpdate(*this, *frogpilotUIState());

  // FrogPilot variables
  FrogPilotUIState *fs = frogpilotUIState();
  FrogPilotUIScene &frogpilot_scene = fs->frogpilot_scene;
  QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  if (frogpilot_scene.downloading_update || frogpilot_scene.frogpilot_panel_active) {
    device()->resetInteractiveTimeout(frogpilot_toggles.value("screen_timeout").toInt(), frogpilot_toggles.value("screen_timeout_onroad").toInt());
  }

  fs->update();
}

Device::Device(QObject *parent) : brightness_filter(BACKLIGHT_OFFROAD, BACKLIGHT_TS, BACKLIGHT_DT), QObject(parent) {
  setAwake(true);
  resetInteractiveTimeout();

  QObject::connect(uiState(), &UIState::uiUpdate, this, &Device::update);
}

void Device::update(const UIState &s, const FrogPilotUIState &fs) {
  updateBrightness(s, fs);
  updateWakefulness(s, fs);
}

void Device::setAwake(bool on) {
  if (on != awake) {
    awake = on;
    Hardware::set_display_power(awake);
    LOGD("setting display power %d", awake);
    emit displayPowerChanged(awake);
  }
}

void Device::resetInteractiveTimeout(int timeout, int timeout_onroad) {
  if (timeout == -1) {
    timeout = (ignition_on ? 10 : 30);
  } else {
    // FrogPilot variables
    timeout = (ignition_on ? timeout_onroad : timeout);
  }
  interactive_timeout = timeout * UI_FREQ;
}

void Device::updateBrightness(const UIState &s, const FrogPilotUIState &fs) {
  // FrogPilot variables
  const FrogPilotUIScene &frogpilot_scene = fs.frogpilot_scene;
  const QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  float clipped_brightness = offroad_brightness;
  if (s.scene.started && s.scene.light_sensor >= 0) {
    const float raw_light_sensor = s.scene.light_sensor;
    const float auto_brightness_min = raw_light_sensor > AUTO_BRIGHTNESS_DARK_THRESHOLD ? AUTO_BRIGHTNESS_DIM_FLOOR : AUTO_BRIGHTNESS_MIN;
    // scene.light_sensor is already a non-linear brightness estimate derived
    // from camera exposure, so applying another CIE transform crushes dusk and
    // street-lit scenes down to the dim floor too aggressively.
    clipped_brightness = std::clamp(raw_light_sensor, auto_brightness_min, AUTO_BRIGHTNESS_MAX);
  }

  int brightness = brightness_filter.update(clipped_brightness);
  if (!awake) {
    brightness = 0;
  } else if (s.scene.started && !frogpilot_scene.wake_up_screen && interactive_timeout == 0 && frogpilot_toggles.value("standby_mode").toBool()) {
    brightness = 0;
  } else if (s.scene.started && frogpilot_toggles.value("screen_brightness_onroad").toInt() != 101) {
    brightness = interactive_timeout > 0 ? fmax(5, frogpilot_toggles.value("screen_brightness_onroad").toInt()) : frogpilot_toggles.value("screen_brightness_onroad").toInt();
  } else if (frogpilot_toggles.value("screen_brightness").toInt() != 101) {
    brightness = frogpilot_toggles.value("screen_brightness").toInt();
  }

  if (brightness != last_brightness) {
    if (!brightness_future.isRunning()) {
      brightness_future = QtConcurrent::run(Hardware::set_brightness, brightness);
      last_brightness = brightness;
    }
  }
}

void Device::updateWakefulness(const UIState &s, const FrogPilotUIState &fs) {
  // FrogPilot variables
  const FrogPilotUIScene &frogpilot_scene = fs.frogpilot_scene;
  const QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  const int offroad_wake_counter = params_memory.getInt("OffroadWakeCounter");
  if (!s.scene.started && !s.scene.ignition && offroad_wake_counter != last_offroad_wake_counter) {
    int wake_timeout = frogpilot_toggles.value("screen_timeout").toInt();
    if (wake_timeout <= 0) {
      wake_timeout = 20;
    }
    resetInteractiveTimeout(wake_timeout, wake_timeout);
  }
  last_offroad_wake_counter = offroad_wake_counter;

  bool ignition_just_turned_off = !s.scene.ignition && ignition_on;
  ignition_on = s.scene.ignition;

  if (ignition_on && frogpilot_toggles.value("standby_mode").toBool()) {
    if (frogpilot_scene.wake_up_screen) {
      resetInteractiveTimeout(frogpilot_toggles.value("screen_timeout").toInt(), frogpilot_toggles.value("screen_timeout_onroad").toInt());
    } else {
      resetInteractiveTimeout(0, 0);
    }
  } else if (ignition_just_turned_off) {
    resetInteractiveTimeout(frogpilot_toggles.value("screen_timeout").toInt(), frogpilot_toggles.value("screen_timeout_onroad").toInt());
  } else if (ignition_on && frogpilot_toggles.value("screen_brightness_onroad").toInt() == 0) {
    resetInteractiveTimeout(0, 0);
  } else if (interactive_timeout > 0 && --interactive_timeout == 0) {
    emit interactiveTimeout();
  }

  setAwake(s.scene.ignition || interactive_timeout > 0);
}

UIState *uiState() {
  static UIState ui_state;
  return &ui_state;
}

Device *device() {
  static Device _device;
  return &_device;
}
