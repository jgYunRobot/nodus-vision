# Nodus Vision camera provider 이관 설계

## 1. 문서 상태

- 대상 저장소: `nodus-vision`
- 대상 책임 경로: `src/nodus_vision`, `app`
- 상태: foundation 및 migration baseline 확정
- PA-CONTROL 기준 revision: `1c44efbe0b03fa77187305d0f50948f731e972f0`
- Pilot 공개 계약 기준: OpenAPI `1.0.2`, revision
  `46d35dea702e71ae78aa2bb932a11e1bf5e79a73`
- 이 문서는 소스 복사를 승인하지 않는다. 각 checkpoint에서 허용된 책임만 재구현하고 검증한다.

관련 source와 disposition은 `migration/source_manifest.json`, 외부 계약과 dependency 근거는
`docs/migration_ledger.md`가 소유한다.

## 2. 결론

`nodus-vision`은 독립 실행되는 native Camera ServiceProvider로 만든다.

- 한 process가 한 physical camera instance를 소유한다.
- D435 같은 vendor camera는 adapter 뒤에 둔다.
- capture, frame lifetime, preview encoding, depth query, point cloud, recording writer는 C++17
  process 안에 둔다.
- Vision process는 스스로 Pilot에 component를 등록하고 heartbeat와 endpoint catalog를 갱신한다.
- Pilot은 Vision process를 spawn/stop하지 않고 image, depth, point cloud, video를 relay하지 않는다.
- Portal, Operator/Policy, MetaGate는 Pilot에서 compatible endpoint를 찾은 뒤 Vision을 직접 읽는다.
- UI와 3D rendering은 `nodus-portal`, Policy observation과 추론은 `nodus-operator`, recording
  session 정합과 durable dataset commit은 MetaGate/Gym 책임으로 유지한다.
- `nodus-control`, Control IPC, UDS는 Vision dependency가 아니다.

초기 migration은 PA-CONTROL의 검증된 Camera data-plane 의미를 유지하되, 기존 Pilot subprocess
supervisor와 PA-CPU camera 전용 API는 옮기지 않는다.

```text
                                   lifecycle + endpoint catalog
                           +------------------------------------+
                           |                                    v
physical camera -> nodus-vision provider ----------------> nodus-pilot
                         |     ^                         metadata only
                         |     |
                         |     +--- direct service request
                         +--------> nodus-portal
                         +--------> nodus-operator / Policy
                         +--------> MetaGate / recording collector

nodus-control is not on the Vision path.
```

## 3. PA-CONTROL 구현 분석

### 3.1 native camera module

`modules/vision/intel_d435`는 현재 다음 동작을 제공한다.

- `librealsense2` pipeline과 frame lifetime을 `IntelD435Camera` 내부 RAII state로 소유한다.
- serial selector, depth/color profile, align option, min/max depth를 config로 받는다.
- blocking `readFrame()`과 non-blocking `pollFrame()`을 제공한다.
- frame number, monotonic capture timestamp, Unix epoch capture timestamp, device timestamp/domain,
  profile, intrinsics와 depth scale을 snapshot으로 보존한다.
- latest depth frame 기반 단일 pixel depth/deprojection과 ROI 통계를 제공한다.
- `rs2::pointcloud::calculate()`와 texture mapping으로 RGB point cloud를 만든다.
- JPEG/recording encoder가 SDK frame memory를 복사 없이 읽도록 owner를 포함한 frame view를
  반환한다.
- public header는 `rs2::frame`이나 raw SDK handle을 노출하지 않는다.

이 경계는 유효하다. 다만 public type 이름 전체가 Intel D435에 묶여 있으므로 target에서는 공통
camera value contract와 vendor adapter를 분리한다. 실제 두 번째 vendor가 생기기 전까지 speculative
virtual hierarchy는 만들지 않고, composition이 concrete adapter를 선택하는 작은 interface만 둔다.

### 3.2 camera runtime process

`apps/pacf/camera_runtime`은 C++17 executable이며 다음을 한 process에 조합한다.

- CLI config와 D435 lazy creation
- camera start/stop과 capture thread
- latest color/depth JPEG cache
- MJPEG color/depth preview
- color JPEG snapshot
- ROI depth와 pixel-to-point query
- JSON/binary point-cloud snapshot
- mutable reference-frame transform
- FFmpeg/libx264 RGB MP4 recording
- `frames.jsonl`과 `recording_manifest.json`
- health/metadata HTTP endpoint

