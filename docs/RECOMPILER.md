# The recompiler

How `lc8670recomp` gets from an image to compilable C.

## 1. Container

`vms.c` classifies the input:

- **`.vms` game file** — the file *is* the ROM image: ROM address 0 is file
  offset 0, and the 128-byte descriptive header sits in the second block
  (`$200`), where the code simply jumps over it.
- **`.vms` data file** — header at offset 0, no code. Rejected with an
  explanation instead of being recompiled into garbage.
- **`.dci`** — a 32-byte directory entry followed by the file with every 4-byte
  group byte-reversed. Both are undone.
- **raw** — a headerless dump, such as a BIOS image.

## 2. Discovery

Two passes, because the LC8670 draws no distinction between a jump and a tail
call: a `JMPF` to another routine looks exactly like a jump to a basic block.

**Pass 1** walks everything reachable from the seeds, treating jumps as
intra-procedural, and records every *call* target. Those plus the vectors are
the function entries.

**Pass 2** walks each entry's blocks. A jump landing on some *other* entry is
recorded as a tail call rather than inlined — without that, one hot routine
absorbs half the ROM.

Seeds are the eleven hardware vectors:

| Address | Source |
|---|---|
| `$0000` | reset |
| `$0003` | INT0 |
| `$000B` | INT1 |
| `$0013` | INT2 or T0L overflow |
| `$001B` | INT3 or base timer overflow |
| `$0023` | T0H overflow |
| `$002B` | T1H or T1L overflow |
| `$0033` | SIO0 |
| `$003B` | SIO1 |
| `$0043` | RFB |
| `$004B` | P3 |

plus anything given to `--seed`.

### When coverage looks low

Recursive descent only finds what something statically points at. Firmware
typically has an entry-point table nothing branches to — on the VMS BIOS,
`$0100` onward is a run of 8-byte thunks:

```
0100  callf $3CB7
0103  not1 $10D,0     ; EXT - swap ROM and flash address space
0105  jmpf $0105      ; spin
0108  callf $3CB7
...
```

Nothing jumps there; the Dreamcast calls in. Seeding the table more than doubles
what is found:

```sh
lc8670recomp info bios.bin                      #  109 functions,  5.1%
lc8670recomp info bios.bin --seed 0x100:0x1F8:8 #  148 functions, 13.2%
```

`--seed` takes a comma-separated list where any entry may be
`START:END:STRIDE`.

## 3. Emission

One LC8670 routine becomes one C function. Every instruction lowers to a call
into `recomp_rt.h`, so no semantics are duplicated in generated code.

```c
/* vmu_func_0110: $0110-$0112, 2 instructions */
void vmu_func_0110(void) {
L_0110:
    /* 0110: inc $030                 */ vmu_rt_inc(0x030);
    vmu_rt_tick(3);
    /* 0112: ret                      */ vmu_rt_ret(); return;
}
```

Three files come out:

| File | Contents |
|---|---|
| `recomp_funcs.c` | one C function per routine |
| `recomp_funcs.h` | declarations, plus `vmu_recomp_register_all()` |
| `recomp_dispatch.c` | populates the address → function table |

All three carry a *do not commit* banner, and `generated/` is gitignored in
full.

### Choices worth knowing

- **A label is emitted only where something branches**, so the output reads as
  structured C rather than one label per instruction.
- **Cycles are batched**, flushed as a single `vmu_rt_tick(n)` before each
  control transfer. Timers only need to be right where code can observe them.
- **Operands are annotated with register names.** On this CPU every peripheral
  is an address, so `vmu_rt_mov(0x10E, 0xA3)` carries
  `; OCR (oscillator: 32kHz / 600kHz / 6MHz)` beside it.
- **Undefined opcodes become runtime traps, not build failures.** Discovery
  walking into data is normal; a trap records the address and returns, and
  `traps: 0` at run time is the signal that nothing did.

## 4. Verify

Compile it, run it, and diff against the interpreter:

```sh
cmake -S . -B build-gen -DVMURECOMP_GENERATED=generated
cmake --build build-gen --config Release --target vmuhost

build-gen/vmuhost image.vms --frames 60 --pgm recomp.pgm
build/tools/lc8670recomp/vmurun image.vms --steps 400000 --pgm interp.pgm
cmp recomp.pgm interp.pgm
```

Identical frames mean the two engines agree. See [`RUN.md`](RUN.md).
