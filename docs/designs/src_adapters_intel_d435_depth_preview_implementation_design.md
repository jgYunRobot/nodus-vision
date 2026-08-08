# Intel D435 Depth Preview Implementation Design

## 1. 목적

Intel D435 adapter가 이미 보유한 Z16 Depth frame을 사람이 확인할 수 있는 RGB 컬러맵으로
변환하여 기존 Vision Depth preview data plane에 공급한다.

이 작업은 새로운 공개 API를 추가하는 작업이 아니다. 이미 공개된 다음 계약을 실제 D435에서도
충족하는 구현 보완이다.

- `GET /snapshot/depth`: 최신 colorized Depth JPEG
- `GET /stream/depth.mjpg`: 최신 colorized Depth MJPEG
- Pilot descriptor `depth-preview`: `camera.stream.depth.preview`
- Pilot descriptor `depth-snapshot`: `camera.snapshot.depth.preview`

Depth preview는 시각화 전용이다. 거리 계산, pixel deprojection, ROI, point cloud와 Policy 입력은
계속 원본 Z16 Depth frame을 사용한다.

## 2. 현재 상태와 확인된 원인

### 2.1 정상인 경로

- D435는 Depth Z16 640x480@30과 Color RGB8 640x480@30을 함께 캡처한다.
- `IntelD435CapturedFrame`은 원본 `rs2::depth_frame`을 소유한다.
- pixel query는 이 원본 Depth frame에서 meter 거리와 optical point를 계산한다.
- Vision encoder, latest-only preview cache, snapshot route와 MJPEG server는 Fake Camera의 Color와
  Depth preview에서 이미 같은 camera-neutral 경계를 사용한다.

### 2.2 누락된 경로

`IntelD435CapturedFrame::getDepthPreviewFrameView()`가 항상 `std::nullopt`를 반환한다. 따라서
encoder thread는 D435 Depth JPEG를 한 번도 생성하지 않고 `/snapshot/depth`는 fresh preview를
얻지 못한다. `/stream/depth.mjpg`도 초기 frame이 없으므로 정상 MJPEG를 시작할 수 없다.

Pilot catalog와 `/metadata`는 D435가 원본 Depth를 항상 제공한다는 전제에서 Depth preview
endpoint를 이미 광고한다. 그 결과 Portal은 유효한 descriptor를 발견하지만 이미지 대신
`no_fresh_frame` 오류 응답을 받는다.

### 2.3 2026-08-09 관찰 근거

- 실물 D435 Color MJPEG는 3초 동안 70개 이상의 multipart frame과 증가하는 frame number를
  전송했다.
- localhost와 trusted-LAN Portal을 연 Chromium에서 Color 이미지 픽셀은 시간에 따라 변경됐다.
- 같은 D435 카드의 Depth `<img>`는 크기 0x0이며 `/stream/depth.mjpg`가 이미지 payload를
  제공하지 못했다.
- 직접 원인은 `src/adapters/intel_d435/intel_d435_adapter.cpp`의 Depth preview `nullopt` 반환이다.

## 3. PA-CONTROL 기준 동작

이관 기준은 `migration/source_manifest.json`에 고정된 PA-CONTROL revision
`1c44efbe0b03fa77187305d0f50948f731e972f0`의 다음 소스다.

- `modules/vision/intel_d435/src/intel_d435_camera.cpp`
- `modules/vision/intel_d435/include/vision/intel_d435/intel_d435_camera.hpp`

PA-CONTROL은 adapter가 소유한 `rs2::colorizer`로 각 원본 Depth frame을 colorize하고,
결과 `rs2::frame`을 원본 Depth와 함께 보관한다. 소비자는 SDK frame owner를 유지하는 RGB
view를 받아 JPEG로 인코딩한다.

Nodus Vision은 이 동작을 재구현하되 PA-CONTROL의 public vendor type이나 latest-frame 전역
저장 방식을 복사하지 않는다. 변환 결과는 하나의 immutable `CapturedFrame`에 원본 Depth와
함께 귀속한다.

## 4. 범위

### 4.1 포함

- D435 Z16 frame의 RealSense colorized RGB preview 생성
- preview SDK frame의 immutable lifetime 보장
- camera-neutral `VideoFrameView` 반환
- 기존 JPEG encoder, preview cache, snapshot/MJPEG 경로 재사용
- colorizer 실패를 원본 Depth capture 실패와 분리
- hardware-independent regression과 실물 D435 acceptance 절차 정의
- 완료 후 `docs/progress.md`에 검증 근거 기록

### 4.2 제외

