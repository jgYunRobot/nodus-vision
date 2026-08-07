# Nodus Vision Phase 4 Pilot public integration 상세 설계

## 1. 문서 상태

- 대상 저장소: `nodus-vision`
- 대상 기준 revision: `dacc6a5` (`feat(provider): complete MJPEG backpressure`)
- 대상 migration checkpoint: V4 Pilot public integration
- 선행 완료 범위: Phase 0-3 camera core, provider HTTP data plane, MJPEG backpressure
- Pilot 공개 계약: OpenAPI `1.0.2`
- Pilot source revision: `46d35dea702e71ae78aa2bb932a11e1bf5e79a73`
- Pilot contract path: `schemas/pilot/v1/openapi.yaml`
- Pilot contract SHA-256: `4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8`
- 상태: 구현 기준 확정

현재 `nodus-pilot` HEAD에서 읽은 committed OpenAPI도 위 digest와 동일하다. Phase 4는 작업 중인
Pilot source나 test fixture를 runtime dependency로 사용하지 않고, 위 immutable revision의 공개
artifact만 소비한다.

## 2. 목표

Phase 4의 목표는 Vision provider가 자기 lifecycle과 직접 endpoint catalog를 Pilot에 게시하고,
Pilot 중단·재시작·session replacement 이후 자동으로 복구하도록 만드는 것이다.

완료 후 다음 경로가 성립해야 한다.

```text
                            lifecycle/catalog only
nodus-vision ------------------------------------------> nodus-pilot
     |
     +---- image/depth/query/point-cloud payload ------> direct consumer
```

핵심 결과는 다음과 같다.

1. Vision이 provider HTTP bind를 완료한 뒤 generic camera component로 등록한다.
2. Pilot이 반환한 heartbeat interval과 opaque session을 그대로 사용한다.
3. Vision이 현재 제공하는 endpoint만 full replacement catalog로 게시한다.
4. Pilot이 없어도 camera capture와 direct provider HTTP는 계속 동작한다.
5. Pilot이 복구되면 single worker가 재등록하고 전체 catalog를 다시 게시한다.
6. consumer는 Pilot에서 descriptor를 찾지만 payload는 Vision endpoint에서 직접 읽는다.

## 3. 범위와 제외 범위

### 3.1 포함

- Pilot OpenAPI artifact와 provenance pin
- strict Pilot client configuration
- bounded HTTP transport와 JSON codec
- component registration, heartbeat, coarse state update, disconnect
- endpoint descriptor 생성과 atomic full catalog publication
- Pilot loss, expiry, replacement, restart, uncertain response recovery
- Vision health에 redacted Pilot integration 상태 투영
- fake Pilot와 fake camera를 이용한 hardware-independent integration test
- direct mock consumer가 catalog discovery 후 Vision을 직접 읽는 acceptance

### 3.2 제외

- Pilot source import, Python module import 또는 Pilot 내부 fixture 복사
- Control IPC, UDS, robot operation 제출
- image, depth, point-cloud, MJPEG 또는 video payload의 Pilot relay
- FFmpeg recording과 recording descriptor
- calibration/geometry Phase 6 확장
- Portal, Operator, MetaGate 또는 Gym 저장소 변경
- TLS, authentication, internet-facing trust 또는 privacy hardening 주장
- physical D435 실행과 `/dev/bus/usb` 접근
- Pilot process spawn, stop, restart 또는 supervisor 구현

Phase 4 구현 중 Pilot contract에 필요한 route나 field가 없다면 Vision에서 추측하지 않는다. 해당
작업을 중단하고 Pilot 공개 계약 변경을 별도 issue/design으로 돌린다.

## 4. 계약 pin과 provenance

P4-0에서 다음 두 파일을 함께 추가한다.

```text
schemas/pilot/v1/openapi.yaml
schemas/pilot/v1/provenance.json
```

`openapi.yaml`은 source revision의 bytes를 그대로 복사한다. 정렬, formatting, 주석 추가를 포함해
내용을 수정하지 않는다. `provenance.json`은 최소 다음 값을 가진다.

```json
{
  "schema_version": 1,
  "source_repository": "https://github.com/jgYunRobot/nodus-pilot",
  "source_revision": "46d35dea702e71ae78aa2bb932a11e1bf5e79a73",
  "source_path": "schemas/pilot/v1/openapi.yaml",
  "semantic_version": "1.0.2",
  "sha256": "4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8"
}
```

