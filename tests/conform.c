/* conform.c - the conformance harness.
 *
 * Two corpora, and both report a pass/fail count rather than a bare "tests
 * passed":
 *
 *   1. The opcode space. All 256 opcode bytes, each checked against the
 *      documented encoding: mnemonic, operand form, length, cycle count, and
 *      the resolved target for every control-transfer form. This corpus is
 *      redistributable because it is the published instruction set, so it
 *      always runs, including in CI.
 *
 *   2. Real images, if you have any. Point VMURECOMP_CORPUS (or argv[1]) at a
 *      directory of .vms / .dci / raw dumps and every file is parsed,
 *      discovered and recompiled to a scratch directory. Nothing from that
 *      corpus is committed, and the harness skips with a clear message when
 *      the directory is absent.
 *
 * A regression in corpus 1 fails the build outright. For corpus 2 the pass
 * count is compared against tests/conformance-baseline.txt, which holds a
 * single integer and no derived data.
 */
#include "decode.h"
#include "analyze.h"
#include "emit.h"
#include "vms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passes, failures;

static void check(int ok, const char *what) {
    if (ok) passes++;
    else { failures++; printf("  FAIL %s\n", what); }
}

/* --- corpus 1: the opcode space ---------------------------------------- */

static uint8_t scratch[65536];

static vmu_insn_t decode_at(uint16_t pc, uint8_t b0, uint8_t b1, uint8_t b2) {
    memset(scratch, 0, sizeof(scratch));
    scratch[pc] = b0;
    scratch[(uint16_t)(pc + 1)] = b1;
    scratch[(uint16_t)(pc + 2)] = b2;
    vmu_insn_t in;
    vmu_decode(scratch, sizeof(scratch), pc, &in);
    return in;
}

/* What each opcode byte must decode to. Derived from the published opcode map
 * by the same rules the decoder implements, but spelled out independently here
 * so a mistake in one shows up as a mismatch rather than agreeing with itself. */
static void expect_for(uint8_t b, vmu_op_t *op, uint8_t *len, uint8_t *cyc) {
    int lo = b & 0x0F, hi = b >> 4;

    if (lo >= 8) {
        switch (b >> 5) {
        case 0:  *op = OP_CALL; *len = 2; *cyc = 2; return;
        case 1:  *op = OP_JMP;  *len = 2; *cyc = 2; return;
        case 2:  *op = OP_BPC;  *len = 3; *cyc = 2; return;
        case 3:  *op = OP_BP;   *len = 3; *cyc = 2; return;
        case 4:  *op = OP_BN;   *len = 3; *cyc = 2; return;
        case 5:  *op = OP_NOT1; *len = 2; *cyc = 1; return;
        case 6:  *op = OP_CLR1; *len = 2; *cyc = 1; return;
        default: *op = OP_SET1; *len = 2; *cyc = 1; return;
        }
    }

    static const vmu_op_t ROW[16] = {
        OP_LD, OP_ST, OP_MOV, OP_BE, OP_BNE, OP_DBNZ, OP_INC, OP_DEC,
        OP_ADD, OP_ADDC, OP_SUB, OP_SUBC, OP_XCH, OP_OR, OP_AND, OP_XOR
    };
    /* column 0/1 of each row is a one-off */
    static const struct { uint8_t b; vmu_op_t op; uint8_t len, cyc; } SPECIAL[] = {
        { 0x00, OP_NOP,   1, 1 }, { 0x01, OP_BR,    2, 2 },
        { 0x10, OP_CALLR, 3, 4 }, { 0x11, OP_BRF,   3, 4 },
        { 0x20, OP_CALLF, 3, 2 }, { 0x21, OP_JMPF,  3, 2 },
        { 0x30, OP_MUL,   1, 7 }, { 0x40, OP_DIV,   1, 7 },
        { 0x50, OP_INVALID, 1, 1 }, { 0x51, OP_INVALID, 1, 1 },
        { 0x60, OP_PUSH,  2, 2 }, { 0x61, OP_PUSH,  2, 2 },
        { 0x70, OP_POP,   2, 2 }, { 0x71, OP_POP,   2, 2 },
        { 0x80, OP_BZ,    2, 2 }, { 0x90, OP_BNZ,   2, 2 },
        { 0xA0, OP_RET,   1, 2 }, { 0xB0, OP_RETI,  1, 2 },
        { 0xC0, OP_ROR,   1, 1 }, { 0xC1, OP_LDC,   1, 2 },
        { 0xD0, OP_RORC,  1, 1 }, { 0xE0, OP_ROL,   1, 1 },
        { 0xF0, OP_ROLC,  1, 1 },
    };
    for (size_t i = 0; i < sizeof(SPECIAL) / sizeof(SPECIAL[0]); i++) {
        if (SPECIAL[i].b == b) {
            *op = SPECIAL[i].op; *len = SPECIAL[i].len; *cyc = SPECIAL[i].cyc;
            return;
        }
    }

    *op = ROW[hi];
    *cyc = (hi == 2 || hi == 3 || hi == 4 || hi == 5) ? 2 : 1;

    if (lo < 2) {                       /* immediate form */
        *len = (hi == 3 || hi == 4) ? 3 : 2;
        if (hi == 3 || hi == 4) *cyc = 2;
    } else if (lo < 4) {                /* direct d9 */
        *len = (hi == 2 || hi == 3 || hi == 4 || hi == 5) ? 3 : 2;
    } else {                            /* indirect @Ri */
        if (hi == 2)      { *len = 2; *cyc = 1; }
        else if (hi == 3 || hi == 4) *len = 3;
        else if (hi == 5) *len = 2;
        else              *len = 1;
    }
}

