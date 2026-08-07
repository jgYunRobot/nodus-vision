# Nodus Vision Phase 0-3 상세 구현 설계

## 1. 문서 상태와 관계

- 대상 저장소: `nodus-vision`
- 대상 책임 경로: `include/nodus_vision`, `src`, `app`, `tests`
- 구현 범위: Phase 0부터 Phase 3까지
- 상위 설계: `docs/designs/src_nodus_vision_camera_provider_migration_design.md`
- PA-CONTROL 기준 revision: `1c44efbe0b03fa77187305d0f50948f731e972f0`
- 상태: 구현 착수용 상세 설계

이 문서는 상위 설계의 V0-V3을 각각 Phase 0-3으로 구체화한다. 상위 설계와 충돌하면 상위 설계의
ownership와 외부 repository 경계가 우선한다. 구현 과정에서 public contract, timestamp 의미,
camera lifecycle 또는 dependency 선택을 바꾸려면 코드보다 이 문서를 먼저 갱신한다.

## 2. 목표와 완료 지점

Phase 3 완료 시 다음 상태를 만든다.

1. C++17 `nodus-vision` executable이 strict JSON config로 시작된다.
2. camera-neutral public contract와 latest-only frame store가 존재한다.
3. deterministic fake adapter와 Intel D435 adapter가 같은 최소 adapter 경계를 사용한다.
4. camera가 없거나 disabled여도 `/health`와 `/metadata`를 제공하는 degraded process가 실행된다.
5. frame이 공급되면 color/depth preview, color snapshot, ROI/pixel query와 binary point cloud를
   Vision-owned HTTP endpoint로 제공한다.
6. capture, encoding, HTTP client 속도 사이에 unbounded queue가 없고 느린 client가 capture를 막지
   않는다.
7. public header와 provider schema에 `rs2` type이 노출되지 않는다.

Phase 3에서는 Pilot registration/catalog, recording, Portal, Operator, MetaGate, Control integration을
구현하지 않는다.

## 3. 고정 결정

### 3.1 ownership

- process당 physical camera 하나만 소유한다.
- capture와 camera SDK lifetime은 Vision adapter가 소유한다.
- provider HTTP payload는 Vision이 직접 제공한다.
- Pilot은 Phase 4 전까지 dependency가 아니며 payload relay로 사용하지 않는다.
- UI와 robot/world transform은 Vision에 추가하지 않는다.
- `nodus-control`, Control UDS와 PA-CONTROL checkout은 build/runtime dependency가 아니다.

### 3.2 data와 좌표계

- latest frame slot은 FIFO가 아니라 latest-wins single slot이다.
- authoritative spatial frame은 configured camera optical frame이다.
- Phase 3 point cloud 좌표는 raw optical coordinate다.
- PA-CONTROL의 mutable `/reference_frame` endpoint와 runtime robot pose push는 이관하지 않는다.
- Portal의 `optical(x,y,z) -> mount(x,z,-y)` rendering 변환은 Vision에 중복 구현하지 않는다.

### 3.3 실행과 검증

- physical D435 실행과 `/dev/bus/usb` 접근은 Phase 0-3 구현 acceptance에 포함하지 않는다.
- fake adapter로 process와 data plane을 결정론적으로 검증한다.
- D435 target은 compile/link 및 hardware-independent validation까지만 수행한다.
- 실제 camera evidence는 상위 설계 Phase 8에서 별도 승인 후 기록한다.

## 4. Phase 의존 관계와 stop gate

```text
Phase 0 foundation/provenance
  -> Phase 1 camera contracts + latest frame + fake/D435 adapters
      -> Phase 2 strict config + process lifecycle + health/metadata server
          -> Phase 3 encoded preview + query + point-cloud data plane
              -> STOP: Phase 4 Pilot integration is a separate task
```

각 Phase는 아래 조건을 모두 만족해야 다음 Phase로 진행한다.

- phase-owned source와 test만 추가한다.
- public contract와 schema가 현재 design과 일치한다.
- 지정된 build/test가 통과한다.
- `docs/progress.md`에 결과와 남은 위험을 기록한다.
- staged diff를 검토하고 독립 Conventional Commit을 만든다.

Phase gate가 실패하면 뒤 Phase 구현을 시작하지 않는다. 실패한 검증을 삭제하거나 test를 완화해
통과시키지 않는다.

