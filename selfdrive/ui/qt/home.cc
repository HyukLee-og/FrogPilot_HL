#include "selfdrive/ui/qt/home.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "selfdrive/ui/qt/offroad/experimental_mode.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/widgets/prime.h"

#include "frogpilot/ui/qt/widgets/drive_stats.h"
#include "frogpilot/ui/qt/widgets/drive_summary.h"

// HomeWindow: the container for the offroad and onroad UIs

HomeWindow::HomeWindow(QWidget* parent) : QWidget(parent) {
  QHBoxLayout *main_layout = new QHBoxLayout(this);
  main_layout->setMargin(0);
  main_layout->setSpacing(0);

  sidebar = new Sidebar(this);
  main_layout->addWidget(sidebar);
  sidebar->setVisible(false);
  QObject::connect(sidebar, &Sidebar::openSettings, this, &HomeWindow::openSettings);

  slayout = new QStackedLayout();
  main_layout->addLayout(slayout);

  home = new OffroadHome(this);
  QObject::connect(home, &OffroadHome::openSettings, this, &HomeWindow::openSettings);
  slayout->addWidget(home);

  onroad = new OnroadWindow(this);
  slayout->addWidget(onroad);

  body = new BodyWindow(this);
  slayout->addWidget(body);

  driver_view = new DriverViewWindow(this);
  connect(driver_view, &DriverViewWindow::done, [=] {
    showDriverView(false);
  });
  slayout->addWidget(driver_view);
  setAttribute(Qt::WA_NoSystemBackground);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &HomeWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &HomeWindow::offroadTransition);
  QObject::connect(uiState(), &UIState::offroadTransition, sidebar, &Sidebar::offroadTransition);

  // FrogPilot variables
  developer_sidebar = new DeveloperSidebar(this);
  main_layout->addWidget(developer_sidebar);
  developer_sidebar->setVisible(false);
}

void HomeWindow::showSidebar(bool show) {
  sidebar->setVisible(show && uiState()->scene.started);
}

void HomeWindow::updateState(const UIState &s, const FrogPilotUIState &fs) {
  const SubMaster &sm = *(s.sm);

  // switch to the generic robot UI
  if (onroad->isVisible() && !body->isEnabled() && sm["carParams"].getCarParams().getNotCar()) {
    body->setEnabled(true);
    slayout->setCurrentWidget(body);
  }

  // FrogPilot variables
  const FrogPilotUIScene &frogpilot_scene = fs.frogpilot_scene;
  const QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  if (s.scene.started) {
    if (frogpilot_scene.driver_camera_timer >= UI_FREQ / 2) {
      showDriverView(true, true);
    } else {
      if (driver_view->isVisible()) {
        sidebar->setVisible(params.getBool("Sidebar") || frogpilot_toggles.value("debug_mode").toBool());
        slayout->setCurrentWidget(onroad);
      }

      developer_sidebar->setVisible(frogpilot_toggles.value("developer_sidebar").toBool());

      frogpilotUIState()->frogpilot_scene.sidebars_open = developer_sidebar->isVisible() && sidebar->isVisible();
    }
  }
}

void HomeWindow::offroadTransition(bool offroad) {
  // FrogPilot variables
  FrogPilotUIState &fs = *frogpilotUIState();
  FrogPilotUIScene &frogpilot_scene = fs.frogpilot_scene;
  QJsonObject &frogpilot_toggles = frogpilot_scene.frogpilot_toggles;

  body->setEnabled(false);
  sidebar->setVisible(!offroad && (params.getBool("SidebarOpen") || frogpilot_toggles.value("debug_mode").toBool()));
  if (offroad) {
    slayout->setCurrentWidget(home);

    // FrogPilot variables
    developer_sidebar->setVisible(false);
  } else {
    slayout->setCurrentWidget(onroad);
  }
}

void HomeWindow::showDriverView(bool show, bool started) {
  if (show) {
    if (!started) {
      emit closeSettings();
    }
    slayout->setCurrentWidget(driver_view);
  } else {
    slayout->setCurrentWidget(home);
  }
  sidebar->setVisible(!show && uiState()->scene.started);

  // FrogPilot variables
  developer_sidebar->setVisible(false);
}