contract verification은 다음을 확인한다.

- artifact SHA-256이 provenance와 일치한다.
- OpenAPI version은 `3.1.0`, API version은 `1.0.2`다.
- register, heartbeat, state, disconnect, endpoint-catalog route가 존재한다.
- implementation 또는 test가 `/home/.../nodus-pilot` 경로를 참조하지 않는다.

새 Pilot contract를 소비할 때 기존 artifact만 조용히 수정하지 않는다. 새 revision, semantic version,
digest와 변경 영향을 같은 reviewed change로 갱신한다.

## 5. target architecture

```text
VisionApplication
  +-- CameraAdapter / FrameStore / Encoder worker       existing Phase 1-3
  +-- ProviderHttpServer                                existing Phase 2-3
  +-- PilotIntegrationClient                            new Phase 4 owner
        +-- PilotHttpTransport                          bounded HTTP/JSON
        +-- PilotContractCodec                          public DTO validation
        +-- VisionEndpointCatalogBuilder                deterministic descriptors
        +-- one lifecycle worker                        session/sequence/recovery
```

책임은 다음처럼 분리한다.

| unit | responsibility | prohibited responsibility |
|---|---|---|
| `PilotHttpTransport` | URL parsing, connect/write/read deadline, body bound, cancellation | lifecycle state 판단 |
| `PilotContractCodec` | request serialization, success/error response validation | network retry |
| `VisionEndpointCatalogBuilder` | current config에서 deterministic full catalog 생성 | Pilot request 전송 |
| `PilotIntegrationClient` | session, lifecycle sequence, publication identity, recovery | camera capture/payload serving |
| `VisionApplication` | local provider와 Pilot client의 ordered composition | session field 직접 조작 |

재사용 library는 logging-neutral로 유지한다. 오류와 상태는 explicit result와 snapshot으로 application에
전달한다. per-frame path는 Pilot client를 호출하지 않는다.

## 6. strict configuration

기존 config에 required `pilot` object를 추가한다. 기존 fake example에는 `enabled: false`를 명시하고,
Pilot integration example을 별도 추가한다.

```json
{
  "pilot": {
    "enabled": true,
    "base_url": "http://127.0.0.1:8765",
    "clock_domain": "monotonic_same_host",
    "connect_timeout_ms": 500,
    "request_timeout_ms": 1000,
    "max_response_bytes": 65536,
    "retry_initial_delay_ms": 100,
    "retry_max_delay_ms": 5000,
    "shutdown_timeout_ms": 1500
  }
}
```

검증 규칙은 다음과 같다.

- 모든 duration과 byte bound는 양수다.
- `retry_initial_delay_ms <= retry_max_delay_ms`다.
- Phase 4 transport는 `http`만 허용한다. `https`는 TLS dependency와 trust 정책을 설계한 뒤 추가한다.
- `base_url`은 absolute URL이고 query, fragment, userinfo를 허용하지 않는다.
- Pilot enabled 상태에서 provider port는 0이 아니어야 한다.
- `provider.advertised_base_url`은 absolute `http` URL이며 wildcard host를 허용하지 않는다.
- advertised port는 provider bind port와 일치해야 한다.
- heartbeat interval과 lease timeout은 config에 넣지 않는다. Pilot response가 소유한다.
- session ID, catalog generation과 publication ID는 config나 disk에 저장하지 않는다.

`pilot.enabled: false`는 local provider 개발 모드다. 이 모드에서는 Pilot worker와 network request를
생성하지 않으며 health는 명시적으로 `disabled`를 표시한다.

## 7. component identity와 registration

registration request는 다음 의미를 사용한다.

| field | value/owner |
|---|---|
| `component_id` | existing `VisionConfig.component_id`, camera logical identity |
| `instance_id` | process start마다 생성하는 `vision-<128-bit lowercase hex>` |
| `component_type` | `camera` |
| `protocol_version` | `1` |
| `supported_schema_versions` | `[1]` |
| `capabilities` | 현재 publish할 descriptor capability의 stable sorted unique list |
| `service_endpoints` | `{}`; deprecated registration endpoint map을 사용하지 않음 |
| `initial_state` | 현재 coarse Vision health projection |
| `started_at` | process monotonic start nanoseconds |
| `metadata` | device/calibration/provider API identity의 bounded scalar values |
| `clock_domain` | strict config의 `monotonic_same_host` |

