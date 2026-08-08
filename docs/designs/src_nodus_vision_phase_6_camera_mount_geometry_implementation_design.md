# Nodus Vision Phase 6 camera mount geometry implementation design

## 1. 문서 상태

- 대상 repository: `nodus-vision`
- 대상 단계: Phase 6 / camera mount geometry contract
- 작성일: 2026-08-08
- 선행 조건: Phase 5 recording acceptance 및 review finding remediation 완료
- PA-CONTROL 기준 revision: `1c44efbe0b03fa77187305d0f50948f731e972f0`
- 상태: implementation ready after M6-0 contract review

이 문서는 PA-CONTROL의 `mount_frame`, `mount_local_transform`, `mount_link_id` 역할을 분리하고,
Nodus 구조에서 카메라가 움직이는 robot frame에 부착되는 계약을 정의한다. 구현자는 이 문서와 root
`AGENTS.md`, `docs/rules.md`, `docs/agent_docs/coding_rules.md`를 함께 따른다.

## 2. PA-CONTROL 의미와 이관 기준

PA-CONTROL camera config의 세 필드는 다음 역할이었다.

- `mount_frame`: 카메라가 부착될 frame 이름. 예: `e_rob_wrist_cam`
- `mount_local_transform`: parent link 기준으로 위 frame을 등록하기 위한 고정 위치/회전
- `mount_link_id`: parent link를 숫자로 조회하기 위한 legacy 식별자

Pilot은 `mount_link_id`와 `mount_local_transform`으로 Control에 frame 등록을 요청했다. Control은 매
kinematics update마다 해당 frame pose를 계산해 RobotStatus에 넣었고, Pilot은 이름이 `mount_frame`과 같은
최신 pose를 camera runtime의 mutable `/reference_frame`으로 전송했다.

따라서 의도 자체는 다음과 같다.

```text
robot mount frame이 움직임
  -> mount frame에 고정된 camera도 함께 움직임
  -> camera point를 robot/base/world에 표시할 때 같은 frame pose를 사용
```

Nodus에서는 Control이 이미 이름 있는 frame을 공개하므로 `mount_link_id`를 다시 노출하지 않는다.
카메라는 `mount_frame` 이름으로 동적 pose를 연결하고, `mount_local_transform`은 그 frame에 대한 카메라의
고정 부착 pose만 설명한다.

## 3. 결정 요약

### 3.1 config의 단일 의미

Vision config는 다음 calibration 정보를 사용한다.

```json
{
  "calibration": {
    "calibration_id": "front_d435_mount_v1",
    "sensor_frame": "front_d435_color_optical_frame",
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
  }
}
```

- `mount_frame`은 Control/Pilot RobotStatus에서 찾을 frame 이름이다.
- `mount_local_transform`은 `mount_frame`에서 camera body까지의 고정 rigid transform이다.
- `sensor_frame`은 camera optical point의 frame 이름이다.
- `mount_link_id`는 config, public schema, runtime에서 사용하지 않는다.
- 임의 output frame 선택이나 Vision 내부 frame graph는 만들지 않는다.

### 3.2 runtime canonical matrix

사람이 작성하는 config는 translation/Euler 표현을 유지하되 runtime은 시작 시 한 번만 다음 행렬로
정규화한다.

```text
T_mount_camera_optical
  = T_mount_camera_body(mount_local_transform)
  * T_camera_body_camera_optical(axis convention)
```

좌표 변환은 항상 다음 식이다.

```text
p_mount = T_mount_camera_optical * p_camera_optical
```

- 위치 단위는 meter다.
- `r1`, `r2`, `r3`의 각도 단위는 radian이다.
- homogeneous column vector `[x, y, z, 1]^T`를 사용한다.
- public 4x4 array는 row-major다.
- `euler_type=A1A2A3`이면 local rotation은
  `R_mount_camera_body = R_A1(r1) * R_A2(r2) * R_A3(r3)` 순서로 합성한다. 이 순서는
  PA-CONTROL의 `nodus_rm::matrix::RotationMatrix(euler, EulerType)` 의미와 동일해야 한다.
