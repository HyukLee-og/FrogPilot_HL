#include "selfdrive/ui/qt/onroad/alerts.h"

#include <QPainter>
#include <map>

#include "selfdrive/ui/qt/util.h"

namespace {
void drawAlertChip(QPainter &p, const QString &label, const QColor &bg_color, int width) {
  if (label.isEmpty()) return;

  const int chip_height = 52;
  const int chip_y = 330;
  const QFont chip_font = InterFont(28, QFont::DemiBold);
  const int chip_width = std::max(188, QFontMetrics(chip_font).horizontalAdvance(label) + 52);
  QRect chip_rect((width - chip_width) / 2, chip_y, chip_width, chip_height);

  p.setPen(Qt::NoPen);
  p.setBrush(bg_color);
  p.drawRoundedRect(chip_rect, 18, 18);

  p.setFont(chip_font);
  p.setPen(QColor(0x08, 0x0C, 0x12));
  p.drawText(chip_rect, Qt::AlignCenter, label);
}
}  // namespace

void OnroadAlerts::updateState(const UIState &s, const FrogPilotUIState &fs) {
  Alert a = getAlert(*(s.sm), *(fs.sm), s.scene.started_frame);
  const auto selfdrive_state = (*s.sm)["selfdriveState"].getSelfdriveState();
  bool selfdrive_enabled = selfdrive_state.getEnabled();
  bool selfdrive_engageable = selfdrive_state.getEngageable() || selfdrive_enabled;

  if (!alert.equal(a) || selfdriveEnabled != selfdrive_enabled || selfdriveEngageable != selfdrive_engageable) {
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
}

OnroadAlerts::Alert OnroadAlerts::getAlert(const SubMaster &sm, const SubMaster &fpsm, uint64_t started_frame) {
  const cereal::SelfdriveState::Reader &ss = sm["selfdriveState"].getSelfdriveState();
  const uint64_t selfdrive_frame = sm.rcv_frame("selfdriveState");
  const bool force_onroad = Params().getBool("ForceOnroad") || frogpilot_toggles.value("force_onroad").toBool();

  // FrogPilot variables
  const cereal::FrogPilotSelfdriveState::Reader &fpss = fpsm["frogpilotSelfdriveState"].getFrogpilotSelfdriveState();

  Alert a = {};
  static QString crash_log_path = "/data/error_logs/error.txt";
  if (QFile::exists(crash_log_path)) {
    if (frogpilot_toggles.value("random_events").toBool()) {
      a = {tr("openpilot crashed 💩"),
           tr("Please post the \"Error Log\" in the FrogPilot Discord!"),
           "openpilotCrashedRandomEvent",
           cereal::SelfdriveState::AlertSize::MID,
           cereal::SelfdriveState::AlertStatus::CRITICAL};
    } else {
      a = {tr("openpilot crashed"),
           tr("Please post the \"Error Log\" in the FrogPilot Discord!"),
           "openpilotCrashed",
           cereal::SelfdriveState::AlertSize::MID,
           cereal::SelfdriveState::AlertStatus::CRITICAL};
    }
    return a;
  } else if (selfdrive_frame >= started_frame) {  // Don't get old alert.
    a = {ss.getAlertText1().cStr(), ss.getAlertText2().cStr(),
         ss.getAlertType().cStr(), ss.getAlertSize(), ss.getAlertStatus()};

    // FrogPilot variables
    if (a.size == cereal::SelfdriveState::AlertSize::NONE) {
      a = {fpss.getAlertText1().cStr(), fpss.getAlertText2().cStr(),
           fpss.getAlertType().cStr(), static_cast<cereal::SelfdriveState::AlertSize>(fpss.getAlertSize()), static_cast<cereal::SelfdriveState::AlertStatus>(fpss.getAlertStatus())};
    }
  }

  if (!sm.updated("selfdriveState") && (sm.frame - started_frame) > 5 * UI_FREQ && !force_onroad) {
    const int SELFDRIVE_STATE_TIMEOUT = 5;
    const int ss_missing = (nanos_since_boot() - sm.rcv_time("selfdriveState")) / 1e9;

    // Handle selfdrive timeout
    if (selfdrive_frame < started_frame) {
      // car is started, but selfdriveState hasn't been seen at all
      a = {tr("openpilot Unavailable"), tr("Waiting to start"),
           "selfdriveWaiting", cereal::SelfdriveState::AlertSize::MID,
           cereal::SelfdriveState::AlertStatus::NORMAL};
    } else if (ss_missing > SELFDRIVE_STATE_TIMEOUT && !Hardware::PC()) {
      // car is started, but selfdrive is lagging or died
      if (ss.getEnabled() && (ss_missing - SELFDRIVE_STATE_TIMEOUT) < 10) {
        a = {tr("TAKE CONTROL IMMEDIATELY"), tr("System Unresponsive"),
             "selfdriveUnresponsive", cereal::SelfdriveState::AlertSize::FULL,
             cereal::SelfdriveState::AlertStatus::CRITICAL};
      } else {
        a = {tr("System Unresponsive"), tr("Reboot Device"),
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

    p.setPen(Qt::NoPen);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setBrush(QBrush(frogpilot_alert_colors[static_cast<cereal::FrogPilotSelfdriveState::AlertStatus>(alert.status)]));
    p.drawRoundedRect(r, radius, radius);

    QLinearGradient g(0, r.y(), 0, r.bottom());
    g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
    g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

    p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
    p.setBrush(QBrush(g));
    p.drawRoundedRect(r, radius, radius);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QPoint c = r.center();
    p.setPen(QColor(0xff, 0xff, 0xff));
    if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
      bool long_alert1 = alert.text1.length() > 40;
      p.setFont(InterFont(long_alert1 && sidebarsOpen ? 64 : 74, QFont::DemiBold));
      p.drawText(r, Qt::AlignCenter, alert.text1);
    } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
      bool long_alert1 = alert.text1.length() > 30;
      p.setFont(InterFont(long_alert1 && sidebarsOpen ? 78 : 88, QFont::Bold));
      p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, alert.text1);
      bool long_alert2 = alert.text2.length() > 40;
      p.setFont(InterFont(long_alert2 && sidebarsOpen ? 56 : 66));
      p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, alert.text2);
    } else if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
      bool l = alert.text1.length() > 15;
      p.setFont(InterFont(l ? 132 : 177, QFont::Bold));
      p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);
      p.setFont(InterFont(88));
      p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
    }
  }

  if (selfdriveEngageable) {
    const QColor chip_bg = selfdriveEnabled ? QColor(0x36, 0xC2, 0x75, 0xF4)
                                            : QColor(0xF2, 0xF5, 0xF8, 0xF2);
    drawAlertChip(p, "LFA", chip_bg, width());
  }
}
