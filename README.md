# frogpilot-testing-v1

본 저장소는 comma.ai의 오픈소스 자율주행 소프트웨어인 `openpilot`의 비공식 확장판인 `FrogPilot`을 기반으로,  
한국어 사용자 환경에 적합한 UI 개선 및 시스템 안정성 강화 목적으로 제작된 개인 포크입니다.

현재 브랜치는 순정 openpilot 또는 upstream FrogPilot과는 다르게, 다음과 같은 방향에 초점을 맞춰 개발되었습니다.

- 한국어 중심 UI/알럿 정비
- GM 차량 대상 실험 기능 및 디버그 도구 확장
- Offroad 상태에서의 웹 대시보드/카메라/상태 조회 기능 강화
- UTM 기반 device-ABI 빌드 및 실기 배포 안정성 개선
- 주행 모델/NNFF/알럿/밝기/전원 로직 등 런타임 동작 보정

---

## ⚖️ 법적 고지사항 (Legal Notice)

🚨 **본 프로젝트는 어떠한 경우에도 실제 차량 주행에 사용되어서는 안 됩니다.**

2025년 8월 14일 시행되는 개정 「자동차관리법」에 따라,

⚠️ **차량의 안전운행에 영향을 미칠 수 있는 전자장치 또는 소프트웨어의 임의 설치·변경은 법률로 금지됩니다.**

이 저장소에 포함된 모든 소스코드, 리소스, 파생물은 다음 목적에 한해 제한적으로 사용될 수 있습니다.

- 🎓 학술 연구 및 알고리즘 탐색
- 🧪 시뮬레이션 또는 에뮬레이터 기반 테스트
- 🛠️ 비상업적 개인 실험

실제 차량에의 적용, 공공도로 주행 등은 절대적으로 금지되며,  
이와 관련하여 발생하는 모든 법적, 민사적, 형사적 책임은 전적으로 사용자에게 귀속됩니다.

This software and its derivatives are intended strictly for non-commercial, academic, and simulation purposes.  
In compliance with the amended Korean Motor Vehicle Management Act (effective August 14, 2025),  
any unauthorized installation or modification of software affecting vehicle safety is strictly prohibited.  
The developer assumes no liability for any real-world usage or legal consequences resulting from violations of this notice.

---

## 저장소 성격

이 저장소는 제품 배포용 브랜치가 아니라, 다음 항목을 실험·검증하기 위한 개발 브랜치입니다.

- GM 차량용 사용자 경험 보정
- APN / CarrotNavi 연동
- fake-long 및 ACC 버튼 실험 도구
- 한국어 알럿 / HUD / offroad 웹 콘솔
- offroad wake, snapshot, live preview 같은 부가 기능
- precompiled driving model / NNFF / startup alert / power logic 같은 런타임 보정

---

## 핵심 변경 사항

아래 목록은 `HISTORY.md`와 `RELEASES.md`에 기록된 주요 작업을 기능 기준으로 재구성한 것입니다.

### 1. Onroad UI / HUD / 알럿

- `resumeRequired`가 운전자 부주의/무반응 및 `belowSteerSpeed`보다 우선되도록 알럿 우선순위 조정
- `P + 정차` 상태에서 하단 정차 타이머 대신 full-screen dim 방식의 `정차중` 오버레이 추가
  - `parking.png`
  - `정차중`
  - 정차 경과 타이머
- 안전벨트 미착용 시 현재 속도 HUD 좌상단에 `seatbelt.png` 아이콘 표시
- 안전벨트 아이콘 원본 비율 유지 및 크기 미세조정
- startup alert를 `CLEAR`로 비웠을 때 alert 자체를 띄우지 않도록 수정
- `selfdriveWaiting` fallback 문구를 한국어 부팅 문구로 변경
  - `오픈파일럿 준비중`
  - `주행 제어 시스템 부팅중입니다`
- 커스텀 startup alert를 FrogPilot 전용 색상 상태가 아니라 일반 `normal` 상태로 정리
- NNFF 로드/미지원 알럿을 한국어화
  - `NNFF 토크 컨트롤러 로드됨`
  - `인공 신경망 기반 모델이 차량을 제어합니다`
  - `NNFF 토크 컨트롤러 사용 불가`
  - `주행 로그를 기부하면 차량 지원에 도움이 됩니다`
- 자동 밝기 로직 재조정
  - 일반 저조도에서 너무 빨리 어두워지지 않도록 하한/암흑 기준 재설계
  - 이중 감쇄 경로 제거

### 2. APN / SDI / CarrotNavi 연동

