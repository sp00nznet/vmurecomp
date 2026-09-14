# Tests

Everything here runs on hand-written synthetic input. No dump of any kind is
required to run the suite, and nothing derived from a proprietary binary is
committed.

| Test | What it covers |
|---|---|
| `test_decode` | the decoder against the documented LC8670 encodings |
| `test_runtime` | CPU flag semantics, the RAM/SFR map, the LCD layout |
| `test_pipeline` | discovery and emission on a hand-assembled ROM |
| `conform` | the conformance harness - see below |

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Conformance harness

Two corpora, both reporting a pass/fail count rather than a bare "tests passed".

**Corpus 1 - the opcode space.** All 256 opcode bytes checked against the
published encoding for mnemonic, operand form, length and cycle count, plus
target resolution for every control-transfer mode. The expectations are spelled
out independently of the decoder, so a mistake in one shows up as a mismatch
instead of agreeing with itself. Redistributable, because it is the instruction
set, so it always runs in CI.

**Corpus 2 - real images.** Opt-in and skipped with a clear message when absent,
because VMU images cannot be redistributed. Point it at a directory of your own:

```sh
export VMURECOMP_CORPUS=/path/to/images
build/tests/conform "$VMURECOMP_CORPUS" tests/conformance-baseline.txt
```

The directory needs a `manifest.txt` listing one relative path per line; `#`
starts a comment. Each image is parsed, discovered and recompiled, and an image
passes when discovery finds real code and reaches no undefined opcode.

`conformance-baseline.txt` holds a single integer: the number of images expected
to pass. The harness exits non-zero when fewer pass, so a regression fails the
build rather than only a total failure. It holds no data derived from any image.
It is 0 here because the public CI has no corpus.
