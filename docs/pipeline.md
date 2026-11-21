# Read capture-converter output through stdin

The two programs share the HC22000 text format. The converter reads the capture
and emits hash records on stdout; WiFi GPU reads them with `--hashes -` and
verifies password candidates on the selected backend. No intermediate hash file
is needed.

These examples require WiFi GPU **0.17.0 or newer**. Earlier versions do not
support `--hashes -`. The pipeline guide is included in the binary packages.

Examples assume both executables are on PATH. You can substitute their full
paths; in PowerShell use `&` before a quoted executable path. Keep WiFi GPU's
`kernels/` directory next to its executable. Confirm stdin support with
`wifi-gpu recover --help`, which must describe `--hashes -`.

Use a single capture file and omit the converter's `--output` option so its hash
records go to stdout. `--directory` is a batch-to-files workflow, not this pipe.

## Windows PowerShell with NVIDIA OpenCL

With both executables on PATH:

```powershell
wifi-cap2hashcat capture.pcapng |
  wifi-gpu recover --hashes - --backend opencl --device 0 --wordlist words.txt --output results.json
```

Use `--backend vulkan` to select Vulkan. Both backends were verified with a
synthetic capture and its known password on a GeForce GTX 650. The converter
also accepts classic `.pcap` input.

```powershell
wifi-cap2hashcat capture.pcap |
  wifi-gpu recover --hashes - --backend vulkan --device 0 --wordlist words.txt --output vulkan-results.json
```

To inspect the converted records without testing password candidates:

```powershell
wifi-cap2hashcat capture.pcapng | wifi-gpu inspect --hashes -
```

## Linux Bash

```sh
set -o pipefail
wifi-cap2hashcat capture.pcapng |
  wifi-gpu recover --hashes - --cpu --wordlist words.txt --output results.json
```

This CPU command was verified on Ubuntu 26.04 under WSL. On a Linux host with a
working GPU runtime, replace `--cpu` with the appropriate backend option.
NVIDIA GPU access was unavailable in the local WSL test environment.

For example, on a Linux host where OpenCL device 0 is available:

```sh
wifi-gpu devices --backend opencl
set -o pipefail
wifi-cap2hashcat capture.pcapng |
  wifi-gpu recover --hashes - --backend opencl --device 0 --wordlist words.txt --output gpu-results.json
```

The converter itself needs no GPU. GPU selection belongs to WiFi GPU.

## Reproduce with synthetic data

From the `wifi-gpu` repository root, with `wifi-cap2hashcat` checked out in the
sibling directory, use Python 3.11+ to wrap the converter's bundled synthetic
fixture as PCAP and PCAPNG and create a two-candidate wordlist. The
`.tools/pipe-demo` directory must be new; choose a different name for another run.

If the sibling checkout does not exist, clone the public converter using this
directory name, or adjust the fixture path below to your existing checkout:

```sh
git clone https://github.com/securecodelabs/wifi-cap2hashcat-public.git ../wifi-cap2hashcat
```

Windows PowerShell:

```powershell
@'
from pathlib import Path
import runpy
pcap, pcapng, _ = runpy.run_path("../wifi-cap2hashcat/scripts/test-container.py")["fixture"]()
demo = Path(".tools/pipe-demo")
demo.mkdir(parents=True)
(demo / "synthetic.pcap").write_bytes(pcap)
(demo / "synthetic.pcapng").write_bytes(pcapng)
(demo / "words.txt").write_bytes(b"incorrect-password\nsynthetic-test-password\n")
'@ | python -
wifi-cap2hashcat .tools/pipe-demo/synthetic.pcapng |
  wifi-gpu recover --hashes - --backend opencl --wordlist .tools/pipe-demo/words.txt
```

Ubuntu Bash:

```sh
python3 - <<'PY'
from pathlib import Path
import runpy
pcap, pcapng, _ = runpy.run_path("../wifi-cap2hashcat/scripts/test-container.py")["fixture"]()
demo = Path(".tools/pipe-demo")
demo.mkdir(parents=True)
(demo / "synthetic.pcap").write_bytes(pcap)
(demo / "synthetic.pcapng").write_bytes(pcapng)
(demo / "words.txt").write_bytes(b"incorrect-password\nsynthetic-test-password\n")
PY
set -o pipefail
wifi-cap2hashcat .tools/pipe-demo/synthetic.pcapng |
  wifi-gpu recover --hashes - --cpu --wordlist .tools/pipe-demo/words.txt
```

Expected result: SSID `cipher-test-lab`, password `synthetic-test-password`,
`status=recovered`, and `recovered=1/1`. Replacing `synthetic.pcapng` with
`synthetic.pcap` exercises the same handshake in the other container format.
The setup creates capture and wordlist files; the actual pipeline transfers
HC22000 through the pipe without writing a hash file.

## Reports and resume

The converter's diagnostics go to stderr, so they do not enter the hash pipe.
Keep the streams separate: do not add `2>&1` before `|`. A conversion report can
be saved with `wifi-cap2hashcat capture.pcapng --report conversion.json` while
the hash output still goes through the pipe.

WiFi GPU validates the complete stream through EOF before recovery, with the
same parser limits as a hash file. Empty or malformed input fails. Candidate
wordlists remain ordinary files. Bash `pipefail` also reports an upstream
converter failure in the pipeline exit status.

Add `--session recovery.session` to checkpoint a job. To resume, repeat the same
pipe and candidate options with `--resume recovery.session`. The piped hash bytes
must be identical, including line endings; changed input is rejected.

For example, in Bash, save the conversion report and recovery checkpoint:

```sh
set -o pipefail
wifi-cap2hashcat capture.pcapng --report conversion.json |
  wifi-gpu recover --hashes - --cpu --wordlist words.txt --output results.json --session recovery.session
```

After an interrupted job, repeat the pipe with `--resume`:

```sh
set -o pipefail
wifi-cap2hashcat capture.pcapng |
  wifi-gpu recover --hashes - --cpu --wordlist words.txt --output results.json --resume recovery.session
```

Omit `--report conversion.json` on resume to avoid replacing the existing
conversion report. Existing report/result files are protected; use fresh names
for a separate job. The converter's `--overwrite` applies only to converter
outputs; WiFi GPU has its own output policy. The same two pipes work in
PowerShell without the Bash `set -o pipefail` line.

WiFi GPU exit 0 means all records were recovered; exit 2 means the candidate
sources were exhausted, not that a password was recovered. Empty or malformed
hash input returns exit 1. Do not merge stderr into the hash stream with `2>&1`.

PowerShell's `$LASTEXITCODE` after a native pipeline reports the last executable's
exit status. Inspect converter errors on stderr too; use a shell with `pipefail`
when automation must combine both exit statuses.

The converter establishes structural usability of the capture records. WiFi
GPU's successful match independently verifies the password against those records.

`--hashes -` is supported by both `inspect` and `recover`. It does not make the
capture converter part of WiFi GPU: other tools can supply valid HC22000 text to
the same stdin interface. A literal hash file named `-` can be read as `./-`.
Wordlists and mask files still use normal file paths; stdin is reserved for hashes.