## 5. Phase 3 완료 시 repository 구조

```text
nodus-vision/
  app/
    CMakeLists.txt
    main.cpp
  assets/configs/examples/
    fake_camera.json
    intel_d435.json
  include/nodus_vision/
    camera_adapter.hpp
    camera_contracts.hpp
    provider_health.hpp
  schemas/vision/v1/
    config.schema.json
    openapi.yaml
    pointcloud_pcd1_v2.md
  src/
    CMakeLists.txt
    adapters/
      fake/
        CMakeLists.txt
        fake_camera_adapter.cpp
        fake_camera_adapter.hpp
      intel_d435/
        CMakeLists.txt
        intel_d435_adapter.cpp
        intel_d435_adapter.hpp
    config/
      CMakeLists.txt
      vision_config.cpp
      vision_config.hpp
    core/
      CMakeLists.txt
      frame_store.cpp
    provider/http/
      CMakeLists.txt
      provider_http_server.cpp
      provider_http_server.hpp
      provider_routes.cpp
      provider_routes.hpp
    runtime/
      CMakeLists.txt
      vision_application.cpp
      vision_application.hpp
  tests/
    adapters/
    contracts/
    core/
    integration/
    provider/
```

폴더와 파일은 해당 Phase에서 실제 책임이 생길 때 추가한다. 단순 forwarding용 빈 class나 미래 vendor를
위한 추가 hierarchy는 만들지 않는다.

## 6. build target와 dependency 경계

### 6.1 target graph

```text
nodus_vision_core                 no vendor SDK
nodus_vision_fake_adapter         -> nodus_vision_core
nodus_vision_intel_d435           -> nodus_vision_core + realsense2
nodus_vision_config               -> nodus_vision_core + typed JSON dependency
nodus_vision_provider_http        -> nodus_vision_core + Boost.Asio/Beast + typed JSON dependency
nodus_vision_runtime              -> core + selected adapter + config + provider_http
nodus-vision executable           -> nodus_vision_runtime
```

각 library는 static library로 독립 build할 수 있어야 한다. compile option은 compiler가 지원하는 경우
`-Wall -Wextra`를 적용한다. public include directory에는 camera-neutral header만 설치한다. RealSense와
HTTP implementation header는 `src` 내부에 둔다.

### 6.2 dependency policy

- librealsense는 `migration/source_manifest.json`에 기록한 revision
  `05e3d1e57f3c87e6c9768eaca9e89639966beee2`를 기준으로 pin한다.
- JSON parser는 strict typed conversion을 제공하는 maintained C++17 library를 exact revision으로
  pin한다. handwritten substring parser를 이관하지 않는다.
- Phase 2 HTTP server는 Boost.Asio/Beast 기반으로 구현한다. one-thread-per-client 방식은 사용하지
  않는다.
- JPEG encoder는 librealsense 내부 `third-party` include를 사용하지 않는다. 직접 pin한
  `libjpeg-turbo` 또는 동등한 owned dependency를 사용하고 revision/license를 ledger에 기록한다.
- dependency를 추가할 때 source URL, revision/version, license, consumed target을
  `docs/migration_ledger.md`에 함께 기록한다.

새 dependency를 exact하게 고정할 수 없거나 license를 확인할 수 없으면 관련 구현을 진행하지 않고 해당
Phase를 blocked로 보고한다.

## 7. 공통 camera contract

### 7.1 value types

`include/nodus_vision/camera_contracts.hpp`는 최소 다음 value type을 제공한다.

| Type | 필수 필드와 의미 |
|---|---|
| `StreamProfile` | `width`, `height`, `fps`, camera-neutral pixel `format` |
| `CameraIntrinsics` | `width`, `height`, `fx`, `fy`, `ppx`, `ppy`, distortion model/coefficients |
| `FrameIdentity` | `capture_generation`, `frame_number`, monotonic/unix/device timestamp와 domain |
| `FrameSnapshot` | identity, profiles, intrinsics, depth scale, color/depth availability |
| `VideoFrameView` | dimensions, stride, format, immutable owner, const byte pointer, identity |
| `PixelPointResult` | identity, requested pixel, validity, metric depth와 optical point |
| `RoiDepthResult` | identity, requested/clamped ROI, sample counts, depth statistics, median point |
| `PointCloudPoint` | optical-frame XYZ meters와 RGB bytes |
| `PointCloudSnapshot` | identity, source profile/intrinsics, stride, bounded point vector |
| `CameraHealthSnapshot` | lifecycle, device identity, latest identity/age, counters, last diagnostic |

