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

For OpenCL, change the backend and executable. For Vulkan, keep batch at most
4096. To use ten uppercase hexadecimal candidates, add
`--mask-file masks/hex-uppercase-10.txt`. Reports include command arguments,
versions, executable/kernel hashes, search throughput, and whole-process wall
throughput. Output may include local paths and device details; review before sharing.

For a derivation-only synthetic benchmark:

```sh
wifi-gpu benchmark --backend hip --batch 262144 --group 64 --chunk 256 --repeats 5
```

## Prior measurements

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
