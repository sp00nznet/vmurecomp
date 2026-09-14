/* test_decode.c - the decoder against the documented LC8670 encodings.
 *
 * Every check here is an encoding stated in the VMS CPU documentation, written
 * out by hand. No game data is involved.
 */
#include "decode.h"
/* Tests must assert even in a release build, where CMake defines NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static vmu_insn_t at(const uint8_t *rom, size_t n, uint16_t pc) {
    vmu_insn_t in;
    int len = vmu_decode(rom, n, pc, &in);
    assert(len > 0);
    return in;
}

/* Decode a 1-3 byte instruction placed at `pc` inside a 64 KB scratch image. */
static vmu_insn_t enc(uint16_t pc, uint8_t b0, uint8_t b1, uint8_t b2) {
    static uint8_t rom[65536];
    memset(rom, 0, sizeof(rom));
    rom[pc] = b0;
    rom[(uint16_t)(pc + 1)] = b1;
    rom[(uint16_t)(pc + 2)] = b2;
    return at(rom, sizeof(rom), pc);
}

static void test_lengths_and_ops(void) {
    /* the no-operand forms */
    assert(enc(0, 0x00, 0, 0).op == OP_NOP  && enc(0, 0x00, 0, 0).len == 1);
    assert(enc(0, 0x30, 0, 0).op == OP_MUL  && enc(0, 0x30, 0, 0).cycles == 7);
    assert(enc(0, 0x40, 0, 0).op == OP_DIV  && enc(0, 0x40, 0, 0).cycles == 7);
    assert(enc(0, 0xA0, 0, 0).op == OP_RET  && enc(0, 0xA0, 0, 0).is_ret);
    assert(enc(0, 0xB0, 0, 0).op == OP_RETI && enc(0, 0xB0, 0, 0).is_ret);
    assert(enc(0, 0xC0, 0, 0).op == OP_ROR);
    assert(enc(0, 0xC1, 0, 0).op == OP_LDC  && enc(0, 0xC1, 0, 0).len == 1);
    assert(enc(0, 0xD0, 0, 0).op == OP_RORC);
    assert(enc(0, 0xE0, 0, 0).op == OP_ROL);
    assert(enc(0, 0xF0, 0, 0).op == OP_ROLC);

    /* 0x50 and 0x51 are the only undefined opcodes */
    assert(enc(0, 0x50, 0, 0).op == OP_INVALID);
    assert(enc(0, 0x51, 0, 0).op == OP_INVALID);

    /* d9 forms: bit 0 of the opcode supplies the 9th address bit */
    vmu_insn_t i = enc(0, 0x02, 0x34, 0);
    assert(i.op == OP_LD && i.mode == M_D9 && i.d9 == 0x034 && i.len == 2);
    i = enc(0, 0x03, 0x34, 0);
    assert(i.d9 == 0x134);

    i = enc(0, 0x13, 0x01, 0);
    assert(i.op == OP_ST && i.d9 == 0x101);

    /* indirect forms are one byte and carry the register number in bits 1:0 */
    i = enc(0, 0x06, 0, 0);
    assert(i.op == OP_LD && i.mode == M_IND && i.ri == 2 && i.len == 1);

    /* PUSH/POP share a row with INC/DEC */
    assert(enc(0, 0x60, 0x04, 0).op == OP_PUSH);
    assert(enc(0, 0x61, 0x04, 0).op == OP_PUSH);
    assert(enc(0, 0x62, 0x04, 0).op == OP_INC);
    assert(enc(0, 0x66, 0x00, 0).op == OP_INC);
    assert(enc(0, 0x70, 0x04, 0).op == OP_POP);
    assert(enc(0, 0x72, 0x04, 0).op == OP_DEC);

    /* MOV is 3 bytes direct, 2 bytes indirect */
    i = enc(0, 0x22, 0x25, 0xFF);
    assert(i.op == OP_MOV && i.len == 3 && i.d9 == 0x025 && i.i8 == 0xFF);
    i = enc(0, 0x26, 0x80, 0);
    assert(i.op == OP_MOV && i.len == 2 && i.ri == 2 && i.i8 == 0x80);

    /* arithmetic: immediate, direct, indirect */
    assert(enc(0, 0x81, 0x05, 0).op == OP_ADD && enc(0, 0x81, 0x05, 0).i8 == 0x05);
    assert(enc(0, 0x83, 0x00, 0).op == OP_ADD && enc(0, 0x83, 0x00, 0).d9 == 0x100);
    assert(enc(0, 0x87, 0, 0).op == OP_ADD    && enc(0, 0x87, 0, 0).ri == 3);
    assert(enc(0, 0x91, 0, 0).op == OP_ADDC);
    assert(enc(0, 0xA1, 0, 0).op == OP_SUB);
    assert(enc(0, 0xB1, 0, 0).op == OP_SUBC);
    assert(enc(0, 0xD1, 0, 0).op == OP_OR);
    assert(enc(0, 0xE1, 0, 0).op == OP_AND);
    assert(enc(0, 0xF1, 0, 0).op == OP_XOR);
    assert(enc(0, 0xC3, 0x00, 0).op == OP_XCH && enc(0, 0xC3, 0x00, 0).d9 == 0x100);
}

