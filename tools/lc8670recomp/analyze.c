/* analyze.c - linear sweep and recursive-descent function discovery. */
#include "analyze.h"
#include <stdlib.h>
#include <string.h>

const uint16_t VMU_VECTORS[11] = {
    0x0000, 0x0003, 0x000B, 0x0013, 0x001B, 0x0023,
    0x002B, 0x0033, 0x003B, 0x0043, 0x004B
};

const char *const VMU_VECTOR_NAMES[11] = {
    "reset", "int0", "int1", "int2_t0l", "int3_basetimer", "t0h",
    "t1", "sio0", "sio1", "rfb", "p3"
};

void analyze_linear(const uint8_t *rom, size_t rom_size, uint16_t start,
                    size_t count, insn_cb_t cb, void *user) {
    size_t pc = start;
    size_t n = 0;
    while (pc < rom_size && (count == 0 || n < count)) {
        vmu_insn_t in;
        int len = vmu_decode(rom, rom_size, (uint16_t)pc, &in);
        if (len <= 0) break;
        cb(&in, user);
        pc += (size_t)len;
        n++;
    }
}

/* --- a plain uint16 worklist ------------------------------------------- */

typedef struct {
    uint16_t *v;
    size_t    n, cap;
} u16vec_t;

static int vec_push(u16vec_t *w, uint16_t x) {
    if (w->n == w->cap) {
        size_t cap = w->cap ? w->cap * 2 : 64;
        uint16_t *v = (uint16_t *)realloc(w->v, cap * sizeof(uint16_t));
        if (!v) return -1;
        w->v = v;
        w->cap = cap;
    }
    w->v[w->n++] = x;
    return 0;
}

static int func_push(vmu_func_t *f, uint16_t pc) {
    if (f->n_pcs == f->cap) {
        size_t cap = f->cap ? f->cap * 2 : 64;
        uint16_t *v = (uint16_t *)realloc(f->pcs, cap * sizeof(uint16_t));
        if (!v) return -1;
        f->pcs = v;
        f->cap = cap;
    }
    f->pcs[f->n_pcs++] = pc;
    return 0;
}

static int cmp_u16(const void *a, const void *b) {
    uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b;
    return (x > y) - (x < y);
}

/* --- pass 1: which addresses are called? -------------------------------- */

/* Walk everything reachable from `seeds`, treating jumps as intra-procedural,
 * and mark every call target as a function entry. */
static int find_entries(const uint8_t *rom, size_t rom_size,
                        const u16vec_t *seeds, uint8_t *is_entry,
                        uint8_t *is_label, u16vec_t *entries) {
    uint8_t *visited = (uint8_t *)calloc(65536, 1);
    if (!visited) return -1;

    u16vec_t work = {0};
    for (size_t i = 0; i < seeds->n; i++) {
        if (seeds->v[i] >= rom_size) continue;
        if (!is_entry[seeds->v[i]]) {
            is_entry[seeds->v[i]] = 1;
            if (vec_push(entries, seeds->v[i]) != 0) goto oom;
        }
        if (vec_push(&work, seeds->v[i]) != 0) goto oom;
    }

    while (work.n) {
        uint16_t pc = work.v[--work.n];
        while (1) {
            if ((size_t)pc >= rom_size || visited[pc]) break;
            visited[pc] = 1;

            vmu_insn_t in;
            if (vmu_decode(rom, rom_size, pc, &in) <= 0) break;

            if (in.has_target) {
                if (in.is_call) {
                    if (!is_entry[in.target]) {
                        is_entry[in.target] = 1;
                        if (vec_push(entries, in.target) != 0) goto oom;
                    }
                    if (vec_push(&work, in.target) != 0) goto oom;
                } else {
                    is_label[in.target] = 1;
                    if (vec_push(&work, in.target) != 0) goto oom;
                }
            }

            if (in.ends_block) break;
            pc = (uint16_t)(pc + in.len);
        }
    }

    free(work.v);
    free(visited);
    return 0;
oom:
    free(work.v);
    free(visited);
    return -1;
}

/* --- pass 2: collect each function's instructions ----------------------- */

