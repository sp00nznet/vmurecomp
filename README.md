# vmurecomp

**A static-recompilation toolkit for Dreamcast VMU software — the first one we
know of.**

The VMU (1998) is about the smallest target a recompiler can have, and that is
exactly what makes it a good one. One CPU — a Sanyo **LC8670**, code-compatible
with the LC86000 series — with a flat 8-bit opcode map, 254 of whose 256 opcode
bytes are defined. No second processor, no coprocessor, no DMA engine. Every
peripheral is a byte in a 512-byte RAM space. That shape recompiles cleanly:
translate the one CPU to native C, and model the handful of peripherals as a
runtime.

`vmurecomp` is the reusable toolkit. Titles brought up on it belong in separate
repos that consume this one as a submodule — that split is deliberate: the
toolkit is the thing other people fork to recompile *their* VMU software.

> **No binaries here.** VMS files, BIOS dumps and ROM images are `.gitignore`d,
> and so is every line of generated C. This repo is the recompiler, the runtime
> and the docs — bring your own dump.

This repo follows the shared house style of `ps3recomp`, `xboxrecomp`,
`snesrecomp` and `lynxrecomp`: same directory layout, same CLI surface, same
conformance-harness reporting.

## How it works

```
  image.vms / BIOS dump
        │
        ▼
  ┌──────────────────┐   decode LC8670, discover functions from the reset and
  │  lc8670recomp    │   interrupt vectors (plus any --seed entry points),
  │  (tools/)        │   translate each routine to readable C: vmu_func_<addr>
  └──────────────────┘
        │  generated/recomp_funcs.c
        ▼
  ┌──────────────────┐   the runtime the generated C links against:
  │  vmurecomp (lib) │   CPU semantics · RAM/SFR map · dispatch · LCD · timers
  │  (src/, include/)│   · buzzer · interrupt delivery
  └──────────────────┘
        │
        ▼   (+ a host: load image, register, run, present)
  native executable — the recompiled code runs
```

Alongside it, `vmurun` **interprets** the same image against the same runtime.
That is the oracle: when the recompiled build and the interpreter disagree, the
bug is in the recompiler; when they agree and the screen is wrong, the bug is in
the runtime.

## Status — alpha: it recompiles, it runs, it boots and idles

Version **0.1.0**. The pipeline works end to end on a real image:

**dump → decode → discover → emit C → compile → run natively.**

