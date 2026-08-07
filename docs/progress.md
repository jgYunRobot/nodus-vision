# Progress

## 2026-08-08 - Phase 5 P5-7 recording acceptance and handoff

### Changes

- Preserved full accepted start/stop request bytes for idempotency evidence, persisted the stop
  request before finalization, and fail closed when a worker fault prevents finalization.
- Completed finalized manifest metadata with component/process/device identity, profile/video timing,
  counters, frame bounds, request IDs, and SHA-256 artifact digests. Current and health responses
  now expose only low-rate recording status and a relative finalized artifact reference.
- Hardened durable file writes against short writes and verified direct fake-camera HTTP lifecycle
  artifacts, including non-empty finalized recording evidence.

### Status

- Phase 5 is complete. Vision retains only camera-local artifact ownership; Pilot remains
  discovery-only, and MetaGate/Gym episode or dataset commit work remains external. No physical
  camera or product integration acceptance was performed.

### Validation

- `cmake --build --preset debug` and complete `ctest --test-dir build/debug --output-on-failure`
  passed (23/23).
- `recording_test_manager` passed 20 consecutive runs after its non-blocking ingress fixture was
  made contention-safe. `git diff --check` passed.

## 2026-08-08 - Phase 5 P5-6 recording catalog gate

### Changes

- Added enabled-only, stable-sorted Pilot discovery descriptors for direct Vision recording start,
  stop, and current HTTP endpoints.

### Status

- P5-6 is complete. Pilot remains discovery-only; recording request bodies and artifact bytes are
  never relayed through Pilot. P5-7 acceptance remains.

### Validation

- `pilot_test_vision_endpoint_catalog` passed with the 12-descriptor recording-enabled catalog.

## 2026-08-08 - Phase 5 P5-5 immutable artifact finalize gate

### Changes

- Added bounded linked-libavutil SHA-256 digesting for regular, non-symlink artifact files.
- Finalization now closes video and sidecar, records their size/checksum in an atomically persisted
  manifest, then atomically renames the complete staging identity into `finalized/` and fsyncs both parents.

### Status

- P5-5 is complete. P5-6 remains: Pilot catalog descriptor publication and the HTTP-only MetaGate fixture.

### Validation

- Recording manifest/store/manager CTest selection passed (3/3), including the known `abc` SHA-256 fixture
  and finalized artifact layout.

## 2026-08-08 - Phase 5 P5-4 recording HTTP lifecycle gate

### Changes

- Added direct provider `POST /recordings/start`, `POST /recordings/stop`, and `GET /recordings/current`
  callbacks without exposing artifact bytes or adding a Pilot payload path.
- Start strictly validates device, calibration, RGB24 profile, and idempotency identity before opening a
  staging artifact. Stop and start replay exact request IDs; conflicting reuse fails closed.
- Enabled recording owns a bounded manager in the Vision application and capture only attempts its
  non-blocking frame admission; shutdown drains/finalizes the manager before camera teardown.

### Status

- P5-4 is complete. P5-5 is next: checksum-backed manifest validation and atomic staging-to-finalized
  activation. No Pilot catalog changes, MetaGate fixture, physical camera, or payload relay was added.

### Validation

- `integration_test_application_lifecycle` passed with a 64x64 fake camera and temporary absolute
  artifact root, covering direct HTTP start 201, start replay 200, stop 202, stop replay 200, and shutdown.

## 2026-08-08 - Phase 5 P5-3 sidecar and bounded manager gate

### Changes

- Added a compact, versioned append-only `frames.jsonl` writer with contiguous video indexes,
  sequential PTS, strict `(capture_generation, frame_number)` ordering, immutable capture timestamps,
  and a required trailing newline.
- Added a worker-owned recording manager with a preallocated bounded ring. Capture admission uses
  `try_lock`, so mutex contention and full capacity drop the current frame without waiting; both are
  reflected in the recording drop counter.
- The worker retains only immutable `CapturedFrame` owners, writes color input before its one matching
  sidecar entry, drains on finalize, and faults instead of mixing a changed capture generation.

### Status

- P5-3 is complete. P5-4 is next: idempotent HTTP start/stop/current lifecycle over this manager.
  Artifacts remain staging-only; manifest generation and atomic final activation remain P5-5 work.

### Validation

- Recording CTest selection passed (5/5). `recording_test_manager` also passed 10 consecutive runs,
  covering staging drain, disabled admission, generation fault, and non-blocking overflow drops.
- `clang-format-18` (host version 18.1.3) formatted the changed C++ files and `git diff --check`
  passed. No physical camera, external Pilot process, or payload relay was used.

## 2026-08-08 - Phase 5 P5-2 FFmpeg color writer gate

### Changes

- Added the worker-owned `ColorVideoWriter`: packed RGB24 input is converted to YUV420P and
  encoded into an MP4/H.264 stream through the runtime-resolved `libx264` encoder.
- Fixed the stream profile to the configured dimensions, FPS time base, one-second GOP, zero
  B-frames, strict bitrate, `veryfast` preset, and `zerolatency` tune. Submitted frame indexes are
  sequential PTS values with an explicit one-frame duration.