- PA-CONTROL의 optical remap `optical(x,y,z) -> body(x,z,-y)`를 canonical matrix에 정확히 한 번
  합성한다.
- request마다 Euler를 계산하거나 별도 hidden axis remap을 추가하지 않는다.

`mount_local_transform`과 `camera_to_mount_matrix4x4`를 config의 독립 입력 두 개로 동시에 허용하지
않는다. 두 값이 달라지는 dual source of truth를 피하기 위해 matrix는 normalized runtime/public output이다.

### 3.3 동적 pose 합성

카메라가 wrist와 함께 움직이는 최종 식은 다음과 같다.

```text
T_root_camera_optical(t_capture)
  = T_root_mount(t_capture)
  * T_mount_camera_optical

p_root
  = T_root_mount(t_capture)
  * T_mount_camera_optical
  * p_camera_optical
```

- `T_mount_camera_optical`은 Vision calibration이 소유하는 static 값이다.
- `T_root_mount(t_capture)`는 `mount_frame` 이름과 capture timestamp에 맞는 RobotStatus/FK consumer가
  소유한다.
- mount frame이 움직이면 같은 합성으로 카메라와 point cloud도 움직인다.
- Vision은 Control IPC에 연결하거나 최신 robot pose를 mutable state로 받지 않는다.

### 3.4 consumer가 동적 합성을 소유

Vision은 raw optical payload와 static mount metadata를 제공한다. Portal, Policy observation adapter처럼
camera와 robot state를 함께 사용하는 consumer가 timestamp에 맞춰 dynamic mount pose를 합성한다.

이 경계를 택하는 이유는 다음과 같다.

- camera capture와 robot pose의 timestamp/skew를 consumer가 명시적으로 판단할 수 있다.
- 늦게 도착한 latest pose가 과거 camera payload 의미를 바꾸지 않는다.
- Vision payload 경로가 Pilot/Control 상태 때문에 block되지 않는다.
- 동일 Vision endpoint를 fixed camera와 moving wrist camera가 함께 사용할 수 있다.

## 4. 목표

1. `mount_frame` 이름으로 camera와 dynamic robot frame을 연결한다.
2. `mount_local_transform`을 camera의 고정 부착 pose로 유지한다.
3. legacy `mount_link_id`를 제거한다.
4. optical axis remap과 local extrinsic을 canonical matrix에 한 번만 합성한다.
5. JSON spatial query와 PCD1 v2가 static camera-to-mount 정보를 명확히 운반한다.
6. capture timestamp에 맞는 mount pose가 있을 때만 root/base/world 합성을 수행한다.
7. Portal fixture로 static transform과 dynamic mount pose가 각각 한 번만 적용되는지 검증한다.
8. capture/stream/recording 경로를 geometry consumer로부터 분리한다.

## 5. 범위 밖

- `mount_link_id` 또는 link index 기반 조회
- Vision의 Control IPC/UDS 연결
- Vision의 RobotStatus polling과 mutable `/reference_frame`
- Vision 내부 arbitrary frame graph, graph search 또는 cycle resolution
- dynamic pose interpolation service 구현
- hand-eye calibration 측정 UI와 calibration artifact signing
- 실제 D435/robot hardware acceptance
- Portal production camera workspace 구현
- Operator/Policy product integration
- ROS TF/TF2 bridge

## 6. ownership

| 책임 | owner |
|---|---|
| camera optical deprojection | camera adapter / Vision core |
| `mount_frame`, `mount_local_transform`, calibration ID | Vision config |
| optical-to-body convention과 canonical static matrix | Vision geometry |
| raw point/query/stream/recording | Vision provider |
| named robot frame pose와 kinematics | Control/Pilot public status path |
| capture-time mount pose 선택과 dynamic composition | Portal/Policy 등 consumer |
| calibration 측정 workflow | deferred external tool |

