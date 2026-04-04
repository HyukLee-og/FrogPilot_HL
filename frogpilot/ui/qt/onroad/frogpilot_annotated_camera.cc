#include "frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h"

#include <QDateTime>
#include <QPainterPath>
#include <QTransform>

#include "common/util.h"

namespace {

enum APNSdiStyle {
  APN_SDI_STYLE_NONE = 0,
  APN_SDI_STYLE_ENFORCEMENT = 1,
  APN_SDI_STYLE_ZONE = 2,
  APN_SDI_STYLE_WARNING = 3,
  APN_SDI_STYLE_INFO = 4,
  APN_SDI_STYLE_RESTRICTION = 5,
};

QString apnSdiLabelForType(int type) {
  switch (type) {
    case 0: return QStringLiteral("신호과속");
    case 1: return QStringLiteral("과속");
    case 2: return QStringLiteral("구간단속 시작");
    case 3: return QStringLiteral("구간단속 끝");
    case 4: return QStringLiteral("구간단속중");
    case 5: return QStringLiteral("꼬리물기 단속");
    case 6: return QStringLiteral("신호 단속");
    case 7: return QStringLiteral("이동식 과속");
    case 8: return QStringLiteral("박스형 과속");
    case 9: return QStringLiteral("버스전용차로");
    case 10: return QStringLiteral("가변 차로 단속");
    case 11: return QStringLiteral("갓길 감시");
    case 12: return QStringLiteral("끼어들기 금지");
    case 13: return QStringLiteral("교통정보 수집");
    case 14: return QStringLiteral("방범용 CCTV");
    case 15: return QStringLiteral("과적 위험구간");
    case 16: return QStringLiteral("적재 불량 단속");
    case 17: return QStringLiteral("주차단속 지점");
    case 18: return QStringLiteral("일방통행도로");
    case 19: return QStringLiteral("철길 건널목");
    case 20: return QStringLiteral("어린이 보호구역 시작");
    case 21: return QStringLiteral("어린이 보호구역 끝");
    case 22: return QStringLiteral("과속방지턱");
    case 23: return QStringLiteral("LPG 충전소");
    case 24: return QStringLiteral("터널 구간");
    case 25: return QStringLiteral("휴게소");
    case 26: return QStringLiteral("톨게이트");
    case 27: return QStringLiteral("안개주의 지역");
    case 28: return QStringLiteral("유해물질 지역");
    case 29: return QStringLiteral("사고다발");
    case 30: return QStringLiteral("급커브지역");
    case 31: return QStringLiteral("급커브구간");
    case 32: return QStringLiteral("급경사구간");
    case 33: return QStringLiteral("야생동물 사고구간");
    case 34: return QStringLiteral("우측 시야불량");
    case 35: return QStringLiteral("시야불량지점");
    case 36: return QStringLiteral("좌측 시야불량");
    case 37: return QStringLiteral("신호위반다발");
    case 38: return QStringLiteral("과속다발");
    case 39: return QStringLiteral("교통혼잡지역");
    case 40: return QStringLiteral("차로선택지점");
    case 41: return QStringLiteral("무단횡단 사고다발");
    case 42: return QStringLiteral("갓길 사고다발");
    case 43: return QStringLiteral("과속 사고다발");
    case 44: return QStringLiteral("졸음 사고다발");
    case 45: return QStringLiteral("사고다발지점");
    case 46: return QStringLiteral("보행자 사고다발");
    case 47: return QStringLiteral("차량도난 상습발생");
    case 48: return QStringLiteral("낙석주의지역");
    case 49: return QStringLiteral("결빙주의지역");
    case 50: return QStringLiteral("병목지점");
    case 51: return QStringLiteral("합류 도로");
    case 52: return QStringLiteral("추락주의지역");
    case 53: return QStringLiteral("지하차도 구간");
    case 54: return QStringLiteral("교통진정지역");
    case 55: return QStringLiteral("인터체인지");
    case 56: return QStringLiteral("분기점");
    case 57: return QStringLiteral("LPG 휴게소");
    case 58: return QStringLiteral("교량");
    case 59: return QStringLiteral("제동장치 사고다발");
    case 60: return QStringLiteral("중앙선침범 사고다발");
    case 61: return QStringLiteral("통행위반 사고다발");
    case 62: return QStringLiteral("목적지 건너편");
    case 63: return QStringLiteral("졸음쉼터");
    case 64: return QStringLiteral("노후경유차 단속");
    case 65: return QStringLiteral("터널내 차로변경 단속");
    case 66: return QStringLiteral("장애인 보호구역 시작");
    case 67: return QStringLiteral("장애인 보호구역 끝");
    case 68: return QStringLiteral("노인 보호구역 시작");
    case 69: return QStringLiteral("노인 보호구역 끝");
    case 70: return QStringLiteral("마을주민 보호구역 시작");
    case 71: return QStringLiteral("마을주민 보호구역 끝");
    case 72: return QStringLiteral("트럭 높이 제한");
    case 73: return QStringLiteral("트럭 중량 제한");
    case 74: return QStringLiteral("트럭 폭 제한");
    case 75: return QStringLiteral("후면 과속카메라");
    case 76: return QStringLiteral("후면 신호단속카메라");
    case 77: return QStringLiteral("홍수 주의구간");
    case 78: return QStringLiteral("댐 방류 주의");
    case 79: return QStringLiteral("보행자 우선도로 시작");
    case 80: return QStringLiteral("보행자 우선도로 끝");
    case 81: return QStringLiteral("침수 심각구간");
    case 82: return QStringLiteral("산사태 위험구간");
    case 83: return QStringLiteral("침수 주의구간");
    case 84: return QStringLiteral("가변 구간단속 시작");
    case 85: return QStringLiteral("가변 구간단속 끝");
    default: return QString();
  }
}

int apnSdiStyleForType(int type) {
  switch (type) {
    case 0:
    case 1:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 16:
    case 17:
    case 64:
    case 65:
    case 75:
    case 76:
      return APN_SDI_STYLE_ENFORCEMENT;
    case 20:
    case 21:
    case 54:
    case 66:
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 79:
    case 80:
      return APN_SDI_STYLE_ZONE;
    case 18:
    case 19:
    case 22:
    case 15:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 51:
    case 52:
    case 53:
    case 59:
    case 60:
    case 61:
    case 77:
    case 78:
    case 81:
    case 82:
    case 83:
      return APN_SDI_STYLE_WARNING;
    case 23:
    case 24:
    case 25:
    case 26:
    case 13:
    case 14:
    case 55:
    case 56:
    case 57:
    case 58:
    case 62:
    case 63:
      return APN_SDI_STYLE_INFO;
    case 72:
    case 73:
    case 74:
      return APN_SDI_STYLE_RESTRICTION;
    default:
      return APN_SDI_STYLE_NONE;
  }
}

QString apnSdiCategoryLabelForStyle(int style) {
  switch (style) {
    case APN_SDI_STYLE_ENFORCEMENT: return QStringLiteral("단속");
    case APN_SDI_STYLE_ZONE: return QStringLiteral("보호구역");
    case APN_SDI_STYLE_WARNING: return QStringLiteral("주의");
    case APN_SDI_STYLE_INFO: return QStringLiteral("안내");
    case APN_SDI_STYLE_RESTRICTION: return QStringLiteral("제한");
    default: return QString();
  }
}

QColor apnSdiAccentColorForStyle(int style) {
  switch (style) {
    case APN_SDI_STYLE_ENFORCEMENT: return QColor(201, 34, 49, 235);
    case APN_SDI_STYLE_ZONE: return QColor(231, 123, 34, 235);
    case APN_SDI_STYLE_WARNING: return QColor(212, 170, 36, 235);
    case APN_SDI_STYLE_INFO: return QColor(41, 88, 82, 235);
    case APN_SDI_STYLE_RESTRICTION: return QColor(33, 92, 176, 235);
    default: return QColor(70, 74, 80, 235);
  }
}

int apnTypeFromHazardString(const QString &hazard) {
  const int separator = hazard.lastIndexOf(':');
  if (separator < 0 || separator >= hazard.size() - 1) {
    return 0;
  }

  bool ok = false;
  const int parsed = hazard.mid(separator + 1).trimmed().toInt(&ok);
  return ok ? parsed : 0;
}

bool apnSdiIsSignalEnforcementType(int type) {
  switch (type) {
    case 0:
    case 76:
      return true;
    default:
      return false;
  }
}

bool apnSdiIsPlainSpeedCameraType(int type) {
  switch (type) {
    case 1:
    case 7:
    case 8:
    case 75:
      return true;
    default:
      return false;
  }
}

bool apnSdiIsTextOnlyCameraType(int type) {
  switch (type) {
    case 5:
    case 6:
    case 9:
    case 10:
    case 64:
    case 65:
      return true;
    default:
      return false;
  }
}

QString apnSdiTextOnlyCameraLabel(int type) {
  switch (type) {
    case 5: return QStringLiteral("꼬리물기");
    case 6: return QStringLiteral("신호");
    case 9: return QStringLiteral("버스 차로");
    case 10: return QStringLiteral("가변 차로");
    case 64: return QStringLiteral("노후");
    case 65: return QStringLiteral("차선변경");
    default: return QString();
  }
}

QString apnSdiBadgeForType(int type) {
  switch (type) {
    case 13: return QStringLiteral("정보");
    case 14: return QStringLiteral("CCTV");
    case 20:
    case 21: return QStringLiteral("스쿨");
    case 23: return QStringLiteral("LPG");
    case 24: return QStringLiteral("터널");
    case 25: return QStringLiteral("휴게");
    case 26: return QStringLiteral("TG");
    case 55: return QStringLiteral("IC");
    case 56: return QStringLiteral("JC");
    case 57: return QStringLiteral("LPG");
    case 58: return QStringLiteral("교량");
    case 62: return QStringLiteral("목적지");
    case 63: return QStringLiteral("쉼터");
    case 66:
    case 67: return QStringLiteral("장애");
    case 68:
    case 69: return QStringLiteral("노인");
    case 70:
    case 71: return QStringLiteral("주민");
    case 72: return QStringLiteral("높이");
    case 73: return QStringLiteral("중량");
    case 74: return QStringLiteral("폭");
    case 77:
    case 78:
    case 81:
    case 82:
    case 83: return QStringLiteral("재난");
    default: return QString();
  }
}

QString apnSdiSupportLabelForType(int type) {
  switch (type) {
    case 20:
    case 66:
    case 68:
    case 70:
      return QStringLiteral("전방 진입");
    case 21:
    case 67:
    case 69:
    case 71:
      return QStringLiteral("전방 종료");
    case 23:
      return QStringLiteral("근처 충전 가능");
    case 25:
      return QStringLiteral("휴식 및 편의시설");
    case 26:
      return QStringLiteral("요금소 접근");
    case 55:
    case 56:
      return QStringLiteral("분기 안내");
    case 57:
      return QStringLiteral("LPG 충전 가능");
    case 62:
      return QStringLiteral("건너편 위치");
    case 63:
      return QStringLiteral("휴식 권장");
    case 72:
    case 73:
    case 74:
      return QStringLiteral("차량 제한 확인");
    case 77:
    case 78:
    case 81:
    case 82:
    case 83:
      return QStringLiteral("기상 주의");
    default:
      return QString();
  }
}

QString apnSdiPhaseLabelForType(int type) {
  switch (type) {
    case 20:
    case 66:
    case 68:
    case 70:
    case 79:
      return QStringLiteral("시작");
    case 21:
    case 67:
    case 69:
    case 71:
    case 80:
      return QStringLiteral("종료");
    default:
      return QString();
  }
}

QString apnSdiZoneCoreLabelForType(int type) {
  switch (type) {
    case 20:
    case 21: return QStringLiteral("어린이 보호");
    case 66:
    case 67: return QStringLiteral("장애인 보호");
    case 68:
    case 69: return QStringLiteral("노인 보호");
    case 70:
    case 71: return QStringLiteral("주민 보호");
    case 79:
    case 80: return QStringLiteral("보행자 우선");
    default: return QString();
  }
}

bool apnSdiZoneUsesTextOnlySign(int type) {
  switch (type) {
    case 20:
    case 21:
    case 66:
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
      return true;
    default:
      return false;
  }
}

QString apnSdiZoneTextOnlySignText(int type) {
  switch (type) {
    case 20:
    case 21:
      return QStringLiteral("어린이\n보호");
    case 66:
    case 67:
      return QStringLiteral("장애인\n보호");
    case 68:
    case 69:
      return QStringLiteral("노인\n보호");
    case 70:
    case 71:
      return QStringLiteral("주민\n보호");
    default:
      return QStringLiteral("보호구역");
  }
}

QString apnSdiWarningFallbackTextForType(int type) {
  switch (type) {
    case 18: return QStringLiteral("일방");
    case 19: return QStringLiteral("철길");
    case 22: return QStringLiteral("방지턱");
    case 27: return QStringLiteral("안개");
    case 28: return QStringLiteral("유해");
    case 29:
    case 45: return QStringLiteral("사고");
    case 30:
    case 31: return QStringLiteral("커브");
    case 32: return QStringLiteral("급경사");
    case 33: return QStringLiteral("야생");
    case 34:
    case 35:
    case 36: return QStringLiteral("시야");
    case 37: return QStringLiteral("신호");
    case 38:
    case 43: return QStringLiteral("과속");
    case 39: return QStringLiteral("혼잡");
    case 40: return QStringLiteral("차로");
    case 41:
    case 46: return QStringLiteral("보행");
    case 42: return QStringLiteral("갓길");
    case 44: return QStringLiteral("졸음");
    case 47: return QStringLiteral("도난");
    case 48: return QStringLiteral("낙석");
    case 49: return QStringLiteral("결빙");
    case 50: return QStringLiteral("병목");
    case 51: return QStringLiteral("합류");
    case 52: return QStringLiteral("추락");
    case 53: return QStringLiteral("지하");
    case 59: return QStringLiteral("제동");
    case 60: return QStringLiteral("중앙");
    case 61: return QStringLiteral("위반");
    case 77: return QStringLiteral("홍수");
    case 78: return QStringLiteral("방류");
    case 81:
    case 83: return QStringLiteral("침수");
    case 82: return QStringLiteral("산사");
    default: return QStringLiteral("주의");
  }
}

QString apnSdiInfoShortTitleForType(int type) {
  switch (type) {
    case 13: return QStringLiteral("정보");
    case 14: return QStringLiteral("CCTV");
    case 23: return QStringLiteral("LPG");
    case 24: return QStringLiteral("터널");
    case 25: return QStringLiteral("휴게소");
    case 26: return QStringLiteral("TG");
    case 55: return QStringLiteral("IC");
    case 56: return QStringLiteral("JC");
    case 57: return QStringLiteral("LPG 휴게");
    case 58: return QStringLiteral("교량");
    case 62: return QStringLiteral("건너편");
    case 63: return QStringLiteral("졸음쉼터");
    default: return QString();
  }
}

QString apnSdiRestrictionShortTextForType(int type) {
  switch (type) {
    case 72: return QStringLiteral("높이");
    case 73: return QStringLiteral("중량");
    case 74: return QStringLiteral("폭");
    default: return QStringLiteral("제한");
  }
}

QPainterPath apnPentagonSignPath(const QRect &rect) {
  const qreal left = rect.left();
  const qreal right = rect.right();
  const qreal top = rect.top();
  const qreal bottom = rect.bottom();
  const qreal mid_x = rect.center().x();

  QPainterPath path;
  path.moveTo(mid_x, top);
  path.lineTo(right - rect.width() * 0.08, top + rect.height() * 0.23);
  path.lineTo(right - rect.width() * 0.12, bottom - rect.height() * 0.12);
  path.lineTo(left + rect.width() * 0.12, bottom - rect.height() * 0.12);
  path.lineTo(left + rect.width() * 0.08, top + rect.height() * 0.23);
  path.closeSubpath();
  return path;
}

void apnDrawStickPerson(QPainter &p, QPointF center, qreal scale, const QColor &color, bool cane = false) {
  QPen pen(color);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  pen.setWidthF(6.0 * scale);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);