현재 endpoint는 다음과 같다.

| Method | Path | 현재 의미 | 이관 결정 |
|---|---|---|---|
| GET | `/health` | camera/driver/frame/recording health | 유지, schema 명시 |
| GET | `/metadata` | profile, calibration, endpoint snapshot | 유지, provider metadata로 재정의 |
| GET | `/stream/color.mjpg` | color MJPEG stream | 초기 호환 유지 |
| GET | `/stream/depth.mjpg` | display용 depth MJPEG | 초기 호환 유지 |
| GET | `/snapshot/color` | Policy/consumer용 JPEG snapshot | 유지 |
| GET | `/snapshot/depth` | display용 latest depth JPEG snapshot | preview 보조 endpoint로 추가 |
| POST | `/query/roi_depth` | latest metric depth ROI 통계 | 유지 |
| POST | `/query/pixel_to_point` | pixel/depth/camera point | 유지 |
| GET | `/snapshot/pointcloud` | JSON RGB point cloud | debug compatibility로만 유지 |
| GET | `/snapshot/pointcloud.bin` | PCD1 v2 binary RGB point cloud | primary spatial snapshot으로 유지 |
| POST | `/reference_frame` | 외부가 runtime transform을 덮어씀 | baseline에서 제거/호환 격리 |
| POST | `/recordings/start` | current RGB recording 시작 | idempotent 계약으로 보강 |
| POST | `/recordings/stop` | writer finalize | idempotent 계약으로 보강 |
| GET | `/recordings/current` | current artifact 상태 | 유지 |

frame마다 MJPEG part에 `X-Frame-Number`, `X-Capture-Timestamp-Ns`,
`X-Capture-Unix-Epoch-Ns`를 붙이고, 같은 identity가 recording sidecar로 연결되는 점은 유지한다.

### 3.3 현재 runtime의 기술 부채

현재 동작은 migration evidence로 유용하지만 구현을 그대로 복사하면 안 된다.

1. HTTP server가 request parsing, CORS, client thread와 streaming lifecycle을 직접 구현한다.
2. stream client마다 thread를 만들고 종료 시 모든 client thread를 join하므로 느리거나 끊기지 않은
   client가 shutdown을 지연시킬 수 있다.
3. wildcard CORS와 plain HTTP가 항상 켜져 있고 advertised address와 bind address가 분리되지 않는다.
4. JSON response를 문자열 조합으로 만들며 public schema/version artifact가 없다.
5. health/query/point-cloud 요청이 capture SDK state mutex와 경쟁할 수 있다.
6. recording root가 repository-relative `assets/camera/tmp` 의미에 묶여 있고 recording identity와
   idempotency key가 없다.
7. JPEG encoder header를 librealsense 내부 third-party path에서 우연히 가져온다.
8. metadata의 owner가 `pilot`으로 고정돼 새 독립 ownership과 맞지 않는다.
9. 외부가 `/reference_frame`으로 latest point cloud 좌표를 바꾸므로 calibration, robot pose와 frame
   timestamp의 provenance가 섞인다.
10. Python config model, C++ CLI와 endpoint snapshot이 같은 필드를 중복 정의한다.

### 3.4 기존 Pilot camera manager

`apps/pacf/pilot/src/pilot/camera_manager`는 config loader, process supervisor, device manager와 health
polling을 제공한다. `runtime_executable_path`, `auto_start`, `restart_on_exit`, startup grace/timeout을
소유하고 camera runtime command를 조립한다.

이 subprocess ownership은 target Pilot architecture와 충돌하므로 복사하지 않는다.

- process launch/restart는 shell, systemd, container 또는 별도 deployment supervisor가 맡는다.
- Vision이 자기 config를 직접 읽는다.
- Vision이 Pilot public HTTP lifecycle client를 소유한다.
- Pilot session loss는 capture를 자동 중단하지 않는다. Vision은 local data plane을 유지하면서
  bounded backoff로 재등록한다.
- catalog는 Vision이 실제 bind된 endpoint를 기준으로 full replacement publication한다.

기존 manager의 config validation, lifecycle state 이름, health timeout과 endpoint field는 behavior
reference로 사용한다.

### 3.5 PA-CPU, UI, Policy와 recording consumer

