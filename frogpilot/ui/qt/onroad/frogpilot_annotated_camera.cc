#include "frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h"

#include <QDateTime>
#include <QPainterPath>

FrogPilotAnnotatedCameraWidget::FrogPilotAnnotatedCameraWidget(QWidget *parent) : QWidget(parent) {
  animationTimer = new QTimer(this);

  QSize iconSize(img_size / 4, img_size / 4);

  brakePedalImg = loadPixmap("../../frogpilot/assets/other_images/brake_pedal.png", {btn_size, btn_size});
  blindspotLeftImg = loadPixmap("../../files/icons/blindspot_left.png", {196, 224});
  blindspotRightImg = loadPixmap("../../files/icons/blindspot_right.png", {196, 224});
  curveSpeedIcon = loadPixmap("../../frogpilot/assets/other_images/curve_speed.png", {btn_size, btn_size});
  curveSpeedIconFlipped = curveSpeedIcon.transformed(QTransform().scale(-1, 1));
  dashboardIcon = loadPixmap("../../frogpilot/assets/other_images/dashboard_icon.png", {btn_size / 2, btn_size / 2}).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  gasPedalImg = loadPixmap("../../frogpilot/assets/other_images/gas_pedal.png", {btn_size, btn_size});
  mapboxIcon = loadPixmap("../../frogpilot/assets/other_images/mapbox_icon.png", {btn_size / 2, btn_size / 2}).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  mapDataIcon = loadPixmap("../../frogpilot/assets/other_images/offline_maps_icon.png", {btn_size / 2, btn_size / 2}).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  nextMapsIcon = loadPixmap("../../frogpilot/assets/other_images/next_maps_icon.png", {btn_size / 2, btn_size / 2}).scaled(iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  pausedIcon = loadPixmap("../../frogpilot/assets/other_images/paused_icon.png", {widget_size, widget_size});
  speedIcon = loadPixmap("../../frogpilot/assets/other_images/speed_icon.png", {widget_size, widget_size});
  stopSignImg = loadPixmap("../../frogpilot/assets/other_images/stop_sign.png", {btn_size, btn_size});
  turnIcon = loadPixmap("../../frogpilot/assets/other_images/turn_icon.png", {widget_size, widget_size});

  loadGif("../../frogpilot/assets/other_images/curve_icon.gif", cemCurveIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/lead_icon.gif", cemLeadIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/speed_icon.gif", cemSpeedIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/light_icon.gif", cemStopIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/turn_icon.gif", cemTurnIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/chill_mode_icon.gif", chillModeIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/experimental_mode_icon.gif", experimentalModeIcon, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/weather_clear_day.gif", weatherClearDay, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/weather_clear_night.gif", weatherClearNight, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/weather_low_visibility.gif", weatherLowVisibility, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/weather_rain.gif", weatherRain, QSize(widget_size, widget_size), this);
  loadGif("../../frogpilot/assets/other_images/weather_snow.gif", weatherSnow, QSize(widget_size, widget_size), this);

  QObject::connect(animationTimer, &QTimer::timeout, [this] {
    animationFrameIndex = (animationFrameIndex + 1) % totalFrames;
  });
  QObject::connect(frogpilotUIState(), &FrogPilotUIState::themeUpdated, this, &FrogPilotAnnotatedCameraWidget::updateSignals);
  QObject::connect(uiState(), &UIState::offroadTransition, [this] {
    standstillTimer.invalidate();

    QJsonObject stats = QJsonDocument::fromJson(QString::fromStdString(params.get("FrogPilotStats")).toUtf8()).object();
    stats["FrogHops"] = stats.value("FrogHops").toInt(0) + frogHopCount;
    params.putNonBlocking("FrogPilotStats", QJsonDocument(stats).toJson(QJsonDocument::Compact).toStdString());

    frogHopCount = 0;
  });
}

void FrogPilotAnnotatedCameraWidget::showEvent(QShowEvent *event) {
  updateSignals();
}

void FrogPilotAnnotatedCameraWidget::updateSignals() {
  QVector<QPixmap>().swap(blindspotImages);
  QVector<QPixmap>().swap(blindspotImagesRight);
  QVector<QPixmap>().swap(signalImages);
  QVector<QPixmap>().swap(signalImagesRight);

  bool isGif = false;

  QFileInfoList files = QDir("../../frogpilot/assets/active_theme/signals/").entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo &fileInfo : files) {
    QString fileName = fileInfo.fileName();
    QString filePath = fileInfo.absoluteFilePath();

    if (fileName.endsWith(".gif", Qt::CaseInsensitive)) {
      isGif = true;

      QMovie movie(filePath);
      movie.setCacheMode(QMovie::CacheNone);
      movie.start();

      int frameCount = movie.frameCount();
      signalImages.reserve(frameCount);
      signalImagesRight.reserve(frameCount);

      for (int i = 0; i < frameCount; ++i) {
        movie.jumpToFrame(i);

        QPixmap frame = movie.currentPixmap();
        signalImages.append(frame);
        signalImagesRight.append(frame.transformed(QTransform().scale(-1, 1)));
      }

      movie.stop();
    } else if (fileName.endsWith(".png", Qt::CaseInsensitive)) {
      QPixmap img(filePath);
      if (fileName.contains("blindspot", Qt::CaseInsensitive)) {
        blindspotImages.append(img);
        blindspotImagesRight.append(img.transformed(QTransform().scale(-1, 1)));
      } else {
        signalImages.append(img);
        signalImagesRight.append(img.transformed(QTransform().scale(-1, 1)));
      }
    } else {
      QStringList parts = fileName.split('_');
      if (parts.size() == 2) {
        signalStyle = parts[0];
        signalAnimationLength = parts[1].toInt();
      }
    }
  }

  if (!signalImages.isEmpty()) {
    QPixmap &firstImage = signalImages.front();
    signalHeight = firstImage.height();
    signalWidth = firstImage.width();
    totalFrames = signalImages.size();

    if (isGif && signalStyle == "traditional") {
      signalMovement = (width() + signalWidth * 2) / totalFrames;
      signalStyle = "traditional_gif";
    } else {
      signalMovement = 0;
    }
  } else {
    signalAnimationLength = 0;
    signalHeight = 0;
    signalMovement = 0;
    signalWidth = 0;
    totalFrames = 0;

    signalStyle = "None";
  }
}

