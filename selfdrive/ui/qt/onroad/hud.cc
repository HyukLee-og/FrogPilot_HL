#include "selfdrive/ui/qt/onroad/hud.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "common/params.h"
#include "common/util.h"
#include "selfdrive/ui/qt/util.h"

constexpr int SET_SPEED_NA = 255;
constexpr int STEERING_ICON_SIZE = 156;
constexpr int STEERING_ICON_WARNING_SIZE = 188;
constexpr int LFA_ICON_SIZE = 104;
constexpr int SEATBELT_ICON_HEIGHT = 88;
constexpr int HUD_SIDE_MARGIN = 104;
constexpr int HUD_BOTTOM_MARGIN = 96;
constexpr float PREVIEW_SPEED_KPH = 192.0f;
constexpr float PREVIEW_SET_SPEED_KPH = 65.0f;

namespace {
QColor blendColor(const QColor &a, const QColor &b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return QColor(
    std::lround(a.red() + (b.red() - a.red()) * t),
    std::lround(a.green() + (b.green() - a.green()) * t),
    std::lround(a.blue() + (b.blue() - a.blue()) * t),
    std::lround(a.alpha() + (b.alpha() - a.alpha()) * t)
  );
}
}  // namespace

HudRenderer::HudRenderer() {}

void HudRenderer::updateState(const UIState &s) {
  is_metric = s.scene.is_metric;
  status = s.status;

  const SubMaster &sm = *(s.sm);
  const auto &selfdrive_state = sm["selfdriveState"].getSelfdriveState();
  const QString current_alert_type = QString::fromUtf8(selfdrive_state.getAlertType().cStr());
  bool torque_preview_ok = false;
  const float torque_preview = qEnvironmentVariable("STEERING_TORQUE_PREVIEW").trimmed().toFloat(&torque_preview_ok);
  bool angle_preview_ok = false;
  const float angle_preview = qEnvironmentVariable("STEERING_ANGLE_PREVIEW").trimmed().toFloat(&angle_preview_ok);
  const bool accel_override_preview = qEnvironmentVariableIntValue("LFA_ACCEL_OVERRIDE_PREVIEW") == 1;
  const bool steering_override_preview = qEnvironmentVariableIntValue("STEERING_OVERRIDE_PREVIEW") == 1;
  selfdrive_enabled = selfdrive_state.getEnabled();
  selfdrive_engageable = selfdrive_state.getEngageable() || selfdrive_enabled;
  longitudinal_override_active = accel_override_preview;
  lateral_override_active = steering_override_preview;
  steer_limit_warning_active = current_alert_type.contains("steerSaturated", Qt::CaseInsensitive) ||
                               qEnvironmentVariableIntValue("STEER_LIMIT_PREVIEW") == 1 ||
                               (torque_preview_ok && torque_preview >= 0.95f);
  steering_torque_pct = 0.0f;
  steering_angle_deg = angle_preview_ok ? angle_preview : 0.0f;
  Params params;
  const bool force_preview = params.getBool("ForceOnroad") && sm.rcv_frame("carState") < s.scene.started_frame;
  if (force_preview) {
    is_cruise_set = true;
    is_cruise_available = true;
    set_speed = is_metric ? PREVIEW_SET_SPEED_KPH : PREVIEW_SET_SPEED_KPH * KM_TO_MILE;
    speed = is_metric ? PREVIEW_SPEED_KPH : PREVIEW_SPEED_KPH * KM_TO_MILE;
  }

  if (!force_preview && sm.rcv_frame("carState") < s.scene.started_frame) {
    is_cruise_set = false;
    set_speed = SET_SPEED_NA;
    speed = 0.0;
    show_modified_speed = false;
    return;
  }

  if (!force_preview) {
    const auto &controls_state = sm["controlsState"].getControlsState();
    const auto &car_state = sm["carState"].getCarState();
    seatbelt_unlatched = car_state.getSeatbeltUnlatched();
    const auto lateral_state = controls_state.getLateralControlState();
    const auto lateral_which = lateral_state.which();
    const bool is_overriding = selfdrive_state.getState() == cereal::SelfdriveState::OpenpilotState::OVERRIDING;
    if (is_overriding) {
      longitudinal_override_active = longitudinal_override_active || car_state.getGasPressed();
      lateral_override_active = lateral_override_active || car_state.getSteeringPressed();
    }
    if (!angle_preview_ok) {
      steering_angle_deg = -car_state.getSteeringAngleDeg();
    }

    switch (lateral_which) {
      case cereal::ControlsState::LateralControlState::TORQUE_STATE: {
        const auto torque_state = lateral_state.getTorqueState();
        steering_torque_pct = std::clamp(std::abs(torque_state.getOutput()), 0.0f, 1.0f);
        if (torque_state.getSaturated()) steering_torque_pct = 1.0f;
        break;
      }
      case cereal::ControlsState::LateralControlState::PID_STATE: {
        const auto pid_state = lateral_state.getPidState();
        steering_torque_pct = std::clamp(std::abs(pid_state.getOutput()), 0.0f, 1.0f);
        if (pid_state.getSaturated()) steering_torque_pct = 1.0f;
        break;
      }
      case cereal::ControlsState::LateralControlState::ANGLE_STATE: {
        const auto angle_state = lateral_state.getAngleState();
        steering_torque_pct = std::clamp(std::abs(angle_state.getOutput()), 0.0f, 1.0f);
        if (angle_state.getSaturated()) steering_torque_pct = 1.0f;
        break;
      }
      case cereal::ControlsState::LateralControlState::DEBUG_STATE: {
        const auto debug_state = lateral_state.getDebugState();
        steering_torque_pct = std::clamp(std::abs(debug_state.getOutput()), 0.0f, 1.0f);
        if (debug_state.getSaturated()) steering_torque_pct = 1.0f;
        break;
      }
      default:
        break;
    }

    // Handle older routes where vCruiseCluster is not set
    set_speed = car_state.getVCruiseCluster() == 0.0 ? controls_state.getVCruiseDEPRECATED() : car_state.getVCruiseCluster();
    is_cruise_set = set_speed > 0 && set_speed != SET_SPEED_NA;
    is_cruise_available = set_speed != -1;

    if (is_cruise_set && !is_metric) {
      set_speed *= KM_TO_MILE;
    }

    // Handle older routes where vEgoCluster is not set
    v_ego_cluster_seen = v_ego_cluster_seen || car_state.getVEgoCluster() != 0.0;
    float v_ego = v_ego_cluster_seen && !frogpilot_toggles.value("use_wheel_speed").toBool() ? car_state.getVEgoCluster() : car_state.getVEgo();
    speed = std::max<float>(0.0f, v_ego * (is_metric ? MS_TO_KPH : MS_TO_MPH));
  }

  show_modified_speed = false;
  modified_speed_blink = false;
  modified_speed_hide_phase = false;
  modified_speed = 0.0f;
  modified_speed_color = QColor(0xF6, 0xF8, 0xFB, 0xF4);

  Params params_memory{"", true};
  const std::string fake_long_debug = params_memory.get("FakeLongDebug");
  if (!fake_long_debug.empty()) {
    const QJsonObject debug = QJsonDocument::fromJson(QByteArray::fromStdString(fake_long_debug)).object();
    const bool apn_enabled = debug.value("apnEnabled").toBool(false);
    const bool apn_control_active = debug.value("apnControlActive").toBool(false);
    const bool apn_recovery_active = debug.value("apnRecoveryActive").toBool(false);
    const float apn_target_speed = debug.value("apnTarget").toDouble(0.0);
    const float user_set_speed = debug.value("userSet").toDouble(0.0);
    const float actual_set_speed = debug.value("set").toDouble(0.0);

    const float unit_conversion = is_metric ? MS_TO_KPH : MS_TO_MPH;
    const float phase_match_window = 3.0f;
    const float actual_speed = speed;

    if (apn_enabled && apn_control_active && apn_target_speed > 0.1f) {
      modified_speed = apn_target_speed * unit_conversion;
      show_modified_speed = true;
      if (std::abs(actual_speed - modified_speed) <= phase_match_window) {
        modified_speed_color = QColor(0x35, 0xD0, 0x7F, 0xF6);
      } else {
        modified_speed_color = QColor(0xFF, 0xA1, 0x2A, 0xF6);
        modified_speed_blink = true;
      }
    } else if (apn_enabled && apn_recovery_active && user_set_speed > 0.1f && actual_set_speed > 0.1f) {
      const float user_set_display = user_set_speed * unit_conversion;
      const float actual_set_display = actual_set_speed * unit_conversion;
      if (actual_set_display < user_set_display - 0.5f) {
        modified_speed = user_set_display;
        show_modified_speed = true;
        modified_speed_color = QColor(0x4F, 0x8D, 0xFF, 0xF6);
        modified_speed_blink = true;
      } else {
        modified_speed_hide_phase = true;
      }
    }
  }
}

