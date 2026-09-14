# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-09-14

First release. The pipeline works end to end on a real image: dump → decode →
discover → emit C → compile → run natively.

### Added

**Recompiler (`lc8670recomp`)**
- Complete LC8670 decoder covering all 256 opcode bytes with correct operand
  forms, lengths and cycle counts. Target resolution for every control-transfer
  mode: `a12` in-segment jumps, big-endian `a16`, little-endian `r16` taken
  relative to `next - 1`, and signed `r8`. 0x50 and 0x51 are the only undefined
  opcodes and decode as traps so a linear sweep always makes progress.
- Two-pass recursive-descent discovery seeded from the reset vector and all ten
  interrupt vectors. Pass 1 finds call targets, pass 2 walks each function's
  blocks, so a `JMPF` onto another routine's entry becomes a tail call rather
  than swallowing that routine.
- `--seed` for entry points nothing reaches statically, accepting
  `START:END:STRIDE` ranges for firmware call-thunk tables.
- C emitter producing one readable `vmu_func_<addr>` per routine, with each line
  annotated with its address, disassembly and special-function-register name.
- VMS container handling: `.vms` game files (header at `$200`), `.dci`
  unwrapping including the 4-byte word reversal, and raw dumps. `.vms` data
  files are rejected with an explanation rather than recompiled into garbage.
- Annotated disassembly (`dis`) and an image summary (`info`).

**Runtime (`vmurecomp`)**
- LC8670 instruction semantics with the documented CY / AC / OV rules, the
  24-bit MUL, the quotient/remainder DIV, rotates through carry, bit operations,
  and compare-and-branch forms that leave CY set to `left < right`.
- RAM space: two general-purpose banks, the SFR file, `@Ri` indirection through
  all four IRBK windows, and a stack pinned to bank 0 regardless of
  `PSW.RAMBK0`.
- LCD: the full XRAM layout (six bytes per line, line pairs back to back then a
  four-byte skip, two dot-matrix banks plus icons) and a PGM writer.
- Base timer, Timer 0 with prescaler, Timer 1, interrupt vector arbitration, and
  cooperative interrupt delivery from `vmu_rt_tick`.
- `vmu_rt_run`, a cycle budget that unwinds out of a firmware main loop, which
  never returns on its own.
- Buzzer: Timer 1 PWM rendered to square-wave PCM with a WAV writer, and an
  audible-ceiling knob so periods the piezo cannot physically sound are captured
  as silence rather than as an inaudible several-hundred-kHz tone.
- Work RAM through `VRMAD1` / `VRMAD2` / `VTRBF` with auto-increment.
- `vmu_set_buttons`, which raises the Port 3 interrupt on a press edge. Firmware
  sleeps in a HALT loop and only an edge wakes it, so a held button does nothing.

**Tooling and tests**
- `vmurun`, an interpreter over the same runtime, used as the bring-up oracle:
  `--pgm`, `--wav`, `--trace`, `--buttons`, `--press`, and a register dump that
  explains a stall.
- `vmuhost`, which runs the recompiled C directly, built when
  `-DVMURECOMP_GENERATED=<dir>` is set.
- Conformance harness reporting a pass/fail count: 786 checks over the opcode
  space, always runnable, plus an opt-in image corpus compared against a
  baseline so regressions fail the build.
- Unit tests for the decoder, the runtime and the discovery/emission pipeline,
  all on hand-written synthetic input.
- GitHub Actions CI building and testing on Linux, macOS and Windows.

### Known limitations

- The recompiled BIOS reaches the firmware idle loop at `$3424` and does not
  leave it. Interrupts are delivered and a press edge is raised, but the source
  the loop waits on is unidentified; resolving it needs the `BTCR` / `T0CON` /
  `P3INT` control-register bit assignments confirmed against hardware, as they
  are not in the published documentation.
- Discovery reaches 13.2% of the BIOS image with the thunk table seeded.
- The `EXT` flash/ROM address-space switch is not modelled.
- Recompiled code does not honour a rewritten return address; the interpreter
  does.

[Unreleased]: https://github.com/sp00nznet/vmurecomp/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/sp00nznet/vmurecomp/releases/tag/v0.1.0
