# Contributing

Open an issue to describe a bug or proposed change, then submit a focused pull
request against `main`. Include OS, GPU, driver/runtime, backend, build options,
and a minimal synthetic reproduction when relevant. Keep private captures,
credentials, real recovered passwords, and deployment records out of issues,
commits, and logs. See SECURITY.md for vulnerability reporting.

Contributions are accepted under this project's Apache-2.0 license. Submit only
work you have authority to contribute and retain third-party notices and provenance.
Do not copy optimized kernels from another project without checking its license,
recording attribution, and preserving all required notices.

Build using the README and run `ctest --test-dir build -C Release --output-on-failure
-LE gpu` (omit `-C Release` for a single-configuration Linux build). GPU changes need
explicit self-tests, recovery tests, and boundary/negative cases on the affected
backend. Compare full-recovery performance using synthetic data, warmups, repeated
runs, and identical settings; include regressions and hardware limits.

For static checks, install `.github/requirements-ci.txt` in a virtual environment
and run `python scripts/ci_security.py`. Downloaded lint binaries are checksum-pinned.
Public workflows run on GitHub-hosted runners. Keep personal/GPU runners attached
to private repositories and run only reviewed code there.

Vulkan shader edits require `scripts/compile-shaders.ps1` with Khronos glslang or
the Vulkan SDK. Commit the shader source, SPIR-V, checksums, and compiler metadata
together. Generated SHA-1 code is checked by `python scripts/generate-sha1.py --check`.

Release maintainers should update the version, run the full checks, and explicitly
create a matching version tag when ready to publish binaries. A routine source
push is not a binary release. Archives must contain LICENSE, NOTICE, third-party
license texts, provenance, and checksums. Keep the binary and runtime kernels together.
