# nodus-vision

`nodus-vision` is the independent native camera ServiceProvider for Nodus robots. It owns
physical camera adapters, frame capture, preview encoding, bounded spatial queries, point-cloud
payloads, and eventually camera recording artifacts.

Phase 0-3 are implemented. The current C++17 provider supports deterministic fake-camera operation,
hardware-independent Intel D435 adapter build validation, strict configuration, health/metadata,
JPEG snapshots, latest-only MJPEG preview, depth queries, and PCD1 v2 binary point clouds. Pilot
registration and recording are later phases.

## Target boundary

```text
physical camera -> nodus-vision -> direct payload consumers
                         |
                         +-> Pilot public HTTP lifecycle and endpoint catalog
```

- Pilot discovers Vision and its endpoints but does not relay image/depth/video payloads.
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
JPEG snapshots, ROI/pixel depth queries, and `/snapshot/pointcloud.bin`. Their public contract is
`schemas/vision/v1/openapi.yaml`; the binary point-cloud layout is documented in
`schemas/vision/v1/pointcloud_pcd1_v2.md`.

No physical D435 acceptance has been performed. Starting an Intel D435 config requires an explicit
hardware validation task and device-access approval.

See
`docs/designs/src_nodus_vision_camera_provider_migration_design.md` for the PA-CONTROL inventory,
target ownership, contracts, and phased migration plan.