  p.drawEllipse(center + QPointF(0.0, -20.0 * scale), 7.0 * scale, 7.0 * scale);
  p.drawLine(center + QPointF(0.0, -11.0 * scale), center + QPointF(0.0, 13.0 * scale));
  p.drawLine(center + QPointF(0.0, -4.0 * scale), center + QPointF(-12.0 * scale, 8.0 * scale));
  p.drawLine(center + QPointF(0.0, -4.0 * scale), center + QPointF(12.0 * scale, 4.0 * scale));
  p.drawLine(center + QPointF(0.0, 13.0 * scale), center + QPointF(-11.0 * scale, 28.0 * scale));
  p.drawLine(center + QPointF(0.0, 13.0 * scale), center + QPointF(11.0 * scale, 28.0 * scale));
  if (cane) {
    p.drawLine(center + QPointF(14.0 * scale, 6.0 * scale), center + QPointF(14.0 * scale, 30.0 * scale));
  }
}

void apnDrawChildrenZoneGlyph(QPainter &p, const QRect &rect) {
  p.save();
  apnDrawStickPerson(p, QPointF(rect.center().x() - rect.width() * 0.16, rect.center().y() + rect.height() * 0.02), 1.0, Qt::white);
  apnDrawStickPerson(p, QPointF(rect.center().x() + rect.width() * 0.10, rect.center().y() + rect.height() * 0.05), 0.92, Qt::white);
  QPen hand_pen(Qt::white);
  hand_pen.setWidth(5);
  hand_pen.setCapStyle(Qt::RoundCap);
  p.setPen(hand_pen);
  p.drawLine(QPointF(rect.center().x() - rect.width() * 0.02, rect.center().y() - rect.height() * 0.02),
             QPointF(rect.center().x() + rect.width() * 0.07, rect.center().y() - rect.height() * 0.01));
  p.restore();
}