`FrameIdentity` 규칙은 다음과 같다.

- `capture_generation`은 process가 adapter stream을 새로 시작할 때 증가한다.
- `frame_number`는 generation 내부 ordering 값이며 restart 전후 globally unique하다고 주장하지 않는다.
- `capture_timestamp_ns`는 process-local monotonic freshness와 ordering에 사용한다.
- `capture_unix_epoch_ns`는 offline/same-host correlation용이며 synchronized clock을 주장하지 않는다.
- `device_timestamp`와 `device_timestamp_domain`은 vendor observation 그대로 보존한다.

### 7.2 immutable captured frame와 adapter interface

mutable adapter에서 metadata, image와 query를 각각 읽으면 그 사이에 latest frame이 바뀌어 response
identity가 섞일 수 있다. 이를 막기 위해 capture 결과 자체를 immutable per-frame object로 만든다.
`CapturedFrame` 구현은 SDK frame owner를 보유하고 모든 view/query를 같은 frame에 대해 수행한다.

`include/nodus_vision/camera_adapter.hpp`에는 fake와 D435가 공유하는 의도적인 두 경계만 둔다.

```cpp
class CapturedFrame {
public:
    virtual ~CapturedFrame() = default;
    virtual const FrameSnapshot& getSnapshot() const noexcept = 0;
    virtual std::optional<VideoFrameView> getColorFrameView() const = 0;
    virtual std::optional<VideoFrameView> getDepthPreviewFrameView() const = 0;
    virtual std::optional<PixelPointResult> queryPixelPoint(int pixel_x, int pixel_y) const = 0;
    virtual RoiDepthResult
    queryDepthInRoi(int pixel_x, int pixel_y, int width, int height) const = 0;
    virtual PointCloudSnapshot
    buildPointCloudSnapshot(std::size_t max_points, int stride_pixels) const = 0;
};

class CameraAdapter {
public:
    virtual ~CameraAdapter() = default;
    virtual void connectCamera() = 0;
    virtual void disconnectCamera() noexcept = 0;
    virtual void startStream() = 0;
    virtual void stopStream() noexcept = 0;
    virtual std::shared_ptr<const CapturedFrame>
    readFrame(std::chrono::milliseconds timeout) = 0;
    virtual CameraHealthSnapshot getHealthSnapshot() const = 0;
};
```

실제 signature는 coding rules에 맞춰 line wrap/Doxygen/include guard를 적용한다. Config는 adapter 생성
시 immutable하게 전달하고 interface에 vendor-specific option을 추가하지 않는다. Consumer가 실제 두
번째 vendor 요구를 제시하기 전에는 capability registry나 plugin ABI를 만들지 않는다. 이 virtual
dispatch는 non-RT camera vendor/per-frame lifetime 경계를 위한 의도적인 선택이며 pixel loop 안에서는
사용하지 않는다.

### 7.3 frame lifetime와 latest store

`FrameStore`는 immutable `std::shared_ptr<const CapturedFrame>` 하나를 보유한다.

- publish는 완성된 snapshot을 한 번에 교체한다.
- read는 현재 snapshot owner를 복사해 lock 밖에서 사용한다.
- reader별 cursor나 FIFO를 만들지 않는다.
- 동일 `(capture_generation, frame_number)` 재게시를 거부하거나 no-op 처리한다.
- 낮은 generation 또는 과거 frame number가 최신 값을 되돌리지 못하게 한다.
- captured frame이 adapter SDK frame owner를 보유하고, 필요한 경우 `VideoFrameView`도 같은 owner를
  전달해 encoder가 끝날 때까지 memory를 유지한다.
- capture lock을 잡은 상태에서 JPEG encoding, JSON serialization 또는 socket write를 하지 않는다.

## 8. Phase 0 - foundation와 provenance 확인

### 8.1 범위

Phase 0은 이미 만들어진 repository foundation을 구현 시작 전에 재검증한다.

