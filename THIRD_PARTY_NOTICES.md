# Third-party notices

Original wifi-gpu code is licensed under Apache-2.0. The following materials
retain their upstream copyrights and licenses; the project license does not
replace them.

| Material | Origin | License / retained text |
| --- | --- | --- |
| OpenCL headers | Khronos OpenCL-Headers, commit `82f60316cc142e7b21dc1d8983b26de18858fbd9` | Apache-2.0; `third_party/OpenCL-Headers/LICENSE` and header notices |
| Vulkan headers | Khronos Vulkan-Headers, tag `v1.2.162` | Apache-2.0; `third_party/Vulkan-Headers/LICENSE.txt` and header notices |
| Mesa NIR patch context | Mesa 26.0.8 `src/compiler/nir/nir_opt_algebraic.py` | MIT, Copyright (C) 2014 Intel Corporation; `third_party/mesa/LICENSE.txt` |
| Public HC22000 self-test fixture | Hashcat v8.0.0 `src/modules/module_22000.c` | MIT, Copyright 2015–2026 Jens Steube; `third_party/hashcat/LICENSE.txt` |

The OpenCL and Vulkan header provenance is recorded alongside each header set.
The Hashcat fixture is `tests/fixtures/hashcat-public.hc22000`; no Hashcat engine
or kernel implementation is bundled. Its source is
https://github.com/hashcat/hashcat/blob/v8.0.0/src/modules/module_22000.c.
Other bundled recovery fixtures are synthetic project test data; see their README.

Linux builds link to system OpenSSL 3. Windows builds use system BCrypt. GPU
backends load installed OpenCL, Vulkan, or HIP/HIPRTC runtimes; those drivers and
runtime binaries are not included in the source or engine ZIP/tar archives.
OpenSSL 3 is Apache-2.0 licensed; see https://www.openssl.org/source/license.html.
Container images additionally contain OS and ROCm packages under their respective
licenses and package notices, retained by their package managers. Redistributing
such images requires preserving those package-specific notices too.

Binary engine archives include this document and the project and header licenses
under `share/wifi-gpu/`. Source distributions retain the original notice locations.
