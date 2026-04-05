# frogpilot-testing-v1 작업 이력 / 인수인계 문서

최종 갱신: 2026-04-05

## 추가: 2026-04-04 ~ 2026-04-05 대시보드 경량화 / seatbelt bypass / 정차 오버레이 / UTM 빌드 경로 정리

이 섹션은 `4b7feec3` 이후부터 2026-04-05 현재까지 진행한 웹 대시보드 경량화, GM fake-long 테스트 경로 재정리, seatbelt bypass 토글, seatbelt HUD 아이콘, `P + standstill` 정차 오버레이, 자동 밝기 추가 보정, UTM build ABI 문제 정리 내역을 이어서 정리한다.

### 1. 웹 대시보드 경량화 + 디버그탭 재정리

관련 파일:

- `tools/device_dashboard_mock/server.py`
- `tools/device_dashboard_mock/index.html`
- `tools/device_dashboard_mock/app.js`
- `tools/device_dashboard_mock/styles.css`

#### 변경 이유

- 주행 중 웹세팅 접속 또는 디버그/모니터링 사용 시 `commIssue`, `locationd`, `liveParameters`, `alertDebug` 류 invalid가 잦게 발생했음
- 원인은 상태탭/디버그탭/CAN 보기에서 live polling이 너무 공격적이고, 모바일 레이아웃도 세로 공간을 과하게 소비하던 점이었음

#### 수정 내용

- 상단 헤더를 `Openpilot Console` 로 통일
- 상태탭:
  - 상단 핵심 요약만 기본 노출
  - 하단 상세는 `더보기`를 눌렀을 때만 렌더
  - 기본 요청은 `lite` 상태로 내려 무거운 상세 데이터를 아예 만들지 않도록 변경
- 디버그탭:
  - `SET / RES / MAIN / CANCEL / UNPRESS`
  - camera-only GM synthetic button debug controls 추가
  - 자동 polling 기본 `OFF`
  - `디버그 새로고침` 버튼 추가
  - `실시간 보기` 토글을 켠 경우에만 주기 polling
- CAN 모니터링:
  - status 탭에서 항상 돌지 않고, 사용자가 명시적으로 눌렀을 때만 동작하도록 변경
- 통계탭:
  - 모바일에서 한 화면에 더 많은 값을 보이도록 카드/섹션 밀도 재구성
  - 중복되는 총 주행 시간/거리/횟수류를 제거
  - `주행 성향`, `날씨별 주행`, `이벤트 / 개구리 통계`는 제거하고 핵심 지표만 남김

#### 결과

- 웹세팅의 기본 진입 비용이 줄어듦
- 디버그용 live polling은 opt-in 구조가 됨
- 주행 중엔 “무거운 패널을 기본으로 열면 바로 부하가 생기는” 형태가 아니라, 필요한 정보만 눌러서 보는 구조로 바뀜

### 2. GM fake-long / synthetic button test 경로 재정리

관련 파일:

- `opendbc_repo/opendbc/car/gm/carcontroller.py`
- `opendbc_repo/opendbc/car/gm/carstate.py`
- `opendbc_repo/opendbc/car/interfaces.py`
- `opendbc_repo/opendbc/car/gm/cluster_speed.py`
- `frogpilot/frogpilot_process.py`
- `frogpilot/common/frogpilot_variables.py`
- `frogpilot/ui/qt/offroad/longitudinal_settings.cc`
- `frogpilot/ui/qt/offroad/vehicle_settings.cc`
- `frogpilot/ui/qt/offroad/vehicle_settings.h`

#### 확인된 사실

- `cancel` 은 synthetic path에서도 실제 반응
- `set/res` 는 memory param → web debug → carcontroller 경로까지 정상 진입하지만, 차량이 synthetic frame을 받아들이지 않음
- `panda / gm safety / forwarding` 본체는 예전 성공 시점과 구조 차이가 거의 없고, 주원인일 가능성이 낮음
- 실제 가장 큰 차이는 `carcontroller.py` 쪽 synthetic test path가 예전보다 복잡해진 점이었음

#### 수정 내용

- 테스트 버튼 경로를 메인 fake-long/APN 상태머신과 분리
- `FakeLongTestUI && stock ACC + forward camera path` 가드 복원
- pending repeat/release 상태를 새 테스트 입력 전에 정리
- 버스 선택은 다시 `camera-only` 위주로 단순화
- 웹 디버그탭에서도 `camera-only` 기준으로 다시 정리
- GM cluster/current speed를 보정 lookup table로 다시 계산하도록 추가
  - `cluster_speed.py` helper 추가
  - `carstate.py` 에서 `vEgoCluster` 보정

#### 현재 결론

- `web debug -> memory param -> carcontroller` 경로는 정상
- `FakeLongTestUI` 는 여전히 테스트를 위해 필요
- 남은 핵심 미해결은 `synthetic SET/RES 수용성` 이고, 이는 `panda` 보다는 synthetic button cadence/수용 조건 문제에 더 가까움

### 3. resumeRequired 우선순위 정리

관련 파일:

- `selfdrive/selfdrived/events.py`
- `selfdrive/selfdrived/selfdrived.py`

#### 문제

- 오토홀드/정차 상황에서 `resumeRequired`가 떠야 하는데, 운전자 부주의 또는 `belowSteerSpeed`가 위로 올라오는 경우가 있었음

#### 수정

- `resumeRequired` 가 활성인 프레임에서는 다음 이벤트를 제거:
  - `preDriverDistracted`
  - `promptDriverDistracted`
  - `driverDistracted`
  - `preDriverUnresponsive`
  - `promptDriverUnresponsive`
  - `driverUnresponsive`
  - `belowSteerSpeed`

#### 결과

- 오토홀드 정차 상황에서는 `resumeRequired` 가 우선 표시됨
- 같은 상황에서 불필요한 운전자 부주의 / 저속 조향 경고가 끼어들지 않도록 정리됨

### 4. 안전벨트 미착용 bypass 토글 + HUD 아이콘

관련 파일:

- `common/params_keys.h`
- `selfdrive/ui/qt/offroad/settings.cc`
- `selfdrive/selfdrived/selfdrived.py`
- `selfdrive/ui/qt/onroad/hud.cc`
- `selfdrive/ui/qt/onroad/hud.h`
- `files/icons/seatbelt.png`

#### 수정 내용

- `More` 에 `안전벨트 착용 여부 미확인` 토글 추가
- 초기 구현은 `seatbeltNotLatched` 이벤트를 selfdrived 단계에서 사후 제거하는 방식이었음
- 이후 `IgnoreSeatbeltUnlatched` 가 켜졌을 때는 car event 생성 전에 `CS.seatbeltUnlatched` 를 임시로 가려, no-entry 자체가 생성되지 않도록 수정
- 현재 속도 패널 좌측 상단에 `seatbelt.png` 를 추가
  - `carState.seatbeltUnlatched = true` 일 때만 표시
  - 착용 시 즉시 숨김
- `seatbelt.png` 원본 비율(`820x1210`)을 유지하도록 HUD 렌더 rect 계산을 수정해, 아이콘 찌그러짐 제거

#### 결과

- 테스트/특수 상황에서 seatbelt no-entry만 우회할 수 있음
- 동시에 화면에서도 belt 상태를 즉시 확인 가능

### 5. `P + 정차` 오버레이를 하단 카드에서 alert-style full-screen dim으로 재설계

관련 파일:

- `selfdrive/ui/qt/onroad/alerts.cc`
- `selfdrive/ui/qt/onroad/alerts.h`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h`
- `files/icons/parking.png`

#### 요구사항

- 기존 하단 정차 카드가 아니라, `resumeRequired` 처럼 전체 화면 dim
- 상단 제목 줄에 `parking.png + 정차중`
- 그 아래에 정차 타이머

#### 구현 내용

- `parkedStandstill` MID overlay를 alerts layer에 합성
- `P + standstill + stopped_timer` 조건일 때만 활성
- 기존 FrogPilot 하단 `정차중` 카드는 제거
- `resumeRequired` 등 실제 alert가 있을 경우 그쪽이 우선

#### UTM preview 보조 코드

- `selfdrive/ui/qt/widgets/cameraview.cc`
  - `ONROAD_ROUTE_IMAGE` 가 있으면 VIPC startup을 건너뛰어 preview용 static route background 사용
- `selfdrive/ui/qt/onroad/alerts.cc`
  - `PARKED_STANDSTILL_PREVIEW` 환경변수로 overlay만 강제 preview 가능하도록 추가

### 6. 자동 밝기 추가 보정

관련 파일:

- `selfdrive/ui/ui.cc`
- `selfdrive/ui/ui_state.py`
- `system/hardware/tici/hardware.h`

#### 수정

- `AUTO_BRIGHTNESS_DARK_THRESHOLD`
  - `4.0 -> 2.0`

#### 의도

- “비교적 밝은데도 화면이 너무 쉽게 어두워진다”는 피드백에 맞춰
- 일반 저조도에서는 기존 floor를 더 오래 유지하고, 더 어두운 환경에서만 floor 아래로 떨어지게 조정

### 7. UTM build ABI 문제와 최종 해결

이 구간에서 가장 중요했던 문제다.

#### 발생한 문제

- 최신 `정차중` UI를 UTM에서 빌드해 `192.168.0.7`에 올리면:
  - `./ui: symbol lookup error: ./ui: undefined symbol: _ZNK11QPushButton9hitButtonERK6QPoint, version Qt_5`
- 그래서 기기에서는 이전 정상 UI (`a26f9dc...`)로 되돌려야 했음

#### 실제 원인

- UTM 작업 디렉터리가 로컬 최신 소스가 아닌 오래된 clone (`d066851`) 상태였음
- 그 상태에 stale build artifact 까지 섞여, 잘못된 device-ABI binary가 만들어짐

#### 해결

- 최신 UI 관련 소스를 UTM clone에 다시 동기화
- 아래 파일들을 clean source 기준으로 UTM에 덮어씀:
  - `alerts.cc / alerts.h`
  - `hud.cc / hud.h`
  - `window.cc / window.h`
  - `cameraview.cc`
  - `ui.cc / ui_state.py`
  - `frogpilot_annotated_camera.cc / .h`
  - `parking.png / seatbelt.png`
- UTM clone에서:
  - `selfdrive/ui/ui`
  - `common/params_pyx.so`
  - `.sconsign.dblite`
  - `*.o`
  - `moc_*.cc`
  를 지운 뒤 clean build 수행
- 결과물:
  - `selfdrive/ui/ui`
    - `09ef153466fc15f014c807e3fb51107cb882c466`
  - `common/params_pyx.so`
    - `4116761a3b0fcce47b271f7b3727a6ef1ab57ca0`

#### 배포 결과

- 위 clean UTM build 결과물을 `192.168.0.7`에 배포
- `comma.service: active`
- 실행 중 확인:
  - `./ui`
  - `frogpilot.frogpilot_process`
  - `./mapd`
  - `tools.apn_bridge.apn_bridge`

#### 인수인계 포인트

- 이 구간 UI는 **기기 native build가 아니라 UTM clean build 기준**으로 다루는 것이 안전함
- UTM에서 다시 작업할 때는:
  1. 오래된 clone 여부 확인
  2. 최신 UI 소스 동기화
  3. stale object / `.sconsign` 제거
  4. `tools/utm/build_device_abi.sh`
  순서를 지켜야 함

## 추가: 2026-03-22 전원 종료 로직 복원 + 강제 전원 로직 비활성화 토글

이 섹션은 자동 종료를 통째로 막아둔 실험 상태를 정리하고, 기본적으로는 FrogPilot 원래 전원 로직을 다시 살리면서 필요할 때만 강제로 자동 종료를 비활성화할 수 있게 만든 작업을 정리한다.

### 변경 배경

- 이전에는 `system/hardware/power_monitoring.py` 의 `should_shutdown(...)` 이 무조건 `False` 를 반환하도록 바뀌어 있었음
- 그래서 FrogPilot 설정의:
  - `Device Shutdown Timer`
  - `Low-Voltage Cutoff`
  값을 바꿔도 실제 자동 종료에는 전혀 반영되지 않았음

### 수정 내용

- `system/hardware/power_monitoring.py`
  - 원래 FrogPilot automatic shutdown 판단 로직 복원
  - 다만 새 토글이 켜져 있을 때만 예외적으로 `return False`

- `common/params_keys.h`
  - 새 파라미터:
    - `DisableForcedPowerLogic`
  - 기본값:
    - `0` (`OFF`)

- `frogpilot/common/frogpilot_variables.py`
  - `toggle.disable_forced_power_logic` 연결

- `frogpilot/ui/qt/offroad/device_settings.cc`
- `frogpilot/ui/qt/offroad/device_settings.h`
  - `FrogPilot Settings > Device Settings` 에
    - `강제 전원 로직 비활성화`
    토글 추가

### 최종 동작

- 토글 `OFF` (기본값)
  - FrogPilot 원래 전원 종료 로직 사용
  - `Device Shutdown Timer` 동작
  - `Low-Voltage Cutoff` 동작

- 토글 `ON`
  - 자동 전원 종료 로직 전체 우회
  - 사실상 이전 실험 상태처럼 자동으로는 꺼지지 않음

### 빌드/반영

- 이 변경은 `params key` + `Device Settings UI` 변경이 함께 있으므로:
  - `common/params_pyx.so`
  - `selfdrive/ui/ui`
  를 다시 빌드해야 함
- 이번 반영은 `기기 직접 빌드`가 아니라 `UTM device-ABI 빌드` 후 결과물만 기기에 복사하는 방식으로 처리했음

## 추가: 2026-03-22 fake-long 실험 UI / 기기 대시보드 / 요약 지표 보정

이 섹션은 2026-03-20~2026-03-22 동안 진행한 `Fake-Long` 실험 도구, 기기 웹 대시보드, 부팅/로딩 자산, recent drive summary 보정 작업을 묶어서 정리한다.

### 1. Fake-Long 실험 경로 추가

- GM stock ACC 차량에서 broad `CC_LONG` safety bit를 항상 켜면 시동 직후 stock ACC fault가 발생하는 차량이 있었음
- 그래서 fake-long 쪽은 `stock ACC는 그대로 두고, 버튼만 흉내 내는 실험 경로`로 다시 분리했음

#### 추가된 설정 / 파라미터

- `common/params_keys.h`
  - `FakeLong`
  - `FakeLongDebug`
  - `FakeLongTestButton`
  - `FakeLongTestUI`

- `frogpilot/common/frogpilot_variables.py`
  - 위 토글들을 조건 없이 읽도록 연결

- `frogpilot/ui/qt/offroad/vehicle_settings.cc`
- `frogpilot/ui/qt/offroad/vehicle_settings.h`
  - `Vehicle Settings` 안에 `Long` 패널을 새로 만들고:
    - `Fake-Long`
    - `Fake-Long Test UI`
    를 항상 보이도록 추가

#### GM safety / interface 변경

- `opendbc_repo/opendbc/car/gm/values.py`
  - `FLAG_GM_FAKE_LONG_BUTTONS = 128` 추가

- `opendbc_repo/opendbc/car/interfaces.py`
  - GM stock ACC + forward camera 경로에서 `FakeLong` / `FakeLongTestUI` 가 켜졌을 때만 위 fake-long button safety bit를 설정

- `opendbc_repo/opendbc/safety/modes/gm.h`
  - broad `gm_cc_long` 대신, fake-long 전용 button allow path 추가
  - `SET / RESUME / UNPRESS / MAIN` 을 stock ACC button emulation 실험용으로 허용

#### fake-long runtime / debug UI

- `opendbc_repo/opendbc/car/gm/carcontroller.py`
  - fake-long 테스트 버튼 및 자동 버튼 로직을 실험용으로 누적 정리
  - `FakeLongDebug` memory param으로 다음 상태를 노출:
    - `armed`
    - `paused`
    - `userSet`
    - `commanded`
    - `target`
    - `last`
  - 디스인게이지 / 리인게이지 시 user ACC target 유지 실험
  - `10 km/h` 미만에선 버튼 신호를 보내지 않도록 guard 추가
  - 현재 속도 기반 fake target 추종 로직을 계속 조정 중

- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h`
  - onroad에 fake-long 실험 UI 추가:
    - `FAKE`
    - `ACC`
    - `ARMED`
    - `PAUSED`
    - `TARGET`
    - `LAST`
  - `Fake-Long Test UI` 켠 상태에서:
    - `MAIN`
    - `CANCEL`
    - `RES`
    - `SET`
    버튼 패널을 띄우도록 추가
  - fake-long 카드 상태에 따라 색상 변화:
    - 상승 중
    - 하강 중
    - paused
    - ready/off