기존 `pa_cpu`는 `/api/cameras`와 camera lifecycle/recording route를 제공하고 endpoint URL을 UI에
재포장한다. target에서는 Pilot의 generic component/endpoint directory가 이 역할을 대체한다.
Camera별 Pilot route나 manager를 추가하지 않는다.

기존 `apps/web_ui`의 camera panel과 point-cloud parser는 다음 repository로 이동한다.

- device list와 descriptor discovery: `nodus-portal`
- preview/ROI/point-cloud rendering: `nodus-portal`
- optical-to-render conversion `optical(x,y,z) -> mount(x,z,-y)`: Portal rendering boundary
- lifecycle/recording button: Pilot에서 endpoint를 찾은 뒤 Vision service 직접 호출

기존 Policy readiness와 `color_snapshot_url` 소비는 `nodus-operator` 책임이다. Vision은 Policy
feature 이름이나 model input tensor를 알지 않는다.

기존 Camera recording writer와 manifest 생성은 Vision으로 옮기지만 다음은 옮기지 않는다.

- RobotStatus/action/camera reference 정합
- 여러 camera의 recording start/stop transaction
- episode commit/discard
- dataset revision, materialization, training validation

이 책임은 MetaGate/Gym에 남긴다.

## 4. target ownership

| 책임 | owner | 비고 |
|---|---|---|
| physical camera handle/pipeline | Vision adapter | process당 한 camera |
| frame timestamp/lifetime/latest slot | Vision core | bounded latest-wins |
| JPEG/MJPEG/point-cloud encoding | Vision provider | frame당 한 번 encode/cache |
| RGB recording writer/sidecar/manifest | Vision recording | durable commit은 아님 |
| camera config/calibration artifact | Vision | UI preference 제외 |
| component session/heartbeat/catalog client | Vision Pilot client | public HTTP only |
| provider registry/catalog/generation | Pilot | payload relay 없음 |
| camera UI/Three.js rendering | Portal | direct endpoint consumer |
| Policy observation selection/inference | Operator | direct endpoint consumer |
| recording orchestration/sample alignment | MetaGate | direct Vision call |
| dataset final commit/materialization | Gym | finalized artifact consumer |
| robot command/control loop | Control | Vision dependency 없음 |

## 5. repository 구조

구현 checkpoint가 진행되면 다음 구조를 목표로 한다.

```text
nodus-vision/
  AGENTS.md
  CMakeLists.txt
  CMakePresets.json
  app/
    CMakeLists.txt
    main.cpp
  include/nodus_vision/
    camera_contracts.hpp
    provider_health.hpp
  src/
    core/
      frame_store/
      geometry/
      recording/
    adapters/
      intel_d435/
    provider/
      http/
    pilot/
      component_client/
    config/
  assets/configs/
    examples/
  schemas/
    pilot/v1/
    vision/v1/
  migration/source_manifest.json
  tests/
    core/
    adapters/
    provider/
    contracts/
    integration/
```

폴더는 실제 책임이 생기는 checkpoint에서만 추가한다. `core`에 빈 abstraction이나 vendor 두 개를
가정한 class hierarchy를 미리 만들지 않는다.

## 6. native runtime와 library 경계

### 6.1 executable

설치 executable 이름은 `nodus-vision`으로 둔다. 하나의 binary를 camera별 config로 여러 번
실행한다.

```bash
nodus-vision /etc/nodus/vision/top_d435.json
nodus-vision /etc/nodus/vision/wrist_d435.json
```

각 process는 하나의 `device_id`, one adapter, one HTTP listener, one Pilot component session과 one
recording state machine을 가진다. 한 process가 여러 USB camera를 동시에 소유하는 manager 구조는
초기에 만들지 않는다.

### 6.2 libraries

- `nodus_vision_core`: camera-neutral frame, timestamp, geometry, recording value contracts
- `nodus_vision_intel_d435`: RealSense concrete adapter
- `nodus_vision_provider_http`: direct data-plane server
- `nodus_vision_pilot_client`: public Pilot lifecycle/catalog client
- `nodus_vision`: executable composition이 소비하는 aggregate static library

Library code는 concrete logger를 소유하지 않고 app에서 diagnostics sink를 연결한다. capture hot path는
per-frame INFO log를 만들지 않는다.

## 7. config 계약

Vision config는 strict versioned JSON object로 둔다. unknown field는 거부한다.