Pilot catalog는 Vision direct endpoint와 static metadata를 발견하게 할 뿐 image, point, matrix payload를 relay하지
않는다.

## 7. config contract

### 7.1 schema 유지와 변경

Provider config `schema_version: 1`을 유지한다. calibration object는 다음 필드를 요구한다.

```text
calibration_id
sensor_frame
mount_frame
mount_local_transform{x,y,z,r1,r2,r3,euler_type}
```

현재 config의 `camera_to_mount_matrix4x4`는 `mount_local_transform`으로 교체한다. Phase 6 구현 시 repository
example과 parser/schema/test를 같은 checkpoint에서 전환한다. 아직 외부 release consumer가 없다는 현재
전제에서 한 개의 canonical config shape만 유지하며, 두 입력 형식의 장기 compatibility layer는 만들지 않는다.

### 7.2 validation

startup에서 다음을 검증하고 실패하면 provider/camera start 전에 fail closed한다.

- frame/calibration string이 non-empty bounded UTF-8 text
- translation은 meter, Euler 값은 radian이며 모두 finite
- `euler_type`이 지원 목록 `XYZ`, `XZY`, `YXZ`, `YZX`, `ZXY`, `ZYX`, `ZXZ`, `ZYZ` 중 하나
- translation 각 축이 configured maximum absolute bound 이하
- normalized matrix의 마지막 row가 `[0,0,0,1]`
- rotation이 tolerance 내 orthonormal이고 determinant가 `+1`
- scale, shear, reflection이 없음

초기 bound/tolerance는 named constant로 두고 request에서 바꾸지 않는다.

### 7.3 frame name 의미

Vision은 `mount_frame` 문자열의 robot 종류나 link 의미를 해석하지 않는다. Consumer는 Pilot public
RobotStatus가 제공한 frame map에서 정확한 이름으로 찾는다.

- frame이 없으면 camera payload는 계속 제공하되 robot overlay는 `mount_frame_unavailable` 상태다.
- frame identity/generation이 바뀌면 이전 pose를 재사용하지 않는다.
- fixed camera는 움직이지 않는 workspace/table frame을 같은 방식으로 지정할 수 있다.

## 8. C++ geometry boundary

권장 layout:

```text
src/geometry/
  CMakeLists.txt
  camera_mount_transform.hpp
  camera_mount_transform.cpp

tests/geometry/
  CMakeLists.txt
  test_camera_mount_transform.cpp
```

internal normalized value:

```cpp
struct CameraMountTransform {
    std::string calibration_id;
    std::string sensor_frame;
    std::string mount_frame;
    std::array<double, 16> mount_from_camera_optical_matrix4x4;
};
```

minimum operations:

```cpp
CameraMountTransform buildCameraMountTransform(const CalibrationConfig& calibration);
std::array<float, 3> transformCameraPointToMount(
    const std::array<float, 3>& point_camera_optical,
    const CameraMountTransform& transform);
std::array<float, 12> buildMountFromCameraMatrix3x4(
    const CameraMountTransform& transform);
```

새 Eigen/OpenCV/PCL dependency를 추가하지 않는다. config load 시 matrix를 한 번 만들고 runtime point transform은
precomputed matrix multiply만 수행한다.

## 9. runtime integration

adapter와 `CapturedFrame`은 계속 raw optical data만 생성하고 보관한다.

```text
camera adapter
  -> immutable FrameStore raw optical owner
      -> preview/recording: unchanged
      -> pixel/ROI query: raw point + one static mount point
      -> point cloud: raw points + static matrix metadata
```

- capture thread에서 whole point cloud를 mount/root frame으로 변환하지 않는다.
- preview JPEG/MJPEG, RGB recording, depth storage semantics는 바꾸지 않는다.
- JSON query의 소수 point만 provider request worker에서 static 변환한다.
- PCD point array는 raw optical로 유지해 기존 decoder와 recording 의미를 보존한다.
- runtime calibration은 immutable이므로 mutable geometry lock을 추가하지 않는다.

## 10. public spatial contract