- Added CMake linkage for only `libavformat`, `libavcodec`, `libavutil`, and `libswscale`; no direct
  `x264.pc` dependency was added. Constructor, finalization, flush, trailer, and cleanup paths fail
  closed on FFmpeg errors.
- Added linked-FFmpeg synthetic MP4 validation for 1, 2, and 5 submitted frames, including H.264,
  YUV420P, dimensions, packet presence, and decoder-visible frame count checks.

### Status

- P5-2 is complete. P5-3 remains the next gate: versioned sidecar lines and the bounded recording
  manager. No recording HTTP lifecycle, final artifact activation, Pilot descriptor, MetaGate
  fixture, physical camera, or external Pilot process was added.

### Validation

- `cmake --build build/debug --target recording_test_contracts recording_test_store
  recording_test_color_video_writer -j2` and the matching CTest selection passed (3/3).
- `clang-format-18` (host version 18.1.3) formatted changed C++ files and `git diff --check`
  passed. The decoder check uses the linked FFmpeg libraries; no shell `ffprobe` or physical camera
  was used.

## 2026-08-08 - Phase 5 P5-0 FFmpeg provenance gate

### Changes

- Recorded the host FFmpeg 6.1.1 component versions, GPL-enabled `libx264` encoder availability,
  package provenance, CMake linkage plan, and libavutil SHA-256 API decision in the migration
  ledger.

### Status

- P5-0 is complete: R4 is closed and the host can provide the required encoder through FFmpeg.
- No recording source, FFmpeg CMake linkage, writer, provider route, artifact directory, or
  filesystem output was added at this checkpoint.

### Validation

- `pkg-config` resolved `libavformat`, `libavcodec`, `libavutil`, and `libswscale`; FFmpeg listed
  both `libx264` and `libx264rgb`. The direct `x264.pc` module is intentionally absent and is not
  a P5 build requirement.

## 2026-08-08 - Phase 4 R4 remediation acceptance

### Changes

- Closed the R4-A through R4-D findings: health/OpenAPI parity, atomic worker shutdown, bounded
  replacement recovery, explicit latest-state ownership, accepted-response identity validation, and
  success-age reset/bounds.
- Added the failure matrix for persistent recoverable catalog loss, malformed accepted responses,
  unknown and permanent conflicts, failed degraded-state publication, lifecycle identity mismatch,
  absent Pilot, and stop/start reset.

### Status

- The Phase 4 R4 gate is closed. FFmpeg provenance investigation may now begin in the separately
  designed Phase 5 P5-0 checkpoint.
- Recording implementation, FFmpeg build linkage, provider recording routes, filesystem artifacts,
  Pilot recording descriptors, and all later phases remain unimplemented.

### Validation

- `cmake --build --preset debug` and complete `ctest --test-dir build/debug --output-on-failure`
  passed (18/18).
- `pilot_test_integration_client` and `integration_test_application_lifecycle` each passed 25
  consecutive runs (50 repeated loopback recovery/shutdown executions total).
- `clang-format-18` (host version 18.1.3) formatted changed C++ files and `git diff --check`
  passed. No physical camera, Pilot process/source, or payload relay was accessed.

## 2026-08-08 - Phase 4 R4-A health contract parity

### Changes

- Promoted the additive Vision Provider API version from `1.0.0` to `1.1.0`; the stable `v1`
  schema IDs remain unchanged.
- Declared the existing redacted Pilot snapshot as the exact `/health` OpenAPI field set, including
  nullable unavailable values and bounds for generation, descriptor, retry, and age values.
- Aligned runtime metadata, Vision catalog descriptor metadata, and Pilot registration metadata with
  `1.1.0`.

### Status

- R4-A is complete. R4-B through R4-E remain required before FFmpeg or recording implementation.
- No Pilot source import, payload relay, recording dependency, filesystem artifact, or hardware path
  was added.

### Validation

- Targeted contract, catalog, fake-Pilot integration, and application lifecycle CTest selection
  passed (4/4), including the serialized disabled-Pilot `/health` fixture and OpenAPI contract check.
- `clang-format-18` (host version 18.1.3) formatted the changed C++ files and `git diff --check`
  passed.

## 2026-08-08 - Phase 4 implementation review and Phase 5 recording design

### Changes

- Reviewed the complete `dacc6a5..cb86cdd` Phase 4 commit range and recorded six actionable
  findings covering public health contract drift, a lifecycle stop-flag data race, unbounded
  replacement recovery, inaccurate success age, stale state recovery, and incomplete response
  validation.
- Added an R4 remediation gate that must close the Phase 4 findings before adding recording threads
  or FFmpeg dependencies.
- Added the Phase 5 recording-artifact detailed design with FFmpeg/libx264 provenance, bounded
  capture ingress, idempotent HTTP lifecycle, append-only frame mapping, immutable staging/finalized
  storage, checksummed manifests, atomic activation, and MetaGate handoff fixtures.

