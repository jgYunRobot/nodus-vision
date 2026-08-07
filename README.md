# nodus-vision

`nodus-vision` is the independent native camera ServiceProvider for Nodus robots. It will own
physical camera adapters, frame capture, preview encoding, bounded spatial queries, point-cloud
payloads, and camera recording artifacts.

The repository is currently at the foundation and migration-design stage. PA-CONTROL camera source
code has not been copied yet.

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

## Foundation

- CMake 3.28+
- C++17
- Ninja presets for Debug and Release
- pinned shared agent documentation under `docs/agent_docs`

Initialize the shared submodule:

```bash
./setup_dev.sh
```

The current CMake project intentionally has no runtime or library target. Those targets are added
checkpoint-by-checkpoint after their dependency and public-contract decisions are approved.

See
`docs/designs/src_nodus_vision_camera_provider_migration_design.md` for the PA-CONTROL inventory,
target ownership, contracts, and phased migration plan.
