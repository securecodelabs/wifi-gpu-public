#!/usr/bin/env bash
# Build the opt-in RADV profile without changing the system Vulkan driver.
set -euo pipefail
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 NEW_WORK_DIRECTORY" >&2
    exit 2
fi
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd -- "$script_dir/.." && pwd)
work=$(realpath -m -- "$1")
if [[ -e "$work" ]]; then
    echo "Refusing to overwrite existing work directory: $work" >&2
    exit 2
fi
for tool in curl sha256sum tar patch meson ninja python3; do
    command -v "$tool" >/dev/null || { echo "Missing build dependency: $tool" >&2; exit 2; }
done
python3 "$script_dir/verify-radv-identities.py"
mkdir -p -- "$work"
archive="$work/mesa-26.0.8.tar.xz"
curl --fail --location --retry 3 --output "$archive" \
    https://archive.mesa3d.org/mesa-26.0.8.tar.xz
printf '%s  %s\n' caf1c0061a68e88dfa74967a7e780c0e85d65b6c4e334cd69095a5dc54ad78bc "$archive" | sha256sum --check -
tar -xJf "$archive" -C "$work"
source_dir="$work/mesa-26.0.8"
patch --batch --fuzz=0 -d "$source_dir" -p1 < "$repo/third_party/mesa/sha1-idioms.patch"
meson setup "$work/build" "$source_dir" \
    --prefix="$work/install" --libdir=lib --buildtype=release \
    -Db_ndebug=true -Dgallium-drivers= -Dvulkan-drivers=amd -Dplatforms= \
    -Dllvm=disabled -Damd-use-llvm=false -Dglx=disabled -Degl=disabled \
    -Dgbm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled \
    -Dvideo-codecs= -Dbuild-tests=false
ninja -C "$work/build" -j "${WIFI_GPU_BUILD_JOBS:-4}" install
printf '\nBuilt isolated RADV profile. ICD files:\n'
find "$work/install/share/vulkan/icd.d" -maxdepth 1 -name '*.json' -print
printf 'Set VK_DRIVER_FILES to that absolute ICD path for the wifi-gpu process.\n'
