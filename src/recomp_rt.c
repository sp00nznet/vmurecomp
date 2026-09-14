/* recomp_rt.c - instruction semantics for recompiled code.
 *
 * Flag rules follow the LC8670 documentation: CY is carry out of bit 7 (borrow
 * into bit 7 for subtraction), AC the same for bit 3, OV signed overflow.
 * INC, DEC and the logical ops touch no flags at all.
 */
#include "vmurecomp/recomp_rt.h"
#include "vmurecomp/timer.h"
#include "vmurecomp/audio.h"
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#define ACC  vmu.sfr[SFR_ACC & 0xFF]
#define PSW  vmu.sfr[SFR_PSW & 0xFF]
#define REGB vmu.sfr[SFR_B & 0xFF]
#define REGC vmu.sfr[SFR_C & 0xFF]

static void set_flag(uint8_t mask, int on) {
    if (on) PSW |= mask; else PSW = (uint8_t)(PSW & ~mask);
}

static int carry(void) { return (PSW & PSW_CY) ? 1 : 0; }

/* --- data movement ------------------------------------------------------ */

void vmu_rt_ld(uint16_t addr)  { ACC = vmu_read(addr); }
void vmu_rt_st(uint16_t addr)  { vmu_write(addr, ACC); }
void vmu_rt_mov(uint16_t addr, uint8_t imm) { vmu_write(addr, imm); }

void vmu_rt_xch(uint16_t addr) {
    uint8_t t = vmu_read(addr);
    vmu_write(addr, ACC);
    ACC = t;
}

void vmu_rt_ldc(void) {
    uint16_t base = (uint16_t)((vmu.sfr[SFR_TRH & 0xFF] << 8) |
                                vmu.sfr[SFR_TRL & 0xFF]);
    ACC = vmu_rom((uint16_t)(base + ACC));
}

void vmu_rt_push(uint16_t addr) { vmu_push_byte(vmu_read(addr)); }
void vmu_rt_pop(uint16_t addr)  { vmu_write(addr, vmu_pop_byte()); }

/* --- arithmetic --------------------------------------------------------- */

static void do_add(uint8_t v, int cin) {
    uint8_t  a = ACC;
    uint16_t r = (uint16_t)(a + v + cin);
    set_flag(PSW_CY, r > 0xFF);
    set_flag(PSW_AC, ((a & 0x0F) + (v & 0x0F) + cin) > 0x0F);
    set_flag(PSW_OV, ((a ^ (uint8_t)r) & (v ^ (uint8_t)r) & 0x80) != 0);
    ACC = (uint8_t)r;
}

static void do_sub(uint8_t v, int bin) {
    uint8_t a = ACC;
    int     r = a - v - bin;
    set_flag(PSW_CY, r < 0);
    set_flag(PSW_AC, ((a & 0x0F) - (v & 0x0F) - bin) < 0);
    set_flag(PSW_OV, ((a ^ v) & (a ^ (uint8_t)r) & 0x80) != 0);
    ACC = (uint8_t)r;
}

void vmu_rt_add(uint8_t v)  { do_add(v, 0); }
void vmu_rt_addc(uint8_t v) { do_add(v, carry()); }
void vmu_rt_sub(uint8_t v)  { do_sub(v, 0); }
void vmu_rt_subc(uint8_t v) { do_sub(v, carry()); }

void vmu_rt_inc(uint16_t addr) { vmu_write(addr, (uint8_t)(vmu_read(addr) + 1)); }
void vmu_rt_dec(uint16_t addr) { vmu_write(addr, (uint8_t)(vmu_read(addr) - 1)); }

void vmu_rt_mul(void) {
    /* (ACC:C) * B -> B:ACC:C, 24 bits. */
    uint32_t lhs = (uint32_t)((ACC << 8) | REGC);
    uint32_t r = lhs * (uint32_t)REGB;
    REGB = (uint8_t)((r >> 16) & 0xFF);
    ACC  = (uint8_t)((r >> 8) & 0xFF);
    REGC = (uint8_t)(r & 0xFF);
    set_flag(PSW_CY, 0);
    set_flag(PSW_OV, r > 0xFFFF);
}