### Status

- Phase 4 structure and reported 17/17 tests are preserved, but completion is conditionally reopened
  until the three P1 and three P2 review findings are remediated.
- Phase 5 is implementation-ready only after the R4 gate. Recording implementation, FFmpeg build
  integration, filesystem artifacts, provider routes, and Pilot descriptor changes were not added.
- Phase 6 geometry, Phase 7 product integration, and Phase 8 physical hardware acceptance remain
  excluded.

### Validation

- Confirmed a clean `main...origin/main` worktree before documentation changes and reviewed all 37
  files changed by the seven Phase 4 checkpoints.
- `git diff --check dacc6a5..HEAD` passed; static search confirmed no Pilot source import or payload
  relay was introduced.
- Re-read the pinned Pilot lifecycle/catalog contract and the PA-CONTROL recording writer,
  append-only `frames.jsonl`, manifest, and recording HTTP behavior at migration revision
  `1c44efbe0b03fa77187305d0f50948f731e972f0`.
- Build and CTest were not rerun for this review/design-only task. The implementation report remains
  17/17 plus 50 repeated lifecycle/application runs; no hardware or external process was executed.

### Next goals

- Give Terra one task that closes R4-A through R4-E, verifies the unchanged Phase 4 baseline, and
  only then executes P5-0 through P5-7 in order.
- Stop after Phase 5 acceptance and request separate Phase 6 authorization.

## 2026-08-08 - Phase 4 P4-6 acceptance and handoff

### Changes

- Completed the Phase 4-only handoff documentation: README now identifies the immutable Pilot
  OpenAPI 1.0.2 input, direct-payload boundary, enabled fake configuration, and excluded Phase 5+
  scopes.
- Fixed the fake-Pilot test server teardown exposed by repeated acceptance. It now has an explicit
  stop flag so an accept wakeup cannot race into another blocking accept.

### Status

- Phase 4 Pilot public integration is complete: Vision registers, publishes only direct endpoint
  metadata, maintains lifecycle, and recovers Pilot sessions without owning/relaying camera data.
- Physical D435, TLS/authentication, recording, geometry, and Portal/Operator/MetaGate/Gym product
  integration remain unvalidated and out of scope.

### Validation

- `clang-format-18` completed on every Phase 4 C++ header/source/test touched by this task.
- `cmake --preset debug`, `cmake --build --preset debug`, and
  `ctest --test-dir build/debug --output-on-failure` passed with fake camera/local fake Pilot only.
- `pilot_test_integration_client` and `integration_test_application_lifecycle` each passed 25
  consecutive executions (50 repeated runs total), covering lifecycle retry/shutdown and Pilot
  restart/provider-continuity teardown.
- Contract pin, public-path, session redaction, staged diff, and working-tree checks are recorded
  at the final checkpoint. No physical camera or Pilot process/source was accessed.

### Next goals

- Stop after Phase 4. Design Phase 5 recording separately before adding any recording lifecycle or
  artifact behavior.

## 2026-08-08 - Phase 4 P4-5 application composition and recovery

### Changes

- Composed `PilotIntegrationClient` into `VisionApplication` only after the local provider is
  bound and camera start/degraded state is known; worker startup does not wait for Pilot.
- Ordered shutdown to stop the Pilot worker before accepting/stream shutdown and camera teardown.
  `/health` now projects only redacted Pilot integration state.
- Added application-level fake-camera/fake-Pilot coverage for disabled and absent Pilot, direct
  provider continuity, Pilot restart recovery, new server identity, stable provider port, and
  unchanged camera capture generation.

### Status

- P4-5 is complete. The application does not spawn or import Pilot; the test server implements
  only the pinned public HTTP lifecycle/catalog shape.

### Validation

- `clang-format-18` formatted changed C++ files.
- The fake-Pilot restart integration test passed: absent/restarted Pilot changed only the Pilot
  snapshot and did not restart direct provider serving or capture generation.
- No physical camera, Pilot process/source, payload relay, recording, or geometry work was used.

### Next goals

- Run P4-6 acceptance: repeated recovery/shutdown, full format/build/CTest/static checks, staged
  review, README/progress handoff, and final checkpoint commit.

## 2026-08-08 - Phase 4 P4-4 endpoint catalog publication

### Changes

- Added registration-gated full replacement catalog publication. Each new session begins with
  expected generation `0`, a session-local deterministic publication ID, and the stable direct
  Vision descriptor set.
- Added strict accepted-response validation for component identity, positive catalog/session
  generations, revision, and exact descriptor count. Redacted health now exposes only the accepted
  catalog generation/count.
- Catalog transport failure or 503 retries the exact same immutable publication JSON and ID;
  stale catalog generation discards the session for fresh registration, while idempotency conflict
  and configuration errors become explicit contract faults without mutating the payload.
- Extended fake-Pilot tests to observe only lifecycle/catalog metadata and verify two byte-equal
  catalog retries, descriptor count, and catalog snapshot projection.

### Status

