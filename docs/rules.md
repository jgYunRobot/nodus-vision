# Project Documentation Rules

## Design documents

- Keep every project design document directly under `docs/designs/` in a flat layout. Do not create
  subdirectories below `docs/designs/`.
- Name design documents `<path>_<design_name>_design.md`. Convert the repository-relative owner path
  and design name to lowercase `snake_case`, and replace path separators with underscores.
- Update the applicable design before changing a public provider contract, camera lifecycle,
  timestamp/frame semantics, recording artifact, dependency, or repository boundary.

## Contract and migration provenance

- Store immutable consumed public contracts under `schemas/<provider>/<major>/` with a sibling
  `provenance.json` containing the source repository, revision, path, semantic version, and SHA-256
  digest.
- Do not edit a pinned provider artifact in place. Import a new artifact and update provenance in
  the same reviewed change.
- Keep migration sources in `migration/source_manifest.json`. Record repository-relative paths and
  exact revisions; never add PA-CONTROL as a runtime include or import path.
- Preserve source repositories as read-only migration references.

## Provider and deployment boundary

- The initial target is a private same-host or trusted-LAN research deployment. HTTP endpoints may
  be used for bring-up, but do not claim production authentication, TLS, remote trust, or camera
  privacy hardening until those controls are implemented and accepted.
- A bind address and an advertised client endpoint are separate configuration values. Never publish
  `0.0.0.0` as a consumer endpoint.
- Camera payloads bypass Pilot. Pilot stores only component lifecycle, health, and endpoint catalog
  metadata.
- Hardware validation must name the camera model/serial, stream profile, host access boundary, and
  exact command. A synthetic or disconnected self-test is not hardware acceptance.

## Progress log

- Keep the project progress log in `docs/progress.md`.
- After every coding task, update `docs/progress.md` with the change summary, current status/result,
  validation performed, and next planned goals or remaining TODOs.
