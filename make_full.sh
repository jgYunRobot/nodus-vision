#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_type="Release"
jobs=20
clean_build=0
configure_only=0
build_only=0

printUsage() {
    cat <<'EOF'
description:
  initialize dependencies, build and locally install nodus-vision.

usage:
  ./make_full.sh [options]

options:
  --build-type <type>  CMAKE_BUILD_TYPE: Debug or Release (default: Release)
  --jobs, -j <count>   Parallel build jobs (default: 20)
  --clean              Remove the selected build and install directories first
  --configure-only     Initialize and configure without building
  --build-only         Build and install an existing configuration
  --help, -h           Show this help

examples:
  ./make_full.sh
  ./make_full.sh -j 32
  ./make_full.sh --build-type Debug --clean
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
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
        --clean)
            clean_build=1
            shift
            ;;
        --configure-only)
            configure_only=1
            shift
            ;;
        --build-only)
            build_only=1
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

if [[ "${configure_only}" -eq 1 && "${build_only}" -eq 1 ]]; then
    echo "error: --configure-only and --build-only cannot be used together." >&2
    exit 2
fi

if [[ "${clean_build}" -eq 1 && "${build_only}" -eq 1 ]]; then
    echo "error: --clean and --build-only cannot be used together." >&2
    exit 2
fi

if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: --jobs must be a positive integer." >&2
    exit 2
fi

case "${build_type}" in
    Debug|debug)
        build_preset="debug"
        ;;
    Release|release)
        build_preset="release"
        ;;
    *)
        echo "error: --build-type must be Debug or Release." >&2
        exit 2
        ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake command not found." >&2
    exit 127
fi

if ! command -v ninja >/dev/null 2>&1; then
    echo "error: ninja command not found." >&2
    exit 127
fi

build_root="${project_root}/build/${build_preset}"
install_root="${project_root}/install/${build_preset}"

if [[ "${clean_build}" -eq 1 ]]; then
    echo "[make_full] remove ${build_root}."
    cmake -E remove_directory "${build_root}"
    echo "[make_full] remove ${install_root}."
    cmake -E remove_directory "${install_root}"
fi

cd "${project_root}"

if [[ "${build_only}" -eq 0 ]]; then
    "${project_root}/setup_dev.sh"
    echo "[make_full] configure nodus-vision with ${build_preset} preset."
    cmake --preset "${build_preset}"
fi

if [[ "${configure_only}" -eq 0 ]]; then
    echo "[make_full] build nodus-vision with ${jobs} jobs."
    cmake --build --preset "${build_preset}" --parallel "${jobs}"

    echo "[make_full] install nodus-vision to ${install_root}."
    cmake --install "${build_root}"
fi

echo "[make_full] full build and local install complete."