Brought up on the **VMS BIOS v1.005**, which is the only executable image in the
Dreamcast VMU set (see [Picking a target](#picking-a-target)):

| | |
|---|---|
| Functions discovered | **148** (109 from the vectors alone) |
| Instructions recompiled | **20,698** |
| Generated C | **1.5 MB**, compiles and links clean |
| Runtime traps during execution | **0** |
| Recompiled frame vs interpreted frame | **byte-identical** |
| Opcode conformance | **786 / 786 checks** |

The recompiled BIOS executes its reset path — set the oscillator, set SP, select
the RAM bank, run the init chain — drives the LCD to its power-on state, and
reaches the firmware idle loop. It does not get past that loop yet; see
[What does not work](#what-does-not-work).

### Screenshot

![The recompiled VMS BIOS driving the LCD](docs/img/vmu-boot.png)

The 48×32 LCD after the recompiled BIOS runs its reset path: **all 1536 pixels
and all four icons lit**, which is the power-on segment test the firmware drives
before it idles. It is a black rectangle because that is genuinely what the
hardware shows at this point, not because the render is broken — the interpreter
produces the identical frame, byte for byte.

### What the generated C looks like

Each LC8670 routine becomes one C function. Every line carries its address and
disassembly, and direct operands are annotated with the special-function-register
name, because on this CPU every peripheral *is* a memory address:

```c
/* vmu_func_0000: $0000-$3F13, 311 instructions */
void vmu_func_0000(void) {
L_0000:
    vmu_rt_tick(2);
    /* 0000: jmp $0200                */ goto L_0200;
L_0200:
    /* 0200: mov #$A3,$10E            ; OCR (oscillator: 32kHz / 600kHz / 6MHz) */ vmu_rt_mov(0x10E, 0xA3);
    /* 0203: mov #$7F,$106            ; SP (stack pointer, always in RAM bank 0) */ vmu_rt_mov(0x106, 0x7F);
    /* 0206: clr1 $101,1              ; PSW (CY AC - IRBK1 IRBK0 OV RAMBK0 P) */ vmu_rt_clr1(0x101, 1);
    /* 0208: clr1 $14E,0              ; P3INT (port 3 interrupt control) */ vmu_rt_clr1(0x14E, 0);
    vmu_rt_tick(12);
    /* 0210: callf $3BA9              */ vmu_rt_call(0x3BA9, 0x0213);
    vmu_rt_tick(2);
    /* 0213: callf $338F              */ vmu_rt_call(0x338F, 0x0216);
```

### What works

**The recompiler** (`lc8670recomp`)
- A complete, validated **LC8670 decoder**: all 256 opcode bytes, correct
  operand forms, lengths and cycle counts, with resolved targets for every
  control-transfer mode — `a12` in-segment jumps, big-endian `a16`,
  little-endian `r16` relative to `next - 1`, and signed `r8`.
  0x50 and 0x51 are the only undefined opcodes and decode as traps.
- **Recursive-descent discovery** seeded from the reset vector and all ten
  interrupt vectors, with a two-pass split so that a `JMPF` onto another
  routine's entry becomes a tail call instead of swallowing it.
- `--seed` for entry points nothing reaches statically, including
  `START:END:STRIDE` for firmware call-thunk tables.
- **A C emitter** producing one readable `vmu_func_<addr>` per routine, lowered
  to centralized flag-correct runtime helpers.

**The runtime** (`vmurecomp`) — each unit-tested on synthetic input, no dumps:
- **CPU semantics**: documented CY / AC / OV rules for ADD, ADDC, SUB, SUBC; the
  24-bit MUL and the quotient/remainder DIV; rotates through carry; bit ops;
  the compare-and-branch forms that leave CY set to `left < right`.
- **RAM space**: two general-purpose banks, the SFR file, `@Ri` indirection
  through all four IRBK windows, and the stack that stays in bank 0 whatever
  `PSW.RAMBK0` says.
- **LCD**: the XRAM layout in full — six bytes per 48-pixel line, line pairs
  stored back to back then a four-byte skip, two dot-matrix banks plus icons —
  and a PGM writer.
- **Timers and interrupts**: base timer, Timer 0 with its prescaler, Timer 1,
  vector arbitration, and cooperative delivery that lets a firmware HALT loop
  make progress.
- **Buzzer**: Timer 1 PWM to square-wave PCM, with a WAV writer.
- **Work RAM** through `VRMAD1`/`VRMAD2`/`VTRBF` with auto-increment.

**Running it**
- `vmurun` interprets against the runtime — the bring-up oracle — with
  `--pgm`, `--wav`, `--trace`, `--press`, and a register dump that explains a
  stall.
- `vmuhost` runs the **recompiled** C directly.

### What does not work

Honest list; details and the reasoning in [`ROADMAP.md`](ROADMAP.md).

- **The firmware idle loop is never left.** The BIOS parks in a
  `set1 PCON,0` / test-flag / repeat loop at `$3424`, waiting for an interrupt
  to set a RAM flag. Interrupts *are* delivered, and a button press edge *is*
  raised, but the specific source it waits on has not been identified. Getting
  past it needs the `BTCR` / `T0CON` / `P3INT` **control-register bit
  assignments confirmed against hardware** — those bits are not in the
  published VMS documentation, and the current model uses the conventional LC86
  positions rather than guessing further. Every such spot is marked in the
  source.
- **Coverage is 13.2% of the BIOS image** with the thunk table seeded. Much of
  the rest is font and icon data, but some is code reached only through
  computed jumps and through the `EXT` bit that switches ROM and flash address
  space, which discovery does not model.
- **Flash address space is not modelled.** The BIOS swaps `EXT` to run code out
  of flash; only ROM space is decoded.
- **A rewritten return address is not honoured** by recompiled code. The
  interpreter honours it, which is one of the things it is for.
- No save states, no SDL frontend, no Maple/serial link.

## Getting started

### Prerequisites

- **CMake** 3.16 or newer
- A **C11 compiler** — MSVC 2022, GCC 9+, or Clang 10+
- A VMU image you own: a `.vms` mini-game, a `.dci`, or a raw dump

### 1. Build

```sh
git clone https://github.com/sp00nznet/vmurecomp
cd vmurecomp
cmake -S . -B build
cmake --build build --config Release
```

### 2. Check the toolchain

```sh
ctest --test-dir build -C Release
```

Expected: `100% tests passed, 0 tests failed out of 4`. This needs no image —
every test runs on synthetic input.

### 3. Inspect your image

```sh
build/tools/lc8670recomp/lc8670recomp info mygame.vms
```

```
container      : VMS mini-game (header at $200)
description    : MY GAME
rom image      : 65536 bytes
functions      : 148
instructions   : 20698
undefined ops  : 58
bytes reached  : 8663 of 65536 (13.2%)
vectors        : reset int0 int1 int2_t0l int3_basetimer t0h t1 sio0 sio1 rfb p3
```

If `bytes reached` looks low, the image probably has entry points nothing
reaches statically — a firmware call-thunk table, say. Seed them:

```sh
lc8670recomp info mygame.vms --seed 0x100:0x1F8:8
```

### 4. Recompile it to C

```sh
mkdir generated
build/tools/lc8670recomp/lc8670recomp recomp mygame.vms generated --seed 0x100:0x1F8:8
```

That writes `generated/recomp_funcs.{c,h}` and `generated/recomp_dispatch.c`.
**`generated/` is gitignored and must stay that way** — see
[What is not in this repo](#what-is-not-in-this-repo).

### 5. Build and run the recompiled code

```sh
cmake -S . -B build-gen -DVMURECOMP_GENERATED=generated
cmake --build build-gen --config Release --target vmuhost
build-gen/vmuhost mygame.vms --frames 60 --pgm frame.pgm
```

```
outcome        : still running (budget reached)
cycles         : 1200000
traps          : 0
lit pixels     : 1536 of 1536
wrote          : frame.pgm
```

`traps: 0` is the number to watch — a non-zero count means execution reached an
undefined opcode or an address with no function registered.

The image is still needed at run time: the code is recompiled, the data is not,
and `LDC` reads constants straight out of ROM space.

### 6. Cross-check against the interpreter

```sh
build/tools/lc8670recomp/vmurun mygame.vms --steps 400000 --pgm interp.pgm
cmp frame.pgm interp.pgm
```

Identical frames mean the recompiled C and the interpreter agree. They do on the
BIOS.

## Usage

```sh
lc8670recomp info   <image> [--seed LIST]
lc8670recomp dis    <image> [start] [count]
lc8670recomp recomp <image> <outdir> [--seed LIST]
lc8670recomp emit   <image> <outdir> [--seed LIST]

vmurun <image> [--steps N] [--pgm PATH] [--scale N] [--wav PATH]
               [--trace N] [--buttons BITS] [--press BITS]

vmuhost <image> [--frames N] [--pgm PATH] [--wav PATH]
```

Annotated disassembly, which is how the SFR map earns its keep:

```sh
$ lc8670recomp dis bios.bin 0x3420 8
3420  50  ???
3421  22  mov #$00,$034
3424  F8  set1 $107,0                ; PCON (power control: HOLD, HALT)
3426  02  ld $034
3428  90  bnz $3407
342A  01  br $3424
342C  61  push $101                  ; PSW (CY AC - IRBK1 IRBK0 OV RAMBK0 P)
342E  FD  set1 $17F,5                ; BTCR (base timer control)
```

`--press` exists because firmware sleeps in a HALT loop and only a press *edge*
wakes it; a button held from step 0 does nothing:

```sh
vmurun bios.bin --steps 2000000 --press 0x40
```

## Conformance

Per the house style, the harness reports a **pass/fail count against a fixed
corpus**, runs in CI on every push, and fails the build on regression rather
than only on total failure.

```sh
build/tests/conform
```

**Corpus 1 — the opcode space (786 checks, always runs).** All 256 opcode bytes
checked against the published encoding for mnemonic, operand form, length and
cycle count, plus target resolution for every control-transfer mode and a proof
that a linear sweep tiles 64 KB exactly. This corpus is redistributable because
it is the instruction set itself, so CI always has real numbers.

**Corpus 2 — real images (opt-in, skipped by default).** Point
`VMURECOMP_CORPUS` at a directory with a `manifest.txt` listing your own images;
each is parsed, discovered and recompiled, and the pass count is compared
against `tests/conformance-baseline.txt`. Nothing from it is redistributed, and
the harness skips with a clear message when it is absent.

| Corpus | Result |
|---|---|
| Opcode space | **786 / 786** |
| Images | opt-in, see above |

## Picking a target

The Dreamcast VMU set contains exactly one executable image: **`[BIOS] VMS
(World) (1.005)`**, the 64 KB firmware. There are no mini-games in it — VMU
mini-games ship *inside* Dreamcast save files and have to be extracted from a
`.vms`, a `.vmi`+`.vms` pair or a `.dci`. So the BIOS is what this toolkit was
brought up on, and it is a good first target: it is a real LC8670 program that
exercises the LCD, the timers, the buzzer and the interrupt controller, and
every VMU emulator boots it first.

`lc8670recomp` reads `.vms` game files, `.dci` containers and raw dumps. It
rejects `.vms` *data* files with a clear message, since those hold save data and
no code.

## What is not in this repo

Nothing derived from a proprietary binary, in any form:

- No BIOS or firmware images, no `.vms` / `.vmi` / `.dci` files, no ROM dumps.
- No recompiler output. `generated/` is gitignored in full, and every emitted
  file carries a *do not commit* banner.
- No disassembly listings, symbol maps or reconstructed struct definitions.
- No golden files in the test corpus that embed any of the above — every test
  here runs on hand-written synthetic input.

**The tool ships; the output never does.** You supply your own dump, you run the
recompiler locally, and the generated tree lands in a gitignored directory.

## Building from source

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

| Target | What it is |
|---|---|
| `vmurecomp` | the runtime library recompiled code links against |
| `lc8670core` | decoder, container parser, analyzer, emitter |
| `lc8670recomp` | the recompiler CLI |
| `vmurun` | the interpreter oracle |
| `vmuhost` | runs recompiled C (needs `-DVMURECOMP_GENERATED=<dir>`) |

The library and the recompiler have **no external dependencies** — C11 and the
standard library only.

## License

MIT. See [LICENSE](LICENSE).

The LC8670 instruction encodings, register addresses and LCD layout this
implements are published hardware facts; the implementation is original.