instance ID 생성기는 test에서 deterministic value를 주입할 수 있어야 한다. production 기본은
`std::random_device`로 채운 128-bit 값을 lowercase hex로 표현하며 session ID와 결합하지 않는다.

registration response에서 다음 field를 모두 strict하게 확인한다.

- non-empty opaque `session_id`
- non-empty `server_instance_id`
- `accepted_protocol_version == 1`
- `accepted_schema_versions`에 `1` 존재
- positive `heartbeat_interval_ms`, `lease_timeout_ms`
- `heartbeat_interval_ms < lease_timeout_ms`
- non-negative `server_time`

session ID는 URL path segment로 percent-encode하고 log, `/health`, metadata 또는 exception text에
노출하지 않는다.

## 8. endpoint catalog

### 8.1 descriptor set

Phase 4는 Phase 3에서 실제 구현한 endpoint만 게시한다. recording, lifecycle control, JSON point-cloud
descriptor는 게시하지 않는다.

| descriptor_id | capability | kind | method/media type | schema_id |
|---|---|---|---|---|
| `health` | `camera.health.get` | service | GET `application/json` | `nodus.vision.health.response.v1` |
| `metadata` | `camera.metadata.get` | service | GET `application/json` | `nodus.vision.metadata.response.v1` |
| `color-preview` | `camera.stream.color.preview` | stream | `multipart/x-mixed-replace` | `nodus.vision.mjpeg.color_part.v1` |
| `depth-preview` | `camera.stream.depth.preview` | stream | `multipart/x-mixed-replace` | `nodus.vision.mjpeg.depth_part.v1` |
| `color-snapshot` | `camera.snapshot.color` | service | GET `image/jpeg` | `nodus.vision.jpeg.color.v1` |
| `depth-snapshot` | `camera.snapshot.depth.preview` | service | GET `image/jpeg` | `nodus.vision.jpeg.depth_preview.v1` |
| `roi-depth` | `camera.query.roi_depth` | service | POST `application/json` | `nodus.vision.roi_depth.response.v1` |
| `pixel-to-point` | `camera.query.pixel_to_point` | service | POST `application/json` | `nodus.vision.pixel_point.response.v1` |
| `pointcloud-binary` | `camera.snapshot.pointcloud` | service | GET `application/octet-stream` | `nodus.vision.pointcloud.pcd1.v2` |

모든 descriptor의 `contract_version`은 Vision Provider API compatibility major인 `1`이다. service
descriptor의 `request_schema_id`와 `response_schema_id`는 다음처럼 채운다.

- GET/JPEG/binary: request는 `null`, response는 descriptor `schema_id`
- ROI: `nodus.vision.roi_depth.request.v1` / `nodus.vision.roi_depth.response.v1`
- pixel: `nodus.vision.pixel_point.request.v1` / `nodus.vision.pixel_point.response.v1`

stream descriptor는 `clock_domain: monotonic_same_host`와 같은 capture source를 뜻하는
`stream_group_id: <component_id>.capture`를 사용한다. metadata는 `device_id`, `sensor_frame`,
`calibration_id`, `api_version`처럼 bounded scalar만 포함한다.

color가 config에서 비활성화된 adapter/profile이면 color preview와 color snapshot descriptor를 모두
제외한다. degraded/no-frame 상태는 endpoint contract 제거 사유가 아니므로 descriptor를 유지하고
실제 request가 기존 503 health semantics를 반환하게 한다.

각 schema ID는 Vision OpenAPI의 해당 operation/schema에 vendor extension 또는 adjacent mapping으로
기록해 catalog와 contract가 따로 drift하지 않게 한다.

### 8.2 endpoint construction

- provider HTTP bind 성공 후에만 catalog를 만든다.
- endpoint는 normalized `advertised_base_url`과 fixed relative path를 결합한다.
- runtime request나 metadata에서 받은 path를 그대로 붙이지 않는다.
- descriptor는 `descriptor_id` 기준 stable sort한다.
- capabilities도 stable sort하고 descriptor set과 exact equality를 유지한다.
- publication JSON은 같은 logical attempt에서 byte-equivalent해야 한다.

### 8.3 publication identity와 generation

새 session의 첫 publication은 `expected_catalog_generation: 0`이다. 성공 응답의
`catalog_generation`만 다음 replacement의 expected generation으로 사용하며 숫자를 추측하지 않는다.

```text
register new session
  -> catalog_generation local = 0
  -> build immutable publication request
  -> retry uncertain/503 response with same publication_id and same bytes
  -> accept response generation N
  -> later catalog replacement uses expected N and a new publication_id
```