- P4-4 is complete. Pilot is still not composed into `VisionApplication`; absent/start-later/
  restart recovery and provider continuity remain P4-5 responsibilities.

### Validation

- `clang-format-18` formatted changed C++ files.
- `cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` passed
  17/17 with local fake Pilot and fake camera only.
- No Pilot source/process, camera payload relay, physical camera, recording, or geometry work was
  used.

### Next goals

- Implement P4-5 `VisionApplication` composition and fake-Pilot recovery/restart acceptance while
  preserving direct provider continuity.

## 2026-08-08 - Phase 4 P4-3 component lifecycle client

### Changes

- Added a single-worker lifecycle client that owns the opaque session, registration retry state,
  heartbeat/state sequence, and bounded best-effort disconnect. State changes coalesce to one
  pending coarse provider state and share the same increasing sequence with heartbeats.
- Added strict public serializers for heartbeat, state update, and disconnect; session IDs are
  percent-encoded only for the HTTP path and never copied to the public health snapshot.
- Added redacted Pilot integration health values: enabled/state, server instance ID, catalog
  generation/count placeholders, retry count, last-success age, and stable error code only.
- Added fake-Pilot lifecycle tests for no-network disabled mode, absent-Pilot recovery and bounded
  stop, registration, encoded disconnect, and strictly increasing lifecycle writes.

### Status

- P4-3 is complete. Catalog publication is deliberately not issued yet; its publication identity,
  generation, idempotency behavior, and tests are P4-4 responsibilities.

### Validation

- `clang-format-18` formatted the changed C++ files.
- `cmake --build --preset debug` and the local fake-Pilot lifecycle test passed. The test confirms
  that `opaque/session secret` is encoded for requests and excluded from the redacted snapshot.
- No Pilot source/process, physical camera, recording, geometry extension, or payload relay was
  used.

### Next goals

- Implement P4-4 full endpoint catalog publication, session-local generation/publication identity,
  and idempotent publication retry behavior.

## 2026-08-08 - Phase 4 P4-2 bounded public HTTP transport

### Changes

- Added a lifecycle-neutral C++17 Boost.Asio/Beast transport for one-shot public HTTP requests.
  It parses only absolute HTTP base URLs, closes every connection, bounds response bodies, and
  accepts only `application/json` responses.
- Applied bounded resolution, connect, write, and read behavior; an external stop cancels the
  active resolver/socket and releases the waiting lifecycle caller without retrying or deciding
  session state.
- Added a local fake-Pilot HTTP test for JSON success, non-JSON rejection, oversized bodies,
  deadline expiry, and explicit cancellation. The fake server handles lifecycle-shaped HTTP only;
  it receives no Vision image, query, point-cloud, or stream payload.

### Status

- P4-2 is complete. Retry classification, registration/session ownership, and catalog policy are
  intentionally deferred to P4-3 and P4-4.

### Validation

- `clang-format-18` formatted the new C++ transport and test files.
- `cmake --build --preset debug` passed and `pilot_test_http_transport` passed against a local
  fake Pilot server.
- No Pilot source import/process, physical camera, recording, geometry extension, or payload relay
  was used.

### Next goals

- Implement P4-3 single-worker registration, heartbeat/state sequencing, bounded disconnect, and
  redacted lifecycle health snapshots.

## 2026-08-08 - Phase 4 P4-1 configuration, DTO, and catalog

### Changes

- Added the required strict `pilot` configuration, disabled and enabled fake-camera examples, and
  schema parity for HTTP-only Pilot URLs, bounded timeouts/retries, and `monotonic_same_host`.
- Added a private C++17 public-contract codec for deterministic generic camera registration,
  catalog publication JSON, strict registration-response validation, and injectable process
  instance IDs.
- Added a deterministic full catalog builder that contains exactly the active direct Vision
  endpoints, uses stable sorted descriptor/capability sets, and removes only the two color
  descriptors when color is disabled.
- Recorded the Vision OpenAPI-to-catalog schema ID mapping. No transport, lifecycle request, Pilot
  worker, payload relay, or Pilot source dependency was added.

### Status

- P4-1 is complete. P4-2 will add the bounded HTTP transport; session ownership and catalog
  publication remain inactive until their later checkpoints.

### Validation

- `cmake --preset debug`, `cmake --build --preset debug`, and
  `ctest --test-dir build/debug --output-on-failure` passed 15/15 with fake camera and local fake
  HTTP coverage only.
- Codec tests validate exact generic registration fields, capability ordering, invalid registration
  responses, instance identity injection, catalog stable sorting, direct advertised URLs, and
  color-disabled omission.
- No Pilot process/source import, physical camera, recording, geometry extension, or payload relay
  was used.

### Next goals

- Implement P4-2 bounded public HTTP transport with fake-server success, malformed, timeout, and
  cancellation coverage.

## 2026-08-08 - Phase 4 P4-0 Pilot public contract pin

### Changes

- Imported the immutable Pilot OpenAPI 1.0.2 bytes under `schemas/pilot/v1/` with its exact
  revision, source path, semantic version, and SHA-256 provenance.
