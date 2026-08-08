#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_type="Debug"
jobs=20
config_path="assets/configs/examples/fake_camera_pilot.json"
build_before_run=0

printUsage() {
    cat <<'EOF'
description:
  run nodus-vision with a selected configuration and optional rebuild.

usage:
  ./run_app.sh [options]

options:
  --config <path>      Vision config path
                       (default: assets/configs/examples/fake_camera_pilot.json)
  --build-type <type>  CMAKE_BUILD_TYPE: Debug or Release (default: Debug)
  --jobs, -j <count>   Parallel build jobs (default: 20)
  --build              Rebuild the selected binary before running
  --help, -h           Show this help

examples:
  ./run_app.sh
  ./run_app.sh --config assets/configs/examples/fake_camera.json
  ./run_app.sh --build-type Release -j 32
  ./run_app.sh --build
EOF
}

resolveProjectPath() {
    local input_path="$1"
    if [[ "${input_path}" = /* ]]; then
        printf '%s' "${input_path}"
        return
    fi
    printf '%s/%s' "${project_root}" "${input_path}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)
            if [[ $# -lt 2 ]]; then
                echo "error: --config requires a value." >&2
                exit 2
            fi
            config_path="$2"
            shift 2
            ;;
        --build-type)
            if [[ $# -lt 2 ]]; then
                echo "error: --build-type requires a value." >&2
                exit 2
            fi
            build_type="$2"
            shift 2
            ;;
        --jobs|-j)
            if [[ $# -lt 2 ]]; then
                echo "error: --jobs requires a value." >&2
                exit 2
            fi
            jobs="$2"
            shift 2
            ;;
        --build)
            build_before_run=1
            shift
            ;;
        --help|-h)
            printUsage
            exit 0
            ;;
        *)
            echo "error: unknown option '$1'." >&2
            printUsage
            exit 2
            ;;
    esac
done

if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --jobs must be a positive integer." >&2
    exit 2
fi

case "${build_type}" in
    Debug|debug)
        build_type="Debug"
        build_preset="debug"
        ;;
    Release|release)
        build_type="Release"
        build_preset="release"
        ;;
    *)
        echo "error: --build-type must be Debug or Release." >&2
        exit 2
        ;;
esac

config_path="$(resolveProjectPath "${config_path}")"
executable_path="${project_root}/build/${build_preset}/app/nodus-vision"

if [[ ! -f "${config_path}" ]]; then
    echo "error: config not found: ${config_path}" >&2
    exit 1
fi

cd "${project_root}"

if [[ "${build_before_run}" -eq 1 ]]; then
    "${project_root}/make_full.sh" --build-type "${build_type}" --jobs "${jobs}"
fi

if [[ ! -x "${executable_path}" ]]; then
    echo "error: executable not found: ${executable_path}" >&2
    echo "hint: rerun with --build." >&2
    exit 1
fi

run_command=("${executable_path}" "${config_path}")
printf '[run_app] command:'
printf ' %q' "${run_command[@]}"
printf '\n'
exec "${run_command[@]}"