새 session에서는 old session의 publication ID와 generation을 모두 폐기한다. `publication_id`는
`catalog-<instance_id>-<session-local-counter>` 형식으로 만들고 process memory에만 둔다.

## 9. lifecycle state machine

```text
DISABLED

STARTING
  -> REGISTERING
       -> PUBLISHING
            -> ONLINE
                 -> ONLINE                 heartbeat/state accepted
                 -> RECOVERING             transport/session/Pilot restart
       -> RECOVERING                       retryable register error
       -> CONTRACT_FAULT                   non-retryable contract/config error

RECOVERING
  -> REGISTERING                           capped backoff elapsed

any active state
  -> STOPPING
       -> STOPPED                          bounded disconnect attempt completed
```

한 lifecycle worker만 session, lifecycle sequence, catalog generation과 publication identity를
수정한다. heartbeat와 state update가 서로 다른 thread에서 sequence를 증가시키지 않는다.

### 9.1 heartbeat와 state update

- 첫 lifecycle write sequence는 `1`이다.
- heartbeat와 state update는 하나의 strictly increasing sequence를 공유한다.
- scheduling은 registration response의 heartbeat interval을 steady clock 기준으로 사용한다.
- state update는 provider의 coarse state가 바뀔 때 latest pending 하나로 coalesce한다.
- capture frame 번호, frame age의 작은 변화마다 state update를 보내지 않는다.
- network timeout 후 같은 lifecycle write를 그대로 replay하지 않는다. 수락 여부가 불확실하므로
  session을 폐기하고 재등록한다.
- heartbeat/state가 오래 block되어 lease를 넘기지 않도록 request deadline은 lease보다 작아야 한다.

Vision `ProviderState`는 Pilot `CommonState.health`로 다음처럼 투영한다.

| Vision state | Pilot health |
|---|---|
| starting | `starting` |
| ready | `ready` |
| degraded | `degraded` |
| faulted | `faulted` |
| stopping/stopped | `stopping` |

`reason`은 bounded stable reason code/text만 사용하고, `details`에는 payload나 session secret을 넣지
않는다.

### 9.2 error classification

| condition | action |
|---|---|
| connection refused/reset/DNS/timeout | local session 폐기, capped backoff 후 register |
| malformed/oversized/non-JSON response | protocol error 기록, capped backoff 후 register |
| register 503 | capped backoff retry |
| unknown session 404 | session 폐기 후 register |
| `session_expired` 409 | session 폐기 후 register |
| `stale_sequence` 409 | uncertain session으로 간주하고 register |
| catalog network timeout/reset | 같은 publication ID와 bytes로 idempotent retry |
| catalog 503 | 같은 publication request로 capped backoff retry |
| `stale_catalog_generation` 409 | session을 교체해 generation 0부터 full republish |
| `idempotency_conflict` 409 | internal contract fault; automatic payload mutation 금지 |
| 400/413/415 | non-retryable configuration/contract fault |
| graceful disconnect unknown session | 이미 제거된 것으로 간주하고 종료 |

retry delay는 `min(retry_max_delay_ms, retry_initial_delay_ms * 2^attempt)`로 cap한다. retry loop의
시도 횟수는 process lifetime 동안 계속될 수 있지만 각 network operation, memory, response body,
delay와 shutdown은 bounded여야 한다.

## 10. HTTP transport

Phase 4 transport는 existing Boost.Asio/Beast dependency를 사용하는 C++17 static library로 만든다.

- one lifecycle worker에서 synchronous request/response를 수행한다.
- request마다 connection을 닫아 Pilot restart 후 stale keep-alive state를 남기지 않는다.
- resolver/connect/write/read에 deadline을 적용한다.
- response parser에 `max_response_bytes` body limit를 적용한다.
- redirect, compression, chunked unbounded accumulation을 자동 허용하지 않는다.
- success와 error 모두 `application/json`을 확인한다.
- stop은 resolver/socket을 cancel해 `shutdown_timeout_ms` 안에 worker join을 끝낸다.
- response JSON number를 platform `long` 크기에 의존해 parse하지 않는다.
- session path segment는 percent encoding 후 사용한다.

transport는 HTTP status, bounded body, content type과 transport error만 반환한다. retry와 session 판단은
`PilotIntegrationClient`가 담당한다.

## 11. application composition과 shutdown