void apnDrawElderlyZoneGlyph(QPainter &p, const QRect &rect) {
  p.save();
  apnDrawStickPerson(p, QPointF(rect.center().x() - rect.width() * 0.12, rect.center().y() + rect.height() * 0.04), 1.0, Qt::white, true);
  apnDrawStickPerson(p, QPointF(rect.center().x() + rect.width() * 0.12, rect.center().y() + rect.height() * 0.02), 0.88, Qt::white);
  p.restore();
}

void apnDrawDisabledZoneGlyph(QPainter &p, const QRect &rect) {
  p.save();
  QPen pen(Qt::white);
  pen.setWidth(7);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);

  const QPointF base(rect.center().x(), rect.center().y() + rect.height() * 0.03);
  p.drawEllipse(base + QPointF(-20, 12), 22, 22);
  p.drawEllipse(base + QPointF(-6, -34), 8, 8);
  p.drawLine(base + QPointF(0, -25), base + QPointF(0, -2));
  p.drawLine(base + QPointF(0, -6), base + QPointF(22, 4));
  p.drawLine(base + QPointF(0, -2), base + QPointF(-10, 16));
  p.drawLine(base + QPointF(0, -2), base + QPointF(18, 20));
  p.drawLine(base + QPointF(18, 20), base + QPointF(2, 20));
  p.restore();
}

void apnDrawResidentZoneGlyph(QPainter &p, const QRect &rect) {
  p.save();
  apnDrawStickPerson(p, QPointF(rect.center().x() - rect.width() * 0.18, rect.center().y() + rect.height() * 0.06), 0.78, Qt::white);
  apnDrawStickPerson(p, QPointF(rect.center().x(), rect.center().y() - rect.height() * 0.02), 1.02, Qt::white);
  apnDrawStickPerson(p, QPointF(rect.center().x() + rect.width() * 0.18, rect.center().y() + rect.height() * 0.04), 0.78, Qt::white);
  p.restore();
}

void apnDrawPedestrianZoneGlyph(QPainter &p, const QRect &rect) {
  p.save();
  QPen stripe_pen(Qt::white);
  stripe_pen.setWidth(6);
  stripe_pen.setCapStyle(Qt::RoundCap);
  p.setPen(stripe_pen);
  for (int i = -2; i <= 2; ++i) {
    const int x = rect.center().x() + i * 16;
    p.drawLine(QPointF(x - 8, rect.bottom() - 30), QPointF(x + 2, rect.bottom() - 10));
  }
  apnDrawStickPerson(p, QPointF(rect.center().x(), rect.center().y() - rect.height() * 0.02), 1.05, Qt::white);
  p.restore();
}

void apnDrawZoneGlyph(QPainter &p, const QRect &rect, int type) {
  switch (type) {
    case 20:
    case 21:
      apnDrawChildrenZoneGlyph(p, rect);
      break;
    case 66:
    case 67:
      apnDrawDisabledZoneGlyph(p, rect);
      break;
    case 68:
    case 69:
      apnDrawElderlyZoneGlyph(p, rect);
      break;
    case 70:
    case 71:
      apnDrawResidentZoneGlyph(p, rect);
      break;
    case 79:
    case 80:
      apnDrawPedestrianZoneGlyph(p, rect);
      break;
    default:
      break;
  }
}

void apnDrawWarningGlyph(QPainter &p, const QRect &rect, int type) {
  p.save();
  QPen pen(QColor(24, 24, 24));
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  pen.setWidth(8);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);

  switch (type) {
    case 19: {
      p.drawLine(QPointF(rect.center().x(), rect.top() + 18), QPointF(rect.center().x(), rect.bottom() - 12));
      p.drawLine(QPointF(rect.center().x() - 28, rect.center().y() - 16), QPointF(rect.center().x() + 28, rect.center().y() + 20));
      p.drawLine(QPointF(rect.center().x() + 28, rect.center().y() - 16), QPointF(rect.center().x() - 28, rect.center().y() + 20));
      break;
    }
    case 22: {
      QPainterPath path;
      path.moveTo(rect.left() + 18, rect.center().y() + 18);
      path.cubicTo(rect.left() + 36, rect.center().y() - 4, rect.center().x() - 8, rect.center().y() - 4, rect.center().x() + 4, rect.center().y() + 18);
      path.cubicTo(rect.center().x() + 20, rect.center().y() + 36, rect.right() - 28, rect.center().y() + 4, rect.right() - 18, rect.center().y() + 16);
      p.drawPath(path);
      break;
    }
    case 24:
    case 53:
    case 58: {
      p.drawArc(QRect(rect.left() + 24, rect.center().y() - 8, rect.width() - 48, rect.height() * 0.5), 0, 180 * 16);
      p.drawLine(QPointF(rect.left() + 30, rect.bottom() - 18), QPointF(rect.right() - 30, rect.bottom() - 18));
      break;
    }
    case 30:
    case 31: {
      QPainterPath path;
      path.moveTo(rect.left() + 32, rect.bottom() - 28);
      path.cubicTo(rect.center().x() - 10, rect.center().y() + 30, rect.center().x() - 4, rect.center().y() - 4, rect.right() - 34, rect.top() + 28);
      p.drawPath(path);
      p.drawLine(QPointF(rect.right() - 34, rect.top() + 28), QPointF(rect.right() - 54, rect.top() + 36));
      p.drawLine(QPointF(rect.right() - 34, rect.top() + 28), QPointF(rect.right() - 42, rect.top() + 48));
      break;
    }
    case 32: {
      p.drawLine(QPointF(rect.left() + 24, rect.bottom() - 20), QPointF(rect.right() - 22, rect.top() + 28));
      p.drawLine(QPointF(rect.left() + 24, rect.bottom() - 20), QPointF(rect.left() + 52, rect.bottom() - 20));
      break;
    }
    case 48:
    case 82: {
      QPolygon cliff;
      cliff << QPoint(rect.left() + 36, rect.bottom() - 18)
            << QPoint(rect.center().x() - 8, rect.top() + 22)
            << QPoint(rect.center().x() + 10, rect.top() + 18)
            << QPoint(rect.right() - 34, rect.bottom() - 18);
      p.drawPolyline(cliff);
      p.setBrush(QColor(24, 24, 24));
      p.drawEllipse(QPointF(rect.right() - 50, rect.center().y()), 5, 5);
      p.drawEllipse(QPointF(rect.right() - 36, rect.center().y() + 18), 7, 7);
      p.drawEllipse(QPointF(rect.right() - 62, rect.center().y() + 26), 4, 4);
      break;
    }
    case 49: {
      p.drawLine(QPointF(rect.left() + 28, rect.center().y() + 18), QPointF(rect.center().x() - 8, rect.center().y() + 2));
      p.drawLine(QPointF(rect.center().x() + 4, rect.center().y() + 18), QPointF(rect.right() - 28, rect.center().y() + 4));
      p.drawArc(QRect(rect.center().x() - 16, rect.center().y() - 26, 32, 32), 0, 360 * 16);
      break;
    }
    case 50: {
      p.drawLine(QPointF(rect.left() + 34, rect.bottom() - 18), QPointF(rect.center().x() - 8, rect.top() + 28));
      p.drawLine(QPointF(rect.right() - 34, rect.bottom() - 18), QPointF(rect.center().x() + 8, rect.top() + 28));
      break;
    }
    case 51: {
      p.drawLine(QPointF(rect.left() + 42, rect.bottom() - 18), QPointF(rect.left() + 42, rect.top() + 34));
      QPainterPath merge_path;
      merge_path.moveTo(rect.right() - 36, rect.bottom() - 18);
      merge_path.cubicTo(rect.right() - 48, rect.center().y() + 12, rect.center().x() + 12, rect.center().y() - 10, rect.left() + 48, rect.top() + 36);
      p.drawPath(merge_path);
      break;
    }
    case 77:
    case 78:
    case 81:
    case 83: {
      for (int i = 0; i < 3; ++i) {
        const int y = rect.center().y() - 12 + i * 18;
        QPainterPath wave;
        wave.moveTo(rect.left() + 22, y);
        wave.cubicTo(rect.left() + 42, y - 8, rect.left() + 62, y + 8, rect.left() + 82, y);
        wave.cubicTo(rect.left() + 102, y - 8, rect.left() + 122, y + 8, rect.right() - 22, y);
        p.drawPath(wave);
      }
      break;
    }
    default: {
      p.setPen(QColor(24, 24, 24));
      p.setFont(InterFont(24, QFont::Black));
      p.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, apnSdiWarningFallbackTextForType(type));
      break;
    }
  }
  p.restore();
}

