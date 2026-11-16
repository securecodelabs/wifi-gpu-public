#!/usr/bin/env bash
# Build, test, and install this standalone engine without a container.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix="${HOME}/.local/opt/wifi-gpu"
jobs=2
hip=OFF
usage() {
    cat <<'EOF'
Usage: bash scripts/build-native-linux.sh [options]
  --prefix DIR  Installation directory (default: ~/.local/opt/wifi-gpu)
  --jobs N      Parallel build jobs (default: 2)
  --hip         Enable Linux ROCm HIP backend (requires installed HIP headers/runtime)
  --help        Show help
Requires Linux, CMake, Ninja, a C++17 compiler, OpenSSL 3 development files,
and Python 3. Stop running recovery jobs before upgrading an installation.
EOF
}
while (($#)); do
    case "$1" in
        --help) usage; exit 0 ;;
        --hip) hip=ON; shift ;;
        --prefix|--jobs)
            if (($# < 2)) || [[ -z "$2" || "$2" == --* ]]; then
                printf 'Missing value for %s\n' "$1" >&2; exit 2
            fi
            case "$1" in --prefix) prefix=$2 ;; --jobs) jobs=$2 ;; esac
            shift 2 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done
if [[ ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf '%s\n' '--jobs must be a positive integer' >&2; exit 2
fi
if [[ $(uname -s) != Linux ]]; then
    printf '%s\n' 'This script requires Linux.' >&2; exit 1
fi
for tool in cmake ninja c++ python3; do
    command -v "$tool" >/dev/null || { printf 'Required tool missing: %s\n' "$tool" >&2; exit 1; }
done
prefix=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$prefix")
build_dir="$repo_dir/build-native-linux"
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DWIFI_GPU_ENABLE_HIP="$hip" -DWIFI_GPU_TEST_SOFTWARE_VULKAN=OFF
cmake --build "$build_dir" --parallel "$jobs"
ctest --test-dir "$build_dir" --output-on-failure -LE gpu
cmake --install "$build_dir"
"$prefix/bin/wifi-gpu" selftest --cpu-only
printf '\nInstalled wifi-gpu in %s/bin\n' "$prefix"