void FrogPilotAnnotatedCameraWidget::updateState(const UIState &s, const FrogPilotUIState &fs) {
  const UIScene &scene = s.scene;

  const SubMaster &sm = *(s.sm);
  const SubMaster &fpsm = *(fs.sm);

  const cereal::CarState::Reader &carState = sm["carState"].getCarState();
  const cereal::FrogPilotCarState::Reader &frogpilotCarState = fpsm["frogpilotCarState"].getFrogpilotCarState();
  const cereal::FrogPilotPlan::Reader &frogpilotPlan = fpsm["frogpilotPlan"].getFrogpilotPlan();
  const cereal::FrogPilotSelfdriveState::Reader &frogpilotSelfdriveState = fpsm["frogpilotSelfdriveState"].getFrogpilotSelfdriveState();
  const cereal::MapdOut::Reader &mapdOut = fpsm["mapdOut"].getMapdOut();
  const cereal::ModelDataV2::Reader &modelV2 = sm["modelV2"].getModelV2();
  const cereal::SelfdriveState::Reader &selfdriveState = sm["selfdriveState"].getSelfdriveState();

  // Keep the SET-speed area compact by disabling the speed-limit widgets.
  speedLimitHeight = 0;
  speedLimitRect = QRect();
  newSpeedLimitRect = QRect();

  if (scene.is_metric || frogpilot_toggles.value("use_si_metrics").toBool()) {
    leadDistanceUnit = tr(" meters");
    leadSpeedUnit = frogpilot_toggles.value("use_si_metrics").toBool() ? tr(" m/s") : tr(" km/h");
    speedUnit = scene.is_metric ? tr("km/h") : tr("mph");

    distanceConversion = 1.0f;
    speedConversion = scene.is_metric ? MS_TO_KPH : MS_TO_MPH;
    speedConversionMetrics = frogpilot_toggles.value("use_si_metrics").toBool() ? 1.0f : MS_TO_KPH;
  } else {
    leadDistanceUnit = tr(" feet");
    leadSpeedUnit = tr(" mph");
    speedUnit = tr("mph");

    distanceConversion = METER_TO_FOOT;
    speedConversion = MS_TO_MPH;
    speedConversionMetrics = MS_TO_MPH;
  }

  accelerationEgo = carState.getAEgo();
  blindspotLeft = carState.getLeftBlindspot();
  blindspotRight = carState.getRightBlindspot();
  blinkerLeft = carState.getLeftBlinker();
  blinkerRight = carState.getRightBlinker();
  brakeLights = frogpilotCarState.getBrakeLights();
  cscControllingSpeed = frogpilotPlan.getCscControllingSpeed();
  cscSpeed = frogpilotPlan.getCscSpeed();
  cscTraining = frogpilotPlan.getCscTraining();
  dashboardSpeedLimit = frogpilotCarState.getDashboardSpeedLimit();
  desiredFollowDistance = frogpilotPlan.getDesiredFollowDistance();
  experimentalMode = selfdriveState.getExperimentalMode();
  forceCoast = frogpilotCarState.getForceCoast();
  laneWidthLeft = frogpilotPlan.getLaneWidthLeft();
  laneWidthRight = frogpilotPlan.getLaneWidthRight();
  lateralPaused = frogpilotCarState.getPauseLateral();
  longitudinalPaused = frogpilotCarState.getPauseLongitudinal();
  mapSpeedLimit = frogpilotPlan.getSlcMapSpeedLimit();
  mapboxSpeedLimit = frogpilotPlan.getSlcMapboxSpeedLimit();
  nextSpeedLimit = frogpilotPlan.getSlcNextSpeedLimit();
  redLight = frogpilotPlan.getRedLight();
  roadCurvature = frogpilotPlan.getRoadCurvature();
  roadName = QString::fromStdString(mapdOut.getRoadName());
  slcOverriddenSpeed = frogpilotPlan.getSlcOverriddenSpeed();
  speedLimit = slcOverriddenSpeed != 0 ? slcOverriddenSpeed : frogpilotPlan.getSlcSpeedLimit();
  speedLimitChanged = frogpilotPlan.getSpeedLimitChanged();
  speedLimitSource = frogpilotPlan.getSlcSpeedLimitSource();
  stoppingDistance = modelV2.getPosition().getX().size() > 33 - 1 ? modelV2.getPosition().getX()[33 - 1] : 0.0;
  unconfirmedSpeedLimit = frogpilotPlan.getUnconfirmedSlcSpeedLimit();
  weatherDaytime = frogpilotPlan.getWeatherDaytime();
  weatherId = frogpilotPlan.getWeatherId();

  hideBottomIcons = selfdriveState.getAlertSize() != cereal::SelfdriveState::AlertSize::NONE;
  hideBottomIcons |= frogpilotSelfdriveState.getAlertSize() != cereal::FrogPilotSelfdriveState::AlertSize::NONE;
  hideBottomIcons |= signalStyle.startsWith("traditional") && (blinkerLeft || blinkerRight);

  if (slcOverriddenSpeed == 0 && !frogpilot_toggles.value("show_speed_limit_offset").toBool()) {
    speedLimit += frogpilotPlan.getSlcSpeedLimitOffset();
  }
  speedLimit *= (scene.is_metric ? MS_TO_KPH : MS_TO_MPH);
  float speedLimitOffset = frogpilotPlan.getSlcSpeedLimitOffset() * speedConversion;
  speedLimitOffsetStr = (speedLimitOffset != 0) ? QString::number(speedLimitOffset, 'f', 0).prepend((speedLimitOffset > 0) ? "+" : "-") : "–";

  const bool fakeLongEnabled = frogpilot_toggles.value("fake_long").toBool();
  const bool fakeLongTestUIEnabled = frogpilot_toggles.value("fake_long_test_ui").toBool();
  showFakeLongTestUI = fakeLongEnabled || fakeLongTestUIEnabled;
  showFakeLongButtons = fakeLongTestUIEnabled;
  fakeLongCurrentSpeed = carState.getCruiseState().getSpeed() * speedConversion;
  fakeLongApplySpeed = fakeLongCurrentSpeed;
  fakeLongTargetSpeed = fakeLongApplySpeed;
  fakeLongArmed = false;
  fakeLongPaused = false;
  fakeLongLastButton.clear();

  const std::string fake_long_debug = params_memory.get("FakeLongDebug");
  if (!fake_long_debug.empty()) {
    const QJsonObject debug = QJsonDocument::fromJson(QByteArray::fromStdString(fake_long_debug)).object();
    fakeLongArmed = debug.value("armed").toBool(false);
    fakeLongPaused = debug.value("paused").toBool(false);
    fakeLongCurrentSpeed = debug.value("userSet").toDouble(fakeLongCurrentSpeed / speedConversion) * speedConversion;
    fakeLongApplySpeed = debug.value("commanded").toDouble(fakeLongApplySpeed / speedConversion) * speedConversion;
    fakeLongTargetSpeed = debug.value("target").toDouble(fakeLongApplySpeed / speedConversion) * speedConversion;
    fakeLongLastButton = debug.value("last").toString().toUpper();
  }

  bool fake_long_preview_ok = false;
  const QString fake_long_preview = qEnvironmentVariable("FAKE_LONG_UI_PREVIEW").trimmed();
  if (!fake_long_preview.isEmpty()) {
    const QStringList parts = fake_long_preview.split(',');
    if (parts.size() == 2) {
      bool apply_ok = false;
      bool current_ok = false;
      const float preview_apply = parts[0].trimmed().toFloat(&apply_ok);
      const float preview_current = parts[1].trimmed().toFloat(&current_ok);
      fake_long_preview_ok = apply_ok && current_ok;
      if (fake_long_preview_ok) {
        showFakeLongTestUI = true;
        fakeLongApplySpeed = preview_apply;
        fakeLongCurrentSpeed = preview_current;
      }
    }
  }

  static int lastFrameIndex;
  if (lastFrameIndex > animationFrameIndex && frogpilot_toggles.value("signal_icons").toString() == "frog") {
    frogHopCount++;
  }
  lastFrameIndex = animationFrameIndex;

  if ((blinkerLeft || blinkerRight) && signalStyle != "None") {
    if (!animationTimer->isActive()) {
      animationTimer->start(signalAnimationLength);
    }
  } else if (animationTimer->isActive()) {
    animationFrameIndex = 0;
    animationTimer->stop();
  }

  if (cscTraining) {
    if (!glowTimer.isValid()) {
      glowTimer.start();
    }
  } else {
    glowTimer.invalidate();
  }

  if (speedLimitChanged) {
    if (!pendingLimitTimer.isValid()) {
      pendingLimitTimer.start();
    }
  } else {
    pendingLimitTimer.invalidate();
  }

  const bool resume_required_active = QString::fromUtf8(selfdriveState.getAlertType().cStr()).contains("resumeRequired", Qt::CaseInsensitive);
  bool standstill_preview_ok = false;
  const int standstill_preview = qEnvironmentVariableIntValue("STANDSTILL_PREVIEW", &standstill_preview_ok);
  if (resume_required_active) {
    standstillDuration = 0;
    standstillTimer.invalidate();
  } else if (standstill_preview_ok && standstill_preview >= 0) {
    standstillDuration = standstill_preview;
    standstillTimer.invalidate();
  } else if (frogpilot_scene.standstill && frogpilot_toggles.value("stopped_timer").toBool()) {
    if (!standstillTimer.isValid()) {
      standstillTimer.start();
    } else {
      standstillDuration = frogpilot_scene.started_timer / UI_FREQ < 60 ? 0 : standstillTimer.elapsed() / 1000;
    }
  } else {
    standstillDuration = 0;
    standstillTimer.invalidate();
  }
}

void FrogPilotAnnotatedCameraWidget::mousePressEvent(QMouseEvent *mouseEvent) {
  if (speedLimitChanged && newSpeedLimitRect.contains(mouseEvent->pos())) {
    params_memory.putBool("SpeedLimitAccepted", true);
    mouseEvent->accept();
    return;
  }

  if (showFakeLongButtons) {
    const QPoint pos = mouseEvent->pos();
    QString button_key;
    if (fakeLongMainRect.contains(pos)) {
      button_key = "main";
    } else if (fakeLongCancelRect.contains(pos)) {
      button_key = "cancel";
    } else if (fakeLongResRect.contains(pos)) {
      button_key = "res";
    } else if (fakeLongSetRect.contains(pos)) {
      button_key = "set";
    }

    if (!button_key.isEmpty()) {
      const QString payload = QString("%1:%2").arg(button_key).arg(QDateTime::currentMSecsSinceEpoch());
      params_memory.put("FakeLongTestButton", payload.toStdString());
      fakeLongActiveButton = button_key;
      fakeLongButtonTimer.restart();
      mouseEvent->accept();
      return;
    }
  }

  mouseEvent->ignore();
}

