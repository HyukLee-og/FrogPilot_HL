#include "selfdrive/ui/qt/onroad/alerts.h"

#include <QDateTime>
#include <QPainter>
#include <map>

#include "selfdrive/ui/qt/util.h"

void OnroadAlerts::updateState(const UIState &s, const FrogPilotUIState &fs) {
  Alert a = getAlert(*(s.sm), *(fs.sm), s.scene.started_frame);
  const auto selfdrive_state = (*s.sm)["selfdriveState"].getSelfdriveState();
  bool selfdrive_enabled = selfdrive_state.getEnabled();
  bool selfdrive_engageable = selfdrive_state.getEngageable() || selfdrive_enabled;
  const bool was_resume_required = alert.type.contains("resumeRequired", Qt::CaseInsensitive);
  const bool is_resume_required = a.type.contains("resumeRequired", Qt::CaseInsensitive);
  bool parked_preview_ok = false;
  const int parked_preview_seconds = qEnvironmentVariableIntValue("PARKED_STANDSTILL_PREVIEW", &parked_preview_ok);
  const bool parked_overlay_requested =
    !is_resume_required &&
    frogpilot_toggles.value("stopped_timer").toBool() &&
    (parked_preview_ok ? parked_preview_seconds >= 0 : (fs.frogpilot_scene.parked && fs.frogpilot_scene.standstill));
  if (a.size == cereal::SelfdriveState::AlertSize::NONE && parked_overlay_requested) {
    a = Alert{tr("정차중"), "", "parkedStandstill",
              cereal::SelfdriveState::AlertSize::MID,
              cereal::SelfdriveState::AlertStatus::NORMAL};
  }
  const bool was_parked_standstill = parkedStandstillActive;
  const bool is_parked_standstill = a.type.contains("parkedStandstill", Qt::CaseInsensitive);
  const bool animate_special_alert =
    a.type.contains("fcw", Qt::CaseInsensitive) ||
    a.type.contains("aeb", Qt::CaseInsensitive) ||
    a.type.contains("ldw", Qt::CaseInsensitive) ||
    is_resume_required ||
    is_parked_standstill;

  if (is_resume_required && !was_resume_required) {
    resumeRequiredTimer.restart();
  } else if (!is_resume_required && was_resume_required) {
    resumeRequiredTimer.invalidate();
  }

  if (is_parked_standstill && !was_parked_standstill) {
    parkedStandstillTimer.restart();
  } else if (!is_parked_standstill && was_parked_standstill) {
    parkedStandstillTimer.invalidate();
  }

  parkedStandstillActive = is_parked_standstill;

  if (!alert.equal(a) || selfdriveEnabled != selfdrive_enabled || selfdriveEngageable != selfdrive_engageable || animate_special_alert) {
    alert = a;
    selfdriveEnabled = selfdrive_enabled;
    selfdriveEngageable = selfdrive_engageable;
    update();
  }

  // FrogPilot variables
  sidebarsOpen = fs.frogpilot_scene.sidebars_open;
}

void OnroadAlerts::clear() {
  alert = {};
  update();

  // FrogPilot variables
  alertHeight = 0;
  selfdriveEnabled = false;
  selfdriveEngageable = false;
  parkedStandstillActive = false;
  resumeRequiredTimer.invalidate();
  parkedStandstillTimer.invalidate();
}