void HomeWindow::mousePressEvent(QMouseEvent* e) {
  // Handle sidebar collapsing
  if ((onroad->isVisible() || body->isVisible()) && (!sidebar->isVisible() || e->x() > sidebar->width())) {
    sidebar->setVisible(!sidebar->isVisible());

    // FrogPilot variables
    params.putBool("SidebarOpen", sidebar->isVisible());
  }
}

void HomeWindow::mouseDoubleClickEvent(QMouseEvent* e) {
  HomeWindow::mousePressEvent(e);
  const SubMaster &sm = *(uiState()->sm);
  if (sm["carParams"].getCarParams().getNotCar()) {
    if (onroad->isVisible()) {
      slayout->setCurrentWidget(body);
    } else if (body->isVisible()) {
      slayout->setCurrentWidget(onroad);
    }
    showSidebar(false);
  }
}

// OffroadHome: the offroad home page

OffroadHome::OffroadHome(QWidget* parent) : QFrame(parent) {
  QVBoxLayout* main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(48, 40, 48, 48);

  QHBoxLayout *top_layout = new QHBoxLayout();
  top_layout->setContentsMargins(0, 0, 0, 0);
  top_layout->setSpacing(0);

  settings_button = new QPushButton(this);
  settings_button->setFixedSize(110, 110);
  settings_button->setIcon(QIcon("../assets/icons/settings.png"));
  settings_button->setIconSize(QSize(50, 50));
  settings_button->setCursor(Qt::PointingHandCursor);
  settings_button->setStyleSheet(R"(
    QPushButton {
      background-color: #1D1D1D;
      border: 2px solid #3A3A3A;
      border-radius: 55px;
      padding: 0;
    }
    QPushButton:pressed {
      background-color: #2A2A2A;
    }
  )");
  settings_button->raise();
  QObject::connect(settings_button, &QPushButton::pressed, [=]() { emit openSettings(); });
  QObject::connect(settings_button, &QPushButton::clicked, [=]() { emit openSettings(); });
  top_layout->addWidget(settings_button, 0, Qt::AlignLeft | Qt::AlignTop);
  top_layout->addStretch(1);
  main_layout->addLayout(top_layout);
  main_layout->addSpacing(18);

  date = new ElidedLabel();
  date->setVisible(false);
  version = new ElidedLabel();
  version->setVisible(false);
  update_notif = new QPushButton(tr("UPDATE"));
  update_notif->setVisible(false);
  QObject::connect(update_notif, &QPushButton::clicked, [=]() { center_layout->setCurrentIndex(1); });
  alert_notif = new QPushButton();
  alert_notif->setVisible(false);
  QObject::connect(alert_notif, &QPushButton::clicked, [=] { center_layout->setCurrentIndex(2); });

  center_layout = new QStackedLayout();

  QWidget *home_widget = new QWidget(this);
  {
    QVBoxLayout *home_layout = new QVBoxLayout(home_widget);
    home_layout->setContentsMargins(0, 0, 0, 0);
    home_layout->setSpacing(0);

    home_layout->addSpacing(24);

    greeting_title = new QLabel(tr("안녕하세요"), this);
    greeting_title->setAlignment(Qt::AlignHCenter);
    greeting_title->setStyleSheet("font-size: 112px; font-weight: 800; color: #FFFFFF;");
    home_layout->addWidget(greeting_title, 0, Qt::AlignHCenter);

    home_layout->addSpacing(16);

    greeting_description = new QLabel(tr("오늘도 편안한 주행 되세요"), this);
    greeting_description->setAlignment(Qt::AlignHCenter);
    greeting_description->setStyleSheet("font-size: 52px; font-weight: 500; color: #AFAFAF;");
    home_layout->addWidget(greeting_description, 0, Qt::AlignHCenter);

    home_layout->addSpacing(72);

    QHBoxLayout *stats_layout = new QHBoxLayout();
    stats_layout->setContentsMargins(80, 0, 80, 0);
    stats_layout->setSpacing(34);

    auto createStat = [this](QWidget **card, QLabel **value, QLabel **label, const QString &title) {
      QWidget *stat = new QWidget(this);
      stat->setObjectName("summaryStatCard");
      stat->setMinimumSize(0, 250);
      stat->setStyleSheet(R"(
        QWidget#summaryStatCard {
          background-color: #141414;
          border: 1px solid #242424;
          border-radius: 30px;
        }
        QWidget#summaryStatCard QLabel {
          background: transparent;
          border: none;
        }
      )");

      QVBoxLayout *layout = new QVBoxLayout(stat);
      layout->setContentsMargins(40, 34, 40, 34);
      layout->setSpacing(14);

      *value = new QLabel("0", stat);
      (*value)->setAlignment(Qt::AlignCenter);
      (*value)->setStyleSheet("font-size: 84px; font-weight: 800; color: #FFFFFF;");

      *label = new QLabel(title, stat);
      (*label)->setAlignment(Qt::AlignCenter);
      (*label)->setStyleSheet("font-size: 34px; font-weight: 600; color: #8D8D8D;");

      layout->addStretch(1);
      layout->addWidget(*value);
      layout->addWidget(*label);
      layout->addStretch(1);
      *card = stat;
      return stat;
    };

    stats_layout->addWidget(createStat(&drive_time_card, &drive_time_value, &drive_time_label, tr("주행 시간")), 1);
    stats_layout->addWidget(createStat(&drive_distance_card, &drive_distance_value, &drive_distance_label, tr("주행 거리")), 1);
    stats_layout->addWidget(createStat(&drive_count_card, &drive_count_value, &drive_count_label, tr("오픈파일럿 사용 비율")), 1);

    home_layout->addLayout(stats_layout);
    home_layout->addStretch(1);
  }
  center_layout->addWidget(home_widget);

  // add update & alerts widgets
  update_widget = new UpdateAlert();
  QObject::connect(update_widget, &UpdateAlert::dismiss, [=]() { center_layout->setCurrentIndex(0); });
  center_layout->addWidget(update_widget);
  alerts_widget = new OffroadAlert();
  QObject::connect(alerts_widget, &OffroadAlert::dismiss, [=]() { center_layout->setCurrentIndex(0); });
  center_layout->addWidget(alerts_widget);

  main_layout->addLayout(center_layout, 1);

  // set up refresh timer
  timer = new QTimer(this);
  timer->callOnTimeout(this, &OffroadHome::refresh);

  setStyleSheet(R"(
    * {
      color: white;
    }
    OffroadHome {
      background-color: black;
    }
  )");

  QObject::connect(uiState(), &UIState::offroadTransition, [this](bool offroad) {
    const QJsonObject current_stats = QJsonDocument::fromJson(QByteArray::fromStdString(params.get("FrogPilotStats"))).object();

    if (!offroad) {
      previous_drive_stats = current_stats;
    } else if (previously_onroad) {
      last_drive_ended_at = QDateTime::currentDateTime();
      show_recent_drive_summary = true;
    }

    previously_onroad = !offroad;
    updateOffroadContent();
  });
}