void apnDrawInfoGlyph(QPainter &p, const QRect &rect, int type) {
  p.save();
  QPen pen(Qt::white);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  pen.setWidth(8);
  p.setPen(pen);
  p.setBrush(Qt::NoBrush);

  switch (type) {
    case 14: {
      p.drawRoundedRect(QRect(rect.center().x() - 34, rect.center().y() - 12, 68, 40), 8, 8);
      p.drawEllipse(QPointF(rect.center().x(), rect.center().y() + 8), 11, 11);
      p.drawLine(QPointF(rect.center().x() + 18, rect.center().y() - 12), QPointF(rect.center().x() + 36, rect.center().y() - 30));
      break;
    }
    case 23: {
      p.drawRoundedRect(QRect(rect.center().x() - 26, rect.top() + 24, 52, 74), 10, 10);
      p.drawLine(QPointF(rect.center().x() + 26, rect.top() + 38), QPointF(rect.center().x() + 48, rect.top() + 38));
      p.drawLine(QPointF(rect.center().x() + 48, rect.top() + 38), QPointF(rect.center().x() + 48, rect.top() + 74));
      p.drawLine(QPointF(rect.center().x() + 48, rect.top() + 74), QPointF(rect.center().x() + 36, rect.top() + 88));
      p.setFont(InterFont(24, QFont::Black));
      p.drawText(QRect(rect.left(), rect.bottom() - 50, rect.width(), 34), Qt::AlignCenter, QStringLiteral("LPG"));
      break;
    }
    case 25:
    case 57: {
      p.setFont(InterFont(74, QFont::Black));
      p.drawText(QRect(rect.left(), rect.top() + 8, rect.width(), 84), Qt::AlignCenter, QStringLiteral("P"));
      if (type == 57) {
        p.setFont(InterFont(20, QFont::Black));
        p.drawText(QRect(rect.left(), rect.bottom() - 54, rect.width(), 26), Qt::AlignCenter, QStringLiteral("LPG"));
      }
      break;
    }
    case 26: {
      p.drawLine(QPointF(rect.left() + 32, rect.bottom() - 24), QPointF(rect.right() - 32, rect.bottom() - 24));
      for (int i = -2; i <= 2; ++i) {
        const int x = rect.center().x() + i * 18;
        p.drawLine(QPointF(x, rect.top() + 36), QPointF(x, rect.bottom() - 30));
      }
      p.drawLine(QPointF(rect.left() + 26, rect.top() + 54), QPointF(rect.right() - 26, rect.top() + 54));
      break;
    }
    case 55:
    case 56: {
      QPainterPath branch;
      branch.moveTo(rect.center().x(), rect.bottom() - 26);
      branch.lineTo(rect.center().x(), rect.top() + 44);
      branch.moveTo(rect.center().x(), rect.center().y() + 6);
      branch.lineTo(rect.left() + 44, rect.top() + 42);
      branch.moveTo(rect.center().x(), rect.center().y() + 6);
      branch.lineTo(rect.right() - 44, rect.top() + 42);
      p.drawPath(branch);
      p.drawLine(QPointF(rect.left() + 44, rect.top() + 42), QPointF(rect.left() + 60, rect.top() + 44));
      p.drawLine(QPointF(rect.left() + 44, rect.top() + 42), QPointF(rect.left() + 48, rect.top() + 58));
      p.drawLine(QPointF(rect.right() - 44, rect.top() + 42), QPointF(rect.right() - 60, rect.top() + 44));
      p.drawLine(QPointF(rect.right() - 44, rect.top() + 42), QPointF(rect.right() - 48, rect.top() + 58));
      break;
    }
    case 62: {
      p.drawLine(QPointF(rect.left() + 34, rect.center().y() + 28), QPointF(rect.right() - 38, rect.center().y() + 28));
      p.drawLine(QPointF(rect.right() - 38, rect.center().y() + 28), QPointF(rect.right() - 56, rect.center().y() + 12));
      p.drawLine(QPointF(rect.right() - 38, rect.center().y() + 28), QPointF(rect.right() - 56, rect.center().y() + 44));
      p.drawLine(QPointF(rect.left() + 34, rect.center().y() + 28), QPointF(rect.left() + 34, rect.top() + 42));
      break;
    }
    case 63: {
      p.drawLine(QPointF(rect.left() + 34, rect.center().y() + 24), QPointF(rect.right() - 34, rect.center().y() + 24));
      p.drawLine(QPointF(rect.left() + 46, rect.center().y() + 24), QPointF(rect.left() + 46, rect.bottom() - 24));
      p.drawLine(QPointF(rect.right() - 46, rect.center().y() + 24), QPointF(rect.right() - 46, rect.bottom() - 24));
      p.drawArc(QRect(rect.center().x() - 14, rect.top() + 22, 28, 28), 30 * 16, 300 * 16);
      break;
    }
    default: {
      p.setFont(InterFont(52, QFont::Black));
      const QString text = apnSdiInfoShortTitleForType(type).isEmpty() ? QStringLiteral("i") : apnSdiInfoShortTitleForType(type).left(3);
      p.drawText(rect, Qt::AlignCenter, text);
      break;
    }
  }
  p.restore();
}