- `selfdrive/ui/qt/onroad/onroad_home.cc`
  - fake-long 버튼/카드가 이벤트, 정차 타이머, 오토홀드 UI에 가려지지 않도록 overlay 우선순위 조정

### 2. recent drive summary 사용 비율 보정

- stock ACC 차량에서 recent drive summary 의 `오픈파일럿 사용 비율`이 실제 lateral 사용 시간이 있어도 `0%` 로 보이는 문제가 있었음
- 원인은 `AOLTime + LongitudinalTime` 만 사용해서, lateral-only 사용이 계산에서 빠지던 구조였음

- 수정:
  - `selfdrive/ui/qt/home.cc`
  - `frogpilot/ui/qt/widgets/drive_summary.cc`

- 새 계산:
  - `engaged_time = min(TrackedTime, max(LateralTime, LongitudinalTime) + AOLTime)`

- 결과:
  - stock ACC / lateral-only 차량도 실제 engage 비율에 더 가깝게 recent summary 가 표시되도록 보정됨

### 3. 기기 웹 대시보드 추가

- 새 경로:
  - `tools/device_dashboard_mock/`
  - 파일:
    - `server.py`
    - `index.html`
    - `app.js`
    - `styles.css`
    - `run_dashboard.sh`
    - `frogpilot-dashboard.service` (참고용)

#### 기능

- `상태`
  - 차량 이름
  - 차량 전압
  - 차량 속도
  - ACC 속도
  - openpilot 활성/비활성 상태
  - 기기 상태 / openpilot 상태 / 차량 정보 카드

- `설정`
  - 실제 Params read/write
  - 브랜치 / 커밋 / Params 수 요약
  - 토글/숫자/문자 설정 편집

- `통계`
  - `FrogPilotStats` 를 raw key dump 대신 한국어 카테고리로 정리:
    - 운행 개요
    - 제어 사용
    - 개입 / 정차
    - 주행 모델
    - 주행 성향
    - 날씨별 주행
    - 이벤트 / 개구리 통계

- `조회`
  - tmux
  - comma.service
  - dashboard server
  - system journal
  로그 확인

#### 구현 포인트

- `server.py`
  - device일 때 `/data/params` 기준 실제 Params 읽기/쓰기
  - `cereal.messaging` 이 가능하면 live:
    - `carState`
    - `selfdriveState`
    - `deviceState`
    - `pandaStates`
    - `peripheralState`
    실시간 스냅샷 사용
  - `/api/meta`, `/api/status`, `/api/stats`, `/api/params`, `/api/logs` 제공

- `app.js`
  - status 1초 polling
  - meta 5초 polling
  - logs 2초 polling
  - stats 10초 polling
  - 연결 끊김 시 상단에 offline 상태 표시
  - Params 토글 저장 시 UI가 한 박자 늦게 반영되던 버그 수정

- `styles.css`
  - 모바일 환경에 맞춘 dense layout 반복 조정
  - 상태 / 설정 / 통계 / 조회 탭 모두 모바일에서 정보량은 늘리고 가로 overflow는 줄이는 방향으로 수정

### 4. 부팅 / 로딩 자산

- `frogpilot/assets/other_images/frogpilot_boot_logo.jpg`
  - FrogPilot boot logo 원본을 stock background 이미지로 교체
  - 이유:
    - 부팅할 때 manager/FrogPilot 함수가 `/usr/comma/bg.jpg` 를 다시 덮어써서, 수동으로 stock bg를 넣어도 다음 부팅에 다시 FrogPilot 로고로 돌아가던 문제를 원천 차단

- `selfdrive/assets/images/spinner_comma.png`
- `selfdrive/assets/images/spinner_track.png`
  - spinner 이미지 2종 교체

### 5. 대시보드 자동 실행

- `launch_chffrplus.sh`
  - `launch_device_dashboard()` 추가
  - openpilot launch 과정에서 `tools/device_dashboard_mock/run_dashboard.sh` 를 같이 띄우도록 연결

- 이유:
  - 기기 `/etc/systemd/system` 이 read-only 라서 persistent custom systemd unit 설치가 불가능했음
  - 대신 branch가 이미 매 부팅마다 타는 `launch_chffrplus.sh` 에 붙이면:
    - 재부팅 후 자동 실행
    - branch restart 후 자동 복구
    가 가능함

### 현재 정리 상태

- `Fake-Long` 은 GM stock ACC 실험 도구로 계속 조정 중이며, 완성된 long replacement 로 문서화하지 않음
- 기기 웹 대시보드는 `http://<device-ip>:8123` 에서 접근 가능
- 모바일에서도 사용할 수 있도록 layout을 계속 다듬어둔 상태
- boot background는 이제 다시 FrogPilot 전용 배경으로 되돌아가지 않고 stock 배경을 유지함

## 추가: 2026-03-19 precompiled 다운로드 모델 실제 실행 복구 및 onroad 검증 완료

이 섹션은 `steam-powered` / `sc-driving` 가 더 이상 built-in default fallback 상태가 아니라, 현재 브랜치 tinygrad/QCOM 런타임에서 실제로 실행되도록 끝까지 맞춘 내역과 실기 검증 결과를 정리한다.

### 직전 문제 상태

- 앞 단계 수정으로 precompiled artifact 자체는 다시 다운로드되고, `load_driving_model_bundle(...)` 도 통과하는 수준까지는 복구됐음
- 하지만 실제 onroad 진입 시에는 여전히 `modeld` 가 죽거나 frame drop 이 발생했고, 특히 다음 호환성 문제가 남아 있었음:
  - `AssertionError: args mismatch in JIT`
  - legacy `BUFFER_VIEW` / `ShapeTracker` 직렬화 구조와 현재 tinygrad 런타임 구조 불일치
- 즉 `pickle load 성공` 과 `실제 추론 성공` 사이에 남은 runtime-level incompatibility 가 있었음

### 핵심 원인

- 예전 FrogPilot precompiled 모델은 current tinygrad 보다 오래된 graph/JIT serialization 구조를 기준으로 저장돼 있었음
- 현재 tinygrad 는:
  - JIT input signature 에서 raw object equality 를 더 엄격하게 보았고
  - `BUFFER_VIEW` / `VIEW` / `ShapeTracker` 처리 구조도 바뀌어 있었고
  - 직렬화된 output graph 쪽 legacy view 표현도 그대로는 실행되지 않았음
- 그 결과 artifact 는 `load` 는 되더라도 실제 QCOM 추론 단계에서 죽는 상태였음

### 수정 내용

- `tinygrad_repo/tinygrad/engine/jit.py`
  - legacy shape descriptor 와 current runtime descriptor 를 raw object equality 대신 normalized shape signature 로 비교하도록 완화
  - `args mismatch in JIT` 로 죽던 구간을 현재 shape 의미 기준으로 통과하게 수정

- `tinygrad_repo/tinygrad/uop/ops.py`
  - `Ops.BUFFER_VIEW` 의 shape 계산에서 tuple arg 뿐 아니라 legacy single-view `ShapeTracker` 형태도 인식하게 수정

- `tinygrad_repo/tinygrad/uop/spec.py`
  - tensor spec validation 에서 legacy `BUFFER_VIEW(ShapeTracker)` arg 도 허용하도록 확장

- `tinygrad_repo/tinygrad/engine/schedule.py`
  - schedule 생성 중 legacy `BUFFER_VIEW` arg 에서 offset 을 안전하게 추출하도록 수정

- `selfdrive/modeld/modeld.py`
  - unpickle 후 output tensor graph 에 남아 있는 legacy `BUFFER_VIEW` 노드를 현재 런타임이 이해하는 형태로 재작성하는 호환 레이어 추가
  - 즉 `captured.ret` 쪽까지 포함해, 예전 artifact output graph 를 현재 tinygrad graph 로 정리한 뒤 실행하게 변경

- `system/manager/manager.py`
- `frogpilot/frogpilot_process.py`
- `system/hardware/hardwared.py`
  - `ForceOnroad` / `ForceOffroad` 가 live session 중에도 반영되도록 started 판단 경로 보강
  - 이건 이번 모델 검증을 위해 실제 기기에서 onroad 프로세스를 강제로 띄워 확인할 수 있게 만든 보조 수정이기도 함

### 실기 검증

- 대상 기기: `192.168.0.11`

- 먼저 device-side standalone inference 로 실제 QCOM 추론 검증:
  - `steam-powered`
    - vision / policy 둘 다 성공
  - `sc-driving`
    - vision / policy 둘 다 성공

- warm-run benchmark 비교:
  - `steam-powered`
    - warm 기준 total latency 약 `25.6ms`
  - `sc-driving`
    - warm 기준 total latency 약 `22.9ms`
  - 둘 다 built-in default 와 같은 성능 클래스에 들어옴

- 이후 강제 onroad 검증:
  - `steam-powered`
    - `modeld`, `controlsd`, `selfdrived`, `plannerd`, `radard`, `camerad` 실제 기동 확인
    - `Dropped`, `skipping model eval`, `Traceback`, `AssertionError`, `System Lagging` 미발생 확인
  - `sc-driving`
    - 동일하게 실제 onroad 프로세스 기동 확인
    - `modeld` crash / JIT crash / dropped model eval 미발생 확인

- 추가 확인:
  - `annotated_camera.cc: slow frame rate: ~14fps` 로그는 `sc-driving` 에서만이 아니라 `steam-powered` 에서도 동일하게 재현됨
  - 따라서 이 증상은 이번 다운로드 모델 runtime compatibility 문제와는 별개이며, 강제 onroad 검증 환경 또는 UI/camera 쪽과 더 관련 있는 것으로 판단

### 최종 상태

- `steam-powered`, `sc-driving` 모두 현재 브랜치 tinygrad/QCOM 런타임에서 실제 실행 가능
- 즉 더 이상:
  - built-in default fallback 에만 머무르거나
  - precompiled artifact runtime crash 로 죽는 상태가 아님
- 기기 `192.168.0.11` 는 최종적으로
  - `DrivingModel=sc-driving`
  - `ForceOnroad=0`
  - `ForceOffroad=0`
  - `IsOnroad=0`
  의 안전한 offroad 상태로 정리해둠

## 추가: 2026-03-19 precompiled 다운로드 모델 호환성 복구 완료

이 섹션은 `steam-powered` / `sc-driving` 같은 다운로드 모델이 실제로는 built-in default로 fallback 되거나, precompiled artifact는 호환성 에러로 버려지고 무거운 local-compile 산출물만 남던 문제를 끝까지 추적해서 고친 내역을 정리한다.

### 직전 상태

- 앞 단계 수정으로 `frogpilot/tools/compile_models.py` 는 `Models/compiled` 를 우선 받게 되었음
- 하지만 현재 브랜치 tinygrad/runtime 가 예전 FrogPilot precompiled pickle 구조를 그대로 읽지 못해서:
  - `Ops.VIEW`
  - `tinygrad.shape.shapetracker`
  - `tinygrad.shape.view`
  - `tinygrad.codegen.opt.kernel`
  같은 레거시 참조 때문에 validation 이 실패했음
- 그 결과 `compile_models.py` 는 precompiled artifact 를 지워버리고 `Models/uncompiled` ONNX를 다시 받아 기기에서 local compile 로 fallback 했음
- 이 fallback 산출물은 다시:
  - policy `24M`
  - vision `94M`
  수준으로 커졌고, 원래 목적이던 `sunnypilot에서 잘 돌던 경량 precompiled artifact` 를 쓰지 못하는 상태였음
- 이후 추가 확인에서, precompiled artifact 를 현재 tinygrad로 직접 읽으면 다음 호환성 문제가 순차적으로 나왔음:
  - `tinygrad.shape.shapetracker` 모듈 없음
  - `Ops.RECIP` 없음
  - `Ops.ENDRANGE` 없음
  - `ProgramSpec.estimates` 재구성 중 `tuple index out of range`

### 핵심 원인

- 현재 저장소의 tinygrad 는 예전 FrogPilot precompiled driving-model pickle 이 직렬화되던 시점의 tinygrad 와 모듈/enum/ProgramSpec 구조가 일부 달라졌음
- 그래서 `Models/compiled` 의 작은 artifact 는 성능적으로는 맞았지만, 런타임 호환성 부족 때문에 validation 단계에서 버려지고 있었음
- 반면 local compile fallback 은 현재 런타임에서는 읽히지만, 산출물이 너무 커져서 frame drop 을 유발했음

### 수정 내용

- `tinygrad_repo/tinygrad/shape/__init__.py`
- `tinygrad_repo/tinygrad/shape/view.py`
- `tinygrad_repo/tinygrad/shape/shapetracker.py`
- `tinygrad_repo/tinygrad/codegen/opt/kernel.py`
  - 예전 FrogPilot precompiled pickle 이 기대하는 레거시 tinygrad 모듈 경로를 shim 으로 복원
  - 현재 tinygrad 에 남아 있는 타입들은 재노출하고, 더 이상 존재하지 않는 shape 관련 객체는 pickle load 를 통과할 수 있도록 state-holder shim 클래스로 제공

- `selfdrive/modeld/modeld.py`
  - precompiled artifact 호환용 tinygrad compatibility layer 추가
  - legacy enum alias 추가:
    - `Ops.VIEW = Ops.BUFFER_VIEW`
    - `Ops.RECIP = Ops.RECIPROCAL`
    - `Ops.ENDRANGE = Ops.END`
  - 구형 serialized `ProgramSpec/UOp` 구조에서도 `estimates` 계산이 죽지 않도록 `ProgramSpec.estimates` 를 안전 fallback 형태로 덮어씀
  - 그 상태에서 다운로드된 모델 override 로딩을 다시 수행하도록 정리