```json
{
  "schema_version": 1,
  "device_id": "top_d435",
  "component_id": "camera.top_d435",
  "device": {
    "adapter": "intel_d435",
    "serial_number": "241222076339",
    "depth": {"width": 640, "height": 480, "fps": 30},
    "color": {"enabled": true, "width": 640, "height": 480, "fps": 30},
    "align_target": "color",
    "depth_range_m": {"min": 0.1, "max": 6.0}
  },
  "calibration": {
    "calibration_id": "top_d435_mount_v1",
    "sensor_frame": "top_d435_color_optical_frame",
    "mount_frame": "e_rob_wrist_cam",
    "mount_local_transform": {
      "x": 0.0,
      "y": 0.0,
      "z": 0.06,
      "r1": 0.0,
      "r2": 0.0,
      "r3": 0.0,
      "euler_type": "XYZ"
    }
  },
  "provider": {
    "bind_host": "127.0.0.1",
    "port": 8902,
    "advertised_base_url": "http://127.0.0.1:8902",
    "allowed_origins": [],
    "max_connections": 32,
    "max_stream_clients": 8
  },
  "pilot": {
    "base_url": "http://127.0.0.1:8765",
    "heartbeat_interval_ms": 2000
  },
  "recording": {
    "root": "/var/lib/nodus/vision/top_d435",
    "color_codec": "h264"
  }
}
```

기존 config에서 다음 field는 그대로 또는 이름을 정규화해 이관한다.

- `device_id`, `device_type/adapter`, `serial_number`
- depth/color profile와 align
- `calibration_id`, `sensor_frame`, `mount_frame`, `mount_local_transform`
- bind host/port
- exact browser `allowed_origins`; omitted or empty means cross-origin browser access disabled

다음 field는 Vision config에서 제거한다.

- `runtime_executable_path`: deployment owner 책임
- `auto_start`, `restart_on_exit`, process stop timeout: systemd/container/supervisor 책임
- `ui`: Portal preference 책임
- `runtime_endpoint`: `advertised_base_url`에서 파생
- arbitrary `extra_arguments`: strict typed config로 대체
- `mount_link_id`: Control이 공개한 named frame을 사용하므로 legacy numeric link lookup 제거

`bind_host=0.0.0.0`은 허용할 수 있지만 `advertised_base_url`에는 wildcard address를 허용하지 않는다.
serial이 비어 있으면 hardware가 둘 이상인 환경에서 fail closed한다.

## 8. camera-neutral frame 계약

공통 frame metadata는 최소 다음을 가진다.

- `device_id`
- `frame_number`
- `capture_timestamp_ns`: process-local monotonic ordering/freshness
- `capture_unix_epoch_ns`: same-host/offline 정합
- `device_timestamp`와 `device_timestamp_domain`
- color/depth profile
- intrinsics와 depth scale
- `sensor_frame`, `calibration_id`
- drop/timeout/freshness diagnostics

SDK frame object는 adapter 내부에 남긴다. core/public API는 owned view 또는 immutable snapshot만
사용한다. consumer가 frame view를 보유하는 동안 SDK memory owner가 살아 있어야 한다.

capture와 consumer 사이에는 FIFO backlog를 만들지 않는다.

```text
camera capture thread
  -> immutable latest frame slot (generation + frame_number)
      -> preview encoder latest cache
      -> bounded query snapshot
      -> recording writer
      -> point-cloud builder on demand or configured rate
```

느린 browser, Policy, recording consumer가 capture thread를 block하지 않는다. capture callback에서
HTTP write, JSON serialization, file flush를 수행하지 않는다.

## 9. timestamp와 clock domain

기존 dual-clock 측정은 유지한다.

1. frameset 획득 직후 monotonic clock을 읽는다.
2. 이어서 Unix epoch clock을 읽는다.
3. 두 값과 device timestamp/domain을 같은 frame identity에 붙인다.

두 host clock read는 원자적인 같은 물리 시점이 아니다. cross-host 정합은 NTP/PTP 또는 별도 clock
contract 없이는 보장하지 않는다.

Pilot endpoint descriptor의 `clock_domain`은 실제 deployment를 반영한다.

- same-host monotonic만 보장: `monotonic_same_host`
- clock synchronization을 배포에서 확인: `unix_epoch_synchronized`
- 그 외: `provider_defined`

frame number만으로 process restart 전후 frame을 식별하지 않는다. consumer identity에는 Pilot
`server_instance_id`, Vision `instance_id`, session/catalog generation과 provider capture generation을
포함한다.

