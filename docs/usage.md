# Usage

Run `wifi-gpu --help` and `wifi-gpu recover --help` for the complete CLI.
Input is HC22000 (supported record types 01–04). `inspect --hashes FILE` validates
and describes records without searching. Password candidates are byte strings
of 8–63 bytes; UTF-16 wordlists are rejected. Use `--hex-wordlist` for hexadecimal
byte encodings, and UTF-8 or byte-oriented text for ordinary wordlists.

## Wordlists, masks, and hybrids

`--wordlist`, `--mask`, and `--mask-file` can be repeated. With wordlists and masks
together, `--hybrid append` appends generated mask bytes; `prepend` reverses that.
Repeated wordlist entries are repeated attempts.

| Token | Characters |
| --- | --- |
| `?l`, `?u` | Lowercase / uppercase ASCII letters |
| `?d` | Digits 0–9 |
| `?h`, `?H` | Lowercase / uppercase hexadecimal |
| `?s` | ASCII punctuation and space |
| `?a` | Printable ASCII including space |
| `?b` | All 256 byte values |
| `?1` … `?4` | Custom sets defined by `--charset1` … `--charset4` |
| `??` | Literal question mark |

```sh
wifi-gpu recover --cpu --hashes tests/fixtures/all-types.hc22000 --mask 'labpass?d?d'
wifi-gpu recover --backend opencl --hashes network.hc22000 --wordlist words.txt --mask '?d?d' --hybrid append
wifi-gpu recover --backend hip --hashes network.hc22000 --charset1 '?u?d' --mask '?1?1?1?1?1?1?1?1'
```

Masks enumerate rightmost position fastest. Mask files contain one mask per line;
blank lines and lines beginning with `#` are ignored. `masks/hex-uppercase-10.txt`
contains `?H?H?H?H?H?H?H?H?H?H`. Keyspaces must fit the supported 64-bit index.

## Results, progress, and resume

Use `--output results.json --session recovery.session` to retain results and a
checkpoint. Ctrl+C requests a checkpoint and interruption; Linux SIGTERM also
requests a graceful stop. Resume by repeating the original input options:

```sh
wifi-gpu recover --backend opencl --hashes network.hc22000 --wordlist words.txt --output results.json --resume recovery.session
```

Input paths, contents, candidate order, and nonce policy must match. Backend,
device, and batch size can change. `--skip` cannot be combined with `--resume`.
A partial batch is replayed after interruption so candidates are not lost.
Snapshots are atomic; their checksums detect corruption, not malicious edits.
Result/session files contain recovered secrets and should be kept private.

`--limit N` bounds candidate enumeration; `--runtime SECONDS` bounds recovery time
between work boundaries. Zero means unlimited. `--status-every SECONDS` controls
progress reports. `--activity-json` adds `WIFI_ACTIVITY` JSON lines for integrations,
including source-loading progress and the latest tested candidate in hex; protect
these logs too. Totals count valid candidates and may be null when unavailable.

CPU mode accepts `--threads 1..64`. GPU mode accepts `--device`, `--batch`,
`--group 1..256`, and `--chunk` (OpenCL/Vulkan 1..1024; HIP 1..256). HIP
defaults to batch 262144; other backends default to 1024. CPU batches stop at
4096; every GPU backend supports up to 262144. Vulkan chunks above 256 opt
into a longer adaptive submission target; see [Vulkan setup](vulkan.md).
OpenCL retains its adaptive 50 ms launch target; see [OpenCL setup](opencl.md).

| Recovery exit code | Meaning |
| --- | --- |
| 0 | All records recovered |
| 1 | Error |
| 2 | Candidate sources exhausted |
| 3 | Interrupted |
| 4 | Search limit reached |

Treat exit codes 2–4 as job outcomes, not successful password recovery.
