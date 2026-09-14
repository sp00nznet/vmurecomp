# Security

## What the attack surface actually is

`vmurecomp` is not a network service and handles no credentials, but it does
something worth taking seriously: **it parses untrusted binary input.**

`lc8670recomp` and `vmurun` take a `.vms`, `.dci` or raw ROM image supplied by
the user, and that file drives a container parser, an instruction decoder, a
recursive-descent analyzer and a code generator. A malformed or hostile image is
the realistic threat, not a malicious user of the API.

Places a bug there would matter:

- **`vms.c`** — header offsets, the `.dci` byte-reversal, and length fields read
  out of the file itself.
- **`decode.c`** — reads up to two bytes past the opcode. It bounds-checks
  against the image size and truncates rather than reading past the end.
- **`analyze.c`** — discovery follows targets taken from the image. Targets are
  16-bit and range-checked against the image before becoming entries; a
  worklist grows with attacker-influenced addresses, so it is bounded by the
  64 KB address space, not by the file.
- **`emit.c`** — writes files into a directory you name.

The runtime (`src/`) only ever indexes fixed-size arrays with masked addresses:
RAM space is masked to 9 bits, XRAM to its bank, and the dispatch table is a
flat 64 KB array indexed by a `uint16_t`. That is deliberate — it means a bad
address is a wrong answer, not an out-of-bounds write.

## What generated code is

`lc8670recomp` emits C that you then compile and run. **Treat the output as you
would any code derived from an untrusted input**: it is a faithful translation
of whatever was in the image, so recompiling a hostile image and running the
result gives that image the same privileges as any other program you build.
There is no sandbox. Recompile things you are willing to run.

The runtime does not `exec`, open sockets, or write outside paths you pass it.

## Reporting a vulnerability

Please **do not** open a public issue for a memory-safety bug.

Use GitHub's private vulnerability reporting — the **Security** tab, *Report a
vulnerability* — on this repository. Include:

- the **bytes** that trigger it, not a game file (see `CONTRIBUTING.md` for why)
- the command line, platform and compiler
- what you observed: crash, hang, out-of-bounds read, unbounded allocation

A minimal reproducer as a hex dump or a short C snippet that builds the input is
ideal, and keeps the report free of anything proprietary.

Expect an acknowledgement within a week. This is a hobby project maintained in
spare time, so please size your expectations accordingly — but parser crashes
are taken seriously and fixed with a regression test.

## Scope

**In scope:** memory safety in the parser, decoder, analyzer or emitter;
unbounded allocation or non-termination on a crafted image; path handling in
the emitter.

**Out of scope:** the recompiled program misbehaving (that is a correctness bug
— open a normal issue); anything requiring you to already be able to run code as
the user; the absence of a sandbox around generated code, which is documented
above and intended.
