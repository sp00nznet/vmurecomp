# Roadmap

What is next, what is deferred, and what is out of scope. Ordered by what
actually unblocks the next thing.

## Next

### 1. Get past the firmware idle loop

The one thing standing between "recompiles and runs" and "recompiles and works".

The BIOS parks here:

```
3421  mov #$00,$034     clear a flag
3424  set1 $107,0       PCON.HALT - suspend until an interrupt
3426  ld $034           read the flag back
3428  bnz $3407         an ISR set it: go do work
342A  br $3424          it did not: halt again
```

Interrupts *are* delivered, and a Port 3 press edge *is* raised — pressing a
button moves `P3INT` from `$05` to `$07`, so the source flag is landing. The
handler still never sets `$034`.

The blocker is that the **bit assignments of `BTCR`, `T0CON`, `T1CNT` and
`P3INT` are not in the published VMS documentation.** The runtime currently uses
the conventional LC86 positions, and at the stall the machine reads
`IE=83 PCON=01 BTCR=41 T0CON=00 T1CNT=50 P3INT=05` — Timer 0 is stopped
outright, so whatever the loop waits on is the base timer or Port 3.

Resolving this is measurement, not guesswork: capture the register writes from
a real unit, or diff against a known-good emulator's trace. Every place the
model relies on an unverified bit is marked in `src/timer.c` and `src/cpu.c`,
and `vmu_timer_set_divisor()` exists so cadence can be trimmed without touching
either.

### 2. Model the `EXT` address-space switch

The BIOS's firmware-call thunks are `callf <addr>` / `not1 EXT,0` /
`jmpf <self>`: the `EXT` bit swaps ROM and flash address space, which is how the
BIOS hands control to a game living in flash. Discovery decodes ROM space only,
so anything behind that switch is invisible. Modelling it should lift coverage
well past the current 13.2% and is a prerequisite for running a mini-game that
the BIOS launches rather than one recompiled directly.

### 3. Recompile an actual mini-game

The toolkit was brought up on the BIOS because it is the only executable image
in the Dreamcast VMU set. A mini-game extracted from a Dreamcast save would be a
generalization test — the same thing `crystalmines2-lynx-recomp` was for
`lynxrecomp` — and would live in its own repo consuming this one as a submodule.

### 4. Shared blocks are duplicated across functions

Discovery walks each function independently, so a routine reached by a plain
jump from several callers is emitted into each of them. On the BIOS that is
20,698 emitted instructions over 8,663 distinct bytes. It is correct and it
compiles; it just bloats the output. The fix is to promote a block with multiple
distinct predecessors into its own function.

## Deferred

- **Per-frame capture.** `vmuhost` dumps only the final frame, because the cycle
  budget unwinds out of the firmware main loop rather than resuming it. Per-frame
  capture wants a present callback fired from `vmu_rt_tick`, not a host loop.
- **Honouring a rewritten return address.** Recompiled code keeps control flow on
  the C stack and the emulated stack in parallel, and only the C stack decides
  where control goes. No VMU title is known to need otherwise; the interpreter
  already honours it, so the oracle will show it if one does. The fix would be a
  trampoline that re-dispatches on the popped value.
- **Save states.** The machine is a fixed-size struct plus the ROM pointer, so
  this is mostly a serializer; nothing depends on it yet.
- **An SDL frontend.** A window, a pad and live audio. The runtime already
  produces frames and PCM, so this is presentation only.
- **Maple bus and the serial link.** `SCON0`/`SCON1`/`SBUF` are stored but inert.
  Needed for VMU-to-Dreamcast and VMU-to-VMU communication.
- **Flash filesystem writes.** The directory and FAT are readable in principle;
  nothing writes saves back.
- **Cycle accuracy.** Time is retired once per basic block, not per instruction,
  and interrupts land only at those boundaries. Fine for the LCD and the buzzer;
  not enough for anything that races the timers deliberately.

## Out of scope

- **Shipping any binary.** No BIOS, no VMS files, no recompiler output, ever.
  See the README.
- **A general LC86000 toolchain.** This targets the VMU's LC8670 and its
  peripherals. The decoder is reusable; the runtime is not meant to be.
- **An assembler.** Several already exist.
- **Dreamcast-side emulation.** A VMU plugged into a Dreamcast is a different
  project.