- `frogpilot/tools/compile_models.py`
  - compiled artifact validation 도 동일한 tinygrad compatibility layer 를 사용하도록 수정
  - 즉 `Models/compiled` tinygrad pkl 이 실제로 현재 런타임에서 load 가능한지 올바르게 판정하게 변경
  - 호환되는 precompiled artifact 는 유지하고, 진짜로 깨진 경우에만 ONNX local compile fallback 하도록 정리

### 실기 검증

- 기기 `10.43.111.127` 에 최신 `compile_models.py`, `modeld.py`, tinygrad shim 파일 반영
- 수동 검증:
  - `/usr/local/venv/bin/python` + 현재 런타임 환경에서 원격 precompiled `steam-powered` policy/vision tinygrad pkl 을 각각 직접 `pickle.load(...)`
  - 두 파일 모두 최종적으로 `OK <class 'tinygrad.engine.jit.TinyJit'>` 확인
- 이후 기기 offroad 상태에서 `comma.service` 재시작 후 `steam-powered` 를 다시 다운로드
- 최종 `/data/models/steam-powered_*` 파일 크기:
  - policy `13M`
  - vision `57M`
- 마지막으로 기기에서:
  - `DrivingModel=steam-powered`
  - `resolve_driving_model_paths()`
  - `load_driving_model_bundle(...)`
  를 실제로 실행해, built-in default fallback 이 아니라 다운로드된 `/data/models/steam-powered_*` override 세트를 성공적으로 읽는 것까지 확인함

### 현재 상태 / 의미

- 이제 `steam-powered` 는 단순히 UI 상에서만 선택되는 게 아니라, 실제로 작은 precompiled artifact 를 현재 브랜치 런타임에서 직접 로드할 수 있는 상태
- 즉 기존의
  - `compiled artifact 호환 실패 -> local compile fallback -> 24M/94M -> frame drop`
  경로가 막혔고,
  - `compiled artifact 유지 -> 13M/57M -> downloaded override 직접 사용`
  경로로 복구됨
- 이 수정은 `steam-powered` 뿐 아니라 같은 계열의 다른 precompiled driving model 에도 동일하게 적용될 가능성이 높음

## 추가: 2026-03-19 드라이빙 모델 frame drop 원인 확정 및 precompiled 다운로드 경로 복구

이 섹션은 `sc-driving`, `steam-powered` 같은 다운로드 모델을 선택하면 frame drop이 생기는데, sunnypilot에서는 같은 모델명이 정상 동작했던 이유를 실제 기기 기준으로 추적한 결과를 정리한다.

### 증상

- 기본 내장 `WMI` 계열 모델은 비교적 정상인데, 다운로드 모델(`sc-driving`, `steam-powered`) 선택 시 `frame drop` / `skipping model eval. Dropped N frames` 가 반복적으로 발생했음
- 사용자 체감상 sunnypilot에서는 `steam-powered` 가 정상 동작했던 기억이 있어, 단순히 모델 자체가 무겁다는 설명만으로는 부족했음

### 조사 과정에서 확인한 사실

- 현재 브랜치에서는 `selfdrive/modeld/modeld.py` 가 `DrivingModel` 설정을 읽고, `/data/models/<model>_*` 가 모두 있으면 built-in default 대신 실제 다운로드된 tinygrad vision/policy + metadata 를 사용함
- 즉 예전처럼 `모델 선택 UI만 있고 실제론 기본 모델을 쓰는 상태`가 아니었고, 다운로드 모델이 진짜 런타임에 로드되고 있었음
- 기기 `10.43.111.127` 에서 frame drop이 발생하던 시점의 `steam-powered` 파일 크기는:
  - policy `24M`
  - vision `94M`
- 이전에 확인했던 `sc-driving` 도 비슷하게:
  - policy `28M`
  - vision `94M`
- 반면 built-in default WMI 모델은 훨씬 작았음:
  - policy `14M`
  - vision `57M`

### 핵심 원인

- 기존 `frogpilot/tools/compile_models.py` 는 `Models/uncompiled` 의 ONNX를 내려받아, 기기/브랜치의 현재 tinygrad 툴체인으로 로컬 재컴파일하는 구조였음
- 그런데 FrogPilot 리소스 저장소에는 별도로 `Models/compiled` 폴더가 있고, 여기에 이미 tinygrad pkl + metadata 가 제공되고 있었음
- 실제 원격 `compiled` 산출물 크기를 확인해보니:
  - `steam-powered` policy `12.9M`, vision `59M`
  - `sc-driving` policy `14.5M`, vision `59M`
  로 현재 브랜치가 on-device 재컴파일로 만든 결과물보다 훨씬 작았음
- 즉 sunnypilot에서 문제 없었던 이유는, 모델 이름이 같더라도 실제로 쓰던 산출물이 `precompiled artifact` 였거나 그와 유사한 최적화 경로였을 가능성이 높고, 현재 브랜치는 ONNX 재컴파일 경로 때문에 훨씬 무거운 모델을 런타임에 올리고 있었던 것임

### 수정 내용

- `frogpilot/tools/compile_models.py`
  - 다운로드 경로를 `Models/compiled` 우선으로 변경
  - tinygrad `.pkl` + metadata 4종:
    - `driving_policy_metadata.pkl`
    - `driving_policy_tinygrad.pkl`
    - `driving_vision_metadata.pkl`
    - `driving_vision_tinygrad.pkl`
    를 직접 다운로드하도록 변경
  - 기존 `Models/uncompiled` ONNX 다운로드 후 local compile 흐름은, compiled artifact 가 없을 때만 fallback 되도록 유지
  - 원격 모델 인덱스도 `compiled` 를 우선 조회하고, 없을 때만 `uncompiled` 로 fallback 하도록 정리

### 실기 확인

- 기기 `10.43.111.127` 에 새 스크립트를 직접 반영한 뒤, `/data/models/steam-powered_*` 를 삭제하고 `steam-powered` 를 다시 다운로드함
- 재다운로드 후 기기 파일 크기:
  - policy `13M`
  - vision `57M`
- 즉 기존의 무거운 local-compile 산출물(`24M/94M`)이 아니라, 기대하던 precompiled 산출물 계열로 정상 교체됨
- 이후 기기를 offroad 상태에서 `comma.service` 재시작하여, 다음 onroad 에서 새로 받은 `steam-powered` 모델 세트를 물게 함

### 현재 판단

- 이번 수정으로 `다운로드 모델 경로가 불필요하게 무거운 산출물을 만드는 문제`는 해결한 상태
- 다만 실제 frame drop이 완전히 사라졌는지는, offroad 재시작 이후 다음 onroad에서 다시 실차로 검증해야 함
- 만약 여기서도 여전히 frame drop이 남으면, 그때는 `모델 아티팩트 크기` 다음 단계로 `modeld` 런타임 스케줄링/latency 쪽을 추가 추적해야 함

## 추가: 2026-03-19 FCW 민감도 후속 조정

이 섹션은 FCW가 실제 주행에서 기대보다 잘 보이지 않는다는 피드백 이후, 최근 FCW 관련 커밋 적용 상태를 다시 점검하고 마지막으로 남아 있던 브레이크 차단 조건을 제거한 내용을 정리한다.

### 확인 배경

- FCW 전용 UI 자체는 preview에서 정상적으로 표시되고 있었음
- 실제 주행에서는 FCW가 잘 뜨지 않는다는 피드백이 있었음
- 이전에 참고한 커밋들
  - `0b43e7f77e09ac2a4981444a778a60b264f3f568`
  - `6716db92917e9ddb0edd929aed76651c02a99c5f`
  - `3d906b18e9646fee3df3363774174943d0bb1af2`
  와 현재 브랜치 구현이 완전히 같은지 다시 대조할 필요가 있었음

### 원인 판단

- 현재 브랜치의 FCW 로직은 원본 커밋들과 동일 파일이 아니라 `selfdrive/selfdrived/selfdrived.py` 쪽으로 옮겨진 상태였음
- 이 안에서 `lead_fcw` 는 이미
  - `dRel < 30`
  - `vRel < -3.0`
  - `TTC < 2.5`
  - 브레이크 입력과 무관
  으로 동작하고 있었음
- 하지만 `model_fcw` 는 여전히 `not CS.brakePressed` 조건이 남아 있어서, 운전자가 이미 브레이크를 밟기 시작한 경우 모델 기반 FCW가 차단될 수 있었음
- 즉 최근 FCW 커밋 취지 중 `브레이크 중에도 FCW 허용` 이 lead/TTC 경로에는 반영돼 있었지만, model-based FCW 경로에는 일부만 반영된 상태였음

### 수정 내용

- `selfdrive/selfdrived/selfdrived.py`
  - `model_fcw = self.sm['modelV2'].meta.hardBrakePredicted and not CS.brakePressed and not stock_long_is_braking`
  - 위 조건에서 `not CS.brakePressed` 를 제거
  - 최종적으로:
    - `model_fcw = self.sm['modelV2'].meta.hardBrakePredicted and not stock_long_is_braking`
  - 즉 드라이버가 브레이크를 밟기 시작했더라도, 모델이 강한 제동 예측을 내면 FCW가 계속 살아남도록 변경

### 결과 해석

- 현재 FCW는 보수적이었던 `vRel < -5.0` 기준보다 완화된 `vRel < -3.0` 및 `TTC < 2.5` 조건을 사용
- 여기에 model-based FCW까지 브레이크 입력으로 막히지 않게 되어, 이전보다 더 일찍 혹은 더 자주 FCW가 뜰 가능성이 높아짐
- 다만 `stock_long_is_braking` 차단은 그대로 유지되어, 순정 longitudinal 제동 상황과의 중복 경고는 계속 줄이는 방향을 유지함

### 검증

- `python3 -m py_compile selfdrive/selfdrived/selfdrived.py` 통과
- 변경분은 별도 커밋으로 정리되어 `testing-v1` 에 푸시됨

## 추가: 2026-03-19 드라이빙 모델 다운로드 복구 / 밝기 재조정

이 섹션은 FrogPilot의 `Driving Model` 관리 기능이 실제로 동작하지 않던 문제와, 야간 자동밝기가 여전히 과하게 어둡다는 피드백 이후의 수정 내역을 정리한다.

### 증상

- offroad `Driving Controls` 안에서 `DRIVING MODEL` 버튼 자체가 보이지 않았음
- 기기 UI에는 `Downloading...` 이라고 표시되지만, 실제 모델 다운로드/컴파일 프로세스는 돌지 않았음
- `DownloadAllModels` 를 눌러도 `/data/models` 에 결과물이 생기지 않거나, stale memory params 때문에 계속 진행 중처럼 보이는 상태가 발생했음
- 이전 밝기 하한 패치 후에도, 가로등이 있고 주변이 꽤 밝은 편인 상황에서 화면이 여전히 너무 어둡다는 피드백이 있었음

### 원인 판단

- `frogpilot/ui/qt/offroad/frogpilot_settings.cc` 에서 `Driving Controls` 패널의 `DRIVING MODEL` 버튼이 강제로 숨겨져 있었음
- `frogpilot/tools/compile_models.py` 는 현재 브랜치 구조와 맞지 않는 import / metadata script 경로 / 출력 디렉터리를 사용하고 있어, UI에서 memory param 을 써도 실제 worker 경로가 정상 동작하지 않았음
- `frogpilot/frogpilot_process.py` 쪽도 모델 다운로드 요청을 실제로 소비하는 살아있는 offroad 처리 경로가 없어서, UI 상태값만 `Downloading...` 으로 남는 경우가 있었음
- 자동밝기는 이전 패치에서 하한을 올렸지만, `완전 어두움` 판정이 아직 너무 느슨해서 일반 저조도 환경도 과하게 어둡게 취급될 수 있었음

### 수정 내용

- `frogpilot/ui/qt/offroad/frogpilot_settings.cc`
  - `DRIVING MODEL` 버튼을 다시 보이게 변경
- `frogpilot/tools/compile_models.py`
  - 끊겨 있던 모델 다운로드/컴파일 경로를 현재 브랜치 기준으로 전면 정리
  - `selfdrive/modeld/get_model_metadata.py` 를 사용하도록 수정
  - 다운로드한 ONNX는 `/data/models/uncompiled_downloads` 에 받고, 최종 tinygrad/metadata 산출물은 `/data/models` 바로 아래에 저장하도록 변경
  - GitHub/GitLab 원격 목록을 읽어 실제로 호스팅된 모델만 다운로드 대상으로 필터링하도록 수정
  - 단일 모델 요청(`ModelToDownload`)과 전체 다운로드(`DownloadAllModels`) 모두 현재 memory param 체계와 맞게 동작하도록 정리
- `frogpilot/frogpilot_process.py`
  - offroad 상태에서 `DownloadAllModels`, `ModelToDownload`, `UpdateTinygrad` 요청이 있으면 `process_model_download_request(...)` 를 실제로 실행하도록 연결
  - silent background thread failure 를 피하기 위해 모델 다운로드는 사용자 요청 기반 offroad 작업으로 inline 처리되게 변경
- `selfdrive/modeld/modeld.py`
  - 선택된 `DrivingModel` 이 `/data/models/<model>_*` 파일 세트를 모두 갖고 있으면, built-in default 대신 다운로드된 tinygrad vision/policy + metadata 를 사용하도록 연결
  - 파일이 없거나 세트가 불완전하면 기존 기본 모델로 안전하게 fallback
- `selfdrive/ui/ui.cc`
  - 자동밝기 하한을 최종 `10` 으로 조정
  - `완전 어두움` 판정 기준은 `4` 로 더 보수적으로 내려, 아주 어두운 상황에만 `10` 아래로 내려가게 변경

### 실기 확인 / 진행 결과

- 기기 `192.168.0.11` 에서 stale `DownloadAllModels` 상태를 정리한 뒤, `steam-powered` 단일 다운로드/컴파일을 직접 실행해 끝까지 성공시켰음
- 생성 확인 파일:
  - `/data/models/steam-powered_driving_policy_metadata.pkl`
  - `/data/models/steam-powered_driving_policy_tinygrad.pkl`
  - `/data/models/steam-powered_driving_vision_metadata.pkl`
  - `/data/models/steam-powered_driving_vision_tinygrad.pkl`
- 완료 후 memory params 상태:
  - `ModelToDownload` 비움
  - `ModelDownloadProgress = Downloaded!`
- 최신 기기용 UI 바이너리도 다시 빌드/배포하여, 새 밝기 하한(`10`) 및 어두움 기준(`4`) 이 반영된 상태로 기기에서 `ui`, `mapd` 정상 실행 확인

### 비고

