# Linux and backend setup

## Standalone native build

No sibling repository, container, Rust toolchain, or capture converter is required.
Install a C++17 compiler, CMake 3.16+, Ninja, OpenSSL 3 development files, and
Python 3.9+. Then run:

```sh
bash scripts/build-native-linux.sh --prefix "$HOME/.local/opt/wifi-gpu"
"$HOME/.local/opt/wifi-gpu/bin/wifi-gpu" selftest --cpu-only
```

The script builds, runs non-GPU tests, and installs the executable, kernels, and
license notices. Stop active jobs before upgrading an installation. You can also
use the direct CMake commands in the README.

## AMD HIP

This project's HIP implementation is Linux-only and uses ROCm 7 HIP/HIPRTC.
ROCm 7.2 was tested with the Radeon RX 9060 XT (`gfx1200`). Consult AMD's
[installation documentation](https://rocm.docs.amd.com/projects/install-on-linux/en/latest/)
for supported host OS, driver, GPU, and package combinations.
Install `hip-dev` and `hip-runtime-amd` for your ROCm release. No CUDA SDK is used.

```sh
cmake -S . -B build-hip -G Ninja -DCMAKE_BUILD_TYPE=Release -DWIFI_GPU_ENABLE_HIP=ON
cmake --build build-hip --parallel 2
ctest --test-dir build-hip --output-on-failure -LE gpu
LD_LIBRARY_PATH=/opt/rocm/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ./build-hip/wifi-gpu devices --backend hip
LD_LIBRARY_PATH=/opt/rocm/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} ./build-hip/wifi-gpu selftest --backend hip
```

If ROCm headers are elsewhere, pass `-DWIFI_GPU_HIP_INCLUDE_DIR=/path/to/include`.
Ensure the runtime loader can find ROCm libraries and your user can access the GPU
render node and `/dev/kfd` according to the driver installation instructions.
The engine compiles its bundled `pbkdf2.cl` through HIPRTC at runtime. HIP builds
default to HIP with batch 262144, group 64, and chunk 256. Other backends remain
selectable explicitly; backend initialization errors do not prove CPU fallback.

## OpenCL and Vulkan

Install your GPU vendor's runtime. AMD ROCm OpenCL and NVIDIA OpenCL are distinct
implementations. The engine loads installed runtimes dynamically; vendored headers
do not install drivers. Use `devices --backend opencl` or `devices --backend vulkan`
and select a backend-specific device index.

For AMD systems with multiple OpenCL ICDs, `scripts/rocm-opencl.py --help` documents
the optional runtime-selection wrapper. Test the chosen runtime with `selftest`
before recovery. OpenCL/HIP batches allow up to 262144 candidates; CPU/Vulkan allow
up to 4096. Large batches can increase cancellation and progress-update latency.

Vulkan CPU emulation is rejected unless `--allow-software-vulkan` is supplied.
CI can exercise it using `-DWIFI_GPU_TEST_SOFTWARE_VULKAN=ON` and Mesa's software
Vulkan driver. Passing that test does not validate a physical GPU.

## Optional container

The Dockerfile builds and tests the engine with HIP enabled, then installs it in
a non-root runtime image. It pins a tested ROCm 7.2 userspace and Ubuntu base;
the host still needs a compatible driver. Building a container is optional:

```sh
docker build -t wifi-gpu:local .
docker run --rm wifi-gpu:local selftest --cpu-only
```

GPU container access requires explicit device and group permissions appropriate
to your host. Consult your driver/container runtime documentation; the engine
itself does not change host drivers, permissions, or GPU power settings.