void HudRenderer::draw(QPainter &p, const QRect &surface_rect) {
  p.save();

  if (is_cruise_available) {
    drawSetSpeed(p, surface_rect);
  }
  if (frogpilot_nvg->standstillDuration == 0 && !frogpilot_toggles.value("hide_speed").toBool()) {
    drawCurrentSpeed(p, surface_rect);
  }
  drawSeatbeltIcon(p, surface_rect);
  drawLfaIcon(p, surface_rect);
  drawSteeringLimitWarningIcon(p, surface_rect);
  drawSteeringWheelIcon(p, surface_rect);

  p.restore();
}

void HudRenderer::drawSetSpeed(QPainter &p, const QRect &surface_rect) {
  const QSize default_size = {172, 204};
  QSize set_speed_size = default_size;
  if (frogpilot_nvg->speedLimitHeight != 0) {
    set_speed_size.rheight() += frogpilot_nvg->speedLimitHeight;
  }

  QString set_speed_str = is_cruise_set ? QString::number(std::nearbyint(set_speed)) : "–";
  QColor value_color = QColor(0xF2, 0xF5, 0xF9, 0xF2);
  QColor label_color = QColor(0xD1, 0xD8, 0xDF, 0xD0);
  if (!is_cruise_set || status == STATUS_DISENGAGED) {
    value_color = QColor(0xD2, 0xD9, 0xE1, 0xCA);
    label_color = QColor(0xA7, 0xB0, 0xBA, 0xB8);
  } else if (status == STATUS_OVERRIDE) {
    value_color = QColor(0xF3, 0xE6, 0xCB, 0xE2);
    label_color = QColor(0xD6, 0xC3, 0x98, 0xBA);
  }

  QFont label_font = InterFont(28, QFont::DemiBold);
  QFont value_font = InterFont(88, QFont::Bold);
  const int steering_block_width = STEERING_ICON_SIZE;
  const int right_margin = HUD_SIDE_MARGIN;
  const int bottom_margin = HUD_BOTTOM_MARGIN;
  const int gap = 28;
  const int block_width = 236;
  const int block_height = 118;
  QRect set_speed_rect(surface_rect.width() - right_margin - steering_block_width - gap - block_width,
                       surface_rect.height() - bottom_margin - block_height,
                       block_width, block_height);

  if (!frogpilot_toggles.value("hide_max_speed").toBool()) {
    p.setFont(label_font);
    drawText(p, QRect(set_speed_rect.x(), set_speed_rect.y() + 44, 72, 34), tr("SET"), label_color,
             Qt::AlignLeft | Qt::AlignVCenter);

    p.setFont(value_font);
    drawText(p, QRect(set_speed_rect.x() + 72, set_speed_rect.y() - 6, set_speed_rect.width() - 72, block_height), set_speed_str,
             value_color, Qt::AlignLeft | Qt::AlignVCenter);
  }

  QRect set_speed_layout_rect = set_speed_rect;
  if (frogpilot_nvg->speedLimitHeight != 0) {
    set_speed_layout_rect.moveTop(set_speed_rect.top() - 40);
    set_speed_layout_rect.setHeight(default_size.height() + frogpilot_nvg->speedLimitHeight + 40);
  }

  frogpilot_nvg->defaultSize = default_size;
  frogpilot_nvg->isCruiseSet = is_cruise_set;
  frogpilot_nvg->setSpeedRect = set_speed_layout_rect;
  frogpilot_nvg->speed = speed;
}

