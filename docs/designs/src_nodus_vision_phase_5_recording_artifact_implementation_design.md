# Nodus Vision Phase 5 recording artifact 상세 구현 설계

## 1. 문서 상태

- 대상 저장소: `nodus-vision`
- 선행 기능 기준: Phase 4 `cb86cdd`
- 선행 필수 gate: `src_nodus_vision_phase_4_implementation_review_design.md`의 R4 remediation 완료
- target migration checkpoint: V5 recording artifact parity와 hardening
- PA-CONTROL source revision: `1c44efbe0b03fa77187305d0f50948f731e972f0`
- source references:
  - `apps/pacf/camera_runtime/src/camera_runtime_color_video_writer.*`
  - `apps/pacf/camera_runtime/src/camera_runtime_app.*`
  - `docs/apps/camera_recording_design.md`
- 상태: implementation ready after R4 gate

## 2. 목표

Vision process가 자기 camera의 RGB recording writer와 immutable artifact를 소유하도록 이관한다.
MetaGate가 idempotent start/stop을 조정하고 finalized artifact를 인계받을 수 있어야 하지만, Vision은
episode 또는 dataset commit을 수행하지 않는다.

```text
CapturedFrame
  -> bounded recording ingress
  -> FFmpeg H.264 writer + append-only frames.jsonl
  -> validated .staging artifact
  -> atomic finalized/<recording_id>
  -> MetaGate handoff reference
  -> Gym materialization/commit                         external owner
```

완료 결과:

1. capture thread를 block하지 않는 bounded recording queue가 있다.
2. retry-safe start/stop/current HTTP contract가 있다.
3. `color.mp4`, `frames.jsonl`, `recording_manifest.json`이 같은 recording identity를 가진다.
4. half-finalized artifact는 finalized/current로 노출되지 않는다.
5. short recording의 submitted sidecar frame과 decoder-visible frame count가 일치한다.
6. finalized artifact에는 size와 SHA-256 checksum이 있다.

## 3. 제외 범위

- Gym episode/dataset directory와 revision activation
- MetaGate 실제 repository 구현 또는 multi-camera transaction
- robot status/action/camera timestamp alignment
- depth recording, audio, segmentation 또는 perception output
- recording video download/relay through Pilot
- Portal recording UI
- camera-to-mount/robot geometry Phase 6
- physical D435 recording acceptance
- arbitrary client-provided filesystem output path

Phase 5의 MetaGate 범위는 public HTTP fixture와 artifact handoff contract 검증뿐이다.

## 4. PA-CONTROL behavior disposition

| source behavior | decision | reason |
|---|---|---|
| FFmpeg/libx264 RGB to H.264 MP4 | migrate and harden | 검증된 parity baseline |
| RGB24 to YUV420P `sws_scale` | migrate | encoder input parity |
| `veryfast`, `zerolatency`, no B-frame | retain initially | deterministic low-latency flush |
| sequential color frame index | retain | sidecar/video mapping identity |
| append-only `frames.jsonl` | retain and version | exact frame mapping |
| one active recording per camera process | retain | one-camera-per-process ownership |
| fixed `assets/camera/tmp/<id>` overwrite | reject | retry/crash/identity에 안전하지 않음 |
| body 없는 start/stop | reject | idempotency와 expected identity 없음 |
| manifest direct truncate write | reject | partial final manifest 노출 가능 |
| capture thread에서 FFmpeg encode | reject | capture latency/backpressure 결합 |
| dataset commit/discard | external owner | MetaGate/Gym responsibility |

## 5. target modules

```text
src/recording/
  CMakeLists.txt
  color_video_writer.hpp/.cpp
  recording_contracts.hpp/.cpp
  recording_manifest.hpp/.cpp
  recording_store.hpp/.cpp
  recording_manager.hpp/.cpp
tests/recording/
  test_color_video_writer.cpp
  test_recording_contracts.cpp
  test_recording_manifest.cpp
  test_recording_store.cpp
  test_recording_manager.cpp
tests/integration/
  test_recording_http_lifecycle.cpp
```

