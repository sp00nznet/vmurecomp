/* lc8670recomp - VMU static-recompiler driver.
 *
 * Subcommands:
 *   info   <image>                 container, header and discovery summary
 *   dis    <image> [start] [count] annotated disassembly
 *   recomp <image> <outdir>        discover functions and emit C
 *   emit   <image> <outdir>        alias for recomp
 *
 * `image` is a .vms mini-game, a .dci, or a raw ROM dump such as a BIOS image.
 * Bring your own: no binaries ship with this repository, and nothing this tool
 * writes may be committed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vms.h"
#include "decode.h"
#include "analyze.h"
#include "emit.h"
#include "sfr.h"

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)n;
    return buf;
}

static const char *kind_name(vms_kind_t k) {
    switch (k) {
    case VMS_GAME: return "VMS mini-game (header at $200)";
    case VMS_DATA: return "VMS data file (no executable code)";
    default:       return "raw image (headerless)";
    }
}

static void dis_cb(const vmu_insn_t *in, void *user) {
    (void)user;
    char text[64];
    vmu_format(in, text, sizeof(text));

    const char *name = NULL;
    switch (in->mode) {
    case M_D9: case M_D9_B3: case M_D9_B3_R8: case M_D9_R8: case M_D9_I8:
        name = vmu_sfr_name(in->d9);
        break;
    default:
        break;
    }

    if (name) {
        const char *note = vmu_sfr_note(in->d9);
        if (note) printf("%04X  %02X  %-26s ; %s (%s)\n", in->pc, in->opcode, text, name, note);
        else      printf("%04X  %02X  %-26s ; %s\n", in->pc, in->opcode, text, name);
    } else {
        printf("%04X  %02X  %s\n", in->pc, in->opcode, text);
    }
}

static void print_discovery(const vmu_prog_t *prog, size_t rom_size) {
    size_t covered = 0;
    for (size_t i = 0; i < rom_size && i < 65536; i++)
        if (prog->covered[i]) covered++;

    printf("functions      : %zu\n", prog->n_funcs);
    printf("instructions   : %zu\n", prog->n_insns);
    printf("undefined ops  : %zu\n", prog->n_invalid);
    printf("bytes reached  : %zu of %zu (%.1f%%)\n", covered, rom_size,
           rom_size ? 100.0 * (double)covered / (double)rom_size : 0.0);

    printf("vectors        :");
    for (size_t i = 0; i < sizeof(VMU_VECTORS) / sizeof(VMU_VECTORS[0]); i++)
        if (vmu_func_at(prog, VMU_VECTORS[i]))
            printf(" %s", VMU_VECTOR_NAMES[i]);
    printf("\n");
}

/* Parse a --seed list: comma-separated addresses, where an entry may also be a
 * START:END:STRIDE range. Firmware entry points usually come as a table of
 * fixed-size thunks, so a range spells one out without listing 32 addresses. */
static uint16_t *parse_seeds(const char *spec, size_t *out_n) {
    size_t cap = 16, n = 0;
    uint16_t *v = (uint16_t *)malloc(cap * sizeof(uint16_t));
    if (!v) return NULL;

    const char *p = spec;
    while (*p) {
        char *end;
        unsigned long a = strtoul(p, &end, 0);
        if (end == p) break;
        unsigned long b = a, stride = 1;
        if (*end == ':') {
            p = end + 1;
            b = strtoul(p, &end, 0);
            if (*end == ':') {
                p = end + 1;
                stride = strtoul(p, &end, 0);
            }
        }
        if (stride == 0) stride = 1;
        for (unsigned long x = a; x <= b && x <= 0xFFFF; x += stride) {
            if (n == cap) {
                cap *= 2;
                uint16_t *nv = (uint16_t *)realloc(v, cap * sizeof(uint16_t));
                if (!nv) { free(v); return NULL; }
                v = nv;
            }
            v[n++] = (uint16_t)x;
        }
        p = end;
        while (*p == ',' || *p == ' ') p++;
    }
    *out_n = n;
    return v;
}

