# Migration Ledger

## PA-CONTROL baseline

Vision migration is based on PA-CONTROL revision
`1c44efbe0b03fa77187305d0f50948f731e972f0`. The selected camera/vision paths were clean at capture
time. The source worktree had unrelated progress and dependency-directory changes, so the revision
plus the path inventory in `migration/source_manifest.json` defines the baseline.

PA-CONTROL remains a read-only design and behavior reference. Nodus Vision must not include, import,
or execute files from the PA-CONTROL checkout.

## Pilot public contract baseline

The implementation phase should pin Pilot OpenAPI version `1.0.2` from revision
`46d35dea702e71ae78aa2bb932a11e1bf5e79a73`. The observed SHA-256 for
`schemas/pilot/v1/openapi.yaml` is
`4c536a3b099a478f6af5420af63b5a32965913f8fc5943c655a55e8d2a3ed5a8`.

That contract already supplies generic component lifecycle and endpoint catalog publication.
Vision must use those public HTTP routes rather than importing or linking Pilot implementation
code. The contract artifact and provenance are added together at the Pilot-integration checkpoint,
not during this design-only foundation.

## Native dependency observations

- PA-CONTROL currently uses `librealsense` revision
  `05e3d1e57f3c87e6c9768eaca9e89639966beee2` (`v2.58.1-3-g05e3d1e57`) under Apache-2.0.
- Existing camera reference-frame math uses `nodus_rm` revision
  `ea12ce3070157835518c9436ae0517690fb51224`.
- RGB recording uses the host FFmpeg libraries and the `libx264` encoder. Distribution licensing
  and codec availability depend on the final FFmpeg build and must be recorded before a packaged
  release.
- JPEG preview encoding currently obtains `stb_image_write.h` indirectly from the librealsense
  checkout. Nodus Vision must pin and document that dependency directly rather than depending on an
  incidental third-party include path.

No native dependency source was copied or added by the foundation task.

## Phase 1 native dependencies

| Dependency | Source | Exact revision/version | License | Consuming target |
|---|---|---|---|---|
| librealsense | `https://github.com/realsenseai/librealsense.git` | `05e3d1e57f3c87e6c9768eaca9e89639966beee2` | Apache-2.0 | `nodus_vision_intel_d435` |
| nlohmann/json | `https://github.com/nlohmann/json.git` | `65ee68451d8eb2b5f3a30b410476ab83deb3289b` (`v3.12.0`) | MIT | librealsense configuration dependency |
| GoogleTest | `https://github.com/google/googletest.git` | `e39786088138f2749d64e9e90e0f9902daa77c40` (`v1.15.0`) | BSD-3-Clause | hardware-independent C++ tests |

The pinned librealsense revision configures its declared `v3.12.0` nlohmann/json dependency during
CMake configure. It is a build-tree dependency only; Nodus Vision does not import PA-CONTROL's copy.

## Phase 2 native dependencies

| Dependency | Source | Exact revision/version | License | Consuming target |
|---|---|---|---|---|
| Boost.Asio/Beast/JSON/System | Ubuntu package `libboost-all-dev` | `1.83.0.1ubuntu2` | BSL-1.0 | `nodus_vision_config`, `nodus_vision_provider_http`, `nodus_vision_runtime` |

Boost 1.83.0 is a host-provided pinned development dependency for the C++17 Phase 2 HTTP server and
typed JSON parser. Its version is resolved by CMake and recorded here rather than inherited from
PA-CONTROL.

## Phase 3 native dependencies

| Dependency | Source | Exact revision/version | License | Consuming target |
|---|---|---|---|---|
| libjpeg-turbo | `https://github.com/libjpeg-turbo/libjpeg-turbo.git` | `7fa4b5b762c9a99b46b0b7838f5fd55071b92ea5` (`3.0.3`) | BSD-3-Clause, IJG | `nodus_vision_provider_http` |

The pinned source is configured through a CMake `ExternalProject` because upstream explicitly does
not support `add_subdirectory()` integration. Vision links its generated static `libjpeg.a` directly.