float apnAlertDistanceForType(int type, float speed_limit_kph) {
  if (type == 0 || type == 1 || type == 5 || type == 6 || type == 7 || type == 8 ||
      type == 64 || type == 65 || type == 75 || type == 76) {
    return speed_limit_kph >= 80.0f ? 1000.0f : 500.0f;
  }
  if (type == 2 || type == 3 || type == 4 || type == 84 || type == 85) {
    return speed_limit_kph >= 80.0f ? 1000.0f : 500.0f;
  }
  return 500.0f;
}

}  // namespace

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
  showAPNCameraAlert = false;
  showAPNNavigationBanner = false;
  showAPNSectionAlert = false;
  showAPNSectionActive = false;
  showAPNVariableSectionAlert = false;
  showAPNGenericSdiAlert = false;
  apnCameraDistance = 0.0f;
  apnCameraSpeed = 0.0f;
  apnEtaLabel.clear();
  apnRemainingDistanceLabel.clear();
  apnRemainingTimeLabel.clear();
  apnSdiCategoryLabel.clear();
  apnSdiLabel.clear();
  apnSdiStyle = APN_SDI_STYLE_NONE;
  apnSdiType = 0;
  apnCameraType = 0;
  const std::string apn_active_raw = util::read_file(params_memory.getParamPath("APNDataActive"));
  if (apn_active_raw == "1") {
    const double apn_timestamp = QString::fromStdString(util::read_file(params_memory.getParamPath("APNDataTimestamp"))).toDouble();
    const double now_secs = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    if (apn_timestamp > 0.0 && (now_secs - apn_timestamp) < 10.0) {
      const QString apn_road_name = QString::fromStdString(util::read_file(params_memory.getParamPath("APNRoadName"))).trimmed();
      if (!apn_road_name.isEmpty()) {
        roadName = apn_road_name;
      }

      const QString apn_hazard = QString::fromStdString(util::read_file(params_memory.getParamPath("APNNextHazard"))).trimmed();
      const float apn_hazard_distance = QString::fromStdString(util::read_file(params_memory.getParamPath("APNNextHazardDistance"))).toFloat();
      float apn_hazard_speed = QString::fromStdString(util::read_file(params_memory.getParamPath("APNNextSpeedLimit"))).toFloat();
      if (apn_hazard_speed <= 0.1f) {
        apn_hazard_speed = QString::fromStdString(util::read_file(params_memory.getParamPath("APNSpeedLimit"))).toFloat();
      }
      const QString apn_raw_payload = QString::fromStdString(util::read_file(params_memory.getParamPath("APNLastRGData"))).trimmed();
      bool apn_section_block_active = false;
      bool apn_changeable_speed_type = false;
      bool apn_section_progress_active = false;
      int apn_sdi_type = 0;
      int apn_sdi_plus_type = 0;
      if (!apn_raw_payload.isEmpty()) {
        const QJsonDocument apn_json = QJsonDocument::fromJson(apn_raw_payload.toUtf8());
        if (!apn_json.isNull()) {
          QJsonObject apn_object = apn_json.object();
          if (apn_object.contains("rgdata") && apn_object.value("rgdata").isObject()) {
            apn_object = apn_object.value("rgdata").toObject();
          }
          const auto json_bool_value = [](const QJsonValue &value) {
            if (value.isBool()) {
              return value.toBool(false);
            }
            if (value.isDouble()) {
              return value.toInt(0) != 0;
            }
            if (value.isString()) {
              const QString normalized = value.toString().trimmed().toLower();
              return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
            }
            return false;
          };
          apn_sdi_type = apn_object.value("nSdiType").toInt(apn_object.value("nsdiType").toInt(0));
          apn_sdi_plus_type = apn_object.value("nSdiPlusType").toInt(apn_object.value("nsdiPlusType").toInt(0));
          const QJsonValue block_section_value = apn_object.contains("bSdiBlockSection") ? apn_object.value("bSdiBlockSection") : apn_object.value("bsdiBlockSection");
          apn_section_block_active = json_bool_value(block_section_value);
          const QJsonValue changeable_speed_value = apn_object.contains("bIsChangeableSpeedType") ? apn_object.value("bIsChangeableSpeedType") : apn_object.value("bisChangeableSpeedType");
          apn_changeable_speed_type = json_bool_value(changeable_speed_value);
          const double apn_sdi_block_dist = apn_object.value("nSdiBlockDist").toDouble(apn_object.value("nsdiBlockDist").toDouble(0.0));
          const int apn_sdi_block_type = apn_object.value("nSdiBlockType").toInt(apn_object.value("nsdiBlockType").toInt(0));
          apn_section_progress_active = apn_section_block_active || apn_sdi_block_dist > 0.0 || apn_sdi_block_type > 0;

          const double apn_go_pos_dist = apn_object.value("nGoPosDist").toDouble(apn_object.value("ngoPosDist").toDouble(0.0));
          const double apn_go_pos_time = apn_object.value("nGoPosTime").toDouble(apn_object.value("ngoPosTime").toDouble(0.0));
          const double apn_tbt_dist = apn_object.value("nTBTDist").toDouble(apn_object.value("ntbtdist").toDouble(0.0));
          const int apn_tbt_turn_type = apn_object.value("nTBTTurnType").toInt(apn_object.value("ntbtturnType").toInt(0));
          const QString apn_tbt_main_text = apn_object.value("szTBTMainText").toString(apn_object.value("tbtMainText").toString()).trimmed();
          const bool apn_route_active = apn_go_pos_dist > 0.0 || apn_go_pos_time > 0.0 || apn_tbt_dist > 0.0 || apn_tbt_turn_type > 0 || !apn_tbt_main_text.isEmpty();

          auto format_apn_distance = [](double meters) -> QString {
            if (meters <= 0.0) return QStringLiteral("-");
            if (meters >= 1000.0) return QString("%1km").arg(meters / 1000.0, 0, 'f', 1);
            return QString("%1m").arg(qRound(meters));
          };
          auto format_apn_duration = [](double seconds) -> QString {
            int total = qRound(seconds);
            if (total <= 0) return QStringLiteral("-");
            const int hours = total / 3600;
            const int minutes = (total % 3600) / 60;
            if (hours > 0) return QString("%1시간 %2분").arg(hours).arg(minutes);
            return QString("%1분").arg(qMax(1, minutes));
          };

          if (apn_route_active) {
            showAPNNavigationBanner = true;
            apnRemainingDistanceLabel = format_apn_distance(apn_go_pos_dist);
            apnRemainingTimeLabel = format_apn_duration(apn_go_pos_time);
            apnEtaLabel = QDateTime::currentDateTime().addSecs(qRound(apn_go_pos_time)).toString("HH:mm");
          }
        }
      }

      const int apn_hazard_type = apnTypeFromHazardString(apn_hazard);
      const int apn_effective_sdi_type = apn_sdi_plus_type > 0 ? apn_sdi_plus_type : (apn_sdi_type > 0 ? apn_sdi_type : apn_hazard_type);
      const float apn_hazard_speed_kph = apn_hazard_speed * MS_TO_KPH;
      const float apn_alert_distance = apnAlertDistanceForType(apn_effective_sdi_type, apn_hazard_speed_kph);
      const bool apn_alert_in_range = apn_hazard_distance > 0.0f && apn_hazard_distance <= apn_alert_distance;
      const bool apn_camera_named = apn_hazard.contains("camera", Qt::CaseInsensitive);
      const bool apn_generic_sdi_named = apn_hazard.startsWith("sdi:", Qt::CaseInsensitive);
      const bool apn_camera_alert = apn_alert_in_range && apn_hazard_speed > 0.1f;
      if (apn_camera_alert && (apn_camera_named || apn_hazard.isEmpty() || apn_hazard == "test")) {
        const bool section_pre_alert = apn_hazard.startsWith("section_camera", Qt::CaseInsensitive) &&
                                       apn_effective_sdi_type > 0;
        showAPNCameraAlert = true;
        showAPNSectionAlert = section_pre_alert;
        showAPNSectionActive = section_pre_alert && apn_section_progress_active;
        showAPNVariableSectionAlert = section_pre_alert &&
                                      ((apn_effective_sdi_type == 84 || apn_effective_sdi_type == 85) || apn_changeable_speed_type);
        apnCameraDistance = apn_hazard_distance;
        apnCameraSpeed = apn_hazard_speed * speedConversion;
        apnCameraType = apn_effective_sdi_type;
      } else if (apn_alert_in_range && apn_generic_sdi_named) {
        const int sdi_style = apnSdiStyleForType(apn_effective_sdi_type);
        const QString sdi_label = apnSdiLabelForType(apn_effective_sdi_type);
        // Keep warning SDI styling code available for preview/design work,
        // but suppress live warning-type SDI cards from the onroad UI.
        if (sdi_style != APN_SDI_STYLE_NONE && sdi_style != APN_SDI_STYLE_WARNING && !sdi_label.isEmpty()) {
          showAPNCameraAlert = true;
          showAPNGenericSdiAlert = true;
          apnCameraDistance = apn_hazard_distance;
          apnCameraSpeed = 0.0f;
          apnSdiType = apn_effective_sdi_type;
          apnSdiStyle = sdi_style;
          apnSdiCategoryLabel = apnSdiCategoryLabelForStyle(sdi_style);
          apnSdiLabel = sdi_label;
        }
      }
    }
  }
  const bool apn_demo_preview = util::getenv("OPENPILOT_PREFIX", "") == "routedemo" ||
                                util::getenv("FROGPILOT_APN_CAMERA_PREVIEW", "") == "1";
  if (apn_demo_preview) {
    if (!showAPNNavigationBanner) {
      showAPNNavigationBanner = true;
      apnEtaLabel = QDateTime::currentDateTime().addSecs(23 * 60).toString("HH:mm");
      apnRemainingDistanceLabel = QStringLiteral("17.7km");
      apnRemainingTimeLabel = QStringLiteral("23분");
    }
  }
  const QString apn_sdi_preview = qEnvironmentVariable("FROGPILOT_APN_SDI_PREVIEW").trimmed();
  if (!apn_sdi_preview.isEmpty()) {
    const QStringList parts = apn_sdi_preview.split(',', QString::SkipEmptyParts);
    bool type_ok = false;
    const int preview_type = parts.value(0).trimmed().toInt(&type_ok);
    bool dist_ok = false;
    const float preview_distance = parts.value(1).trimmed().toFloat(&dist_ok);
    const int preview_style = apnSdiStyleForType(preview_type);
    const QString preview_label = apnSdiLabelForType(preview_type);
    if (type_ok && preview_style != APN_SDI_STYLE_NONE && !preview_label.isEmpty()) {
      roadName = QString::fromUtf8("영동고속도로");
      showAPNCameraAlert = true;
      showAPNSectionAlert = false;
      showAPNSectionActive = false;
      showAPNVariableSectionAlert = false;
      showAPNGenericSdiAlert = true;
      apnCameraDistance = dist_ok && preview_distance > 0.0f ? preview_distance : 320.0f;
      apnCameraSpeed = 0.0f;
      apnSdiType = preview_type;
      apnSdiStyle = preview_style;
      apnSdiCategoryLabel = apnSdiCategoryLabelForStyle(preview_style);
      apnSdiLabel = preview_label;
      apnCameraType = 0;
    }
  }
  if (!showAPNCameraAlert && apn_demo_preview) {
    roadName = QString::fromUtf8("영동고속도로");
    showAPNCameraAlert = true;
    showAPNSectionAlert = false;
    showAPNSectionActive = false;
    showAPNVariableSectionAlert = false;
    showAPNGenericSdiAlert = false;
    apnCameraDistance = 246.0f;
    apnCameraSpeed = (80.0f / 3.6f) * speedConversion;
    apnCameraType = 1;
  }
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
  const bool parked_preview = qEnvironmentVariableIntValue("PARKED_PREVIEW") == 1;
  if (resume_required_active) {
    standstillDuration = 0;
    standstillTimer.invalidate();
  } else if (standstill_preview_ok && standstill_preview >= 0) {
    if (parked_preview) {
      standstillDuration = 0;
      standstillTimer.invalidate();
    } else {
      standstillDuration = standstill_preview;
      standstillTimer.invalidate();
    }
  } else if (frogpilot_scene.standstill && frogpilot_toggles.value("stopped_timer").toBool()) {
    if (frogpilot_scene.parked) {
      standstillDuration = 0;
      standstillTimer.invalidate();
    } else if (!standstillTimer.isValid()) {
      standstillTimer.start();
    } else {
      standstillDuration = standstillTimer.elapsed() / 1000;
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

  if (showAPNNavigationBanner) {
    paintAPNNavigationBanner(p);
  }

  if (showAPNCameraAlert) {
    paintAPNCameraAlert(p);
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

void FrogPilotAnnotatedCameraWidget::paintAPNCameraAlert(QPainter &p) {
  if (!showAPNCameraAlert) {
    return;
  }

  const QString distance_text = apnCameraDistance >= 1000.0f
    ? QString("%1km").arg(apnCameraDistance / 1000.0f, 0, 'f', 1)
    : QString("%1m").arg(std::nearbyint(apnCameraDistance));

  if (showAPNGenericSdiAlert) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QColor accent = apnSdiAccentColorForStyle(apnSdiStyle);
    const QString badge_text = apnSdiBadgeForType(apnSdiType);
    const QString support_text = apnSdiSupportLabelForType(apnSdiType);
    const int card_width = 276;
    const int card_height = 228;
    const int left_margin = 28;
    const int top_margin = std::max(110, rect().center().y() - 246);
    const QRect card_rect(left_margin, top_margin, card_width, card_height);

    p.setPen(Qt::NoPen);

    if (apnSdiStyle == APN_SDI_STYLE_INFO) {
      const QRect glass_rect = card_rect.adjusted(10, 0, -42, 12);
      const QRect sign_rect(glass_rect.left() + 28, glass_rect.top() + 10, 184, 184);
      const QRect title_rect(glass_rect.left() + 18, glass_rect.bottom() - 72, glass_rect.width() - 36, 28);
      const QRect distance_rect(glass_rect.left() + 28, glass_rect.bottom() - 38, glass_rect.width() - 56, 30);

      p.setBrush(QColor(10, 14, 20, 150));
      p.drawRoundedRect(glass_rect, 26, 26);

      if (apnSdiType == 24 || apnSdiType == 58) {
        QPainterPath outer_triangle;
        outer_triangle.moveTo(sign_rect.center().x(), sign_rect.top() + 6);
        outer_triangle.lineTo(sign_rect.right() - 10, sign_rect.bottom() - 10);
        outer_triangle.lineTo(sign_rect.left() + 10, sign_rect.bottom() - 10);
        outer_triangle.closeSubpath();
        p.setBrush(QColor(208, 38, 38));
        p.drawPath(outer_triangle);

        const QRect middle_rect = sign_rect.adjusted(16, 18, -16, -16);
        QPainterPath middle_triangle;
        middle_triangle.moveTo(middle_rect.center().x(), middle_rect.top());
        middle_triangle.lineTo(middle_rect.right(), middle_rect.bottom());
        middle_triangle.lineTo(middle_rect.left(), middle_rect.bottom());
        middle_triangle.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(middle_triangle);

        const QRect inner_rect = middle_rect.adjusted(12, 12, -12, -12);
        QPainterPath inner_triangle;
        inner_triangle.moveTo(inner_rect.center().x(), inner_rect.top());
        inner_triangle.lineTo(inner_rect.right(), inner_rect.bottom());
        inner_triangle.lineTo(inner_rect.left(), inner_rect.bottom());
        inner_triangle.closeSubpath();
        p.setBrush(QColor(255, 207, 46));
        p.drawPath(inner_triangle);
        apnDrawWarningGlyph(p, inner_rect.adjusted(20, 24, -20, -18), apnSdiType);
      } else {
        p.setBrush(QColor(9, 87, 176));
        p.drawRoundedRect(sign_rect, 20, 20);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 6));
        p.drawRoundedRect(sign_rect.adjusted(8, 8, -8, -8), 16, 16);
        apnDrawInfoGlyph(p, sign_rect.adjusted(20, 18, -20, -18), apnSdiType);
      }

      p.setPen(QColor(243, 247, 251));
      p.setFont(InterFont(apnSdiLabel.size() >= 10 ? 22 : 24, QFont::Black));
      p.drawText(title_rect, Qt::AlignCenter, apnSdiInfoShortTitleForType(apnSdiType).isEmpty() ? apnSdiLabel : apnSdiInfoShortTitleForType(apnSdiType));

      p.setPen(Qt::NoPen);
      p.setBrush(accent);
      p.drawRoundedRect(distance_rect, 15, 15);
      p.setPen(Qt::white);
      p.setFont(InterFont(distance_text.size() >= 5 ? 22 : 26, QFont::Black));
      p.drawText(distance_rect, Qt::AlignCenter, distance_text);
    } else if (apnSdiStyle == APN_SDI_STYLE_ZONE) {
      if (apnSdiZoneUsesTextOnlySign(apnSdiType)) {
        const int sign_size = 190;
        const QRect sign_rect(left_margin + 20, top_margin + 8, sign_size, sign_size);
        const QRect face_rect = sign_rect.adjusted(16, 16, -16, -16);
        const int distance_box_width = 220;
        const int distance_box_height = 74;
        const QRect distance_rect(sign_rect.center().x() - (distance_box_width / 2), sign_rect.bottom() - 8, distance_box_width, distance_box_height);

        p.setBrush(QColor(18, 95, 205, 238));
        p.setPen(Qt::NoPen);
        p.drawEllipse(sign_rect);

        p.setBrush(Qt::white);
        p.drawEllipse(face_rect);

        p.setPen(blackColor());
        p.setFont(InterFont(30, QFont::Black));
        p.drawText(sign_rect.adjusted(22, 32, -22, -32), Qt::AlignCenter | Qt::TextWordWrap, apnSdiZoneTextOnlySignText(apnSdiType));

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 95, 205, 238));
        p.drawRoundedRect(distance_rect, 10, 10);
        p.setPen(Qt::white);
        p.setFont(InterFont(distance_text.size() >= 5 ? 36 : 46, QFont::Black));
        p.drawText(distance_rect, Qt::AlignCenter, distance_text);
      } else {
        const QRect glass_rect = card_rect.adjusted(16, 0, -56, 12);
        const QRect sign_rect(glass_rect.left() + 18, glass_rect.top() + 4, 200, 200);
        const QRect glyph_rect = sign_rect.adjusted(30, 24, -30, -60);
        const QRect zone_text_rect(sign_rect.left() + 18, sign_rect.bottom() - 58, sign_rect.width() - 36, 36);
        const QRect phase_rect(glass_rect.left() + 138, glass_rect.top() + 12, 72, 30);
        const QRect distance_rect(glass_rect.left() + 34, glass_rect.bottom() - 38, glass_rect.width() - 68, 30);

        p.setBrush(QColor(10, 14, 20, 145));
        p.drawRoundedRect(glass_rect, 28, 28);

        const QPainterPath outer_path = apnPentagonSignPath(sign_rect);
        p.setBrush(QColor(18, 95, 205));
        p.drawPath(outer_path);

        const QRect border_rect = sign_rect.adjusted(10, 12, -10, -12);
        const QPainterPath border_path = apnPentagonSignPath(border_rect);
        p.setBrush(Qt::white);
        p.drawPath(border_path);

        const QRect inner_zone_rect = sign_rect.adjusted(18, 20, -18, -20);
        const QPainterPath inner_path = apnPentagonSignPath(inner_zone_rect);
        p.setBrush(QColor(18, 95, 205));
        p.drawPath(inner_path);

        apnDrawZoneGlyph(p, glyph_rect, apnSdiType);

        p.setPen(Qt::white);
        p.setFont(InterFont(20, QFont::Black));
        p.drawText(zone_text_rect, Qt::AlignCenter, apnSdiZoneCoreLabelForType(apnSdiType));

        const QString phase_text = apnSdiPhaseLabelForType(apnSdiType);
        if (!phase_text.isEmpty()) {
          p.setPen(Qt::NoPen);
          p.setBrush(QColor(255, 190, 86));
          p.drawRoundedRect(phase_rect, 15, 15);
          p.setPen(QColor(89, 49, 0));
          p.setFont(InterFont(18, QFont::Black));
          p.drawText(phase_rect, Qt::AlignCenter, phase_text);
        }

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 95, 205));
        p.drawRoundedRect(distance_rect, 15, 15);
        p.setPen(Qt::white);
        p.setFont(InterFont(distance_text.size() >= 5 ? 22 : 26, QFont::Black));
        p.drawText(distance_rect, Qt::AlignCenter, distance_text);
      }
    } else if (apnSdiStyle == APN_SDI_STYLE_WARNING) {
      const int sign_size = 198;
      const QRect sign_rect(left_margin + 20, top_margin + 8, sign_size, sign_size);
      const int distance_box_width = 220;
      const int distance_box_height = 74;
      const QRect distance_rect(sign_rect.center().x() - (distance_box_width / 2), sign_rect.bottom() + 8, distance_box_width, distance_box_height);

      p.setPen(Qt::NoPen);
      p.setBrush(QColor(208, 38, 38));
      p.drawEllipse(sign_rect.adjusted(2, 2, -2, -2));

      p.setBrush(QColor(255, 205, 56));
      p.drawEllipse(sign_rect.adjusted(26, 26, -26, -26));

      p.setPen(blackColor());
      p.setFont(InterFont(38, QFont::Black));
      p.drawText(sign_rect.adjusted(24, 44, -24, -40), Qt::AlignCenter | Qt::TextWordWrap, apnSdiWarningFallbackTextForType(apnSdiType));

      p.setBrush(QColor(255, 205, 56));
      p.setPen(Qt::NoPen);
      p.drawRoundedRect(distance_rect, 10, 10);
      p.setPen(blackColor());
      p.setFont(InterFont(distance_text.size() >= 5 ? 36 : 46, QFont::Black));
      p.drawText(distance_rect, Qt::AlignCenter, distance_text);
    } else if (apnSdiStyle == APN_SDI_STYLE_RESTRICTION) {
      const QRect glass_rect = card_rect.adjusted(16, 0, -56, 12);
      const QRect sign_rect(glass_rect.left() + 30, glass_rect.top() + 10, 176, 176);
      const QRect center_rect = sign_rect.adjusted(22, 22, -22, -22);
      const QRect title_rect(glass_rect.left() + 18, glass_rect.bottom() - 72, glass_rect.width() - 36, 28);
      const QRect distance_rect(glass_rect.left() + 32, glass_rect.bottom() - 38, glass_rect.width() - 64, 30);

      p.setBrush(QColor(10, 14, 20, 145));
      p.drawRoundedRect(glass_rect, 26, 26);
      p.setBrush(QColor(208, 38, 38));
      p.drawEllipse(sign_rect);
      p.setBrush(Qt::white);
      p.drawEllipse(sign_rect.adjusted(12, 12, -12, -12));

      p.setPen(QColor(30, 33, 40));
      p.setFont(InterFont(28, QFont::Black));
      p.drawText(center_rect, Qt::AlignCenter | Qt::TextWordWrap, apnSdiRestrictionShortTextForType(apnSdiType));

      p.setPen(Qt::white);
      p.setFont(InterFont(22, QFont::Black));
      p.drawText(title_rect, Qt::AlignCenter, apnSdiLabel);

      p.setPen(Qt::NoPen);
      p.setBrush(accent);
      p.drawRoundedRect(distance_rect, 15, 15);
      p.setPen(Qt::white);
      p.setFont(InterFont(distance_text.size() >= 5 ? 22 : 26, QFont::Black));
      p.drawText(distance_rect, Qt::AlignCenter, distance_text);
    } else {
      const QRect rail_rect(card_rect.left(), card_rect.top(), 18, card_rect.height());
      const QRect badge_rect(card_rect.left() + 28, card_rect.top() + 18, 72, 34);
      const QRect category_rect(card_rect.left() + 112, card_rect.top() + 18, card_rect.width() - 132, 30);
      const QRect body_rect(card_rect.left() + 28, card_rect.top() + 72, card_rect.width() - 56, 88);
      const QRect distance_rect(card_rect.left() + 24, card_rect.bottom() - 52, card_rect.width() - 48, 38);

      p.setBrush(QColor(18, 17, 18, 220));
      p.drawRoundedRect(card_rect, 24, 24);
      p.setBrush(accent);
      p.drawRoundedRect(rail_rect, 24, 24);
      p.drawRect(rail_rect.adjusted(10, 0, 0, 0));

      p.setBrush(QColor(73, 18, 24));
      p.drawRoundedRect(badge_rect, 17, 17);
      p.setPen(whiteColor());
      p.setFont(InterFont(18, QFont::Black));
      p.drawText(badge_rect, Qt::AlignCenter, badge_text.isEmpty() ? apnSdiCategoryLabel : badge_text);

      p.setPen(QColor(244, 226, 228));
      p.setFont(InterFont(20, QFont::DemiBold));
      p.drawText(category_rect, Qt::AlignVCenter | Qt::AlignLeft, apnSdiCategoryLabel);

      p.setPen(whiteColor());
      p.setFont(InterFont(apnSdiLabel.size() >= 11 ? 24 : 30, QFont::Black));
      p.drawText(body_rect, Qt::AlignCenter | Qt::TextWordWrap, apnSdiLabel);

      p.setBrush(accent);
      p.setPen(Qt::NoPen);
      p.drawRoundedRect(distance_rect, 12, 12);
      p.setPen(whiteColor());
      p.setFont(InterFont(distance_text.size() >= 5 ? 24 : 28, QFont::Black));
      p.drawText(distance_rect, Qt::AlignCenter, distance_text);
    }

    p.restore();
    return;
  }

  p.save();
  p.setRenderHint(QPainter::Antialiasing);

  const int sign_size = 190;
  const int ring_thickness = 16;
  const int left_margin = 42;
  const int top_margin = std::max(110, rect().center().y() - 240);
  const QRect sign_rect(left_margin, top_margin, sign_size, sign_size);
  const QRect inner_rect = sign_rect.adjusted(ring_thickness, ring_thickness, -ring_thickness, -ring_thickness);
  const QString speed_text = QString::number(std::nearbyint(apnCameraSpeed));

  p.setOpacity(1.0);
  p.setPen(Qt::NoPen);
  p.setBrush(showAPNSectionActive ? QColor(143, 32, 43, 235) : redColor(235));
  p.drawEllipse(sign_rect);

  p.setBrush(whiteColor());
  p.drawEllipse(inner_rect);

  if (showAPNSectionAlert) {
    p.setPen(blackColor());
    p.setFont(InterFont(28, QFont::Bold));
    p.drawText(inner_rect.adjusted(0, 18, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("구간"));

    const QRect section_speed_rect = showAPNVariableSectionAlert ? inner_rect.adjusted(0, 44, 0, 6) : inner_rect.adjusted(0, 34, 0, -6);
    p.setPen(QPen(blackColor(), 2));
    p.setFont(InterFont(showAPNVariableSectionAlert ? 56 : (speed_text.size() >= 3 ? 74 : 86), QFont::Black));
    p.drawText(section_speed_rect, Qt::AlignCenter, showAPNVariableSectionAlert ? tr("가변") : speed_text);
  } else {
    if (apnSdiIsSignalEnforcementType(apnCameraType)) {
      const QRect light_box(inner_rect.center().x() - 38, inner_rect.top() + 12, 76, 30);
      p.setBrush(blackColor());
      p.drawRoundedRect(light_box, 15, 15);

      const int light_radius = 7;
      const int light_y = light_box.center().y();
      p.setBrush(QColor(255, 74, 74));
      p.drawEllipse(QPoint(light_box.left() + 18, light_y), light_radius, light_radius);
      p.setBrush(QColor(255, 207, 51));
      p.drawEllipse(QPoint(light_box.center().x(), light_y), light_radius, light_radius);
      p.setBrush(QColor(71, 214, 96));
      p.drawEllipse(QPoint(light_box.right() - 18, light_y), light_radius, light_radius);

      const QRect speed_text_rect = apnCameraType == 76 ? inner_rect.adjusted(0, 26, 0, -22) : inner_rect.adjusted(0, 34, 0, -4);
      p.setPen(QPen(blackColor(), 2));
      p.setFont(InterFont(speed_text.size() >= 3 ? (apnCameraType == 76 ? 72 : 82) : (apnCameraType == 76 ? 84 : 96), QFont::Black));
      p.drawText(speed_text_rect, Qt::AlignCenter, speed_text);
      if (apnCameraType == 76) {
        const QRect rear_text_rect(inner_rect.left(), inner_rect.bottom() - 38, inner_rect.width(), 24);
        p.setPen(blackColor());
        p.setFont(InterFont(24, QFont::Black));
        p.drawText(rear_text_rect, Qt::AlignHCenter | Qt::AlignVCenter, tr("후면"));
      }
    } else if (apnSdiIsTextOnlyCameraType(apnCameraType)) {
      QString label = apnSdiTextOnlyCameraLabel(apnCameraType);
      if (apnCameraType == 9 || apnCameraType == 10) {
        label.replace(QStringLiteral(" "), QStringLiteral("\n"));
      }

      const QRect label_rect = inner_rect.adjusted(18, 18, -18, -18);
      const int label_font_size =
          apnCameraType == 5 ? 30 :
          apnCameraType == 6 ? 42 :
          apnCameraType == 9 ? 28 :
          apnCameraType == 10 ? 28 :
          apnCameraType == 64 ? 42 :
          26;

      p.setPen(QPen(blackColor(), 2));
      p.setFont(InterFont(label_font_size, QFont::Black));
      p.drawText(label_rect, Qt::AlignCenter | Qt::TextWordWrap, label);
    } else if (apnCameraType == 75) {
      const QRect speed_text_rect = inner_rect.adjusted(0, 4, 0, -16);
      const QRect rear_text_rect(inner_rect.left(), inner_rect.bottom() - 42, inner_rect.width(), 24);
      p.setPen(QPen(blackColor(), 2));
      p.setFont(InterFont(speed_text.size() >= 3 ? 76 : 90, QFont::Black));
      p.drawText(speed_text_rect, Qt::AlignCenter, speed_text);
      p.setPen(blackColor());
      p.setFont(InterFont(24, QFont::Black));
      p.drawText(rear_text_rect, Qt::AlignHCenter | Qt::AlignVCenter, tr("후면"));
    } else if (apnSdiIsPlainSpeedCameraType(apnCameraType)) {
      const QRect speed_text_rect = inner_rect.adjusted(0, 6, 0, -2);
      p.setPen(QPen(blackColor(), 2));
      p.setFont(InterFont(speed_text.size() >= 3 ? 84 : 98, QFont::Black));
      p.drawText(speed_text_rect, Qt::AlignCenter, speed_text);
    } else {
      p.setPen(QPen(blackColor(), 2));
      p.setFont(InterFont(30, QFont::Bold));
      p.drawText(inner_rect.adjusted(0, 18, 0, 0), Qt::AlignTop | Qt::AlignHCenter, tr("단속"));

      const QRect speed_text_rect = inner_rect.adjusted(0, 28, 0, -10);
      p.setFont(InterFont(speed_text.size() >= 3 ? 82 : 96, QFont::Black));
      p.drawText(speed_text_rect, Qt::AlignCenter, speed_text);
    }
  }

  const int distance_box_width = showAPNSectionActive ? 236 : 220;
  const int distance_box_height = showAPNSectionActive ? 84 : 74;
  const QRect distance_rect(sign_rect.center().x() - (distance_box_width / 2), sign_rect.bottom() - 8, distance_box_width, distance_box_height);
  p.setPen(Qt::NoPen);
  p.setBrush(showAPNSectionActive ? QColor(56, 60, 66, 235) : redColor(235));
  p.drawRoundedRect(distance_rect, 10, 10);

  if (showAPNSectionActive) {
    const float progress_ratio = std::clamp(1.0f - (apnCameraDistance / 5000.0f), 0.20f, 0.92f);
    const QRect progress_track_rect(sign_rect.right() + 16, sign_rect.top() + 16, 24, sign_rect.height() - 32);
    const QRect progress_fill_bounds = progress_track_rect.adjusted(3, 3, -3, -3);
    const int progress_fill_height = qRound(progress_fill_bounds.height() * progress_ratio);
    const QRect progress_fill_rect(progress_fill_bounds.left(),
                                   progress_fill_bounds.bottom() - progress_fill_height + 1,
                                   progress_fill_bounds.width(),
                                   progress_fill_height);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(56, 60, 66, 220));
    p.drawRoundedRect(progress_track_rect, 9, 9);
    p.setBrush(redColor(235));
    p.drawRoundedRect(progress_fill_rect, 6, 6);

    const QRect remaining_label_rect(distance_rect.left(), distance_rect.top() + 6, distance_rect.width(), 22);
    const QRect remaining_value_rect(distance_rect.left(), distance_rect.top() + 22, distance_rect.width(), distance_rect.height() - 24);
    p.setPen(QColor(255, 220, 112));
    p.setFont(InterFont(18, QFont::Black));
    p.drawText(remaining_label_rect, Qt::AlignCenter, tr("잔여"));
    p.setPen(whiteColor());
    p.setFont(InterFont(distance_text.size() >= 5 ? 34 : 42, QFont::Black));
    p.drawText(remaining_value_rect, Qt::AlignCenter, distance_text);
  } else {
    p.setPen(whiteColor());
    p.setFont(InterFont(distance_text.size() >= 5 ? 36 : 46, QFont::Black));
    p.drawText(distance_rect, Qt::AlignCenter, distance_text);
  }

  p.restore();
}