- root `AGENTS.md`, `docs/rules.md`
- pinned `docs/agent_docs`
- CMake 3.28/C++17 presets
- `.clang-format`, `.gitignore`, `setup_dev.sh`
- migration manifest/ledger와 상위 설계

### 8.2 작업

1. 기존 사용자 변경과 staged/untracked 상태를 기록한다.
2. PA-CONTROL source revision과 relevant paths가 manifest와 일치하는지 확인한다.
3. submodule pin을 바꾸지 않고 초기화 가능 여부를 확인한다.
4. Phase 1-3 구현에 필요한 dependency를 추가하기 전 provenance entry 형식을 확정한다.
5. empty-target CMake baseline이 parse되는지 확인한다.

### 8.3 완료 조건

- source revision/path/ownership가 추측 없이 재현 가능하다.
- PA-CONTROL file을 직접 include/import하는 CMake 또는 script가 없다.
- 기존 foundation 변경을 보존하고 Phase 0 변경만 별도 staged diff로 확인한다.

### 8.4 checkpoint

- 권장 commit: `chore: establish vision migration foundation`
- 기존 foundation이 이미 같은 내용으로 commit돼 있으면 중복 commit을 만들지 않고 evidence만
  `docs/progress.md`에 기록한다.

## 9. Phase 1 - camera core와 adapter

### 9.1 P1.1 camera-neutral contracts

1. `camera_contracts.hpp`와 `camera_adapter.hpp`를 추가한다.
2. 모든 public type에서 `rs2`, RealSense enum, raw SDK handle을 제거한다.
3. pixel format과 timestamp domain은 string 남발 대신 bounded enum과 explicit serializer mapping을
   사용한다.
4. public C++ header에 Korean Doxygen file/API comment와 include guard를 적용한다.

검증:

- public headers만 include하는 consumer compile test
- type default/invalid state test
- SDK identifier가 installed public headers에 없는 정적 검사

### 9.2 P1.2 latest frame store

1. completed immutable snapshot publication과 snapshot acquisition을 구현한다.
2. generation/frame ordering과 duplicate rule을 구현한다.
3. concurrent writer 한 개와 reader 여러 개를 가정하되 reader가 writer를 장시간 block하지 않게 한다.
4. shutdown 시 reader가 dangling SDK pointer를 보지 않도록 owner lifetime test를 추가한다.

검증:

- empty store
- first publication
- newer replacement
- duplicate/no-regression
- owner lifetime
- deterministic bounded concurrency test

### 9.3 P1.3 fake adapter

fake adapter는 unit/integration용 production test seam이다.

- fixed seed/pattern으로 RGB8와 depth data를 만든다.
- config로 width/height/fps, start frame number, timeout/error injection을 받는다.
- wall-clock sleep에 의존하지 않고 test가 caller-driven frame advance를 사용할 수 있게 한다.
- center/pixel/ROI/deprojection 결과가 hand-calculated fixture와 일치해야 한다.
- point cloud는 configured maximum과 stride를 지킨다.
- disconnect/reconnect 때 capture generation이 바뀐다.

fake adapter를 runtime에서 선택할 수는 있지만 기본 production config로 사용하지 않는다.

### 9.4 P1.4 Intel D435 adapter

PA-CONTROL의 `modules/vision/intel_d435`에서 다음 behavior를 재구현한다.

- serial/device name selection과 ambiguous-device fail-closed
- depth/color profile, align target, depth range validation
- pipeline/config/frame RAII
- blocking frame read와 immutable per-frame SDK ownership
- frame/profile/intrinsics/depth scale snapshot
- pixel depth/deprojection, ROI statistics, bounded point cloud
- health, timeout counter와 actionable last diagnostic

다음은 복사하지 않는다.

- `IntelD435*` 이름이 붙은 public provider contract
- PA-CONTROL include path와 CMake hierarchy
- executable test가 자동으로 hardware를 여는 동작
- `rs2` type이 public header로 새는 API

D435 내부에서는 SDK exception을 adapter diagnostic과 명확한 runtime error로 변환한다. capture
hot path에서 per-frame INFO log를 남기지 않는다.

### 9.5 Phase 1 test matrix