- APN bridge를 통해 안전운전 모드 카메라 정보가 다시 살아나도록 브리지 경로 수정
- `nSdiType` 기준으로 SDI 타입 분류 체계 재정리
  - `nSdiSection` 중심이 아니라 `SdiCodeConvert` 기준 해석으로 전환
- APN hazard/SDI 시그니처 라벨링 기능 추가
- onroad에서 SDI 타입별 커스텀 렌더링 추가
  - 보호구역 계열
  - 일반 단속 카메라 계열
  - 후면 단속 카메라 계열
  - 구간단속 시작/끝/진행중
  - 가변 구간단속
  - 특수 단속 카메라
- APN 관련 HUD 정보 정리
  - 도착예정 / 남은거리 / 남은시간
  - APN 제한속도 / SDI / fake-long 상태 시각화

### 3. GM fake-long / ACC 버튼 실험 경로

- `Fake-Long`, `Fake-Long Test UI` 토글 추가
- GM 순정 ACC 버튼 에뮬레이션 safety 경로 추가
- onroad fake-long 디버그 오버레이 추가
  - `FAKE / ACC` 카드
  - `ARMED / PAUSED / TARGET / LAST` 상태 칩
  - `MAIN / CANCEL / RES / SET` 테스트 버튼
- GM synthetic button 경로를 실차 검증용 `camera-only` 구조로 단순화
- 웹 디버그탭을 통해 `SET / RES / MAIN / CANCEL / UNPRESS` 테스트 가능
- fake-long 런타임 상태/디버그 파라미터 추가
  - `FakeLongDebug`
  - `FakeLongTestButton`
- 10 km/h 이하 자동 버튼 전송 금지
- disengage/re-engage 사이에서도 fake-long 목표속도 관리 로직 정리
- GM 계기판 속도와 현재속도 표시 보정 경로 추가

### 4. Offroad 웹 대시보드 (`8123`)

- 기기 웹 대시보드 추가
  - 상태
  - 설정
  - 통계
  - 조회
  - 디버그
- 헤더를 `Openpilot Console`로 통일
- 모바일 환경 중심으로 레이아웃 재설계
- 상태 탭은 기본적으로 가벼운 요약만 표시
- 무거운 상세 상태는 `더보기` 뒤로 이동
- CAN 모니터링/디버그 polling을 기본 비활성화해 주행 중 부하 감소
- 통계 탭은 중복 지표 제거 및 모바일 밀도 개선
- 오프로드 전용 콘솔 UI 추가
  - 차량 개요
  - 마지막 위치
  - 최근 스냅샷
  - 실시간 조회 진입점
- `carState`가 없을 때 `정차 / 파킹 / 문 / 벨트`를 실제 값처럼 보이지 않게 `확인 불가` 처리

### 5. Offroad snapshot / live preview

- 시동이 꺼질 때 wide/driver 스냅샷 1회 자동 저장
- 웹 접속 시 마지막 스냅샷이 오래된 경우 1회 자동 갱신
- stale 기준을 시간 기반으로 재조정
- 수동 `갱신` 버튼과 선택형 `LIVE` 버튼 추가
- driver snapshot이 `RecordFront`에 종속되지 않도록 대시보드 전용 캡처 경로 분리
- `wide / driver / both`를 직접 지정해 캡처하는 경로 추가

### 6. Offroad wake

- GM offroad wake watcher 추가
- offroad + ignition off 상태에서 raw CAN만 경량 감시
- 감시 신호:
  - `Door_Open_Switch_Status_LS`
  - `Door_Handle_Switch_Status_LS`
  - fallback `DriverDoorStatus.DriverDoorOpened`
- 감지 시 memory params의 `OffroadWakeCounter` 증가
- UI는 `OffroadWakeCounter` 변화만 보고 기존 `interactive_timeout` 경로로 화면 wake
- full `card/carState`를 offroad에서 띄우지 않는 구조
- 이후 `CarMake`가 bytes로 저장되는 문제를 고쳐, GM 차량에서 watcher가 실제로 동작하도록 수정

### 7. 안전벨트 관련 기능

- `More` 아래에 `안전벨트 착용 여부 미확인` 토글 추가
- 토글이 켜졌을 때만 `seatbeltNotLatched` no-entry를 우회하도록 구성
- 초기 구현에서 capnp reader를 직접 수정하던 경로로 인해 `selfdrived` 크래시가 발생했으나,
  이후 car-event 생성 전에 seatbelt 상태를 가리는 방식으로 재구성해 해결
- 안전벨트 미착용 상태는 UI 아이콘으로는 유지하되, 토글이 켜진 경우에만 engage-blocking 경로를 분리

### 8. 전원 / 화면 / 부팅 관련