void OffroadHome::mousePressEvent(QMouseEvent *event) {
  if (settings_button->geometry().adjusted(-12, -12, 12, 12).contains(event->pos())) {
    emit openSettings();
    event->accept();
    return;
  }
  QFrame::mousePressEvent(event);
}

void OffroadHome::showEvent(QShowEvent *event) {
  refresh();
  timer->start(10 * 1000);
}

void OffroadHome::hideEvent(QHideEvent *event) {
  timer->stop();
}

void OffroadHome::refresh() {
  bool updateAvailable = update_widget->refresh();
  int alerts = alerts_widget->refresh();

  // pop-up new notification
  int idx = center_layout->currentIndex();
  if (!updateAvailable && !alerts) {
    idx = 0;
  } else if (updateAvailable && (!update_notif->isVisible() || (!alerts && idx == 2))) {
    idx = 1;
  } else if (alerts && (!alert_notif->isVisible() || (!updateAvailable && idx == 1))) {
    idx = 2;
  }
  center_layout->setCurrentIndex(idx);

  update_notif->setVisible(updateAvailable);
  alert_notif->setVisible(alerts);
  if (alerts) {
    alert_notif->setText(QString::number(alerts) + (alerts > 1 ? tr(" ALERTS") : tr(" ALERT")));
  }
  updateOffroadContent();
}

