# Optional RADV SHA-1 compiler profile

`sha1-idioms.patch` applies to Mesa 26.0.8 `src/compiler/nir/nir_opt_algebraic.py`.
It recognizes constant 32-bit funnel shifts and equivalent majority expressions
under the driver's existing capability guards. Changes are by securecodelabs,
2026. The original file's MIT notice is retained in `LICENSE.txt`.

This repository distributes the patch, not a Mesa binary. The build helper
downloads a pinned archive and preserves the upstream source and its licenses.
See [Vulkan setup](../../docs/vulkan.md) for dependencies, per-process driver
selection, validation and limitations. HIP remains the default in HIP builds.