void vmu_rt_div(void) {
    /* (ACC:C) / B -> quotient in ACC:C, remainder in B. */
    uint32_t lhs = (uint32_t)((ACC << 8) | REGC);
    uint8_t  rhs = REGB;
    set_flag(PSW_CY, 0);

    if (rhs == 0) {
        /* ponytail: divide-by-zero sets OV, but the hardware's quotient is not
         * documented. Saturating is the least surprising choice; if a title
         * turns out to depend on the real value, capture it from a unit and
         * change it here only. */
        ACC = 0xFF;
        REGC = 0xFF;
        REGB = 0;
        set_flag(PSW_OV, 1);
        return;
    }

    uint32_t q = lhs / rhs;
    uint8_t  rem = (uint8_t)(lhs % rhs);
    ACC  = (uint8_t)((q >> 8) & 0xFF);
    REGC = (uint8_t)(q & 0xFF);
    REGB = rem;
    set_flag(PSW_OV, rem == 0);
}

/* --- logic and rotates -------------------------------------------------- */

void vmu_rt_and(uint8_t v) { ACC &= v; }
void vmu_rt_or(uint8_t v)  { ACC |= v; }
void vmu_rt_xor(uint8_t v) { ACC ^= v; }

void vmu_rt_rol(void)  { ACC = (uint8_t)((ACC << 1) | (ACC >> 7)); }
void vmu_rt_ror(void)  { ACC = (uint8_t)((ACC >> 1) | (ACC << 7)); }

void vmu_rt_rolc(void) {
    int cin = carry();
    set_flag(PSW_CY, (ACC & 0x80) != 0);
    ACC = (uint8_t)((ACC << 1) | cin);
}

void vmu_rt_rorc(void) {
    int cin = carry();
    set_flag(PSW_CY, (ACC & 0x01) != 0);
    ACC = (uint8_t)((ACC >> 1) | (cin << 7));
}

/* --- bit operations ----------------------------------------------------- */

void vmu_rt_set1(uint16_t addr, uint8_t bit) {
    vmu_write(addr, (uint8_t)(vmu_read(addr) | (1u << (bit & 7))));
}

void vmu_rt_clr1(uint16_t addr, uint8_t bit) {
    vmu_write(addr, (uint8_t)(vmu_read(addr) & ~(1u << (bit & 7))));
}

void vmu_rt_not1(uint16_t addr, uint8_t bit) {
    vmu_write(addr, (uint8_t)(vmu_read(addr) ^ (1u << (bit & 7))));
}

int vmu_rt_bp(uint16_t addr, uint8_t bit) {
    return (vmu_read(addr) >> (bit & 7)) & 1;
}

int vmu_rt_bpc(uint16_t addr, uint8_t bit) {
    uint8_t v = vmu_read(addr);
    uint8_t m = (uint8_t)(1u << (bit & 7));
    if (!(v & m)) return 0;
    vmu_write(addr, (uint8_t)(v & ~m));
    return 1;
}

/* --- compare and count branches ----------------------------------------- */

int vmu_rt_cmp_acc(uint8_t v) {
    set_flag(PSW_CY, ACC < v);
    return ACC == v;
}

int vmu_rt_cmp_at(uint16_t addr, uint8_t v) {
    uint8_t a = vmu_read(addr);
    set_flag(PSW_CY, a < v);
    return a == v;
}

int vmu_rt_dbnz(uint16_t addr) {
    uint8_t v = (uint8_t)(vmu_read(addr) - 1);
    vmu_write(addr, v);
    return v != 0;
}

/* --- dispatch ----------------------------------------------------------- */

/* ROM space is 64 KB, so a flat table costs one allocation and turns every
 * computed jump into an array index. */
static vmu_func_fn *registry;

