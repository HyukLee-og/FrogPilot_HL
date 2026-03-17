#pragma once

#include <QDateTime>
#include <QFrame>
#include <QIcon>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QStackedLayout>
#include <QTimer>
#include <QWidget>

#include "common/params.h"
#include "selfdrive/ui/qt/offroad/driverview.h"
#include "selfdrive/ui/qt/body.h"
#include "selfdrive/ui/qt/onroad/onroad_home.h"
#include "selfdrive/ui/qt/sidebar.h"
#include "selfdrive/ui/qt/widgets/controls.h"
#include "selfdrive/ui/qt/widgets/offroad_alerts.h"
#include "selfdrive/ui/ui.h"

#include "frogpilot/ui/qt/widgets/developer_sidebar.h"

class OffroadHome : public QFrame {
  Q_OBJECT

public:
  explicit OffroadHome(QWidget* parent = 0);

signals:
  void openSettings(int index = 0, const QString &param = "");

private:
  void mousePressEvent(QMouseEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;
  void refresh();
  void updateGreetingStats();
  void updateDriveSummaryStats();
  void updateOffroadContent();

  Params params;

  QTimer* timer;
  ElidedLabel* version;
  QStackedLayout* center_layout;
  UpdateAlert *update_widget;
  OffroadAlert* alerts_widget;
  QPushButton* alert_notif;
  QPushButton* update_notif;

  // FrogPilot variables
  ElidedLabel* date;
  QPushButton *settings_button;
  QLabel *greeting_title;
  QLabel *greeting_description;
  QLabel *drive_count_value;
  QLabel *drive_distance_value;
  QLabel *drive_time_value;
  QLabel *drive_count_label;
  QLabel *drive_distance_label;
  QLabel *drive_time_label;
  QWidget *drive_count_card;
  QWidget *drive_distance_card;
  QWidget *drive_time_card;
  QWidget *extra_stat_card;
  QLabel *extra_stat_value;
  QLabel *extra_stat_label;
  QJsonObject previous_drive_stats;
  QDateTime last_drive_ended_at;
  bool show_recent_drive_summary = false;
  bool previously_onroad = false;
};

class HomeWindow : public QWidget {
  Q_OBJECT

public:
  explicit HomeWindow(QWidget* parent = 0);

signals:
  void openSettings(int index = 0, const QString &param = "");
  void closeSettings();

public slots:
  void offroadTransition(bool offroad);
  void showDriverView(bool show, bool started=false);
  void showSidebar(bool show);

protected:
  void mousePressEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
  Sidebar *sidebar;
  OffroadHome *home;
  OnroadWindow *onroad;
  BodyWindow *body;
  DriverViewWindow *driver_view;
  QStackedLayout *slayout;

  // FrogPilot variables
  DeveloperSidebar *developer_sidebar;

  Params params;

private slots:
  void updateState(const UIState &s, const FrogPilotUIState &fs);
};
