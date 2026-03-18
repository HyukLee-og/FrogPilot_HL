#include "selfdrive/ui/qt/onroad/hud.h"

#include <algorithm>
#include <cmath>

#include "selfdrive/ui/qt/util.h"

constexpr int SET_SPEED_NA = 255;
constexpr int STEERING_ICON_SIZE = 156;
constexpr int STEERING_ICON_WARNING_SIZE = 188;
constexpr int LFA_ICON_SIZE = 104;
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
    return;
  }

  if (sm.rcv_frame("carState") < s.scene.started_frame) {
    is_cruise_set = false;
    set_speed = SET_SPEED_NA;
    speed = 0.0;
    return;
  }

  const auto &controls_state = sm["controlsState"].getControlsState();
  const auto &car_state = sm["carState"].getCarState();
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

void HudRenderer::draw(QPainter &p, const QRect &surface_rect) {
  p.save();

  if (is_cruise_available) {
    drawSetSpeed(p, surface_rect);
  }
  if (frogpilot_nvg->standstillDuration == 0 && !frogpilot_toggles.value("hide_speed").toBool()) {
    drawCurrentSpeed(p, surface_rect);
  }
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

  const int group_top = surface_rect.height() - 246;
  const int center_x = surface_rect.center().x();
  p.setFont(InterFont(148, QFont::Bold));
  QRect speed_rect(center_x - 230, group_top - 18, 460, 162);
  drawText(p, speed_rect, speedStr, speed_color, Qt::AlignHCenter | Qt::AlignBottom);

  p.setFont(InterFont(34, QFont::DemiBold));
  QRect unit_rect(center_x - 116, speed_rect.bottom() + 2, 232, 40);
  drawText(p, unit_rect, is_metric ? tr("KM/H") : tr("MPH"), unit_color, Qt::AlignHCenter | Qt::AlignTop);
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