void FrogPilotAnnotatedCameraWidget::paintFrogPilotWidgets(QPainter &p, UIState &s) {
  if (!hideBottomIcons && frogpilot_toggles.value("cem_status").toBool()) {
    paintCEMStatus(p);
  } else {
    cemStatusPosition.setX(0);
    cemStatusPosition.setY(0);
  }

  if (!hideBottomIcons && frogpilot_toggles.value("compass").toBool()) {
    paintCompass(p);
  } else {
    compassPosition.setX(0);
    compassPosition.setY(0);
  }

  if (!speedLimitChanged && !(signalStyle == "static" && blinkerLeft) && frogpilot_toggles.value("csc_status").toBool()) {
    if (cscTraining) {
      paintCurveSpeedControlTraining(p);
    } else if (isCruiseSet && cscControllingSpeed) {
      paintCurveSpeedControl(p);
    }
  }

  if (!hideBottomIcons && lateralPaused) {
    paintLateralPaused(p);
  } else {
    lateralPausedPosition.setX(0);
    lateralPausedPosition.setY(0);
  }

  if (!hideBottomIcons && (forceCoast || longitudinalPaused)) {
    paintLongitudinalPaused(p);
  }

  if (frogpilot_toggles.value("pedals_on_ui").toBool()) {
    paintPedalIcons(p);
  }

  if (frogpilot_toggles.value("radar_tracks").toBool()) {
    paintRadarTracks(p);
  }

  if (frogpilot_toggles.value("road_name_ui").toBool()) {
    paintRoadName(p);
  }

  if (standstillDuration != 0) {
    paintStandstillTimer(p);
  }

  if (showFakeLongTestUI) {
    paintFakeLongTestUI(p);
  }

  if (track_vertices.length() >= 1 && redLight && frogpilot_toggles.value("show_stopping_point").toBool()) {
    paintStoppingPoint(p);
  }

  if ((blinkerLeft || blinkerRight) && signalStyle != "None" && (standstillDuration == 0 || signalStyle != "static")) {
    paintTurnSignals(p);
  }

  if (!hideBottomIcons) {
    paintWeather(p);
  }

  paintBlindspotIcons(p);
}