startup 순서는 다음과 같다.

```text
parse strict config
  -> construct adapter/provider/Pilot client
  -> bind and start provider HTTP
  -> start camera/capture/encoder or establish degraded state
  -> start Pilot lifecycle worker without waiting for Pilot availability
  -> return local application started
```

Pilot worker는 provider의 actual bound/listening 상태가 확정되기 전에 registration/catalog를 보내지
않는다. Pilot 연결 실패는 `VisionApplication::startApplication()` 실패가 아니다.

shutdown 순서는 다음과 같다.

```text
mark stopping and stop new Pilot state requests
  -> best-effort bounded state/disconnect while provider endpoint is still alive
  -> stop/cancel Pilot worker
  -> stop provider accept and stream sessions
  -> join capture/encoder
  -> stop/disconnect camera adapter
  -> clear stores and publish stopped snapshot
```

disconnect가 실패하거나 Pilot이 없더라도 shutdown은 진행한다. application mutex를 잡은 채 network
request나 worker join을 하지 않는다.

## 12. health와 diagnostics

Vision `/health`의 `pilot` object는 최소 다음을 제공한다.

```json
{
  "enabled": true,
  "state": "online",
  "server_instance_id": "pilot-instance",
  "catalog_generation": 1,
  "descriptor_count": 9,
  "retry_count": 0,
  "last_success_age_ms": 120,
  "last_error": null
}
```

다음 값은 노출하지 않는다.

- session ID
- complete registration/catalog request body
- internal publication retry bytes
- credential 또는 URL userinfo

Pilot 상태는 camera/provider overall state를 강제로 faulted로 바꾸지 않는다. local provider가
정상이고 Pilot만 끊겼으면 overall provider는 direct service 가능 상태를 유지하고 `pilot.state`만
`recovering`으로 표시한다.

## 13. 예상 파일 구조와 allowlist

```text
schemas/pilot/v1/
  openapi.yaml
  provenance.json
src/pilot/
  CMakeLists.txt
  pilot_contract_codec.hpp/.cpp
  pilot_http_transport.hpp/.cpp
  pilot_integration_client.hpp/.cpp
  vision_endpoint_catalog.hpp/.cpp
tests/pilot/
  CMakeLists.txt
  test_pilot_contract_codec.cpp
  test_pilot_http_transport.cpp
  test_pilot_integration_client.cpp
  test_vision_endpoint_catalog.cpp
tests/integration/
  test_pilot_recovery.cpp
```

Phase 4 허용 경로:

- `schemas/pilot/v1`
- `src/pilot`과 관련 CMake
- `src/config`, config schema/examples와 config tests
- `src/runtime`, `include/nodus_vision/provider_health.hpp`
- `tests/contracts`, `tests/pilot`, `tests/integration`
- 이 design, migration ledger, README, `docs/progress.md`

원칙적으로 `src/provider/http`의 request/stream implementation, camera adapter, encoder, PCD1과
recording 경로는 수정하지 않는다. Phase 4 integration 때문에 불가피한 기존 seam 결함이 발견되면
해당 근거와 최소 수정 범위를 먼저 progress에 기록한다.

## 14. 테스트와 acceptance matrix

모든 기본 테스트는 fake camera와 local fake Pilot로 동작하며 외부 service와 hardware를 요구하지
않는다.

| scenario | expected result |
|---|---|
| Pilot disabled | network request 없이 local provider 동작 |
| Pilot absent at startup | start 성공, local endpoint 200, bounded retry |
| Pilot starts later | register 후 full catalog publication |
| exact registration | component type/capabilities/service_endpoints/state가 contract와 일치 |
| dynamic color disabled | color descriptor 두 개만 제외 |
| catalog discovery | descriptor가 stable sort되고 endpoint가 advertised URL과 일치 |
| direct consumer | Pilot directory에서 URL 선택 후 Vision을 직접 요청 |
| payload bypass | fake Pilot가 image/query/point-cloud body를 받지 않음 |
| heartbeat | server interval 사용, sequence strictly increases |
| state and heartbeat race | single worker sequence라 duplicate/stale 없음 |
| uncertain catalog response | same publication ID/body retry, duplicate generation 없음 |
| session replacement/expiry | old identity 폐기, new registration, generation 0 full republish |
| Pilot restart | new server instance/session/catalog, provider port/capture generation 유지 |
| malformed/oversized response | bounded rejection, local provider unaffected |
| Pilot stalls during stop | shutdown timeout 안에 worker 종료 |
| restart VisionApplication | 이전 session/generation/retry state 재사용 없음 |
| secret redaction | health/error/test output에 session ID 없음 |