- Added a contract-level verifier for the pinned digest, OpenAPI/API versions, and the required
  public registration, heartbeat, state, disconnect, and endpoint-catalog routes.
- Updated the migration ledger to mark the public artifact as the consumed Phase 4 input. No Pilot
  source, package, fixture, or runtime path was added.

### Status

- P4-0 is complete. The Phase 4 design and progress changes that preceded implementation are
  preserved in this checkpoint; strict configuration, DTOs, catalog construction, and all runtime
  integration remain pending P4-1 onward.
- The pinned artifact digest is
  `4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8`.

### Validation

- `cmake --preset debug` and `cmake --build --preset debug` passed.
- `ctest --test-dir build/debug --output-on-failure` passed 13/13 with normal local execution.
  The sandbox-only attempt failed only where it prohibited local fake-server socket setup and
  librealsense udev monitor initialization; the elevated rerun verified the unchanged baseline.
- The new `contract_test_pilot_openapi_pin` passed and the artifact SHA-256 matched provenance.
- No Pilot process, Pilot source import, camera hardware, `/dev/bus/usb`, recording, or payload
  relay was used.

### Next goals

- Implement P4-1 strict Pilot configuration, public DTO codec, deterministic catalog builder, and
  network-free unit coverage.

## 2026-08-08 - Phase 4 Pilot public integration detailed design

### Changes

- Added a Phase 4-only detailed design for consuming the pinned Pilot OpenAPI 1.0.2 contract through
  a C++17 bounded HTTP client without importing Pilot source or relaying camera payloads.
- Defined strict Pilot configuration, component/session identity, deterministic Vision endpoint
  descriptors, catalog generation/idempotency rules, heartbeat/state sequencing, recovery error
  classification, startup/shutdown ordering, redacted health, and hardware-independent acceptance.
- Split implementation into P4-0 through P4-6 contract, config/codec, transport, lifecycle, catalog,
  recovery, and acceptance checkpoints with path allowlists, stop gates, and Conventional Commits.

### Status

- Phase 4 is specified for implementation by a separate agent. No Pilot client, schema artifact,
  runtime integration, test target, dependency, or provider behavior was implemented by this task.
- Pilot OpenAPI revision `46d35dea702e71ae78aa2bb932a11e1bf5e79a73` remains the selected immutable
  source; its observed SHA-256 still matches the current committed Pilot OpenAPI bytes.
- Phase 5 recording, Phase 6 geometry, Phase 7 product integrations, and Phase 8 physical hardware
  acceptance remain excluded.

### Validation

- Reviewed the repository and shared agent rules, the Phase 0-3 handoff, migration ledger, current
  Vision runtime/config/provider surface, and the committed Pilot lifecycle/catalog OpenAPI.
- Verified the recorded Pilot OpenAPI SHA-256 as
  `4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8` at both the
  selected source revision and current committed Pilot HEAD.
- `git diff --check` is the required documentation validation; build and tests are not required for
  this design-only task.
- No source implementation, external service, Pilot process, camera device, or hardware path was
  executed.

### Next goals

- Give Terra the Phase 4 implementation prompt and execute P4-0 through P4-6 in order, committing
  only after each checkpoint gate passes.
- Stop after Phase 4 acceptance and design Phase 5 recording separately.

## 2026-08-08 - Phase 3 completion review and MJPEG backpressure

### Changes

- Replaced the one-response-only HTTP session with a bounded registry that owns cancellable
  long-lived color/depth MJPEG sessions, enforces an independent stream-client limit, collapses
  overlapping notifications to one latest pending frame, and closes blocked writers during
  bounded server shutdown.
- Split JPEG encoding from the capture thread. Capture now publishes immutable latest frames while
  one encoder worker skips superseded frames, updates generation-aware color/depth caches, and
  notifies stream sessions without waiting for client socket writes.
- Fixed application shutdown lock ordering, server restart, fresh-frame enforcement, query response
  header propagation, full sensor/calibration identity headers, strict coordinate validation, and
  runtime metadata/health endpoint and stream counts.
- Hardened libjpeg fatal-error handling and input stride validation, and replaced host-endian PCD1
  serialization with explicit little-endian encoding plus malformed/bounds/finite-value checks.
- Replaced the incomplete nested-object configuration schema with the parser-matching Draft
  2020-12 contract, added the Vision OpenAPI 1.0.0 and PCD1 v2 binary contract, and updated the
  migration designs and README for the completed Phase 3 provider surface.
- Added regression coverage for MJPEG part identity, stream admission, slow-writer cancellation,
  server/application restart, fresh latest stores, direct runtime snapshots/queries, query JSON,
  JPEG validation, and exact PCD1 bytes.

### Status

- Phase 3 is complete with hardware-independent fake-camera evidence. Direct health, metadata,
  color/depth JPEG snapshots, color/depth MJPEG streams, ROI/pixel queries, and PCD1 v2 snapshots
  are implemented with finite connection/client/payload bounds and latest-only backpressure.
