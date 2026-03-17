#include "selfdrive/ui/qt/onroad/hud.h"

#include <cmath>

#include "selfdrive/ui/qt/util.h"

constexpr int SET_SPEED_NA = 255;
constexpr int STEERING_ICON_SIZE = 156;
constexpr int HUD_SIDE_MARGIN = 104;
constexpr int HUD_BOTTOM_MARGIN = 96;
constexpr float PREVIEW_SPEED_KPH = 192.0f;
constexpr float PREVIEW_SET_SPEED_KPH = 65.0f;

HudRenderer::HudRenderer() {}

void HudRenderer::updateState(const UIState &s) {
  is_metric = s.scene.is_metric;
  status = s.status;

  const SubMaster &sm = *(s.sm);
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

void HudRenderer::drawSteeringWheelIcon(QPainter &p, const QRect &surface_rect) {
  static const int wheel_button_size = STEERING_ICON_SIZE;
  static const int wheel_icon_size = 150;
  static const QPixmap wheel_img = loadPixmap("../../files/icons/steeringwheel.png", {wheel_icon_size, wheel_icon_size});

  const int right_margin = HUD_SIDE_MARGIN;
  const int bottom_margin = HUD_BOTTOM_MARGIN;
  QPoint center(surface_rect.width() - right_margin - wheel_button_size / 2,
                surface_rect.height() - bottom_margin - wheel_button_size / 2);

  QColor bg = QColor(0x0A, 0x10, 0x16, 0xA8);
  QColor tint = QColor(0xE9, 0xEF, 0xF5, 0xF0);
  if (status == STATUS_ENGAGED || status == STATUS_ALWAYS_ON_LATERAL_ACTIVE || status == STATUS_TRAFFIC_MODE_ENABLED) {
    bg = QColor(0x0D, 0x16, 0x12, 0xB6);
    tint = QColor(0x49, 0xD2, 0x83);
  } else if (status == STATUS_OVERRIDE) {
    tint = QColor(0xF1, 0xE7, 0xD0, 0xF0);
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

    p.drawPixmap(center.x() - tinted.width() / 2, center.y() - tinted.height() / 2, tinted);
  }
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
