# wifi-gpu

A Windows and Linux command-line tool for recovering passwords from HC22000
records, with HIP, OpenCL, Vulkan, and CPU backends. Version **0.17.1**.
Use it with networks and data you own or have permission to audit.

The engine supports streamed wordlists, masks, hybrid wordlist/mask searches,
multiple records and SSIDs, JSON results, progress, checkpoints, and resume.
Every reported match is independently re-derived and verified on the CPU.
It consumes HC22000 directly; capturing traffic and converting PCAP files are
outside this project's scope. No container or Hashcat installation is required.

## Pipe captures from wifi-cap2hashcat

Version 0.17.0 accepts HC22000 directly from stdin, allowing the converter and
recovery engine to cooperate without an intermediate hash file:

```sh
wifi-cap2hashcat capture.pcapng | wifi-gpu recover --hashes - --backend opencl --wordlist words.txt
```

Use `--backend vulkan` for Vulkan or `--cpu` for CPU recovery. The stream is
validated with the same limits as a hash file, and session resume requires
identical input bytes. See [Windows, Linux and synthetic pipe examples](docs/pipeline.md)
for inspection, reports, checkpoint/resume and backend requirements.

## Earlier releases

Version 0.16 tunes OpenCL on AMD gfx1200: repeated complete synthetic jobs
measured **655.8 kH/s for masks** and **654.0 kH/s for wordlists**, gains of
**5.17%** and **3.49%** over the matched v0.15 OpenCL baseline. No driver patch
is required. See [OpenCL setup](docs/opencl.md) and [benchmark conditions](docs/benchmarks.md).

Version 0.15 adds large Vulkan batches, GPU verification and an optional tuned
AMD RADV compiler profile. Repeated synthetic tests on one RX 9060 XT measured
657.1 kH/s for masks and 658.0 kH/s for wordlists with that profile.
See [Vulkan setup](docs/vulkan.md) and [benchmark conditions](docs/benchmarks.md).

## Build

Windows: install Visual Studio 2022 C++ build tools, CMake, and Python 3.9+.
Run from the repository root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure -LE gpu
.\build\Release\wifi-gpu.exe selftest --cpu-only
```

Linux: install a C++17 compiler, CMake 3.16+, OpenSSL 3 development files,
and Python 3.9+. On Ubuntu:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build libssl-dev python3
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -LE gpu
./build/wifi-gpu selftest --cpu-only
```

For AMD HIP, install ROCm 7.2 headers and runtime first, then configure with
`-DWIFI_GPU_ENABLE_HIP=ON`. See [Linux and backend setup](docs/linux.md).
CMake builds without HIP default to Vulkan; builds with HIP default to HIP.
Windows builds currently support OpenCL, Vulkan, and CPU, with HIP limited to Linux.

## Choose a backend

| Hardware / environment | Backend | Runtime |
| --- | --- | --- |
| Supported AMD GPU on Linux | `--backend hip` | ROCm 7.2 HIP/HIPRTC; HIP-enabled build |
| AMD, NVIDIA, or Intel with OpenCL support | `--backend opencl` | Vendor's OpenCL driver/runtime |
| Vulkan-capable GPU | `--backend vulkan` | Vendor's Vulkan driver/runtime |
| CPU on Windows / Linux | `--cpu` | Windows BCrypt / Linux OpenSSL 3 |

ROCm targets AMD. NVIDIA OpenCL uses NVIDIA's runtime, not ROCm.
Device support depends on the installed runtime. Enumerate and test the selected
backend before starting recovery:

```sh
wifi-gpu devices --backend opencl
wifi-gpu selftest --backend opencl --device 0
```

In these examples, replace `wifi-gpu` with your built executable's path or add its
folder to PATH. Keep the executable and its adjacent `kernels/` directory together.
The Vulkan shader is included and verified during builds; no shader compiler is
needed unless you change its source.

## Quick start with synthetic data

From the repository root, this recovers the bundled synthetic fixture on the CPU:

```sh
wifi-gpu inspect --hashes tests/fixtures/all-types.hc22000
wifi-gpu recover --cpu --hashes tests/fixtures/all-types.hc22000 --mask 'labpass42'
```

For your own authorized data:

```sh
wifi-gpu recover --backend opencl --hashes network.hc22000 --wordlist words.txt --output results.json --session recovery.session
```

Exactly ten uppercase hexadecimal characters use this mask:

```sh
wifi-gpu recover --backend hip --hashes network.hc22000 --mask-file masks/hex-uppercase-10.txt --session recovery.session
```

That mask has 1,099,511,627,776 candidates. Quoting masks prevents the shell from
expanding `?`. Search duration depends on the hardware, record types, number of
SSIDs, and workload. See [usage and resume](docs/usage.md).

## Testing and performance

CPU tests run without GPU drivers. Explicit GPU checks are opt-in:

```sh
wifi-gpu selftest --backend hip
python tests/test_recovery.py --exe ./build/wifi-gpu --backend hip
python tests/test_opencl_batches.py --exe ./build/wifi-gpu --backend hip
```

On one Radeon RX 9060 XT / ROCm 7.2 setup, prior synthetic complete-recovery
measurements were approximately 656â€“657 kH/s at stock settings. These are
hardware-specific observations, not a performance guarantee. See
[benchmark methodology and reproduction](docs/benchmarks.md).
An optional [RX 9060 XT memory profile](docs/amd-memory-profile.md) measured a
further approximately 0.7%; installing the engine does not apply that setting.

## Development and licensing

- Recovery complexity is checked locally and in CI with pinned clang-tidy;
  see [code-quality checks](docs/code-quality.md). Releases require builds,
  tests, sanitizer, security and complexity checks without a hosted analysis subscription.
- [Contributing and local checks](CONTRIBUTING.md)
- [Security reports](SECURITY.md)
- [Architecture and recovery semantics](docs/architecture.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

Original project code is licensed under [Apache-2.0](LICENSE). Third-party files
retain their own licenses. This repository begins with a reviewed source snapshot;
private operational logs, captures, deployment records, and experimental history
are excluded. Public CI uses GitHub-hosted runners and synthetic fixtures.
