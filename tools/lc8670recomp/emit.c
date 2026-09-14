/* emit.c - lower discovered functions to readable C. See emit.h. */
#include "emit.h"
#include "sfr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cycles are batched between control transfers: retiring time once per basic
 * block instead of once per instruction keeps the generated code readable and
 * costs nothing that matters, since the timers only need to be right at the
 * points where code can observe them. */
typedef struct {
    FILE    *f;
    uint32_t pending_cycles;
} emit_ctx_t;

static void flush_cycles(emit_ctx_t *e) {
    if (e->pending_cycles) {
        fprintf(e->f, "    vmu_rt_tick(%u);\n", e->pending_cycles);
        e->pending_cycles = 0;
    }
}

static int cmp_u16(const void *a, const void *b) {
    uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b;
    return (x > y) - (x < y);
}

static int in_func(const vmu_func_t *f, uint16_t pc) {
    return bsearch(&pc, f->pcs, f->n_pcs, sizeof(uint16_t), cmp_u16) != NULL;
}

/* RAM-space address of the instruction's operand, as a C expression. */
static void addr_expr(const vmu_insn_t *in, char *buf, size_t n) {
    switch (in->mode) {
    case M_IND:
    case M_IND_R8:
    case M_IND_I8:
    case M_IND_I8_R8:
        snprintf(buf, n, "vmu_ind(%u)", in->ri);
        break;
    default:
        snprintf(buf, n, "0x%03X", in->d9);
        break;
    }
}

/* Value of the instruction's source operand, as a C expression. */
static void value_expr(const vmu_insn_t *in, char *buf, size_t n) {
    if (in->mode == M_I8) {
        snprintf(buf, n, "0x%02X", in->i8);
    } else {
        char a[32];
        addr_expr(in, a, sizeof(a));
        snprintf(buf, n, "vmu_read(%s)", a);
    }
}

/* The `; NAME (what it does)` tail on an instruction comment. */
static void annotate(const vmu_insn_t *in, char *buf, size_t n) {
    buf[0] = '\0';
    switch (in->mode) {
    case M_D9: case M_D9_B3: case M_D9_B3_R8: case M_D9_R8: case M_D9_I8:
        break;
    default:
        return;
    }
    const char *name = vmu_sfr_name(in->d9);
    if (!name) return;
    const char *note = vmu_sfr_note(in->d9);
    if (note) snprintf(buf, n, " ; %s (%s)", name, note);
    else      snprintf(buf, n, " ; %s", name);
}

