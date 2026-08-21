# ctl-python

[![test](https://github.com/aforsythe/ctl-python/actions/workflows/test.yml/badge.svg)](https://github.com/aforsythe/ctl-python/actions/workflows/test.yml)
[![wheels](https://github.com/aforsythe/ctl-python/actions/workflows/wheels.yml/badge.svg)](https://github.com/aforsythe/ctl-python/actions/workflows/wheels.yml)
[![codecov](https://codecov.io/gh/aforsythe/ctl-python/branch/main/graph/badge.svg)](https://codecov.io/gh/aforsythe/ctl-python)
[![Python 3.10+](https://img.shields.io/badge/python-3.10%20%7C%203.11%20%7C%203.12%20%7C%203.13%20%7C%203.14-blue.svg)](https://www.python.org/)
[![Code style: Ruff](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/astral-sh/ruff/main/assets/badge/v2.json)](https://github.com/astral-sh/ruff)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

Native Python bindings for [CTL](https://github.com/aces-aswf/CTL) (the
Color Transformation Language). A pybind11 extension applies CTL transforms
to NumPy arrays in-process via `libIlmCtl` / `libIlmCtlSimd`, with
persistent interpreter caching across calls.

## Why

The common alternative for applying CTL from Python is shelling out to the
`ctlrender` binary with an EXR round-trip. That works, but most of the
wall-clock there goes to subprocess spawn, EXR encode/decode, and per-call
CTL module parsing rather than color math. Calling `libIlmCtl` in-process
from a pybind11 extension skips all of those.

Measured end-to-end on M-series Mac (16 cores) running `scale2x.ctl`, warm
cache:

| Image     | ctlrender subprocess | ctl-python (in-process) | Speedup |
| --------- | -------------------: | ----------------------: | ------: |
| 64 x 64   |              9.7 ms  |                  37 us  |   260x  |
| 256 x 256 |             26.7 ms  |                 322 us  |    83x  |
| 512 x 512 |             81.7 ms  |                 603 us  |   136x  |
| 1024^2    |              304 ms  |                 1.7 ms  |   179x  |
| 2048^2    |             1186 ms  |                 6.0 ms  |   198x  |

Reproduce with `pytest benchmarks/bench_apply.py --benchmark-only`.

Plus: no temporary files, no argv string concatenation, the GIL is released
across the compute kernel, Ctrl+C aborts cleanly mid-compute, and
interpreters are cached so repeated calls amortize the module-parse cost --
sub-millisecond for trivial CTLs, up to a few hundred milliseconds for large
modules like the ACES v2 OutputTransform.

## How it works

1. NumPy array enters the extension via `ctl.apply(arr, ["idt.ctl", "odt.ctl"])`
   (a path or a sequence of paths, chained in order).
2. The wrapper coerces shape (1-D ramp, single triplet, `(N, 3)`, or
   `(H, W, 3)`) and dtype, then forwards a contiguous float32 view to the
   native binding.
3. The requested CTL file is looked up in a process-scoped
   `Ctl::SimdInterpreter` cache keyed on `(realpath, mtime)`. First hit
   parses + codegens the module; subsequent hits reuse the loaded
   interpreter. Cache invalidates on mtime change of the main file *or*
   any transitively imported `.ctl` so edits are picked up without
   `cache_clear()`.
4. `FunctionCall` executes against per-channel float planes in tiles of
   up to `SimdInterpreter::maxSamples()` (32K default). Multiple
   `FunctionCall` instances pulled from the same interpreter are dispatched
   across `hardware_concurrency()` worker threads, mirroring `ctlrender`'s
   tile-parallel pattern, so compute scales across cores rather than chunks.
5. The GIL is released across the compute kernel; worker 0 polls
   `PyErr_CheckSignals()` between chunks, so a long call (multi-second ACES
   OT at 4K) aborts cleanly on Ctrl+C, surfacing as `KeyboardInterrupt`
   instead of being queued until return.
6. Output planes are repacked into a fresh NumPy array shaped to match the
   caller's input.

The interpreter link target is swappable across CTL builds -- the extension
only uses the public `Interpreter` / `FunctionCall` API, so it links cleanly
against any build of libIlmCtl that exposes those symbols.

## Requirements

- Linux, macOS, or Windows. Wheels are built for Linux x86_64/aarch64,
  macOS x86_64/arm64, and Windows AMD64. The on-push CI matrix runs Linux
  and macOS only; Windows is exercised by the wheel build pipeline on tag
  pushes (vcpkg-managed Imath/OpenEXR).
- Python 3.10 or newer. CI verifies 3.10, 3.11, 3.12, 3.13, and 3.14 on
  Linux and macOS.
- CMake 3.28+ and a C++17 compiler when building from source (pip installs
  fetch a CMake wheel automatically when the system one is too old).
- CTL itself, fetched and statically vendored into the wheel via CMake
  `FetchContent` against `aces-aswf/CTL@ctl-1.5.5`. Developer builds may
  point at a local CTL checkout via
  `-Ccmake.define.FETCHCONTENT_SOURCE_DIR_CTL=/path/to/CTL`.
- Imath and OpenEXR (CTL transitive deps). Provided automatically by the
  wheel; from-source builds need the platform dev packages
  (`libopenexr-dev` / `libimath-dev` on Debian/Ubuntu, `openexr` / `imath`
  on Homebrew, `vcpkg install openexr imath` on Windows).

## Quick Start

```bash
pip install ctl-python
```

```python
import numpy as np
import ctl

img = np.random.rand(1024, 1024, 3).astype(np.float32)
out = ctl.apply(img, "/path/to/transform.ctl")
```

Building from source:

```bash
git clone https://github.com/aforsythe/ctl-python
cd ctl-python
uv venv && source .venv/bin/activate
uv pip install -e ".[dev]"   # full build isolation; auto-fetches build deps
pytest -v
```

`pip install -e .` runs CMake which fetches `aces-aswf/CTL@ctl-1.5.5` into
the build tree on first configure (~30-60 s on a warm network). Subsequent
rebuilds are incremental.

When iterating on the C++ source, install the build backend into the venv
once and pass `--no-build-isolation` for faster rebuilds (skips the
temporary build environment pip creates by default):

```bash
uv pip install -e ".[build,dev]"           # one-time
uv pip install -e . --no-build-isolation   # subsequent C++ rebuilds
```

The ctlrender parity tests need `ctlrender` on PATH (plus the `parity`
extra). Building with tools enabled installs it into the venv's bin dir:

```bash
uv pip install -e ".[dev,parity]" -Ccmake.define.CTL_BUILD_TOOLS=ON
```

## Usage

`ctl.apply(input, transforms, *, default_alpha=1.0, **params)`. The
canonical form for `transforms` is a sequence of CTL paths, chained in
order:

```python
out = ctl.apply(img, [idt, odt])
out = ctl.apply(img, ["foo.ctl", "bar.ctl"])
```

A single path can be passed bare (`str` or `pathlib.Path`):

```python
out = ctl.apply(img, "foo.ctl")
out = ctl.apply(img, pathlib.Path("foo.ctl"))
```

Supported shapes. Output dtype matches the input where possible (float32
inputs round-trip without copy; integer or float64 inputs are coerced to
float32 for compute and cast back on return):

| Input      | Output     | Notes                                              |
| ---------- | ---------- | -------------------------------------------------- |
| `(3,)`     | `(3,)`     | Single RGB triplet                                 |
| `(N,)`     | `(N, 3)`   | Each scalar replicated to R=G=B on entry; all three output channels returned (slice column 0 yourself if the transform is known gray-preserving) |
| `(N, 3)`   | `(N, 3)`   | RGB triples                                        |
| `(H, W, 3)`| `(H, W, 3)`| Image array                                        |

Internal compute runs in FP32. Non-finite values (`NaN`, `+Inf`, `-Inf`)
pass through identity transforms byte-identically.

### Multi-file transforms (ACES v2)

Transforms with `import` statements (e.g. ACES v2 Output Transforms) need
a module search path so the interpreter can resolve imports. Either set
`CTL_MODULE_PATH` in the environment or call `ctl.set_module_paths()`
before applying:

```python
import os, ctl
os.environ["CTL_MODULE_PATH"] = "/path/to/aces/aces-core/lib"
# or, equivalently:
ctl.set_module_paths(["/path/to/aces/aces-core/lib"])

idt = "/path/to/aces/.../CSC.Academy.ACEScg_to_ACES.ctl"
odt = "/path/to/aces/.../Output.Academy.Rec709-D65_100nit_in_Rec709-D65_BT1886.ctl"
neutrals = np.array([0.0, 0.01, 0.18, 0.5, 0.9, 1.0])
out = ctl.apply(neutrals, [idt, odt])
```

Every ACES IDT and ODT declares a varying `aIn` (alpha) input, and most
leave it without a CTL-source default. `ctl.apply` auto-defaults any
varying `aIn` to 1.0 (the ACES convention: opaque passthrough) so the
chain above just works. Override the alpha broadcast, or any other input
the CTL exposes (uniform or varying), via keyword:

```python
out = ctl.apply(neutrals, [idt, odt], aIn=0.5)
```

Pass `default_alpha=None` to disable the auto-fill; a missing `aIn` then
raises `CtlParameterError`.

A name no stage in the chain declares raises `CtlParameterError` with a
"did you mean..." hint (typo protection). To see what names a given CTL
accepts, use the signature helper:

```python
>>> ctl.signature(odt)
signature of /path/to/Output.Academy.Rec709-D65_100nit_in_Rec709-D65_BT1886.ctl
  inputs:
    rIn                varying  float
    gIn                varying  float
    bIn                varying  float
    aIn                varying  float  (has default)
  outputs:
    rOut               varying  float
    gOut               varying  float
    bOut               varying  float
    aOut               varying  float
```

`ctl.signature(path)` returns a `CtlSignature` dataclass for programmatic
use (fields `path`, `inputs`, `outputs`; each `CtlParam` carries `name`,
`type`, `varying`, `has_default`).

To build overrides programmatically and splat them at the call site:

```python
overrides = {"aIn": 0.5, "exposure": 2.0}
out = ctl.apply(neutrals, [idt, odt], **overrides)
```

First call pays the full parse + codegen for every `.ctl` in the chain
plus its imports (ACES v2 OT walks four `Lib.Academy.*.ctl` files; budget
~500-800 ms cold on M-series for the OT alone). Subsequent calls against
the same chain hit the cache; warm wall-clock for a handful of samples is
a few milliseconds.

## Testing

```bash
pytest -v
```

Parity tests invoke `ctlrender` as a subprocess to confirm extension
output matches the reference implementation byte-for-byte. They auto-skip
if the binary isn't on `PATH` or if the OpenEXR Python bindings aren't
installed:

```bash
pip install ctl-python[parity]
brew install ctl   # or apt-get install ctl
pytest tests/test_parity_ctlrender.py
```

Performance benchmarks live in `benchmarks/`:

```bash
pip install ctl-python[bench]
pytest benchmarks/bench_apply.py --benchmark-only
```

## Helpers & diagnostics

| Function                     | Purpose                                                       |
| ---------------------------- | ------------------------------------------------------------- |
| `ctl.signature(path)`        | Return the declared `main()` signature of a `.ctl`. Shows each input's name, type, varying-vs-uniform, and whether it has a CTL-source default -- useful for discovering what keyword overrides a transform accepts. |
| `ctl.cache_info()`           | List the interpreters currently cached in the extension, with the mtime each was loaded against. Answers "did my `.ctl` edit pick up?" -- compare cached mtime to current on-disk. |
| `ctl.cache_clear()`          | Drop all cached interpreters. Rarely needed in practice; mtime invalidation handles iterative `.ctl` edits including transitively imported modules. |
| `ctl.set_module_paths(paths)`| Configure the CTL `import` search path. Combined with the `CTL_MODULE_PATH` environment variable; user paths are searched first, then env paths. |

## Limitations

Gathered in one place so expectations are clear:

- **Shapes.** `(3,)`, `(N,)`, `(N, 3)`, and `(H, W, 3)` only. RGBA inputs
  must be split to apply CTL to the RGB portion, then the alpha
  re-concatenated.
- **Output is always 3-channel.** Extra output args the CTL declares
  beyond `rOut/gOut/bOut` (e.g. `aOut`) are computed by the interpreter
  but not returned to the caller.
- **Keyword overrides are scalar numerics only** -- `int`, `float`, `bool`.
  Vector, matrix, and CTL struct types (`Chromaticities`, `float[3]`,
  etc.) are not overridable through the wrapper. Varying-float overrides
  are supported (broadcast across all samples in a chunk); other varying
  types are rejected.
- **CTL-source `const` declarations are not overridable at all.**
  ACES v2's `peakLuminance`, `eotf_enum`, `limitingPri`, and `encodingPri`
  are declared `const` at file scope in the `.ctl` source, not as
  `main()` parameters -- the values bake in at parse time. Changing them
  requires editing the `.ctl`.
- **First three CTL inputs are bound to the R/G/B channels of the input
  array.** They're not overridable via keyword -- the value comes from
  `input` itself. If the CTL names these differently (rare), attempting
  `oldName=v` raises a clear error.
- **Overrides apply across every stage in a chain.** A given keyword hits
  every stage that declares a matching input; stages that don't declare
  it are silently skipped (names the chain declares nowhere at all
  trigger a typo error). There's no per-stage override syntax today;
  `ctlrender`'s sequential `-param` semantics is not replicated.

## Project Structure

```
ctl-python/
|-- CMakeLists.txt
|-- cmake/
|   `-- FindOrFetchCTL.cmake     CTL discovery (developer / FetchContent)
|-- pyproject.toml               scikit-build-core build config
|-- README.md
|-- LICENSE
|-- CITATION.cff
|-- ATTRIBUTION.md
|-- .github/                     GitHub Actions: lint + test + wheels
|-- src/
|   |-- ctl/                     Public Python package
|   |   |-- __init__.py
|   |   |-- _api.py              apply() entry point and shape coercion
|   |   |-- _signature.py        CtlSignature / CtlParam dataclasses
|   |   |-- _cache.py            cache_info / cache_clear / set_module_paths
|   |   `-- _errors.py           CtlError hierarchy
|   `-- _ctl_native/             pybind11 extension source
|       |-- module.cpp           PYBIND11_MODULE entry
|       |-- apply.cpp            Per-transform compute orchestration
|       |-- signature.cpp        main() introspection
|       |-- cache.cpp            Interpreter cache + import tracking
|       |-- overrides.cpp        Scalar/varying parameter binding
|       |-- tiling.cpp           Parallel tile dispatch
|       |-- interrupt.cpp        Ctrl+C polling
|       |-- exceptions.hpp       C++ exception types
|       `-- levenshtein.hpp      Edit-distance hint helper
|-- tests/                       pytest suite + CTL fixtures
`-- benchmarks/
    `-- bench_apply.py           Image-size sweep vs ctlrender subprocess
```

## Citation

See [`CITATION.cff`](CITATION.cff).

## License

Apache License 2.0; see [`LICENSE`](LICENSE). Each source file carries an
`SPDX-License-Identifier: Apache-2.0` header.

Third-party components retain their own licenses: CTL (the library this
extension links against) is Apache 2.0; Imath and OpenEXR are
BSD-3-Clause; pybind11 is BSD-3-Clause.

## Copyright

Copyright (c) 2026 Alex Forsythe, Academy of Motion Picture Arts and Sciences.