- Review fixes remain limited to the Phase 0-3 provider, contracts, tests, and documentation.
  Pilot integration, recording, Portal/Operator/MetaGate/Control changes, and physical camera
  acceptance were not added.
- Phase 4 remains a separate authorization and must begin by revalidating and pinning the released
  Pilot OpenAPI artifact.

### Validation

- `clang-format-18` completed on every changed C++ source/header/test file.
- `cmake --preset debug` and `cmake --build --preset debug` passed.
- Final `ctest --test-dir build/debug --output-on-failure` passed 12/12 hardware-independent tests.
- The first final CTest run exposed one new test-only Boost.JSON numeric accessor mismatch; the test
  was corrected to validate numeric meaning and the complete suite then passed.
- `provider_test_http_server` and `integration_test_application_lifecycle` each passed 100 repeated
  executions without an intermittent failure.
- Draft 2020-12 schema validation accepted `assets/configs/examples/fake_camera.json`; the OpenAPI
  document parsed as 3.1.0 with nine paths and no unresolved local references.
- No physical D435, `/dev/bus/usb`, Pilot, recording, Portal, Operator, MetaGate, or Control process
  was accessed.

### Next goals

- Stop at the Phase 3 gate and review the staged completion diff and checkpoint commit.
- Start Phase 4 only after separate authorization, then import the immutable Pilot public contract
  with provenance before implementing component registration, heartbeat, and endpoint catalog
  recovery.

## 2026-08-08 - Phase 3 direct ROI and pixel query checkpoint

### Changes

- Added bounded POST route callbacks for `/query/roi_depth` and `/query/pixel_to_point`.
- Enforced strict expected-field count, typed numeric extraction, invalid-request 400 responses, and
  no-frame 503 responses before querying one immutable `FrameStore` owner.
- Added query result JSON serialization with frame identity headers so response bodies and headers
  refer to the same captured frame.

### Status

- Direct query endpoint implementation is in place. MJPEG streaming lifecycle and slow-client
  backpressure remain before the overall Phase 3 completion gate.

### Validation

- `cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` passed
  11/11 hardware-independent tests. No camera hardware or external component was accessed.

## 2026-08-08 - Phase 3 direct depth snapshot checkpoint

### Changes

- Added `GET /snapshot/depth` as a direct Vision-owned JPEG endpoint backed by the depth latest-only
  cache, with no-frame 503 semantics and complete capture identity headers.
- Added the active depth snapshot endpoint to runtime metadata.

### Status

- Color/depth JPEG and binary point-cloud snapshots are now direct provider routes. MJPEG, strict
  query POST routes, and slow-client backpressure remain in-progress Phase 3 responsibilities.

### Validation

- `cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` passed
  11/11 hardware-independent tests. No physical camera access occurred.

## 2026-08-08 - Phase 3 deterministic depth preview checkpoint

### Changes

- Added deterministic fake depth visualization bytes with the same immutable frame owner and identity
  as the depth frame.
- Wired depth preview JPEG encoding into the capture-loop latest-only depth cache alongside color.

### Status

- Depth preview production is available to the cache boundary. The direct depth snapshot/MJPEG route
  and streaming/backpressure lifecycle remain in progress.

### Validation

- `cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` passed
  11/11 hardware-independent tests. No D435 device access was attempted.

## 2026-08-07 - Phase 3 immutable query semantics checkpoint

### Changes

- Added deterministic fake-adapter coverage for invalid pixel result identity, explicit invalid-depth
  reason, and clamped ROI geometry on the same immutable captured frame.

### Status

- Query value semantics are fixed before adding the direct POST route adapter. Phase 3 remains in
  progress; route parsing, depth preview/MJPEG, and slow-client acceptance are not complete.

### Validation

- `cmake --build --preset debug` and `ctest --test-dir build/debug --output-on-failure` passed
  11/11 hardware-independent tests.
- No physical camera or external component access was attempted.

## 2026-08-07 - Phase 3 JPEG and binary snapshot checkpoint

### Changes

- Added the exact pinned libjpeg-turbo source and an ExternalProject build integration after verifying
  upstream rejects direct `add_subdirectory()` use.
- Added RGB JPEG encoding, stream-kind latest-only encoded cache, and immutable frame capture-loop
  publication. Older/duplicate identities cannot replace a newer cached preview.
- Added direct `GET /snapshot/color` and `GET /snapshot/pointcloud.bin` routes with no-frame 503
  error responses, no-store cache control, complete capture identity headers, and runtime metadata.
- Added PCD1 v2 little-endian writer/reader with exact length, magic/version, and truncation checks.

### Status

- This is an in-progress Phase 3 checkpoint. JPEG color snapshot and binary point-cloud snapshot are
  directly served from Vision-owned data; MJPEG, depth preview, strict ROI/pixel POST queries, and
  slow-client backpressure acceptance remain before the Phase 3 gate.

### Validation

- `cmake --build --preset debug` passed with the pinned libjpeg-turbo ExternalProject.
- `ctest --test-dir build/debug --output-on-failure` passed 11/11 hardware-independent tests.
- No physical D435 or USB access was attempted. No Pilot, recording, Portal, Operator, MetaGate, or
  Control integration was added.