void HudRenderer::drawCurrentSpeed(QPainter &p, const QRect &surface_rect) {
  QString speedStr = QString::number(std::nearbyint(speed));
  QColor speed_color = QColor(0xF6, 0xF8, 0xFB, 0xF4);
  QColor unit_color = QColor(0xDF, 0xE5, 0xEA, 0xD4);

  if (status == STATUS_DISENGAGED) {
    speed_color = QColor(0xE2, 0xE7, 0xEC, 0xE6);
    unit_color = QColor(0xC5, 0xCF, 0xD8, 0xC8);
  } else if (status == STATUS_OVERRIDE) {
    unit_color = QColor(0xE8, 0xDC, 0xC2, 0xC4);
  }

  if (frogpilot_nvg != nullptr &&
      frogpilot_nvg->hasAPNCameraAlert() &&
      frogpilot_nvg->getAPNCameraSpeed() > 0.1f &&
      speed > frogpilot_nvg->getAPNCameraSpeed()) {
    speed_color = QColor(0xFF, 0x45, 0x45, 0xF6);
  }

  const int group_top = surface_rect.height() - 246;
  const int center_x = surface_rect.center().x();
  p.setFont(InterFont(148, QFont::Bold));
  QRect speed_rect(center_x - 230, group_top - 18, 460, 162);
  frogpilot_nvg->currentSpeedRect = speed_rect;
  drawModifiedSpeed(p, speed_rect);
  drawText(p, speed_rect, speedStr, speed_color, Qt::AlignHCenter | Qt::AlignBottom);

  p.setFont(InterFont(34, QFont::DemiBold));
  QRect unit_rect(center_x - 116, speed_rect.bottom() + 2, 232, 40);
  drawText(p, unit_rect, is_metric ? tr("KM/H") : tr("MPH"), unit_color, Qt::AlignHCenter | Qt::AlignTop);
}

