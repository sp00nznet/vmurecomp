# Contributing

Thanks for looking. This is a small, dependency-free C11 project and the bar for
a change is mostly "it is covered by a test and it does not add a dependency".

## The one hard rule

**No binaries, and nothing derived from one.** Do not open a pull request that
adds, attaches, or links:

- BIOS or firmware images, `.vms` / `.vmi` / `.dci` files, or ROM dumps
- recompiler output — generated C, disassembly listings, lifted IR
- symbol maps or struct definitions reconstructed from a proprietary binary
- test fixtures containing any of the above

Every test in this repository runs on hand-written synthetic input, and it needs
to stay that way. If you are reporting a bug in the decoder or the emitter,
describe it with the **bytes**, not the file:

> `0x9F 0x4C 0xFE` at `$0100` decodes `d9` as `$04C`, should be `$14C`

That is reproducible, it is a fact about the published instruction set, and it
costs nobody a copyright question.

## Getting set up

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

No dependencies beyond CMake 3.16+ and a C11 compiler. If that is not true after
your change, the change needs rethinking.

## What a good change looks like

- **It has a test.** Decoder changes go in `tests/test_decode.c`, runtime
  semantics in `tests/test_runtime.c`, discovery and emission in
  `tests/test_pipeline.c`. The two emitter bugs fixed in 0.1.0 each got a
  hand-assembled ROM that reproduces them in about fifteen lines — copy that
  shape.
- **It keeps semantics in one place.** Instruction behaviour belongs in
  `recomp_rt.c`, not inlined into emitted code. If a lowering needs something
  new, add it to `recomp_rt.h` deliberately.
- **It cites the hardware.** If you change a flag rule, a cycle count or a
  register bit, say in the comment what it is based on. Where the published
  documentation is silent — the timer and port control bits, notably — the code
  says so explicitly rather than pretending. Keep that honesty; a confident
  wrong constant is worse than an admitted unknown.
- **It does not add a dependency.** The library and the recompiler are C11 and
  the standard library, deliberately.

## Conformance

`tests/conform.c` reports a pass/fail count rather than a bare pass. The opcode
corpus always runs. The image corpus is opt-in via `VMURECOMP_CORPUS` and lives
outside this repository — see `tests/README.md`.

If your change improves the image count, update
`tests/conformance-baseline.txt` in the same commit and say what moved. If it
lowers it, that is a regression and CI will say so.

## Style

- Match the file you are editing. C11, four spaces, no tabs.
- Comments explain *why*, and name the hardware behaviour being modelled.
- Commit subjects in the imperative mood. Explain the reasoning in the body when
  the change is not obvious.
- `cargo`-style clean build: the CI has a `-Wall -Wextra -Werror` job.

## Reporting a bug

Include the input shape (bytes, not files), what you expected, what happened,
and your platform and compiler. If it is a divergence between the recompiled
build and `vmurun`, say so — that is the most useful bug report this project can
get, because the interpreter is the oracle and a disagreement localises the
fault immediately.