책임:

| unit | owns | does not own |
|---|---|---|
| `ColorVideoWriter` | FFmpeg context, frame submission, flush/trailer | directory/manifest/API |
| `RecordingStore` | root validation, staging/finalized layout, atomic activation | encoder |
| `RecordingManifest` | typed sidecar/manifest serialization and validation | lifecycle thread |
| `RecordingManager` | state machine, idempotency, bounded queue, worker/finalizer | HTTP parsing |
| provider routes | request validation and response mapping | FFmpeg/filesystem internals |

각 unit은 C++17 static library로 독립 build 가능하게 구성한다. reusable library는 stdout/stderr나 concrete
logger를 직접 사용하지 않는다.

## 6. dependency와 provenance

Phase 5는 host FFmpeg development packages를 사용한다.

- `libavformat`
- `libavcodec`
- `libavutil`
- `libswscale`
- H.264 encoder: `libx264`

P5-0에서 `pkg-config`로 실제 version, codec availability와 linker target을 확인해
`docs/migration_ledger.md`에 기록한다. build가 우연히 PA-CONTROL checkout library를 참조하면 안 된다.

license gate:

- FFmpeg build configuration과 component license를 기록한다.
- `libx264`가 포함된 배포물의 GPL 조건을 release 전에 확인한다.
- private research build라는 사실이 dependency provenance 생략 근거가 되지 않는다.

SHA-256은 별도 shell process를 호출하지 않는다. linked `libavutil`의 SHA-256 API 또는 별도로 pin한
reviewed implementation 중 하나를 P5-0에서 확정한다. checksum 계산은 finalize worker가 bounded chunk로
파일을 읽으며 수행한다.

## 7. strict recording configuration

기존 config에 required `recording` object를 추가한다.

```json
{
  "recording": {
    "enabled": true,
    "root": "/var/lib/nodus-vision/recordings",
    "queue_capacity_frames": 120,
    "max_duration_ms": 600000,
    "minimum_free_bytes": 1073741824,
    "finalize_timeout_ms": 10000,
    "bit_rate_bps": 8000000,
    "preset": "veryfast",
    "tune": "zerolatency"
  }
}
```

규칙:

- disabled mode에서도 모든 field를 explicit하게 둔다.
- root는 config-owned absolute path다. HTTP request에서 path를 받지 않는다.
- root, `.staging`, `finalized`는 같은 filesystem이어야 한다.
- root 또는 관리 하위 directory가 symlink면 start를 거부한다.
- queue/duration/free-space/finalize/bitrate bound는 positive upper bound를 가진다.
- initial codec은 `libx264`, pixel format은 `yuv420p`, max B-frame은 0으로 고정한다.
- color가 disabled면 recording enabled config를 거부한다.
- profile width/height가 YUV420P 요구처럼 even dimension인지 validation한다.

test config는 `mkdtemp` 아래 absolute root를 사용한다. repository working tree를 runtime artifact root로
사용하지 않는다.

## 8. filesystem lifecycle

```text
<recording_root>/
  .staging/
    <recording_id>/
      start_request.json
      stop_request.json                 optional until stop
      color.mp4
      frames.jsonl
      recording_manifest.json.tmp
  finalized/
    <recording_id>/
      start_request.json
      stop_request.json
      color.mp4
      frames.jsonl
      recording_manifest.json
```

### 8.1 recording ID

`recording_id`는 filesystem component 전용 closed format을 사용한다.

```text
^[a-z0-9][a-z0-9._-]{0,63}$
```

`.`과 `..`, slash, backslash, control character, percent-encoded path와 Unicode separator를 거부한다.
request ID는 Pilot identifier bound를 따르되 filesystem path로 사용하지 않는다.

### 8.2 staging create

- `create_directory`가 새 directory를 만들었을 때만 writer를 연다.
- 기존 staging/finalized ID를 truncate하거나 재사용하지 않는다.
- canonical start request를 `start_request.json.tmp`에 쓰고 flush/fsync/rename한다.
- required free space를 확인한 뒤 video와 sidecar를 연다.
- 준비가 모두 성공한 뒤에만 state를 `recording`으로 바꾼다.