Phase 6 additive field를 반영해 Vision Provider API minor version은 `1.3.0`으로 올린다. 새 route나
`output_frame` request parameter는 추가하지 않는다.

OpenAPI만 소비하는 구현도 좌표 의미를 재구성할 수 있도록 모든 geometry field에 다음 내용을 직접
기록한다.

- point 단위는 meter
- matrix는 row-major homogeneous 4x4
- column-vector 식 `p_mount = T_mount_camera_optical * p_camera_optical`
- `point_mount_m`은 static camera-to-mount까지만 적용된 값이며 root/base/world point가 아님
- dynamic `T_root_mount(t_capture)`는 timestamp-aware consumer가 별도로 적용

### 10.1 pixel query

기존 `point_camera_m`을 그대로 유지하고 static mount point와 geometry metadata를 추가한다.

```json
{
  "schema_version": 1,
  "frame": {},
  "geometry": {
    "calibration_id": "front_d435_mount_v1",
    "sensor_frame": "front_d435_color_optical_frame",
    "mount_frame": "e_rob_wrist_cam",
    "mount_from_camera_optical_matrix4x4": [
      1, 0, 0, 0,
      0, 0, 1, 0,
      0, -1, 0, 0.06,
      0, 0, 0, 1
    ]
  },
  "point_camera_m": {"x": 0.1, "y": 0.2, "z": 0.5},
  "point_mount_m": {"x": 0.1, "y": 0.5, "z": -0.14}
}
```

`point_mount_m`은 static transform까지만 적용된 값이다. root/base/world point가 아니며 dynamic
`T_root_mount(t_capture)`는 consumer가 적용한다.

### 10.2 ROI query

기존 `median_point_camera_m`을 유지하고 `median_point_mount_m`과 같은 `geometry` object를 추가한다. depth
scalar 통계는 변경하지 않는다.

### 10.3 headers와 metadata

기존 spatial response header를 유지하고 mount frame을 추가한다.

```text
X-Nodus-Sensor-Frame
X-Nodus-Mount-Frame
X-Nodus-Calibration-Id
```

`/metadata` calibration object도 `calibration_id`, `sensor_frame`, `mount_frame`과 canonical matrix를
공개한다. Header/body/PCD metadata가 불일치하면 contract test가 실패해야 한다.

별도 `/geometry` endpoint는 만들지 않는다. 이미 존재하는 `/metadata`와 spatial response가 필요한
정보를 제공한다.

## 11. PCD1 v2 semantics

새 PCD1 v3를 만들지 않는다. 기존 v2 layout의 `camera-to-frame matrix3x4` 슬롯을 원래 목적대로 사용한다.

| 영역 | Phase 6 의미 |
|---|---|
| XYZ point array | raw `sensor_frame` optical coordinates |
| matrix3x4 | `mount_from_camera_optical` static transform |
| response mount header | matrix target인 `mount_frame` |

- magic, version, offsets, length와 raw point array 의미는 바꾸지 않는다.
- 기존 identity hardcoding을 config에서 정규화한 matrix3x4로 교체한다.
- 기존 decoder가 matrix를 무시해도 raw optical point는 이전과 동일하다.
- 새 consumer는 raw point에 matrix를 정확히 한 번 적용한다.
- point array 자체를 mount frame으로 미리 변환하지 않는다.

`schemas/vision/v1/pointcloud_pcd1_v2.md`는 identity-only 표현을 위 의미로 갱신한다. Codec reader도 matrix를
discard하지 않고 decoded value에 보존한다.

## 12. Portal/consumer composition

Portal은 두 public input을 결합한다.

```text
Vision PCD1:
  p_camera_optical, T_mount_camera_optical,
  mount_frame, capture_timestamp, capture_generation

Pilot RobotStatus:
  T_root_mount(t), frame_name, status timestamp/generation
```

render transform은 다음 한 번의 chain이다.

```text
p_root = T_root_mount(t_capture) * T_mount_camera_optical * p_camera_optical
```

금지 사항:

- Portal에서 `(x,z,-y)`를 matrix와 별도로 다시 적용
- Vision이 point를 mount로 변환한 뒤 PCD matrix도 다시 적용
- 최신 RobotStatus pose를 timestamp 확인 없이 과거 point cloud에 적용
- mount frame missing/generation change 시 마지막 pose를 계속 재사용

Vision repository는 Portal source를 수정하지 않고 deterministic fixture를 제공한다.

```text
schemas/vision/v1/fixtures/
  camera_mount_geometry_v1.json
  pointcloud_pcd1_v2_camera_mount.bin
  pointcloud_pcd1_v2_camera_mount_expected.json
```

fixture는 static offset, optical axis remap, 두 개의 다른 dynamic mount pose를 포함한다. 두 pose에서 camera
point가 mount와 함께 정확히 이동하고 static offset이 두 번 적용되지 않음을 검증할 수 있어야 한다.
PCD1 `.bin`은 repository에 immutable golden bytes로 커밋하며 provider test는 binary와 expected JSON을 모두
직접 읽어 codec output, decoded header, raw point, RGB와 matrix를 비교한다. C++ hardcoded 값만 비교해
fixture가 독립적으로 drift하는 상태는 acceptance가 아니다.

## 13. timestamp와 recovery handoff

Vision response는 기존 capture identity를 유지한다.

- Vision instance identity
- capture generation
- frame number
- monotonic capture timestamp
- Unix epoch capture timestamp
- sensor frame, mount frame, calibration ID

robot-aware consumer는 다음 evidence 없이 root/base/world 합성을 확정하지 않는다.

- RobotStatus server/session/generation identity
- 선택한 status sample timestamp
- camera-to-status skew와 maximum allowed skew
- interpolation을 사용했다면 양쪽 sample identity

현재 Pilot public status가 capture-time history/interpolation을 제공하지 않으면 Phase 6 Vision 구현은 static
camera-to-mount까지만 완료한다. Portal의 동적 overlay acceptance는 해당 public status 계약이 준비된 뒤 진행하며,
Vision에 mutable latest-pose 우회 경로를 추가하지 않는다.

## 14. failure and recovery matrix

| scenario | expected behavior |
|---|---|
| invalid local transform | startup fail closed, provider not ready |
| unsupported Euler type | config rejection |
| mount frame unavailable | Vision payload 정상, robot overlay unavailable |
| RobotStatus generation change | old mount pose 폐기 |
| stale status timestamp | consumer가 dynamic composition 거부 |
| camera reconnect | 같은 static calibration, 새 capture generation |
| Pilot absent/restart | direct Vision endpoints와 capture 유지 |
| PCD consumer ignores matrix | raw optical compatibility 유지 |
| geometry-aware consumer | static matrix와 dynamic pose 각각 한 번 적용 |
| recording active | recording artifact semantics 변경 없음 |

## 15. tests

### 15.1 config/geometry

- PA-CONTROL example `z=0.06`, `XYZ` normalization
- supported Euler conventions
- radian 단위와 PA-CONTROL/nodus_rm 다축 Euler composition parity
- non-finite/bounded translation rejection
- optical `(x,y,z) -> body(x,z,-y)` fixture
- translation/rotation composition order
- `mount_link_id` unknown-field rejection
- matrix orthonormal/determinant validation

### 15.2 JSON/provider

- legacy camera point fields unchanged
- mount point equals canonical matrix applied exactly once
- pixel/ROI geometry metadata parity
- headers, `/metadata`, response geometry identity equality
- query failure does not stop capture

### 15.3 PCD1

- v2 layout/length/point bytes remain compatible
- non-identity matrix3x4 round trip
- decoder preserves matrix
- raw point plus matrix equals JSON mount point fixture
- committed `.bin` golden bytes와 expected JSON의 direct conformance
- malformed/non-finite matrix rejection

### 15.4 integration

- fake-camera application lifecycle
- Pilot catalog remains metadata-only
- Pilot restart does not stop Vision direct endpoints
- two dynamic mount pose fixture compositions
- static transform and optical remap double-application detection

