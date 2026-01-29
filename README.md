# 🐸 FrogPilot – 한글화 커스터마이징 포크 (Maintained by HyukLee)

본 저장소는 comma.ai의 오픈소스 자율주행 소프트웨어인 [`openpilot`](https://github.com/commaai/openpilot)의 비공식 확장판인 [`FrogPilot`](https://github.com/FrogAi/FrogPilot)을 기반으로,  
**한국어 사용자 환경에 적합한 UI 개선 및 시스템 안정성 강화 목적**으로 제작된 개인 포크입니다.

---

## ✨ 프로젝트 개요

이 프로젝트는 FrogPilot의 실험적 기능들을 기반으로 다음과 같은 주요 목표를 지향합니다:

- 🧭 **UI 전면 한글화**: 모든 사용자 인터페이스 요소 및 설정 화면의 텍스트를 자연스러운 한국어로 변환
- 🪶 **한글 폰트 적용 및 가독성 개선**: 한국어 표시 최적화를 위한 폰트 조정 및 문장 구조 개선
- 🔒 **시스템 안정성 강화**: 기기의 비의도적 종료를 원천적으로 방지하는 커널/소프트웨어 단 설정 적용
- 🌙 **24시간 슬립모드 유지**: 사용 유무와 관계없이 항상 저전력 대기 상태를 유지하도록 구성
- 🚨 **커스텀 안전 알림 (ver2.8)**:
  - **수동 운전 전환 알림**: 오픈파일럿 해제 시 5초간 알림 표시 (핸들 잡기 유도)
  - **장거리 운전 휴식 권장**: 시동 후 2시간 경과 시 휴식 권장 알림 표시 ☕
  - **전방 추돌 주의**: 시속 30km 이상에서 앞차와 급격히 가까워질 때 강력한 경고 (오픈파일럿 해제 상태에서도 작동)
  - **민감한 선행차 출발 알림**: 앞차가 50cm만 움직여도 즉시 반응하도록 민감도 튜닝

> 본 프로젝트는 테스트 환경에서의 검증, 시뮬레이션 및 학술적 탐색을 목적으로 한 커스터마이징입니다.

---

## ⚖️ 법적 고지사항 (Legal Notice)

**🚨 본 프로젝트는 어떠한 경우에도 실제 차량 주행에 사용되어서는 안 됩니다.**

2025년 8월 14일 시행되는 개정 「자동차관리법」에 따라,  
> ⚠️ *“차량의 안전운행에 영향을 미칠 수 있는 전자장치 또는 소프트웨어의 임의 설치·변경은 법률로 금지”*됩니다.

이 저장소에 포함된 모든 소스코드, 리소스, 파생물은 다음 목적에 한해 제한적으로 사용될 수 있습니다:

- 🎓 학술 연구 및 알고리즘 탐색
- 🧪 시뮬레이션 또는 에뮬레이터 기반 테스트
- 🛠️ 비상업적 개인 실험

실제 차량에의 적용, 공공도로 주행 등은 **절대적으로 금지**되며,  
이와 관련하여 발생하는 모든 법적, 민사적, 형사적 책임은 **전적으로 사용자에게 귀속**됩니다.

> This software and its derivatives are intended strictly for non-commercial, academic, and simulation purposes.  
> In compliance with the amended *Korean Motor Vehicle Management Act* (effective August 14, 2025),  
> any unauthorized installation or modification of software affecting vehicle safety is strictly prohibited.  
> The developer assumes **no liability** for any real-world usage or legal consequences resulting from violations of this notice.

---

## 🔧 설치 및 빌드 안내

본 저장소는 **정식 배포용 소프트웨어가 아니며**, 별도의 설치 문서나 자동화 스크립트를 제공하지 않습니다.  
빌드 및 적용은 FrogPilot 프로젝트와 그 하위 구성에 대한 충분한 이해를 전제로 합니다.

---

## 🚧 개발 및 유지관리 상태

- 본 프로젝트는 **비정기적 업데이트**를 기반으로 유지되며, 기능은 예고 없이 변경 또는 제거될 수 있습니다.
- OTA 업데이트 등 외부 변경 사항과의 호환성은 보장되지 않습니다.
- 개발자는 본 프로젝트와 관련하여 **어떠한 기술적 지원 또는 법적 보증을 제공하지 않습니다.**

---

## 📄 라이선스

이 저장소는 upstream 프로젝트와 동일하게 [MIT License](https://opensource.org/licenses/MIT)를 따릅니다.  
자세한 라이선스 조건은 `LICENSE` 파일을 참조하시기 바랍니다.

---

## 🧾 참고 사항

- 본 저장소는 **비공식 포크**이며, FrogPilot 또는 comma.ai와는 아무런 관련이 없습니다.
- 한글화와 관련된 기여 또는 이슈 제기는 자유롭게 가능하나, 공식 프로젝트에 병합되지 않음을 유의해 주세요.

---