OnroadAlerts::Alert OnroadAlerts::getAlert(const SubMaster &sm, const SubMaster &fpsm, uint64_t started_frame) {
  const cereal::SelfdriveState::Reader &ss = sm["selfdriveState"].getSelfdriveState();
  const uint64_t selfdrive_frame = sm.rcv_frame("selfdriveState");
  const bool force_onroad = Params().getBool("ForceOnroad") || frogpilot_toggles.value("force_onroad").toBool();
  if (qEnvironmentVariableIntValue("FCW_PREVIEW") == 1) {
    return Alert{tr("전방 추돌 주의"), tr("전방 차량과 추돌 위험이 있습니다"),
                 "fcwPreview", cereal::SelfdriveState::AlertSize::MID,
                 cereal::SelfdriveState::AlertStatus::CRITICAL};
  } else if (qEnvironmentVariableIntValue("LDW_PREVIEW") == 1) {
    return Alert{tr("차선 이탈 감지됨"), tr("운전에 주의하세요"),
                 "ldwPreview", cereal::SelfdriveState::AlertSize::MID,
                 cereal::SelfdriveState::AlertStatus::USER_PROMPT};
  } else if (qEnvironmentVariableIntValue("GREEN_LIGHT_PREVIEW") == 1) {
    return Alert{tr("신호가 변경되었습니다"), "",
                 "greenLightPreview", cereal::SelfdriveState::AlertSize::SMALL,
                 static_cast<cereal::SelfdriveState::AlertStatus>(3)};
  } else if (qEnvironmentVariableIntValue("BELOW_STEER_SPEED_PREVIEW") == 1) {
    return Alert{tr("조향 보조 비활성화됨"), tr("30 km/h 이상으로 주행하면 다시 활성화됩니다"),
                 "belowSteerSpeedPreview", cereal::SelfdriveState::AlertSize::FULL,
                 cereal::SelfdriveState::AlertStatus::NORMAL};
  } else if (qEnvironmentVariableIntValue("LEAD_DEPARTING_PREVIEW") == 1) {
    return Alert{"", tr("선행 차량이 출발하였습니다"),
                 "leadDepartingPreview", cereal::SelfdriveState::AlertSize::FULL,
                 cereal::SelfdriveState::AlertStatus::NORMAL};
  } else if (qEnvironmentVariableIntValue("RESUME_REQUIRED_PREVIEW") == 1) {
    return Alert{tr("오토 홀드"), tr("해제하려면 악셀을 밟거나 RES버튼을 누르세요"),
                 "resumeRequiredPreview", cereal::SelfdriveState::AlertSize::MID,
                 cereal::SelfdriveState::AlertStatus::NORMAL};
  } else {
    bool parked_preview_ok = false;
    const int parked_preview_seconds = qEnvironmentVariableIntValue("PARKED_STANDSTILL_PREVIEW", &parked_preview_ok);
    if (parked_preview_ok && parked_preview_seconds >= 0) {
      return Alert{tr("정차중"), "", "parkedStandstillPreview",
                   cereal::SelfdriveState::AlertSize::MID,
                   cereal::SelfdriveState::AlertStatus::NORMAL};
    }
  }

  // FrogPilot variables
  const cereal::FrogPilotSelfdriveState::Reader &fpss = fpsm["frogpilotSelfdriveState"].getFrogpilotSelfdriveState();

  Alert a = Alert{};
  static QString crash_log_path = "/data/error_logs/error.txt";
  if (QFile::exists(crash_log_path)) {
    if (frogpilot_toggles.value("random_events").toBool()) {
      a = Alert{tr("openpilot crashed 💩"),
                tr("Please post the \"Error Log\" in the FrogPilot Discord!"),
                "openpilotCrashedRandomEvent",
                cereal::SelfdriveState::AlertSize::MID,
                cereal::SelfdriveState::AlertStatus::CRITICAL};
    } else {
      a = Alert{tr("openpilot crashed"),
                tr("Please post the \"Error Log\" in the FrogPilot Discord!"),
                "openpilotCrashed",
                cereal::SelfdriveState::AlertSize::MID,
                cereal::SelfdriveState::AlertStatus::CRITICAL};
    }
    return a;
  } else if (selfdrive_frame >= started_frame) {  // Don't get old alert.
    a = Alert{ss.getAlertText1().cStr(), ss.getAlertText2().cStr(),
              ss.getAlertType().cStr(), ss.getAlertSize(), ss.getAlertStatus()};

    const Alert frogpilot_alert = Alert{
      fpss.getAlertText1().cStr(), fpss.getAlertText2().cStr(),
      fpss.getAlertType().cStr(),
      static_cast<cereal::SelfdriveState::AlertSize>(fpss.getAlertSize()),
      static_cast<cereal::SelfdriveState::AlertStatus>(fpss.getAlertStatus())
    };
    const bool selfdrive_resume_required = a.type.contains("resumeRequired", Qt::CaseInsensitive);
    const bool frogpilot_priority_override =
      frogpilot_alert.type.contains("greenLight", Qt::CaseInsensitive) ||
      frogpilot_alert.type.contains("leadDeparting", Qt::CaseInsensitive);

    // FrogPilot variables
    if (a.size == cereal::SelfdriveState::AlertSize::NONE || (selfdrive_resume_required && frogpilot_priority_override && frogpilot_alert.size != cereal::SelfdriveState::AlertSize::NONE)) {
      a = frogpilot_alert;
    }
  }

  if (!sm.updated("selfdriveState") && (sm.frame - started_frame) > 5 * UI_FREQ && !force_onroad) {
    const int SELFDRIVE_STATE_TIMEOUT = 5;
    const int ss_missing = (nanos_since_boot() - sm.rcv_time("selfdriveState")) / 1e9;

    // Handle selfdrive timeout
    if (selfdrive_frame < started_frame) {
      // car is started, but selfdriveState hasn't been seen at all
      a = Alert{tr("openpilot Unavailable"), tr("Waiting to start"),
                "selfdriveWaiting", cereal::SelfdriveState::AlertSize::MID,
                cereal::SelfdriveState::AlertStatus::NORMAL};
    } else if (ss_missing > SELFDRIVE_STATE_TIMEOUT && !Hardware::PC()) {
      // car is started, but selfdrive is lagging or died
      if (ss.getEnabled() && (ss_missing - SELFDRIVE_STATE_TIMEOUT) < 10) {
        a = Alert{tr("TAKE CONTROL IMMEDIATELY"), tr("System Unresponsive"),
                  "selfdriveUnresponsive", cereal::SelfdriveState::AlertSize::FULL,
                  cereal::SelfdriveState::AlertStatus::CRITICAL};
      } else {
        a = Alert{tr("System Unresponsive"), tr("Reboot Device"),
                  "selfdriveUnresponsivePermanent", cereal::SelfdriveState::AlertSize::MID,
                  cereal::SelfdriveState::AlertStatus::NORMAL};
      }
    }
  }
  return a;
}