| Test | hardware | 기대 결과 |
|---|---:|---|
| public contract consumer | 없음 | vendor SDK include 없이 compile |
| frame store ordering/lifetime | 없음 | latest-only와 owner lifetime 보장 |
| fake RGB/depth frame | 없음 | deterministic identity와 buffer |
| fake pixel/ROI/deprojection | 없음 | fixture 값과 일치 |
| fake bounded point cloud | 없음 | max/stride 준수 |
| D435 config validation | 없음 | invalid/ambiguous config fail closed |
| D435 construction/destruction | 없음 | device open 없이 leak/throw 없음 |

### 9.6 Phase 1 완료 조건과 commits

- public API에 vendor type이 없다.
- fake adapter만으로 core 기능이 모두 검증된다.
- D435 adapter target은 hardware를 자동 탐색하거나 실행하지 않고 build 가능하다.
- PA-CONTROL은 build/runtime dependency가 아니다.

권장 commit 단위:

1. `feat(core): add camera-neutral frame contracts`
2. `feat(core): add latest-only frame store`
3. `test(adapter): add deterministic fake camera`
4. `feat(adapter): migrate Intel D435 camera adapter`

## 10. Phase 2 - strict config와 provider skeleton

### 10.1 P2.1 config schema

`schemas/vision/v1/config.schema.json`을 canonical config contract로 둔다. root와 nested object는
`additionalProperties: false`를 사용한다.

필수 section:

- `schema_version`
- `device_id`, `component_id`
- `device.adapter`와 adapter-specific typed config
- `calibration.calibration_id`, `sensor_frame`, `mount_frame`, static matrix
- `provider.bind_host`, `port`, `advertised_base_url`
- provider bounds와 timeout

Phase 2 config에서는 `pilot`과 `recording` section을 받지 않는다. 해당 Phase가 시작될 때 schema를
확장한다. unknown future field를 묵인하지 않는다.

검증 규칙:

- `device_id`와 `component_id`는 비어 있지 않고 bounded length다.
- port는 1-65535다.
- `max_connections`, `max_stream_clients`, timeout/body/header limit는 양수의 bounded 값이다.
- `advertised_base_url`은 absolute HTTP URL이며 wildcard host를 금지한다.
- `bind_host`는 wildcard를 허용하되 advertise 값과 동일시하지 않는다.
- adapter가 `intel_d435`이고 serial이 비어 있으면 multi-device ambiguity를 허용하지 않는다.
- static matrix는 finite 16-element array이며 calibration identity를 동반한다.

config error는 JSON pointer/path와 이유를 포함하고 process 시작 전에 fail closed한다.

### 10.2 P2.2 application lifecycle

`VisionApplication`이 다음 순서를 소유한다.

```text
parse and validate config
  -> construct adapter and bounded stores
  -> bind HTTP acceptor
  -> connect/start adapter or enter explicit degraded state
  -> start capture task
  -> serve health/metadata
  -> stop request
  -> stop accepting new clients
  -> cancel active HTTP sessions by deadline
  -> stop capture task
  -> stop/disconnect adapter
  -> release stores and exit
```

camera connect 실패는 config error가 아니라 runtime degraded state로 유지할 수 있다. bind/config
실패는 process startup failure다. Phase 2에는 automatic USB reconnect loop를 일반화하지 않는다.

signal은 app entry point의 Boost.Asio `signal_set` 또는 동등한 async-safe mechanism으로 stop request로
변환한다. library code가 signal handler를 설치하지 않는다.

### 10.3 P2.3 bounded HTTP server

Boost.Asio/Beast session model은 다음 bounds를 강제한다.

- fixed `io_context` worker count
- `max_connections` 초과 시 즉시 거부
- request header/body 최대 byte
- read/write/idle deadline
- response body maximum
- active session registry와 bounded shutdown deadline
- request당 detached thread 금지
- socket write는 capture/adapter lock 밖에서 수행

Phase 2에서는 streaming session을 아직 열지 않는다. Phase 3 long-lived stream을 수용할 session
interface와 cancellation seam만 둔다.

### 10.4 P2.4 health contract

`GET /health`는 `application/json`과 다음 semantic을 제공한다.

```json
{
  "schema_version": 1,
  "device_id": "top_d435",
  "instance_id": "process-generated-identity",
  "state": "ready",
  "server": {
    "listening": true,
    "active_connections": 0,
    "max_connections": 32
  },
  "camera": {
    "adapter": "intel_d435",
    "state": "streaming",
    "capture_generation": 1,
    "latest_frame_number": 42,
    "latest_frame_age_ms": 12,
    "timeout_count": 0,
    "drop_count": 0
  },
  "last_error": null
}
```

