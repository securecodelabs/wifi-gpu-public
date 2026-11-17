# Vulkan setup and tuning

Vulkan is available on Windows and Linux through the vendor's Vulkan runtime.
Use `--backend vulkan` explicitly. HIP-enabled builds continue to default to HIP.
Keep the executable and its adjacent `kernels/pbkdf2.spv` from the same release:
v0.15 uses shader interface v4 and rejects older v1/v2/v3 shaders.

```sh
wifi-gpu devices --backend vulkan
wifi-gpu selftest --backend vulkan
wifi-gpu recover --backend vulkan --hashes network.hc22000 --wordlist words.txt
```

Vulkan supports batches of 1..262144 and chunks of 1..1024. Defaults remain
batch 1024, group 64 and chunk 256. Exact-nonce WPA2 EAPOL v2 MIC verification
runs on the GPU, followed by independent CPU verification for every match.
Other record types use the shared verifier. Full 32-byte PMKs and all 4096
PBKDF2 iterations are retained. All candidate sources and resume semantics apply.

The backend requires Vulkan 1.0 compute, four storage buffers, host-visible
memory and compute timestamps. It uses SPIR-V 1.0 and 32-bit integer arithmetic;
no subgroup or 64-bit shader extension is required. Test each device and driver
with `selftest`; API portability does not establish support for every GPU.

## Optional AMD RADV compiler profile

The measured RX 9060 XT profile uses a separately built Mesa 26.0.8 RADV driver
with the included NIR patch. The driver recognizes SHA-1 rotations and majority
expressions so ACO can select fewer vector instructions. Installing the engine
alone does not install or enable that driver. No modified driver binary is
bundled in the release.

This is an experimental compute profile tested against this application's CPU
oracle. It is not an upstream or generally Vulkan-conformant driver release.
Use it per process rather than replacing the system driver.

From a source checkout on a compatible Linux Mesa build environment:

```sh
sudo apt-get install build-essential meson ninja-build python3-mako python3-yaml \
  python3-packaging bison flex pkg-config libdrm-dev libexpat1-dev libzstd-dev \
  libelf-dev libvulkan-dev glslang-tools libxcb-dri3-dev libxcb-present-dev \
  libxshmfence-dev libwayland-dev wayland-protocols zlib1g-dev curl patch
bash scripts/build-radv-tuned.sh "$HOME/.local/opt/wifi-gpu-radv-26.0.8"
```

The helper refuses an existing destination, verifies the pinned source archive,
proves its bitwise transformations, applies the patch with zero fuzz and installs
only inside that new directory. Mesa's upstream licenses remain in the source.
See [patch provenance](../third_party/mesa/README.md).

Select the printed ICD path for the engine process. For the tested x86-64 build:

```sh
export VK_DRIVER_FILES="$HOME/.local/opt/wifi-gpu-radv-26.0.8/install/share/vulkan/icd.d/radeon_icd.x86_64.json"
wifi-gpu selftest --backend vulkan
wifi-gpu recover --backend vulkan --batch 262144 --group 256 --chunk 1024 \
  --hashes network.hc22000 --mask-file masks/hex-uppercase-10.txt \
  --limit 16777216 --output results.json --session run.session
```

These settings measured sustained medians of 657.1 kH/s for masks and
658.0 kH/s for wordlists on one RX 9060 XT. They require the tuned driver and
the documented host conditions; see [measurements and limits](benchmarks.md).
Unset `VK_DRIVER_FILES` to return to the installed system driver.

## Submission timing and cancellation

Chunks are maximum rounds per dispatch. Large batches first measure a short
dispatch and may reduce the requested chunk. Submissions combine at most 16
dependent dispatches with barriers and fence synchronization. Ordinary settings
target 50 ms with an eight-round probe; chunks above 256 opt into a 100 ms target
and a 32-round probe. A measured submission over 250 ms stops further work.
This guard cannot preempt already submitted work or guarantee duration under a
sudden slowdown. Larger batches can increase memory use and cancellation delay.

`benchmark` reports submission count and maximum measured submission time.
The selected profile measured 61.4–63.0 ms maximum submissions in its separate
three-repeat timing check. Measure the intended GPU before adopting these settings.
