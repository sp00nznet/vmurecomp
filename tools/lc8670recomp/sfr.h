/* sfr.h - names for the VMU special function registers.
 *
 * The LC8670 has no separate I/O space: every peripheral is a byte in RAM
 * space 0x100-0x1FF. That makes disassembly of `ld $125` useless on its own,
 * so the disassembler and the C emitter annotate direct operands with the
 * register name and a short note (`ld $125 ; XBNK (LCD bank select)`).
 */
#ifndef LC8670_SFR_H
#define LC8670_SFR_H

#include <stdint.h>

/* Name for a 9-bit RAM-space address, or NULL if it is plain RAM / unused. */
const char *vmu_sfr_name(uint16_t addr);

/* One-line description of what the register does, or NULL. */
const char *vmu_sfr_note(uint16_t addr);

#endif /* LC8670_SFR_H */