## 10. 좌표계와 calibration

Vision adapter의 authoritative source geometry는 camera optical frame이다. `mount_frame`은 Control/Pilot
RobotStatus에서 선택할 named frame이며 `mount_local_transform`은 그 frame에 대한 camera의 고정 부착
pose다.

```text
pixel + depth -> point_camera_optical
point_mount = T_mount_camera_optical * point_camera_optical
point_root(t) = T_root_mount(t) * point_mount
```

기존 runtime의 mutable `/reference_frame`은 외부 robot pose가 camera payload 자체를 바꾸게 하므로
target baseline으로 사용하지 않는다.

- `mount_local_transform`은 versioned calibration config이며 translation/Euler를 startup에서 canonical
  `T_mount_camera_optical` matrix로 한 번 정규화한다.
- translation 단위는 meter, Euler `r1/r2/r3` 단위는 radian이다. `euler_type=A1A2A3`의 rotation은
  `R_A1(r1) * R_A2(r2) * R_A3(r3)`이며 PA-CONTROL nodus_rm convention과 같아야 한다.
- PA-CONTROL의 `optical(x,y,z) -> body(x,z,-y)` convention은 canonical matrix에 한 번 합성한다.
- point-cloud payload는 raw optical point와 transform metadata를 명확히 구분한다.
- Portal은 optical remap을 별도로 반복하지 않고 public matrix를 정확히 한 번 적용한다.
- dynamic `mount_frame -> base/world`는 camera timestamp에 맞는 RobotStatus/kinematics sample을 가진
  consumer가 계산한다.
- mount frame pose가 움직이면 같은 합성 chain을 사용하는 camera와 point cloud도 함께 움직인다.
- Vision이 나중에 robot-frame query를 제공하려면 Pilot status sample contract, interpolation,
  maximum skew와 used-sample identity를 별도 설계한 뒤 추가한다.

이렇게 해야 runtime transform을 반복해서 push하다 생기는 stale pose와 double axis conversion을 막을
수 있다.

## 11. direct provider HTTP 계약

첫 migration은 기존 payload path를 유지해 Portal/Policy 이관을 단순화한다. 동시에 Vision-owned
OpenAPI와 binary schema를 추가해 response shape를 고정한다.

### 11.1 descriptor catalog

Vision은 Pilot에 다음 descriptor를 필요한 기능만큼 publish한다.

| descriptor id | capability | kind | method/media |
|---|---|---|---|
| `health` | `camera.health.get` | service | GET JSON |
| `metadata` | `camera.metadata.get` | service | GET JSON |
| `color-preview` | `camera.stream.color.preview` | stream | MJPEG |
| `depth-preview` | `camera.stream.depth.preview` | stream | MJPEG |
| `color-snapshot` | `camera.snapshot.color` | service | GET image/jpeg |
| `depth-snapshot` | `camera.snapshot.depth.preview` | service | GET image/jpeg |
| `roi-depth` | `camera.query.roi_depth` | service | POST JSON |
| `pixel-to-point` | `camera.query.pixel_to_point` | service | POST JSON |
| `pointcloud-binary` | `camera.snapshot.pointcloud` | service | GET binary |
| `recording-start` | `camera.recording.start` | service | POST JSON |
| `recording-stop` | `camera.recording.stop` | service | POST JSON |
| `recording-current` | `camera.recording.current` | service | GET JSON |
| `lifecycle` | `camera.lifecycle.set` | service | POST JSON |

JSON point cloud는 compatibility/debug descriptor로만 선택적으로 publish한다. Consumer는 component
type만 보고 endpoint를 선택하지 않고 `(component_id, descriptor_id, contract_version, schema_id)`를
확인한다.

### 11.2 HTTP server 선택

기존 handwritten socket server를 그대로 복사하는 선택과 maintained server를 사용하는 선택은 다음
차이가 있다.

| 선택 | 장점 | 단점 |
|---|---|---|
| 기존 server 복사 | migration diff가 작고 기존 endpoint 동작 재사용 | parser/CORS/thread/shutdown 유지보수와 streaming client leak를 직접 책임 |
| `cpp-httplib` | integration이 작고 C++17/header-only, bounded task queue 구성 가능 | long-lived MJPEG client가 worker를 점유하고 advanced streaming 제어가 제한적 |
| Boost.Asio/Beast | connection/session/backpressure/shutdown을 명시적으로 소유, 이후 WebSocket 확장 가능 | 코드량과 dependency/build 복잡도가 큼 |

