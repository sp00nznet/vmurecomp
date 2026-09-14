/* host.c - run recompiled code.
 *
 * This is the reference host: it links the generated tree instead of
 * interpreting, so what executes here is native C compiled from LC8670
 * instructions. A per-title repo will want its own host with a window and a
 * pad; this one runs headless and dumps the final frame, which is what you
 * need during bring-up and in CI.
 *
 * The ROM image is still required at run time because LDC reads constants
 * straight out of ROM space - the code is recompiled, the data is not.
 *
 * Built only when -DVMURECOMP_GENERATED=<dir> points at a directory holding
 * recomp_funcs.c / recomp_dispatch.c. Nothing in that directory is committed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmurecomp/recomp_rt.h"
#include "vmurecomp/lcd.h"
#include "vmurecomp/audio.h"
#include "vmurecomp/timer.h"
#include "recomp_funcs.h"

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "vmuhost - run recompiled VMU code\n"
            "usage: %s <image> [--frames N] [--pgm PATH] [--wav PATH]\n"
            "  The image must be the same one the C was generated from: LDC\n"
            "  reads constants out of ROM space at run time.\n", argv[0]);
        return 2;
    }

    const char *path = argv[1];
    const char *pgm = NULL, *wav = NULL;
    long frames = 1;

    for (int i = 2; i < argc; i++) {
        int last = (i + 1 >= argc);
        if (!strcmp(argv[i], "--frames") && !last)   frames = strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--pgm") && !last) pgm = argv[++i];
        else if (!strcmp(argv[i], "--wav") && !last) wav = argv[++i];
        else { fprintf(stderr, "error: unknown or incomplete option %s\n", argv[i]); return 2; }
    }

    size_t size = 0;
    uint8_t *rom = read_file(path, &size);
    if (!rom) { fprintf(stderr, "error: cannot read %s\n", path); return 1; }

    vmu_reset(rom, size);
    vmu_recomp_register_all();

    vmu_func_fn entry = vmu_rt_lookup(0x0000);
    if (!entry) {
        fprintf(stderr, "error: no function registered at the reset vector\n");
        free(rom);
        return 1;
    }

    /* One run for the whole session, not one per frame. The reset path does
     * not return - firmware spins waiting for interrupts - and the budget
     * unwinds out of that loop rather than resuming it, so re-entering per
     * frame would restart the machine every time. Interrupts are serviced from
     * inside the run, which is what lets the spin loop make progress.
     *
     * ponytail: that makes this host capture only the final frame. Per-frame
     * capture wants a present callback fired from vmu_rt_tick, not a host loop. */
    const uint64_t CYCLES_PER_FRAME = 20000;   /* ~1/30 s at 600 kHz */
    int budget_hit = vmu_rt_run(entry, (uint64_t)frames * CYCLES_PER_FRAME);

    printf("outcome        : %s\n",
           budget_hit == 1 ? "still running (budget reached)"
                           : "reset path returned");
    printf("cycles         : %llu\n", (unsigned long long)vmu.cycles);
    printf("traps          : %u\n", vmu_rt_trap_count());

    int lit = 0;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            lit += vmu_lcd_pixel(x, y);
    printf("lit pixels     : %d of %d\n", lit, LCD_W * LCD_H);

    int rc = 0;
    if (pgm) {
        if (vmu_lcd_write_pgm(pgm, 4) != 0) { fprintf(stderr, "error: cannot write %s\n", pgm); rc = 1; }
        else printf("wrote          : %s\n", pgm);
    }
    if (wav) {
        if (vmu_audio_write_wav(wav) != 0) { fprintf(stderr, "error: cannot write %s\n", wav); rc = 1; }
        else printf("wrote          : %s\n", wav);
    }

    free(rom);
    return rc;
}