void FrogPilotAnnotatedCameraWidget::paintAdjacentPaths(QPainter &p) {
  std::function<void(const QPolygonF&, bool, bool, float)> paintPath = [&](const QPolygonF &path, bool isLeft, bool isBlindSpot, float laneWidth) {
    if (laneWidth == 0.0f) {
      return;
    }

    p.save();

    QLinearGradient gradient(0, height(), 0, 0);
    if (isBlindSpot && frogpilot_toggles.value("blind_spot_path").toBool()) {
      gradient.setColorAt(0.0f, QColor::fromHslF(0.0f, 0.75f, 0.5f, 0.4f));
      gradient.setColorAt(0.5f, QColor::fromHslF(0.0f, 0.75f, 0.5f, 0.35f));
      gradient.setColorAt(1.0f, QColor::fromHslF(0.0f, 0.75f, 0.5f, 0.0f));
    } else {
      float ratio = std::clamp(laneWidth / frogpilot_toggles.value("lane_detection_width").toDouble(), 0.0, 1.0);
      float hue = (ratio * ratio) * (120.0f / 360.0f);

      gradient.setColorAt(0.0f, QColor::fromHslF(hue, 0.75f, 0.5f, 0.4f));
      gradient.setColorAt(0.5f, QColor::fromHslF(hue, 0.75f, 0.5f, 0.35f));
      gradient.setColorAt(1.0f, QColor::fromHslF(hue, 0.75f, 0.5f, 0.0f));
    }

    p.setBrush(gradient);
    p.drawPolygon(path);

    if (frogpilot_toggles.value("adjacent_path_metrics").toBool()) {
      QString text;
      if (isBlindSpot && frogpilot_toggles.value("blind_spot_path").toBool()) {
        text = tr("Vehicle in blind spot");
      } else {
        text = QString::number(laneWidth * distanceConversion, 'f', 2) + leadDistanceUnit;
      }

      int midIndex = path.size() / 2;
      QPointF anchorPoint = isLeft ? path[midIndex / 2] : path[midIndex + (path.size() - midIndex) / 2];

      p.setFont(InterFont(45, QFont::DemiBold));
      QFontMetrics metrics(p.font());

      int textXPosition = isLeft ? anchorPoint.x() - metrics.horizontalAdvance(text) : anchorPoint.x();
      int textYPosition = anchorPoint.y() - metrics.height() / 2 + metrics.ascent();

      QPainterPath textPath;
      textPath.addText(textXPosition, textYPosition, p.font(), text);
      p.strokePath(textPath, QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

      p.setPen(whiteColor());
      p.drawText(textXPosition, textYPosition, text);
    }

    p.restore();
  };

  paintPath(track_adjacent_vertices[0], true, blindspotLeft, laneWidthLeft);
  paintPath(track_adjacent_vertices[1], false, blindspotRight, laneWidthRight);
}

void FrogPilotAnnotatedCameraWidget::paintBlindSpotPath(QPainter &p) {
  p.save();

  QLinearGradient bs(0, height(), 0, 0);
  bs.setColorAt(0.0f, QColor::fromHslF(0 / 360.0f, 0.75f, 0.5f, 0.4f));
  bs.setColorAt(0.5f, QColor::fromHslF(0 / 360.0f, 0.75f, 0.5f, 0.35f));
  bs.setColorAt(1.0f, QColor::fromHslF(0 / 360.0f, 0.75f, 0.5f, 0.0f));
  p.setBrush(bs);

  if (track_adjacent_vertices[0].boundingRect().width() > 0 && blindspotLeft) {
    p.drawPolygon(track_adjacent_vertices[0]);
  }
  if (track_adjacent_vertices[1].boundingRect().width() > 0 && blindspotRight) {
    p.drawPolygon(track_adjacent_vertices[1]);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintBlindspotIcons(QPainter &p) {
  const QString preview = qEnvironmentVariable("BLINDSPOT_PREVIEW").trimmed().toLower();
  const bool preview_left = preview == "1" || preview.contains("left") || preview.contains("both");
  const bool preview_right = preview == "1" || preview.contains("right") || preview.contains("both");
  const bool preview_blink_left = preview.contains("left-blink") || preview.contains("both-blink");
  const bool preview_blink_right = preview.contains("right-blink") || preview.contains("both-blink");

  const bool show_left = blindspotLeft || preview_left;
  const bool show_right = blindspotRight || preview_right;
  if (!show_left && !show_right) return;

  const bool blink_left = (blindspotLeft && blinkerLeft) || preview_blink_left;
  const bool blink_right = (blindspotRight && blinkerRight) || preview_blink_right;
  const bool blink_visible = ((QDateTime::currentMSecsSinceEpoch() / 120) % 2) == 0;

  const int left_margin = 44;
  const int right_margin = 40;
  const QRect viewport = p.viewport();
  const int y = viewport.center().y() - 180;

  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);

  auto draw_blindspot_glow = [&](const QRect &icon_rect, const QColor &glow_color) {
    QRect glow_rect = icon_rect.adjusted(-34, -28, 34, 28);
    QRadialGradient glow(glow_rect.center(), glow_rect.width() * 0.56);
    glow.setColorAt(0.0, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 82));
    glow.setColorAt(0.45, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 34));
    glow.setColorAt(1.0, QColor(glow_color.red(), glow_color.green(), glow_color.blue(), 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(glow_rect);
  };

  if (show_left && (!blink_left || blink_visible) && !blindspotLeftImg.isNull()) {
    const int x = viewport.left() + left_margin;
    draw_blindspot_glow(QRect(x, y, blindspotLeftImg.width(), blindspotLeftImg.height()), QColor(0xFF, 0x67, 0x4A));
    p.drawPixmap(x, y, blindspotLeftImg);
  }

  if (show_right && (!blink_right || blink_visible) && !blindspotRightImg.isNull()) {
    const int x = viewport.right() - right_margin - blindspotRightImg.width() + 1;
    draw_blindspot_glow(QRect(x, y, blindspotRightImg.width(), blindspotRightImg.height()), QColor(0xFF, 0x67, 0x4A));
    p.drawPixmap(x, y, blindspotRightImg);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintCEMStatus(QPainter &p) {
  if (dmIconPosition == QPoint(0, 0)) {
    return;
  }

  p.save();

  cemStatusPosition.setX(dmIconPosition.x() + (rightHandDM ? -img_size - widget_size : widget_size));
  cemStatusPosition.setY(dmIconPosition.y() - widget_size / 2);

  QRect cemWidget(cemStatusPosition, QSize(widget_size, widget_size));

  p.setBrush(blackColor(166));
  if (frogpilot_scene.conditional_status == 1) {
    p.setPen(QPen(QColor(bg_colors[STATUS_CEM_DISABLED]), 10));
  } else if (experimentalMode) {
    p.setPen(QPen(QColor(bg_colors[STATUS_EXPERIMENTAL_MODE_ENABLED]), 10));
  } else {
    p.setPen(QPen(blackColor(), 10));
  }
  p.drawRoundedRect(cemWidget, 24, 24);

  QSharedPointer<QMovie> icon = chillModeIcon;
  if (experimentalMode) {
    if (frogpilot_scene.conditional_status == 1) {
      icon = chillModeIcon;
    } else if (frogpilot_scene.conditional_status == 2) {
      icon = experimentalModeIcon;
    } else if (frogpilot_scene.conditional_status == 3) {
      icon = cemCurveIcon;
    } else if (frogpilot_scene.conditional_status == 4) {
      icon = cemLeadIcon;
    } else if (frogpilot_scene.conditional_status == 5) {
      icon = cemTurnIcon;
    } else if (frogpilot_scene.conditional_status == 6 || frogpilot_scene.conditional_status == 7) {
      icon = cemSpeedIcon;
    } else if (frogpilot_scene.conditional_status == 8) {
      icon = cemStopIcon;
    } else {
      icon = experimentalModeIcon;
    }
  }
  p.drawPixmap(cemWidget, icon->currentPixmap());

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintCompass(QPainter &p) {
  if (dmIconPosition == QPoint(0, 0)) {
    return;
  }

  p.save();

  constexpr double PIXELS_PER_DEGREE = 2.5;

  constexpr int BASE_RIBBON_WIDTH = static_cast<int>(360 * PIXELS_PER_DEGREE);
  constexpr int BORDER_WIDTH = 10;
  constexpr int MARGIN = 5;
  constexpr int TRIANGLE_SIZE = 40;

  static QPixmap compassRibbon = [&]() {
    QPixmap ribbon(BASE_RIBBON_WIDTH * 2, widget_size);
    ribbon.fill(Qt::transparent);

    QPainter ribbonPainter(&ribbon);
    ribbonPainter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    QFont font = InterFont(65, QFont::Bold);
    ribbonPainter.setFont(font);
    QFontMetrics fm(font);

    QMap<int, QString> directionLabels = {{0, "N"}, {45, "NE"}, {90, "E"}, {135, "SE"}, {180, "S"}, {225, "SW"}, {270, "W"}, {315, "NW"}, {360, "N"}};

    for (int cycle = 0; cycle < 2; ++cycle) {
      int xOffset = cycle * 360;

      for (int degree = 0; degree < 360; ++degree) {
        int x = qRound((xOffset + degree) * PIXELS_PER_DEGREE);

        if (directionLabels.contains(degree)) {
          QString label = directionLabels[degree];
          ribbonPainter.setPen(whiteColor());
          ribbonPainter.drawText(x - fm.horizontalAdvance(label) / 2, fm.ascent(), label);
        }

        int notchHeight = (degree % 45 == 0) ? 35 : (degree % 15 == 0) ? 25 : 15;
        int notchWidth = (degree % 45 == 0) ? 5 : (degree % 15 == 0) ? 4 : 3;

        ribbonPainter.setPen(QPen(whiteColor(), notchWidth));
        ribbonPainter.drawLine(x, widget_size - notchHeight - MARGIN, x, widget_size);
      }
    }

    return ribbon;
  }();

  compassPosition.rx() = rightHandDM ? UI_BORDER_SIZE + widget_size / 2 : width() - UI_BORDER_SIZE - btn_size;
  compassPosition.ry() = dmIconPosition.y() - widget_size / 2;

  QRect compassWidget(compassPosition, QSize(widget_size, widget_size));

  p.setBrush(blackColor(166));
  p.setPen(QPen(blackColor(), BORDER_WIDTH));
  p.drawRoundedRect(compassWidget, 24, 24);

  QPainterPath clipPath;
  clipPath.addRoundedRect(compassWidget.adjusted(MARGIN, MARGIN, -MARGIN, -MARGIN), 24, 24);
  p.setClipPath(clipPath);

  double rawBearing = QJsonDocument::fromJson(QByteArray::fromStdString(params_memory.get("LastGPSPosition"))).object().value("bearing").toDouble(0.0);
  int bearing = qRound(fmod(rawBearing + 360.0, 360.0));
  int offset = qRound(bearing * PIXELS_PER_DEGREE) % BASE_RIBBON_WIDTH;
  int drawX = compassWidget.center().x() - offset;

  p.drawPixmap(drawX - BASE_RIBBON_WIDTH, compassWidget.top() + MARGIN, compassRibbon);
  p.drawPixmap(drawX, compassWidget.top() + MARGIN, compassRibbon);

  int triangleX = compassWidget.center().x();
  int triangleY = compassWidget.bottom() - TRIANGLE_SIZE;
  QPolygon triangle({
    QPoint(triangleX, triangleY - TRIANGLE_SIZE),
    QPoint(triangleX - TRIANGLE_SIZE / 1.5, triangleY),
    QPoint(triangleX + TRIANGLE_SIZE / 1.5, triangleY)
  });

  p.setBrush(whiteColor());
  p.setPen(Qt::NoPen);
  p.drawPolygon(triangle);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintCurveSpeedControl(QPainter &p) {
  p.save();

  QRect curveSpeedRect(QPoint(setSpeedRect.right() + UI_BORDER_SIZE, setSpeedRect.top()), QSize(defaultSize.width() * 1.25, defaultSize.width() * 1.25));

  QPixmap &curveSpeedImage = roadCurvature < 0 ? curveSpeedIcon : curveSpeedIconFlipped;
  QSize curveSpeedSize = curveSpeedImage.size();
  QPoint curveSpeedPoint = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, curveSpeedSize, curveSpeedRect).topLeft();

  p.setOpacity(1.0);

  QRect cscRect(curveSpeedRect.topLeft() + QPoint(0, curveSpeedRect.height() + 10), QSize(curveSpeedRect.width(), 100));
  p.setBrush(blueColor(166));
  p.setFont(InterFont(45, QFont::Bold));
  p.setPen(QPen(blueColor(), 10));
  p.drawRoundedRect(cscRect, 24, 24);
  p.setPen(QPen(whiteColor(), 6));
  p.drawText(cscRect.adjusted(20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, QString::number(std::nearbyint(fmin(speed, cscSpeed * speedConversion))) + speedUnit);
  p.drawPixmap(curveSpeedPoint, curveSpeedImage);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintCurveSpeedControlTraining(QPainter &p) {
  p.save();

  qreal phase = (glowTimer.elapsed() % 2000) / 2000.0 * 2 * M_PI;
  qreal alphaFactor = 0.5 + 0.5 * sin(phase);

  QColor glowColor = blueColor();
  glowColor.setAlphaF(0.3 + 0.7 * alphaFactor);

  int glowWidth = 8 + static_cast<int>(2 * alphaFactor);

  QRect curveSpeedRect(QPoint(setSpeedRect.right() + UI_BORDER_SIZE, setSpeedRect.top()), QSize(defaultSize.width() * 1.25, defaultSize.width() * 1.25));

  QPixmap &curveSpeedImage = roadCurvature < 0 ? curveSpeedIcon : curveSpeedIconFlipped;
  QSize curveSpeedSize = curveSpeedImage.size();
  QPoint curveSpeedPoint = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, curveSpeedSize, curveSpeedRect).topLeft();

  p.setOpacity(1.0);

  p.setBrush(blackColor(166));
  p.setPen(QPen(glowColor, glowWidth));
  p.drawRoundedRect(curveSpeedRect, 24, 24);
  p.drawPixmap(curveSpeedPoint, curveSpeedImage);
  p.setBrush(blackColor(166));
  p.setFont(InterFont(35, QFont::Bold));
  p.setPen(QPen(blackColor(), 10));

  QRect textRect(curveSpeedRect.topLeft() + QPoint(0, curveSpeedRect.height() + 10), QSize(curveSpeedRect.width(), 50));
  p.drawRoundedRect(textRect, 24, 24);
  p.setPen(QPen(whiteColor(), 6));
  p.drawText(textRect.adjusted(20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, "Training...");

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintLateralPaused(QPainter &p) {
  if (dmIconPosition == QPoint(0, 0)) {
    return;
  }

  p.save();

  if (cemStatusPosition != QPoint(0, 0)) {
    lateralPausedPosition = cemStatusPosition;
  } else {
    lateralPausedPosition.rx() = dmIconPosition.x();
    lateralPausedPosition.ry() = dmIconPosition.y() - widget_size / 2;
  }
  lateralPausedPosition.rx() += rightHandDM ? -UI_BORDER_SIZE - widget_size - UI_BORDER_SIZE : UI_BORDER_SIZE + widget_size + UI_BORDER_SIZE;

  QRect lateralWidget(lateralPausedPosition, QSize(widget_size, widget_size));

  p.setBrush(blackColor(166));
  p.setPen(QPen(QColor(bg_colors[STATUS_TRAFFIC_MODE_ENABLED]), 10));
  p.drawRoundedRect(lateralWidget, 24, 24);

  p.setOpacity(0.5);
  p.drawPixmap(lateralWidget, turnIcon);
  p.setOpacity(0.75);
  p.drawPixmap(lateralWidget, pausedIcon);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintLeadMetrics(QPainter &p, bool adjacent, QPointF *chevron, const cereal::RadarState::LeadData::Reader &lead_data) {
  float leadDistance = lead_data.getDRel() + (adjacent ? std::abs(lead_data.getYRel()) : 0.0f);
  float leadSpeed = std::max(lead_data.getVLead(), 0.0f);

  QString distanceString = QString::number(qRound(leadDistance * distanceConversion));
  QString speedString = QString::number(qRound(leadSpeed * speedConversionMetrics));

  QVector<QString> textLines;
  textLines.reserve(3);
  if (adjacent) {
    textLines.append(QString("%1 %2").arg(distanceString, leadDistanceUnit));
    textLines.append(QString("%1 %2").arg(speedString, leadSpeedUnit));
  } else {
    if (frogpilot_toggles.value("openpilot_longitudinal").toBool()) {
      int desiredDistance = std::max(0, qRound(desiredFollowDistance * distanceConversion));
      textLines.append(QString("%1 %2 (%3)").arg(distanceString, leadDistanceUnit, tr("Desired: %1").arg(desiredDistance)));
    } else {
      textLines.append(QString("%1 %2").arg(distanceString, leadDistanceUnit));
    }
    textLines.append(QString("%1 %2").arg(speedString, leadSpeedUnit));

    float timeGap = leadDistance / std::max(speed / speedConversion, 1.0f);
    textLines.append(QString("%1 %2").arg(QString::number(timeGap, 'f', 2), tr("seconds")));
  }

  p.setFont(InterFont(45, QFont::DemiBold));
  p.setPen(whiteColor());

  QFontMetrics metrics(p.font());
  int lineHeight = metrics.lineSpacing();

  int maxTextWidth = 0;
  for (QString &line : textLines) {
    maxTextWidth = std::max(maxTextWidth, metrics.horizontalAdvance(line));
  }

  int centerX = (chevron[2].x() + chevron[0].x()) / 2;
  int startY = chevron[0].y() + lineHeight + 5;

  int xMargin = maxTextWidth * 0.1;
  int yMargin = lineHeight * 0.1;

  QRect textRect(centerX - maxTextWidth / 2, startY - lineHeight, maxTextWidth, textLines.size() * lineHeight);
  textRect.adjust(-xMargin, -yMargin, xMargin, yMargin);

  if (adjacent) {
    if (textRect.intersects(adjacentLeadTextRect) || textRect.intersects(leadTextRect)) {
      return;
    }
    adjacentLeadTextRect = textRect;
  } else {
    leadTextRect = textRect;
  }

  for (int i = 0; i < textLines.size(); ++i) {
    int lineX = centerX - metrics.horizontalAdvance(textLines[i]) / 2;
    int lineY = startY + (i * lineHeight);

    QPainterPath path;
    path.addText(lineX, lineY, p.font(), textLines[i]);
    p.strokePath(path, QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    p.setPen(whiteColor());
    p.drawText(lineX, lineY, textLines[i]);
  }
}

void FrogPilotAnnotatedCameraWidget::paintLongitudinalPaused(QPainter &p) {
  if (dmIconPosition == QPoint(0, 0)) {
    return;
  }

  p.save();

  QPoint longitudinalIconPosition;
  if (lateralPausedPosition != QPoint(0, 0)) {
    longitudinalIconPosition = lateralPausedPosition;
  } else if (cemStatusPosition != QPoint(0, 0)) {
    longitudinalIconPosition = cemStatusPosition;
  } else {
    longitudinalIconPosition.rx() = dmIconPosition.x();
    longitudinalIconPosition.ry() = dmIconPosition.y() - widget_size / 2;
  }
  longitudinalIconPosition.rx() += rightHandDM ? -UI_BORDER_SIZE - widget_size - UI_BORDER_SIZE : UI_BORDER_SIZE + widget_size + UI_BORDER_SIZE;

  QRect longitudinalWidget(longitudinalIconPosition, QSize(widget_size, widget_size));

  p.setBrush(blackColor(166));
  p.setPen(QPen(QColor(bg_colors[STATUS_TRAFFIC_MODE_ENABLED]), 10));
  p.drawRoundedRect(longitudinalWidget, 24, 24);

  p.setOpacity(0.5);
  p.drawPixmap(longitudinalWidget, speedIcon);
  p.setOpacity(0.75);
  p.drawPixmap(longitudinalWidget, pausedIcon);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintPathEdges(QPainter &p, int height) {
  p.save();

  QLinearGradient gradient(0, height, 0, 0);
  const bool disengaged = uiState()->status == STATUS_DISENGAGED;

  std::function<void(const QColor &)> setPathEdgeColors = [&gradient](const QColor &baseColor) {
    gradient.setColorAt(0.0f, QColor(baseColor.red(), baseColor.green(), baseColor.blue(), 255.0f * 0.4f));
    gradient.setColorAt(0.5f, QColor(baseColor.red(), baseColor.green(), baseColor.blue(), 255.0f * 0.35f));
    gradient.setColorAt(1.0f, QColor(baseColor.red(), baseColor.green(), baseColor.blue(), 255.0f * 0.0f));
  };

  if (disengaged) {
    const QColor gray_edge(0xe7, 0xea, 0xef);
    setPathEdgeColors(gray_edge);
  } else if (frogpilot_scene.always_on_lateral_active) {
    setPathEdgeColors(bg_colors[STATUS_ALWAYS_ON_LATERAL_ACTIVE]);
  } else if (frogpilot_scene.conditional_status == 1) {
    setPathEdgeColors(bg_colors[STATUS_CEM_DISABLED]);
  } else if (experimentalMode) {
    setPathEdgeColors(bg_colors[STATUS_EXPERIMENTAL_MODE_ENABLED]);
  } else if (frogpilot_scene.traffic_mode_enabled) {
    setPathEdgeColors(bg_colors[STATUS_TRAFFIC_MODE_ENABLED]);
  } else if (frogpilot_toggles.value("color_scheme").toString() != "stock") {
    setPathEdgeColors(QColor(frogpilot_toggles.value("path_edges_color").toString()));
  } else {
    gradient.setColorAt(0.0f, QColor::fromHslF(148.0f / 360.0f, 0.94f, 0.41f, 0.4f));
    gradient.setColorAt(0.5f, QColor::fromHslF(112.0f / 360.0f, 1.0f, 0.54f, 0.35f));
    gradient.setColorAt(1.0f, QColor::fromHslF(112.0f / 360.0f, 1.0f, 0.54f, 0.0f));
  }

  p.setBrush(gradient);

  QPainterPath path;
  path.addPolygon(track_vertices);
  path.addPolygon(track_edge_vertices);
  p.drawPath(path);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintPedalIcons(QPainter &p) {
  p.save();

  float brakeOpacity = 1.0f;
  float gasOpacity = 1.0f;

  if (frogpilot_toggles.value("dynamic_pedals_on_ui").toBool()) {
    brakeOpacity = frogpilot_scene.standstill ? 1.0f : accelerationEgo < -0.25f ? std::max(0.25f, std::abs(accelerationEgo)) : 0.25f;
    gasOpacity = std::max(0.25f, accelerationEgo);
  } else if (frogpilot_toggles.value("static_pedals_on_ui").toBool()) {
    brakeOpacity = frogpilot_scene.standstill || brakeLights || accelerationEgo < -0.25f ? 1.0f : 0.25f;
    gasOpacity = accelerationEgo > 0.25 ? 1.0f : 0.25f;
  }

  int startX = experimentalButtonPosition.x();
  int startY = experimentalButtonPosition.y() + btn_size + UI_BORDER_SIZE;

  p.setOpacity(brakeOpacity);
  p.drawPixmap(startX, startY, brakePedalImg);

  p.setOpacity(gasOpacity);
  p.drawPixmap(startX + btn_size / 2, startY, gasPedalImg);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintPendingSpeedLimit(QPainter &p) {
  p.save();

  QString newSpeedLimitStr = (unconfirmedSpeedLimit > 1) ? QString::number(std::nearbyint(unconfirmedSpeedLimit * speedConversion)) : "–";
  newSpeedLimitRect = speedLimitRect.translated(speedLimitRect.width() + UI_BORDER_SIZE, 0);

  if (!frogpilot_toggles.value("speed_limit_vienna").toBool()) {
    newSpeedLimitRect.setWidth(newSpeedLimitStr.size() >= 3 ? 200 : 175);

    p.setBrush(whiteColor());
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(newSpeedLimitRect, 24, 24);
    p.setPen(pendingLimitTimer.elapsed() % 1000 < 500 ? QPen(blackColor(), 6) : QPen(redColor(), 6));
    p.drawRoundedRect(newSpeedLimitRect.adjusted(9, 9, -9, -9), 16, 16);

    p.setFont(InterFont(28, QFont::DemiBold));
    p.drawText(newSpeedLimitRect.adjusted(0, 22, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("PENDING"));
    p.drawText(newSpeedLimitRect.adjusted(0, 51, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("LIMIT"));
    p.setFont(InterFont(70, QFont::Bold));
    p.drawText(newSpeedLimitRect.adjusted(0, 85, 0, 0), Qt::AlignTop | Qt::AlignHCenter, newSpeedLimitStr);
  } else {
    p.setBrush(whiteColor());
    p.setPen(Qt::NoPen);
    p.drawEllipse(newSpeedLimitRect);
    p.setPen(QPen(Qt::red, 20));
    p.drawEllipse(newSpeedLimitRect.adjusted(16, 16, -16, -16));

    p.setPen(pendingLimitTimer.elapsed() % 1000 < 500 ? QPen(blackColor(), 6) : QPen(redColor(), 6));
    p.setFont(InterFont((newSpeedLimitStr.size() >= 3) ? 60 : 70, QFont::Bold));
    p.drawText(newSpeedLimitRect, Qt::AlignCenter, newSpeedLimitStr);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintRainbowPath(QPainter &p, QLinearGradient &bg, float lin_grad_point) {
  p.save();

  static float hueOffset = 0.0f;
  if (speed > 0) {
    hueOffset += speed / speedConversion * 0.02f;

    if (hueOffset >= 360.0f) {
      hueOffset = fmodf(hueOffset, 360.0f);
    }
  }

  float alpha = util::map_val(lin_grad_point, 0.0f, 1.0f, 0.5f, 0.1f);
  float pathHue = fmodf(lin_grad_point * 120.0f + hueOffset, 360.0f);

  bg.setColorAt(lin_grad_point, QColor::fromHslF(pathHue / 360.0f, 1.0f, 0.5f, alpha));
  bg.setSpread(QGradient::RepeatSpread);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintRadarTracks(QPainter &p) {
  if (radar_tracks.empty()) {
    return;
  }

  p.save();

  int diameter = 25;

  float radius = diameter / 2.0f;
  float track_x = p.viewport().width() - diameter;
  float track_y = p.viewport().height() - diameter;

  p.setBrush(redColor());

  for (const QPointF &track : radar_tracks) {
    float x = std::clamp(static_cast<float>(track.x()), 0.0f, track_x);
    float y = std::clamp(static_cast<float>(track.y()), 0.0f, track_y);

    p.drawEllipse(QPointF(x + radius, y + radius), radius, radius);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintRoadName(QPainter &p) {
  if (roadName.isEmpty()) {
    return;
  }

  p.save();

  QFont font = InterFont(40, QFont::DemiBold);

  int textWidth = QFontMetrics(font).horizontalAdvance(roadName);

  QSize size(textWidth + 100, 50);
  QRect roadNameRect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignHCenter | Qt::AlignBottom, size, rect().adjusted(0, 0, 0, -5));

  p.setBrush(blackColor(166));
  p.setOpacity(1.0);
  p.setPen(QPen(blackColor(), 10));
  p.drawRoundedRect(roadNameRect, 24, 24);

  p.setFont(font);
  p.setPen(QPen(whiteColor(), 6));
  p.drawText(roadNameRect, Qt::AlignCenter, roadName);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintSpeedLimit(QPainter &p) {
  if (setSpeedRect.isEmpty()) {
    return;
  }

  p.save();

  QString speedLimitStr = (speedLimit > 1) ? QString::number(std::nearbyint(speedLimit)) : "–";

  bool hasUsSpeedLimit = !frogpilot_toggles.value("speed_limit_vienna").toBool();
  bool hasEuSpeedLimit = !hasUsSpeedLimit;

  int euSignSize = 176;
  int usSignHeight = 186;
  int signMargin = 12;

  if (hasUsSpeedLimit) {
    speedLimitHeight = usSignHeight + signMargin;
  } else if (hasEuSpeedLimit) {
    speedLimitHeight = euSignSize + signMargin;
  }

  QRect signRect;
  if (hasUsSpeedLimit) {
    signRect = QRect(setSpeedRect.x() + signMargin, setSpeedRect.bottom() - speedLimitHeight, setSpeedRect.width() - 2 * signMargin, usSignHeight);
  } else if (hasEuSpeedLimit) {
    signRect = QRect(setSpeedRect.x() + signMargin, setSpeedRect.bottom() - speedLimitHeight, setSpeedRect.width() - 2 * signMargin, euSignSize);
  }
  speedLimitRect = signRect;

  if (hasUsSpeedLimit) {
    p.setPen(Qt::NoPen);
    p.setBrush(whiteColor());
    p.drawRoundedRect(signRect, 24, 24);
    p.setPen(QPen(blackColor(), 6));
    p.drawRoundedRect(signRect.adjusted(9, 9, -9, -9), 16, 16);

    p.setOpacity(slcOverriddenSpeed == 0 ? 1.0 : 0.25);
    if (slcOverriddenSpeed == 0 && frogpilot_toggles.value("show_speed_limit_offset").toBool()) {
      p.setFont(InterFont(28, QFont::DemiBold));
      p.drawText(signRect.adjusted(0, 22, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("LIMIT"));
      p.setFont(InterFont(70, QFont::Bold));
      p.drawText(signRect.adjusted(0, 51, 0, 0), Qt::AlignTop | Qt::AlignHCenter, speedLimitStr);
      p.setFont(InterFont(50, QFont::DemiBold));
      p.drawText(signRect.adjusted(0, 120, 0, 0), Qt::AlignTop | Qt::AlignHCenter, speedLimitOffsetStr);
    } else {
      p.setFont(InterFont(28, QFont::DemiBold));
      p.drawText(signRect.adjusted(0, 22, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("SPEED"));
      p.drawText(signRect.adjusted(0, 51, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("LIMIT"));
      p.setFont(InterFont(70, QFont::Bold));
      p.drawText(signRect.adjusted(0, 85, 0, 0), Qt::AlignTop | Qt::AlignHCenter, speedLimitStr);
    }
  }

  if (hasEuSpeedLimit) {
    p.setPen(Qt::NoPen);
    p.setBrush(whiteColor());
    p.drawEllipse(signRect);
    p.setPen(QPen(Qt::red, 20));
    p.drawEllipse(signRect.adjusted(16, 16, -16, -16));

    p.setOpacity(slcOverriddenSpeed == 0 ? 1.0 : 0.25);
    p.setPen(blackColor());
    if (frogpilot_toggles.value("show_speed_limit_offset").toBool()) {
      p.setFont(InterFont((speedLimitStr.size() >= 3) ? 60 : 70, QFont::Bold));
      p.drawText(signRect.adjusted(0, -25, 0, 0), Qt::AlignCenter, speedLimitStr);
      p.setFont(InterFont(40, QFont::DemiBold));
      p.drawText(signRect.adjusted(0, 100, 0, 0), Qt::AlignTop | Qt::AlignHCenter, speedLimitOffsetStr);
    } else {
      p.setFont(InterFont((speedLimitStr.size() >= 3) ? 60 : 70, QFont::Bold));
      p.drawText(signRect, Qt::AlignCenter, speedLimitStr);
    }
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintSpeedLimitSources(QPainter &p) {
  p.save();

  std::function<void(QRect&, QPixmap&, const QString&, const double)> drawSource = [&](QRect &rect, QPixmap &icon, const QString &title, double speedLimitValue) {
    bool isActive = QString::fromUtf8(speedLimitSource.c_str()) == title && speedLimitValue != 0;

    if (isActive) {
      p.setBrush(redColor(166));
      p.setFont(InterFont(35, QFont::Bold));
      p.setPen(QPen(redColor(), 10));
    } else {
      p.setBrush(blackColor(166));
      p.setFont(InterFont(35, QFont::DemiBold));
      p.setPen(QPen(blackColor(), 10));
    }

    QSize size(img_size / 4, img_size / 4);
    QRect iconRect = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignLeft | Qt::AlignVCenter, size, rect.adjusted(20, 0, 0, 0));

    QString speedText;
    if (speedLimitValue != 0) {
      speedText = QString::number(std::nearbyint(speedLimitValue)) + speedUnit;
    } else {
      speedText = "N/A";
    }

    QString fullText = tr(title.toUtf8().constData()) + " - " + speedText;

    p.setOpacity(1.0);
    p.drawRoundedRect(rect, 24, 24);
    p.drawPixmap(iconRect, icon);

    p.setPen(QPen(whiteColor(), 6));
    QRect textRect(iconRect.right() + 10, rect.y(), rect.width() - iconRect.width() - 30, rect.height());

    if (isActive) {
      QFontMetrics fm(p.font());
      int textYPosition = textRect.y() + (textRect.height() - fm.height()) / 2 + fm.ascent();

      QPainterPath path;
      path.addText(textRect.x(), textYPosition, p.font(), fullText);
      p.strokePath(path, QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.drawText(textRect.x(), textYPosition, fullText);
    } else {
      p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fullText);
    }
  };

  int signMargin = 12;

  QRect dashboardRect(speedLimitRect.x() - signMargin, speedLimitRect.y() + speedLimitRect.height() + UI_BORDER_SIZE, 450, 60);
  QRect mapDataRect(dashboardRect.x(), dashboardRect.y() + dashboardRect.height() + UI_BORDER_SIZE / 2, 450, 60);
  QRect mapboxRect(mapDataRect.x(), mapDataRect.y() + mapDataRect.height() + UI_BORDER_SIZE / 2, 450, 60);
  QRect nextLimitRect(mapboxRect.x(), mapboxRect.y() + mapboxRect.height() + UI_BORDER_SIZE / 2, 450, 60);

  drawSource(dashboardRect, dashboardIcon, "Dashboard", dashboardSpeedLimit * speedConversion);
  drawSource(mapDataRect, mapDataIcon, "Map Data", mapSpeedLimit * speedConversion);
  drawSource(mapboxRect, mapboxIcon, "Mapbox", mapboxSpeedLimit * speedConversion);
  drawSource(nextLimitRect, nextMapsIcon, "Upcoming", nextSpeedLimit * speedConversion);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintStandstillTimer(QPainter &p) {
  p.save();

  const int minutes = standstillDuration / 60;
  const int seconds = standstillDuration % 60;
  const QString timer_text = QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));

  const int group_top = rect().height() - 246;
  const int center_x = rect().center().x();

  p.setFont(InterFont(132, QFont::Bold));
  p.setPen(whiteColor());

  QRect timer_rect(center_x - 260, group_top - 6, 520, 150);
  p.drawText(timer_rect, Qt::AlignHCenter | Qt::AlignBottom, timer_text);

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintFakeLongTestUI(QPainter &p) {
  p.save();

  const int card_width = 272;
  const int card_height = 116;
  const int card_gap = 28;
  const int fake_long_y_offset = 96;
  const int card_top = rect().height() - 398 - fake_long_y_offset;
  const int center_x = rect().center().x();
  const int total_width = (card_width * 2) + card_gap;
  const QRect left_card(center_x - (total_width / 2), card_top, card_width, card_height);
  const QRect right_card(left_card.right() + card_gap + 1, card_top, card_width, card_height);
  const int button_width = 204;
  const int button_height = 78;
  const int button_gap = 18;
  const int button_left = 56;
  const int button_top = card_top - 256;

  auto format_speed = [](float value) {
    return value > 0.1f ? QString::number(std::nearbyint(value)) : "–";
  };

  auto is_active_button = [&](const QString &button_key) {
    return fakeLongActiveButton == button_key && fakeLongButtonTimer.isValid() && fakeLongButtonTimer.elapsed() < 240;
  };

  auto draw_card = [&](const QRect &card, const QString &title, const QString &value,
                       const QString &status, const QColor &accent, const QColor &fill, bool highlighted) {
    p.setPen(QPen(highlighted ? accent : QColor(255, 255, 255, 42), highlighted ? 3 : 2));
    p.setBrush(fill);
    p.drawRoundedRect(card, 28, 28);

    p.setPen(highlighted ? QColor(240, 247, 255, 224) : QColor(210, 217, 226, 212));
    p.setFont(InterFont(25, QFont::DemiBold));
    p.drawText(card.adjusted(0, 14, 0, 0), Qt::AlignHCenter | Qt::AlignTop, title);

    p.setPen(highlighted ? QColor(255, 255, 255) : whiteColor());
    p.setFont(InterFont(64, QFont::Bold));
    p.drawText(card.adjusted(0, 18, 0, -6), Qt::AlignHCenter | Qt::AlignBottom, value);

    QRect status_rect(card.left() + 24, card.bottom() - 42, card.width() - 48, 28);
    p.setPen(highlighted ? accent : QColor(220, 220, 220, 180));
    p.setFont(InterFont(22, QFont::DemiBold));
    p.drawText(status_rect, Qt::AlignHCenter | Qt::AlignVCenter, status);
  };

  auto draw_status_chip = [&](const QRect &chip_rect, const QString &label, const QString &value, const QColor &accent) {
    p.setPen(QPen(QColor(255, 255, 255, 28), 2));
    p.setBrush(QColor(0, 0, 0, 148));
    p.drawRoundedRect(chip_rect, 22, 22);

    p.setPen(QColor(210, 217, 226, 180));
    p.setFont(InterFont(21, QFont::DemiBold));
    p.drawText(chip_rect.adjusted(0, 10, 0, 0), Qt::AlignHCenter | Qt::AlignTop, label);

    p.setPen(accent);
    p.setFont(InterFont(29, QFont::Bold));
    p.drawText(chip_rect.adjusted(0, 0, 0, -10), Qt::AlignHCenter | Qt::AlignBottom, value);
  };

  auto draw_button = [&](QRect &button_rect, const QString &label, const QString &button_key) {
    const bool active = is_active_button(button_key);
    p.setPen(QPen(active ? QColor(110, 220, 255, 220) : QColor(255, 255, 255, 42), active ? 3 : 2));
    p.setBrush(active ? QColor(20, 88, 120, 196) : QColor(0, 0, 0, 138));
    p.drawRoundedRect(button_rect, 26, 26);

    p.setPen(active ? QColor(220, 246, 255) : QColor(255, 255, 255, 224));
    p.setFont(InterFont(33, QFont::Bold));
    p.drawText(button_rect, Qt::AlignCenter, label);
  };

  if (showFakeLongButtons) {
    fakeLongMainRect = QRect(button_left, button_top, button_width, button_height);
    fakeLongCancelRect = QRect(button_left, fakeLongMainRect.bottom() + button_gap + 1, button_width, button_height);
    fakeLongResRect = QRect(button_left, fakeLongCancelRect.bottom() + button_gap + 1, button_width, button_height);
    fakeLongSetRect = QRect(button_left, fakeLongResRect.bottom() + button_gap + 1, button_width, button_height);

    draw_button(fakeLongMainRect, tr("MAIN"), "main");
    draw_button(fakeLongCancelRect, tr("CANCEL"), "cancel");
    draw_button(fakeLongResRect, tr("RES"), "res");
    draw_button(fakeLongSetRect, tr("SET"), "set");
  } else {
    fakeLongMainRect = QRect();
    fakeLongCancelRect = QRect();
    fakeLongResRect = QRect();
    fakeLongSetRect = QRect();
  }

  const float fake_sync_delta = fakeLongApplySpeed - fakeLongCurrentSpeed;
  const bool fake_syncing_up = fakeLongArmed && !fakeLongPaused && fake_sync_delta > 0.5f;
  const bool fake_syncing_down = fakeLongArmed && !fakeLongPaused && fake_sync_delta < -0.5f;
  const bool fake_ready = fakeLongArmed && !fakeLongPaused && !fake_syncing_up && !fake_syncing_down;

  QString fake_status = tr("OFF");
  QColor fake_accent(220, 220, 220, 220);
  QColor fake_fill(0, 0, 0, 144);
  bool fake_highlight = false;

  if (fakeLongPaused) {
    fake_status = tr("PAUSED");
    fake_accent = QColor(255, 194, 92, 235);
    fake_fill = QColor(74, 48, 10, 184);
    fake_highlight = true;
  } else if (fake_syncing_up) {
    fake_status = tr("UP");
    fake_accent = QColor(110, 235, 160, 235);
    fake_fill = QColor(18, 78, 44, 184);
    fake_highlight = true;
  } else if (fake_syncing_down) {
    fake_status = tr("DOWN");
    fake_accent = QColor(108, 196, 255, 235);
    fake_fill = QColor(14, 52, 88, 184);
    fake_highlight = true;
  } else if (fake_ready) {
    fake_status = tr("READY");
    fake_accent = QColor(255, 255, 255, 220);
    fake_fill = QColor(0, 0, 0, 144);
  }

  draw_card(left_card, tr("FAKE"), format_speed(fakeLongApplySpeed), fake_status, fake_accent, fake_fill, fake_highlight);
  draw_card(right_card, tr("ACC"), format_speed(fakeLongCurrentSpeed), tr("LIVE"), QColor(255, 255, 255, 200), QColor(0, 0, 0, 144), false);

  const int chip_width = 182;
  const int chip_height = 84;
  const int chip_gap = 18;
  const int chip_top = left_card.bottom() + 18;
  const int chip_total = (chip_width * 4) + (chip_gap * 3);
  const int chip_left = center_x - (chip_total / 2);

  const QRect armed_chip(chip_left, chip_top, chip_width, chip_height);
  const QRect paused_chip(armed_chip.right() + chip_gap + 1, chip_top, chip_width, chip_height);
  const QRect target_chip(paused_chip.right() + chip_gap + 1, chip_top, chip_width, chip_height);
  const QRect last_chip(target_chip.right() + chip_gap + 1, chip_top, chip_width, chip_height);

  draw_status_chip(armed_chip, tr("ARMED"), fakeLongArmed ? tr("ON") : tr("OFF"), fakeLongArmed ? QColor(134, 233, 164) : QColor(220, 220, 220));
  draw_status_chip(paused_chip, tr("PAUSED"), fakeLongPaused ? tr("YES") : tr("NO"), fakeLongPaused ? QColor(255, 198, 106) : QColor(220, 220, 220));
  draw_status_chip(target_chip, tr("TARGET"), format_speed(fakeLongTargetSpeed), QColor(255, 255, 255));
  draw_status_chip(last_chip, tr("LAST"), fakeLongLastButton.isEmpty() ? "–" : fakeLongLastButton, QColor(136, 214, 255));

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintStoppingPoint(QPainter &p) {
  p.save();

  QPointF centerPoint = (track_vertices.first() + track_vertices.last()) / 2.0f;
  QPointF stopSignPosition = centerPoint - QPointF(stopSignImg.width() / 2.0f, stopSignImg.height());
  p.drawPixmap(stopSignPosition, stopSignImg);

  if (frogpilot_toggles.value("show_stopping_point_metrics").toBool()) {
    float distance = stoppingDistance * distanceConversion;
    QString distanceText = QString::number(std::nearbyint(distance)) + leadDistanceUnit;

    QFont font = InterFont(45, QFont::DemiBold);
    QFontMetrics fm(font);

    QPointF textPosition(centerPoint.x() - fm.horizontalAdvance(distanceText) / 2.0f, centerPoint.y() - stopSignImg.height() - fm.ascent());

    QPainterPath path;
    path.addText(textPosition, font, distanceText);
    p.strokePath(path, QPen(Qt::black, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    p.setFont(font);
    p.setPen(whiteColor());
    p.drawText(textPosition, distanceText);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintTurnSignals(QPainter &p) {
  int frameIndex = qBound(0, animationFrameIndex, totalFrames - 1);

  bool blindspotActive = blinkerLeft ? blindspotLeft : blindspotRight;

  int signalXPosition = 0;
  int signalYPosition = 0;

  if (signalStyle == "static") {
    signalXPosition = blinkerLeft ? (rect().center().x() * 0.75) - signalWidth : rect().center().x() * 1.25;
    signalYPosition = signalHeight / 2;
  } else {
    if (signalStyle == "traditional_gif") {
      signalXPosition = blinkerLeft ? width() - (frameIndex * signalMovement) + signalWidth : (frameIndex * signalMovement) - signalWidth;
    } else {
      signalXPosition = blinkerLeft ? width() - ((frameIndex + 1) * signalWidth) : frameIndex * signalWidth;
    }
    signalYPosition = height() - signalHeight - alertHeight;
  }

  if (blinkerLeft) {
    QPixmap &imgToDraw = (blindspotActive && !blindspotImages.empty()) ? blindspotImages[0] : signalImages[frameIndex];
    p.drawPixmap(signalXPosition, signalYPosition, signalWidth, signalHeight, imgToDraw);
  } else {
    QPixmap &imgToDraw = (blindspotActive && !blindspotImagesRight.empty()) ? blindspotImagesRight[0] : signalImagesRight[frameIndex];
    p.drawPixmap(signalXPosition, signalYPosition, signalWidth, signalHeight, imgToDraw);
  }
}

void FrogPilotAnnotatedCameraWidget::paintWeather(QPainter &p) {
  if (weatherId == 0) {
    return;
  }

  p.save();

  QPoint weatherIconPosition;
  if (compassPosition != QPoint(0, 0)) {
    weatherIconPosition = compassPosition;
    weatherIconPosition.rx() += (rightHandDM ? UI_BORDER_SIZE + widget_size + UI_BORDER_SIZE : -UI_BORDER_SIZE - widget_size - UI_BORDER_SIZE);
  } else {
    weatherIconPosition.rx() = rightHandDM ? UI_BORDER_SIZE + widget_size / 2 : width() - UI_BORDER_SIZE - btn_size;
    weatherIconPosition.ry() = dmIconPosition.y() - widget_size / 2;
  }

  QRect weatherRect(weatherIconPosition, QSize(widget_size, widget_size));

  p.setBrush(blackColor(166));
  p.setPen(QPen(blackColor(), 10));
  p.drawRoundedRect(weatherRect, 24, 24);

  QSharedPointer<QMovie> icon = weatherDaytime ? weatherClearDay : weatherClearNight;
  if ((weatherId >= 200 && weatherId <= 232) || (weatherId >= 300 && weatherId <= 321) || (weatherId >= 500 && weatherId <= 531)) {
    icon = weatherRain;
  } else if (weatherId >= 600 && weatherId <= 622) {
    icon = weatherSnow;
  } else if (weatherId >= 701 && weatherId <= 762) {
    icon = weatherLowVisibility;
  }

  p.drawPixmap(weatherRect, icon->currentPixmap());

  p.restore();
}
