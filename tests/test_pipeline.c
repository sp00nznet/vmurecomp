/* test_pipeline.c - discovery and emission end to end on a hand-written ROM.
 *
 * The image below is assembled by hand from the documented encodings, so this
 * test needs no dump of any kind. It exercises the part of the pipeline that a
 * decoder test cannot: that a CALL target becomes its own function, that a
 * backward branch becomes a label, and that the emitter produces compilable C.
 */
#include "analyze.h"
#include "emit.h"
#include "vms.h"
/* Tests must assert even in a release build, where CMake defines NDEBUG. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE 0x200

/*      $0000  jmp  $0100          29 00
 *      $0003  ret                 A0        (and the same at every vector)
 *      $0100  mov  #$FF,$180      23 80 FF  paint the top-left of the LCD
 *      $0103  call $0110          09 10
 *      $0105  br   $0105          01 FE     spin
 *      $0110  inc  $030           62 30
 *      $0112  ret                 A0
 */
static void build_rom(uint8_t *rom) {
    memset(rom, 0, ROM_SIZE);

    rom[0x000] = 0x29; rom[0x001] = 0x00;

    /* A bare RET at each interrupt vector keeps discovery from wandering into
     * the zero fill, the same way real firmware stubs unused vectors. */
    static const uint16_t vec[] = { 0x0003, 0x000B, 0x0013, 0x001B, 0x0023,
                                    0x002B, 0x0033, 0x003B, 0x0043, 0x004B };
    for (size_t i = 0; i < sizeof(vec) / sizeof(vec[0]); i++) rom[vec[i]] = 0xA0;

    rom[0x100] = 0x23; rom[0x101] = 0x80; rom[0x102] = 0xFF;
    rom[0x103] = 0x09; rom[0x104] = 0x10;
    rom[0x105] = 0x01; rom[0x106] = 0xFE;

    rom[0x110] = 0x62; rom[0x111] = 0x30;
    rom[0x112] = 0xA0;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = '\0';
    fclose(f);
    if (len) *len = got;
    return buf;
}

/* A small image may call past its own end - a mini-game reaching a BIOS
 * routine, say. Such a target must not become a function: walking it yields no
 * instructions, and an empty function used to crash the emitter. */
static void test_call_past_end_of_image(const char *outdir) {
    uint8_t rom[0x100];
    memset(rom, 0, sizeof(rom));

    /*  $0000  callf $3CB7   20 3C B7   far outside this 256-byte image
     *  $0003  ret           A0
     */
    rom[0x000] = 0x20; rom[0x001] = 0x3C; rom[0x002] = 0xB7;
    rom[0x003] = 0xA0;

    vmu_prog_t prog;
    assert(vmu_discover(rom, sizeof(rom), NULL, 0, &prog) == 0);

    /* The out-of-range target is not an entry, and no function is empty. */
    assert(!prog.is_entry[0x3CB7]);
    for (size_t i = 0; i < prog.n_funcs; i++)
        assert(prog.funcs[i].n_pcs > 0);

    /* Emission must survive it and lower the call to a dispatch that traps. */
    emit_stats_t st;
    assert(vmu_emit(rom, sizeof(rom), &prog, outdir, "call-past-end", &st) == 0);

    char path[1024];
    snprintf(path, sizeof(path), "%s/recomp_funcs.c", outdir);
    char *c = slurp(path, NULL);
    assert(c != NULL);
    assert(strstr(c, "vmu_rt_call(0x3CB7,") != NULL);
    free(c);

    vmu_prog_free(&prog);
}

/* A function's entry is not always its lowest address: a routine that branches
 * backwards absorbs a block below itself. Instructions are emitted in address
 * order, so the body must open with a jump to the entry - otherwise control
 * falls into the lower block and the entry never runs. */
static void test_entry_below_lowest_address(const char *outdir) {
    uint8_t rom[0x200];
    memset(rom, 0, sizeof(rom));

    /*  $0000  call $0120    09 20
     *  $0002  ret           A0
     *  $0110  inc  $030     62 30     <- absorbed, and lower than the entry
     *  $0112  ret           A0
     *  $0120  br   $0110    01 EE     <- the entry
     */
    rom[0x000] = 0x09; rom[0x001] = 0x20;
    rom[0x002] = 0xA0;
    rom[0x110] = 0x62; rom[0x111] = 0x30;
    rom[0x112] = 0xA0;
    rom[0x120] = 0x01; rom[0x121] = 0xEE;

    static const uint16_t vec[] = { 0x0003, 0x000B, 0x0013, 0x001B, 0x0023,
                                    0x002B, 0x0033, 0x003B, 0x0043, 0x004B };
    for (size_t i = 0; i < sizeof(vec) / sizeof(vec[0]); i++) rom[vec[i]] = 0xA0;

    vmu_prog_t prog;
    assert(vmu_discover(rom, sizeof(rom), NULL, 0, &prog) == 0);

    const vmu_func_t *f = vmu_func_at(&prog, 0x0120);
    assert(f != NULL);
    assert(f->n_pcs == 3);              /* br, inc, ret */
    assert(f->pcs[0] == 0x0110);        /* lowest address is NOT the entry */
    assert(f->entry == 0x0120);

    emit_stats_t st;
    assert(vmu_emit(rom, sizeof(rom), &prog, outdir, "entry-not-lowest", &st) == 0);

    char path[1024];
    snprintf(path, sizeof(path), "%s/recomp_funcs.c", outdir);
    char *c = slurp(path, NULL);
    assert(c != NULL);
    assert(strstr(c, "void vmu_func_0120(void) {\n    goto L_0120;") != NULL);
    /* and a function whose entry *is* its lowest address gets no such jump */
    assert(strstr(c, "void vmu_func_0000(void) {\n    goto L_0000;") == NULL);
    free(c);

    vmu_prog_free(&prog);
}

