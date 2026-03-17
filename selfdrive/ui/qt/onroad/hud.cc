#include "selfdrive/ui/qt/onroad/hud.h"

#include <cmath>

#include "selfdrive/ui/qt/util.h"

constexpr int SET_SPEED_NA = 255;

HudRenderer::HudRenderer() {}

void HudRenderer::updateState(const UIState &s) {
  is_metric = s.scene.is_metric;
  status = s.status;

  const SubMaster &sm = *(s.sm);
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

  // Draw header gradient
  QLinearGradient bg(0, UI_HEADER_HEIGHT - (UI_HEADER_HEIGHT / 2.5), 0, UI_HEADER_HEIGHT);
  bg.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.45));
  bg.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
  p.fillRect(0, 0, surface_rect.width(), UI_HEADER_HEIGHT, bg);


  if (is_cruise_available) {
    drawSetSpeed(p, surface_rect);
  }
  if (frogpilot_nvg->standstillDuration == 0 && !frogpilot_toggles.value("hide_speed").toBool()) {
    drawCurrentSpeed(p, surface_rect);
  }

  p.restore();
}

void HudRenderer::drawSetSpeed(QPainter &p, const QRect &surface_rect) {
  const QSize default_size = {172, 204};
  QSize set_speed_size = default_size;
  if (frogpilot_nvg->speedLimitHeight != 0) {
    set_speed_size.rheight() += frogpilot_nvg->speedLimitHeight;
  }

  QRect set_speed_rect(QPoint(60, 45), set_speed_size);

  if (!frogpilot_toggles.value("hide_max_speed").toBool()) {
    QString set_speed_str = is_cruise_set ? QString::number(std::nearbyint(set_speed)) : "–";
    QFont value_font = InterFont(52, QFont::Bold);
    QFont label_font = InterFont(20, QFont::DemiBold);
    label_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);

    QRect current_speed_rect = QFontMetrics(InterFont(176, QFont::Bold)).boundingRect(QString::number(std::nearbyint(speed)));
    int speed_center_x = surface_rect.center().x();
    int set_speed_x = speed_center_x + (current_speed_rect.width() / 2) + 34;
    int set_speed_y = 132;
    int label_width = QFontMetrics(label_font).horizontalAdvance(tr("SET"));
    int value_width = QFontMetrics(value_font).horizontalAdvance(set_speed_str);
    int content_width = std::max(label_width, value_width);

    set_speed_rect = QRect(set_speed_x, set_speed_y, std::max(72, content_width + 8), 78);

    QColor label_color = QColor(0xA5, 0xAE, 0xB8, 0xFF);
    QColor value_color = QColor(0x75, 0x7D, 0x88, 0xFF);
    QColor accent_color = QColor(0x7F, 0x87, 0x91, 0xFF);
    if (is_cruise_set) {
      value_color = QColor(255, 255, 255, 0xFF);
      if (status == STATUS_ENGAGED || status == STATUS_ALWAYS_ON_LATERAL_ACTIVE || status == STATUS_TRAFFIC_MODE_ENABLED) {
        label_color = QColor(0x49, 0xD2, 0x83, 0xFF);
        accent_color = QColor(0x49, 0xD2, 0x83, 0xFF);
      } else if (status == STATUS_OVERRIDE) {
        label_color = QColor(0xC2, 0xCB, 0xC6, 0xFF);
        accent_color = QColor(0xB8, 0xC2, 0xBC, 0xFF);
      } else {
        label_color = QColor(0xE8, 0xEB, 0xEF, 0xFF);
        accent_color = QColor(0xD4, 0xDA, 0xE1, 0xFF);
      }
    }

    p.setPen(Qt::NoPen);
    QRect accent_rect(set_speed_rect.x() + 5, set_speed_rect.y() + 10, 8, 8);
    p.setBrush(accent_color);
    p.drawEllipse(accent_rect);

    p.setFont(label_font);
    p.setPen(label_color);
    p.drawText(set_speed_rect.adjusted(10, 0, 6, 0), Qt::AlignHCenter | Qt::AlignTop, tr("SET"));

    p.setFont(value_font);
    p.setPen(value_color);
    p.drawText(set_speed_rect.adjusted(0, 18, 0, 0), Qt::AlignHCenter | Qt::AlignBottom, set_speed_str);
  }

  QRect set_speed_layout_rect = set_speed_rect;
  if (frogpilot_nvg->speedLimitHeight != 0) {
    // Keep the visible SET card compact, but give FrogPilot's speed-limit widget
    // the vertical space it still expects beneath the card.
    set_speed_layout_rect.setHeight(set_speed_rect.height() + frogpilot_nvg->speedLimitHeight + 12);
  }

  frogpilot_nvg->defaultSize = default_size;
  frogpilot_nvg->isCruiseSet = is_cruise_set;
  frogpilot_nvg->setSpeedRect = set_speed_layout_rect;
  frogpilot_nvg->speed = speed;
}

void HudRenderer::drawCurrentSpeed(QPainter &p, const QRect &surface_rect) {
  QString speedStr = QString::number(std::nearbyint(speed));

  p.setFont(InterFont(176, QFont::Bold));
  drawText(p, surface_rect.center().x(), 210, speedStr);

  p.setFont(InterFont(66));
  drawText(p, surface_rect.center().x(), 290, is_metric ? tr("km/h") : tr("mph"), 200);
}

void HudRenderer::drawText(QPainter &p, int x, int y, const QString &text, int alpha) {
  QRect real_rect = p.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}