void HudRenderer::drawModifiedSpeed(QPainter &p, const QRect &speed_rect) {
  if (!show_modified_speed || modified_speed <= 0.1f || modified_speed_hide_phase) {
    return;
  }

  const bool blink_on = ((QDateTime::currentMSecsSinceEpoch() / 420) % 2) == 0;
  if (modified_speed_blink && !blink_on) {
    return;
  }

  const QString modified_speed_str = QString::number(std::nearbyint(modified_speed));
  const QRect modified_speed_rect(speed_rect.left() - 154, speed_rect.top() + 26, 138, 106);

  p.save();
  p.setFont(InterFont(76, QFont::Bold));
  drawText(p, modified_speed_rect, modified_speed_str, modified_speed_color, Qt::AlignRight | Qt::AlignVCenter);
  p.restore();
}

void HudRenderer::drawSeatbeltIcon(QPainter &p, const QRect &surface_rect) {
  static const QPixmap seatbelt_img = loadPixmap("../../files/icons/seatbelt.png", {LFA_ICON_SIZE, LFA_ICON_SIZE});
  if (seatbelt_img.isNull() || !seatbelt_unlatched) return;

  const int group_top = surface_rect.height() - 246;
  const int center_x = surface_rect.center().x();
  const QRect speed_rect(center_x - 230, group_top - 18, 460, 162);
  const int icon_height = SEATBELT_ICON_HEIGHT;
  const int icon_width = std::lround(float(seatbelt_img.width()) * float(icon_height) / float(seatbelt_img.height()));
  const QRect icon_rect(speed_rect.left() - 10 - icon_width, speed_rect.top() + 2, icon_width, icon_height);

  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);

  QRect glow_rect = icon_rect.adjusted(-18, -18, 18, 18);
  QRadialGradient glow(glow_rect.center(), glow_rect.width() * 0.55);
  glow.setColorAt(0.0, QColor(0xFF, 0x45, 0x45, 84));
  glow.setColorAt(0.45, QColor(0xFF, 0x45, 0x45, 36));
  glow.setColorAt(1.0, QColor(0xFF, 0x45, 0x45, 0));
  p.setBrush(glow);
  p.drawEllipse(glow_rect);
  p.restore();

  p.save();
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawPixmap(icon_rect, seatbelt_img);
  p.restore();
}