static void emit_insn(emit_ctx_t *e, const vmu_func_t *f, const vmu_insn_t *in,
                      emit_stats_t *st) {
    char text[64], note[96], a[32], v[48];
    vmu_format(in, text, sizeof(text));
    annotate(in, note, sizeof(note));
    addr_expr(in, a, sizeof(a));
    value_expr(in, v, sizeof(v));

    char lead[160];
    snprintf(lead, sizeof(lead), "    /* %04X: %-24s%s */ ", in->pc, text, note);

    int transfers = in->has_target || in->is_ret || in->op == OP_INVALID;
    if (transfers) {
        e->pending_cycles += in->cycles;
        flush_cycles(e);
    }

    FILE *o = e->f;

    /* A branch to an address inside this function is a goto; one that leaves
     * it is a tail call through the dispatch table. */
    char jump[96];
    if (in->has_target && !in->is_call) {
        if (in_func(f, in->target)) {
            snprintf(jump, sizeof(jump), "goto L_%04X;", in->target);
        } else {
            snprintf(jump, sizeof(jump),
                     "{ vmu_rt_dispatch(0x%04X); return; }", in->target);
            if (st) st->tailcalls++;
        }
    } else {
        jump[0] = '\0';
    }

    switch (in->op) {
    case OP_NOP:   fprintf(o, "%s(void)0;\n", lead); break;

    case OP_LD:    fprintf(o, "%svmu_rt_ld(%s);\n", lead, a); break;
    case OP_ST:    fprintf(o, "%svmu_rt_st(%s);\n", lead, a); break;
    case OP_MOV:   fprintf(o, "%svmu_rt_mov(%s, 0x%02X);\n", lead, a, in->i8); break;
    case OP_XCH:   fprintf(o, "%svmu_rt_xch(%s);\n", lead, a); break;
    case OP_LDC:   fprintf(o, "%svmu_rt_ldc();\n", lead); break;
    case OP_PUSH:  fprintf(o, "%svmu_rt_push(%s);\n", lead, a); break;
    case OP_POP:   fprintf(o, "%svmu_rt_pop(%s);\n", lead, a); break;

    case OP_ADD:   fprintf(o, "%svmu_rt_add(%s);\n", lead, v); break;
    case OP_ADDC:  fprintf(o, "%svmu_rt_addc(%s);\n", lead, v); break;
    case OP_SUB:   fprintf(o, "%svmu_rt_sub(%s);\n", lead, v); break;
    case OP_SUBC:  fprintf(o, "%svmu_rt_subc(%s);\n", lead, v); break;
    case OP_AND:   fprintf(o, "%svmu_rt_and(%s);\n", lead, v); break;
    case OP_OR:    fprintf(o, "%svmu_rt_or(%s);\n", lead, v); break;
    case OP_XOR:   fprintf(o, "%svmu_rt_xor(%s);\n", lead, v); break;

    case OP_INC:   fprintf(o, "%svmu_rt_inc(%s);\n", lead, a); break;
    case OP_DEC:   fprintf(o, "%svmu_rt_dec(%s);\n", lead, a); break;
    case OP_MUL:   fprintf(o, "%svmu_rt_mul();\n", lead); break;
    case OP_DIV:   fprintf(o, "%svmu_rt_div();\n", lead); break;

    case OP_ROL:   fprintf(o, "%svmu_rt_rol();\n", lead); break;
    case OP_ROLC:  fprintf(o, "%svmu_rt_rolc();\n", lead); break;
    case OP_ROR:   fprintf(o, "%svmu_rt_ror();\n", lead); break;
    case OP_RORC:  fprintf(o, "%svmu_rt_rorc();\n", lead); break;

    case OP_SET1:  fprintf(o, "%svmu_rt_set1(%s, %u);\n", lead, a, in->b3); break;
    case OP_CLR1:  fprintf(o, "%svmu_rt_clr1(%s, %u);\n", lead, a, in->b3); break;
    case OP_NOT1:  fprintf(o, "%svmu_rt_not1(%s, %u);\n", lead, a, in->b3); break;

    case OP_BP:    fprintf(o, "%sif (vmu_rt_bp(%s, %u)) %s\n", lead, a, in->b3, jump); break;
    case OP_BN:    fprintf(o, "%sif (!vmu_rt_bp(%s, %u)) %s\n", lead, a, in->b3, jump); break;
    case OP_BPC:   fprintf(o, "%sif (vmu_rt_bpc(%s, %u)) %s\n", lead, a, in->b3, jump); break;

    case OP_BZ:    fprintf(o, "%sif (vmu_acc() == 0) %s\n", lead, jump); break;
    case OP_BNZ:   fprintf(o, "%sif (vmu_acc() != 0) %s\n", lead, jump); break;

    case OP_BE:
    case OP_BNE: {
        const char *neg = (in->op == OP_BNE) ? "!" : "";
        if (in->mode == M_I8_R8)
            fprintf(o, "%sif (%svmu_rt_cmp_acc(0x%02X)) %s\n", lead, neg, in->i8, jump);
        else if (in->mode == M_D9_R8)
            fprintf(o, "%sif (%svmu_rt_cmp_acc(vmu_read(0x%03X))) %s\n", lead, neg, in->d9, jump);
        else
            fprintf(o, "%sif (%svmu_rt_cmp_at(vmu_ind(%u), 0x%02X)) %s\n",
                    lead, neg, in->ri, in->i8, jump);
        break;
    }

    case OP_DBNZ:  fprintf(o, "%sif (vmu_rt_dbnz(%s)) %s\n", lead, a, jump); break;

    case OP_BR:
    case OP_BRF:
    case OP_JMP:
    case OP_JMPF:  fprintf(o, "%s%s\n", lead, jump); break;

    case OP_CALL:
    case OP_CALLF:
    case OP_CALLR:
        fprintf(o, "%svmu_rt_call(0x%04X, 0x%04X);\n", lead, in->target,
                (uint16_t)(in->pc + in->len));
        break;

    case OP_RET:   fprintf(o, "%svmu_rt_ret(); return;\n", lead); break;
    case OP_RETI:  fprintf(o, "%svmu_rt_reti(); return;\n", lead); break;

    case OP_INVALID:
    default:
        fprintf(o, "%svmu_rt_trap(0x%04X, \"undefined opcode 0x%02X\"); return;\n",
                lead, in->pc, in->opcode);
        if (st) st->traps++;
        break;
    }

    if (!transfers) e->pending_cycles += in->cycles;
}