integration test의 fake Pilot는 공개 HTTP shape만 구현한다. `nodus_pilot` import, local source path,
private class나 registry 직접 호출을 금지한다.

## 15. 구현 checkpoint

### P4-0 contract pin과 baseline

- clean baseline과 Phase 3 test 결과 확인
- Pilot artifact/provenance import와 digest verification test
- migration ledger의 consumed contract 상태 갱신

완료 gate: artifact bytes와 provenance가 exact하고 source checkout dependency가 없다.

권장 커밋: `chore(contract): pin Pilot OpenAPI 1.0.2`

### P4-1 config, DTO와 deterministic catalog

- strict Pilot config/schema/examples
- public request/response DTO와 codec
- instance ID seam과 endpoint catalog builder
- schema ID mapping 및 pure unit tests

완료 gate: network 없이 registration/catalog JSON과 descriptor set을 검증한다.

권장 커밋: `feat(pilot): define lifecycle contracts and catalog`

### P4-2 bounded HTTP transport

- HTTP URL parser, request/response bounds와 cancellation
- fake server success/error/malformed/timeout coverage
- no retry/session policy in transport

완료 gate: every network stage가 deadline/cancel/body bound를 가진다.

권장 커밋: `feat(pilot): add bounded public HTTP transport`

### P4-3 session lifecycle

- registration, response validation, heartbeat/state sequence, disconnect
- redacted health snapshot
- disabled/absent Pilot behavior

완료 gate: single worker ownership과 bounded stop이 deterministic test로 확인된다.

권장 커밋: `feat(pilot): manage component lifecycle`

### P4-4 catalog publication

- bind 후 full catalog publish
- generation/publication ID ownership과 idempotent retry
- catalog replacement/error classification tests

완료 gate: exact advertised endpoint가 게시되고 payload는 Pilot를 통과하지 않는다.

권장 커밋: `feat(pilot): publish Vision endpoint catalog`

### P4-5 recovery와 application composition

- Pilot absent/start-later/restart/replacement/expiry recovery
- VisionApplication startup/shutdown/restart integration
- local provider continuity와 health projection

완료 gate: Pilot lifecycle 변화가 capture generation, provider port 또는 client serving을 재시작하지
않는다.

권장 커밋: `feat(runtime): recover Pilot integration independently`

### P4-6 acceptance와 handoff

- full format/build/lint/static/test validation
- repeated recovery/shutdown test
- README와 progress 결과 기록
- physical hardware와 product integration 미검증 사실 기록

완료 gate: V4 acceptance matrix가 통과하고 working tree/staged diff가 검토됐다.

권장 커밋: `test(pilot): complete Phase 4 acceptance`

각 checkpoint는 이전 gate가 통과한 뒤 진행한다. intentionally failing test를 commit하지 않고, 다른
checkpoint 책임을 한 커밋에 섞지 않는다. push는 사용자 요청 전까지 하지 않는다.

## 16. 검증 명령

Terra 구현 task는 build/test 실행을 명시적으로 승인하는 것으로 간주한다.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
git diff --check
git status --short
```

추가 정적 확인:

```bash
sha256sum schemas/pilot/v1/openapi.yaml
rg -n "/home/.*/nodus-pilot|from nodus_pilot|import nodus_pilot" .
rg -n "session_id" src include app
```

마지막 검색은 session 사용 자체를 금지하는 것이 아니라 log, health, exception에 opaque value가
노출되지 않는지 staged diff에서 확인하기 위한 것이다.

## 17. 중단 조건과 Phase 5 handoff

다음 조건에서는 임의 구현을 하지 않고 blocked evidence를 남긴다.

- pinned OpenAPI digest가 source revision과 다름
- required lifecycle/catalog field가 공개 artifact에 없음
- Pilot endpoint capacity가 Phase 4 descriptor set보다 작고 배포 설정을 확인할 수 없음
- HTTPS/credential이 실제 요구되지만 public trust/auth contract가 없음
- existing provider continuity를 깨야만 Pilot recovery를 구현할 수 있음

Phase 4 완료 후에는 멈춘다. FFmpeg recording, sidecar, staging/finalized artifact와 MetaGate handoff
fixture는 Phase 5 상세 설계 및 별도 승인 범위다.