## 2026-08-07 - Phase 2 strict provider configuration and lifecycle

### Changes

- Added strict typed JSON parsing and the canonical Phase 2 config schema/example. Root and parsed
  nested objects reject unknown fields; IDs, stream bounds, calibration matrix, bind/advertise
  separation, provider limits, and Intel serial selection are validated before startup.
- Added a bounded single-worker Boost.Asio/Beast listener for `GET /health` and `GET /metadata`.
  It enforces request header/body/deadline and active-connection bounds without per-request thread
  creation, and responses use `Cache-Control: no-store` plus the common JSON error shape.
- Added `VisionApplication` ordered composition: parse-config caller, construct adapter/store, bind
  provider, start camera or retain a degraded server, then stop acceptor before capture and adapter
  teardown. The executable converts SIGINT/SIGTERM through `boost::asio::signal_set`.
- Recorded the exact host Boost dependency version and BSL-1.0 license in the migration ledger.

### Status

- Phase 2 is complete with a fake camera: `/health` and `/metadata` are served without Pilot,
  recording, Control, or physical D435 access. A camera start failure leaves the provider in an
  explicit degraded state rather than failing the bound HTTP process.

### Validation

- `cmake --preset debug` and `cmake --build --preset debug` passed.
- `ctest --test-dir build/debug --output-on-failure` passed 8/8 tests, including strict config
  rejection, direct health/metadata HTTP responses, fake provider ready startup, and idempotent
  application shutdown.
- Public-header vendor scan and PA-CONTROL path search remain clean. `git diff --check` is run at
  the phase staging gate.
- `clang-format` 18.1.8 remains unavailable on this host; this required command cannot be executed
  until the host tool is installed. No test or compiler warning policy was removed.
- No physical camera, `/dev/bus/usb`, Pilot, recording, Portal, Operator, MetaGate, or Control path
  was executed or changed.

### Next goals

- Implement Phase 3 latest JPEG cache, direct snapshot/MJPEG/query endpoints, and PCD1 v2 binary
  point-cloud data plane with fake-adapter backpressure acceptance.

## 2026-08-07 - Phase 1 camera-neutral core and adapters

### Changes

- Added vendor-neutral public frame, camera adapter, bounded enum serialization, health, query, and
  point-cloud contracts with Korean Doxygen and include guards.
- Added the immutable latest-only `FrameStore`; duplicate and regressing frame identities do not
  replace the current owner, and readers retain a valid frame owner after replacement.
- Added a deterministic caller-driven fake RGBD adapter with reproducible identity, RGB/depth
  pattern, depth ROI/deprojection, and bounded point cloud behavior.
- Added a private Intel D435 adapter target that owns librealsense lifecycle and frame objects while
  keeping all SDK types out of installed public headers. Device selection fails closed unless it
  resolves exactly one device.
- Pinned Vision-owned librealsense and GoogleTest submodules and recorded their exact revisions and
  licenses, including librealsense's exact JSON configuration dependency.

### Status

- Phase 1 is complete with fake-adapter acceptance and no physical camera execution.
- The D435 target is compile/link validated only. It has not opened a device, captured a frame, or
  claimed hardware acceptance.

### Validation

- `cmake --preset debug` and `cmake --build --preset debug` passed with the pinned dependencies.
- `ctest --test-dir build/debug --output-on-failure` passed 5/5 hardware-independent tests:
  public contract and vendor identifier scan, latest-frame ordering/lifetime/concurrency, fake RGBD
  geometry/point cloud/manual advance, and D435 config/construction without device open.
- `clang-format` 18.1.8 was not available on the host, so formatting command execution is pending
  host tool installation; CMake's build and static tests were not weakened.
- No physical camera, `/dev/bus/usb`, Pilot, Control, recording, or PA-CONTROL runtime dependency
  was accessed.

### Next goals

- Implement Phase 2 strict configuration, ordered degraded lifecycle, and bounded health/metadata
  server after reviewing this Phase 1 staged diff and commit.

## 2026-08-07 - Phase 0 foundation and provenance verification

### Changes

- Verified the repository foundation, pinned `docs/agent_docs` gitlink, CMake 3.28/C++17 presets,
  migration manifest, ledger, and PA-CONTROL camera migration ownership before adding runtime code.
- Confirmed PA-CONTROL revision `1c44efbe0b03fa77187305d0f50948f731e972f0` matches the recorded
  read-only migration baseline and that the selected camera paths are clean at that revision.
- Kept the manifest's dependency provenance structure as the required record for every Phase 1-3
  dependency: source repository, exact revision or version, license, and consuming target.

### Status

- Phase 0 is complete. The repository remains independent of PA-CONTROL at build and runtime, and
  no source or dependency implementation has been copied from that checkout.
- The foundation is ready for Phase 1 camera-neutral contracts, latest-frame storage, fake adapter,
  and hardware-independent Intel D435 target work.

### Validation

