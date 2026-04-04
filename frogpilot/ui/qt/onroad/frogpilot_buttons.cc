#include "frogpilot/ui/qt/onroad/frogpilot_buttons.h"

#include <QDateTime>
#include <QPainter>

#include "common/util.h"

DrivingPersonalityButton::DrivingPersonalityButton(QWidget *parent) : QPushButton(parent) {
  setFixedSize(btn_size + UI_BORDER_SIZE, btn_size);

  QObject::connect(frogpilotUIState(), &FrogPilotUIState::themeUpdated, this, &DrivingPersonalityButton::updateTheme);
  QObject::connect(this, &QPushButton::pressed, [this] {params_memory.putBool("OnroadDistanceButtonPressed", true);});
  QObject::connect(this, &QPushButton::released, [this] {params_memory.putBool("OnroadDistanceButtonPressed", false);});
}

void DrivingPersonalityButton::showEvent(QShowEvent *event) {
  updateTheme();
}

void DrivingPersonalityButton::updateTheme() {
  for (QMap<int, QPair<QPixmap, QSharedPointer<QMovie>>>::iterator it = icon_map.begin(); it != icon_map.end(); ++it) {
    QSharedPointer<QMovie> &movie = it.value().second;
    if (!movie.isNull()) {
      QObject::disconnect(movie.data(), nullptr, this, nullptr);
      movie->stop();
    }
  }

  icon_map.clear();

  QPixmap traffic_img, aggressive_img, standard_img, relaxed_img;
  QSharedPointer<QMovie> traffic_gif, aggressive_gif, standard_gif, relaxed_gif;

  loadImage("../../frogpilot/assets/active_theme/distance_icons/traffic", traffic_img, traffic_gif, QSize(btn_size, btn_size), this);
  loadImage("../../frogpilot/assets/active_theme/distance_icons/aggressive", aggressive_img, aggressive_gif, QSize(btn_size, btn_size), this);
  loadImage("../../frogpilot/assets/active_theme/distance_icons/standard", standard_img, standard_gif, QSize(btn_size, btn_size), this);
  loadImage("../../frogpilot/assets/active_theme/distance_icons/relaxed", relaxed_img, relaxed_gif, QSize(btn_size, btn_size), this);

  icon_map.insert(0, qMakePair(traffic_img, traffic_gif));
  icon_map.insert(1, qMakePair(aggressive_img, aggressive_gif));
  icon_map.insert(2, qMakePair(standard_img, standard_gif));
  icon_map.insert(3, qMakePair(relaxed_img, relaxed_gif));

  theme_updated = true;
}

void DrivingPersonalityButton::updateState(const UIState &s, const FrogPilotUIState &fs) {
  const UIScene &scene = s.scene;

  const SubMaster &fpsm = *(fs.sm);

  const cereal::FrogPilotCarState::Reader &frogpilotCarState = fpsm["frogpilotCarState"].getFrogpilotCarState();

  bool new_traffic_mode_active = frogpilotCarState.getTrafficModeEnabled();
  bool new_apn_connected = false;
  if (util::read_file(params_memory.getParamPath("APNDataActive")) == "1") {
    const QString apn_timestamp_raw = QString::fromStdString(util::read_file(params_memory.getParamPath("APNDataTimestamp"))).trimmed();
    const double apn_timestamp = apn_timestamp_raw.toDouble();
    const double now_secs = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    new_apn_connected = apn_timestamp > 0.0 && (now_secs - apn_timestamp) < 10.0;
  }
  if (!new_apn_connected && util::getenv("OPENPILOT_PREFIX", "") == "routedemo") {
    new_apn_connected = true;
  }

  int new_personality = static_cast<int>(scene.personality) + 1;

  bool state_changed = (traffic_mode_active != new_traffic_mode_active) ||
                       (personality != new_personality && !new_traffic_mode_active) ||
                       (apn_connected != new_apn_connected);

  if (!state_changed && !theme_updated) {
    return;
  }

  traffic_mode_active = new_traffic_mode_active;
  apn_connected = new_apn_connected;

  personality = new_personality;

  theme_updated = false;

  QPair<QPixmap, QSharedPointer<QMovie>> icon = icon_map.value(traffic_mode_active ? 0 : personality);
  currentImg = icon.first;
  currentGif = icon.second.data();
}

void DrivingPersonalityButton::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  drawIcon(p, rect().center() + QPoint(UI_BORDER_SIZE / 2, 0), currentGif ? currentGif->currentPixmap() : currentImg, Qt::transparent, 1.0);

  if (apn_connected) {
    const QRect badge_rect((width() - 92) / 2, 8, 92, 34);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(28, 176, 94, 235));
    p.drawRoundedRect(badge_rect, 16, 16);

    p.setPen(Qt::white);
    p.setFont(InterFont(20, QFont::DemiBold));
    p.drawText(badge_rect, Qt::AlignCenter, "APN");
  }
}