- Portal 코드 변경
- Pilot OpenAPI 또는 Vision public schema/version 변경
- raw Z16 HTTP 전송
- Depth palette, histogram equalization, min/max 시각 범위의 신규 설정
- ROI/PCD 미구현 범위 보완
- timestamp와 `latest_frame_age_ms` 보완
- WebRTC, HLS 또는 MJPEG 대체 transport
- 카메라 재연결 lifecycle 변경

## 5. 소유권과 data flow

```text
rs2::pipeline::wait_for_frames
                |
                v
       rs2::depth_frame Z16 --------------------+
                |                               |
                |                               +--> pixel / ROI / PCD
                v                                    원본 metric Depth
       rs2::colorizer::colorize
                |
                v
       rs2::video_frame RGB8
                |
                v
   IntelD435CapturedFrame immutable owner
                |
                v
 getDepthPreviewFrameView() -> VideoFrameView
                |
                v
 encoder thread -> JPEG -> latest-only cache
                |
          +-----+-----+
          |           |
 /snapshot/depth  /stream/depth.mjpg
```

### 5.1 책임 경계

- `IntelD435Adapter::Impl`은 vendor processing block인 `rs2::colorizer`를 소유한다.
- `IntelD435Adapter::readFrame()`은 같은 capture mutex 안에서 colorization을 수행한다.
- `IntelD435CapturedFrame`은 원본 Depth, 선택적 Color, 선택적 colorized Depth preview frame을
  함께 소유한다.
- `VisionApplication`은 vendor type을 알지 않고 기존 `VideoFrameView`만 인코딩한다.
- `ProviderHttpServer`와 Pilot catalog는 변경하지 않는다.
- Portal은 Vision endpoint를 직접 소비하며 Pilot은 payload를 relay하지 않는다.

## 6. Adapter 상세 설계

### 6.1 Colorizer 수명

`IntelD435Adapter::Impl`에 하나의 `rs2::colorizer m_depth_colorizer`를 둔다. 매 frame마다
colorizer 객체를 생성하지 않는다. `readFrame()`만 colorizer를 호출하고 기존 mutex가 호출을
직렬화하므로 추가 lock이나 worker thread를 만들지 않는다.

초기 구현은 PA-CONTROL과 동일한 librealsense 기본 color map을 사용한다. 기존
`depth_min_m`과 `depth_max_m`은 metric query 유효성 경계이며 preview palette 계약으로
재해석하지 않는다. palette/range 설정이 필요하면 별도 versioned config 설계로 추가한다.

### 6.2 Frame 생성

`readFrame()`은 다음 순서를 따른다.

1. configured timeout으로 frameset을 읽는다.
2. 원본 `rs2::depth_frame`과 선택적 `rs2::video_frame` Color를 획득한다.
3. 원본 Depth가 유효할 때 `m_depth_colorizer.colorize(depth_frame)`를 호출한다.
4. 결과를 `rs2::video_frame`으로 변환하고 width, height, stride와 format을 확인한다.
5. 원본 Depth snapshot identity를 만든다.
6. 세 SDK frame을 `IntelD435CapturedFrame` constructor로 이동한다.

colorized frame의 자체 SDK timestamp나 frame number를 새 identity로 사용하지 않는다. 모든
preview, query와 header는 같은 원본 Depth capture를 나타내도록 `FrameSnapshot::identity`를
사용한다.

### 6.3 Format 검증

기존 `encodeRgbJpeg()`는 `PixelFormat::e_RGB8`만 허용한다. 따라서 Depth preview view는 다음
조건을 모두 충족할 때만 반환한다.

- 유효한 `rs2::video_frame`
- width와 height가 양수
- format이 `RS2_FORMAT_RGB8`
- stride가 `width * 3` 이상
- data pointer가 null이 아님

예상 밖 format을 BGR로 암묵 변환하거나 JPEG encoder를 확장하지 않는다. 이는 잘못된 색상과
계약 drift를 숨길 수 있으므로 preview를 unavailable로 두고 진단을 남긴다.

### 6.4 Immutable lifetime

`IntelD435CapturedFrame`에 `rs2::video_frame m_depth_preview_frame`을 추가한다.
`getDepthPreviewFrameView()`는 Color view와 동일한 aliasing owner 방식을 사용한다.

- `shared_from_this()`로 captured-frame owner를 얻는다.
- `VideoFrameView::p_owner`는 captured-frame 수명을 연장한다.
- `p_data`는 `m_depth_preview_frame.get_data()`를 가리킨다.
- 다음 capture나 adapter stop 이후에도 기존 view가 살아 있는 동안 SDK buffer는 유효하다.
- view identity는 `m_snapshot.identity`다.

전역 latest Depth preview frame이나 adapter 내부 raw pointer를 추가하지 않는다.

### 6.5 실패 격리