static void emit_func(emit_ctx_t *e, const uint8_t *rom, size_t rom_size,
                      const vmu_prog_t *prog, const vmu_func_t *f,
                      emit_stats_t *st) {
    uint16_t lo = f->pcs[0];
    uint16_t hi = f->pcs[f->n_pcs - 1];

    fprintf(e->f, "\n/* vmu_func_%04X: $%04X-$%04X, %zu instructions */\n",
            f->entry, lo, hi, f->n_pcs);
    fprintf(e->f, "void vmu_func_%04X(void) {\n", f->entry);
    e->pending_cycles = 0;

    for (size_t i = 0; i < f->n_pcs; i++) {
        uint16_t pc = f->pcs[i];
        vmu_insn_t in;
        if (vmu_decode(rom, rom_size, pc, &in) <= 0) break;

        if (prog->is_label[pc] || pc == f->entry) {
            flush_cycles(e);
            fprintf(e->f, "L_%04X:\n", pc);
        }

        /* A gap between this instruction and the previous one means the
         * fallthrough path was never reached; control can only arrive here by
         * a branch, and the label above is what gets us in. */
        emit_insn(e, f, &in, st);
    }

    flush_cycles(e);
    fprintf(e->f, "}\n");
}

static FILE *open_out(const char *dir, const char *name) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) fprintf(stderr, "error: cannot write %s\n", path);
    return f;
}

static void banner(FILE *f, const char *title) {
    fprintf(f,
        "/* GENERATED by lc8670recomp - do not edit, do not commit.\n"
        " *\n"
        " * Recompiled from: %s\n"
        " * This file is derived from a proprietary binary. It belongs in a\n"
        " * gitignored generated/ directory and is never redistributed.\n"
        " */\n", title ? title : "(unnamed image)");
}

int vmu_emit(const uint8_t *rom, size_t rom_size, const vmu_prog_t *prog,
             const char *outdir, const char *title, emit_stats_t *stats) {
    if (!rom || !prog || !outdir) return -1;

    emit_stats_t st = {0};

    /* --- header --------------------------------------------------------- */
    FILE *h = open_out(outdir, "recomp_funcs.h");
    if (!h) return -1;
    banner(h, title);
    fprintf(h, "#ifndef VMU_RECOMP_FUNCS_H\n#define VMU_RECOMP_FUNCS_H\n\n");
    fprintf(h, "#include \"vmurecomp/recomp_rt.h\"\n\n");
    for (size_t i = 0; i < prog->n_funcs; i++)
        fprintf(h, "void vmu_func_%04X(void);\n", prog->funcs[i].entry);
    fprintf(h, "\n/* Populate the address -> function dispatch table. */\n");
    fprintf(h, "void vmu_recomp_register_all(void);\n\n");
    fprintf(h, "#endif\n");
    fclose(h);

    /* --- functions ------------------------------------------------------ */
    FILE *c = open_out(outdir, "recomp_funcs.c");
    if (!c) return -1;
    banner(c, title);
    fprintf(c, "#include \"recomp_funcs.h\"\n");

    emit_ctx_t e = { c, 0 };
    for (size_t i = 0; i < prog->n_funcs; i++) {
        emit_func(&e, rom, rom_size, prog, &prog->funcs[i], &st);
        st.insns += prog->funcs[i].n_pcs;
    }
    st.funcs = prog->n_funcs;
    fclose(c);

    /* --- dispatch ------------------------------------------------------- */
    FILE *d = open_out(outdir, "recomp_dispatch.c");
    if (!d) return -1;
    banner(d, title);
    fprintf(d, "#include \"recomp_funcs.h\"\n\n");
    fprintf(d, "void vmu_recomp_register_all(void) {\n");
    for (size_t i = 0; i < prog->n_funcs; i++)
        fprintf(d, "    vmu_rt_register(0x%04X, vmu_func_%04X);\n",
                prog->funcs[i].entry, prog->funcs[i].entry);
    fprintf(d, "}\n");
    fclose(d);

    if (stats) *stats = st;
    return 0;
}
