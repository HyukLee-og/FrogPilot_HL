#include "selfdrive/ui/qt/onroad/buttons.h"

#include <QPainter>

#include "selfdrive/ui/qt/util.h"

namespace {
QPixmap tintedPixmap(const QPixmap &img, const QColor &tint) {
  if (!tint.isValid() || img.isNull()) return img;

  QPixmap tinted(img.size());
  tinted.fill(Qt::transparent);

  QPainter painter(&tinted);
  painter.drawPixmap(0, 0, img);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(tinted.rect(), tint);
  painter.end();

  return tinted;
}
}  // namespace

void drawIcon(QPainter &p, const QPoint &center, const QPixmap &img, const QBrush &bg, float opacity, const int &angle,
              bool draw_bg, const QColor &tint) {
  p.setRenderHint(QPainter::Antialiasing);
  if (draw_bg) {
    p.setOpacity(1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawEllipse(center, btn_size / 2, btn_size / 2);
  }
  p.save();
  p.translate(center);
  p.rotate(angle);
  p.setOpacity(opacity);
  QPixmap pixmap = tintedPixmap(img, tint);
  p.drawPixmap(-QPoint(pixmap.width() / 2, pixmap.height() / 2), pixmap);
  p.restore();
  p.setOpacity(1.0);
}

// ExperimentalButton
ExperimentalButton::ExperimentalButton(QWidget *parent) : experimental_mode(false), engageable(false), QPushButton(parent) {
  setFixedSize(btn_size, btn_size);

  engage_img = loadPixmap("../assets/icons/chffr_wheel.png", {img_size, img_size});
  experimental_img = loadPixmap("../assets/icons/experimental.svg", {img_size, img_size});
  QObject::connect(this, &QPushButton::clicked, this, &ExperimentalButton::changeMode);

  // FrogPilot variables
  QObject::connect(frogpilotUIState(), &FrogPilotUIState::themeUpdated, this, &ExperimentalButton::updateTheme);
}

void ExperimentalButton::changeMode() {
  const auto cp = (*uiState()->sm)["carParams"].getCarParams();
  bool can_change = hasLongitudinalControl(cp) && params.getBool("ExperimentalModeConfirmed");
  if (can_change) {
    // FrogPilot variables
    if (frogpilot_toggles.value("conditional_experimental_mode").toBool()) {
      int override_value = (frogpilot_scene.conditional_status == 1 || frogpilot_scene.conditional_status == 2) ? 0 : experimental_mode ? 1 : 2;
      params_memory.putInt("CEStatus", override_value);
    } else {
      params.putBool("ExperimentalMode", !experimental_mode);
    }
  }
}

void ExperimentalButton::updateState(const UIState &s, const FrogPilotUIState &fs) {
  const SubMaster &sm = *(s.sm);
  const bool force_preview = params.getBool("ForceOnroad") && sm.rcv_frame("carState") < s.scene.started_frame;
  const auto cs = (*s.sm)["selfdriveState"].getSelfdriveState();
  bool eng = force_preview || cs.getEngageable() || cs.getEnabled() || fs.frogpilot_scene.always_on_lateral_active;
  bool is_enabled = force_preview || cs.getEnabled();
  if ((cs.getExperimentalMode() != experimental_mode) || (eng != engageable) || (is_enabled != enabled)) {
    engageable = eng;
    enabled = is_enabled;
    experimental_mode = cs.getExperimentalMode();
    update();
  }

  // FrogPilot variables
  const cereal::CarState::Reader &carState = (*s.sm)["carState"].getCarState();

  updateBackgroundColor();

  int current_steering_angle_deg = -carState.getSteeringAngleDeg();
  if (current_steering_angle_deg != steering_angle_deg && frogpilot_toggles.value("rotating_wheel").toBool()) {
    steering_angle_deg = current_steering_angle_deg;
    update();
  } else if (!frogpilot_toggles.value("rotating_wheel").toBool()) {
    steering_angle_deg = 0;
  }

  if (params_memory.getBool("UpdateWheelImage")) {
    updateTheme();
    params_memory.remove("UpdateWheelImage");
  }
}

void ExperimentalButton::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setClipRegion(QRegion(QRect(0, 0, btn_size, btn_size), QRegion::Ellipse));
  p.setRenderHint(QPainter::Antialiasing);

  QColor tint;
  if (frogpilot_toggles.value("wheel_image").toString() == "stock") {
    tint = enabled ? QColor(0x49, 0xD2, 0x83) : QColor(0xE9, 0xEF, 0xF5);
  }

  if (frogpilot_toggles.value("wheel_image").toString() == "stock") {
    QPixmap img = experimental_mode ? experimental_img : engage_img;
    drawIcon(p, QPoint(btn_size / 2, btn_size / 2), img, background_color, (isDown() || !engageable) ? 0.6 : 1.0, steering_angle_deg, false, tint);
  } else if (wheel_gif) {
    drawIcon(p, QPoint(btn_size / 2, btn_size / 2), wheel_gif->currentPixmap(), background_color, (isDown() || !engageable) ? 0.6 : 1.0, steering_angle_deg, false);
  } else if (!wheel_img.isNull()) {
    drawIcon(p, QPoint(btn_size / 2, btn_size / 2), wheel_img, background_color, (isDown() || !engageable) ? 0.6 : 1.0, steering_angle_deg, false);
  }
}

// FrogPilot variables
void ExperimentalButton::showEvent(QShowEvent *event) {
  updateTheme();
}

void ExperimentalButton::updateBackgroundColor() {
  if (isDown() || !engageable) {
    background_color = QColor(0, 0, 0, 166);
  } else if (frogpilot_scene.always_on_lateral_active) {
    background_color = bg_colors[STATUS_ALWAYS_ON_LATERAL_ACTIVE];
  } else if (frogpilot_scene.conditional_status == 1) {
    background_color = bg_colors[STATUS_CEM_DISABLED];
  } else if (experimental_mode) {
    background_color = bg_colors[STATUS_EXPERIMENTAL_MODE_ENABLED];
  } else if (frogpilot_scene.traffic_mode_enabled) {
    background_color = bg_colors[STATUS_TRAFFIC_MODE_ENABLED];
  } else {
    background_color = QColor(0, 0, 0, 166);
  }
}

void ExperimentalButton::updateTheme() {
  loadImage("../../frogpilot/assets/active_theme/steering_wheel/wheel", wheel_img, wheel_gif, QSize(img_size, img_size), this);
}
