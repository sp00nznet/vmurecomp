/* decode.h - Sanyo LC8670 ("Potato") instruction decoder.
 *
 * The VMU CPU is code-compatible with the Sanyo LC86000 series. The encoding
 * is a flat 8-bit opcode map with no prefixes: 254 of the 256 opcode bytes are
 * defined (0x50 and 0x51 are the only holes), so a decoder is a single switch.
 *
 * Two address spaces:
 *   ROM  64 KB - instruction fetch and LDC only.
 *   RAM 512 B  - 0x000-0x0FF general purpose (2 banks), 0x100-0x1FF SFRs.
 *
 * Operand forms are named after the doc's notation: i8 immediate, d9 direct
 * 9-bit RAM address, @Ri indirect, b3 bit index, a12/a16 absolute ROM,
 * r8/r16 relative ROM.
 */
#ifndef LC8670_DECODE_H
#define LC8670_DECODE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    OP_INVALID = 0,
    OP_NOP, OP_BR, OP_BRF, OP_LD, OP_ST, OP_MOV, OP_LDC, OP_XCH,
    OP_PUSH, OP_POP, OP_CALL, OP_CALLF, OP_CALLR, OP_RET, OP_RETI,
    OP_JMP, OP_JMPF, OP_BZ, OP_BNZ, OP_BE, OP_BNE, OP_DBNZ,
    OP_BP, OP_BPC, OP_BN,
    OP_ADD, OP_ADDC, OP_SUB, OP_SUBC, OP_INC, OP_DEC, OP_MUL, OP_DIV,
    OP_AND, OP_OR, OP_XOR, OP_ROL, OP_ROLC, OP_ROR, OP_RORC,
    OP_SET1, OP_CLR1, OP_NOT1,
    OP__COUNT
} vmu_op_t;

typedef enum {
    M_NONE = 0,     /* no operand                       */
    M_I8,           /* #i8                              */
    M_D9,           /* d9                               */
    M_IND,          /* @Ri                              */
    M_A12,          /* a12 (same 4K segment)            */
    M_A16,          /* a16                              */
    M_R8,           /* r8                               */
    M_R16,          /* r16                              */
    M_D9_B3,        /* d9,b3                            */
    M_D9_B3_R8,     /* d9,b3,r8                         */
    M_D9_R8,        /* d9,r8                            */
    M_IND_R8,       /* @Ri,r8                           */
    M_I8_R8,        /* #i8,r8                           */
    M_D9_I8,        /* #i8,d9                           */
    M_IND_I8,       /* #i8,@Ri                          */
    M_IND_I8_R8     /* @Ri,#i8,r8                       */
} vmu_mode_t;

typedef struct {
    uint16_t    pc;         /* address this instruction was decoded at      */
    uint8_t     opcode;     /* first byte                                   */
    vmu_op_t    op;
    vmu_mode_t  mode;
    uint8_t     len;        /* 1..3                                         */
    uint8_t     cycles;
    uint16_t    d9;         /* direct operand address (9-bit)               */
    uint8_t     i8;         /* immediate                                    */
    uint8_t     b3;         /* bit index                                    */
    uint8_t     ri;         /* indirection register 0..3                    */
    uint16_t    target;     /* resolved branch/call/jump target             */
    uint8_t     has_target; /* `target` is meaningful                       */
    uint8_t     is_call;    /* CALL / CALLF / CALLR                         */
    uint8_t     is_ret;     /* RET / RETI                                   */
    uint8_t     ends_block; /* control leaves fallthrough (ret/jmp)         */
} vmu_insn_t;

/* Decode one instruction from rom[pc]. Returns the instruction length, or 0 if
 * pc is out of range. An undefined opcode decodes as OP_INVALID with len 1 so a
 * linear sweep can always make progress. */
int vmu_decode(const uint8_t *rom, size_t rom_size, uint16_t pc, vmu_insn_t *out);

/* Render `in` as assembly into buf (e.g. "ld $104"). */
void vmu_format(const vmu_insn_t *in, char *buf, size_t buflen);

/* Mnemonic without operands. */
const char *vmu_mnemonic(vmu_op_t op);

#endif /* LC8670_DECODE_H */
