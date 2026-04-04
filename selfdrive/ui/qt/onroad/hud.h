#pragma once

#include <QPainter>
#include "selfdrive/ui/ui.h"

#include "frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h"

class HudRenderer : public QObject {
  Q_OBJECT

public:
  HudRenderer();
  void updateState(const UIState &s);
  void draw(QPainter &p, const QRect &surface_rect);

  // FrogPilot variables
  FrogPilotAnnotatedCameraWidget *frogpilot_nvg;

  QJsonObject frogpilot_toggles;

private:
  void drawSetSpeed(QPainter &p, const QRect &surface_rect);
  void drawCurrentSpeed(QPainter &p, const QRect &surface_rect);
  void drawModifiedSpeed(QPainter &p, const QRect &speed_rect);
  void drawSeatbeltIcon(QPainter &p, const QRect &surface_rect);
  void drawLfaIcon(QPainter &p, const QRect &surface_rect);
  void drawSteeringLimitWarningIcon(QPainter &p, const QRect &surface_rect);
  void drawSteeringWheelIcon(QPainter &p, const QRect &surface_rect);
  void drawText(QPainter &p, const QRect &rect, const QString &text, const QColor &color = QColor(0xFF, 0xFF, 0xFF),
                Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter);
  void drawSpeedChevronCluster(QPainter &p, const QPoint &origin, const QColor &color);

  float speed = 0;
  float set_speed = 0;
  bool is_cruise_set = false;
  bool is_cruise_available = true;
  bool is_metric = false;
  bool v_ego_cluster_seen = false;
  bool selfdrive_enabled = false;
  bool selfdrive_engageable = false;
  bool seatbelt_unlatched = false;
  bool longitudinal_override_active = false;
  bool lateral_override_active = false;
  bool steer_limit_warning_active = false;
  float steering_torque_pct = 0.0f;
  float steering_angle_deg = 0.0f;
  float modified_speed = 0.0f;
  bool show_modified_speed = false;
  bool modified_speed_blink = false;
  bool modified_speed_hide_phase = false;
  QColor modified_speed_color = QColor(0xF6, 0xF8, 0xFB, 0xF4);
  int status = STATUS_DISENGAGED;
};
