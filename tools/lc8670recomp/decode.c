/* decode.c - LC8670 instruction decoder.
 *
 * The opcode map has a very regular shape, which is why this is a switch and
 * not a 256-entry table: the low nibble selects the operand form and the high
 * nibble (mostly) the operation.
 *
 *        lo=0      lo=1      lo=2,3    lo=4-7    lo=8-F
 *   hi=0 NOP       BR  r8    LD   d9   LD   @Ri  CALL a12
 *   hi=1 CALLR r16 BRF r16   ST   d9   ST   @Ri  CALL a12
 *   hi=2 CALLF a16 JMPF a16  MOV  d9   MOV  @Ri  JMP  a12
 *   hi=3 MUL       BE  #i8   BE   d9   BE   @Ri  JMP  a12
 *   hi=4 DIV       BNE #i8   BNE  d9   BNE  @Ri  BPC  d9,b3,r8
 *   hi=5 -         -         DBNZ d9   DBNZ @Ri  BPC  d9,b3,r8
 *   hi=6 PUSH d9   PUSH d9   INC  d9   INC  @Ri  BP   d9,b3,r8
 *   hi=7 POP  d9   POP  d9   DEC  d9   DEC  @Ri  BP   d9,b3,r8
 *   hi=8 BZ   r8   ADD  #i8  ADD  d9   ADD  @Ri  BN   d9,b3,r8
 *   hi=9 BNZ  r8   ADDC #i8  ADDC d9   ADDC @Ri  BN   d9,b3,r8
 *   hi=A RET       SUB  #i8  SUB  d9   SUB  @Ri  NOT1 d9,b3
 *   hi=B RETI      SUBC #i8  SUBC d9   SUBC @Ri  NOT1 d9,b3
 *   hi=C ROR       LDC       XCH  d9   XCH  @Ri  CLR1 d9,b3
 *   hi=D RORC      OR   #i8  OR   d9   OR   @Ri  CLR1 d9,b3
 *   hi=E ROL       AND  #i8  AND  d9   AND  @Ri  SET1 d9,b3
 *   hi=F ROLC      XOR  #i8  XOR  d9   XOR  @Ri  SET1 d9,b3
 *
 * For lo>=8 the d8/a11 bit lives in bit 4 of the opcode and bit 3 is always
 * set, which is what separates that column from the lo<8 forms.
 *
 * 0x50 and 0x51 are the only undefined opcodes.
 */
#include "decode.h"
#include <stdio.h>
#include <string.h>

static const char *const MNEMONIC[OP__COUNT] = {
    "???",
    "nop", "br", "brf", "ld", "st", "mov", "ldc", "xch",
    "push", "pop", "call", "callf", "callr", "ret", "reti",
    "jmp", "jmpf", "bz", "bnz", "be", "bne", "dbnz",
    "bp", "bpc", "bn",
    "add", "addc", "sub", "subc", "inc", "dec", "mul", "div",
    "and", "or", "xor", "rol", "rolc", "ror", "rorc",
    "set1", "clr1", "not1"
};

const char *vmu_mnemonic(vmu_op_t op) {
    return (op > 0 && op < OP__COUNT) ? MNEMONIC[op] : MNEMONIC[0];
}

