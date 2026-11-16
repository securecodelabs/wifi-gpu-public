#!/usr/bin/env bash
# Use the source Dockerfile's baseline toolchain, independent of runner CPU variants.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
image=wifi-gpu:release-build-$$
container=
cleanup() {
    if [[ -n "$container" ]]; then docker rm -f "$container" >/dev/null; fi
    docker image rm "$image" >/dev/null
}
trap cleanup EXIT
docker build --target test --tag "$image" "$repo"
container=$(docker create "$image")
mkdir -p "$repo/build-linux-release"
docker cp "$container:/out/bin/wifi-gpu" "$repo/build-linux-release/wifi-gpu"