void OffroadHome::updateGreetingStats() {
  const bool is_metric = params.getBool("IsMetric");
  const QJsonObject frogpilot_stats = QJsonDocument::fromJson(QByteArray::fromStdString(params.get("FrogPilotStats"))).object();

  const int total_drives = frogpilot_stats.value("FrogPilotDrives").toInt();
  const double total_meters = frogpilot_stats.value("FrogPilotMeters").toDouble();
  const int total_seconds = qMax(0, qRound(frogpilot_stats.value("FrogPilotSeconds").toDouble()));

  const double distance_value = is_metric ? total_meters / 1000.0 : total_meters * METER_TO_MILE;
  const QString distance_unit = is_metric ? tr("km") : tr("mi");

  const int total_hours = total_seconds / 3600;
  const int total_minutes = (total_seconds % 3600) / 60;
  QString formatted_time;
  if (total_hours > 0) {
    formatted_time = QString("%1시간 %2분").arg(QLocale().toString(total_hours), QLocale().toString(total_minutes));
  } else {
    formatted_time = QString("%1분").arg(QLocale().toString(total_minutes));
  }

  drive_time_value->setText(formatted_time);
  drive_time_label->setText(tr("주행 시간"));

  drive_distance_value->setText(QString("%1 %2").arg(QLocale().toString(qRound(distance_value)), distance_unit));
  drive_distance_label->setText(tr("주행 거리"));

  drive_count_value->setText(QLocale().toString(total_drives));
  drive_count_label->setText(tr("주행 횟수"));
}

void OffroadHome::updateDriveSummaryStats() {
  const bool is_metric = params.getBool("IsMetric");
  const QJsonObject current_stats = QJsonDocument::fromJson(QByteArray::fromStdString(params.get("FrogPilotStats"))).object();

  auto diff_double = [&](const QString &key) {
    return current_stats.value(key).toDouble() - previous_drive_stats.value(key).toDouble();
  };

  const int tracked_time = qMax(0, qRound(diff_double("TrackedTime")));
  const int aol_time = qMax(0, qRound(diff_double("AOLTime")));
  const int lateral_time = qMax(0, qRound(diff_double("LateralTime")));
  const int longitudinal_time = qMax(0, qRound(diff_double("LongitudinalTime")));
  const int engaged_time = qMin(tracked_time, qMax(lateral_time, longitudinal_time) + aol_time);
  const double drive_meters = qMax(0.0, diff_double("FrogPilotMeters"));

  const int engagement_percent = tracked_time > 0 ? engaged_time * 100 / tracked_time : 0;
  const double drive_distance = is_metric ? drive_meters / 1000.0 : drive_meters * METER_TO_MILE;
  const QString distance_unit = is_metric ? tr("km") : tr("mi");

  const int drive_hours = tracked_time / 3600;
  const int drive_minutes = (tracked_time % 3600) / 60;
  QString formatted_time;
  if (drive_hours > 0) {
    formatted_time = QString("%1시간 %2분").arg(QLocale().toString(drive_hours), QLocale().toString(drive_minutes));
  } else {
    formatted_time = QString("%1분").arg(QLocale().toString(drive_minutes));
  }

  drive_time_value->setText(formatted_time);
  drive_time_label->setText(tr("주행 시간"));

  drive_distance_value->setText(QString("%1 %2").arg(QLocale().toString(qRound(drive_distance)), distance_unit));
  drive_distance_label->setText(tr("주행 거리"));

  drive_count_value->setText(QString("%1%").arg(QLocale().toString(engagement_percent)));
  drive_count_label->setText(tr("오픈파일럿 사용 비율"));
}

void OffroadHome::updateOffroadContent() {
  const bool summary_preview = util::getenv("OFFROAD_SUMMARY_PREVIEW", 0) == 1;
  const bool recent_summary_active = summary_preview || (show_recent_drive_summary && last_drive_ended_at.isValid() &&
                                     last_drive_ended_at.secsTo(QDateTime::currentDateTime()) < 600);

  if (recent_summary_active) {
    greeting_title->setText(tr("주행이 종료되었습니다"));
    greeting_description->setText(tr("수고하셨습니다"));
    updateDriveSummaryStats();
  } else {
    show_recent_drive_summary = false;
    greeting_title->setText(tr("안녕하세요"));
    greeting_description->setText(tr("오늘도 편안한 주행 되세요"));
    updateGreetingStats();
  }
}