권장은 Boost.Asio/Beast다. camera stream은 장기 connection이고 다음 단계에서 binary WebSocket을
추가할 가능성이 있으므로 connection state와 backpressure를 명시적으로 다루는 편이 낫다. 다만 V2
checkpoint에서 health/metadata와 한 MJPEG stream spike를 먼저 만들고, shutdown deadline과 bounded
connection evidence가 없으면 dependency 선택을 확정하지 않는다.

server 구현과 무관하게 다음 제한을 contract로 고정한다.

- bounded request body, header, connection, stream-client 수
- read/write/idle/shutdown deadline
- stream client별 latest-frame only, catch-up replay 없음
- exact allowed CORS origin list, wildcard는 local explicit config에서만 허용
- endpoint bind 성공 후 Pilot catalog publication
- app stop 시 accept 중단, stream session cancel, capture stop, recording finalize 순서

## 12. Pilot integration

Vision은 `component_type: camera` compatibility label로 등록할 수 있지만 Pilot behavior가 이 값에
분기한다고 가정하지 않는다. capability와 descriptor가 실제 의미를 가진다.

초기 lifecycle은 다음과 같다.

```text
load strict config
  -> bind provider HTTP server
  -> initialize camera adapter
  -> connected/streaming 또는 degraded health 확정
  -> Pilot component register
  -> publish complete endpoint catalog
  -> heartbeat + capture + serve
```

`service_endpoints` registration field는 빈 object로 보내고, endpoint는 Phase F catalog로만
publish한다.

Pilot session replacement, expiry 또는 restart가 발생하면 다음 순서를 따른다.

1. old session/catalog identity 폐기
2. capture와 local direct server는 유지
3. single-flight bounded backoff registration
4. new session으로 full catalog publication
5. health state 갱신

catalog generation을 추측하지 않는다. publication response와 exact expected generation을 사용한다.
Pilot이 없다고 camera device를 반복 open/close하지 않는다.

## 13. recording 계약

Vision은 camera별 한 active recording을 초기 제한으로 둔다. 그러나 start/stop은 request retry에
안전해야 한다.

```text
idle
  -> preparing(recording_id)
  -> recording
  -> finalizing
  -> finalized
  -> handed_off | superseded
```

start request는 최소 `request_id`, `recording_id`, expected profile을 가진다. 동일 request retry는 같은
결과를 반환하고 다른 recording이 active면 conflict를 반환한다. arbitrary output path를 받지 않는다.

Vision-owned state root 예시는 다음과 같다.

```text
<recording_root>/
  .staging/<recording_id>/
    color.mp4
    frames.jsonl
    recording_manifest.json.tmp
  finalized/<recording_id>/
    color.mp4
    frames.jsonl
    recording_manifest.json
```

finalize는 writer flush/trailer, sidecar close, manifest validation 후 atomic directory activation으로
끝난다. manifest는 다음을 포함한다.

- schema version, Vision instance/capture generation
- device/camera/calibration identity
- color/depth profile와 codec
- start/stop timestamp
- submitted frame count와 latest frame mapping
- artifact relative path, size, checksum
- incomplete/error state

MetaGate는 여러 camera와 RobotStatus/action cursor를 조율하고 finalized artifact를 handoff한다. Gym은
그 artifact를 dataset revision으로 materialize/commit한다. Vision은 dataset path나 Gym revision을
알지 않는다.

초기 RGB H.264 writer는 기존 FFmpeg/libx264 동작을 migration baseline으로 사용한다. depth recording,
hardware sync와 multi-camera transaction은 별도 phase다.

## 14. logging과 health

health는 low-rate snapshot이며 frame payload를 포함하지 않는다.

- process/server ready
- adapter connected/streaming
- active profile and calibration id
- capture generation/latest frame number
- latest frame age
- timeout/drop/error counters
- preview encoder status
- recording state/id/frame count
- Pilot session/catalog state
- active/bounded client counts

정상 per-frame capture/encode는 INFO로 남기지 않는다. lifecycle, config summary, registration,
recording start/finalize는 INFO, recoverable frame/USB/consumer 문제는 sampled WARNING, 상세 frame
diagnostic은 rate-limited DEBUG로 둔다.

## 15. dependency 정책

