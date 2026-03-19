# frogpilot-testing-v1 작업 이력 / 인수인계 문서

최종 갱신: 2026-03-19

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
