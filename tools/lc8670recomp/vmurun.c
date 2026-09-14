/* vmurun - interpret a VMU image against the vmurecomp runtime.
 *
 * This is the oracle, not the product. It executes the same ROM through the
 * same memory map, timers, LCD and buzzer that recompiled code links against,
 * so a recompiled build can be diffed against it frame by frame. When the two
 * disagree, the bug is in the recompiler; when they agree and the screen is
 * wrong, the bug is in the runtime.
 *
 * usage: vmurun <image> [options]
 *   --steps N      stop after N instructions (default 2000000)
 *   --pgm PATH     write the final LCD frame as a PGM
 *   --scale N      PGM scale factor (default 4)
 *   --wav PATH     write captured buzzer audio
 *   --trace N      print the first N instructions as they execute
 *   --buttons BITS initial P3 button state, e.g. 0x10 for A held
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmurecomp/recomp_rt.h"
#include "vmurecomp/timer.h"
#include "vmurecomp/audio.h"
#include "vmurecomp/lcd.h"
#include "decode.h"
#include "vms.h"

/* Interrupts are checked on this cadence: often enough that the firmware clock
 * and game ticks land, rare enough that the check is free. */
#define IRQ_CHECK_INTERVAL 256

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

/* Operand address for the current instruction. */
static uint16_t operand_addr(const vmu_insn_t *in) {
    switch (in->mode) {
    case M_IND: case M_IND_R8: case M_IND_I8: case M_IND_I8_R8:
        return vmu_ind(in->ri);
    default:
        return in->d9;
    }
}

static uint8_t operand_value(const vmu_insn_t *in) {
    return (in->mode == M_I8) ? in->i8 : vmu_read(operand_addr(in));
}

/* Execute one instruction; returns the next PC. */
static uint16_t step(const vmu_insn_t *in) {
    uint16_t next = (uint16_t)(in->pc + in->len);
    uint16_t a = operand_addr(in);
    int taken = 0;

    switch (in->op) {
    case OP_NOP: break;

    case OP_LD:   vmu_rt_ld(a); break;
    case OP_ST:   vmu_rt_st(a); break;
    case OP_MOV:  vmu_rt_mov(a, in->i8); break;
    case OP_XCH:  vmu_rt_xch(a); break;
    case OP_LDC:  vmu_rt_ldc(); break;
    case OP_PUSH: vmu_rt_push(a); break;
    case OP_POP:  vmu_rt_pop(a); break;

    case OP_ADD:  vmu_rt_add(operand_value(in)); break;
    case OP_ADDC: vmu_rt_addc(operand_value(in)); break;
    case OP_SUB:  vmu_rt_sub(operand_value(in)); break;
    case OP_SUBC: vmu_rt_subc(operand_value(in)); break;
    case OP_AND:  vmu_rt_and(operand_value(in)); break;
    case OP_OR:   vmu_rt_or(operand_value(in)); break;
    case OP_XOR:  vmu_rt_xor(operand_value(in)); break;

    case OP_INC:  vmu_rt_inc(a); break;
    case OP_DEC:  vmu_rt_dec(a); break;
    case OP_MUL:  vmu_rt_mul(); break;
    case OP_DIV:  vmu_rt_div(); break;

    case OP_ROL:  vmu_rt_rol(); break;
    case OP_ROLC: vmu_rt_rolc(); break;
    case OP_ROR:  vmu_rt_ror(); break;
    case OP_RORC: vmu_rt_rorc(); break;

    case OP_SET1: vmu_rt_set1(a, in->b3); break;
    case OP_CLR1: vmu_rt_clr1(a, in->b3); break;
    case OP_NOT1: vmu_rt_not1(a, in->b3); break;

    case OP_BP:   taken = vmu_rt_bp(a, in->b3); break;
    case OP_BN:   taken = !vmu_rt_bp(a, in->b3); break;
    case OP_BPC:  taken = vmu_rt_bpc(a, in->b3); break;
    case OP_BZ:   taken = (vmu_acc() == 0); break;
    case OP_BNZ:  taken = (vmu_acc() != 0); break;

    case OP_BE:
    case OP_BNE: {
        int eq;
        if (in->mode == M_I8_R8)        eq = vmu_rt_cmp_acc(in->i8);
        else if (in->mode == M_D9_R8)   eq = vmu_rt_cmp_acc(vmu_read(in->d9));
        else                            eq = vmu_rt_cmp_at(a, in->i8);
        taken = (in->op == OP_BE) ? eq : !eq;
        break;
    }

    case OP_DBNZ: taken = vmu_rt_dbnz(a); break;

    case OP_BR: case OP_BRF: case OP_JMP: case OP_JMPF:
        taken = 1;
        break;

    case OP_CALL: case OP_CALLF: case OP_CALLR:
        /* A mini-game is not self-contained: it calls BIOS routines that live
         * outside its own image. Recompiled code lowers those to a dispatch
         * that traps and returns, so do the same here - otherwise the oracle
         * would halt where the recompiled build carries on, and the two could
         * never be compared on a game. */
        if ((size_t)in->target >= vmu.rom_size) {
            vmu_rt_trap(in->target, "call outside the image");
            return next;
        }
        /* The interpreter keeps control flow on the emulated stack, so unlike
         * recompiled code it honours a handler that rewrites its own return
         * address. That is exactly why it is the oracle. */
        vmu_push_byte((uint8_t)(next & 0xFF));
        vmu_push_byte((uint8_t)(next >> 8));
        return in->target;

    case OP_RET:
    case OP_RETI: {
        uint8_t hi = vmu_pop_byte();
        uint8_t lo = vmu_pop_byte();
        if (in->op == OP_RETI) vmu.in_isr = 0;
        return (uint16_t)((hi << 8) | lo);
    }

    case OP_INVALID:
    default:
        vmu_rt_trap(in->pc, "undefined opcode");
        return next;
    }

    return taken ? in->target : next;
}

