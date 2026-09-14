/* mem.h - RAM-space load/store and indirect address resolution.
 *
 * Every peripheral on the VMU is a byte in RAM space, so these two functions
 * are also the whole I/O path: writes to XRAM land in the LCD frame buffer,
 * writes to T1LR reprogram the buzzer, reads of P3 sample the buttons.
 */
#ifndef VMURECOMP_MEM_H
#define VMURECOMP_MEM_H

#include "cpu.h"

/* 9-bit RAM-space access. Addresses are masked to 0x1FF. */
uint8_t vmu_read(uint16_t addr);
void    vmu_write(uint16_t addr, uint8_t value);

/* Resolve @Ri. The pointer byte is read from 0x00 + 4*IRBK + i in the current
 * RAM bank; bit 1 of i supplies the 9th address bit, so @R0/@R1 always reach
 * RAM and @R2/@R3 always reach the SFR half. */
uint16_t vmu_ind(uint8_t ri);

/* Stack. SP points at the topmost element and the stack grows upward from
 * 0x80; it always lives in RAM bank 0 whatever PSW.RAMBK0 says. */
void    vmu_push_byte(uint8_t v);
uint8_t vmu_pop_byte(void);

/* ROM fetch for LDC and for the interpreter. Out-of-range reads return 0xFF,
 * which is what an unprogrammed flash cell reads as. */
uint8_t vmu_rom(uint16_t addr);

#endif /* VMURECOMP_MEM_H */