static void corpus_opcodes(void) {
    printf("corpus: opcode space (256 encodings)\n");

    for (int b = 0; b < 256; b++) {
        vmu_op_t xop; uint8_t xlen, xcyc;
        expect_for((uint8_t)b, &xop, &xlen, &xcyc);

        vmu_insn_t in = decode_at(0x400, (uint8_t)b, 0x12, 0x34);
        char what[64];

        snprintf(what, sizeof(what), "opcode %02X mnemonic", b);
        check(in.op == xop, what);
        snprintf(what, sizeof(what), "opcode %02X length", b);
        check(in.len == xlen, what);
        snprintf(what, sizeof(what), "opcode %02X cycles", b);
        check(in.cycles == xcyc, what);
    }

    /* Target resolution, one case per addressing mode. */
    check(decode_at(0x1000, 0x01, 0x10, 0).target == 0x1012, "BR forward");
    check(decode_at(0x1000, 0x01, 0xF0, 0).target == 0x0FF2, "BR backward");
    check(decode_at(0x1000, 0x08, 0x34, 0).target == 0x1034, "CALL a12 low");
    check(decode_at(0x1000, 0x1F, 0x34, 0).target == 0x1F34, "CALL a12 high");
    check(decode_at(0x2000, 0x28, 0x00, 0).target == 0x2000, "JMP a12 self");
    check(decode_at(0x0000, 0x20, 0x12, 0x34).target == 0x1234, "CALLF big endian");
    check(decode_at(0x0000, 0x21, 0xFF, 0xFE).target == 0xFFFE, "JMPF big endian");
    check(decode_at(0x1000, 0x11, 0x02, 0x00).target == 0x1004, "BRF little endian");
    check(decode_at(0x1000, 0x10, 0x00, 0x00).target == 0x1002, "CALLR base");
    check(decode_at(0x1000, 0x11, 0xFF, 0xFF).target == 0x1001, "BRF wraps");
    check(decode_at(0x1000, 0x68, 0x4C, 0x05).target == 0x1008, "BP r8");
    check(decode_at(0x1000, 0x52, 0x30, 0x05).target == 0x1008, "DBNZ d9 r8");
    check(decode_at(0x1000, 0x55, 0x05, 0).target == 0x1007, "DBNZ @Ri r8");

    /* d9 assembly from the two different bit positions. */
    check(decode_at(0, 0x03, 0x44, 0).d9 == 0x144, "d9 from opcode bit 0");
    check(decode_at(0, 0xFF, 0x44, 0).d9 == 0x144, "d9 from opcode bit 4");
    check(decode_at(0, 0xFF, 0x44, 0).b3 == 7, "b3 from opcode bits 2:0");
    check(decode_at(0, 0x07, 0, 0).ri == 3, "@Ri from opcode bits 1:0");

    /* A sweep must tile the image exactly. */
    for (size_t i = 0; i < sizeof(scratch); i++) scratch[i] = (uint8_t)(i * 31 + 7);
    size_t pc = 0;
    int tiled = 1;
    while (pc < sizeof(scratch)) {
        vmu_insn_t in;
        int len = vmu_decode(scratch, sizeof(scratch), (uint16_t)pc, &in);
        if (len <= 0 || pc + (size_t)len > sizeof(scratch)) { tiled = 0; break; }
        pc += (size_t)len;
    }
    check(tiled && pc == sizeof(scratch), "linear sweep tiles 64 KB exactly");
}

