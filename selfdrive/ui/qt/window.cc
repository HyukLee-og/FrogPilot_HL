#include "selfdrive/ui/qt/window.h"

#include <QFontDatabase>
#include <QPainter>
#include <QResizeEvent>

#include "system/hardware/hw.h"

namespace {
constexpr int kScreenCornerRadius = 44;

enum class CornerPosition {
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
};
}

class RoundFrameOverlay : public QWidget {
public:
  explicit RoundFrameOverlay(CornerPosition position, QWidget *parent = nullptr) : QWidget(parent) {
    Q_UNUSED(position);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);
  }
};

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
  main_layout = new QStackedLayout(this);
  main_layout->setMargin(0);

  homeWindow = new HomeWindow(this);
  main_layout->addWidget(homeWindow);
  QObject::connect(homeWindow, &HomeWindow::openSettings, this, &MainWindow::openSettings);
  QObject::connect(homeWindow, &HomeWindow::closeSettings, this, &MainWindow::closeSettings);

  settingsWindow = new SettingsWindow(this);
  main_layout->addWidget(settingsWindow);
  QObject::connect(settingsWindow, &SettingsWindow::closeSettings, this, &MainWindow::closeSettings);
  QObject::connect(settingsWindow, &SettingsWindow::reviewTrainingGuide, [=]() {
    onboardingWindow->showTrainingGuide();
    main_layout->setCurrentWidget(onboardingWindow);
  });
  QObject::connect(settingsWindow, &SettingsWindow::showDriverView, [=] {
    homeWindow->showDriverView(true);
  });

  onboardingWindow = new OnboardingWindow(this);
  main_layout->addWidget(onboardingWindow);
  QObject::connect(onboardingWindow, &OnboardingWindow::onboardingDone, [=]() {
    main_layout->setCurrentWidget(homeWindow);
  });
  if (!onboardingWindow->completed()) {
    main_layout->setCurrentWidget(onboardingWindow);
  }

  QObject::connect(uiState(), &UIState::offroadTransition, [=](bool offroad) {
    if (!offroad) {
      closeSettings();
    }
  });
  QObject::connect(device(), &Device::interactiveTimeout, [=]() {
    if (main_layout->currentWidget() == settingsWindow) {
      closeSettings();
    }
  });

  // load fonts
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-Black.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-Bold.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-ExtraBold.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-ExtraLight.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-Medium.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-Regular.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-SemiBold.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/Inter-Thin.ttf");
  QFontDatabase::addApplicationFont("../assets/fonts/JetBrainsMono-Medium.ttf");

  // no outline to prevent the focus rectangle
  setStyleSheet(R"(
    * {
      font-family: Inter;
      outline: none;
    }
  )");
  setAttribute(Qt::WA_NoSystemBackground);

  roundFrameOverlays[0] = new RoundFrameOverlay(CornerPosition::TopLeft, this);
  roundFrameOverlays[1] = new RoundFrameOverlay(CornerPosition::TopRight, this);
  roundFrameOverlays[2] = new RoundFrameOverlay(CornerPosition::BottomLeft, this);
  roundFrameOverlays[3] = new RoundFrameOverlay(CornerPosition::BottomRight, this);
  for (auto *overlay : roundFrameOverlays) {
    overlay->show();
    overlay->raise();
  }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);

  const int r = kScreenCornerRadius;

  roundFrameOverlays[0]->setGeometry(0, 0, r, r);
  roundFrameOverlays[0]->setMask(QRegion(QRect(0, 0, r, r)).subtracted(QRegion(QRect(0, 0, r * 2, r * 2), QRegion::Ellipse)));

  roundFrameOverlays[1]->setGeometry(width() - r, 0, r, r);
  roundFrameOverlays[1]->setMask(QRegion(QRect(0, 0, r, r)).subtracted(QRegion(QRect(-r, 0, r * 2, r * 2), QRegion::Ellipse)));

  roundFrameOverlays[2]->setGeometry(0, height() - r, r, r);
  roundFrameOverlays[2]->setMask(QRegion(QRect(0, 0, r, r)).subtracted(QRegion(QRect(0, -r, r * 2, r * 2), QRegion::Ellipse)));

  roundFrameOverlays[3]->setGeometry(width() - r, height() - r, r, r);
  roundFrameOverlays[3]->setMask(QRegion(QRect(0, 0, r, r)).subtracted(QRegion(QRect(-r, -r, r * 2, r * 2), QRegion::Ellipse)));

  for (auto *overlay : roundFrameOverlays) {
    overlay->raise();
  }
}

void MainWindow::openSettings(int index, const QString &param) {
  main_layout->setCurrentWidget(settingsWindow);
  settingsWindow->setCurrentPanel(index, param);
  for (auto *overlay : roundFrameOverlays) {
    overlay->raise();
  }
}

void MainWindow::closeSettings() {
  main_layout->setCurrentWidget(homeWindow);
  for (auto *overlay : roundFrameOverlays) {
    overlay->raise();
  }

  if (uiState()->scene.started) {
    homeWindow->showSidebar(params.getBool("SidebarOpen") || frogpilotUIState()->frogpilot_scene.frogpilot_toggles.value("debug_mode").toBool());
  }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  // FrogPilot variables
  FrogPilotUIState &fs = *frogpilotUIState();
  FrogPilotUIScene &frogpilot_scene = fs.frogpilot_scene;
  QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  bool ignore = false;
  switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove: {
      // UTM/PC preview should stay fully interactive even when the device wake logic is inactive.
      if (Hardware::PC()) {
        ignore = false;
      } else {
        // ignore events when device is awakened by resetInteractiveTimeout
        ignore = !device()->isAwake() || frogpilot_scene.driver_camera_timer >= UI_FREQ / 2;
      }
      device()->resetInteractiveTimeout(frogpilot_toggles.value("screen_timeout").toInt(), frogpilot_toggles.value("screen_timeout_onroad").toInt());
      break;
    }
    default:
      break;
  }
  return ignore;
}