/* The VMS header CRC is plain CRC-16/CCITT seeded at zero (the XMODEM
 * parameters), whose published check value over "123456789" is 0x31C3. */
static void test_vms_crc(void) {
    assert(vms_crc((const uint8_t *)"123456789", 9) == 0x31C3);
    assert(vms_crc((const uint8_t *)"", 0) == 0x0000);
    /* a single set bit must propagate the polynomial, not stay zero */
    assert(vms_crc((const uint8_t *)"\x01", 1) == 0x1021);
}

int main(int argc, char **argv) {
    const char *outdir = (argc > 1) ? argv[1] : ".";

    uint8_t rom[ROM_SIZE];
    build_rom(rom);

    vmu_prog_t prog;
    assert(vmu_discover(rom, ROM_SIZE, NULL, 0, &prog) == 0);

    /* The reset vector, ten interrupt stubs, and the one routine that is
     * actually called. */
    assert(prog.n_funcs == 12);
    assert(vmu_func_at(&prog, 0x0000) != NULL);
    assert(vmu_func_at(&prog, 0x0110) != NULL);
    assert(prog.is_entry[0x0110]);
    assert(prog.n_invalid == 0);

    /* $0105 branches to itself, so it must be a label. */
    assert(prog.is_label[0x0105]);

    /* The JMP at $0000 is not a call, so $0100 stays part of the reset
     * function rather than becoming an entry of its own. */
    assert(!prog.is_entry[0x0100]);
    const vmu_func_t *reset = vmu_func_at(&prog, 0x0000);
    assert(reset->n_pcs == 4);            /* jmp, mov, call, br */

    const vmu_func_t *sub = vmu_func_at(&prog, 0x0110);
    assert(sub->n_pcs == 2);              /* inc, ret */

    /* Every byte of real code must have been reached. */
    assert(prog.covered[0x000] && prog.covered[0x100] && prog.covered[0x110]);
    assert(!prog.covered[0x150]);         /* zero fill, never reached */

    emit_stats_t st;
    assert(vmu_emit(rom, ROM_SIZE, &prog, outdir, "synthetic", &st) == 0);
    assert(st.funcs == 12);
    assert(st.traps == 0);
    assert(st.insns == prog.n_insns);

    char path[1024];
    snprintf(path, sizeof(path), "%s/recomp_funcs.c", outdir);
    char *c = slurp(path, NULL);
    assert(c != NULL);

    assert(strstr(c, "void vmu_func_0000(void)") != NULL);
    assert(strstr(c, "void vmu_func_0110(void)") != NULL);
    assert(strstr(c, "vmu_rt_call(0x0110, 0x0105)") != NULL);
    assert(strstr(c, "vmu_rt_mov(0x180, 0xFF)") != NULL);
    assert(strstr(c, "vmu_rt_inc(0x030)") != NULL);
    assert(strstr(c, "vmu_rt_ret(); return;") != NULL);
    assert(strstr(c, "L_0105:") != NULL);
    assert(strstr(c, "goto L_0105;") != NULL);
    /* XRAM writes are annotated so the generated C says what it touches. */
    assert(strstr(c, "XRAM") != NULL);
    /* Time is retired, or the timers would never advance. */
    assert(strstr(c, "vmu_rt_tick(") != NULL);
    free(c);

    snprintf(path, sizeof(path), "%s/recomp_dispatch.c", outdir);
    c = slurp(path, NULL);
    assert(c != NULL);
    assert(strstr(c, "vmu_rt_register(0x0110, vmu_func_0110);") != NULL);
    free(c);

    snprintf(path, sizeof(path), "%s/recomp_funcs.h", outdir);
    c = slurp(path, NULL);
    assert(c != NULL);
    assert(strstr(c, "void vmu_recomp_register_all(void);") != NULL);
    /* The banner has to say the output is not committable. */
    assert(strstr(c, "do not commit") != NULL);
    free(c);

    vmu_prog_free(&prog);

    test_vms_crc();
    test_call_past_end_of_image(outdir);
    test_entry_below_lowest_address(outdir);

    printf("test_pipeline: ok\n");
    return 0;
}