- `AvailableModels` 목록에는 실제 원격에 없는 이름도 남아 있어, UI 선택 목록과 실제 다운로드 가능한 모델 목록 사이에 차이가 존재할 수 있음
- 현재는 downloader 쪽에서 원격 호스팅 여부를 필터링하므로, UI에서 잘못된 모델을 눌러도 무한 진행 대신 실패/무시 쪽으로 정리되는 상태
- 이번 수정으로 `steam-powered` 는 실제 사용 가능한 컴파일 세트까지 내려받았고, 이후 `Driving Model` 선택 메뉴에서 해당 모델을 선택해 사용할 수 있는 기반이 갖춰짐

## 추가: 2026-03-18 자동밝기 하한 / resumeRequired 우선순위 조정

이 섹션은 주행 중 체감 밝기와 stop-and-go 상황 alert 우선순위에 대한 후속 피드백을 반영한 수정이다.

### 수정 배경

- 자동밝기가 주변이 그렇게 어둡지 않은데도 너무 깊게 떨어져 화면이 과하게 어두워진다는 피드백이 있었음
- `resumeRequired` 가 떠 있는 동안 `greenLight`, `leadDeparting` 알림이 화면에서 가려져, 정차 후 재출발 상황에서 중요한 시각 피드백이 묻힘
- 정차 타이머는 기존 변경 후 `시:분` 느낌으로 읽히는 상태였고, `resumeRequired` 중에는 타이머보다 현재 속도 `0` 표시가 더 직관적이라는 요구가 있었음

### 수정 내용

- `selfdrive/ui/ui.cc`
  - 자동밝기 하한용 상수 추가
    - `AUTO_BRIGHTNESS_DIM_FLOOR = 8.0f`
    - `AUTO_BRIGHTNESS_DARK_THRESHOLD = 8.0f`
  - 카메라 노출 기반 `raw_light_sensor` 값을 따로 보존
  - `raw_light_sensor` 가 정말 낮은 경우가 아니면 auto brightness minimum 을 `8` 로 clamp 하도록 변경
  - 결과적으로 일반적인 저조도에서는 화면 밝기가 `8` 아래로 떨어지지 않고, 진짜 어두운 상황만 예외 처리됨
- `selfdrive/ui/qt/onroad/alerts.cc`
  - `selfdriveState` alert 가 `resumeRequired` 인 경우에 한해, `frogpilotSelfdriveState` 의 `greenLight` 또는 `leadDeparting` alert 가 존재하면 그것을 화면상 우선 표시하도록 변경
  - 즉 stop-and-go 상황에서 `resumeRequired` 가 계속 떠 있어도 `greenLight` / `leadDeparting` 가 시각적으로 덮어쓰도록 수정
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
  - 정차 타이머 포맷을 `분:초` 로 변경
  - `resumeRequired` alert 가 활성 상태일 때는 standstill timer 를 무효화하고, 하단에는 타이머 대신 현재 속도 `0` 이 다시 표시되도록 변경

### 빌드 / 배포 결과

- UTM에서 최신 수정본으로 device-ABI UI를 다시 빌드
- 새 기기용 UI 해시:
  - `52fba2410153a3bd4ef6dc216ee9a6652e8f7278`
- 콤마 기기 `192.168.0.11` 에 새 바이너리와 최신 소스 패치를 같이 배포
- 배포 후 확인:
  - `./ui` 실행 중
  - `./mapd` 실행 중
  - 기기 소스 `selfdrive/ui/ui.cc` 에 밝기 하한 상수와 clamp 로직 반영 확인

## 추가: 2026-03-18 주행 중 새로고침 / 부팅 안정성 수정

이 섹션은 실제 콤마 기기 주행 중 `화면이 한 번 새로고침되는 느낌` 이 있었다는 피드백 이후, 그 원인 분석과 안정성 수정 내역을 정리한 것이다.

### 증상

- 주행 중 UI가 한 번 새로고침된 것처럼 보임
- `ui`/`mapd` PID가 바뀐 흔적이 있었음
- tmux 로그를 보면 단순 `ui` 크래시가 아니라, onroad 프로세스 여러 개가 한 번에 `SIGINT` 를 받고 다시 올라간 흐름이 있었음
- 같은 날, git 업데이트 후 체크인된 `ui` 바이너리가 기기 라이브러리와 안 맞아 부팅이 막히는 문제도 재발

### 원인 판단

- `manager.py`/tmux 로그 기준으로, 화면 문제는 `ui` 단독 세그폴트보다는 `deviceState.started` 가 잠깐 false로 떨어졌을 때처럼 onroad 프로세스 세트가 한 번 offroad 전환 취급을 받은 쪽에 더 가까웠음
- 같은 시점에 FrogPilot의 토글 백업, 테마/업데이트 점검 같은 유지보수 작업이 onroad 중에도 돌고 있었고, 실제 로그에도 `toggle backup`, `Theme validation complete`, `Checking for updates...` 등이 함께 보였음
- 부팅 문제는 별개로, 체크인된 `selfdrive/ui/ui` 바이너리가 host/UTM 계열 라이브러리에 링크돼 있으면 기기에서 `capnp`/`ffmpeg` 불일치로 죽는 문제가 있었음

### 수정 내용

- `system/manager/manager.py`
  - `deviceState.started` falling edge에 5초 debounce 추가
  - 짧은 순간의 started glitch는 즉시 offroad 전환으로 보지 않도록 변경
- `frogpilot/frogpilot_process.py`
  - 동일하게 started falling edge debounce 추가
  - toggle backup은 offroad에서만 수행하도록 변경
  - 정기 `update_checks` / 테마 점검 / 자동 업데이트 점검은 offroad일 때만 돌도록 변경
- `selfdrive/ui/ui.cc`, `selfdrive/ui/ui.h`
  - Qt UI의 onroad/offroad 전환도 같은 기준으로 debounce 적용
  - 그래서 started glitch가 와도 UI가 즉시 offroad처럼 튀지 않게 변경
- `SConstruct`
  - UTM device-ABI 빌드시 repo 내부 `third_party/runtime_link_libs` 를 우선 libpath에 넣어, comma와 맞는 `capnp 1.0.2 / ffmpeg58` 조합으로 링크되게 유지
- `launch_env.sh`
  - runtime compatibility library path 유지
  - git 기반 업데이트 후에도 checked-in UI 바이너리가 필요한 호환 symlink를 보게끔 유지

### 빌드 / 배포 결과

- UTM에서 최신 수정본으로 device-ABI UI를 다시 빌드
- 새 기기용 UI 해시:
  - `deb9475b30b7c6a18fbc000f30b4b7e45db08f36`
- `objdump -p selfdrive/ui/ui | grep NEEDED` 기준:
  - `libcapnp-1.0.2.so`
  - `libkj-1.0.2.so`
  - `libavcodec.so.58`
  - `libavformat.so.58`
  - `libavutil.so.56`
  - `libOmxCore.so`
  - 등 comma 기기 런타임과 맞는 방향으로 재확인 완료
- 기기 `10.43.111.127` 에 새 바이너리와 최신 소스 패치를 같이 동기화
- `comma.service` 재시작 후 `./ui`, `./mapd` 정상 기동 확인

### 비고

- 로그상 `manager.py` 가 두 개 보이는 현상은 Python `multiprocessing` 래퍼 성격일 수 있어, 그것만으로는 원인 확정 근거로 쓰지 않았음
- 실제 패치 방향은 `started glitch 완충 + onroad 중 불필요한 유지보수 지연 + device-compatible UI binary 재빌드` 쪽으로 잡음

## 0. 2026-03-18 추가 후속 작업

이 섹션은 `8ac7cfd3` 이후, 아직 별도 인수인계 반영이 안 되었던 후속 수정들을 정리한 것이다.

### Onroad 후속 수정

- blindspot 아이콘 렌더 좌표를 `mapTo()/window()` 기준이 아니라 실제 `QPainter viewport` 기준으로 재계산하도록 수정
  - 목적: 기기에서 BSM 신호가 들어와도 아이콘이 안 보이는 문제 완화
  - 관련 파일: `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- blindspot 아이콘 표시 로직 정리
  - onroad 상태이면 openpilot enabled 여부와 무관하게 BSM 신호로 표시
  - 같은 방향 깜빡이가 켜진 상태에서만 아이콘 점멸
  - 좌우 glow 추가
- 정차 타이머 UI 수정
  - 위치를 하단 현재 속도 영역으로 이동
  - 포맷을 `00h 00m` 에서 `0:00` (`시:분`) 으로 변경
  - 숫자/문자 색상은 흰색 통일
  - 관련 파일: `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- LFA 아이콘 상태 체계 재정리
  - `files/icons/lfa.png` 사용
  - 활성 불가: 회색
  - 활성 가능: 흰색
  - 활성 상태: 초록 + glow
  - 악셀 오버라이드(longitudinal override): 파란색 + blue glow
  - 관련 파일: `selfdrive/ui/qt/onroad/hud.cc`, `selfdrive/ui/qt/onroad/hud.h`
- steering wheel 아이콘 후속 수정
  - `files/icons/steeringwheel.png` 사용
  - 실제 조향각(`carState.getSteeringAngleDeg()`)에 따라 회전하도록 로직 추가
  - 활성 불가/활성 가능: 회색
  - 활성 상태: 흰색
  - 토크 한계에 가까워질수록 흰색 -> 주황/빨강
  - lateral override(핸들 힘줘서 overriding) 시 파란색
  - steer saturated / 조향 한계 preview에서 핸들 확대 + `files/icons/warning.png` 표시
  - 관련 파일: `selfdrive/ui/qt/onroad/hud.cc`, `selfdrive/ui/qt/onroad/hud.h`, `selfdrive/ui/qt/onroad/alerts.cc`

### Summary / Tracking 후속 수정

- drive summary가 `0분 0km` 로 뜨는 문제 원인 분석
  - 기존 `frogpilot_tracking.py` 는 사실상 정차 시점 조건에서만 통계를 저장해서, onroad 종료 시점 값이 offroad summary에 반영되지 않는 경우가 있었음
- 수정 내용
  - stat 저장 로직을 별도 persist/flush 흐름으로 분리
  - onroad -> offroad 전환 시 남은 시간/거리도 flush 되도록 변경
  - 관련 파일:
    - `frogpilot/system/frogpilot_tracking.py`
    - `frogpilot/frogpilot_process.py`

### Sounds 후속 수정

- 실제 콤마 기기에서는 `soundd.py` 가 `engage.wav/disengage.wav` 대신 `engage_tizi.wav/disengage_tizi.wav` 를 쓰는 점 재확인
- 이전에는 일반 `engage.wav/disengage.wav` 만 커스텀된 상태라, git 업데이트 후 체감상 원래 소리로 돌아간 것처럼 느껴질 수 있었음
- 해결:
  - `selfdrive/assets/sounds/engage_tizi.wav`
  - `selfdrive/assets/sounds/disengage_tizi.wav`
  를 현재 커스텀 `engage.wav/disengage.wav` 와 동일한 내용으로 동기화

### 빌드 / 패키징 후속 수정

- 위 onroad 수정들을 반영한 최신 기기용 UI를 UTM에서 다시 빌드
- 결과 바이너리를 로컬 repo `selfdrive/ui/ui` 로 동기화
- 현재 빌드 후 로컬 `selfdrive/ui/ui` 해시:
  - `ec3c533c24b760c4f41477e02541bdc0ceefedf2`

## 1. 문서 목적

이 문서는 `/Users/ijonghyeog/Desktop/frogpilot-testing-v1` 저장소를 클론한 시점부터 2026-03-18 현재까지 진행한 모든 작업을 정리한 인수인계 문서다.

목적은 다음과 같다.

- 어떤 작업을 했는지 빠짐없이 기록
- 현재 무엇이 유지 상태인지, 무엇이 실험 후 되돌려졌는지 구분
- UTM 미리보기와 실제 콤마 기기 배포 흐름을 다시 시행착오 없이 재현
- 이후 다른 작업자가 문제를 겪을 때 원인 후보를 빠르게 좁힐 수 있게 함

이 문서는 `RELEASES.md` 보다 훨씬 자세한 내부 작업 기록이다.

## 2. 작업 기준 정보

- 로컬 저장소: `/Users/ijonghyeog/Desktop/frogpilot-testing-v1`
- UTM Linux 저장소: `/home/hyuklee/frogpilot-testing-v1`
- 콤마 기기 저장소: `/data/openpilot`
- 최초 작업 시작 기준 브랜치: `testing-v1`
- 최초 클론 직후 기준 커밋: `61c139ab` (`Compile FrogPilot`)
- 문서 작성 직전 원격 브랜치 HEAD: `8ac7cfd3` (`Add blindspot onroad warning icons`)

최근 이 작업과 직접 관련된 커밋 흐름:

- `db8206db` `Refine onroad UI preview and runtime fixes`
- `1e83bc89` `Redesign onroad alert cards`
- `14c5b5b7` `Refine onroad and offroad UI`
- `c79b911e` `Check in compiled UI binary`
- `25b258d9` `Fix device UI runtime environment`
- `8ac7cfd3` `Add blindspot onroad warning icons`

## 3. 현재 최종 유지 상태 요약

### Onroad

- onroad 바깥 상태 테두리 제거
- fullscreen일 때 왼쪽 남색 띠 문제는 overscan/edge 조정으로 최대한 줄인 상태
- 화면 녹화 버튼 숨김
- `MAX` 계열 속도 박스는 `SET` 표시로 변경
- `SET` 아래에 겹치던 speed limit/pending/source UI 제거
- disengaged 상태일 때:
  - 카메라 화면 흑백 + 어둡게
  - path 회색톤
- steering wheel 버튼/DM 위치, 하단 그라데이션, 일부 HUD 재배치는 현재 accepted 상태
- blindspot 좌우 아이콘 기능 추가

### Offroad

- 전체 검정 배경
- 기본 좌측 sidebar 숨김
- 좌상단 원형 설정 버튼
- 기본 offroad 홈:
  - 타이틀 `안녕하세요 종혁님`
  - 설명 `오늘도 편안한 주행 되세요`
  - 누적 통계 카드 표시
- onroad 종료 직후 offroad 홈:
  - 타이틀 `주행이 종료되었습니다`
  - 설명 `수고하셨습니다`
  - 최근 주행 요약 카드 3개 표시
- 최근 주행 요약 노출 시간: 10분

### Alerts / Events

- 여러 차례 커스텀 카드형 이벤트 디자인을 시도했지만, 최종적으로는 기존 stock 이벤트 렌더러로 되돌림
- 이벤트 문구/FCW/lead departing/steering 관련 텍스트는 수정 반영

### Runtime / Build / Deploy

- UTM 미리보기는 `tools/utm/force_onroad_preview.sh` 기준
- 직접 콤마 기기에서 `scons selfdrive/ui/ui` 빌드는 불안정해서 비권장
- 기기 배포용 checked-in `selfdrive/ui/ui` 바이너리와 `launch_env.sh`, `libyuv` 런타임까지 함께 관리
- `git pull` 후 `ui`가 디스플레이에 못 붙거나 `libyuv` 때문에 죽던 문제는 `25b258d9` 로 해결

## 4. 전체 작업 내역 상세