static void ensure_registry(void) {
    if (!registry) registry = (vmu_func_fn *)calloc(65536, sizeof(vmu_func_fn));
}

void vmu_rt_register(uint16_t addr, vmu_func_fn fn) {
    ensure_registry();
    if (registry) registry[addr] = fn;
}

vmu_func_fn vmu_rt_lookup(uint16_t addr) {
    return registry ? registry[addr] : NULL;
}

void vmu_rt_clear_registry(void) {
    free(registry);
    registry = NULL;
}

static unsigned trap_count;
static uint16_t trap_last;

void vmu_rt_trap(uint16_t addr, const char *what) {
    trap_count++;
    trap_last = addr;
    if (trap_count <= 16)
        fprintf(stderr, "vmurecomp: trap at $%04X (%s)\n", addr,
                what ? what : "unknown");
}

unsigned vmu_rt_trap_count(void) { return trap_count; }
uint16_t vmu_rt_last_trap(void)  { return trap_last; }

void vmu_rt_dispatch(uint16_t target) {
    vmu_func_fn fn = vmu_rt_lookup(target);
    if (!fn) {
        vmu_rt_trap(target, "no function registered");
        return;
    }
    fn();
}

void vmu_rt_call(uint16_t target, uint16_t return_addr) {
    /* Hardware pushes the low byte first, then the high byte. Keeping the
     * emulated stack in step matters because game code reads SP. */
    vmu_push_byte((uint8_t)(return_addr & 0xFF));
    vmu_push_byte((uint8_t)(return_addr >> 8));
    vmu_rt_dispatch(target);
}

void vmu_rt_ret(void) {
    /* Balance the pair vmu_rt_call pushed; control returns via the C stack. */
    (void)vmu_pop_byte();
    (void)vmu_pop_byte();
}

void vmu_rt_reti(void) {
    vmu_rt_ret();
    vmu.in_isr = 0;
}

/* Escape hatch for firmware main loops, which never return. See vmu_rt_run. */
static jmp_buf run_escape;
static int      run_active;
static uint64_t run_deadline;

int vmu_rt_run(vmu_func_fn entry, uint64_t cycle_budget) {
    if (!entry) return -1;

    run_deadline = vmu.cycles + cycle_budget;
    run_active = 1;

    if (setjmp(run_escape) != 0) {
        /* budget exhausted inside vmu_rt_tick */
        run_active = 0;
        return 1;
    }

    entry();
    run_active = 0;
    return 0;
}

void vmu_rt_tick(uint32_t cycles) {
    vmu.cycles += cycles;
    vmu_timer_tick(cycles);
    vmu_audio_tick(cycles);

    /* A spinning main loop only reaches the runtime here, so this is where an
     * interrupt gets its chance. vmu_rt_service_irq guards against re-entry. */
    vmu_rt_service_irq();

    if (run_active && vmu.cycles >= run_deadline) {
        run_active = 0;
        longjmp(run_escape, 1);
    }
}

int vmu_rt_service_irq(void) {
    if (vmu.in_isr) return 0;
    int irq = vmu_irq_take();
    if (irq < 0) return 0;

    vmu_func_fn fn = vmu_rt_lookup(vmu_irq_vector((vmu_irq_t)irq));
    if (!fn) {
        /* Put it back. vmu_irq_take clears the source, so returning here
         * without re-raising would swallow the interrupt - which is exactly
         * what happened to the interpreter, whose registry is empty and which
         * delivers interrupts itself. Never consume what you cannot deliver. */
        vmu_irq_raise((vmu_irq_t)irq);
        return 0;
    }

    vmu.in_isr = 1;
    /* The hardware pushes the interrupted PC. We do not have one - recompiled
     * code is not interruptible mid-function - but the handler's RETI pops two
     * bytes, so push a matching pair to keep SP balanced. */
    vmu_push_byte(0);
    vmu_push_byte(0);
    fn();
    vmu.in_isr = 0;
    return 1;
}
