/* timer.h - base timer, Timer 0 and Timer 1, and interrupt pending state.
 *
 * Recompiled code cannot be preempted in the middle of a C function, so
 * interrupts are cooperative: the runtime accumulates pending sources as
 * cycles retire and the host delivers them at a safe point by dispatching the
 * corresponding vector. This is the same model N64Recomp-style recompilers
 * use, and it is why a recompiled build is not cycle-exact.
 *
 * What is modelled: the reload-counter cadence of the base timer, T0 (with its
 * prescaler) and T1, which is what drives the firmware clock, game ticks, and
 * the buzzer pitch. The control-register bit assignments follow the
 * conventional LC86 layout; they are the first thing to check against a real
 * unit, so vmu_timer_set_divisor() exists to trim the cadence without touching
 * this file.
 */
#ifndef VMURECOMP_TIMER_H
#define VMURECOMP_TIMER_H

#include "cpu.h"

/* Interrupt sources, in vector order. */
typedef enum {
    IRQ_INT0 = 0, IRQ_INT1, IRQ_INT2_T0L, IRQ_INT3_BASE,
    IRQ_T0H, IRQ_T1, IRQ_SIO0, IRQ_SIO1, IRQ_RFB, IRQ_P3,
    IRQ__COUNT
} vmu_irq_t;

/* ROM address of the vector for `irq`. */
uint16_t vmu_irq_vector(vmu_irq_t irq);

void vmu_timer_reset(void);

/* Advance all timers by `cycles` CPU cycles, raising pending interrupts. */
void vmu_timer_tick(uint32_t cycles);

/* SFR hooks, called from vmu_write / vmu_read. */
void    vmu_timer_write(uint16_t addr, uint8_t value);
uint8_t vmu_timer_read(uint16_t addr);

/* Highest-priority pending-and-enabled interrupt, or -1. Clears it. */
int vmu_irq_take(void);

/* Raise a source by hand (button edges, serial). */
void vmu_irq_raise(vmu_irq_t irq);

/* Calibration: cycles per tick of the base timer source. The default assumes
 * the 32.768 kHz crystal against a 600 kHz CPU clock; a host running at another
 * OCR setting should set this rather than scaling elsewhere. */
void vmu_timer_set_divisor(uint32_t base_timer_cycles_per_tick);

/* Work RAM access through VRMAD1/VRMAD2/VTRBF, which auto-increments. */
uint8_t vmu_wram_read(void);
void    vmu_wram_write(uint8_t v);

#endif /* VMURECOMP_TIMER_H */
