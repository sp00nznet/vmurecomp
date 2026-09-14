/* emit.h - C code generation.
 *
 * Writes three files into `outdir`:
 *   recomp_funcs.h      declarations for every discovered function
 *   recomp_funcs.c      one C function per LC8670 routine
 *   recomp_dispatch.c   vmu_recomp_register_all(), which populates the
 *                       address -> function table used by computed jumps,
 *                       tail calls and interrupt vectors
 *
 * These are derived from the input binary and must never be committed; the
 * repository's .gitignore covers the whole generated/ directory.
 */
#ifndef LC8670_EMIT_H
#define LC8670_EMIT_H

#include "analyze.h"

typedef struct {
    size_t funcs;
    size_t insns;
    size_t traps;       /* undefined opcodes emitted as runtime traps */
    size_t tailcalls;   /* jumps that left the function                */
} emit_stats_t;

/* Returns 0 on success. `title` is used only in the generated banner. */
int vmu_emit(const uint8_t *rom, size_t rom_size, const vmu_prog_t *prog,
             const char *outdir, const char *title, emit_stats_t *stats);

#endif /* LC8670_EMIT_H */