- 기존 FrogPilot 자동 종료 로직 복구
  - `Device Shutdown Timer`
  - `Low-Voltage Cutoff`
- `강제 전원 로직 비활성화` 토글 추가
- 부팅 배경/스피너 자산 조정
- offroad wake와 screen timeout 로직 연동
- 주행 중 웹세팅/디버그 접속 시 `commIssue`, `locationd`, `alertDebug` 폭주가 나던 문제를 줄이기 위해 대시보드 polling 경량화

### 9. 주행 모델 / tinygrad / 모델 다운로드

- FrogPilot 주행 모델 다운로드 경로 복구
- `Driving Model` UI 다시 활성화
- 다운로드 시 사전컴파일 artifact를 우선 사용하도록 전환
- legacy tinygrad pickle/runtime 호환성 보강
  - old module path alias
  - enum alias
  - `BUFFER_VIEW` legacy 처리
  - `ProgramSpec.estimates` 복구
- 기기에서 precompiled 다운로드 모델이 실제로 끝까지 로드/실행되도록 경로 수정
- `steam-powered`, `sc-driving` 등 다운로드 모델을 실기에서 검증
- `ForceOnroad / ForceOffroad` 런타임 반영 경로도 함께 정리

### 10. NNFF / Traverse 지원 보정

- Traverse가 NNFF 지원 판단에서 제외되던 문제를 해결하기 위해 별도 NNFF substitute 추가
- `CHEVROLET_TRAVERSE -> CHEVROLET_TRAILBLAZER`
- torque substitute와 분리된 NNFF 전용 substitute 파일 사용
- 설정 UI, 모델 탐색, 런타임 로딩 모두 substitute 경로 반영

### 11. 주행 이벤트 / FCW / 기타 보정

- FCW 민감도 후속 조정
  - 운전자가 이미 브레이크를 밟고 있어도 전방 충돌 경고가 suppress되지 않도록 수정
- lane / blindspot / no-lane / green-light / lead-departing 계열 알럿 보정
- onroad stop timer 포맷과 standstill 표현 정리
- 이전에 추가된 blindspot 아이콘 기능 유지

### 12. 빌드 / 배포 / 안정성

- stale UTM 워크스페이스로 빌드한 UI가 기기 Qt ABI와 맞지 않아 `QPushButton::hitButton` 심볼 오류가 나던 문제를 분석 및 해결
- UTM clean build + device-ABI 빌드 워크플로 정리
- checked-in `selfdrive/ui/ui`, `common/params_pyx.so` 갱신
- `mapd`, launch, backup, service restart 관련 런타임 문제 다수 수정
- fresh clone 후 기기 부팅/서비스 정상화 검증 경험 축적

---

## 현재 이 브랜치에서 볼 수 있는 대표 기능

- 한국어 startup / NNFF / onroad 알럿
- 보호구역 / 단속 / 구간단속 중심의 APN HUD
- GM fake-long 테스트용 onroad / 웹 디버그 도구
- ignition off 상태의 offroad 콘솔
- 최근 wide / driver snapshot 및 live preview 진입
- 문 / 도어핸들 기반 offroad 화면 wake
- 안전벨트 bypass 토글과 HUD 아이콘
- Traverse용 NNFF substitute

---

## 알려진 제한 사항

- GM synthetic `SET / RES`는 `cancel`에 비해 차량 수용성이 떨어지며, 실차 조건에 따라 완전히 해결되지 않은 상태입니다.
- offroad wake는 차체 네트워크가 실제로 깨어나는 door/handle 이벤트에 의존합니다.
- offroad 콘솔의 일부 상태 값은 `carState`가 없으면 `확인 불가`로 표시됩니다.
- 이 저장소는 기능 실험과 기기별 검증이 계속 섞여 있는 개발 브랜치이므로, 모든 기능이 항상 완성 상태를 보장하지 않습니다.

---

## 참고 문서

- [HISTORY.md](/Users/ijonghyeog/Desktop/frogpilot-testing-v1/HISTORY.md)
  - 작업 흐름, 시행착오, 원인 분석, 인수인계 기록
- [RELEASES.md](/Users/ijonghyeog/Desktop/frogpilot-testing-v1/RELEASES.md)
  - 날짜별 패치 요약
- [GM_CAN_SIGNAL_INVENTORY.md](/Users/ijonghyeog/Desktop/frogpilot-testing-v1/GM_CAN_SIGNAL_INVENTORY.md)
  - GM DBC / CAN 신호 정리

---

## 주의

이 저장소는 **실차 주행용 소프트웨어 배포를 목적으로 하지 않습니다.**  
연구, 시뮬레이션, 실험, UI/알고리즘 검증 목적으로만 다뤄야 합니다.
