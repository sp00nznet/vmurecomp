/* timer.c - base timer, T0, T1, and interrupt arbitration. See timer.h. */
#include "vmurecomp/timer.h"
#include "vmurecomp/mem.h"

/* Vector addresses, in IRQ order. Reset (0x0000) is not an interrupt. */
static const uint16_t VECTOR[IRQ__COUNT] = {
    0x0003, 0x000B, 0x0013, 0x001B, 0x0023,
    0x002B, 0x0033, 0x003B, 0x0043, 0x004B
};

uint16_t vmu_irq_vector(vmu_irq_t irq) {
    return (irq >= 0 && irq < IRQ__COUNT) ? VECTOR[irq] : 0;
}

/* Run bits, conventional LC86 layout. See the header's note on calibration. */
#define T0CON_T0LRUN  0x40
#define T0CON_T0HRUN  0x80
#define T0CON_T0LONG  0x20      /* T0L:T0H chained as one 16-bit counter */
#define T1CNT_T1LRUN  0x40
#define T1CNT_T1HRUN  0x80

/* The base timer is clocked from the 32.768 kHz crystal. At the 600 kHz CPU
 * setting that is a little over 18 CPU cycles per tick. */
#define BASE_DIVISOR_DEFAULT 18

static struct {
    uint32_t base_divisor;
    uint32_t base_accum;
    uint16_t base_count;

    uint32_t prescale_accum;
    uint8_t  t0l, t0h;
    uint8_t  t1l, t1h;

    uint16_t pending;           /* bitmask of vmu_irq_t */
} tm;

void vmu_timer_set_divisor(uint32_t cycles_per_tick) {
    tm.base_divisor = cycles_per_tick ? cycles_per_tick : BASE_DIVISOR_DEFAULT;
}

void vmu_timer_reset(void) {
    tm.base_divisor = BASE_DIVISOR_DEFAULT;
    tm.base_accum = 0;
    tm.base_count = 0;
    tm.prescale_accum = 0;
    tm.t0l = tm.t0h = 0;
    tm.t1l = tm.t1h = 0;
    tm.pending = 0;
}

void vmu_irq_raise(vmu_irq_t irq) {
    if (irq >= 0 && irq < IRQ__COUNT) tm.pending |= (uint16_t)(1u << irq);
}

/* IE bit 7 is the master enable; IE0/IE1 gate the two external sources. The
 * timer sources are gated by their own control registers, which is what the
 * run-bit checks below already do. */
int vmu_irq_take(void) {
    if (!(vmu.sfr[SFR_IE & 0xFF] & 0x80)) return -1;
    for (int i = 0; i < IRQ__COUNT; i++) {
        if (tm.pending & (1u << i)) {
            tm.pending &= (uint16_t)~(1u << i);
            return i;
        }
    }
    return -1;
}

/* Advance an 8-bit reload counter by one step; returns 1 on overflow. */
static int step_reload(uint8_t *counter, uint8_t reload) {
    if (*counter == 0xFF) {
        *counter = reload;
        return 1;
    }
    (*counter)++;
    return 0;
}

