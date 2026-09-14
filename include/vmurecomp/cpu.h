/* cpu.h - LC8670 machine state.
 *
 * There is exactly one machine, declared here as a global. Recompiled code is
 * straight-line C that reads and writes it directly through the helpers in
 * recomp_rt.h, so passing a context pointer through every generated call would
 * cost a lot and buy nothing - the VMU has no second core.
 *
 * RAM space is 512 bytes:
 *   0x000-0x0FF  general purpose, two banks selected by PSW.RAMBK0.
 *                Bank 0 holds firmware variables and the stack, bank 1 is the
 *                application's. The stack is ALWAYS in bank 0 regardless.
 *   0x100-0x1FF  special function registers, including ACC/PSW/B/C/TRL/TRH/SP.
 *   0x180-0x1FB  a window onto XRAM, the LCD frame buffer, banked by XBNK.
 */
#ifndef VMURECOMP_CPU_H
#define VMURECOMP_CPU_H

#include <stdint.h>
#include <stddef.h>

/* SFR addresses in RAM space. */
#define SFR_ACC     0x100
#define SFR_PSW     0x101
#define SFR_B       0x102
#define SFR_C       0x103
#define SFR_TRL     0x104
#define SFR_TRH     0x105
#define SFR_SP      0x106
#define SFR_PCON    0x107
#define SFR_IE      0x108
#define SFR_IP      0x109
#define SFR_EXT     0x10D
#define SFR_OCR     0x10E
#define SFR_T0CON   0x110
#define SFR_T0PRR   0x111
#define SFR_T0L     0x112
#define SFR_T0LR    0x113
#define SFR_T0H     0x114
#define SFR_T0HR    0x115
#define SFR_T1CNT   0x118
#define SFR_T1LC    0x11A
#define SFR_T1LR    0x11B
#define SFR_T1HC    0x11C
#define SFR_T1HR    0x11D
#define SFR_MCR     0x120
#define SFR_STAD    0x122
#define SFR_CNR     0x123
#define SFR_TDR     0x124
#define SFR_XBNK    0x125
#define SFR_VCCR    0x127
#define SFR_P1      0x144
#define SFR_P3      0x14C
#define SFR_P3INT   0x14E
#define SFR_P7      0x15C
#define SFR_I01CR   0x15D
#define SFR_I23CR   0x15E
#define SFR_ISL     0x15F
#define SFR_VSEL    0x163
#define SFR_VRMAD1  0x164
#define SFR_VRMAD2  0x165
#define SFR_VTRBF   0x166
#define SFR_VLREG   0x167
#define SFR_BTCR    0x17F

#define XRAM_BASE   0x180
#define XRAM_END    0x1FB

/* PSW bits. P is read-only and recomputed from ACC on every read. */
#define PSW_CY      0x80
#define PSW_AC      0x40
#define PSW_IRBK1   0x10
#define PSW_IRBK0   0x08
#define PSW_OV      0x04
#define PSW_RAMBK0  0x02
#define PSW_P       0x01

/* Button bits in P3, active low: a pressed button reads 0. */
#define BTN_UP      0x01
#define BTN_DOWN    0x02
#define BTN_LEFT    0x04
#define BTN_RIGHT   0x08
#define BTN_A       0x10
#define BTN_B       0x20
#define BTN_MODE    0x40
#define BTN_SLEEP   0x80

typedef struct {
    uint8_t        ram[2][256];  /* general purpose banks                   */
    uint8_t        sfr[256];     /* 0x100-0x1FF; 0x80-0x7B is the XRAM hole */
    uint8_t        xram[3][128]; /* 0 top half, 1 bottom half, 2 icons      */
    uint8_t        wram[512];    /* work RAM, reached through VRMAD/VTRBF   */

    const uint8_t *rom;          /* 64 KB instruction/LDC space             */
    size_t         rom_size;

    uint64_t       cycles;       /* CPU cycles retired                      */
    uint8_t        buttons;      /* 1 = pressed; inverted into P3           */
    int            halted;       /* PCON.HALT / HOLD                        */
    int            in_isr;       /* suppresses re-entrant interrupt delivery*/
} vmu_cpu_t;

extern vmu_cpu_t vmu;

/* Bind a ROM image and reset the machine to power-on state. */
void vmu_reset(const uint8_t *rom, size_t rom_size);

/* Set the pressed-button mask (BTN_* bits, 1 = pressed). Use this rather than
 * assigning vmu.buttons directly: firmware sleeps in a HALT loop and only a
 * press *edge* wakes it, which is what this raises. */
void vmu_set_buttons(uint8_t buttons);

/* Convenience accessors for the registers that are hot in generated code. */
static inline uint8_t vmu_acc(void)          { return vmu.sfr[SFR_ACC & 0xFF]; }
static inline void    vmu_set_acc(uint8_t v) { vmu.sfr[SFR_ACC & 0xFF] = v; }
static inline uint8_t vmu_psw(void)          { return vmu.sfr[SFR_PSW & 0xFF]; }

#endif /* VMURECOMP_CPU_H */