void FrogPilotAnnotatedCameraWidget::paintAPNNavigationBanner(QPainter &p) {
  const QRect anchor_rect = !currentSpeedRect.isEmpty() ? currentSpeedRect : setSpeedRect;
  if (!showAPNNavigationBanner || anchor_rect.isEmpty()) {
    return;
  }

  p.save();
  p.setRenderHint(QPainter::Antialiasing);

  const QColor banner_color(41, 88, 82, 232);
  const int banner_width = 700;
  const int banner_height = 144;
  const int gap = 40;
  const QRect banner_rect(anchor_rect.center().x() - banner_width / 2, anchor_rect.top() - banner_height - gap, banner_width, banner_height);

  p.setPen(Qt::NoPen);
  p.setBrush(banner_color);
  p.drawRoundedRect(banner_rect, 22, 22);

  const int section_width = banner_rect.width() / 3;
  const QRect left_rect(banner_rect.left(), banner_rect.top(), section_width, banner_rect.height());
  const QRect center_rect(left_rect.right(), banner_rect.top(), section_width, banner_rect.height());
  const QRect right_rect(center_rect.right(), banner_rect.top(), banner_rect.width() - (section_width * 2), banner_rect.height());

  p.setPen(QColor(255, 255, 255, 48));
  p.drawLine(left_rect.right(), banner_rect.top() + 16, left_rect.right(), banner_rect.bottom() - 16);
  p.drawLine(center_rect.right(), banner_rect.top() + 16, center_rect.right(), banner_rect.bottom() - 16);

  auto draw_metric = [&](const QRect &rect, const QString &title, const QString &value) {
    const QRect title_rect(rect.left(), rect.top() + 12, rect.width(), 32);
    const QRect value_rect(rect.left(), rect.top() + 50, rect.width(), rect.height() - 58);

    p.setPen(QColor(230, 244, 240, 180));
    p.setFont(InterFont(24, QFont::DemiBold));
    p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignTop, title);

    p.setPen(whiteColor());
    p.setFont(InterFont(value.size() >= 7 ? 38 : 48, QFont::Black));
    p.drawText(value_rect, Qt::AlignHCenter | Qt::AlignVCenter, value);
  };

  draw_metric(left_rect, tr("도착예정"), apnEtaLabel);
  draw_metric(center_rect, tr("남은거리"), apnRemainingDistanceLabel);
  draw_metric(right_rect, tr("남은시간"), apnRemainingTimeLabel);

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