- `git submodule status --recursive` reported the expected `cfba3fbe...` `docs/agent_docs` pin.
- `git -C /home/jgy/workspace/ai_work/pa_control rev-parse HEAD` returned the manifest revision
  `1c44efbe0b03fa77187305d0f50948f731e972f0`.
- Static search found no PA-CONTROL checkout include or import path outside the recorded migration
  documentation.
- `cmake --preset debug`, `cmake --build --preset debug`, and
  `ctest --test-dir build/debug --output-on-failure` passed; the empty foundation defines zero tests.
- No camera device or `/dev/bus/usb` access was attempted.

### Next goals

- Implement and test the Phase 1 camera-neutral core and adapters, then repeat the phase gate before
  beginning strict provider configuration.

## 2026-08-07 - Phase 0-3 detailed implementation design

### Changes

- Added a detailed implementation design that maps the migration design V0-V3 to executable
  Phase 0-3 checkpoints.
- Defined the camera-neutral contracts, latest-only frame lifetime, fake and Intel D435 adapter
  boundaries, strict configuration, bounded Boost.Asio/Beast provider lifecycle, health/metadata
  contracts, and direct preview/query/point-cloud data plane.
- Added per-phase file allowlists, dependency provenance rules, test matrices, commit checkpoints,
  stop gates, and explicit exclusions for Pilot, recording, product integration, and hardware
  acceptance.

### Status

- Phase 0-3 are specified in sufficient detail for implementation without inventing Pilot,
  recording, Portal, Operator, MetaGate, or Control behavior.
- The repository remains at design/foundation state; no runtime source, dependency, schema, build
  target, or hardware behavior was implemented by this documentation task.
- No commit or push was performed.

### Validation

- The new design filename follows the flat `docs/designs` snake-case convention.
- Design headings, referenced repository paths, and the Phase 0-3/V0-V3 mapping were reviewed.
- `git diff --check` passed after the documentation update.
- Build, tests, dependency installation, Pilot startup, camera access, and hardware validation were
  not run because this task changed documentation only.

### Next goals

- Execute Phase 0 verification and implement Phase 1 camera-neutral contracts, latest frame store,
  deterministic fake adapter, and hardware-independent Intel D435 adapter.
- Continue to Phase 2 and Phase 3 only after each preceding validation and commit gate passes.
- Stop after Phase 3 and request separate authorization for Phase 4 Pilot integration.

## 2026-08-07 - Repository foundation and camera-provider migration design

### Changes

- Added the root `AGENTS.md`, project documentation rules, README, C++17/CMake 3.28 foundation,
  Debug/Release presets, clang-format policy, ignore rules, and executable development setup script.
- Added `docs/agent_docs` as a pinned submodule at
  `cfba3fbe8a5fb4b20e6d97a1e5596f1718889612` using its official GitHub URL.
- Recorded PA-CONTROL revision `1c44efbe0b03fa77187305d0f50948f731e972f0`, selected clean
  source paths, librealsense/nodus_rm observations, and the Pilot OpenAPI 1.0.2 contract baseline in
  the migration manifest and ledger.
- Analyzed the native Intel D435 module, camera runtime, Pilot camera manager/config, PA-CPU camera
  routing/MetaGate reference path, Portal predecessor UI, point-cloud rendering, FFmpeg recording,
  and Gym artifact validation.
- Added the detailed target design for a one-camera-per-process native ServiceProvider, direct
  provider payload plane, Pilot lifecycle/catalog client, camera-neutral adapter boundary, bounded
  capture/streaming, timestamp/calibration semantics, recording handoff, and V0-V8 migration plan.

### Status

- V0 repository foundation and migration design are complete in the working tree.
- No PA-CONTROL source, camera driver, FFmpeg writer, Pilot contract artifact, or runtime dependency
  has been copied yet. The root CMake project intentionally defines no build target.
- `nodus-vision` is defined as the native Camera ServiceProvider; Portal owns UI, Operator owns
  Policy observations, MetaGate/Gym own recording coordination and durable dataset commit, and
  Pilot owns generic discovery without payload relay.
- No commit or push was performed.

### Validation

- `git submodule status --recursive` reports the intended `cfba3fbe...` agent-docs pin.
- `git diff --check` passed.
- `bash -n setup_dev.sh` passed.
- `CMakePresets.json` and `migration/source_manifest.json` parsed as JSON.
- `cmake --list-presets` reported the `debug` and `release` presets.
- The design directory is flat and contains the expected snake-case design filename.
- Build, tests, dependency installation, Pilot startup, camera access, and hardware validation were
  not run because they were not requested and the foundation contains no implementation target.

### Next goals

- Implement V1 camera-neutral contracts and the Intel D435 adapter from the recorded PA-CONTROL
  baseline without importing the source checkout at runtime.
- Implement V2 strict config, ordered application lifecycle, and bounded provider health/metadata
  server before exposing preview or hardware behavior.
- Add the pinned Pilot artifact and component/catalog integration only at V4 after the provider data
  plane has a stable public contract.