state는 최소 `starting`, `ready`, `degraded`, `stopping`이다. no camera/deferred connection은 HTTP
200의 `degraded` health로 표현한다. process가 response를 만들 수 없는 transport failure를 JSON state로
위장하지 않는다.

### 10.5 P2.5 metadata contract

`GET /metadata`는 다음을 제공한다.

- schema/provider version
- `device_id`, adapter와 non-secret device identity
- configured and active stream profiles
- intrinsics/depth scale availability
- `calibration_id`, `sensor_frame`, `mount_frame`
- `advertised_base_url`
- 현재 구현되어 실제로 serve 가능한 endpoint 목록
- capture identity/timestamp domain 설명

Phase 2 endpoint 목록은 `/health`, `/metadata`만 포함한다. camera serial은 운영상 필요한 범위에서만
노출하고 config path, filesystem path, credential은 response에 넣지 않는다.

### 10.6 error contract

JSON error는 공통 shape를 사용한다.

```json
{
  "schema_version": 1,
  "error": {
    "code": "no_fresh_frame",
    "message": "No captured frame is available.",
    "retryable": true
  }
}
```

| 상황 | status |
|---|---:|
| malformed/unknown request field | 400 |
| route는 있으나 method가 다름 | 405 |
| disabled capability 또는 conflicting state | 409 |
| body/connection/client bound 초과 | 413 또는 429 |
| camera frame unavailable/stale | 503 |
| unexpected provider failure | 500 |

runtime exception message를 그대로 외부에 노출해 filesystem/device detail이 유출되지 않게 한다.
내부 diagnostic에는 actionable context를 남긴다.

### 10.7 Phase 2 validation과 commits

필수 test:

- valid example config와 각 invalid boundary
- unknown field rejection
- bind/advertise separation
- fake adapter ready health
- disabled/failing adapter degraded health
- metadata endpoint availability filtering
- malformed/oversized request
- connection bound
- SIGINT/SIGTERM-equivalent programmatic stop와 bounded join
- repeated start/stop without leaked task/session

Phase 완료 조건:

- camera와 Pilot 없이 fake/disabled config로 health/metadata를 제공한다.
- stop deadline 내에 accept/session/capture가 종료된다.
- unbounded thread 또는 queue가 없다.

권장 commit 단위:

1. `feat(config): add strict vision provider configuration`
2. `feat(provider): add bounded health and metadata server`
3. `feat(runtime): compose ordered provider lifecycle`
4. `test(integration): verify degraded startup and bounded shutdown`

## 11. Phase 3 - direct camera data plane

### 11.1 P3.1 encoded preview cache

capture가 새 frame을 publish하면 preview encoder는 frame identity별 color/depth JPEG를 최대 한 번
만든다.

- encoded cache도 stream kind별 latest-wins single slot이다.
- client마다 JPEG를 다시 encode하지 않는다.
- frame이 cache에 publish된 뒤 old buffer owner를 해제한다.
- encoder가 느리면 intermediate frame을 건너뛰고 최신 pending frame 하나만 유지한다.
- encode 실패는 sampled diagnostic/error counter로 남기고 capture는 계속한다.
- JPEG quality와 max dimensions는 bounded config다.

depth preview는 visualization용 colorized JPEG다. metric depth source나 Policy input으로 주장하지
않는다.

### 11.2 P3.2 endpoint 목록

| Method | Path | Response | 역할 |
|---|---|---|---|
| GET | `/stream/color.mjpg` | multipart MJPEG | color preview |
| GET | `/stream/depth.mjpg` | multipart MJPEG | colorized depth preview |
| GET | `/snapshot/color` | `image/jpeg` | latest color observation |
| GET | `/snapshot/depth` | `image/jpeg` | latest colorized depth preview snapshot |
| POST | `/query/roi_depth` | JSON | latest metric ROI statistics |
| POST | `/query/pixel_to_point` | JSON | latest optical-frame point |
| GET | `/snapshot/pointcloud.bin` | PCD1 v2 binary | primary point-cloud snapshot |
| GET | `/snapshot/pointcloud` | JSON | optional debug-only compatibility |