중간 실패는 `.staging`에 incomplete evidence를 남기되 current/finalized로 광고하지 않는다.

### 8.3 atomic finalize

finalize 순서:

1. new frame admission 차단
2. bounded queue drain
3. encoder flush와 MP4 trailer write
4. `frames.jsonl` flush/close
5. file size와 SHA-256 계산
6. manifest cross-validation
7. `recording_manifest.json.tmp` flush/fsync
8. tmp manifest를 final manifest로 rename하고 staging directory fsync
9. `.staging/<id>`를 `finalized/<id>`로 atomic rename
10. finalized parent directory fsync
11. in-memory state를 `finalized`로 publish

어느 단계든 실패하면 directory는 `.staging`에 남고 `faulted/incomplete`로 표시한다. finalized directory를
부분 수정하거나 rollback 목적으로 삭제하지 않는다.

startup recovery는 `.staging`을 scan해 count와 ID만 redacted health에 표시한다. Phase 5에서는 crash
artifact를 자동 finalize/delete하지 않는다.

## 9. recording state machine

```text
DISABLED
IDLE
  -> PREPARING(recording_id)
       -> RECORDING
       -> FAULTED_STAGING
RECORDING
  -> FINALIZING
       -> FINALIZED
       -> FAULTED_STAGING
FINALIZED
  -> PREPARING(new recording_id)
```

한 process에서 active/preparing/finalizing recording은 최대 하나다. 이전 finalized artifact는 새 recording
start가 와도 삭제하지 않는다.

`RecordingManager` 한 owner가 state와 request ledger를 변경한다. capture thread는 state machine을
수정하지 않고 frame admission 결과만 받는다.

## 10. capture와 backpressure

capture thread path:

```text
CapturedFrame ready
  -> publish FrameStore
  -> RecordingManager.trySubmitFrame(shared immutable owner)
  -> return immediately
```

- `trySubmitFrame`은 preallocated bounded ring과 non-blocking `try_lock`을 사용한다.
- lock contention 또는 full queue에서는 current frame을 recording에서 drop하고 counter를 증가시킨다.
- capture, FrameStore와 preview encoding은 기다리지 않는다.
- queue는 `shared_ptr<const CapturedFrame>` lifetime만 보유하고 raw SDK pointer를 분리 저장하지 않는다.
- writer worker는 frame identity와 RGB view를 같은 immutable owner에서 읽는다.
- recording frame 순서는 `(capture_generation, frame_number)` strictly increasing이어야 한다.
- generation change는 manifest에 기록하고 default로 recording을 controlled fault/finalize한다. 서로 다른
  camera stream incarnation을 한 artifact에 조용히 섞지 않는다.

drop은 숨기지 않는다. manifest에 `admitted_frame_count`, `submitted_frame_count`,
`recording_drop_count`, first/last identity를 기록한다. MetaGate fixture는 기본 acceptance에서 drop 0을
요구한다.

## 11. FFmpeg writer contract

baseline parameters:

- input: packed RGB24, exact configured width/height/stride
- output: MP4/H.264, `libx264`, YUV420P
- time base: `1 / configured_fps`
- preset/tune: configured allowlist, initial `veryfast`/`zerolatency`
- GOP: one second (`fps`)
- B-frame: 0
- bit rate: strict config

`writeFrame()`은 submitted `video_frame_index`와 assigned PTS를 반환한다. Sidecar line은 FFmpeg가 input
frame을 수락한 뒤 append한다. flush/trailer가 실패하면 manifest를 finalize하지 않는다.

PTS 정책은 Phase 5에서 하나로 고정한다.

- initial implementation은 sequential frame index PTS를 사용해 PA-CONTROL parity를 유지한다.
- actual capture monotonic/unix timestamp는 sidecar에 보존한다.
- queue drop이 있으면 playback 간격과 capture time이 다를 수 있으므로 manifest에 drop/gap을 표시한다.
- variable-frame-rate PTS 전환은 consumer compatibility test와 별도 Provider API revision 후 수행한다.

