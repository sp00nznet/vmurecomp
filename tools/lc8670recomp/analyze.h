/* analyze.h - linear sweep and recursive-descent function discovery.
 *
 * Discovery runs in two passes because the LC8670 has no distinction between
 * "jump" and "tail call": a JMPF to another routine looks exactly like a jump
 * to a basic block. Pass 1 finds every address that is a *call* target (plus
 * the hardware vectors); those are the function entries. Pass 2 walks each
 * entry's blocks, and a jump that lands on some *other* entry is recorded as a
 * tail call rather than inlined, which stops one hot routine from absorbing
 * half the ROM.
 */
#ifndef LC8670_ANALYZE_H
#define LC8670_ANALYZE_H

#include "decode.h"

/* Reset and interrupt vectors, in ROM order. Always seeded. */
extern const uint16_t VMU_VECTORS[11];
extern const char *const VMU_VECTOR_NAMES[11];

typedef void (*insn_cb_t)(const vmu_insn_t *in, void *user);

/* Decode `count` instructions from `start`, calling `cb` for each. A count of
 * 0 sweeps to the end of the image. */
void analyze_linear(const uint8_t *rom, size_t rom_size, uint16_t start,
                    size_t count, insn_cb_t cb, void *user);

typedef struct {
    uint16_t  entry;
    uint16_t *pcs;      /* instruction addresses, ascending */
    size_t    n_pcs;
    size_t    cap;
} vmu_func_t;

typedef struct {
    vmu_func_t *funcs;
    size_t      n_funcs;
    uint8_t    *is_entry;   /* 65536 bytes: address is a function entry     */
    uint8_t    *is_label;   /* 65536 bytes: address is a branch target      */
    uint8_t    *covered;    /* 65536 bytes: address decoded as instruction  */
    size_t      n_insns;    /* total instructions across all functions      */
    size_t      n_invalid;  /* undefined opcodes reached                    */
} vmu_prog_t;

/* Discover functions reachable from the vectors plus any extra seeds.
 * Returns 0 on success. */
int vmu_discover(const uint8_t *rom, size_t rom_size,
                 const uint16_t *extra_seeds, size_t n_extra,
                 vmu_prog_t *out);

void vmu_prog_free(vmu_prog_t *p);

/* Look up the function containing `addr`, or NULL. */
const vmu_func_t *vmu_func_at(const vmu_prog_t *p, uint16_t entry);

#endif /* LC8670_ANALYZE_H */
