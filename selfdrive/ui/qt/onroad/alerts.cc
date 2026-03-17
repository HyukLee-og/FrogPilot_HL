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

QColor alertAccentColor(cereal::SelfdriveState::AlertStatus status) {
  switch (status) {
    case cereal::SelfdriveState::AlertStatus::CRITICAL:
      return QColor(0xFF, 0x5B, 0x6B);
    case cereal::SelfdriveState::AlertStatus::USER_PROMPT:
      return QColor(0xFF, 0xB0, 0x48);
    case cereal::SelfdriveState::AlertStatus::NORMAL:
    default:
      return QColor(0x6F, 0xD3, 0xFF);
  }
}

QString alertBadgeLabel(const QString &type, cereal::SelfdriveState::AlertStatus status) {
  const QString alert_type = type.toLower();
  if (alert_type.contains("crash")) return QObject::tr("SYSTEM");

  switch (status) {
    case cereal::SelfdriveState::AlertStatus::CRITICAL:
      return QObject::tr("TAKE OVER");
    case cereal::SelfdriveState::AlertStatus::USER_PROMPT:
      return QObject::tr("ATTENTION");
    case cereal::SelfdriveState::AlertStatus::NORMAL:
    default:
      return QObject::tr("NOTICE");
  }
}

void drawAlertBadge(QPainter &p, const QString &label, const QColor &accent, const QRect &card_rect, int top) {
  QFont badge_font = InterFont(24, QFont::DemiBold);
  badge_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  const int badge_height = 42;
  const int badge_width = std::max(154, QFontMetrics(badge_font).horizontalAdvance(label) + 40);
  QRect badge_rect(card_rect.x() + 34, top, badge_width, badge_height);

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 42));
  p.drawRoundedRect(badge_rect, 16, 16);

  p.setFont(badge_font);
  p.setPen(accent);
  p.drawText(badge_rect, Qt::AlignCenter, label);
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
  Params params;
  const bool force_onroad = params.getBool("ForceOnroad") || frogpilot_toggles.value("force_onroad").toBool();

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
      {cereal::SelfdriveState::AlertSize::SMALL, 220},
      {cereal::SelfdriveState::AlertSize::MID, 290},
      {cereal::SelfdriveState::AlertSize::FULL, std::min(height() - 52, 560)},
    };
    alertHeight = alert_heights[alert.size];
    const int margin = 36;
    const int bottom_margin = 34;
    const int radius = 30;
    QRect r(margin, height() - alertHeight - bottom_margin, width() - margin * 2, alertHeight);
    alertHeight = r.height();
    const QColor accent = alertAccentColor(alert.status);

    p.setPen(Qt::NoPen);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setBrush(QColor(0x08, 0x0C, 0x12, 0xEA));
    p.drawRoundedRect(r.translated(0, 14), radius, radius);

    QLinearGradient card_gradient(r.topLeft(), r.bottomLeft());
    card_gradient.setColorAt(0.0, QColor(0x15, 0x1A, 0x21, 0xF5));
    card_gradient.setColorAt(1.0, QColor(0x0C, 0x10, 0x16, 0xF1));
    p.setBrush(card_gradient);
    p.drawRoundedRect(r, radius, radius);

    p.setPen(QPen(QColor(0xFF, 0xFF, 0xFF, 0x24), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius, radius);

    QRect accent_rect(r.x() + 28, r.y() + 26, r.width() - 56, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(accent_rect, 4, 4);

    drawAlertBadge(p, alertBadgeLabel(alert.type, alert.status), accent, r, r.y() + 54);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    p.setPen(QColor(0xF8, 0xFA, 0xFC));
    if (alert.size == cereal::SelfdriveState::AlertSize::SMALL) {
      p.setFont(InterFont(sidebarsOpen ? 60 : 68, QFont::Bold));
      QRect text_rect = r.adjusted(32, 108, -32, -30);
      p.drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, alert.text1);
    } else if (alert.size == cereal::SelfdriveState::AlertSize::MID) {
      const int title_font_size = sidebarsOpen ? 60 : 70;
      const int body_font_size = sidebarsOpen ? 38 : 44;
      p.setFont(InterFont(title_font_size, QFont::Bold));
      QRect title_rect = r.adjusted(34, 112, -34, -108);
      p.drawText(title_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, alert.text1);

      p.setFont(InterFont(body_font_size));
      p.setPen(QColor(0xB8, 0xC1, 0xCC));
      QRect body_rect = r.adjusted(36, 194, -36, -34);
      p.drawText(body_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, alert.text2);
    } else if (alert.size == cereal::SelfdriveState::AlertSize::FULL) {
      const bool long_title = alert.text1.length() > 18;
      p.setFont(InterFont(long_title ? 96 : 112, QFont::Bold));
      QRect title_rect = r.adjusted(40, 126, -40, -170);
      p.drawText(title_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, alert.text1);

      p.setFont(InterFont(54));
      p.setPen(QColor(0xCB, 0xD3, 0xDD));
      QRect body_rect = r.adjusted(42, r.height() - 156, -42, -42);
      p.drawText(body_rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, alert.text2);
    }
  }

  if (selfdriveEngageable) {
    const QColor chip_bg = selfdriveEnabled ? QColor(0x36, 0xC2, 0x75, 0xF4)
                                            : QColor(0xF2, 0xF5, 0xF8, 0xF2);
    drawAlertChip(p, "LFA", chip_bg, width());
  }
}