runtime은 decoder를 hot path에서 실행하지 않는다. decoder-visible frame count는 finalize acceptance test와
artifact validator fixture가 확인한다.

## 12. `frames.jsonl` contract

각 encoded input frame마다 exactly one line을 append한다.

```json
{"schema_version":1,"recording_id":"episode-0001-front","video_frame_index":0,"video_pts":0,"capture_generation":1,"frame_number":42,"capture_timestamp_ns":123456789,"capture_unix_epoch_ns":1770000000000000000,"sensor_frame":"front_optical","calibration_id":"front_v1"}
```

규칙:

- UTF-8, one compact JSON object per line, trailing newline required
- `video_frame_index`는 0부터 gap 없이 증가
- frame identity는 strictly increasing
- timestamp와 calibration은 해당 immutable captured frame에서 복사
- line size에 fixed maximum을 둔다.
- finalize validator는 line count, index continuity, identity ordering과 final newline을 확인한다.
- sidecar는 append-only이며 frame마다 fsync하지 않는다. finalize에서 flush/close한다.

## 13. manifest contract

`recording_manifest.json` 최소 shape:

```json
{
  "schema_version": 1,
  "state": "finalized",
  "recording_id": "episode-0001-front",
  "component_id": "camera.front",
  "instance_id": "vision-...",
  "device_id": "front_d435",
  "capture_generation": 1,
  "sensor_frame": "front_optical",
  "calibration_id": "front_v1",
  "profile": {
    "width": 640,
    "height": 480,
    "fps": 30,
    "input_pixel_format": "rgb24"
  },
  "video": {
    "container": "mp4",
    "codec": "h264",
    "encoder": "libx264",
    "pixel_format": "yuv420p",
    "time_base_num": 1,
    "time_base_den": 30
  },
  "started_monotonic_ns": 123,
  "started_unix_epoch_ns": 456,
  "stopped_monotonic_ns": 789,
  "stopped_unix_epoch_ns": 999,
  "admitted_frame_count": 120,
  "submitted_frame_count": 120,
  "recording_drop_count": 0,
  "first_frame": {},
  "last_frame": {},
  "artifacts": [
    {"path":"color.mp4","size_bytes":1234,"sha256":"..."},
    {"path":"frames.jsonl","size_bytes":5678,"sha256":"..."}
  ],
  "start_request_id": "start-001",
  "stop_request_id": "stop-001",
  "stop_reason": "requested"
}
```

manifest path는 recording directory 기준 relative basename만 허용한다. absolute path와 `..`를 넣지
않는다. `instance_id`는 Vision process identity이며 Pilot session ID는 artifact에 기록하지 않는다.

finalize validator:

- state와 identity/profile 일치
- sidecar line count == submitted frame count
- MP4 submitted/decoder-visible count == sidecar count in acceptance fixture
- artifact exists, regular file, non-symlink
- recorded size/checksum exact
- start <= stop for each clock domain
- no NaN/Inf/out-of-range integer

## 14. HTTP API

Vision OpenAPI를 additive minor version으로 갱신하고 다음 endpoint를 추가한다.

### 14.1 start

```http
POST /recordings/start
Content-Type: application/json

{
  "schema_version": 1,
  "request_id": "start-001",
  "recording_id": "episode-0001-front",
  "expected_device_id": "front_d435",
  "expected_calibration_id": "front_v1",
  "expected_profile": {"width":640,"height":480,"fps":30,"pixel_format":"rgb24"}
}
```

- `201`: new recording entered `recording`
- `200`: exact idempotent replay of already accepted request
- `400`: malformed/unsafe identity
- `409`: active different recording, request ID reuse, profile/device/calibration mismatch
- `503`: recording disabled/unavailable or insufficient bounded resource

### 14.2 stop