static void test_bit_column(void) {
    /* The lo>=8 column: d8 is opcode bit 4, b3 is opcode bits 2:0. */
    vmu_insn_t i = enc(0, 0xE8, 0x01, 0);
    assert(i.op == OP_SET1 && i.len == 2 && i.d9 == 0x001 && i.b3 == 0);

    i = enc(0, 0xFF, 0x44, 0);
    assert(i.op == OP_SET1 && i.d9 == 0x144 && i.b3 == 7);

    i = enc(0, 0xC8, 0x10, 0);
    assert(i.op == OP_CLR1 && i.d9 == 0x010 && i.b3 == 0);

    i = enc(0, 0xAF, 0x20, 0);
    assert(i.op == OP_NOT1 && i.d9 == 0x020 && i.b3 == 7);

    /* the three-byte bit-test branches */
    i = enc(0x100, 0x68, 0x4C, 0x10);
    assert(i.op == OP_BP && i.len == 3 && i.d9 == 0x04C && i.b3 == 0);
    assert(i.target == (uint16_t)(0x103 + 0x10));

    /* d8 lives in opcode bit 4, so 0x8F reaches $04C and 0x9F reaches $14C -
     * the latter being the one real code uses, to test a button in P3. */
    i = enc(0x100, 0x8F, 0x4C, 0xFE);
    assert(i.op == OP_BN && i.d9 == 0x04C && i.b3 == 7);
    assert(i.target == (uint16_t)(0x103 - 2));

    i = enc(0x100, 0x9F, 0x4C, 0xFE);
    assert(i.op == OP_BN && i.d9 == 0x14C && i.b3 == 7);

    i = enc(0x100, 0x48, 0x00, 0x00);
    assert(i.op == OP_BPC && i.len == 3);
}