### 4-1. 저장소 클론 및 UI 소스 확인

- 빈 디렉터리였던 `/Users/ijonghyeog/Desktop/frogpilot-testing-v1` 에 `testing-v1` 브랜치 클론
- 클론 당시 HEAD 확인: `61c139a`
- `selfdrive/ui/ui` 가 체크인된 바이너리이긴 하지만, 실제 UI는 Qt/C++ 소스가 함께 포함된 구조임을 확인
- 주요 수정 대상 경로 확인:
  - `selfdrive/ui/qt/onroad/*`
  - `selfdrive/ui/qt/offroad/*`
  - `frogpilot/ui/qt/onroad/*`
  - `frogpilot/ui/qt/offroad/*`

### 4-2. 초기 onroad 진입 테스트 및 방향 전환

- 초기에 콤마 기기에서 `pandaStates` 를 위조하는 `spoof_onroad.py` 방식으로 onroad 진입 테스트를 시도
- 이후 이 방식은 안전/재현성 측면에서 장기 workflow로 쓰지 않기로 정리
- 이후부터는 다음 둘로 분리:
  - UTM preview로 화면 확인
  - 실제 콤마 기기에서는 최종 배포 후 확인

### 4-3. onroad 1차 핵심 수정

#### a. 외곽 테두리 제거

- onroad 바깥 status border 제거
- Qt 경로와 Python fallback 경로 둘 다 맞춤
- 관련 파일:
  - `selfdrive/ui/qt/onroad/onroad_home.cc`
  - `selfdrive/ui/onroad/augmented_road_view.py`

#### b. `MAX` -> `SET`

- 현재 속도 우측 박스의 `MAX` 표시를 `SET` 으로 변경
- Python fallback HUD도 동일하게 맞춤
- 관련 파일:
  - `selfdrive/ui/qt/onroad/hud.cc`
  - `selfdrive/ui/onroad/hud_renderer.py`
  - `selfdrive/ui/mici/onroad/hud_renderer.py`

#### c. `SET` 아래 speed limit 관련 UI 제거

- `SET` 아래에 speed limit이 겹쳐 보이는 문제 확인
- FrogPilot overlay 쪽 speed limit / pending limit / source 표시를 제거
- 현재는 `SET` 하단에 해당 위젯이 나오지 않음
- 관련 파일:
  - `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`

#### d. 화면 녹화 버튼 숨김

- onroad recording 버튼 제거 요청 반영
- Qt 경로에서 hard hide
- 기기에서는 한때 param으로도 숨겼으나, 현재는 코드상 숨김이 기준
- 관련 파일:
  - `selfdrive/ui/qt/onroad/annotated_camera.cc`

#### e. steering wheel 버튼 / DM widget 정리

- wheel 버튼 배경 스타일 정리
- stock wheel일 때 enabled 상태에서 초록 tint 적용
- DM widget은 asset 기반 형태로 재구성
- 관련 파일:
  - `selfdrive/ui/qt/onroad/buttons.cc`
  - `selfdrive/ui/qt/onroad/buttons.h`
  - `selfdrive/ui/qt/onroad/driver_monitoring.cc`
  - `selfdrive/ui/qt/onroad/driver_monitoring.h`

#### f. disengaged 시각 효과

- disengaged 상태일 때 카메라 화면만 grayscale + dim
- path만 회색톤 적용
- 관련 파일:
  - `selfdrive/ui/qt/widgets/cameraview.cc`
  - `selfdrive/ui/qt/widgets/cameraview.h`
  - `selfdrive/ui/qt/onroad/annotated_camera.cc`
  - `selfdrive/ui/qt/onroad/model.cc`

### 4-4. UTM onroad preview workflow 확립

- 단순히 `ui.utm` 만 띄우는 것으로는 onroad 검증이 어려웠음
- UTM 전용 onroad preview 스크립트 작성:
  - `tools/utm/force_onroad_preview.sh`

이 스크립트가 맡는 일:

- manager/ui만 재기동
- stale runtime state 정리
- `ForceOnroad` 적용
- `ui.utm` 실행

중요하게 밝혀진 점:

- `ForceOnroad` 는 `CLEAR_ON_MANAGER_START` 파라미터
- manager 시작 전에 써두면 지워짐
- 반드시 manager 시작 후 다시 써야 함

추가로 나중에 이 스크립트에 다음 보강이 들어감:

- background reassert loop로 일정 시간 `ForceOnroad=True` 재적용
- UTM에서 onroad preview가 offroad로 다시 튀는 현상 완화

### 4-5. UTM full stack / simulator / input 관련 작업

UTM에서 openpilot 전체를 띄우고 UI를 보는 과정에서 다음 작업들이 있었다.

#### a. tinygrad / modeld / arm64 Linux 문제

- UTM이 `aarch64 Linux` 라는 이유로 QCOM 경로를 타며 `/dev/kgsl-3d0` 오류 발생
- `/TICI` 존재 여부를 기준으로만 `DEV=QCOM` 을 타게 수정
- CPU tinygrad 모델 재생성
- `desire` / `desire_pulse` mismatch도 compatibility layer로 해결
- 관련 파일:
  - `selfdrive/modeld/SConscript`
  - `selfdrive/modeld/modeld.py`

#### b. UTM bring-up helper

- 시뮬레이터/preview 진입을 쉽게 하려고 helper 추가
- 관련 파일:
  - `selfdrive/test/__init__.py`
  - `selfdrive/test/helpers.py`

#### c. MetaDrive 렌더/시뮬레이터 bring-up

- MetaDrive 창 visible mode 테스트
- 시뮬레이터와 openpilot 전체 스택을 UTM에 띄워보는 작업 진행
- Honda Civic 2022 + 롱컨 off 상태를 기준으로 맞추는 쪽이 가장 안정적이었음
- 관련 파일:
  - `tools/sim/bridge/metadrive/metadrive_bridge.py`
  - `tools/sim/bridge/common.py`
  - `tools/sim/lib/common.py`
  - `tools/sim/lib/simulated_car.py`
  - `opendbc_repo/opendbc/car/car_helpers.py`

#### d. UTM 설정 화면 입력 문제 해결

- UTM preview에서 설정 버튼, Back 버튼, 일반 버튼들이 먹지 않던 문제 발생
- 원인:
  - `settings.h` 쪽 상태값 초기화 누락
  - PC/UTM에서 `window.cc` 입력 필터가 실제 클릭 이벤트를 막음
- 수정:
  - settings 상태값 기본 초기화
  - PC 환경에서 eventFilter가 입력을 과도하게 막지 않게 조정
- 관련 파일:
  - `selfdrive/ui/qt/offroad/settings.h`
  - `selfdrive/ui/qt/window.cc`

### 4-6. 이벤트/알림 카드 디자인 실험과 최종 되돌림

이 구간은 실험이 많았고, 최종적으로는 대부분 되돌아갔다.

#### 시도했던 것

- dark floating card 형태의 간결한 alert 디자인
- severity accent line
- `NOTICE`, `ATTENTION`, `TAKE OVER`, `SYSTEM` 같은 배지
- 제목/본문 중앙정렬
- full/mid/small 크기별 다른 타이포 조정
- 각 status별 색감/그라데이션 조정

#### 추가로 했던 세부 실험

- `NOTICE` 배지 제거
- `frogpilot` 상태 초록 하이라이트 복구
- full alert에서 title/description 간격 조정
- 모든 배지 제거
- background gradient 강도/높이/완만함 반복 조정

#### 최종 결론

- 여러 시도 끝에 사용자가 stock renderer가 더 낫다고 판단
- 따라서 이벤트 렌더는 기존 방식으로 완전히 되돌림

관련 파일:

- 실험 및 되돌림 중심 파일:
  - `selfdrive/ui/qt/onroad/alerts.cc`
  - `selfdrive/ui/qt/onroad/alerts.h`

보조로 사용한 미리보기 방식:

- `/data/error_logs/error.txt` 를 만들어 `openpilot crashed` 이벤트 강제 표시
- local/UTM용 preview 스크립트도 만들었지만 최종 커밋에는 포함되지 않은 것이 있음

### 4-7. onroad 대형 레이아웃 실험과 되돌림

사용자 제공 SVG(`Group 3`, `Group 10`, `Group 11`) 참고로 큰 onroad 개편도 시도했다.

#### 시도했던 것

- 속도/SET/LFA/휠/이벤트를 상단이 아니라 중단/하단으로 재배치
- 우상단 속도 레이아웃
- 하단 중앙 속도 + 우측 SET + 좌우 DM/휠 배치
- SVG 스타일의 이벤트 배경/카메라 오버레이
- `SET` 가독성 우선형 대형 카드

#### 결과

- 일부 실험은 사용자가 즉시 되돌림 요청
- 특히 아래는 rejected / reverted 상태:
  - 큰 `SET` 카드
  - 상단 정보 전체 재배치
  - SVG 기반 대형 onroad 전면 개편

#### 최종 accepted된 일부 요소

- 하단 그라데이션
- 속도/SET/휠/DM 배치 일부
- `files/icons/steeringwheel.png` 활용

관련 파일:

- `selfdrive/ui/qt/onroad/hud.cc`
- `selfdrive/ui/qt/onroad/hud.h`
- `selfdrive/ui/qt/onroad/annotated_camera.cc`
- `selfdrive/ui/qt/onroad/driver_monitoring.cc`
- `selfdrive/ui/qt/onroad/buttons.cc`

### 4-8. fullscreen 왼쪽 남색 띠 문제

- onroad fullscreen일 때 왼쪽에 남색 띠가 남는 문제 존재
- 여러 단계로 조정:
  - 마진 제거
  - overscan / left bias 조정
  - 카메라 edge-to-edge 확장
- 재부팅 후에도 잔여 픽셀이 남아 추가 조정
- 현재는 크게 줄어든 상태이나, 완전한 근본 원인은 `camera frame + overlay` 조합에서 생긴 여백으로 판단됨

관련 파일:

- `selfdrive/ui/qt/onroad/annotated_camera.cc`

### 4-9. offroad UI 전면 개편

사용자 요구사항에 따라 offroad 홈을 크게 바꿨다.

#### 기본 offroad 홈

- 배경 전체 검정
- 기본 sidebar 숨김
- 좌상단 원형 설정 버튼
- 타이틀: `안녕하세요 종혁님`
- 설명: `오늘도 편안한 주행 되세요`
- 아래 카드:
  - `주행 시간`
  - `주행 거리`
  - `주행 횟수`

#### 주행 종료 직후 offroad 홈

- onroad -> offroad 전환 직후 summary mode로 진입
- 타이틀: `주행이 종료되었습니다`
- 설명: `수고하셨습니다`
- 아래 카드:
  - `주행 시간`
  - `주행 거리`
  - `오픈파일럿 사용 비율`

#### 표시 시간

- 최근 주행 summary는 10분 동안 유지
- 이후 기본 greeting view로 자동 복귀

#### 시각/상호작용 추가 수정

- title / description 폰트 키우고 위로 올림
- 카드 내부 라벨에 같이 먹던 사각형 border 제거
- 설정 버튼 눌림 보정
- Back 버튼과 설정 내부 버튼들 동작하도록 수정

관련 파일:

- `selfdrive/ui/qt/home.cc`
- `selfdrive/ui/qt/home.h`
- `selfdrive/ui/qt/window.cc`
- `selfdrive/ui/qt/offroad/settings.h`

추가 note:

- UTM에서 최근 주행 요약을 강제로 띄우기 위한 preview 훅 존재
  - `OFFROAD_SUMMARY_PREVIEW=1`
  - 코드 위치: `selfdrive/ui/qt/home.cc`

### 4-10. 이벤트 문구 / FCW / steering 관련 포팅

#### a. `events_bf.py` 기준 events.py 반영

- 프로젝트 루트의 `events_bf.py` 를 참고해 현재 브랜치 `events.py` 에 존재하는 이벤트들만 문구 반영
- 일부는 현재 브랜치 의미가 달라서 의도적으로 미반영:
  - `noGps`
  - `canError`
  - `accel35`
  - `hal9000`
- 관련 파일:
  - `selfdrive/selfdrived/events.py`

#### b. FCW 관련 커밋 참조 반영

참고한 커밋:

- `0b43e7f77e09ac2a4981444a778a60b264f3f568`
- `6716db92917e9ddb0edd929aed76651c02a99c5f`
- `3d906b18e9646fee3df3363774174943d0bb1af2`
- `bcce25d409329486ec2e4d196979bd6cdd83b5fa`

적용 내용:

- TTC 기반 FCW 조건 보강
- FCW 문구를 `전방 추돌 주의 / 전방 차량과 추돌 위험이 있습니다` 로 수정
- `AudibleAlert.prompt` / duration 3s

관련 파일:

- `selfdrive/selfdrived/selfdrived.py`
- `selfdrive/selfdrived/events.py`

#### c. lead departing commit 참조 반영

참고 커밋:

- `99a430630430766562a610b6ced290836e3c275f`

적용 내용:

- `FrogPilotEventName.leadDeparting`
- full-screen 형태로 맞춤

관련 파일:

- `selfdrive/selfdrived/events.py`

#### d. steering 관련 sunnypilot 커밋 반영

참고 커밋:

- `6cdaee2491ac82529426a67a6b662496381a82c8`

적용 내용:

- `belowSteerSpeed` 문구 / full-screen / `VisualAlert.steerRequired`
- `preLaneChangeLeft/Right` 문구 수정
- `steerSaturated` 사운드 `warningSoft` 로 조정
- `mici` override 쪽도 맞춤

관련 파일:

- `selfdrive/selfdrived/events.py`

### 4-11. 사운드 동기화

- sunnypilot `staging-n` 의 `selfdrive/assets/sounds` 와 현재 프로젝트 사운드를 비교/동기화
- 결과적으로 주요 7개 사운드는 현재 프로젝트와 해시가 이미 동일했음
- 즉 작업은 수행했지만, 실제 사운드 내용 차이는 거의 없었음

관련 파일:

- `selfdrive/assets/sounds/prompt.wav`
- `selfdrive/assets/sounds/prompt_distracted.wav`
- `selfdrive/assets/sounds/refuse.wav`
- `selfdrive/assets/sounds/warning_immediate.wav`
- `selfdrive/assets/sounds/warning_soft.wav`

### 4-12. power monitoring 자동 종료 비활성화

참고 커밋:

- sunnypilot `22e9ea0155287c0fb7e7236ea1029371fdd2e057`

적용 내용:

- offroad 시간/전압/잔량 조건에 따른 자동 종료를 비활성화
- `should_shutdown()` 이 `False` 를 반환하도록 조정

관련 파일:

- `system/hardware/power_monitoring.py`

### 4-13. 콤마 기기 `mapd` / launch 관련 런타임 문제 해결

#### a. `process not running mapd`

- onroad 진입 후 engage가 안 되고 `process not running mapd` 이벤트 발생
- 원인:
  - manager가 죽은 프로세스의 stale `proc` 핸들을 잡고 있어 재시작 실패
- 수정:
  - stale process cleanup 로직 보완

관련 파일:

- `system/manager/process.py`

#### b. Python runtime 경로 문제

- launch path가 잘못된 Python 을 타며 재시작 흐름이 불안정
- `/usr/local/venv/bin/python` 우선 사용하도록 수정

관련 파일:

- `launch_chffrplus.sh`

### 4-14. `frogpilot_backups.py` race fix

- UTM에서 `openpilot crashed` 팝업 원인을 확인해보니 실제로는 toggle backup race로 `FileExistsError` 발생
- `_in_progress` backup 폴더 이름 충돌에 약했음
- 임시 이름 중복/기존 목적지 존재 상황에 더 안전하게 대응하도록 수정

관련 파일:

- `frogpilot/common/frogpilot_backups.py`

### 4-15. checked-in UI 바이너리와 device runtime env 문제

이 구간은 특히 중요하다.

#### a. UI 바이너리 체크인

- 현재 커스텀 UI 상태를 `selfdrive/ui/ui` 바이너리로 git에 포함시키기 위해 체크인
- 관련 커밋:
  - `c79b911e` `Check in compiled UI binary`

관련 파일:

- `selfdrive/ui/ui`

#### b. 사고: git pull 후 기기에서 UI가 안 뜨는 문제

- 로컬/수동 배포에서는 되던 것이, `git pull` 후 기기에서는 `ui` 실행 실패
- 원인:
  - 체크인된 `ui` 바이너리가 `libyuv` 런타임을 필요로 했음
  - `launch_env.sh` 에 weston/wayland 환경이 없어 display 연결 실패

증상:

- 실제 기기 전체 재부팅처럼 보였지만, 정확히는 openpilot launch loop / ui loop

#### c. 해결

- `launch_env.sh` 에 weston/wayland 환경 변수 추가
- `third_party/libyuv/larch64/lib` 아래 runtime library 포함
- 이후 `git pull` 기반 업데이트도 정상화

관련 커밋:

- `25b258d9` `Fix device UI runtime environment`

관련 파일:

- `launch_env.sh`
- `third_party/libyuv/larch64/lib/libyuv.so`
- `third_party/libyuv/larch64/lib/libyuv.so.0`
- `third_party/libyuv/larch64/lib/libyuv.so.0.0.1883`

### 4-16. blindspot 아이콘 기능 추가

이 기능은 현재 HEAD `8ac7cfd3` 기준 반영돼 있다.

#### 기능 요구사항

- 사각지대 차량 감지 시 좌우 화면 가장자리에 blindspot 아이콘 표시
- 같은 방향 깜빡이와 동시에 들어오면 아이콘이 빠르게 점멸

#### 구현 내용

- 좌우 고정 아이콘 로딩:
  - `files/icons/blindspot_left.png`
  - `files/icons/blindspot_right.png`
- `paintBlindspotIcons(QPainter &p)` 추가
- 실제 조건:
  - `blindspotLeft/right` 는 `carState`
  - `blinkerLeft/right` 도 `carState`
  - `blindspot && same-side blinker` 이면 빠른 점멸
- 점멸 간격:
  - 약 120ms

관련 파일:

- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h`
- `selfdrive/ui/qt/onroad/onroad_home.cc`

#### 위치 조정 및 geometry 보정

- 오른쪽 아이콘이 sidebar open/close 후 중앙으로 끌려오는 문제 발생
- `frogpilot_nvg->setGeometry(rect())` 반영
- 아이콘 좌표 기준을 overlay 내부 폭이 아니라 top-level width 기준으로 계산
- 오른쪽 마진은 여러 번 튜닝 후 현재 값 기준으로 정리

#### UTM preview 전용 환경값

`BLINDSPOT_PREVIEW` 지원값:

- `left`
- `right`
- `both`
- `1`
- `left-blink`
- `right-blink`
- `both-blink`

주의:

- UTM에서 preview는 실제 차 신호가 아니라 환경변수로 강제 표시하는 경로
- 실제 blindspot/blinker 조건과는 별개
- 이 preview 경로는 종종 offroad 전환, stale `ui.utm`, ForceOnroad 초기화, SSH timeout 때문에 불안정했음

#### 현재 검증 상태

- 로컬 repo HEAD와 콤마 기기 repo HEAD 모두 `8ac7cfd3`
- 기기 소스에 blindspot 관련 코드가 존재하는 것 확인
- 기기 `selfdrive/ui/ui` 바이너리 안에 아래 문자열 존재 확인:
  - `BLINDSPOT_PREVIEW`
  - `blindspot_left.png`
  - `blindspot_right.png`
- 즉 코드/바이너리 반영은 되었음
- 다만 실제 차량 주행 중 blindspot 감지 + blinker 상황에서의 최종 현장 검증은 별도 필요

### 4-17. 데모 데이터 preview 훅

onroad preview 시 화면이 비어 보이는 문제를 완화하기 위해 일부 demo 값 훅이 들어갔다.

- `ForceOnroad` 이고 실제 `carState`/`driverState` 가 아직 안 들어온 preview 상황에서만:
  - 현재 속도 demo 값
  - SET 속도 demo 값
  - DM, wheel 표시 강제

중요:

- 이 demo 값은 실제 onroad 주행 경로에는 적용되지 않음
- preview 전용 분기

관련 파일:

- `selfdrive/ui/qt/onroad/hud.cc`
- `selfdrive/ui/qt/onroad/driver_monitoring.cc`
- `selfdrive/ui/qt/onroad/buttons.cc`

## 5. 현재 repo / 기기 / UTM 상태 정리

### 5-1. 현재 원격 브랜치

- `origin/testing-v1`
- HEAD: `8ac7cfd3`

### 5-2. 현재 콤마 기기 확인 사항

2026-03-18 확인 기준:

- `/data/openpilot` HEAD도 `8ac7cfd`
- 기기 `ui` 프로세스 실행 중 확인
- 소스와 바이너리 모두 blindspot 문자열 포함 확인

### 5-3. 현재 UTM 상태 특성

- UTM은 "실차와 동일한 런타임" 이 아니라 "host preview 환경" 으로 보는 게 맞음
- 가장 안정적인 확인 방법:
  - `tools/utm/force_onroad_preview.sh`
  - 필요 시 `BLINDSPOT_PREVIEW=...`
- 단, UTM에서는 다음 문제가 반복적으로 있었다:
  - `ForceOnroad` 가 manager start 때 지워짐
  - SSH `banner exchange timeout`
  - 예전 `ui.utm` 창이 남아 새 빌드를 못 보는 착시
  - `frogpilotPlan` 이 항상 안정적으로 살아 있지 않음

## 6. 현재 유지해야 하는 중요한 결정 사항

- 이벤트 렌더는 stock renderer 유지
- large `SET` 카드 실험은 되살리지 말 것
- SVG 기반 onroad 대형 개편은 되살리지 말 것
- offroad 커스텀 홈/최근 주행 요약 구조는 현재 accepted 상태
- 기기에서 직접 `scons selfdrive/ui/ui` 빌드는 비권장
- checked-in `ui` 바이너리를 쓰는 경우 `launch_env.sh` 와 `libyuv` 런타임을 반드시 같이 유지할 것

## 7. 앞으로 작업 시 권장 workflow

### UTM 미리보기

1. 로컬에서 소스 수정
2. UTM 저장소에 동기화
3. UTM에서 host UI 빌드
4. `tools/utm/force_onroad_preview.sh` 로 onroad preview
5. 필요 시 env:
   - `BLINDSPOT_PREVIEW=both`
   - `BLINDSPOT_PREVIEW=both-blink`
   - `OFFROAD_SUMMARY_PREVIEW=1`

### 실제 콤마 기기 배포

1. 로컬 수정 완료
2. 필요한 경우 device-ABI `selfdrive/ui/ui` 확인
3. `git push` 또는 직접 배포
4. 기기에서 `git pull` 후 `ui` / `mapd` 상태 확인
5. display/libyuv 문제 시 `launch_env.sh` 확인

## 8. 현재 남아 있는 로컬 untracked 참고 파일

아래는 현재 로컬에 있지만 커밋하지 않은 참고/잡파일들이다.
다음 작업자가 PR/추가 커밋 전 반드시 의식하고 정리해야 한다.

- `.DS_Store`
- 각종 `__pycache__`
- `events_bf.py`
- `files/Group 3.svg`
- `files/Group 10.svg`
- `files/Group 11.png`
- `files/icons/collision.png`
- `files/icons/handon.png`
- `files/icons/lanecrossing.png`
- `files/icons/lfa.png`
- 기타 한글 파일명 SVG 레퍼런스
- `tools/utm/cycle_alert_previews.sh`
- `tools/utm/cycle_real_event_previews.sh`

중요:

- blindspot용 `blindspot_left.png`, `blindspot_right.png` 는 이미 커밋됨
- 나머지 아이콘/레퍼런스 이미지는 아직 공식 반영 대상 아님

## 9. 다음 작업자가 가장 먼저 봐야 할 파일

UI 쪽:

- `selfdrive/ui/qt/onroad/hud.cc`
- `selfdrive/ui/qt/onroad/annotated_camera.cc`
- `selfdrive/ui/qt/onroad/model.cc`
- `selfdrive/ui/qt/onroad/alerts.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `selfdrive/ui/qt/home.cc`

Runtime / deploy 쪽:

- `launch_env.sh`
- `launch_chffrplus.sh`
- `system/manager/process.py`
- `system/hardware/power_monitoring.py`
- `tools/utm/force_onroad_preview.sh`

## 10. 가장 중요한 함정 요약

- `ForceOnroad` 는 manager 시작 전에 넣으면 지워진다
- UTM에서 `ui.utm` 이 떠 있다고 해서 새 빌드를 보고 있는 건 아닐 수 있다
- 직접 기기 빌드는 리소스/안정성 문제로 종종 실패한다
- checked-in `ui` 바이너리만 올리고 runtime env를 안 올리면 기기에서 launch loop가 날 수 있다
- blindspot preview가 UTM에서 불안정하다고 해서 실제 기기 로직까지 실패라고 단정하면 안 된다
- 실제 차량 신호 기반 기능은 최종적으로 콤마 기기에서 확인해야 한다

이 문서는 여기까지 작업한 모든 주요 변경과 시행착오를 기록한 기준 문서다.
새 작업을 시작할 때는 `RELEASES.md` 가 아니라 이 파일부터 읽는 것을 권장한다.

## 추가: 2026-03-24 ~ 2026-03-31 APN / SDI / Fake-Long / 웹 디버그 / UTM 디자인 작업

이 섹션은 2026-03-24 이후 진행한 `APN 안전운전 모드`, `SDI 타입 해석`, `GM fake-long 버튼 스니핑`, `웹 디버그 대시보드`, `UTM onroad 프리뷰 디자인` 작업을 처음부터 이어서 정리한다.

이 구간의 목적은 크게 5가지였다.

1. `CarrotNavi(APN)` 에서 들어오는 카메라 / 구간단속 / 일반 SDI 정보를 정확히 해석하고 onroad UI에 반영
2. 안전운전 모드(`routeActive = false`) 에서도 카메라/구간단속 정보가 있으면 onroad / fake-long에 전달
3. GM stock ACC 차량에서 `FakeLong / APN-Fake-Long / FakeLongTestUI / 웹 디버그 버튼`을 통해 synthetic cruise button 테스트
4. `웹세팅(device_dashboard_mock)` 을 주행 중에도 덜 무겁게 동작하게 경량화
5. UTM 프리뷰로 보호구역 / 카메라 / 구간단속 / 일반 SDI 디자인을 계속 검증

---

## A. 작업 환경 / 장치 / 빌드 경로

### 실기기

- `192.168.0.11`
  - 초반 APN / 자동밝기 / safety-driving-mode 카메라 표시 확인용
- `10.85.212.127`
  - 후반 주력 테스트 기기
  - 웹세팅, fake-long, 디버그탭, 최신 APN/UI 반영 대부분 이 기기로 검증

### UTM

- IP: `192.168.64.3`
- user: `hyuklee`
- sysroot: `/home/hyuklee/comma-sysroot`

### 배포/빌드에서 배운 점

- `ui` 변경은 가급적 `UTM device-ABI 빌드` 후 기기에 복사
- Python / web 파일은 기기에 직접 복사 후 서비스 재시작으로 충분
- `기기 파일이 바뀌었다`와 `실제 서버 응답 / 실제 화면이 바뀌었다`는 다르므로:
  - 웹은 반드시 `실제 HTML 응답` 확인
  - UI는 가능하면 `UTM 캡처` 또는 `기기 화면`으로 검증
- 한 번 `UTM`에서 빌드한 `ui`가 기기 Qt ABI와 맞지 않아 `QPushButton::hitButton` unresolved symbol로 죽은 적이 있음
  - 이때는 이전 정상 `ui` 로 즉시 되돌림
  - 이후 `device-ABI` 기준으로 다시 빌드하는 쪽으로 정리

---

## B. 자동 밝기(auto brightness) 분석과 수정

### 문제

- 저녁에 가로등/앞차 불빛이 있어도 밝기가 너무 빨리 `10%` 근처까지 내려감
- 사용자가 원하는 동작은:
  - `완전히 아주 어두울 때만 10 아래`
  - `웬만한 저녁 / 가로등 있음 / 앞차 있음` 에서는 `10 이하로 내려가지 않기`

### 분석

- `selfdrive/ui/ui.cc`
  - `wideRoadCameraState.exposureValPercent` 를 이용해 가짜 `light_sensor` 를 만듦
  - 이후 밝기 계산에서 다시 `CIE 1931` 변환을 타면서 중간 밝기 구간이 두 번 눌림
- 결과적으로 저녁 구간이 과도하게 어두워졌음

### 수정

- 완전 암흑에 가까운 구간에서만 `10 이하`가 허용되도록 기준 조정
- 일반 저녁 / 부분 조명에서는 최소 바닥을 사실상 `10` 이상으로 유지

### 기기 반영

- 초기에 `192.168.0.11` 로 반영하여 사용자가 실제 야간 체감 확인

### 상태

- 이 부분은 사용자 요구대로 정리된 상태
- 추가 조정이 필요하면 다시 `ui.cc` 곡선만 손보면 됨

---

## C. APN / CarrotNavi 브리지: 안전운전 모드 카메라 정보 살리기

### 원래 문제

- 경로 탐색(routeActive)이 아닐 때는 APN 브리지가 데이터를 막는 구조였음
- 하지만 실제 Tmap 안전운전 모드에서도:
  - 과속카메라
  - 구간단속
  정보는 들어옴
- 웹 디버그에는 보이는데 onroad UI / fake-long 쪽에선 안 보이는 상황이 반복됨

### 1차 수정

- `tools/apn_bridge/apn_bridge.py`
- `routeActive=false` 여도
  - `camera`
  - `section_camera`
  - 거리 / 제한속도
  가 있으면 `APNDataActive` 를 켜도록 수정
- 대신 안전운전 모드에서 자주 헷갈리는
  - `현재 도로 제한속도`
  - `도로명`
  은 계속 `routeActive=true` 일 때만 노출되게 유지

