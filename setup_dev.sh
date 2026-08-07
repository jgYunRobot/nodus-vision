#!/usr/bin/env bash

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v git >/dev/null 2>&1; then
    echo "error: git command not found." >&2
    exit 127
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake command not found." >&2
    exit 127
fi

if ! git -C "${project_root}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "error: ${project_root} is not a Git working tree." >&2
    exit 1
fi

echo "[setup_dev] synchronizing submodule URLs."
git -C "${project_root}" submodule sync --recursive

echo "[setup_dev] initializing submodules at pinned commits."
git -C "${project_root}" submodule update --init --recursive --checkout
git -C "${project_root}" config --local submodule.recurse true

echo "[setup_dev] submodule status:"
git -C "${project_root}" submodule status --recursive
echo "[setup_dev] foundation setup complete; native dependencies are added by migration checkpoints."