`/metadata`에는 compile-time route가 아니라 runtime에서 실제 활성화된 endpoint만 넣는다. color가
disabled면 color stream/snapshot을 advertise하지 않는다.

### 11.3 frame identity headers

snapshot response와 각 MJPEG part는 다음 header를 제공한다.

- `X-Nodus-Capture-Generation`
- `X-Nodus-Frame-Number`
- `X-Nodus-Capture-Timestamp-Ns`
- `X-Nodus-Capture-Unix-Epoch-Ns`
- `X-Nodus-Sensor-Frame`
- `X-Nodus-Calibration-Id`

HTTP response에는 `Cache-Control: no-store`를 적용한다. Phase 3에서 legacy `X-PA-*` header를 public
contract로 만들지 않는다.

### 11.4 MJPEG backpressure

- `max_stream_clients`는 일반 connection bound 안에서 별도로 제한한다.
- client session은 마지막으로 보낸 frame identity만 기억한다.
- 새 frame이 없으면 busy loop하지 않고 notification/deadline을 기다린다.
- client socket이 느리면 해당 client는 intermediate frame을 drop하고 latest만 전송한다.
- per-client pending write는 최대 MJPEG part 하나다.
- capture/encoder는 socket write 완료를 기다리지 않는다.
- disconnect, idle timeout 또는 app stop에서 session을 cancel한다.
- replay와 client catch-up queue는 제공하지 않는다.

### 11.5 ROI와 pixel query

request JSON은 strict object다.

```json
{"x": 10, "y": 20, "width": 30, "height": 40}
```

```json
{"x": 10, "y": 20}
```

ROI behavior:

- negative origin, non-positive size와 configured maximum area 초과는 400이다.
- image boundary와 겹치는 valid ROI는 clamped ROI를 response에 함께 돌려준다.
- valid depth가 없으면 request 자체는 성공할 수 있으며 `stats.valid=false`로 구분한다.
- frame 자체가 없거나 stale limit를 넘으면 503이다.

pixel behavior:

- image 밖 pixel은 400이다.
- frame은 있지만 해당 depth가 invalid면 200과 `valid=false`, reason을 반환한다.
- valid point는 meter 단위 raw camera optical XYZ다.
- response에는 query에 실제 사용한 complete `FrameIdentity`를 넣는다.

query는 `FrameStore`에서 하나의 immutable `CapturedFrame` owner를 획득한 뒤 adapter/capture lock
밖에서 수행한다. SDK deprojection이 필요한 D435 captured-frame 구현도 자신이 보유한 depth frame만
사용한다. health와 query를 따로 읽어 identity를 합성하지 않는다.

### 11.6 PCD1 v2 binary compatibility

Phase 3 primary point-cloud format은 PA-CONTROL PCD1 version 2 layout을 byte-compatible하게 유지한다.

```text
offset  size  field
0       4     ASCII "PCD1"
4       4     uint32 little-endian version = 2
8       8     uint64 frame_number
16      8     int64 capture_timestamp_ns
24      4     uint32 source_width
28      4     uint32 source_height
32      4     uint32 requested_stride_pixels
36      4     uint32 actual_stride_pixels
40      4     uint32 point_count
44      4     uint32 reserved = 0
48      16    float32 fx, fy, ppx, ppy
64      48    float32 camera_to_frame matrix3x4
112     12*N  float32 optical XYZ point array
112+12*N 3*N  uint8 RGB array
```

Phase 3에서는 points가 raw optical coordinate이므로 matrix3x4는 identity rotation과 zero translation이다.
sensor/calibration/generation/unix timestamp는 HTTP `X-Nodus-*` header로 보완한다. Phase 6에서 geometry
contract를 확장하기 전 PCD1 layout을 임의로 version 3으로 바꾸지 않는다.

writer와 reader fixture는 다음을 검증한다.

- exact magic/version/header byte count
- little-endian integer/float encoding
- exact total length `112 + 15 * point_count`
- bounds/overflow/truncation rejection
- XYZ/RGB ordering
- PA-CONTROL captured golden fixture와 compatibility

JSON point cloud는 explicit debug config가 켜졌을 때만 활성화하고 point count/body size를 더 작은
bound로 제한한다.

### 11.7 freshness와 response consistency