### 2차 수정: onroad에는 안 뜨는 원인

원인은 두 가지였다.

1. `APNDataTimestamp`
   - upstream payload time을 써서 freshness 판정이 엉킬 수 있었음
   - `apn_bridge.py` 에서 `receivedAt` 기준으로 저장하도록 수정

2. `APNNextSpeedLimit`
   - `nSdiPlusSpeedLimit = 0` 인데도 그 값을 먼저 집어버려서
   - `APNNextSpeedLimit = 0`
   - 결과적으로 웹엔 raw가 보여도 onroad는 speed limit 없는 hazard로 봄
   - base `nSdiSpeedLimit` 을 우선/대체 사용하도록 수정

### 현재 상태

- 안전운전 모드에서도 카메라/구간단속 정보는 `APNDataActive` 기준으로 내려감
- `APNNextHazard`, `APNNextHazardDistance`, `APNNextSpeedLimit` 은 camera-only 상황에서도 memory param으로 유지
- `APNSpeedLimit`, `APNRoadName` 은 routeActive 때만 신뢰하도록 유지

### 관련 파일

- `tools/apn_bridge/apn_bridge.py`
- `frogpilot/common/frogpilot_variables.py`
- `frogpilot/controls/lib/speed_limit_controller.py`
- `selfdrive/ui/qt/onroad/hud.cc`

---

## D. APN / SDI 타입 해석: nSdiSection가 아니라 nSdiType 중심으로 재정리

### 문제

- 구간단속이 아닌데도 구간단속 아이콘이 뜨는 문제가 있었음
- 원래 브리지 로직이 `nSdiSection > 0` 비슷한 조건에 과도하게 기대고 있었음
- 실제로는 일반 카메라에도 `section` 성격의 부가값이 붙을 수 있어서 오분류됨

### 정리된 해석

- `nSdiType`
  - 어떤 SDI인지 결정하는 메인 코드
- `nSdiSection`
  - 구간 관련 보조 상태값
- `bSdiBlockSection`, `nSdiBlockType`, `nSdiBlockDist`, `nSdiBlockSpeed`
  - 실제 구간 진행 여부/블록 상태를 보는 데 더 중요

### 중요 전환점

사용자가 `SdiCodeConvert.SdiType` 기준 authoritative mapping을 제공했고, 이걸 기준으로 해석 기준을 완전히 재정리함.

핵심 결론:

- 웹 라벨 시그니처의 `Txx` 는 `nSdiType` 와 동일
- 예: `T29|S2|...` 에서 `T29 = 사고다발`
- 따라서 `S2` 만 보고 구간단속이라고 판단하면 안 됨

### 현재 카테고리 분류 기준

`tools/apn_bridge/apn_bridge.py` 기준:

- `camera` 로 처리
  - `0, 1, 5, 6, 7, 8, 9, 10, 64, 65, 75, 76`
- `section_camera` 로 처리
  - `2, 3, 4, 84, 85`
- 나머지
  - `sdi:<type>`
  - 일반 SDI로 분리

### 현재 의미

- 일반 과속/신호/후면/특수 단속은 `camera:*`
- 구간단속 시작/끝/진행중/가변 구간단속은 `section_camera:*`
- 보호구역, 휴게소, 사고다발, 낙석, 결빙, 재난 정보 등은 일반 `sdi:*`

---

## E. SDI 라벨링 / 웹 디버그 라벨 기능

실차에서 타입 매핑을 더 쉽게 하려고 웹 디버그에 라벨 기능을 여러 번 붙였다.

### SDI 시그니처 라벨

- 현재 들어온 SDI를 시그니처로 보여줌
  - 예: `T17|S2|P0|B0|BS0|C0|L0`
- 현재 시그니처에 임시 라벨 저장 가능
- JSON으로 보존
- 나중에 이 파일을 읽어서 타입 해석 보강 가능

### CAN / 차량 상태 라벨

- 처음엔 CAN 후보만 저장하려 했지만
- 이후 차량 상태 값도 같이 라벨 가능하게 확장
- 다만 실차에서는 라벨링보다 “실시간 같이 보면서 잡는 방식”이 더 효율적이었음

### 최근 관측값 유지

- 신호가 잠깐 사라져도 바로 안 사라지게
- `LIVE` / `RECENT` 표시 추가
- 마지막 관측 시간 보존

### 저장 위치

- SDI 라벨: `/data/media/0/apn_bridge/sdi_labels.json`
- CAN/차량 상태 라벨: `/data/media/0/button_sniff/can_labels.json`

---

## F. GM ACC 표시속도 / 현재속도 보정

### 문제

- GM 차량에서 클러스터에 보이는 ACC set 속도와 openpilot 내부 raw set speed가 다름
- 사용자가 실차로 `계기판상 ACC 속도 ↔ 오픈파일럿상 ACC 속도` 대응표를 직접 제공함

### 해결

- `opendbc_repo/opendbc/car/gm/cluster_speed.py` 추가
- ACC set speed용 보정 테이블 구현
- `opendbc_repo/opendbc/car/gm/carstate.py`
  - `cruiseState.speedCluster`
  - `vEgoCluster`
  를 보정값으로 채움
- `opendbc_repo/opendbc/car/gm/carcontroller.py`
  - fake-long / APN-fake-long 비교는 보정된 표시 ACC 속도 기준 사용

### 결과

- HUD에 보이는 ACC 속도는 계기판과 더 잘 맞음
- 현재속도도 같은 계열 보정이 들어가서 표시 기준이 맞춰짐

### 주의

- 이건 “실제 cluster CAN 신호를 찾은 것”이 아니라 lookup table 기반 보정
- 나중에 진짜 cluster CAN을 찾으면 보정층은 제거 가능

---

## G. APN-Fake-Long / fake-long 상태 표시와 현재 한계

### 1. APN-Fake-Long 자체 활성 조건

- `FakeLong` 메인 토글이 꼭 켜져 있지 않아도
- `APNFakeLong` 만 켜져 있으면 controller 경로는 활성되게 정리

### 2. HUD 주황/초록/파랑 표시 관련 문제

사용자 기대:

- 과속카메라 접근해 감속 중일 때만 주황색 목표 속도 깜빡임
- 목표 속도 근처 도달 시 초록색
- 카메라 지나고 원래 ACC로 복귀하면서 실제 `RES` 버튼 스니핑할 때만 파란색
- OP 해제 시 파란 잔상 남지 않기

시도한 수정:

- `carcontroller.py` 에 `apnControlActive`, `apnRecoveryActive` 류 상태를 명확히 기록
- `hud.cc` 쪽에서 이걸 더 직접적으로 읽도록 정리
- `op_enabled` 아닐 때 세션 상태 클리어
- 복귀 상태가 새 카메라에 의해 덮일 때 stale blue state 제거

### 현재 상태

- stale recovery/blue 문제는 여러 차례 줄였으나
- synthetic `SET/RES` 자체가 완전히 먹지 않는 문제가 남아 있어 “표시와 실제 동작”의 완전 일치는 아직 미완료

---

## H. GM fake-long / 테스트 버튼 / 웹 디버그탭: 무엇이 되고 무엇이 안 되는가

이 문서에서 가장 중요한 미해결 이슈다.

### 원래 목표

- stock ACC 차량에서 `SET / RES / MAIN / CANCEL / UNPRESS` 를 synthetic cruise button으로 보내서
  - fake-long
  - APN-fake-long
  - 웹 디버그탭
  - onroad FakeLongTestUI
  전부 같은 경로로 검증하고 싶었음

### 이미 해결된 것

1. `FakeLongTestUI` / `FakeLong` / `APNFakeLong` 켜졌을 때 safety bit 경로
   - `opendbc_repo/opendbc/car/interfaces.py`
   - `frogpilot/frogpilot_process.py`
   - live `FrogPilotCarParams` 와 persistent 모두에 fake-long button safety bit 동기화

2. `FakeLongDebug` memory param
   - `armed`, `paused`, `userSet`, `target`, `commanded`, `last`, `apn*`, `raw*` 등 노출

3. 웹 디버그탭 → 백엔드 → memory param → carcontroller 경로
   - 이 경로는 실제로 정상
   - `FakeLongTestButton(memory)` 가 실제 소비됨
   - `FakeLongDebug.last` 도 바뀜

4. `cancel`
   - synthetic 경로에서도 실제로 먹음

### 실제로 막힌 지점

- `SET / RES` 는 synthetic 경로에서 실제 ACC set speed를 안 바꿈
- 여러 번 실차 캡처 결과:
  - 실제 핸들 버튼: `0x1E1`, `src 0/130`
  - synthetic 버튼: `0x1E1`, `src 192/194`
  - payload 바이트 자체는 동일
  - 그럼에도 `cancel`만 먹고 `set/res`는 무시

### 비교해서 확인한 구조 차이

예전 “잘 먹히던 시기”와 현재의 차이:

- 예전:
  - `FakeLongTestButton = "set:timestamp"` 같은 plain string
  - `carcontroller.py` 가 그걸 직접 소비
  - `camera-only`
  - 단순 press + 짧은 delayed `UNPRESS`
- 현재(문제 시점):
  - 웹 JSON payload
  - `pt/camera/both/obstacle/all`
  - repeats / holdFrames / release mode
  - APN 상태머신까지 섞인 복잡한 테스트 경로

### 이번에 다시 되돌린 구조

2026-03-31 기준 현재 코드:

- `carcontroller.py`
  - fake-long test payload는
    - JSON
    - legacy `"set:timestamp"` string
    둘 다 허용
  - fake-long button 경로를 `camera-only` 로 강제
  - 기본 repeat count `1`
  - default press/release 도 camera-only
- `tools/device_dashboard_mock/server.py`
  - 웹 디버그 버튼 payload는 bus 선택과 무관하게 `camera` 로 normalize
- `tools/device_dashboard_mock/index.html`
- `tools/device_dashboard_mock/app.js`
  - 디버그탭 bus selector는 `카메라 송신 전용` 1개만 남김
  - `PT/양쪽/레이더/전부` 프리셋 제거

### 여기까지 내려온 결론

- panda safety bit 경로가 아예 틀린 건 아님
- 웹 디버그탭이 고장난 것도 아님
- 현재 최상위 미해결은:
  - **GM 차량이 synthetic `SET/RES`만 거부하고 `CANCEL`은 받아먹는 이유**

### 다음 작업자에게 추천하는 다음 단계

1. `camera-only` 로 단순화한 현 상태에서 다시 실차 테스트
2. still fail이면:
   - `0x1E1` 실차 핸들 입력과 synthetic 입력의
     - press 길이
     - release timing
     - counter
     - engaged 조건
     차이를 더 파기
3. 절대 “panda가 다 막고 있다”로 단정하지 말 것
   - `cancel`은 이미 먹는다
4. 반대로 “web/debug path가 고장”이라고도 단정하지 말 것
   - param 소비와 `FakeLongDebug.last` 갱신은 이미 확인됨

---

## I. commIssue / locationd / alertDebug 폭주: 웹세팅 경량화

### 문제

- 주행 중 웹세팅이나 모니터링을 열면
  - `commIssue`
  - `locationd`
  - `liveParameters`
  - `liveTorqueParameters`
  - `driverAssistance`
  - `alertDebug`
  등 경고가 한꺼번에 뜨는 경우가 있었음

### 원인

- 초기 웹 대시보드가 너무 공격적으로 정보를 불러왔음
  - 상태
  - 로그
  - params
  - 통계
  - live CAN
  를 동시에 많이 polling
- 또 한동안 실시간 SSH 스트리밍/캡처도 겹쳐서 load를 키움

### 해결 방향

1. 상태탭 기본은 light
   - `server.py`
   - `/api/status?detail=lite`
   - 상단 핵심 정보만 즉시 반환

2. 세부 정보는 `더보기`
   - `statusDetailsWrap`
   - 눌러야 상세 상태 / CAN 등 polling

3. CAN 모니터링은 기본 OFF
   - 별도 버튼을 눌러야 `/api/can-debug`
   - 평소엔 안 돌게

4. 로그/통계도 lazy-load
   - 탭 열기 전엔 불필요 polling 최소화

5. `alertDebug` 같은 ignore 대상은 commIssue 범인처럼 보이지 않게 selfdrived 쪽 필터 조정
   - `selfdrive/selfdrived/selfdrived.py`
   - `selfdrive/selfdrived/events.py`

### 현재 효과

- 이전보다 웹세팅 열 때 리소스 부담이 줄었음
- 다만 아주 공격적인 실시간 캡처를 또 켜면 여전히 부담 줄 수 있음

### 원칙

- 주행 중에는 SSH 실시간 스트리밍보다 기기 내부 파일 캡처 후 나중에 읽기

---

## J. 저부하 캡처 / 모니터링 전략

### 실패한 방식

- 실시간 SSH 출력 + high-frequency CAN 구독
- 결과:
  - 워닝 이벤트 다수
  - 리소스 급증
  - 한 번은 메모리 경고/불안정성까지 유발

### 이후 정리

- 시트 진동 스니핑용 별도 스크립트는 제거
- `vehicle_sniffer.py` 류 실험 경로 정리

### 현재 원칙

- 필요할 때만 짧은 low-overhead file capture
- 주행 끝나고 파일 분석

### 참고 파일

- `tools/debug/fake_long_capture.py`
- `tools/debug/run_fake_long_capture.sh`
- `tools/debug/stop_fake_long_capture.sh`

이 경로는 반복적으로 수정되었고, 현재도 “필요할 때만 켜는 것”이 원칙이다.

---

## K. 기기 웹세팅(device_dashboard_mock) 디자인 / UX 정리

2026-03-29~03-31 구간에 웹 대시보드 UI를 크게 손봤다.

### 상단 헤더

- 제목을 `Openpilot Console` 로 통일
- 연결 상태는 긴 카드 대신
  - 초록 점
  - `연결됨`
  텍스트만 남긴 소형 칩으로 축소
- 모바일/PC 모두 한 줄 배치를 유지하도록 CSS 정리

### 상태탭

- 상단 핵심 요약만 항상 노출
- 상세 상태는 `더보기`
- 모바일 기준으로 세로 높이 줄이기

### 통계탭

- 한때 카드가 너무 많아 모바일에서 스크롤 과다
- 이후 계속 압축하면서:
  - 겹치는 값 제거
  - `운행 개요` 섹션 삭제
  - `주행 성향`, `날씨별 주행`, `이벤트 / 개구리 통계` 제거
  - `긴급 제동 경고`만 남김
  - hero / pulse / summary 구조 재정리

중요한 해석:

- `총 주행 횟수`, `총 주행 거리`, `총 주행 시간`
  - openpilot engaged 기준이 아니라, FrogPilot tracked driving 기준에 가까움
- `최고 가속도`
  - 수동 운전 때 값도 포함될 수 있음

### 디버그탭

- 새 탭 `디버그` 추가
- 목적:
  - fake-long button test
  - runtime/safety 상태 확인
  - 마지막 payload / FakeLongDebug viewer 확인

### 2026-03-31 기준 디버그탭 최종 상태