/* Fill in op/mode/cycles for one opcode byte; length is derived from mode. */
static void classify(uint8_t b, vmu_op_t *op, vmu_mode_t *mode, uint8_t *cyc) {
    *cyc = 1;

    if ((b & 0x0F) >= 0x08) {
        /* the bit-addressed / 12-bit-absolute column */
        switch (b >> 5) {
        case 0:  *op = OP_CALL; *mode = M_A12;      *cyc = 2; return;
        case 1:  *op = OP_JMP;  *mode = M_A12;      *cyc = 2; return;
        case 2:  *op = OP_BPC;  *mode = M_D9_B3_R8; *cyc = 2; return;
        case 3:  *op = OP_BP;   *mode = M_D9_B3_R8; *cyc = 2; return;
        case 4:  *op = OP_BN;   *mode = M_D9_B3_R8; *cyc = 2; return;
        case 5:  *op = OP_NOT1; *mode = M_D9_B3;    return;
        case 6:  *op = OP_CLR1; *mode = M_D9_B3;    return;
        default: *op = OP_SET1; *mode = M_D9_B3;    return;
        }
    }

    switch (b) {
    case 0x00: *op = OP_NOP;   *mode = M_NONE; return;
    case 0x01: *op = OP_BR;    *mode = M_R8;   *cyc = 2; return;
    case 0x10: *op = OP_CALLR; *mode = M_R16;  *cyc = 4; return;
    case 0x11: *op = OP_BRF;   *mode = M_R16;  *cyc = 4; return;
    case 0x20: *op = OP_CALLF; *mode = M_A16;  *cyc = 2; return;
    case 0x21: *op = OP_JMPF;  *mode = M_A16;  *cyc = 2; return;
    case 0x30: *op = OP_MUL;   *mode = M_NONE; *cyc = 7; return;
    case 0x40: *op = OP_DIV;   *mode = M_NONE; *cyc = 7; return;
    case 0x50: case 0x51:
               *op = OP_INVALID; *mode = M_NONE; return;
    case 0x80: *op = OP_BZ;    *mode = M_R8;   *cyc = 2; return;
    case 0x90: *op = OP_BNZ;   *mode = M_R8;   *cyc = 2; return;
    case 0xA0: *op = OP_RET;   *mode = M_NONE; *cyc = 2; return;
    case 0xB0: *op = OP_RETI;  *mode = M_NONE; *cyc = 2; return;
    case 0xC0: *op = OP_ROR;   *mode = M_NONE; return;
    case 0xC1: *op = OP_LDC;   *mode = M_NONE; *cyc = 2; return;
    case 0xD0: *op = OP_RORC;  *mode = M_NONE; return;
    case 0xE0: *op = OP_ROL;   *mode = M_NONE; return;
    case 0xF0: *op = OP_ROLC;  *mode = M_NONE; return;
    default: break;
    }

    /* Regular rows: low nibble picks the operand form.
     *   lo 0,1 -> immediate form (or PUSH/POP in rows 6,7)
     *   lo 2,3 -> direct d9
     *   lo 4-7 -> indirect @Ri
     */
    {
        int lo = b & 0x07;
        vmu_mode_t m_imm = M_NONE, m_dir = M_D9, m_ind = M_IND;

        switch (b >> 4) {
        case 0x0: *op = OP_LD;   break;
        case 0x1: *op = OP_ST;   break;
        case 0x2: *op = OP_MOV;  m_dir = M_D9_I8;    m_ind = M_IND_I8;     *cyc = 2; break;
        case 0x3: *op = OP_BE;   m_imm = M_I8_R8;    m_dir = M_D9_R8;
                                 m_ind = M_IND_I8_R8; *cyc = 2; break;
        case 0x4: *op = OP_BNE;  m_imm = M_I8_R8;    m_dir = M_D9_R8;
                                 m_ind = M_IND_I8_R8; *cyc = 2; break;
        case 0x5: *op = OP_DBNZ; m_dir = M_D9_R8;    m_ind = M_IND_R8;     *cyc = 2; break;
        case 0x6: *op = OP_INC;  break;
        case 0x7: *op = OP_DEC;  break;
        case 0x8: *op = OP_ADD;  m_imm = M_I8; break;
        case 0x9: *op = OP_ADDC; m_imm = M_I8; break;
        case 0xA: *op = OP_SUB;  m_imm = M_I8; break;
        case 0xB: *op = OP_SUBC; m_imm = M_I8; break;
        case 0xC: *op = OP_XCH;  break;
        case 0xD: *op = OP_OR;   m_imm = M_I8; break;
        case 0xE: *op = OP_AND;  m_imm = M_I8; break;
        default:  *op = OP_XOR;  m_imm = M_I8; break;
        }

        /* PUSH/POP share their row with INC/DEC: lo 0,1 is the stack form. */
        if (*op == OP_INC && lo < 2) { *op = OP_PUSH; *cyc = 2; *mode = M_D9; return; }
        if (*op == OP_DEC && lo < 2) { *op = OP_POP;  *cyc = 2; *mode = M_D9; return; }

        /* MOV #i8,@Rj is 1 cycle, unlike the d9 form. */
        if (*op == OP_MOV && lo >= 4) *cyc = 1;

        *mode = (lo < 2) ? m_imm : (lo < 4 ? m_dir : m_ind);
        if (*mode == M_NONE) *op = OP_INVALID;
    }
}

static uint8_t mode_len(vmu_mode_t m) {
    switch (m) {
    case M_NONE:
    case M_IND:
        return 1;
    case M_I8:
    case M_D9:
    case M_R8:
    case M_A12:
    case M_D9_B3:
    case M_IND_R8:
    case M_IND_I8:
        return 2;
    default:
        return 3;
    }
}