void vmu_timer_tick(uint32_t cycles) {
    uint8_t t0con = vmu.sfr[SFR_T0CON & 0xFF];
    uint8_t t1cnt = vmu.sfr[SFR_T1CNT & 0xFF];

    /* --- base timer ---------------------------------------------------- */
    tm.base_accum += cycles;
    while (tm.base_accum >= tm.base_divisor) {
        tm.base_accum -= tm.base_divisor;
        tm.base_count++;
        /* BTCR selects a tap off the base counter; the firmware clock uses the
         * slowest one. Fire on the 14-bit rollover, which is the 2 Hz tap. */
        if ((tm.base_count & 0x3FFF) == 0 && (vmu.sfr[SFR_BTCR & 0xFF] & 0x01))
            vmu_irq_raise(IRQ_INT3_BASE);
    }

    /* --- Timer 0 -------------------------------------------------------- */
    /* T0PRR is a reload for the prescaler, so a larger value means a faster
     * count: the prescaler period is 256 - T0PRR source cycles. */
    {
        uint32_t period = 256u - vmu.sfr[SFR_T0PRR & 0xFF];
        tm.prescale_accum += cycles;
        while (tm.prescale_accum >= period) {
            tm.prescale_accum -= period;

            if (t0con & T0CON_T0LONG) {
                /* chained: T0L overflow carries into T0H */
                if (t0con & T0CON_T0LRUN) {
                    if (step_reload(&tm.t0l, vmu.sfr[SFR_T0LR & 0xFF]) &&
                        (t0con & T0CON_T0HRUN)) {
                        if (step_reload(&tm.t0h, vmu.sfr[SFR_T0HR & 0xFF]))
                            vmu_irq_raise(IRQ_T0H);
                    }
                }
            } else {
                if ((t0con & T0CON_T0LRUN) &&
                    step_reload(&tm.t0l, vmu.sfr[SFR_T0LR & 0xFF]))
                    vmu_irq_raise(IRQ_INT2_T0L);
                if ((t0con & T0CON_T0HRUN) &&
                    step_reload(&tm.t0h, vmu.sfr[SFR_T0HR & 0xFF]))
                    vmu_irq_raise(IRQ_T0H);
            }
        }
    }

    /* --- Timer 1 -------------------------------------------------------- */
    /* T1 runs off the CPU clock directly. Its low half drives the buzzer; the
     * audio module reads the reload/compare pair rather than this counter, so
     * all that matters here is the overflow interrupt. */
    for (uint32_t i = 0; i < cycles; i++) {
        if ((t1cnt & T1CNT_T1LRUN) &&
            step_reload(&tm.t1l, vmu.sfr[SFR_T1LR & 0xFF]))
            vmu_irq_raise(IRQ_T1);
        if ((t1cnt & T1CNT_T1HRUN) &&
            step_reload(&tm.t1h, vmu.sfr[SFR_T1HR & 0xFF]))
            vmu_irq_raise(IRQ_T1);
    }
}

void vmu_timer_write(uint16_t addr, uint8_t value) {
    (void)value;
    /* Writing a control register with its run bit clear resets that counter to
     * its reload value, which is how code restarts a timer cleanly. */
    if (addr == SFR_T0CON) {
        if (!(value & T0CON_T0LRUN)) tm.t0l = vmu.sfr[SFR_T0LR & 0xFF];
        if (!(value & T0CON_T0HRUN)) tm.t0h = vmu.sfr[SFR_T0HR & 0xFF];
    } else if (addr == SFR_T1CNT) {
        if (!(value & T1CNT_T1LRUN)) tm.t1l = vmu.sfr[SFR_T1LR & 0xFF];
        if (!(value & T1CNT_T1HRUN)) tm.t1h = vmu.sfr[SFR_T1HR & 0xFF];
    }
}

uint8_t vmu_timer_read(uint16_t addr) {
    switch (addr) {
    case SFR_T0L:  return tm.t0l;
    case SFR_T0H:  return tm.t0h;
    case SFR_T1LR: return tm.t1l;   /* 0x11B reads as T1L */
    case SFR_T1HR: return tm.t1h;   /* 0x11D reads as T1H */
    default:       return vmu.sfr[addr & 0xFF];
    }
}

/* --- work RAM ----------------------------------------------------------- */
/* VRMAD1/VRMAD2 form a 9-bit address into the 512-byte work RAM; every access
 * through VTRBF post-increments it when VSEL bit 4 is set. */
static uint16_t wram_addr(void) {
    return (uint16_t)(((vmu.sfr[SFR_VRMAD2 & 0xFF] & 1) << 8) |
                      vmu.sfr[SFR_VRMAD1 & 0xFF]);
}

static void wram_bump(void) {
    if (!(vmu.sfr[SFR_VSEL & 0xFF] & 0x10)) return;
    uint16_t a = (uint16_t)((wram_addr() + 1) & 0x1FF);
    vmu.sfr[SFR_VRMAD1 & 0xFF] = (uint8_t)(a & 0xFF);
    vmu.sfr[SFR_VRMAD2 & 0xFF] = (uint8_t)((a >> 8) & 1);
}

uint8_t vmu_wram_read(void) {
    uint8_t v = vmu.wram[wram_addr()];
    wram_bump();
    return v;
}

void vmu_wram_write(uint8_t v) {
    vmu.wram[wram_addr()] = v;
    wram_bump();
}