```http
POST /recordings/stop
Content-Type: application/json

{"schema_version":1,"request_id":"stop-001","recording_id":"episode-0001-front"}
```

finalization은 writer worker에서 수행한다.

- `202`: finalization accepted/in progress
- `200`: exact retry and artifact already finalized
- `404`: recording identity unknown
- `409`: request ID reuse, different active identity, incomplete/faulted staging
- `503`: finalizer resource unavailable

### 14.3 current

```http
GET /recordings/current
```

response는 `disabled|idle|preparing|recording|finalizing|finalized|faulted` state, recording ID,
counters, start/stop time, finalized relative artifact reference와 redacted error를 반환한다. file bytes는 이
endpoint로 반환하지 않는다.

## 15. idempotency

- request ID와 canonical request bytes를 함께 저장한다.
- 같은 request ID + 같은 bytes는 같은 logical result를 반환한다.
- 같은 request ID + 다른 bytes는 `idempotency_conflict`다.
- 같은 recording ID + 다른 start request는 `recording_identity_conflict`다.
- transport timeout 뒤 caller는 same request ID/body를 재전송할 수 있다.
- start/stop ledger는 active memory뿐 아니라 staging/finalized request file과 manifest에서 복구한다.
- process crash 후 `.staging` artifact는 finalized로 가장하지 않는다. automatic salvage는 Phase 5 범위가
  아니다.

idempotency history는 filesystem recording identity로 bound된다. unbounded global in-memory request map을
추가하지 않는다.

## 16. Pilot catalog와 health

`recording.enabled`일 때 다음 descriptor 세 개를 full catalog에 추가한다.

| descriptor_id | capability | method | schema_id |
|---|---|---|---|
| `recording-start` | `camera.recording.start` | POST | `nodus.vision.recording.start.response.v1` |
| `recording-stop` | `camera.recording.stop` | POST | `nodus.vision.recording.stop.response.v1` |
| `recording-current` | `camera.recording.current` | GET | `nodus.vision.recording.current.response.v1` |

start/stop descriptor의 service request schema ID도 Vision OpenAPI mapping에 기록한다. Phase 4의
generation-safe full catalog replacement을 재사용하며 별도 Pilot client를 만들지 않는다.

Vision `/health`에는 payload path나 request body 없이 다음 low-rate recording summary만 추가한다.

- enabled/state/recording_id
- queue depth/capacity
- admitted/submitted/drop count
- finalization state와 redacted last error
- orphan staging count

normal per-frame recording을 INFO log나 Pilot state update로 내보내지 않는다.

## 17. shutdown

normal application stop:

1. Pilot에 stopping state/disconnect를 bounded best effort로 전달해 catalog를 먼저 제거한다.
2. provider가 새 recording request를 받지 않게 한다.
3. capture frame admission을 중단하고 capture thread를 join한다.
4. active recording queue를 drain하고 configured deadline 안에 finalize한다.
5. deadline/finalize failure면 staging incomplete를 보존한다.
6. provider session/stream, encoder, camera adapter를 기존 ordered path로 종료한다.

application mutex를 잡은 채 writer join, FFmpeg flush, checksum 또는 filesystem sync를 수행하지 않는다.

## 18. acceptance matrix

| scenario | expected result |
|---|---|
| recording disabled | descriptor 없음, start explicit unavailable, normal provider 유지 |
| exact start retry | writer/directory 하나, same identity/result |
| request ID reuse | conflict, existing recording unchanged |
| unsafe recording ID | no filesystem path created |
| active different recording | conflict |
| fake RGB short recording | valid MP4, sidecar and decoded frame count exact |
| queue overflow | capture uninterrupted, drop count/gap explicit |
| generation change | controlled fault/finalize, mixed identity 없음 |
| exact stop retry | single finalization and same finalized result |
| stop during finalizing | bounded 202/current polling |
| writer/manifest/checksum failure | staging remains, finalized absent |
| crash staging fixture | startup never advertises finalized |
| finalized path collision | no overwrite |
| symlink/path traversal | rejected |
| checksum mutation | validator rejects |
| Pilot absent/restart | recording/data plane continues; catalog recovers only |
| application shutdown | deadline bound; valid finalized or preserved incomplete staging |
| direct MetaGate fixture | discovery then direct start/stop/current; Pilot receives no video |