- CMake 3.28, C++17, Ninja를 baseline으로 둔다.
- `librealsense`는 exact revision을 pinned external checkout 또는 submodule로 소비하고 source를 수정하지
  않는다.
- `nodus_rm`을 사용한다면 exact gitlink와 필요한 target만 소비한다.
- JSON library, HTTP server와 test dependency는 실제 target과 함께 pinned한다.
- FFmpeg는 build에서 component/version/codec availability를 확인하고 package provenance를 남긴다.
- `stb_image_write`를 librealsense 내부 path에서 include하지 않는다. 직접 pin하거나 다른 JPEG encoder를
  선택한다.
- OpenCV/PCL/CUDA/ROS2는 초기 dependency가 아니다. 실제 perception/geometry 요구와 profiling evidence가
  생긴 뒤 adapter 또는 provider extension으로 추가한다.

## 16. phased implementation

### V0: repository foundation와 provenance

- root AGENTS/rules, pinned `docs/agent_docs`
- CMake/CMakePresets/.clang-format/.gitignore/setup script
- README, progress, migration manifest/ledger
- 이 migration design

완료 조건:

- source code와 native dependency를 복사하지 않는다.
- PA-CONTROL source revision/path와 target ownership이 명확하다.
- repository가 empty-target CMake project로 parse 가능하다.

### V1: camera-neutral core와 Intel D435 adapter

- common timestamp/profile/intrinsics/frame view contract
- Intel D435 lifecycle와 config adapter
- RAII frame owner, latest frame slot
- depth pixel/ROI/deprojection
- disconnected fake adapter와 deterministic unit test

완료 조건:

- public contract에 `rs2` type이 없다.
- no-device path가 hardware 없이 검증된다.
- existing D435 behavior parity fixture가 통과한다.

### V2: provider skeleton와 typed config

- strict JSON config
- app composition와 ordered shutdown
- health/metadata server
- bounded connection/deadline server spike
- adapter disabled/degraded mode

완료 조건:

- camera와 Pilot 없이 health/metadata를 제공한다.
- SIGINT/SIGTERM에서 accept/session/capture thread가 bounded 종료한다.
- bind와 advertised endpoint validation이 분리된다.

### V3: capture와 direct data-plane parity

- color/depth preview encode cache
- MJPEG streams와 color snapshot
- ROI/pixel query
- binary point-cloud PCD1 compatibility reader/writer
- frame headers/timestamps/freshness

완료 조건:

- 여러 client가 같은 frame encode cache를 공유한다.
- slow client가 capture를 막지 않는다.
- JSON point cloud는 optional debug path이고 binary가 primary다.

### V4: Pilot public integration

- pinned OpenAPI/provenance
- component registration/heartbeat/disconnect
- full endpoint catalog publication
- Pilot loss/restart/session replacement recovery
- direct provider mock consumer integration

완료 조건:

- Pilot source import와 Control/UDS dependency가 없다.
- catalog removal/replacement/republication이 generation-safe하다.
- provider payload가 Pilot를 통과하지 않는다.

### V5: recording artifact parity와 hardening

- FFmpeg RGB writer와 frame sidecar 이관
- idempotent recording lifecycle
- owned staging/finalized root와 atomic finalize
- manifest validation/checksum
- MetaGate handoff fixture

완료 조건:

- short recording의 submitted/decoder-visible frame count가 일치한다.
- retry/crash가 half-finalized artifact를 current로 노출하지 않는다.
- Vision은 dataset commit을 수행하지 않는다.

### V6: calibration와 geometry contract

- camera optical contract
- named dynamic `mount_frame`과 versioned static `mount_local_transform`
- legacy `mount_link_id` 제거와 canonical camera-to-mount matrix
- point-cloud transform metadata
- Portal static/dynamic transform composition fixture
- robot-frame transform consumer handoff

완료 조건:

- raw optical point와 static mount point schema가 섞이지 않는다.
- calibration id, sensor/mount frame과 matrix가 모든 spatial response에 있다.
- mount frame pose가 변하면 consumer fixture의 camera pose도 함께 변한다.
- mutable latest robot pose를 Vision에 push하지 않는다.

### V7: product integrations

- Portal camera workspace
- Operator/Policy color snapshot observation
- MetaGate recording coordination
- multi-camera discovery and selection

완료 조건:

- 각 consumer가 Pilot에서 exact descriptor를 고르고 Vision을 직접 읽는다.
- 하나의 consumer 실패가 다른 consumer나 capture를 중단하지 않는다.