- provider config에 `max_frame_age_ms`를 둔다.
- snapshot/query 시작 시 하나의 frame owner와 identity를 고정한다.
- response body와 header는 같은 identity에서 생성한다.
- encode 완료 전에 더 새 frame이 들어와도 현재 response identity를 바꾸지 않는다.
- capture generation이 바뀌면 이전 generation encoded cache를 즉시 invalidation한다.

### 11.8 Phase 3 validation과 commits

필수 fake-adapter test:

- color/depth JPEG가 decode 가능하고 expected dimensions를 가진다.
- 같은 frame에 여러 client가 붙어도 encode count는 stream kind당 한 번이다.
- snapshot body/header identity가 일치한다.
- MJPEG part boundary/content-length/frame headers가 일치한다.
- slow client가 frame을 drop하는 동안 capture frame number가 전진한다.
- stream client limit와 connection limit가 독립적으로 적용된다.
- stale/no-frame/disabled capability status가 contract와 일치한다.
- ROI clamp/statistics와 invalid-depth behavior가 일치한다.
- pixel point가 optical coordinates와 일치한다.
- PCD1 writer/reader/golden fixture가 byte-compatible하다.
- app stop이 active MJPEG client를 deadline 안에 cancel한다.

Phase 완료 조건:

- 모든 data-plane endpoint가 Vision process에서 직접 제공된다.
- 여러 consumer가 encoded latest cache를 공유한다.
- slow consumer가 capture나 다른 consumer를 block하지 않는다.
- payload, queue, client와 point count에 finite bound가 있다.
- Pilot, recording, Portal과 hardware 없이 test가 재현된다.

권장 commit 단위:

1. `feat(provider): add latest JPEG preview cache`
2. `feat(provider): add bounded snapshot and MJPEG endpoints`
3. `feat(provider): add depth query endpoints`
4. `feat(provider): add PCD1 point-cloud snapshot`
5. `test(integration): verify direct data-plane backpressure`

## 12. 전체 validation command와 evidence

구현 에이전트는 repository에 실제로 추가된 target 이름에 맞춰 다음 범주의 검증을 실행한다.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

추가 정적 검증:

- `git diff --check`
- public include에서 `rs2`/PA-CONTROL path 검색
- schema와 example config JSON parse/validation
- submodule/dependency exact revision 확인
- staged diff의 Phase allowlist 확인

보고할 evidence:

- command와 exit status
- passed test count
- skipped hardware test 목록과 이유
- dependency revision/license
- baseline failure와 task regression 구분
- 변경 file과 commit hash

실제 D435 장치 실행 결과가 없으면 `hardware verified`, `D435 accepted` 같은 표현을 사용하지 않는다.

## 13. Phase별 변경 allowlist

| Phase | 허용 경로 |
|---|---|
| 0 | root foundation, `docs`, `migration`, `docs/agent_docs` gitlink |
| 1 | `include/nodus_vision`, `src/core`, `src/adapters`, related CMake/tests/dependency metadata |
| 2 | `src/config`, `src/provider/http`, `src/runtime`, `app`, config/schema/examples, related tests |
| 3 | provider encoding/routes, point-cloud schema/fixtures, related tests/config/docs |

다음 경로와 책임은 Phase 0-3에서 금지한다.

- `schemas/pilot`, Pilot client/session/catalog code
- recording writer, FFmpeg recording lifecycle와 dataset artifacts
- Portal/Operator/MetaGate repository 변경
- `nodus-control` 또는 Control IPC adapter
- mutable robot/reference-frame endpoint
- object detection, inference, ROS2, WebRTC, CUDA/PCL/OpenCV
- physical camera acceptance script 자동 실행

## 14. 구현 종료와 Phase 4 handoff

Phase 3까지 완료하면 더 진행하지 않고 다음을 남긴다.

1. Phase 0-3 commit 목록과 working tree 상태
2. build/test/static validation 결과
3. fake adapter로 확인한 endpoint 목록
4. unresolved dependency 또는 portability risk
5. hardware 미검증 사실
6. Phase 4가 pin할 Pilot OpenAPI revision/digest 재확인 TODO

Phase 4는 released Pilot public contract를 다시 확인하고 immutable artifact/provenance를 추가한 뒤에만
시작한다. Phase 0-3 구현이 Pilot session 값이나 endpoint catalog generation을 미리 추측해서는 안 된다.
