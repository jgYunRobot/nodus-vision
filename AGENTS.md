# Agent Instructions

- Shared baseline: `docs/agent_docs/AGENTS.md`.
- Project documentation rules: `docs/rules.md`.
- Read and follow the shared baseline and project documentation rules before every coding task.
- The project-specific rules in `docs/rules.md` and below may narrow the shared baseline. If a rule
  conflicts, the project-specific rule takes precedence.

## Project-specific rules

- `nodus-vision` is the independent native ServiceProvider that owns physical camera adapters,
  frame capture, encoding, camera-local queries, streaming, and recording artifacts.
- Preserve the original PA-CONTROL sources during migration. Record every source revision and path
  in `migration/source_manifest.json` before copying behavior.
- Use C++17 for the native runtime and reusable libraries. Keep installed public headers under
  `include/nodus_vision/`, implementation under `src/`, the executable composition under `app/`,
  and tests under `tests/`.
- Keep camera vendors behind adapters. Provider contracts must use camera-neutral identifiers and
  schemas rather than exposing RealSense SDK types.
- Integrate with Pilot only through a pinned public HTTP contract under `schemas/pilot/`. Do not
  import Pilot internals, connect to Control IPC or UDS, or depend directly on `nodus-control`.
- Keep image, depth, point-cloud, and video payloads on Vision-owned direct endpoints. Pilot owns
  discovery metadata and sessions, not camera payload relay.
- Portal owns camera presentation and operator interaction. MetaGate/Gym own episode coordination,
  dataset commit, and durable recording lifecycle. Do not move those responsibilities into Vision.
- Run `./setup_dev.sh` to initialize pinned submodules.
- Treat `docs/agent_docs` as an independently versioned submodule. Do not modify its contents as
  part of this repository.
- Do not run a physical camera, access `/dev/bus/usb`, or claim hardware acceptance without an
  explicit hardware-execution request and recorded device evidence.