Depth colorization은 presentation 기능이므로 실패가 원본 Depth capture를 폐기해서는 안 된다.

- frameset/원본 Depth 획득 실패: 기존 capture failure로 처리한다.
- colorizer exception, invalid output 또는 예상 밖 format: 해당 captured frame의 Depth preview만
  비운다.
- Color frame, pixel query와 원본 Depth owner는 그대로 publish한다.
- colorizer 실패는 timeout counter를 증가시키지 않는다.
- adapter의 actionable diagnostic에 원인을 기록하되 frame마다 INFO/WARNING을 출력하지 않는다.
- 이후 frame에서 colorization이 성공하면 preview publication은 자동 복구된다.

현재 public health schema에는 preview-specific counter가 없다. 이 작업에서 임의 field를
추가하지 않으며 별도 진단 counter가 필요하면 public contract 변경 설계로 분리한다.

## 7. Runtime 및 endpoint 동작

기존 encoder thread 흐름을 그대로 사용한다.

1. `FrameStore`에서 최신 immutable frame을 얻는다.
2. `getDepthPreviewFrameView()`가 값을 반환하면 기존 `encodeRgbJpeg()`를 호출한다.
3. `PreviewKind::e_DEPTH` single-slot cache에 newer identity만 publish한다.
4. 성공 시 `ProviderStreamKind::e_DEPTH` session에 notification을 보낸다.
5. snapshot과 각 MJPEG part는 기존 identity header를 유지한다.

Depth preview가 아직 없거나 일시 실패한 동안:

- `/snapshot/depth`는 기존 `503 no_fresh_frame` 의미를 유지한다.
- 새 `/stream/depth.mjpg` 연결도 초기 fresh frame이 없으면 기존 503을 반환한다.
- 이미 활성인 stream은 새 preview notification을 기다리며 queue/replay를 만들지 않는다.
- raw capture와 Color stream은 계속 동작한다.

### 7.1 Catalog 의미

D435는 구성상 Depth stream이 필수이고 이 설계 완료 후 adapter가 Depth preview capability를
제공하므로 기존 정적 descriptor를 유지한다. endpoint 광고는 capability를 뜻하며 매 순간 fresh
frame이 존재한다는 뜻은 아니다.

Fake Camera와 D435가 동일한 Depth preview 계약을 만족하므로 adapter-specific descriptor나
Portal 분기를 추가하지 않는다.

## 8. 파일별 변경 계획

| 파일 | 변경 |
|---|---|
| `src/adapters/intel_d435/intel_d435_adapter.cpp` | colorizer 소유, preview 생성, captured-frame owner와 view 구현 |
| `tests/adapters/test_intel_d435.cpp` | device를 열지 않는 config/lifecycle 회귀 유지, 가능한 vendor-independent validation 보강 |
| `tests/provider/test_jpeg_encoder.cpp` | 기존 RGB8 encoder 계약 회귀 확인; vendor SDK fixture는 추가하지 않음 |
| `tests/integration/test_application_lifecycle.cpp` | Fake Camera의 기존 Depth data-plane 회귀 유지 |
| `docs/progress.md` | 구현 결과, 실행한 검증과 남은 D435 범위 기록 |

공개 header, config schema, endpoint catalog, Pilot contract와 Portal 파일은 변경 대상이 아니다.
구현 중 실제로 해당 변경이 필요하다고 확인되면 이 문서를 먼저 개정하고 별도 checkpoint로
분리한다.

## 9. 단계별 구현 계획

### DP0 - Baseline과 계약 고정

- 현재 D435 Color stream과 Depth `no_fresh_frame`을 증거로 남긴다.
- 기존 public endpoint/schema/version이 변경되지 않음을 확인한다.
- PA-CONTROL source revision과 colorizer behavior를 다시 확인한다.

완료 조건:

- 변경 파일과 제외 범위가 확정된다.
- 실물 실행 없이 실패 원인이 `getDepthPreviewFrameView()` 누락으로 재현된다.

### DP1 - Immutable colorized Depth frame

- adapter `Impl`에 재사용 colorizer를 추가한다.
- `readFrame()`에서 선택적 Depth preview frame을 생성한다.
- `IntelD435CapturedFrame`이 preview frame을 소유하게 한다.
- `getDepthPreviewFrameView()`가 검증된 RGB8 view와 원본 identity를 반환하게 한다.
- colorizer 실패를 원본 capture 실패와 분리한다.

완료 조건:

- public type에 `rs2`가 노출되지 않는다.
- per-frame colorizer 생성과 전역 latest buffer가 없다.
- preview view owner가 SDK memory보다 오래 유지된다.

권장 commit:

`fix(adapter): provide D435 depth preview frames`

### DP2 - Hardware-independent regression

