# Nodus Vision Phase 4 implementation review

## 1. 리뷰 기준

- 대상 범위: `dacc6a5..cb86cdd`
- 대상 checkpoint: P4-0부터 P4-6까지 7개 commit
- 대상 계약: Pilot OpenAPI `1.0.2`, Vision Provider OpenAPI `1.0.0`
- 검토 방식: commit/file diff, lifecycle ownership, public schema, fake-Pilot tests, shutdown/recovery
  path 정적 추적
- 실행하지 않은 항목: build, CTest, physical camera, external Pilot process

사용자가 보고한 build와 CTest 17/17, recovery/shutdown 각 25회 결과는 completion evidence로
존중한다. 이번 review에서는 repository 규칙상 build/test를 다시 실행하지 않고, 해당 테스트가 놓친
contract와 concurrency 문제를 code path 기준으로 검토했다.

## 2. 결론

Phase 4의 기본 구조는 적절하다.

- pinned public artifact만 소비한다.
- Pilot client가 camera payload path와 분리돼 있다.
- endpoint descriptor는 deterministic full catalog다.
- Pilot absent/restart에서 provider와 capture를 유지한다.
- session secret은 public health에서 제외된다.

그러나 Phase 4 완료 판정을 확정하기 전에 고쳐야 할 P1 finding 3건과 P2 finding 3건이 있다.
특히 public `/health` contract drift와 lifecycle worker data race는 Phase 5 recording thread를 추가하기
전에 해결해야 한다.

## 3. findings

### R4-1 [P1] `/health` runtime response가 자기 OpenAPI를 위반한다

근거:

- `src/runtime/vision_application.cpp:144-161`은 모든 health response에 `pilot` object를 추가한다.
- `schemas/vision/v1/openapi.yaml:332-375`의 `HealthResponse`는 `additionalProperties: false`이며
  `pilot` property를 정의하거나 required 목록에 넣지 않았다.

영향:

- strict consumer와 generated validator는 정상 `/health` 응답을 invalid response로 거부한다.
- catalog의 `nodus.vision.health.response.v1` schema identity가 실제 payload와 일치하지 않는다.
- Phase 4가 public provider contract를 확장했지만 API version은 계속 `1.0.0`으로 광고된다.

필수 수정:

- Pilot health schema를 exact runtime shape와 enum/nullability/bound에 맞춰 추가한다.
- additive public contract 변경에 맞는 Provider API version을 결정하고 metadata, registration metadata,
  endpoint catalog와 OpenAPI를 한 번에 갱신한다.
- serialized health fixture를 OpenAPI schema로 검증하는 regression test를 추가한다.

### R4-2 [P1] `m_stop_requested` 접근에 C++ data race가 있다

근거:

- `src/pilot/pilot_integration_client.cpp:307`은 stop flag를 plain `bool`로 소유한다.
- worker는 `:109`, `:112`, `:169`, `:184`, `:255`, `:277`에서 mutex 없이 읽는다.
- public stop path는 `:357`에서 mutex 아래 값을 쓴다.

영향:

- C++ memory model상 undefined behavior이며 반복 CTest 성공으로 안전성을 증명할 수 없다.
- shutdown, request cancellation 또는 recovery loop 종료가 compiler/architecture에 따라 관측되지 않을
  수 있다.

필수 수정:

- stop flag를 `std::atomic<bool>`로 만들고 모든 read/write에 일관된 atomic access를 사용하거나, 모든
  접근을 같은 mutex 아래로 옮긴다.
- condition-variable predicate와 request 완료 직후 stop check도 같은 ownership 규칙을 따른다.
- active request cancellation과 repeated start/stop test를 유지하고 가능하면 sanitizer build에서
  lifecycle test를 추가 검증한다.

### R4-3 [P1] catalog session replacement가 backoff 없이 무한 재등록될 수 있다

근거:

- `src/pilot/pilot_integration_client.cpp:127-148`은 invalid response, stale generation, 404와 대부분의
  named 409를 `e_REPLACE_SESSION`으로 반환한다.
- `:207-210`은 session을 비운 뒤 delay 없이 registration loop의 처음으로 이동한다.
- pinned Pilot는 `duplicate_descriptor_id`, `undeclared_endpoint_capability` 같은 permanent 409도
  반환할 수 있는데 현재 구현은 이를 session replacement로 분류한다.

영향:

- persistent malformed response나 permanent catalog contract error가 register/replace storm을 만든다.
- 같은 component의 `session_generation`과 Pilot log가 빠르게 증가하고 다른 consumer의 discovery가
  불안정해질 수 있다.

필수 수정:

- recoverable session loss와 permanent contract error code를 closed table로 분류한다.
- 모든 fresh registration transition은 capped backoff를 거치며, immediate retry는 한 번의 명확한
  bounded handoff에만 허용한다.
- unknown error는 tight loop가 아니라 recovering/backoff 또는 contract fault로 fail closed한다.
- persistent 404/409/malformed catalog fake-Pilot tests에서 request rate가 bound되는지 검증한다.

