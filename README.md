# nodus-vision

`nodus-vision` is the independent native camera ServiceProvider for Nodus robots. It owns
physical camera adapters, frame capture, preview encoding, bounded spatial queries, point-cloud
payloads, and immutable camera recording artifacts.

Phase 0-5 are implemented. The current C++17 provider supports deterministic fake-camera operation,
hardware-independent Intel D435 adapter build validation, strict configuration, health/metadata,
JPEG snapshots, latest-only MJPEG preview, depth queries, PCD1 v2 binary point clouds, bounded
Pilot public lifecycle/catalog recovery, and direct RGB H.264 recording artifacts.

## Target boundary

```text
physical camera -> nodus-vision -> direct payload consumers
                         |
                         +-> Pilot public HTTP lifecycle and endpoint catalog
```

- Pilot discovers Vision and its endpoints but does not relay image/depth/video payloads.
- Vision consumes only the pinned Pilot OpenAPI 1.0.2 bytes in `schemas/pilot/v1/`; its worker owns
  registration, heartbeats/state updates, full catalog publication, and recovery independently of
  direct provider serving.
- Portal consumes preview, snapshot, query, and point-cloud endpoints directly after Pilot
  discovery.
- Operator/Policy resolves compatible observation endpoints through Pilot and reads Vision
  directly.
- MetaGate coordinates recording and artifact handoff; Gym owns dataset materialization and commit.
- Vision never connects to Control IPC or depends on `nodus-control`.

## Development

- CMake 3.28+
- C++17
- Ninja presets for Debug and Release
- pinned shared agent documentation under `docs/agent_docs`

Initialize the shared submodule:

```bash
./setup_dev.sh
```

Build and run the hardware-independent test suite:

```bash
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

Run with the deterministic fake camera:

```bash
./build/debug/app/nodus-vision assets/configs/examples/fake_camera.json
```

Direct provider endpoints include `/health`, `/metadata`, color/depth MJPEG streams, color/depth
JPEG snapshots, ROI/pixel depth queries, `/snapshot/pointcloud.bin`, and the direct recording
`POST /recordings/start`, `POST /recordings/stop`, and `GET /recordings/current` lifecycle. Recording
creates `color.mp4`, `frames.jsonl`, and checksum-backed `recording_manifest.json` under the
configured absolute artifact root; Vision does not perform episode or dataset commits. Their public contract is
`schemas/vision/v1/openapi.yaml`; the binary point-cloud layout is documented in
`schemas/vision/v1/pointcloud_pcd1_v2.md`.
The additive recording contract is published as Vision Provider API `1.2.0`.

The fake-camera default disables Pilot. Use `assets/configs/examples/fake_camera_pilot.json` to
enable the public HTTP lifecycle client against a local fake or deployed Pilot endpoint. The
integration has no TLS/authentication or physical-camera acceptance claim. Phase 6 geometry and
product integrations remain separate work.

No physical D435 acceptance has been performed. Starting an Intel D435 config requires an explicit
hardware validation task and device-access approval.

See
`docs/designs/src_nodus_vision_camera_provider_migration_design.md` for the PA-CONTROL inventory,
target ownership, contracts, and phased migration plan.
