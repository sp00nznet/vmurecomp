/* recomp_rt.h - the surface recompiled code calls.
 *
 * One LC8670 routine becomes one C function, `vmu_func_<addr>`. Every
 * instruction lowers to a call in here, so flag semantics live in exactly one
 * place and a fix to ADD's overflow rule fixes every ADD in the program.
 *
 * Control flow:
 *   branches      goto a local label
 *   CALL/CALLF    vmu_rt_call(target, return_addr) - pushes the return address
 *                 on the emulated stack, then dispatches
 *   RET/RETI      vmu_rt_ret() / vmu_rt_reti() then a C return
 *   JMP elsewhere vmu_rt_dispatch(target) then a C return (tail call)
 *
 * ponytail: the return address lives on both the C stack and the emulated one,
 * and only the C stack decides where control actually goes. Code that rewrites
 * its own return address on the stack will not do what it does on hardware. No
 * VMU title is known to need it; if one does, the fix is a trampoline that
 * re-dispatches on the popped value rather than returning.
 */
#ifndef VMURECOMP_RECOMP_RT_H
#define VMURECOMP_RECOMP_RT_H

#include "cpu.h"
#include "mem.h"

/* One recompiled LC8670 routine. */
typedef void (*vmu_func_fn)(void);

/* --- data movement ------------------------------------------------------ */
void vmu_rt_ld(uint16_t addr);                    /* ACC = [addr]           */
void vmu_rt_st(uint16_t addr);                    /* [addr] = ACC           */
void vmu_rt_mov(uint16_t addr, uint8_t imm);      /* [addr] = imm           */
void vmu_rt_xch(uint16_t addr);                   /* ACC <-> [addr]         */
void vmu_rt_ldc(void);                            /* ACC = ROM[TRH:TRL+ACC] */
void vmu_rt_push(uint16_t addr);
void vmu_rt_pop(uint16_t addr);

/* --- arithmetic --------------------------------------------------------- */
void vmu_rt_add(uint8_t v);
void vmu_rt_addc(uint8_t v);
void vmu_rt_sub(uint8_t v);
void vmu_rt_subc(uint8_t v);
void vmu_rt_inc(uint16_t addr);
void vmu_rt_dec(uint16_t addr);
void vmu_rt_mul(void);
void vmu_rt_div(void);

/* --- logic and rotates -------------------------------------------------- */
void vmu_rt_and(uint8_t v);
void vmu_rt_or(uint8_t v);
void vmu_rt_xor(uint8_t v);
void vmu_rt_rol(void);
void vmu_rt_rolc(void);
void vmu_rt_ror(void);
void vmu_rt_rorc(void);

/* --- bit operations ----------------------------------------------------- */
void vmu_rt_set1(uint16_t addr, uint8_t bit);
void vmu_rt_clr1(uint16_t addr, uint8_t bit);
void vmu_rt_not1(uint16_t addr, uint8_t bit);
int  vmu_rt_bp(uint16_t addr, uint8_t bit);       /* bit set?               */
int  vmu_rt_bpc(uint16_t addr, uint8_t bit);      /* bit set? clear if so   */

/* --- compare and count branches ----------------------------------------- */
/* BE/BNE also set CY to (left < right), which is the idiom VMU code uses for
 * unsigned comparison, so these return the equality and leave CY behind. */
int vmu_rt_cmp_acc(uint8_t v);                    /* ACC == v               */
int vmu_rt_cmp_at(uint16_t addr, uint8_t v);      /* [addr] == v            */
int vmu_rt_dbnz(uint16_t addr);                   /* --[addr] != 0          */

/* --- control ------------------------------------------------------------ */
void vmu_rt_call(uint16_t target, uint16_t return_addr);
void vmu_rt_dispatch(uint16_t target);
void vmu_rt_ret(void);
void vmu_rt_reti(void);

/* Retire `cycles` of CPU time: advances timers and audio, delivers any pending
 * interrupt, and enforces the budget set by vmu_rt_run. Generated code calls
 * this once per basic block rather than per instruction.
 *
 * Interrupts are delivered from here because this is the only place a spinning
 * firmware loop hands control back to the runtime. That is also why a handler
 * runs to completion before the interrupted code resumes. */
void vmu_rt_tick(uint32_t cycles);

/* Run `entry` until it returns or `cycle_budget` CPU cycles have been retired.
 *
 * Firmware main loops never return - they spin waiting for an interrupt - so
 * "call the entry point and wait" would hang forever. The budget is enforced
 * inside vmu_rt_tick and unwinds with longjmp, which is safe because generated
 * code is plain C that owns no resources across a basic block.
 *
 * Returns 0 if `entry` returned on its own, 1 if the budget ran out, -1 on a
 * bad argument. */
int vmu_rt_run(vmu_func_fn entry, uint64_t cycle_budget);

/* Reached an undefined opcode or an unmapped dispatch target. Records the
 * address and returns; a host can poll vmu_rt_trap_count() to decide whether a
 * run is trustworthy. */
void     vmu_rt_trap(uint16_t addr, const char *what);
unsigned vmu_rt_trap_count(void);
uint16_t vmu_rt_last_trap(void);

/* --- dispatch table ----------------------------------------------------- */
/* Generated code registers each function so computed jumps, interrupt vectors
 * and tail calls can resolve an address to a native function. */
void        vmu_rt_register(uint16_t addr, vmu_func_fn fn);
vmu_func_fn vmu_rt_lookup(uint16_t addr);
void        vmu_rt_clear_registry(void);

/* Deliver one pending interrupt, if any is enabled and a handler is
 * registered. Returns 1 if a handler ran. Call between frames. */
int vmu_rt_service_irq(void);

#endif /* VMURECOMP_RECOMP_RT_H */