No test accesses a physical camera, robot, `/dev/bus/usb`, Control IPC or UDS.

## 16. implementation checkpoints

### M6-0 contract correction

- freeze `mount_frame` and `mount_local_transform` semantics
- remove `mount_link_id` from the migrated contract
- freeze matrix direction and optical composition order
- record PCD1 v2 compatibility decision

Gate: no code before the above meanings are reviewed.

Suggested commit: `docs: define Phase 6 camera mount geometry`

### M6-1 config normalization

- replace config matrix input with `mount_local_transform`
- add strict parsing/validation
- normalize once to canonical matrix

Gate: config/geometry unit tests pass without provider route changes.

Suggested commit: `feat(config): define camera mount calibration`

### M6-2 geometry library

- implement optical convention and local pose composition
- expose immutable normalized value to runtime
- add deterministic transform tests

Gate: geometry target builds independently.

Suggested commit: `feat(geometry): compose camera mount transform`

### M6-3 JSON spatial contract

- add static mount point and geometry metadata
- preserve existing camera point fields
- add mount header and metadata parity

Gate: pixel/ROI/OpenAPI contract tests pass.

Suggested commit: `feat(provider): expose camera mount geometry`

### M6-4 PCD1 v2 matrix

- populate existing matrix3x4 slot
- preserve raw point bytes and layout
- make decoder retain and validate matrix
- add golden fixture

Gate: legacy and geometry-aware PCD1 tests pass.

Suggested commit: `feat(provider): publish PCD1 mount transform`

### M6-5 discovery and handoff

- expose static geometry in Vision metadata/Pilot descriptor metadata
- add Portal/consumer fixture and timestamp handoff documentation
- prove Pilot receives no image/point payload

Gate: fake Pilot discovery and direct Vision reads pass.

Suggested commit: `feat(pilot): publish camera mount metadata`

### M6-6 acceptance

- format/build/full CTest when explicitly authorized
- repeat geometry/provider/application lifecycle tests
- staged diff review and progress update
- stop before Portal product integration or hardware execution

Suggested commit: `test(geometry): complete Phase 6 acceptance`

## 17. validation commands

Build/test는 사용자 명시 요청이 있을 때만 실행한다.

```bash
cmake --preset debug
cmake --build --preset debug -j2
ctest --test-dir build/debug --output-on-failure
ctest --test-dir build/debug --output-on-failure --repeat until-fail:20 \
  -R 'geometry|query_serializer|pcd1|application_lifecycle'
git diff --check
git status --short
```

## 18. acceptance

Phase 6는 다음 조건을 모두 만족할 때 완료다.

1. `mount_frame`이 dynamic robot frame 이름으로 보존된다.
2. `mount_local_transform`이 camera의 fixed local pose로 정규화된다.
3. `mount_link_id`가 Vision contract에 없다.
4. matrix 방향/layout/unit과 optical convention이 명시된다.
5. JSON camera/mount point가 구분되고 PCD1 raw point semantics가 유지된다.
6. PCD1 v2 matrix3x4가 configured static transform을 운반한다.
7. mount frame pose가 변할 때 consumer fixture의 camera pose도 함께 변한다.
8. static transform, optical remap, dynamic pose가 각각 한 번만 적용된다.
9. mutable `/reference_frame`이나 latest robot pose push가 없다.
10. fake-only acceptance를 physical camera/robot acceptance로 주장하지 않는다.

## 19. Phase 7 handoff

Phase 6 완료 후 Vision은 static camera-to-mount contract에서 멈춘다. Portal camera workspace 또는 Policy
observation에서 실제 moving mount를 사용하려면 Pilot public RobotStatus frame identity/history와
capture-to-status skew 정책을 먼저 확정한다. 이 후속 consumer는 Vision의 raw payload와 static matrix를
재사용하며 Vision에 Control-specific link id나 mutable robot pose를 되돌려 넣지 않는다.
