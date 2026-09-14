# Architecture

Two halves that meet at one header.

```
tools/lc8670recomp/          the recompiler - runs once, offline
  decode.c    LC8670 instruction decoder
  analyze.c   function discovery
  emit.c      C code generation
  sfr.c       special-function-register names
  vms.c       container handling
  main.c      the CLI
  vmurun.c    the interpreter oracle
  host.c      runs recompiled C

include/vmurecomp/           the runtime - links into the recompiled program
src/
  cpu.c       machine state, reset, input edges
  mem.c       RAM-space load/store, indirection, stack
  lcd.c       XRAM to framebuffer
  timer.c     base timer, T0, T1, interrupt arbitration
  audio.c     Timer 1 PWM to PCM
  recomp_rt.c instruction semantics, dispatch, run loop
```

`include/vmurecomp/recomp_rt.h` is the seam. Everything the generated C is
allowed to call is declared there and nowhere else, which is what keeps the
emitter honest: if a lowering needs something new, it has to be added to the
runtime deliberately rather than reached for.

## Why the split matters

Flag semantics live in exactly one place. `ADD` is `vmu_rt_add()` whether it came
from opcode `0x81`, `0x82` or `0x87`, so a correction to the overflow rule fixes
every `ADD` in the program at once rather than in thousands of emitted lines.
That is also why the emitter never inlines arithmetic.

## The machine

One global, `vmu`. The VMU has no second core and generated code is
straight-line C, so threading a context pointer through every call would cost
real time and buy nothing.

RAM space is 512 bytes and is the whole of I/O as well:

| Range | What |
|---|---|
| `$000-$0FF` | general-purpose RAM, two banks via `PSW.RAMBK0` |
| `$100-$17F` | special function registers, including ACC, PSW, B, C, SP |
| `$180-$1FB` | XRAM window — the LCD frame buffer, banked by `XBNK` |

Because peripherals *are* memory, `vmu_read` / `vmu_write` are the entire I/O
path. A write to `$180` paints a pixel; a write to `T1LR` retunes the buzzer; a
read of `P3` samples the buttons. That is why the emitter annotates direct
operands with register names — `ld $125` is unreadable, `ld $125 ; XBNK` is not.

## Control flow

| LC8670 | Generated C |
|---|---|
| branch inside the function | `goto L_<addr>;` |
| `CALL` / `CALLF` / `CALLR` | `vmu_rt_call(target, return_addr);` |
| `RET` / `RETI` | `vmu_rt_ret(); return;` |
| jump onto another function | `vmu_rt_dispatch(target); return;` |
| undefined opcode | `vmu_rt_trap(pc, ...); return;` |

Calls go through the dispatch table — a flat 64K array of function pointers,
which makes a computed jump an array index — and also push the return address
onto the *emulated* stack, because game code reads `SP`. Control itself returns
via the C stack. The consequence is recorded in `recomp_rt.h`: code that
rewrites its own return address will not do what it does on hardware. The
interpreter honours it, which is one of the reasons the interpreter exists.

## Time and interrupts

Generated code calls `vmu_rt_tick(n)` once per basic block, not once per
instruction. Timers only need to be right where code can observe them, and
batching keeps the output readable.

Interrupts are delivered *from inside* `vmu_rt_tick`. Recompiled code cannot be
preempted mid-function, and a firmware main loop that spins waiting for an
interrupt only ever hands control back to the runtime through `tick` — so that
is the one place delivery can happen. A handler runs to completion before the
interrupted code resumes.

That leaves one problem: firmware main loops never return, so "call the entry
point and wait" hangs forever. `vmu_rt_run(entry, budget)` sets a cycle deadline
and `longjmp`s out of the loop when it is reached. That is safe because
generated code is plain C holding no resources across a basic block — and it is
why a host gets the final frame rather than a resumable session.

## The oracle

`vmurun` interprets the same image through the same runtime. It is the ground
truth during bring-up:

- recompiled and interpreted **disagree** → the bug is in the recompiler
- they **agree** and the screen is wrong → the bug is in the runtime

On the VMS BIOS they agree, and the output frames are byte-identical.

It earns its keep in a second way: it uses real stack-based control flow, so it
behaves correctly in the cases the recompiled build documents as unsupported.