static int cmd_info(const vms_info_t *img, const char *path, size_t file_size,
                    const uint16_t *seeds, size_t n_seeds) {
    printf("file           : %s\n", path);
    printf("file size      : %zu bytes\n", file_size);
    printf("container      : %s\n", kind_name(img->kind));
    if (img->has_header) {
        printf("description    : %s\n", img->desc_short);
        printf("long desc      : %s\n", img->desc_long);
        printf("creator        : %s\n", img->creator);
        printf("icons          : %u (speed %u)\n", img->icon_count, img->icon_speed);
        printf("eyecatch       : %u\n", img->eyecatch);
    }
    printf("rom image      : %zu bytes\n", img->rom_size);

    vmu_prog_t prog;
    if (vmu_discover(img->rom, img->rom_size, seeds, n_seeds, &prog) != 0) {
        fprintf(stderr, "error: discovery failed\n");
        return 1;
    }
    print_discovery(&prog, img->rom_size);
    vmu_prog_free(&prog);
    return 0;
}

static int cmd_recomp(const vms_info_t *img, const char *outdir, const char *title,
                      const uint16_t *seeds, size_t n_seeds) {
    vmu_prog_t prog;
    if (vmu_discover(img->rom, img->rom_size, seeds, n_seeds, &prog) != 0) {
        fprintf(stderr, "error: discovery failed\n");
        return 1;
    }
    print_discovery(&prog, img->rom_size);

    emit_stats_t st;
    int rc = vmu_emit(img->rom, img->rom_size, &prog, outdir, title, &st);
    if (rc == 0) {
        printf("\nemitted        : %zu functions, %zu instructions\n", st.funcs, st.insns);
        printf("tail calls     : %zu\n", st.tailcalls);
        printf("runtime traps  : %zu\n", st.traps);
        printf("output         : %s/recomp_funcs.{c,h}, %s/recomp_dispatch.c\n",
               outdir, outdir);
    }
    vmu_prog_free(&prog);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "lc8670recomp - Dreamcast VMU static recompiler\n"
        "usage:\n"
        "  %s info   <image> [--seed LIST]\n"
        "  %s dis    <image> [start] [count]\n"
        "  %s recomp <image> <outdir> [--seed LIST]\n"
        "  %s emit   <image> <outdir> [--seed LIST]\n"
        "\n"
        "  --seed LIST  extra entry points discovery cannot reach on its own,\n"
        "               comma separated. An entry may be START:END:STRIDE, which\n"
        "               is how you hand it a table of fixed-size call thunks:\n"
        "                 --seed 0x100:0x1F8:8\n",
        argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 2; }

    const char *cmd = argv[1];
    const char *path = argv[2];

    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) { fprintf(stderr, "error: cannot read %s\n", path); return 1; }

    vms_info_t img;
    int pr = vms_parse(data, size, &img);
    if (pr < 0) {
        fprintf(stderr, "error: cannot parse %s\n", path);
        free(data);
        return 1;
    }
    if (pr > 0) {
        fprintf(stderr,
                "error: %s is a VMS data file - it holds save data, not code.\n"
                "       Recompile a mini-game (.vms game file) or a ROM dump.\n",
                path);
        vms_free(&img);
        free(data);
        return 1;
    }

    /* --seed may appear anywhere after the subcommand's positional args. */
    uint16_t *seeds = NULL;
    size_t n_seeds = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seeds = parse_seeds(argv[i + 1], &n_seeds);
            if (!seeds) {
                fprintf(stderr, "error: bad --seed list\n");
                vms_free(&img);
                free(data);
                return 2;
            }
            break;
        }
    }

    int rc = 0;
    if (strcmp(cmd, "info") == 0) {
        rc = cmd_info(&img, path, size, seeds, n_seeds);
    } else if (strcmp(cmd, "dis") == 0) {
        uint16_t start = (argc > 3) ? (uint16_t)strtoul(argv[3], NULL, 0) : 0;
        size_t   count = (argc > 4) ? (size_t)strtoul(argv[4], NULL, 0) : 32;
        printf("; linear sweep from $%04X\n", start);
        analyze_linear(img.rom, img.rom_size, start, count, dis_cb, NULL);
    } else if (strcmp(cmd, "recomp") == 0 || strcmp(cmd, "emit") == 0) {
        if (argc < 4) { fprintf(stderr, "error: %s needs <outdir>\n", cmd); rc = 2; }
        else rc = cmd_recomp(&img, argv[3], path, seeds, n_seeds);
    } else {
        usage(argv[0]);
        rc = 2;
    }

    free(seeds);
    vms_free(&img);
    free(data);
    return rc;
}