void HudRenderer::drawLfaIcon(QPainter &p, const QRect &surface_rect) {
  static const QPixmap lfa_img = loadPixmap("../../files/icons/lfa.png", {LFA_ICON_SIZE, LFA_ICON_SIZE});
  if (lfa_img.isNull()) return;

  const int group_top = surface_rect.height() - 246;
  const int center_x = surface_rect.center().x();
  QRect speed_rect(center_x - 230, group_top - 18, 460, 162);
  QRect icon_rect(speed_rect.right() - 84, speed_rect.top() + 2, LFA_ICON_SIZE, LFA_ICON_SIZE);
  const bool active_preview = qEnvironmentVariableIntValue("LFA_ACTIVE_PREVIEW") == 1;

  QColor tint = QColor(0x92, 0x9D, 0xA8, 0xE6);
  QColor glow_base(0x49, 0xD2, 0x83);
  if (longitudinal_override_active) {
    tint = QColor(0x4F, 0x8D, 0xFF);
    glow_base = QColor(0x4F, 0x8D, 0xFF);
  } else if (selfdrive_enabled || active_preview) {
    tint = QColor(0x49, 0xD2, 0x83);
  } else if (selfdrive_engageable) {
    tint = QColor(0xF4, 0xF7, 0xFB, 0xF4);
  }

  if (selfdrive_enabled || active_preview || longitudinal_override_active) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    QRect glow_rect = icon_rect.adjusted(-26, -22, 26, 24);
    QRadialGradient glow(glow_rect.center(), glow_rect.width() * 0.55);
    glow.setColorAt(0.0, QColor(glow_base.red(), glow_base.green(), glow_base.blue(), 76));
    glow.setColorAt(0.45, QColor(glow_base.red(), glow_base.green(), glow_base.blue(), 34));
    glow.setColorAt(1.0, QColor(glow_base.red(), glow_base.green(), glow_base.blue(), 0));
    p.setBrush(glow);
    p.drawEllipse(glow_rect);
    p.restore();
  }

  QPixmap tinted(lfa_img.size());
  tinted.fill(Qt::transparent);

  QPainter painter(&tinted);
  painter.drawPixmap(0, 0, lfa_img);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(tinted.rect(), tint);
  painter.end();

  p.save();
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawPixmap(icon_rect, tinted);
  p.restore();

  Params params_memory{"", true};
  bool show_apn_badge = false;
  if (util::read_file(params_memory.getParamPath("APNDataActive")) == "1") {
    const QString apn_timestamp_raw = QString::fromStdString(util::read_file(params_memory.getParamPath("APNDataTimestamp"))).trimmed();
    const double apn_timestamp = apn_timestamp_raw.toDouble();
    const double now_secs = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    show_apn_badge = apn_timestamp > 0.0 && (now_secs - apn_timestamp) < 10.0;
  }
  if (!show_apn_badge && util::getenv("OPENPILOT_PREFIX", "") == "routedemo") {
    show_apn_badge = true;
  }

  if (show_apn_badge) {
    const QRect badge_rect(icon_rect.center().x() - 44, icon_rect.bottom() + 8, 88, 30);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(28, 176, 94, 236));
    p.drawRoundedRect(badge_rect, 15, 15);
    p.setPen(Qt::white);
    p.setFont(InterFont(18, QFont::Bold));
    p.drawText(badge_rect, Qt::AlignCenter, "APN");
    p.restore();
  }
}