static void test_control_flow(void) {
    /* BR: signed 8-bit offset from the following instruction */
    vmu_insn_t i = enc(0x1000, 0x01, 0x7F, 0);
    assert(i.op == OP_BR && i.target == 0x1081 && i.ends_block);
    i = enc(0x1000, 0x01, 0x80, 0);
    assert(i.target == (uint16_t)(0x1002 - 128));

    /* BZ / BNZ use the same form */
    assert(enc(0x1000, 0x80, 0x02, 0).target == 0x1004);
    assert(enc(0x1000, 0x90, 0x02, 0).target == 0x1004);

    /* CALL a12: a11 in opcode bit 4, a10..a8 in bits 2:0, PC's top nibble kept */
    i = enc(0x1000, 0x08, 0x34, 0);
    assert(i.op == OP_CALL && i.is_call && i.target == 0x1034 && i.len == 2);
    i = enc(0x1000, 0x1F, 0x34, 0);
    assert(i.target == 0x1F34);

    /* JMP a12 occupies the same column two rows down */
    i = enc(0x1000, 0x28, 0x34, 0);
    assert(i.op == OP_JMP && i.target == 0x1034 && i.ends_block);
    i = enc(0x1000, 0x3F, 0x34, 0);
    assert(i.target == 0x1F34);

    /* a16 forms are big endian */
    i = enc(0, 0x20, 0x12, 0x34);
    assert(i.op == OP_CALLF && i.target == 0x1234 && i.len == 3);
    i = enc(0, 0x21, 0xAB, 0xCD);
    assert(i.op == OP_JMPF && i.target == 0xABCD);

    /* r16 forms are unsigned little endian, relative to (next - 1) */
    i = enc(0x1000, 0x11, 0x01, 0x00);
    assert(i.op == OP_BRF && i.len == 3 && i.cycles == 4 && i.target == 0x1003);
    i = enc(0x1000, 0x10, 0x00, 0x00);
    assert(i.op == OP_CALLR && i.is_call && i.target == 0x1002);
    /* wrapping backwards is legal: the addition is modulo 65536 */
    i = enc(0x1000, 0x11, 0xFF, 0xFF);
    assert(i.target == (uint16_t)(0x1002 - 1));

    /* compare-and-branch */
    i = enc(0x1000, 0x31, 0x05, 0x10);
    assert(i.op == OP_BE && i.mode == M_I8_R8 && i.i8 == 5 && i.target == 0x1013);
    i = enc(0x1000, 0x42, 0x30, 0x10);
    assert(i.op == OP_BNE && i.mode == M_D9_R8 && i.d9 == 0x030);
    i = enc(0x1000, 0x37, 0x05, 0x10);
    assert(i.op == OP_BE && i.mode == M_IND_I8_R8 && i.ri == 3 && i.i8 == 5);

    /* DBNZ */
    i = enc(0x1000, 0x52, 0x30, 0x10);
    assert(i.op == OP_DBNZ && i.len == 3 && i.d9 == 0x030 && i.target == 0x1013);
    i = enc(0x1000, 0x55, 0x10, 0);
    assert(i.op == OP_DBNZ && i.len == 2 && i.ri == 1 && i.target == 0x1012);
}

/* Every opcode but the two documented holes must decode, and no instruction
 * may claim a length outside 1..3. */
static void test_full_opcode_coverage(void) {
    int undefined = 0;
    for (int b = 0; b < 256; b++) {
        vmu_insn_t i = enc(0x200, (uint8_t)b, 0x55, 0xAA);
        assert(i.len >= 1 && i.len <= 3);
        assert(i.cycles >= 1);
        if (i.op == OP_INVALID) undefined++;
    }
    assert(undefined == 2);
}

/* A sweep must consume the image exactly, never overrun it. */
static void test_sweep_is_total(void) {
    static uint8_t rom[512];
    for (size_t i = 0; i < sizeof(rom); i++) rom[i] = (uint8_t)(i * 7 + 3);

    size_t pc = 0, n = 0;
    while (pc < sizeof(rom)) {
        vmu_insn_t in;
        int len = vmu_decode(rom, sizeof(rom), (uint16_t)pc, &in);
        assert(len > 0);
        assert(pc + (size_t)len <= sizeof(rom));
        pc += (size_t)len;
        n++;
    }
    assert(pc == sizeof(rom));
    assert(n > 0);
}

static void test_format(void) {
    char buf[64];
    vmu_insn_t i = enc(0, 0x02, 0x34, 0);
    vmu_format(&i, buf, sizeof(buf));
    assert(strcmp(buf, "ld $034") == 0);

    i = enc(0, 0x06, 0, 0);
    vmu_format(&i, buf, sizeof(buf));
    assert(strcmp(buf, "ld @R2") == 0);

    i = enc(0, 0xE8, 0x01, 0);
    vmu_format(&i, buf, sizeof(buf));
    assert(strcmp(buf, "set1 $001,0") == 0);
}

int main(void) {
    test_lengths_and_ops();
    test_bit_column();
    test_control_flow();
    test_full_opcode_coverage();
    test_sweep_is_total();
    test_format();
    printf("test_decode: ok\n");
    return 0;
}