### V8: hardware acceptance

- 실제 D435 serial/profile capture
- USB permission/bandwidth evidence
- preview/query/point-cloud/recording smoke
- Pilot restart와 Portal reconnect
- two-camera USB root-hub/bandwidth diagnostic

hardware 실행은 별도 사용자 승인 후에만 수행한다.

#### V8-0 single D435 trusted-LAN bring-up

승인된 최초 실물 검증은 다음과 같이 범위를 고정한다.

- model: Intel RealSense D435 (`8086:0b07`)
- observed librealsense camera serial: `241222076339` (udev USB identity separately reports
  `234423028813`)
- selector: the example leaves `serial_number` empty and accepts only one name-matching D435;
  multiple matches fail closed
- host boundary: local USB 3 device access, local Pilot `127.0.0.1:8765`, trusted-LAN Provider
  `192.168.219.106:8902`
- configured streams: Depth Z16 640x480@30 and Color RGB8 640x480@30
- capture timeout: 100 ms; the observed 5000 ms failures were caused by a USB/UVC fault that
  required a Camera hardware reset, not by the normal frame wait bound
- configured Provider/Pilot/recording failure timeouts remain at least 1000 ms
- migrated identity: PA-CONTROL `top_d435`, `top_d435_mount`, and its existing static local transform
- calibration status: `unconfigured`; hardware bring-up does not claim extrinsic calibration
- exact command: `./run_app.sh --config assets/configs/examples/intel_d435_pilot.json`

첫 acceptance는 process start, exact device selection, frame advance, `/health`, `/metadata`, Color
snapshot/stream, Pilot catalog, and Portal Color preview를 확인했다. 후속 DP acceptance에서 D435
Depth RGB preview snapshot/MJPEG와 Portal Depth card의 frame 진행까지 확인했다. Full ROI statistics,
populated PCD1, recording, disconnect recovery, and two-camera bandwidth는 후속 V8 checkpoints다.

## 17. validation matrix

| 상황 | 기대 결과 |
|---|---|
| no camera | process/health available, explicit degraded adapter state |
| Pilot absent | capture/data plane remains local, catalog retry bounded |
| Pilot restart | new session/catalog identity, payload server unchanged |
| slow MJPEG client | client-local drop/latest, capture frame number advances |
| too many stream clients | bounded rejection, no new unbounded thread |
| invalid ROI | closed 4xx error, capture remains streaming |
| no fresh frame | explicit unavailable/stale response |
| camera disconnect | adapter degraded, client/server and Pilot heartbeat remain alive |
| recording retry | same request returns same recording identity/result |
| recording crash | `.staging` remains non-final and is never advertised as finalized |
| consumer cross-host without sync | no synchronized-clock claim |
| Portal point cloud | one optical conversion, no double transform |
| Policy observation | direct color snapshot with frame/timestamp identity |
| Control running/not running | Vision behavior unchanged; no direct dependency |

## 18. initial acceptance

Migration is complete only when all of the following are true.

1. PA-CONTROL is not a build/runtime dependency.
2. D435 camera handle and frame lifetime are owned only by Vision.
3. Pilot does not spawn Vision or relay camera payloads.
4. Vision registers and republishes its catalog through the pinned public Pilot contract.
5. Portal, Operator and MetaGate discover exact compatible descriptors and connect directly.
6. capture/encode/query/recording paths are bounded and slow consumers do not build an unbounded
   backlog.
7. timestamp, capture generation, calibration and coordinate frame provenance are explicit.
8. recording artifact creation is Vision-owned while episode/dataset commit remains external.
9. no Camera, Pilot or Control hardware acceptance is claimed from mock-only evidence.
10. every implementation checkpoint updates this design and `docs/progress.md` before the next
    responsibility is added.

## 19. deferred decisions

- MJPEG 이후 2D preview를 WebRTC/H.264/VP8로 전환할 시점
- binary spatial stream을 WebSocket, WebTransport 또는 WebRTC data channel 중 무엇으로 둘지
- depth recording format과 compression
- multi-camera hardware sync/trigger group contract
- hand-eye calibration procedure와 artifact signing
- perception/object detection을 Vision process extension으로 둘지 별도 provider로 둘지
- remote auth/TLS/reverse-proxy와 camera privacy deployment profile
- ROS2 camera bridge와 rosbag import/export

이 결정은 initial D435 provider migration의 blocker가 아니다.
