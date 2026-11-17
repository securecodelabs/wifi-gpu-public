# Synthetic benchmarks

Performance depends on the GPU, runtime/compiler, power and thermal state,
candidate length, record type, SSID count, batch size, and verification path.
Compare complete recovery workloads as well as the isolated PBKDF2 benchmark.
Use an idle GPU, warm up first, repeat measurements, and report medians alongside
individual samples. A short kernel benchmark is not a full-job throughput claim.

## Reproduce a bounded complete-recovery workload

The script uses a bundled synthetic WPA2 EAPOL v2 record and absent generated
passwords. It verifies candidate counts and the absence of recovered matches:

```sh
python scripts/benchmark_recovery.py --exe ./build-hip/wifi-gpu --backend hip --batch 262144 --group 64 --chunk 256 --limit 1000000 --repeats 5 --warmup --output-dir build/benchmark-hip
```

For OpenCL or Vulkan, change the backend and executable. All GPU backends
accept batches up to 262144; OpenCL/Vulkan accept chunks up to 1024. To use ten uppercase hexadecimal candidates, add
`--mask-file masks/hex-uppercase-10.txt`. Reports include command arguments,
versions, executable/kernel hashes, search throughput, and whole-process wall
throughput. Output may include local paths and device details; review before sharing.

For a derivation-only synthetic benchmark:

```sh
wifi-gpu benchmark --backend hip --batch 262144 --group 64 --chunk 256 --repeats 5
```

## OpenCL v0.16 measurements

Three alternating rounds on an AMD Radeon RX 9060 XT (gfx1200, 16 GiB)
compare the v0.15 OpenCL implementation with the v0.16 compute source, using
the same compiler, installed runtime and GPU policy. The OpenCL ICD reports
driver 3581.0 (Ubuntu rocm-opencl-icd 7.1.0), alongside ROCm 7.2 libraries.
The existing manual memory setting remains 456 MHz with a 175 W cap.

| Workload | v0.15 OpenCL | v0.16 OpenCL | Gain | Tuned range |
| --- | ---: | ---: | ---: | ---: |
| Ten-character uppercase hexadecimal mask | 623.6 kH/s | **655.8 kH/s** | **5.17%** | 653.1-656.6 kH/s |
| Same candidates in a wordlist | 631.9 kH/s | **654.0 kH/s** | **3.49%** | 653.8-656.2 kH/s |

Every job exhausts 16,777,216 candidates against one absent synthetic exact-nonce
WPA2 EAPOL v2 record. Four known-password controls pass. Sustained throughput
uses completed-candidate progress versus observer time over the central 20-80%
of each complete job, including GPU MIC filtering and normal checkpoint writes.
GPU busy averages are 98.84% and 98.79%. Native search medians are 654.3 and
652.7 kH/s; whole-process medians, including setup and wordlist scanning, are
649.8 and 626.8 kH/s. All six tuned runs exceed 650 kH/s in both sustained and
native search rates.

Batch/group remain 262144/64. The baseline uses chunk 256; the tuned profile
uses chunk 512. No driver patch, power-cap change or clock-policy change is
made. These observations apply to this GPU/runtime/workload, not every AMD
device or dictionary. They are not a new Hashcat comparison.

[Individual measurements](measurements/opencl-0.16.0.json) preserve the numerical
results with operational paths and private history removed. The measured
development builds use the same compute source shipped in v0.16.0. See
[OpenCL setup](opencl.md) for selection, compatibility and reproduction.

## Vulkan v0.15 measurements

Three rotated rounds on one AMD Radeon RX 9060 XT (gfx1200, 16 GiB) compared
the previous tuned Vulkan implementation, the new implementation, and unchanged
HIP. Both Vulkan profiles used the same optional patched Mesa 26.0.8 driver;
HIP used ROCm 7.2.26015. The host retained its existing manual policy, 456 MHz
memory and 175 W power cap throughout.

| Workload | Previous tuned Vulkan | v0.15 Vulkan | HIP reference | Vulkan gain |
| --- | ---: | ---: | ---: | ---: |
| Ten uppercase hexadecimal mask | 645.8 kH/s | **657.1 kH/s** | 660.3 kH/s | **1.76%** |
| Same candidates in a wordlist | 645.3 kH/s | **658.0 kH/s** | 660.7 kH/s | **1.98%** |

Every job processed 16,777,216 candidates against one absent synthetic WPA2
EAPOL v2 target. Twelve known-password controls passed. Sustained rates are
observer-clock regressions over the central 20–80% of complete jobs, rather
than isolated kernel or last-status rates. Every paired run improved. Selected
Vulkan ranges were 655.6–660.9 kH/s for masks and 656.7–658.5 kH/s for wordlists,
with median GPU busy of 98.71% and 98.86%. Whole-process medians, including
startup and wordlist hashing/counting, were 654.2 and 632.6 kH/s respectively.

Vulkan used batch 262144/group 256, with chunk 256 for the baseline and 1024
for the selected build. HIP used batch 262144/group 64/chunk 256. These are
development-build measurements with the same compute source and shader shipped
in v0.15.0. [Individual measurements](measurements/vulkan-0.15.0.json) retain
the numerical results with private operational details removed.

These speeds require the [optional tuned RADV profile](vulkan.md), not just an
engine upgrade using an ordinary system driver. They describe this GPU and
workload; other devices, lengths, SSIDs and record types can differ. The gain
in this table is over the previous tuned Vulkan version, not public v0.14.1.

## Prior HIP measurements

On one Radeon RX 9060 XT (`gfx1200`) using ROCm 7.2, full synthetic recovery
measured approximately 656â€“657 kH/s at stock settings. A separate set of 12 longer
runs (three paired repetitions for each workload) compared the stock memory policy
with fixed stock memory DPM level 1 (456 MHz), at the same 175 W power cap:

Each run covered 67,108,864 candidates after a 4,194,304-candidate warmup, using
batch 262144, group 64, and chunk 256. The table reports sustained throughput
from progress versus observer time over the central 60% of candidates. It is
distinct from the engine-reported and whole-process rates in the portable script.

| Workload | Stock median | Fixed-memory median | Difference |
| --- | ---: | ---: | ---: |
| Ten uppercase hexadecimal mask | 656.289 kH/s | 661.391 kH/s | +0.777% |
| Synthetic wordlist | 657.384 kH/s | 661.723 kH/s | +0.660% |

These historical observations describe that setup only. Raw private operational
archives are not distributed in this source snapshot, so the table is a maintainer
summary rather than a publicly reproducible evidence bundle. The portable command
above is a new measurement procedure, not the exact historical long-run harness.
No new GPU throughput measurement is implied by publishing this snapshot.
See the [optional host profile](amd-memory-profile.md) for its narrow hardware guards.
