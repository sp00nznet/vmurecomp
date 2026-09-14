# Running

Two ways to execute an image, and they exist to be compared.

## `vmurun` — the interpreter oracle

Decodes and executes against the same runtime the recompiled build links. Slower
by a wide margin, and that is fine: it is the reference.

```sh
vmurun <image> [--steps N] [--pgm PATH] [--scale N] [--wav PATH]
               [--trace N] [--buttons BITS] [--press BITS]
```

| Option | Effect |
|---|---|
| `--steps N` | stop after N instructions (default 2,000,000) |
| `--pgm PATH` | write the final LCD frame as a binary PGM |
| `--scale N` | PGM scale factor (default 4) |
| `--wav PATH` | write captured buzzer audio |
| `--trace N` | print the first N instructions with ACC, PSW and SP |
| `--buttons BITS` | buttons held for the whole run |
| `--press BITS` | press a quarter of the way in, release halfway later |

Button bits: `01` up, `02` down, `04` left, `08` right, `10` A, `20` B,
`40` mode, `80` sleep.

`--press` is separate from `--buttons` for a reason. Firmware sleeps in a
`PCON.HALT` loop and only a press **edge** wakes it, so a button held from step 0
does nothing at all.

## `vmuhost` — the recompiled build

Runs the generated C. Built only when the generated tree is pointed at:

```sh
cmake -S . -B build-gen -DVMURECOMP_GENERATED=generated
cmake --build build-gen --config Release --target vmuhost
vmuhost <image> [--frames N] [--pgm PATH] [--wav PATH]
```

The image is still required: the *code* is recompiled, the *data* is not, and
`LDC` reads constants straight out of ROM space at run time.

`--frames N` is a cycle budget, not a capture count — roughly `N/30` seconds at
the 600 kHz game-mode clock. Only the final frame is written; see
[`../ROADMAP.md`](../ROADMAP.md) for why.

## Reading the output

```
outcome        : still running (budget reached)
cycles         : 1200000
traps          : 0
lit pixels     : 1536 of 1536
```

- **`outcome`** — `reset path returned` means the entry point finished.
  `still running` is normal and usually correct: firmware main loops never
  return, so the budget unwinds out of them.
- **`traps`** — the number that matters. Non-zero means execution hit an
  undefined opcode or an address with no registered function. On a clean
  recompile it is 0.
- **`lit pixels`** — a quick sanity check. All 1536 right after boot is the
  power-on segment test, not a bug.

`vmurun` adds a register line, which is what to read when execution is stuck:

```
IE=83 IP=02 PCON=01 BTCR=41 T0CON=00 T1CNT=50 P3INT=05 XBNK=02 OCR=A3
```

`PCON=01` says the CPU is halted waiting for an interrupt; `T0CON=00` says
Timer 0 is stopped, so it is not the source. That is how the current idle-loop
limitation was pinned down rather than guessed at.

## Cross-checking

The point of having both:

```sh
vmuhost image.vms --frames 60 --pgm recomp.pgm
vmurun  image.vms --steps 400000 --pgm interp.pgm
cmp recomp.pgm interp.pgm
```

- frames **differ** → the bug is in the recompiler
- frames **match** and the screen is wrong → the bug is in the runtime

On the VMS BIOS they match byte for byte.

One asymmetry is deliberate. The interpreter keeps control flow on the emulated
stack, so it honours code that rewrites its own return address; the recompiled
build keeps it on the C stack and does not. If a title ever depends on that, the
two will disagree and the oracle will be right.
