# OpenCL setup and gfx1200 tuning

OpenCL uses the vendor's installed runtime on AMD, NVIDIA or Intel. Enumerate
the devices and run the correctness checks first:

```sh
wifi-gpu devices --backend opencl
wifi-gpu selftest --backend opencl --device 0
```

Version 0.16 automatically selects the measured profile when AMD's OpenCL
device name is exactly `gfx1200` (the tested RX 9060 XT). It combines the
project's proved fixed-padding schedule and paired SHA-1 helper, compact input,
mask prefetch, and ROCclr's `-Wb,-misched=gcn-max-ilp` compiler option. Execution
remains OpenCL; it does not call HIP or require a HIP-enabled engine build.
The installed OpenCL runtime may have its own ROCm dependencies.

The profile keeps all 4096 PBKDF2 iterations and the complete 32-byte PMK.
Exact-nonce WPA2 EAPOL v2 records can use GPU MIC filtering; other verification
paths use the existing CPU checks. Every reported match is independently
re-derived and verified on the CPU.

## Measured configuration

```sh
wifi-gpu recover --backend opencl --device 0 --batch 262144 --group 64 --chunk 512 \
  --hashes synthetic.hc22000 --mask '?H?H?H?H?H?H?H?H?H?H' \
  --limit 16777216 --nonce-error-corrections 0 \
  --session synthetic.session --output synthetic.json --activity-json
```

Use a generated synthetic record with a known password outside that bounded
candidate range, and fresh output paths for each repetition. The portable
[benchmark helper](../scripts/benchmark_recovery.py) supplies a bundled synthetic
target for a shorter run; its native/process rates differ from the historical
observer-clock metric. See [benchmark conditions](benchmarks.md).

When multiple ICDs are installed on Linux, select the intended AMD ICD through
`OCL_ICD_VENDORS`, using its actual installed path. The reported setup used
`/etc/OpenCL/vendors/amdocl64.icd`; that path is packaging-dependent.
`WIFI_GPU_OPENCL_STATS=1` prints the selected compiler profile, compact-input
flag and loop-item count. Leave diagnostics unset during performance timing.

## Compatibility and bounds

No driver patch or replacement is required. The optional compiler profile is
limited to the tested device name, and a failed optimized build retries the
standard AMD source/options. Other AMD devices and other vendors retain their
existing kernels. Old custom kernels without the new entry points retain their
legacy input layout and two-work-item loop. NVIDIA's OpenCL runtime was checked
on a GTX 650; the AMD throughput figures do not predict NVIDIA performance.

OpenCL accepts batches up to 262144 and chunk ceilings up to 1024. Defaults
remain batch 1024/group 64/chunk 256. Larger batches first probe eight rounds,
target 50 ms using measured round time and enforce the 250 ms launch check.
Only fully verified batch positions are checkpointed. Cancellation is checked
between batches, so larger batches increase interruption latency and replay.

Replace the executable and its adjacent `kernels/` directory together. HIP
remains the default in HIP-enabled builds; this release does not change GPU
power settings or switch an existing installation to OpenCL automatically.

Physical validation covers full-key and MIC comparisons, binary/variable-length
inputs, shrinking buffers, optional filters, legacy kernels, compiler fallback,
large batches, checkpoints, cancellation and resume. Public CI covers portable
builds, CPU/software-Vulkan tests, sanitizers, packaging and security checks.