/* Deliver a pending interrupt by pushing the current PC and vectoring. */
static uint16_t service_irq(uint16_t pc) {
    if (vmu.in_isr) return pc;
    int irq = vmu_irq_take();
    if (irq < 0) return pc;
    vmu.in_isr = 1;
    vmu_push_byte((uint8_t)(pc & 0xFF));
    vmu_push_byte((uint8_t)(pc >> 8));
    return vmu_irq_vector((vmu_irq_t)irq);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "vmurun - interpret a VMU image against the vmurecomp runtime\n"
            "usage: %s <image> [--steps N] [--pgm PATH] [--scale N]\n"
            "                  [--wav PATH] [--trace N] [--buttons BITS]\n"
            "                  [--press BITS]\n"
            "\n"
            "  --buttons BITS  held for the whole run (BTN_* bits, 1 = pressed)\n"
            "  --press   BITS  pressed a quarter of the way in, released halfway\n"
            "                  later. Firmware sleeps in a HALT loop and only a\n"
            "                  press edge wakes it, so a held button does nothing\n"
            "                  on its own.\n",
            argv[0]);
        return 2;
    }

    const char *path = argv[1];
    unsigned long max_steps = 2000000;
    const char *pgm = NULL, *wav = NULL;
    int scale = 4;
    unsigned long trace = 0;
    unsigned buttons = 0, press = 0;

    for (int i = 2; i < argc; i++) {
        int last = (i + 1 >= argc);
        if (!strcmp(argv[i], "--steps") && !last)        max_steps = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--pgm") && !last)     pgm = argv[++i];
        else if (!strcmp(argv[i], "--wav") && !last)     wav = argv[++i];
        else if (!strcmp(argv[i], "--scale") && !last)   scale = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--trace") && !last)   trace = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--buttons") && !last) buttons = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--press") && !last)   press = (unsigned)strtoul(argv[++i], NULL, 0);
        else { fprintf(stderr, "error: unknown or incomplete option %s\n", argv[i]); return 2; }
    }

    size_t size = 0;
    uint8_t *data = read_file(path, &size);
    if (!data) { fprintf(stderr, "error: cannot read %s\n", path); return 1; }

    vms_info_t img;
    int pr = vms_parse(data, size, &img);
    if (pr != 0) {
        fprintf(stderr, "error: %s is not a runnable VMU image\n", path);
        vms_free(&img);
        free(data);
        return 1;
    }

    vmu_reset(img.rom, img.rom_size);
    if (buttons) vmu_set_buttons((uint8_t)buttons);

    /* A press partway through the run: firmware that has gone to sleep only
     * wakes on an edge, so holding a button from step 0 does nothing. */
    unsigned long press_at = max_steps / 4;
    unsigned long release_at = max_steps / 4 + max_steps / 2;
    int pressed = 0;

    uint16_t pc = 0;
    unsigned long steps = 0;
    unsigned long since_irq = 0;

    while (steps < max_steps) {
        vmu_insn_t in;
        if (vmu_decode(img.rom, img.rom_size, pc, &in) <= 0) {
            fprintf(stderr, "stopped: pc $%04X is outside the image\n", pc);
            break;
        }

        if (steps < trace) {
            char text[64];
            vmu_format(&in, text, sizeof(text));
            printf("%04X  %02X  %-26s acc=%02X psw=%02X sp=%02X\n",
                   in.pc, in.opcode, text, vmu_acc(), vmu_psw(),
                   vmu.sfr[SFR_SP & 0xFF]);
        }

        pc = step(&in);
        vmu_rt_tick(in.cycles);
        steps++;

        if (press) {
            if (pressed == 0 && steps == press_at) {
                vmu_set_buttons((uint8_t)(buttons | press));
                pressed = 1;
            } else if (pressed == 1 && steps == release_at) {
                vmu_set_buttons((uint8_t)buttons);
                pressed = 2;
            }
        }

        if (++since_irq >= IRQ_CHECK_INTERVAL) {
            since_irq = 0;
            pc = service_irq(pc);
        }
    }

    printf("steps          : %lu\n", steps);
    printf("cycles         : %llu\n", (unsigned long long)vmu.cycles);
    printf("final pc       : $%04X\n", pc);
    printf("traps          : %u\n", vmu_rt_trap_count());
    printf("buzzer         : %.1f Hz\n", vmu_audio_freq());

    int lit = 0;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            lit += vmu_lcd_pixel(x, y);
    printf("lit pixels     : %d of %d\n", lit, LCD_W * LCD_H);
    printf("icons          : 0x%02X\n", vmu_lcd_icons());

    /* The registers that explain a stall: if the firmware is sitting in its
     * HALT loop, one of these says which interrupt it is waiting for. */
    printf("IE=%02X IP=%02X PCON=%02X BTCR=%02X T0CON=%02X T1CNT=%02X "
           "P3INT=%02X XBNK=%02X OCR=%02X\n",
           vmu.sfr[SFR_IE & 0xFF], vmu.sfr[SFR_IP & 0xFF],
           vmu.sfr[SFR_PCON & 0xFF], vmu.sfr[SFR_BTCR & 0xFF],
           vmu.sfr[SFR_T0CON & 0xFF], vmu.sfr[SFR_T1CNT & 0xFF],
           vmu.sfr[SFR_P3INT & 0xFF], vmu.sfr[SFR_XBNK & 0xFF],
           vmu.sfr[SFR_OCR & 0xFF]);

    int rc = 0;
    if (pgm && vmu_lcd_write_pgm(pgm, scale) != 0) {
        fprintf(stderr, "error: cannot write %s\n", pgm);
        rc = 1;
    } else if (pgm) {
        printf("wrote          : %s\n", pgm);
    }
    if (wav && vmu_audio_write_wav(wav) != 0) {
        fprintf(stderr, "error: cannot write %s\n", wav);
        rc = 1;
    } else if (wav) {
        printf("wrote          : %s\n", wav);
    }

    vms_free(&img);
    free(data);
    return rc;
}