- D435 config validation과 construction test가 device를 자동으로 열지 않는지 유지한다.
- Fake Depth preview의 JPEG, snapshot와 MJPEG 회귀를 확인한다.
- Depth preview가 Color enable 설정과 독립적임을 catalog test에서 유지한다.
- format, staged diff와 C++17 build 경계를 확인한다.

완료 조건:

- hardware-independent 전체 test가 외부 Camera 없이 결정적으로 통과한다.
- Color, recording, Pilot lifecycle와 mount geometry 계약에 회귀가 없다.

권장 commit:

`test(adapter): cover depth preview integration`

### DP3 - Physical D435 acceptance

사용자가 hardware 실행을 승인한 환경에서만 수행한다.

기준 장치와 실행 경계:

- model: Intel RealSense D435 (`8086:0b07`)
- librealsense serial: `241222076339`
- profile: Depth Z16 640x480@30, Color RGB8 640x480@30
- local Pilot: `127.0.0.1:8765`
- trusted-LAN Provider: `192.168.219.106:8902`
- command: `./run_app.sh --config assets/configs/examples/intel_d435_pilot.json`

검증 항목:

1. `/health`가 Camera `streaming`을 유지하고 capture frame number가 증가한다.
2. `/snapshot/depth`가 `200 image/jpeg`와 640x480 이미지를 반환한다.
3. 연속 Depth snapshot의 `X-Nodus-Frame-Number`가 증가한다.
4. `/stream/depth.mjpg` 한 연결에서 여러 boundary와 증가하는 frame number를 받는다.
5. Portal D435 카드의 Depth 이미지가 640x480으로 로드되고 시간에 따라 픽셀이 변한다.
6. 동시에 Color MJPEG와 raw pixel query가 계속 동작한다.
7. slow Depth client가 capture/Color를 막지 않고 latest-only bound를 유지한다.
8. stop 후 session과 SDK frame owner가 bounded shutdown된다.

완료 조건:

- `docs/progress.md`에 exact command, 장치 identity, profile과 endpoint evidence를 기록한다.
- 위 검증을 수행하지 않았다면 "D435 Depth hardware accepted"라고 표현하지 않는다.

권장 commit:

`test(adapter): accept physical D435 depth preview`

## 10. 검증 명령 계획

저장소 규칙에 따라 구현 요청에서 build/test가 명시됐을 때만 실행한다.

```bash
/path/to/clang-format-18.1.8 --dry-run --Werror \
  src/adapters/intel_d435/intel_d435_adapter.cpp \
  tests/adapters/test_intel_d435.cpp

./make_full.sh --build-type Debug --build-only
ctest --test-dir build/debug --output-on-failure
```

실물 acceptance에서는 stream body를 파일에 저장해 boundary 수와 frame identity 증가를 확인하되
저장 결과를 repository에 commit하지 않는다.

## 11. 위험과 대응

| 위험 | 대응 |
|---|---|
| colorizer가 capture thread 시간을 늘림 | colorizer 객체 재사용, 640x480@30 acceptance에서 timeout/drop 확인 |
| SDK output lifetime 종료 | captured-frame이 `rs2::video_frame`을 직접 소유하고 aliasing owner 제공 |
| 예상 밖 BGR/format으로 색상 오류 | RGB8 strict check, 암묵 변환 금지 |
| preview 실패가 Camera 전체를 degraded로 만듦 | preview만 비우고 원본 Depth/Color frame publication 유지 |
| stale preview가 새 generation에 남음 | 기존 preview cache generation invalidation 재사용 |
| Depth MJPEG가 느린 client에 막힘 | 기존 session별 pending part 1개와 latest-only notification 재사용 |
| preview를 metric Depth로 오해 | 문서와 schema 의미를 visualization-only로 유지 |
| 범위가 ROI/PCD/Portal 수정으로 확장됨 | 제외 범위 고정, 후속 작업은 별도 설계와 checkpoint 사용 |

## 12. 완료 정의

- D435 `getDepthPreviewFrameView()`가 원본 Depth와 동일 identity의 유효한 RGB8 view를 제공한다.
- preview view는 다음 capture와 adapter stop 이후에도 owner가 살아 있는 동안 안전하다.
- `/snapshot/depth`와 `/stream/depth.mjpg`가 기존 공개 계약을 변경하지 않고 동작한다.
- colorizer 실패가 raw Depth, Color, query와 capture lifecycle을 중단하지 않는다.
- Fake Camera, Color stream, Pilot catalog, recording과 mount geometry에 회귀가 없다.
- hardware-independent 검증과 실물 검증 결과를 구분해 보고한다.
- Portal, Pilot, public schema/version에는 변경이 없다.
