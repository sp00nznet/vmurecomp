# The LC8670

Notes on the parts of the CPU that matter to a recompiler. This is not a
datasheet — it records the decisions the decoder and runtime had to make, and
which of them rest on published facts versus convention.

## Shape

8-bit, code-compatible with the Sanyo LC86000 series. Two disjoint address
spaces:

- **ROM**, 64 KB, used for instruction fetch and by `LDC`.
- **RAM**, 512 bytes, used for every operand and every peripheral.

Three clock settings via `OCR`: 32.768 kHz, 600 kHz and 6 MHz. Game mode runs at
600 kHz, which is the runtime's default and what `vmu_audio_set_clock()` exists
to override.

## The opcode map

Flat, no prefixes, and very regular: the low nibble picks the operand form and
the high nibble mostly picks the operation.

|      | lo=0 | lo=1 | lo=2,3 | lo=4-7 | lo=8-F |
|------|------|------|--------|--------|--------|
| hi=0 | NOP | BR r8 | LD d9 | LD @Ri | CALL a12 |
| hi=1 | CALLR r16 | BRF r16 | ST d9 | ST @Ri | CALL a12 |
| hi=2 | CALLF a16 | JMPF a16 | MOV d9 | MOV @Ri | JMP a12 |
| hi=3 | MUL | BE #i8 | BE d9 | BE @Ri | JMP a12 |
| hi=4 | DIV | BNE #i8 | BNE d9 | BNE @Ri | BPC d9,b3,r8 |
| hi=5 | — | — | DBNZ d9 | DBNZ @Ri | BPC d9,b3,r8 |
| hi=6 | PUSH d9 | PUSH d9 | INC d9 | INC @Ri | BP d9,b3,r8 |
| hi=7 | POP d9 | POP d9 | DEC d9 | DEC @Ri | BP d9,b3,r8 |
| hi=8 | BZ r8 | ADD #i8 | ADD d9 | ADD @Ri | BN d9,b3,r8 |
| hi=9 | BNZ r8 | ADDC #i8 | ADDC d9 | ADDC @Ri | BN d9,b3,r8 |
| hi=A | RET | SUB #i8 | SUB d9 | SUB @Ri | NOT1 d9,b3 |
| hi=B | RETI | SUBC #i8 | SUBC d9 | SUBC @Ri | NOT1 d9,b3 |
| hi=C | ROR | LDC | XCH d9 | XCH @Ri | CLR1 d9,b3 |
| hi=D | RORC | OR #i8 | OR d9 | OR @Ri | CLR1 d9,b3 |
| hi=E | ROL | AND #i8 | AND d9 | AND @Ri | SET1 d9,b3 |
| hi=F | ROLC | XOR #i8 | XOR d9 | XOR @Ri | SET1 d9,b3 |

`0x50` and `0x51` are the only undefined bytes. They decode as one-byte traps so
a linear sweep always makes progress.

Two details cost the most bugs:

- **`d8`, the 9th RAM address bit, moves.** In the `lo < 8` forms it is opcode
  bit 0; in the bit-addressed column it is opcode bit 4. So `0x8F 0x4C` is
  `bn $04C,7` while `0x9F 0x4C` is `bn $14C,7` — and `$14C` is `P3`, which is
  the one real code uses to test a button.
- **The three relative forms disagree about everything.** `r8` is signed from
  the next instruction; `r16` is *unsigned*, little-endian, and taken from
  `next - 1` modulo 65536, which is how it still reaches backwards; `a12` keeps
  the top four bits of PC, so it cannot leave its 4K segment; `a16` is big
  endian while `r16` is little endian.

## Flags

`PSW`: `CY AC - IRBK1 IRBK0 OV RAMBK0 P`.

- `CY` — carry out of bit 7, or borrow into it on subtraction.
- `AC` — the same for bit 3.
- `OV` — signed overflow on add and subtract; on `MUL`, set when the product
  exceeds 16 bits; on `DIV`, set when the remainder is zero, and also on divide
  by zero.
- `P` — parity of ACC. **Read only**, so the runtime recomputes it when `PSW` is
  read rather than trying to keep it fresh on every ACC write.

`INC`, `DEC`, `AND`, `OR`, `XOR`, `ROL` and `ROR` touch no flags at all.
`ROLC` and `RORC` touch only `CY`.

`BE` and `BNE` do double duty: besides reporting equality they set `CY` to
`left < right`, which is how VMU code does unsigned comparison.

`MUL` and `DIV` operate on `ACC:C` as a 16-bit value. `MUL` leaves a 24-bit
result in `B:ACC:C`; `DIV` leaves the quotient in `ACC:C` and the remainder in
`B`. **Divide by zero sets `OV`, but the hardware's quotient is undocumented** —
the runtime saturates and says so in a comment.

## Indirection

`@R0`-`@R3` read their pointer byte from RAM. Two things make it more than a
pointer load:

- The four windows `$000-$003`, `$004-$007`, `$008-$00B`, `$00C-$00F` are
  selected by `IRBK1:IRBK0` in `PSW`.
- The pointer is only 8 bits, so the 9th address bit comes from **bit 1 of the
  mode number**. `@R0` and `@R1` therefore always reach RAM, `@R2` and `@R3`
  always reach the SFR half.

## The stack

`SP` points at the topmost element and grows *upward* from `$80`; it resets to
`$7F`, so the first push lands at `$80`. It always lives in RAM bank 0 whatever
`PSW.RAMBK0` says.

`CALL` pushes the low byte of the return address first, then the high byte.
`RET` pops high first, then low.

## What is documented and what is convention

Everything above is published. These are not, and the runtime uses the
conventional LC86 positions instead:

| Register | What the runtime assumes |
|---|---|
| `T0CON` | bit 7 `T0HRUN`, bit 6 `T0LRUN`, bit 5 `T0LONG` |
| `T1CNT` | bit 7 `T1HRUN`, bit 6 `T1LRUN` |
| `BTCR` | bit 0 gates the base-timer interrupt |
| `P3INT` | bit 0 enables, bit 1 is the source flag |

These are the first things to verify against hardware, and they are the reason
the BIOS idle loop is not yet escaped — see [`../ROADMAP.md`](../ROADMAP.md).
`vmu_timer_set_divisor()` trims base-timer cadence without editing any of it.