/* --- corpus 2: real images, if present ---------------------------------- */

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    *out_size = got;
    return buf;
}

static int recompile_one(const char *path) {
    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) return 0;

    vms_info_t img;
    int ok = 0;
    if (vms_parse(data, size, &img) == 0) {
        vmu_prog_t prog;
        if (vmu_discover(img.rom, img.rom_size, NULL, 0, &prog) == 0) {
            /* An image passes when discovery found real code and reached no
             * undefined opcode along any path it will emit. */
            ok = (prog.n_funcs > 0 && prog.n_insns > 0 && prog.n_invalid == 0);
            printf("  %-40s %4zu funcs %6zu insns %s\n",
                   path, prog.n_funcs, prog.n_insns, ok ? "PASS" : "FAIL");
            vmu_prog_free(&prog);
        }
    }
    vms_free(&img);
    free(data);
    return ok;
}

static int read_baseline(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = -1;
    if (fscanf(f, "%d", &n) != 1) n = -1;
    fclose(f);
    return n;
}

static int corpus_images(const char *dir, const char *baseline_path) {
    if (!dir || !*dir) {
        printf("\ncorpus: images - SKIPPED\n");
        printf("  Set VMURECOMP_CORPUS to a directory of .vms / .dci / raw dumps\n"
               "  to run the image corpus. Nothing is redistributed with this\n"
               "  repository, so you supply your own.\n");
        return 0;
    }

    printf("\ncorpus: images from %s\n", dir);

    /* A manifest keeps the harness free of platform directory APIs and lets a
     * corpus live anywhere; one path per line, blank lines and # ignored. */
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s/manifest.txt", dir);
    FILE *m = fopen(manifest, "rb");
    if (!m) {
        printf("  no manifest.txt in %s - SKIPPED\n", dir);
        printf("  Create one with a relative path per line.\n");
        return 0;
    }

    int pass = 0, total = 0;
    char line[512];
    while (fgets(line, sizeof(line), m)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t n = strlen(s);
        while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
        if (!*s || *s == '#') continue;

        char full[1600];
        snprintf(full, sizeof(full), "%s/%s", dir, s);
        total++;
        pass += recompile_one(full);
    }
    fclose(m);

    printf("  images: %d of %d passed\n", pass, total);

    int base = read_baseline(baseline_path);
    if (base < 0) {
        printf("  no baseline at %s; record %d to start tracking\n", baseline_path, pass);
        return 0;
    }
    printf("  baseline: %d\n", base);
    if (pass < base) {
        printf("  REGRESSION: %d fewer images recompile than the baseline\n", base - pass);
        return 1;
    }
    if (pass > base) printf("  improvement: update the baseline to %d\n", pass);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : getenv("VMURECOMP_CORPUS");
    const char *baseline = (argc > 2) ? argv[2] : "conformance-baseline.txt";

    corpus_opcodes();
    printf("  opcodes: %d passed, %d failed\n", passes, failures);

    int regressed = corpus_images(dir, baseline);

    printf("\nconformance: %d passed, %d failed\n", passes, failures);
    return (failures || regressed) ? 1 : 0;
}