static int walk_func(const uint8_t *rom, size_t rom_size, vmu_func_t *f,
                     const uint8_t *is_entry, uint8_t *is_label,
                     uint8_t *covered, uint8_t *visited, size_t *n_invalid) {
    u16vec_t work = {0};
    if (vec_push(&work, f->entry) != 0) return -1;

    while (work.n) {
        uint16_t pc = work.v[--work.n];
        while (1) {
            if ((size_t)pc >= rom_size || visited[pc]) break;
            visited[pc] = 1;

            vmu_insn_t in;
            if (vmu_decode(rom, rom_size, pc, &in) <= 0) break;
            if (in.op == OP_INVALID) (*n_invalid)++;

            if (func_push(f, pc) != 0) { free(work.v); return -1; }
            covered[pc] = 1;

            if (in.has_target && !in.is_call) {
                /* A jump onto another function's entry is a tail call; the
                 * emitter turns it into a dispatch, so do not absorb it. */
                if (!(is_entry[in.target] && in.target != f->entry)) {
                    is_label[in.target] = 1;
                    if (vec_push(&work, in.target) != 0) { free(work.v); return -1; }
                }
            }

            if (in.ends_block) break;
            pc = (uint16_t)(pc + in.len);
        }
    }
    free(work.v);

    qsort(f->pcs, f->n_pcs, sizeof(uint16_t), cmp_u16);
    return 0;
}

int vmu_discover(const uint8_t *rom, size_t rom_size,
                 const uint16_t *extra_seeds, size_t n_extra,
                 vmu_prog_t *out) {
    if (!rom || !out || rom_size == 0) return -1;
    memset(out, 0, sizeof(*out));

    out->is_entry = (uint8_t *)calloc(65536, 1);
    out->is_label = (uint8_t *)calloc(65536, 1);
    out->covered  = (uint8_t *)calloc(65536, 1);
    if (!out->is_entry || !out->is_label || !out->covered) goto fail;

    u16vec_t seeds = {0};
    for (size_t i = 0; i < sizeof(VMU_VECTORS) / sizeof(VMU_VECTORS[0]); i++)
        if (vec_push(&seeds, VMU_VECTORS[i]) != 0) goto fail_seeds;
    for (size_t i = 0; i < n_extra; i++)
        if (vec_push(&seeds, extra_seeds[i]) != 0) goto fail_seeds;

    u16vec_t entries = {0};
    if (find_entries(rom, rom_size, &seeds, out->is_entry, out->is_label,
                     &entries) != 0) {
        free(entries.v);
        goto fail_seeds;
    }
    free(seeds.v);
    seeds.v = NULL;

    qsort(entries.v, entries.n, sizeof(uint16_t), cmp_u16);

    out->funcs = (vmu_func_t *)calloc(entries.n, sizeof(vmu_func_t));
    if (!out->funcs) { free(entries.v); goto fail; }
    out->n_funcs = entries.n;

    /* `visited` is per function: two functions may legitimately share a
     * routine reached by a plain jump, and each needs its own copy. */
    uint8_t *visited = (uint8_t *)malloc(65536);
    if (!visited) { free(entries.v); goto fail; }

    for (size_t i = 0; i < entries.n; i++) {
        memset(visited, 0, 65536);
        out->funcs[i].entry = entries.v[i];
        if (walk_func(rom, rom_size, &out->funcs[i], out->is_entry,
                      out->is_label, out->covered, visited,
                      &out->n_invalid) != 0) {
            free(visited);
            free(entries.v);
            goto fail;
        }
        out->n_insns += out->funcs[i].n_pcs;
    }

    free(visited);
    free(entries.v);
    return 0;

fail_seeds:
    free(seeds.v);
fail:
    vmu_prog_free(out);
    return -1;
}

void vmu_prog_free(vmu_prog_t *p) {
    if (!p) return;
    for (size_t i = 0; i < p->n_funcs; i++) free(p->funcs[i].pcs);
    free(p->funcs);
    free(p->is_entry);
    free(p->is_label);
    free(p->covered);
    memset(p, 0, sizeof(*p));
}

const vmu_func_t *vmu_func_at(const vmu_prog_t *p, uint16_t entry) {
    for (size_t i = 0; i < p->n_funcs; i++)
        if (p->funcs[i].entry == entry) return &p->funcs[i];
    return NULL;
}