void OnroadAlerts::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  if (alert.type.contains("steerSaturated", Qt::CaseInsensitive)) {
    alertHeight = 0;
    return;
  }

  if (alert.size == cereal::SelfdriveState::AlertSize::NONE) {
    // FrogPilot variables
    alertHeight = 0;
  } else {
    static std::map<cereal::SelfdriveState::AlertSize, const int> alert_heights = {
      {cereal::SelfdriveState::AlertSize::SMALL, 271},
      {cereal::SelfdriveState::AlertSize::MID, 420},
      {cereal::SelfdriveState::AlertSize::FULL, height()},
    };
    alertHeight = alert_heights[alert.size];
    int h = alertHeight;

    int margin = 40;
    int radius = 30;
    if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
      margin = 0;
      radius = 0;
    }
    alertHeight -= margin;
    QRect r = QRect(margin, height() - h + margin, width() - margin * 2, h - margin * 2);

    const bool is_resume_required_alert = alert.type.contains("resumeRequired", Qt::CaseInsensitive);
    const bool is_parked_standstill_alert = alert.type.contains("parkedStandstill", Qt::CaseInsensitive);
    const bool is_lead_departing_alert = alert.type.contains("leadDeparting", Qt::CaseInsensitive);
    const bool is_collision_alert = alert.type.contains("fcw", Qt::CaseInsensitive) || alert.type.contains("aeb", Qt::CaseInsensitive);
    const bool is_lane_departure_alert = alert.type.contains("ldw", Qt::CaseInsensitive);
    const bool icon_alert = is_collision_alert || is_lane_departure_alert;
    const QColor alert_color = frogpilot_alert_colors[static_cast<cereal::FrogPilotSelfdriveState::AlertStatus>(alert.status)];

    if (is_lead_departing_alert) {
      QRect full_rect = rect();
      QLinearGradient full_grad(0, full_rect.top(), 0, full_rect.bottom());
      full_grad.setColorAt(0.0, QColor(0x20, 0x22, 0x25, 188));
      full_grad.setColorAt(0.45, QColor(0x16, 0x18, 0x1C, 206));
      full_grad.setColorAt(1.0, QColor(0x10, 0x12, 0x16, 224));
      p.setPen(Qt::NoPen);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setBrush(full_grad);
      p.drawRect(full_rect);
    }

    if (is_resume_required_alert) {
      QRect full_rect = rect();
      QLinearGradient full_grad(0, full_rect.top(), 0, full_rect.bottom());
      full_grad.setColorAt(0.0, QColor(alert_color.red(), alert_color.green(), alert_color.blue(), 178));
      full_grad.setColorAt(0.45, QColor(alert_color.red(), alert_color.green(), alert_color.blue(), 194));
      full_grad.setColorAt(1.0, QColor(std::max(alert_color.red() - 18, 0), std::max(alert_color.green() - 18, 0), std::max(alert_color.blue() - 18, 0), 212));
      p.setPen(Qt::NoPen);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setBrush(full_grad);
      p.drawRect(full_rect);
    }

    if (is_parked_standstill_alert) {
      QRect full_rect = rect();
      QLinearGradient full_grad(0, full_rect.top(), 0, full_rect.bottom());
      full_grad.setColorAt(0.0, QColor(8, 10, 13, 224));
      full_grad.setColorAt(0.45, QColor(6, 8, 10, 236));
      full_grad.setColorAt(1.0, QColor(3, 4, 6, 246));
      p.setPen(Qt::NoPen);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setBrush(full_grad);
      p.drawRect(full_rect);
    }

    if (icon_alert) {
      QRect overlay_rect = rect();
      QLinearGradient overlay_grad(0, overlay_rect.top(), 0, overlay_rect.bottom());
      overlay_grad.setColorAt(0.0, QColor(0x2C, 0x33, 0x3A, 0x74));
      overlay_grad.setColorAt(0.25, QColor(0x25, 0x2B, 0x32, 0x98));
      overlay_grad.setColorAt(0.55, QColor(0x1E, 0x24, 0x2A, 0xBF));
      overlay_grad.setColorAt(1.0, QColor(0x16, 0x1A, 0x1F, 0xD8));
      p.setPen(Qt::NoPen);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
      p.setBrush(overlay_grad);
      p.drawRect(overlay_rect);
    }

    p.setPen(Qt::NoPen);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!is_resume_required_alert && !is_lead_departing_alert && !is_parked_standstill_alert) {
      p.setBrush(QBrush(alert_color));
      p.drawRoundedRect(r, radius, radius);
    }

    QLinearGradient g(0, r.y(), 0, r.bottom());
    g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
    g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

    if (!is_resume_required_alert && !is_lead_departing_alert && !is_parked_standstill_alert) {
      p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
      p.setBrush(QBrush(g));
      p.drawRoundedRect(r, radius, radius);
      p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    if (icon_alert) {
      const int icon_size = is_lane_departure_alert ? 292 : 348;
      const QString icon_path = is_lane_departure_alert ? "../../files/icons/lanecrossing.png" : "../../files/icons/collision.png";
      const QPixmap icon_img = loadPixmap(icon_path, {icon_size, icon_size});
      const int blink_interval_ms = is_lane_departure_alert ? 320 : 120;
      const bool blink_visible = ((QDateTime::currentMSecsSinceEpoch() / blink_interval_ms) % 2) == 0;

      if (!icon_img.isNull() && blink_visible) {
        const int icon_y = is_lane_departure_alert ? 188 : 164;
        QRect icon_rect((width() - icon_size) / 2, icon_y, icon_size, icon_size);
        QRect glow_rect = icon_rect.adjusted(-58, -44, 58, 52);
        QRadialGradient glow(glow_rect.center(), glow_rect.width() * 0.56);
        const QColor glow_color = is_lane_departure_alert ? QColor(0xFF, 0xC3, 0x53) : QColor(0xFF, 0x5D, 0x57);
        glow.setColorAt(0.0, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 82));
        glow.setColorAt(0.45, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 34));
        glow.setColorAt(1.0, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(glow_rect);
        p.drawPixmap(icon_rect, icon_img);
      }
    }

    if (is_lead_departing_alert) {
      static const int lead_depart_icon_size = 320;
      static const QPixmap lead_depart_img = loadPixmap("../../files/icons/lead_depart.png", {lead_depart_icon_size, lead_depart_icon_size});
      if (!lead_depart_img.isNull()) {
        QRect icon_rect((width() - lead_depart_icon_size) / 2, (height() - lead_depart_icon_size) / 2 - 70, lead_depart_icon_size, lead_depart_icon_size);
        const QPixmap scaled_img = lead_depart_img.scaled(icon_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QRect draw_rect(icon_rect.left() + (icon_rect.width() - scaled_img.width()) / 2,
                        icon_rect.top() + (icon_rect.height() - scaled_img.height()) / 2,
                        scaled_img.width(), scaled_img.height());
        p.drawPixmap(draw_rect, scaled_img);

        p.setPen(QColor(0xff, 0xff, 0xff));
        p.setFont(InterFont(82, QFont::Bold));
        QRect text_rect(0, icon_rect.bottom() + 52, width(), 110);
        p.drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop, alert.text2);
      }
    }

    const QPoint c = r.center();
    p.setPen(QColor(0xff, 0xff, 0xff));
    if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
      bool long_alert1 = alert.text1.length() > 40;
      p.setFont(InterFont(long_alert1 && sidebarsOpen ? 64 : 74, QFont::DemiBold));
      p.drawText(r, Qt::AlignCenter, alert.text1);
    } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
      if (is_resume_required_alert) {
        static const int autohold_icon_size = 184;
        static const QPixmap autohold_img = loadPixmap("../../files/icons/autohold.png", {autohold_icon_size, autohold_icon_size});
        const int elapsed_seconds = resumeRequiredTimer.isValid() ? resumeRequiredTimer.elapsed() / 1000 : 0;
        const QString elapsed_text = QString("%1:%2").arg(elapsed_seconds / 60).arg(elapsed_seconds % 60, 2, 10, QChar('0'));
        const QPoint screen_center = rect().center();

        const int group_gap = 28;
        const int timer_width = 260;
        const int group_width = autohold_icon_size + group_gap + timer_width;
        const int group_left = (width() - group_width) / 2;
        const int group_top = screen_center.y() - 150;
        QRect icon_rect(group_left, group_top, autohold_icon_size, autohold_icon_size);
        QRect timer_rect(icon_rect.right() + group_gap, group_top + 10, timer_width, autohold_icon_size - 20);
        QRect description_rect(0, group_top + autohold_icon_size + 26, width(), 90);

        if (!autohold_img.isNull()) {
          const QPixmap scaled_img = autohold_img.scaled(icon_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
          QRect draw_rect(icon_rect.left(), icon_rect.top() + (icon_rect.height() - scaled_img.height()) / 2, scaled_img.width(), scaled_img.height());
          p.drawPixmap(draw_rect, scaled_img);
        }

        p.setFont(InterFont(112, QFont::Bold));
        p.drawText(timer_rect, Qt::AlignCenter, elapsed_text);
        bool long_alert2 = alert.text2.length() > 40;
        p.setFont(InterFont(long_alert2 && sidebarsOpen ? 56 : 66));
        p.drawText(description_rect, Qt::AlignHCenter | Qt::AlignTop, alert.text2);
      } else if (is_parked_standstill_alert) {
        static const int parking_icon_size = 172;
        static const QPixmap parking_img = loadPixmap("../../files/icons/parking.png", {parking_icon_size, parking_icon_size});
        const int elapsed_seconds = parkedStandstillTimer.isValid() ? parkedStandstillTimer.elapsed() / 1000 : 0;
        const QString elapsed_text = QString("%1:%2").arg(elapsed_seconds / 60).arg(elapsed_seconds % 60, 2, 10, QChar('0'));
        const int title_gap = 28;
        const int title_width = 340;
        const int title_group_width = parking_icon_size + title_gap + title_width;
        const int title_left = (width() - title_group_width) / 2;
        const int title_top = rect().center().y() - 168;
        QRect icon_rect(title_left, title_top, parking_icon_size, parking_icon_size);
        QRect title_rect(icon_rect.right() + title_gap, title_top + 10, title_width, parking_icon_size - 20);
        QRect timer_rect(0, title_top + parking_icon_size + 34, width(), 132);

        if (!parking_img.isNull()) {
          const QPixmap scaled_img = parking_img.scaled(icon_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
          QRect draw_rect(icon_rect.left(), icon_rect.top() + (icon_rect.height() - scaled_img.height()) / 2, scaled_img.width(), scaled_img.height());
          p.drawPixmap(draw_rect, scaled_img);
        }

        p.setPen(QColor(0xff, 0xff, 0xff));
        p.setFont(InterFont(92, QFont::Bold));
        p.drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter, alert.text1);

        p.setFont(InterFont(112, QFont::Bold));
        p.drawText(timer_rect, Qt::AlignHCenter | Qt::AlignTop, elapsed_text);
      } else {
        bool long_alert1 = alert.text1.length() > 30;
        p.setFont(InterFont(long_alert1 && sidebarsOpen ? 78 : 88, QFont::Bold));
        p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, alert.text1);
        bool long_alert2 = alert.text2.length() > 40;
        p.setFont(InterFont(long_alert2 && sidebarsOpen ? 56 : 66));
        p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, alert.text2);
      }
    } else if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
      if (!is_lead_departing_alert) {
        bool l = alert.text1.length() > 15;
        p.setFont(InterFont(l ? 132 : 177, QFont::Bold));
        p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);
        p.setFont(InterFont(88));
        p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
      }
    }
  }

}