int vmu_decode(const uint8_t *rom, size_t rom_size, uint16_t pc, vmu_insn_t *out) {
    if (!rom || !out || (size_t)pc >= rom_size) return 0;

    memset(out, 0, sizeof(*out));
    uint8_t b = rom[pc];
    out->pc = pc;
    out->opcode = b;
    classify(b, &out->op, &out->mode, &out->cycles);
    out->len = mode_len(out->mode);

    /* Truncated tail: report the opcode but never read past the image. */
    if ((size_t)pc + out->len > rom_size) {
        out->op = OP_INVALID;
        out->mode = M_NONE;
        out->len = 1;
        out->ends_block = 1;
        return 1;
    }

    uint8_t b1 = (out->len > 1) ? rom[pc + 1] : 0;
    uint8_t b2 = (out->len > 2) ? rom[pc + 2] : 0;
    uint16_t next = (uint16_t)(pc + out->len);

    /* d8, the 9th RAM address bit, is opcode bit 0 for the lo<8 forms and
     * opcode bit 4 for the bit-addressed column. */
    int hi_col = (b & 0x0F) >= 0x08;
    uint16_t d8 = hi_col ? (uint16_t)((b >> 4) & 1) : (uint16_t)(b & 1);

    out->ri = (uint8_t)(b & 0x03);
    out->b3 = (uint8_t)(b & 0x07);

    switch (out->mode) {
    case M_NONE:
    case M_IND:
        break;

    case M_I8:
        out->i8 = b1;
        break;

    case M_D9:
    case M_D9_B3:
        out->d9 = (uint16_t)((d8 << 8) | b1);
        break;

    case M_A12: {
        /* a11 in opcode bit 4, a10..a8 in opcode bits 2..0, a7..a0 follow.
         * The upper 4 bits of PC are unchanged, so the jump stays in-segment. */
        uint16_t a12 = (uint16_t)(((uint16_t)((b >> 4) & 1) << 11) |
                                  ((uint16_t)(b & 0x07) << 8) | b1);
        out->target = (uint16_t)((next & 0xF000) | a12);
        out->has_target = 1;
        break;
    }

    case M_A16:
        out->target = (uint16_t)((b1 << 8) | b2);        /* big endian */
        out->has_target = 1;
        break;

    case M_R8:
        out->target = (uint16_t)(next + (int8_t)b1);
        out->has_target = 1;
        break;

    case M_R16:
        /* unsigned little-endian offset, added to (next - 1) mod 65536 */
        out->target = (uint16_t)(next - 1 + (uint16_t)((b2 << 8) | b1));
        out->has_target = 1;
        break;

    case M_D9_B3_R8:
    case M_D9_R8:
        out->d9 = (uint16_t)((d8 << 8) | b1);
        out->target = (uint16_t)(next + (int8_t)b2);
        out->has_target = 1;
        break;

    case M_IND_R8:
        out->target = (uint16_t)(next + (int8_t)b1);
        out->has_target = 1;
        break;

    case M_I8_R8:
    case M_IND_I8_R8:
        out->i8 = b1;
        out->target = (uint16_t)(next + (int8_t)b2);
        out->has_target = 1;
        break;

    case M_D9_I8:
        out->d9 = (uint16_t)((d8 << 8) | b1);
        out->i8 = b2;
        break;

    case M_IND_I8:
        out->i8 = b1;
        break;
    }

    out->is_call = (uint8_t)(out->op == OP_CALL || out->op == OP_CALLF ||
                             out->op == OP_CALLR);
    out->is_ret  = (uint8_t)(out->op == OP_RET || out->op == OP_RETI);
    out->ends_block = (uint8_t)(out->is_ret ||
                                out->op == OP_JMP || out->op == OP_JMPF ||
                                out->op == OP_BR  || out->op == OP_BRF  ||
                                out->op == OP_INVALID);
    return out->len;
}

void vmu_format(const vmu_insn_t *in, char *buf, size_t buflen) {
    const char *m = vmu_mnemonic(in->op);
    switch (in->mode) {
    case M_NONE:      snprintf(buf, buflen, "%s", m); break;
    case M_I8:        snprintf(buf, buflen, "%s #$%02X", m, in->i8); break;
    case M_D9:        snprintf(buf, buflen, "%s $%03X", m, in->d9); break;
    case M_IND:       snprintf(buf, buflen, "%s @R%u", m, in->ri); break;
    case M_A12:
    case M_A16:
    case M_R8:
    case M_R16:       snprintf(buf, buflen, "%s $%04X", m, in->target); break;
    case M_D9_B3:     snprintf(buf, buflen, "%s $%03X,%u", m, in->d9, in->b3); break;
    case M_D9_B3_R8:  snprintf(buf, buflen, "%s $%03X,%u,$%04X", m, in->d9, in->b3, in->target); break;
    case M_D9_R8:     snprintf(buf, buflen, "%s $%03X,$%04X", m, in->d9, in->target); break;
    case M_IND_R8:    snprintf(buf, buflen, "%s @R%u,$%04X", m, in->ri, in->target); break;
    case M_I8_R8:     snprintf(buf, buflen, "%s #$%02X,$%04X", m, in->i8, in->target); break;
    case M_D9_I8:     snprintf(buf, buflen, "%s #$%02X,$%03X", m, in->i8, in->d9); break;
    case M_IND_I8:    snprintf(buf, buflen, "%s #$%02X,@R%u", m, in->i8, in->ri); break;
    case M_IND_I8_R8: snprintf(buf, buflen, "%s @R%u,#$%02X,$%04X", m, in->ri, in->i8, in->target); break;
    }
}