- `SET / RES / MAIN / CANCEL / UNPRESS`
- bus는 `카메라 송신 전용`
- `tap / press / release`
- repeats / hold frames 선택 가능

주의:

- 실제 버튼이 차에 먹으려면 `FakeLongTestUI = ON`
- `FakeLong = OFF`, `APNFakeLong = OFF`, `FakeLongTestUI = ON` 조합이 디버그 실험엔 가장 깔끔

---

## L. onroad SDI / APN 디자인 작업 (UTM preview 기반)

이 구간에서 UTM을 매우 많이 사용했다.

### 보호구역 계열

최종 현재 방향:

- `20, 21` 어린이 보호
- `66, 67` 장애인 보호
- `68, 69` 노인 보호
- `70, 71` 주민 보호

표시 스타일:

- 흰 원
- 두꺼운 파란 외곽 띠
- 검은 텍스트
- 아래 거리 박스는 단속 아이콘급으로 확대

### 단속 카메라 계열 세부 디자인

- `1, 7, 8, 75`
  - 일반 과속단속
  - 숫자 중심형
  - 일반 속도단속은 `단속` 텍스트 제거
- `0, 6, 76`
  - 신호단속 계열
- `5`
  - 숫자 없이 `꼬리물기`
- `9`
  - 숫자 없이 `버스 차로`
- `10`
  - 숫자 없이 `가변 차로`
- `64`
  - 숫자 없이 `노후`
- `65`
  - 숫자 없이 `차선변경`
- `75`
  - 숫자 위로 올리고 아래에 `후면`
- `76`
  - 기존 신호과속 형식 + 아래 `후면`

### 구간단속 진행 중

여러 안을 시도했다.

시도한 안:

- 노란색 유지
- `구간중` 상단 띠
- 바깥 진행 링
- `잔여` 하단 박스
- 아이콘 우측 세로 진행바

사용자 피드백 후 현재 방향:

- 상단 `구간중` 띠 제거
- 바깥 진행 링 제거
- 아이콘 우측 빨간 세로 진행바
- 아래 `잔여` 박스 유지
- 아이콘 본체는 기존보다 어두운 빨간 계열로 조정

### 주의 / 위험 SDI

- 처음에는 삼각형 표지판풍으로 여러 번 시도
- 이후 `노란 원 + 빨간 테두리 + 검은 글씨` 로 방향 전환
- 하지만 최종적으로 사용자가:
  - “주의/경고 타입 SDI는 live onroad UI에는 안 뜨게”
  요청

현재 상태:

- warning 디자인 실험 코드는 남아 있지만
- live onroad 표시에서는 warning 계열을 숨김

### 매우 중요한 원칙

- UTM preview 결과는 항상 실제 캡처 이미지로 확인할 것
- 말로 “됐다”고 하지 말고 캡처로 검증할 것
- 예전에 stale `APNLastRGData` 때문에 preview 스크립트가 다른 타입을 그리던 문제가 있었음

관련 파일:

- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h`
- `tools/utm/show_apn_fake_long_preview.sh`

---

## M. fake-long / APN 관련 실제 기기 배포 시 주의

### 무한부팅처럼 보였던 문제

실제 원인은 “기기 재부팅”이 아니라 manager crash loop였다.

원인:

- `carcontroller.py` 에서 새 모듈을 import 하도록 바뀌었는데
- 기기에 `opendbc_repo/opendbc/car/gm/cluster_speed.py` 를 안 올려서
- `ModuleNotFoundError`

증상:

- 사용자는 무한부팅처럼 보임

해결:

- `cluster_speed.py` 기기에 추가 복사
- 이후 manager 정상 기동

### 교훈

- `carcontroller.py`, `carstate.py` 같이 올릴 때
  - 새 helper module import 추가 여부를 반드시 같이 확인

---

## N. 2026-03-31 시점 현재 상태 요약

### 잘 된 것

- 자동 밝기 저녁/암흑 구분
- safety-driving mode 카메라/구간단속 브리지
- `nSdiType` authoritative mapping 적용
- 일반 SDI / 보호구역 / 특수 단속 디자인 정리
- 웹세팅 경량화
- 디버그탭 추가
- fake-long safety bit live sync
- current speed / ACC set speed cluster 보정

### 아직 핵심 미해결

- **synthetic `SET / RES` 가 실제 차량에서 안정적으로 먹지 않음**
  - `cancel` 은 먹음
  - `SET/RES` 는 memory/debug/path는 다 정상인데 차가 무시

### 현재 디버그탭 의미

- 웹 탭이 고장인지 확인하는 용도는 이미 끝남
- 지금은 `synthetic SET/RES 수용성` 실험용 도구

### 현재 추천 테스트 순서

1. `FakeLong = OFF`
2. `APNFakeLong = OFF`
3. `FakeLongTestUI = ON`
4. stock ACC 실제 engaged
5. 웹 디버그탭에서 `camera-only` `SET/RES/CANCEL`
6. `FakeLongDebug.last`, ACC set speed, 실제 클러스터 반응 비교

---

## O. 다음 작업자가 바로 이어서 해야 하는 우선순위

### 1순위: synthetic SET/RES

- `opendbc_repo/opendbc/car/gm/carcontroller.py`
- `opendbc_repo/opendbc/safety/modes/gm.h`
- 실제 휠 `0x1E1` 와 synthetic `0x1E1` 를 계속 비교
- 특히:
  - press 길이
  - release timing
  - counter
  - engaged 조건
  - camera path 외 추가 조건
  를 다시 볼 것

### 2순위: APN-fake-long 실제 감속/복귀

- synthetic `SET/RES` 가 풀려야
  - 주황 감속
  - 초록 목표 도달
  - 파란 복귀
  가 완전히 닫힌다

### 3순위: SDI 타입별 세부 UI 확대

- warning 계열은 일단 live 숨김
- 필요하면 나중에 별도 policy 정해서 다시 살릴 것

### 4순위: 웹세팅 안정화

- 지금도 “기본은 가볍게, 상세는 눌렀을 때만” 원칙 유지
- 주행 중 새 무거운 polling 추가하지 말 것

---

## P. 다음 작업자가 절대 놓치면 안 되는 포인트

- `nSdiSection` 만 보고 구간단속으로 판단하지 말 것
- `Txx` 시그니처의 `T` 값은 authoritative `nSdiType`
- `APNDataTimestamp` 는 receivedAt 기준이어야 freshness가 맞음
- `nSdiPlusSpeedLimit = 0` 이라고 `APNNextSpeedLimit` 도 0으로 만들지 말 것
- `FakeLongTestUI` 가 켜져 있어야 웹 디버그 버튼 테스트가 의미 있음
- `cancel` 이 먹는다고 `set/res` 도 먹는다고 생각하지 말 것
- `web debug -> memory param -> carcontroller` 경로는 이미 정상
- 실차 검증 없이 `됐다`고 단정하지 말 것
- UTM preview는 반드시 캡처로 검증할 것

---

## Q. 커밋 기준으로 보면 어디서 무엇이 바뀌었는가

이 문서는 워킹트리까지 포함한 인수인계 문서이기 때문에, 아래처럼 **커밋으로 이미 남아 있는 변경**과 **아직 미커밋인 변경**을 분리해서 봐야 한다.

### 현재 브랜치 / 기준 HEAD

- 브랜치: `testing-v1-apn`
- 문서 작성 시점 HEAD: `4b7feec3`
- 메시지: `Refresh UTM-built device UI artifacts`

즉, 아래에 적는 `2026-03-24 ~ 2026-03-31` 작업 중 상당수는 **`4b7feec3` 이후 워킹트리에만 존재**한다.
다음 작업자는 반드시:

1. `git status`
2. `git diff`
3. `HISTORY.md` 의 이 섹션

을 함께 보면서 “이건 이미 커밋된 것인지, 아직 로컬 수정인지”를 구분해야 한다.

### 현재 구간에서 특히 의미 있는 커밋 앵커

#### `f8b34fb3` `Add fake-long tools and device dashboard`

이 커밋은 지금 작업의 출발점이다.

- fake-long 관련 최초 기반 추가:
  - `common/params_keys.h`
  - `frogpilot/common/frogpilot_variables.py`
  - `frogpilot/ui/qt/offroad/vehicle_settings.cc`
  - `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
  - `opendbc_repo/opendbc/car/gm/carcontroller.py`
  - `opendbc_repo/opendbc/car/interfaces.py`
  - `opendbc_repo/opendbc/safety/modes/gm.h`
- 웹 대시보드 최초 추가:
  - `tools/device_dashboard_mock/server.py`
  - `tools/device_dashboard_mock/index.html`
  - `tools/device_dashboard_mock/app.js`
  - `tools/device_dashboard_mock/styles.css`

중요:

- 예전 “잘 먹던” fake-long test 구조는 사실상 이 커밋 시절의 단순 구조를 기준으로 이해해야 한다
- 이번에 synthetic `SET/RES` 문제를 다시 파면서 계속 이 커밋과 현재를 비교했다

#### `19684af8` `Add power logic override toggle`

- 전원 종료 로직 override 토글 추가
- 관련:
  - `system/hardware/power_monitoring.py`
  - `frogpilot/common/frogpilot_variables.py`
  - `frogpilot/ui/qt/offroad/device_settings.cc`

직접 APN/fake-long 작업은 아니지만, 현재 HISTORY 흐름의 직전 커밋 앵커다.

#### `850d6eb4` `Add APN bridge and onroad UI integration`

이 커밋이 현재 APN 기능의 본격 시작점이다.

- `tools/apn_bridge/apn_bridge.py`
- `tools/apn_bridge/apn_compat.py`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `selfdrive/ui/qt/onroad/hud.cc`
- `frogpilot/controls/lib/speed_limit_controller.py`

즉:

- APN memory param 체계
- APN bridge
- onroad APN 카메라 표시

는 이 커밋이 기초다.

#### `d066851e` `Improve APN dashboard status panel`

- APN 상태를 웹 대시보드에서 더 보기 좋게 만든 커밋
- 관련 파일:
  - `tools/device_dashboard_mock/server.py`
  - `tools/device_dashboard_mock/index.html`
  - `tools/device_dashboard_mock/app.js`
  - `tools/device_dashboard_mock/styles.css`

현재 이후의 웹세팅 개편도 모두 이 구조 위에 얹혀 있다.

#### `3dcacf23` `Add UTM device-ABI build helper`

- `tools/utm/build_device_abi.sh`

의미:

- 이후 `ui`/`params_pyx.so` 를 UTM device-ABI로 빌드해서 기기에 올리는 작업의 핵심 기반
- `QPushButton::hitButton` ABI mismatch 문제를 겪은 뒤 더 중요해졌다

#### `4b7feec3` `Refresh UTM-built device UI artifacts`

- `selfdrive/ui/ui`
- `common/params_pyx.so`

만 갱신한 체크인 커밋이다.

중요:

- 현재 브랜치 HEAD가 여기이므로
- **이 이후 대화에서 계속 진행한 APN / SDI / fake-long / 웹 디버그 / 디자인 작업은 대부분 아직 미커밋이다**

#### `75a74e16` `Tune brightness floor and stop-and-go alerts`

- `selfdrive/ui/ui.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `selfdrive/ui/qt/onroad/alerts.cc`

의미:

- 자동 밝기와 일부 onroad UI 조정의 이전 앵커
- 이번에 다시 손본 자동밝기 논의는 이 커밋 이후의 추가 보정으로 이해하면 된다

#### `ce9a7daf` `Fix driving model downloads and retune brightness`

- `frogpilot/frogpilot_process.py`
- `selfdrive/ui/ui.cc`

의미:

- brightness 조정과 runtime tuning의 이전 앵커
- 이번 대화에서 brightness를 다시 손본 배경 참고점

#### `41041d9d` `Stabilize runtime and refresh device UI build`

- `frogpilot/frogpilot_process.py`
- `selfdrive/selfdrived/events.py`
- `selfdrive/ui/qt/onroad/alerts.cc`
- `launch_env.sh`
- `system/manager/manager.py`

의미:

- runtime 안정화/이벤트/런타임 build 관련 과거 앵커
- 이번에 `commIssue` / `alertDebug` / runtime burden를 다시 다룰 때 참고해야 하는 커밋

#### `49f0793e` `Refresh project handoff history`

- `HISTORY.md` 대규모 갱신

의미:

- 이 문서 체계를 이전에 한 번 크게 정리한 커밋
- 이번 작업도 같은 방식으로 HISTORY를 확장하고 있다

### 현재 워킹트리(미커밋)에서 바뀐 핵심 파일

다음 파일들은 이 문서 섹션에서 설명한 최근 수정이 들어가 있지만, 아직 깃 커밋 앵커가 없다.

- `tools/apn_bridge/apn_bridge.py`
  - safety-driving mode 카메라/구간단속 처리
  - `APNDataTimestamp`
  - `nSdiType` 카테고리 정리
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.cc`
- `frogpilot/ui/qt/onroad/frogpilot_annotated_camera.h`
  - 보호구역 / 일반 SDI / 특수 단속 / 구간단속 디자인
  - warning live hide
- `opendbc_repo/opendbc/car/gm/carcontroller.py`
  - fake-long / APN-fake-long / synthetic button test 경로
  - camera-only rollback
  - legacy test payload support
- `opendbc_repo/opendbc/car/gm/carstate.py`
  - cluster speed / current speed 보정
- `opendbc_repo/opendbc/car/gm/cluster_speed.py`
  - 새 lookup table helper
- `frogpilot/frogpilot_process.py`
  - live `FrogPilotCarParams` safetyParam 동기화
- `tools/device_dashboard_mock/server.py`
- `tools/device_dashboard_mock/app.js`
- `tools/device_dashboard_mock/index.html`
- `tools/device_dashboard_mock/styles.css`
  - 상태탭 경량화
  - 디버그탭
  - stats 재구성
  - CAN monitoring lazy-load
- `selfdrive/selfdrived/selfdrived.py`
- `selfdrive/selfdrived/events.py`
  - ignore 대상 commIssue 표현 정리
- `selfdrive/ui/ui.cc`
- `selfdrive/ui/ui_state.py`
  - auto brightness 조정

### 다음 작업자가 커밋을 만들 때 추천 방식

- 커밋을 한 번에 너무 크게 만들지 말 것
- 최소한 아래 단위로 쪼개는 것을 추천:

1. `APN bridge / SDI classification`
2. `GM cluster speed / fake-long runtime`
3. `웹 디버그탭 / device dashboard 경량화`
4. `onroad SDI 디자인`

이렇게 나누면 나중에 regression이 생겨도 어느 덩어리에서 문제가 생겼는지 되짚기 쉽다.

이 섹션은 2026-03-24 이후 현재까지의 APN / SDI / fake-long / 웹 디버그 작업을 이어받기 위한 실제 인수인계 기록이다.
다음 작업자는 반드시 이 섹션을 먼저 읽고, 특히 `H`, `I`, `N`, `O`, `P` 를 기준으로 다음 액션을 잡는 것을 권장한다.