No test may require physical camera. FFmpeg tests use deterministic synthetic RGB frames and temporary directories.

## 19. implementation checkpoints

### P5-0 Phase 4 remediation and FFmpeg provenance

- complete R4-A through R4-E first
- inspect host FFmpeg/libx264 versions, license and CMake targets
- update migration ledger and dependency gate

Gate: Phase 4 review findings are closed and existing 17-test baseline passes before recording code.

### P5-1 recording contracts and store

- strict config/schema
- recording IDs/request DTOs/state values
- staging/finalized safe path owner
- request persistence and atomic file primitives

Gate: path/idempotency/crash fixtures pass without FFmpeg.

Suggested commit: `feat(recording): define artifact store contracts`

### P5-2 FFmpeg color writer

- RGB24 to YUV420P H.264 MP4 writer
- exact profile/stride validation
- flush/trailer/resource error handling
- synthetic short-video decode validation

Gate: submitted count equals decoder-visible count for 1, 2 and short multi-frame cases.

Suggested commit: `feat(recording): add bounded H264 color writer`

### P5-3 sidecar and bounded manager

- append-only versioned `frames.jsonl`
- bounded non-blocking ingress and recording worker
- state machine/counters/generation policy

Gate: slow writer/overflow never blocks fake capture and gaps are explicit.

Suggested commit: `feat(recording): capture bounded frame artifacts`

### P5-4 idempotent HTTP lifecycle

- start/stop/current schemas and provider routes
- async finalization and exact replay
- health/OpenAPI error mapping

Gate: concurrent/retry/error matrix passes with one logical writer/finalizer.

Suggested commit: `feat(provider): expose recording lifecycle`

### P5-5 manifest and atomic finalize

- checksum/size/manifest validator
- tmp manifest and atomic directory activation
- startup staging detection

Gate: injected failures never create a visible partial finalized artifact.

Suggested commit: `feat(recording): finalize immutable artifacts`

### P5-6 Pilot catalog and MetaGate fixture

- recording descriptors/schema IDs
- full catalog replacement through existing client
- public HTTP-only MetaGate fixture and direct payload boundary proof

Gate: fixture discovers via Pilot metadata but sends recording requests directly to Vision.

Suggested commit: `feat(pilot): publish recording services`

### P5-7 acceptance and handoff

- full formatting/build/CTest/static checks
- repeated start/stop/finalize/shutdown tests
- README/progress/artifact contract update
- staged diff review and checkpoint commit

Gate: V5 acceptance passes, no hardware/product integration claim, clean worktree.

Suggested commit: `test(recording): complete Phase 5 acceptance`

각 checkpoint는 이전 gate 이후 진행한다. intentionally failing test와 half-finalized artifact를 commit하지
않고 push는 사용자 요청 전까지 하지 않는다.

## 20. validation commands

Terra implementation task는 build/test 실행을 명시적으로 승인한다.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
git diff --check
git status --short
```

artifact acceptance는 test binary 안의 libavformat/libavcodec decoder와 SHA-256 validator를 사용한다.
shell `ffprobe`, repository-relative recording output과 external Pilot process에 의존하지 않는다.

## 21. stop conditions과 Phase 6 handoff

다음이면 추측하지 않고 blocked evidence를 남긴다.

- host FFmpeg에 `libx264` encoder가 없음
- license/provenance를 확인할 수 없음
- atomic rename이 불가능한 cross-filesystem root 구성
- current CapturedFrame이 writer lifetime 동안 immutable RGB ownership을 제공하지 않음
- MetaGate handoff에 artifact identity field가 부족함
- Phase 4 remediation이 완료되지 않음

Phase 5 완료 후 멈춘다. camera optical/mount calibration과 spatial transform contract는 Phase 6 별도
설계다.