void HudRenderer::drawSteeringWheelIcon(QPainter &p, const QRect &surface_rect) {
  const int wheel_button_size = steer_limit_warning_active ? STEERING_ICON_WARNING_SIZE : STEERING_ICON_SIZE;
  const int wheel_icon_size = steer_limit_warning_active ? 178 : 150;
  const QPixmap wheel_img = loadPixmap("../../files/icons/steeringwheel.png", {wheel_icon_size, wheel_icon_size});
  const QString torque_preview_str = qEnvironmentVariable("STEERING_TORQUE_PREVIEW").trimmed();
  bool torque_preview_ok = false;
  const float torque_preview = torque_preview_str.toFloat(&torque_preview_ok);

  const int right_margin = HUD_SIDE_MARGIN;
  const int bottom_margin = HUD_BOTTOM_MARGIN;
  QPoint center(surface_rect.width() - right_margin - wheel_button_size / 2,
                surface_rect.height() - bottom_margin - wheel_button_size / 2);

  QColor bg = QColor(0x0A, 0x10, 0x16, 0xA8);
  QColor tint = QColor(0x9F, 0xA8, 0xB2, 0xE6);
  const bool steering_active = selfdrive_enabled || longitudinal_override_active || lateral_override_active || (torque_preview_ok && torque_preview >= 0.0f);
  const float torque_level = torque_preview_ok ? std::clamp(torque_preview, 0.0f, 1.0f) : steering_torque_pct;

  if (lateral_override_active) {
    bg = QColor(0x0A, 0x12, 0x1F, 0xB8);
    tint = QColor(0x4F, 0x8D, 0xFF);
  } else if (steering_active) {
    const QColor active_white(0xF4, 0xF7, 0xFB, 0xF4);
    const QColor warm_color(0xFF, 0xC7, 0x58, 0xF6);
    const QColor hot_color(0xFF, 0x5D, 0x57, 0xF8);
    bg = QColor(0x0D, 0x16, 0x12, 0xB6);
    if (torque_level < 0.65f) {
      tint = blendColor(active_white, warm_color, torque_level / 0.65f * 0.35f);
    } else {
      tint = blendColor(warm_color, hot_color, (torque_level - 0.65f) / 0.35f);
    }
  }

  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawEllipse(center, wheel_button_size / 2, wheel_button_size / 2);

  if (!wheel_img.isNull()) {
    QPixmap tinted(wheel_img.size());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, wheel_img);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), tint);
    painter.end();

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(center);
    p.rotate(steering_angle_deg);
    p.drawPixmap(-tinted.width() / 2, -tinted.height() / 2, tinted);
    p.restore();
  }
  p.restore();
}

void HudRenderer::drawSteeringLimitWarningIcon(QPainter &p, const QRect &surface_rect) {
  if (!steer_limit_warning_active) return;

  static const int warning_icon_size = 76;
  static const QPixmap warning_img = loadPixmap("../../files/icons/warning.png", {warning_icon_size, warning_icon_size});
  if (warning_img.isNull()) return;

  const int wheel_button_size = steer_limit_warning_active ? STEERING_ICON_WARNING_SIZE : STEERING_ICON_SIZE;
  const int right_margin = HUD_SIDE_MARGIN;
  const int bottom_margin = HUD_BOTTOM_MARGIN;
  const QPoint wheel_center(surface_rect.width() - right_margin - wheel_button_size / 2,
                            surface_rect.height() - bottom_margin - wheel_button_size / 2);
  const QRect wheel_rect(wheel_center.x() - wheel_button_size / 2,
                         wheel_center.y() - wheel_button_size / 2,
                         wheel_button_size,
                         wheel_button_size);
  const QRect icon_rect(wheel_rect.left() - 84, wheel_rect.top() - 20, warning_icon_size, warning_icon_size);

  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  QRect glow_rect = icon_rect.adjusted(-26, -22, 26, 22);
  QRadialGradient glow(glow_rect.center(), glow_rect.width() * 0.55);
  glow.setColorAt(0.0, QColor(0xFF, 0x68, 0x5B, 64));
  glow.setColorAt(0.45, QColor(0xFF, 0x68, 0x5B, 26));
  glow.setColorAt(1.0, QColor(0xFF, 0x68, 0x5B, 0));
  p.setPen(Qt::NoPen);
  p.setBrush(glow);
  p.drawEllipse(glow_rect);
  p.drawPixmap(icon_rect, warning_img);
  p.restore();
}

void HudRenderer::drawText(QPainter &p, const QRect &rect, const QString &text, const QColor &color, Qt::Alignment alignment) {
  p.setPen(color);
  p.drawText(rect, alignment | Qt::TextWordWrap, text);
}

void HudRenderer::drawSpeedChevronCluster(QPainter &p, const QPoint &origin, const QColor &color) {
  QPen pen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  p.setPen(pen);

  const int chevron_width = 18;
  const int chevron_height = 20;
  const int spacing = 28;
  for (int i = 0; i < 3; ++i) {
    const int x = origin.x() + (i * spacing);
    const int y = origin.y();
    p.drawLine(QPoint(x, y), QPoint(x + chevron_width, y + chevron_height));
    p.drawLine(QPoint(x, y + (chevron_height * 2)), QPoint(x + chevron_width, y + chevron_height));
  }
}