### R4-4 [P2] `last_success_age_ms`가 성공하지 않은 시점에도 갱신된다

근거:

- `src/pilot/pilot_integration_client.cpp:85-92`의 `setSnapshot()`은 error string이 비어 있으면 상태와
  무관하게 `m_last_success`를 현재 시각으로 바꾼다.
- registration attempt의 `REGISTERING`과 shutdown의 `STOPPING/STOPPED`도 이 helper를 호출한다.

영향:

- Pilot에 한 번도 연결되지 않았거나 disconnect가 실패한 경우에도 health가 최근 성공이 있었던 것처럼
  표시된다.
- retry loop가 반복될수록 last-success age가 작게 유지돼 운영 진단을 오도한다.

필수 수정:

- accepted registration/catalog/heartbeat/state처럼 실제 성공 response에서만 success timestamp를
  갱신한다.
- start/restart 전에 timestamp를 clear하고, 장기 실행 overflow가 없도록 age type/bound를 명시한다.
- never-connected, recovering, stopped snapshot regression test를 추가한다.

### R4-5 [P2] provider의 최신 coarse state가 재등록과 restart에 보존되지 않는다

근거:

- `updateProviderState()`는 `src/pilot/pilot_integration_client.cpp:341-347`에서 pending state만 바꾸고
  `m_current_state`는 갱신하지 않는다.
- registration은 `:175`에서 최초 start 시 저장한 `m_current_state`를 계속 사용한다.
- failed state write는 `:265-266`에서 pending value를 이미 제거하므로 재등록 initial state에서도
  손실된다.
- VisionApplication은 initial start 외에 `updateProviderState()`를 호출하지 않으며 stopping state도
  전송하지 않는다.

영향:

- degraded/stopping transition 뒤 session replacement가 발생하면 새 registration이 오래된 `ready`
  state를 게시할 수 있다.
- client object restart 시 pending state와 previous success timestamp가 명시적으로 reset되지 않는다.

필수 수정:

- mutex-owned latest state와 separately coalesced pending write를 둔다.
- state request 실패 후에도 latest state는 재등록 initial state로 사용한다.
- start/restart에서 pending/session/catalog/retry/success state를 명시적으로 reset한다.
- application state transition과 stopping을 client seam에 연결하되 per-frame update는 금지한다.

### R4-6 [P2] lifecycle/catalog success response validation이 불완전하다

근거:

- `runSession()`은 HTTP 200이면 `LifecycleAcceptedResponse` body를 parse하지 않고 online으로 처리한다.
- catalog accepted response의 `server_instance_id`를 registration response와 비교하지 않는다.

영향:

- malformed 200 response나 restart/misroute identity mismatch를 정상 lifecycle 성공으로 오인할 수 있다.
- strict pinned-contract consumption이라는 Phase 4 목표보다 validation이 약하다.

필수 수정:

- heartbeat/state의 `status: accepted`와 required response envelope를 bounded strict parser로 확인한다.
- catalog response server instance가 active registration과 같은지 검증한다.
- mismatch/malformed response는 redacted protocol error와 capped recovery로 보낸다.

## 4. 테스트 공백

현재 test suite는 정상 registration/catalog, 503 catalog replay, absent Pilot, restart와 shutdown을 잘
검증한다. 다음 failure matrix가 빠져 있다.

- runtime health JSON 대 Vision OpenAPI schema conformance
- stop flag concurrency sanitizer 또는 equivalent ownership proof
- persistent catalog 404, stale generation, unknown/permanent 409 request-rate bound
- never-connected `last_success_age_ms == null`
- degraded state write 실패 후 re-registration initial state
- client stop/start에서 pending state와 success timestamp reset
- malformed lifecycle 200과 catalog server-instance mismatch

## 5. remediation checkpoint

Phase 5 implementation 전 `R4` checkpoint를 먼저 수행한다.

1. R4-A: health OpenAPI/schema/version parity
2. R4-B: stop flag와 restart state ownership 수정
3. R4-C: closed error classification과 all-registration backoff
4. R4-D: success timestamp/latest state/lifecycle response validation
5. R4-E: failure matrix, repeated recovery/shutdown, docs/progress

권장 commit은 defect별로 분리한다.

```text
fix(contract): align Pilot health response schema
fix(pilot): make lifecycle shutdown race-free
fix(pilot): bound session replacement recovery
fix(pilot): preserve validated lifecycle state
test(pilot): cover Phase 4 remediation matrix
```

Phase 4 remediation 완료 전에는 FFmpeg dependency나 recording worker를 같은 commit에 넣지 않는다.

## 6. review validation

- `git status -sb`에서 `main...origin/main`, clean worktree를 확인했다.
- `dacc6a5..cb86cdd`의 37개 변경 파일과 7개 checkpoint를 검토했다.
- `git diff --check dacc6a5..HEAD`는 통과했다.
- pinned Pilot artifact/provenance digest와 no-source-import boundary는 유지됐다.
- build/CTest는 repository 정책에 따라 다시 실행하지 않았으며 사용자 보고 결과는 17/17이다.
