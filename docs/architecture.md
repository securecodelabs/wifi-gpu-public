# Architecture and correctness

HC22000 parsing, candidate enumeration, GPU derivation, verification, and
checkpoint publication are separate parts of the engine. Candidates stream from
wordlists or indexed masks without materializing the complete keyspace. Records
are deduplicated and grouped by exact SSID bytes, so each candidate/SSID pair
requires one PBKDF2-HMAC-SHA1 derivation with 4096 iterations and a 32-byte PMK.

HIP and OpenCL use the bundled `kernels/pbkdf2.cl`; Vulkan uses the checked-in
SPIR-V compiled from `kernels/pbkdf2.comp`. CPU mode uses Windows BCrypt or Linux
OpenSSL. HIP, AMD OpenCL and Vulkan can keep PMKs on the GPU and filter eligible
exact-nonce WPA2 EAPOL v2 records there. Other verification paths run on the CPU.
Every reported match is independently re-derived and verified using CPU crypto.
Both PBKDF2 output blocks are retained; there is no reduced-iteration recovery mode.

Supported verifier paths include ordinary PMKID SHA1/SHA256, EAPOL key versions
1/2/3, and supported FT PMKID/EAPOL records. Nonce correction follows record flags
and an optional explicit bound. Unsupported or malformed data fails validation.
Synthetic fixtures exercise the parser, independent crypto oracle, false-positive
rejection, backend agreement, candidate boundaries, and resume semantics.

A completed batch commits its position only after all relevant SSID groups finish.
Cancellation or a mid-batch failure preserves the prior committed offset and
verified hits; resume repeats unfinished work. Saved hits are checked again when
loading a session. Session fingerprints cover input content, paths, candidate
configuration, and nonce policy, while allowing runtime/backend changes.

File publication uses same-directory atomic replacement. Windows publication
retries temporary sharing conflicts; persistent failures retain prior snapshots.
Filesystem durability still depends on the OS and storage. Session checksums are
integrity checks and do not authenticate untrusted edits. Protect captures,
wordlists, results, sessions, and diagnostic logs with filesystem permissions.

Public CI runs CPU, lifecycle, packaging, software Vulkan, sanitizer, and static
checks. Physical GPU validation is performed separately using synthetic data on
trusted hardware. Untrusted pull requests must not execute on a personal runner.
