/* sfr.c - VMU special function register table.
 *
 * Addresses and register names follow the published VMS hardware documentation.
 * XRAM (0x180-0x1FB) is the LCD frame buffer and is handled as a range rather
 * than named per byte.
 */
#include "sfr.h"
#include <stddef.h>

typedef struct {
    uint16_t    addr;
    const char *name;
    const char *note;
} sfr_ent_t;

static const sfr_ent_t TABLE[] = {
    { 0x100, "ACC",    "accumulator" },
    { 0x101, "PSW",    "CY AC - IRBK1 IRBK0 OV RAMBK0 P" },
    { 0x102, "B",      "general purpose / MUL-DIV operand" },
    { 0x103, "C",      "general purpose / MUL-DIV low half" },
    { 0x104, "TRL",    "table reference low (LDC base)" },
    { 0x105, "TRH",    "table reference high (LDC base)" },
    { 0x106, "SP",     "stack pointer, always in RAM bank 0" },
    { 0x107, "PCON",   "power control: HOLD, HALT" },
    { 0x108, "IE",     "interrupt enable" },
    { 0x109, "IP",     "interrupt priority" },
    { 0x10D, "EXT",    "external memory control" },
    { 0x10E, "OCR",    "oscillator: 32kHz / 600kHz / 6MHz" },

    { 0x110, "T0CON",  "timer 0 control" },
    { 0x111, "T0PRR",  "timer 0 prescaler" },
    { 0x112, "T0L",    "timer 0 low" },
    { 0x113, "T0LR",   "timer 0 low reload" },
    { 0x114, "T0H",    "timer 0 high" },
    { 0x115, "T0HR",   "timer 0 high reload" },

    { 0x118, "T1CNT",  "timer 1 control" },
    { 0x11A, "T1LC",   "timer 1 low compare" },
    { 0x11B, "T1LR",   "timer 1 low (read) / reload (write)" },
    { 0x11C, "T1HC",   "timer 1 high compare" },
    { 0x11D, "T1HR",   "timer 1 high (read) / reload (write)" },

    { 0x120, "MCR",    "LCD mode control" },
    { 0x122, "STAD",   "LCD start address" },
    { 0x123, "CNR",    "LCD character number" },
    { 0x124, "TDR",    "LCD time division" },
    { 0x125, "XBNK",   "XRAM bank: 0 top half, 1 bottom half, 2 icons" },
    { 0x127, "VCCR",   "LCD contrast control" },

    { 0x130, "SCON0",  "serial 0 control" },
    { 0x131, "SBUF0",  "serial 0 buffer" },
    { 0x132, "SBR",    "serial baud rate" },
    { 0x134, "SCON1",  "serial 1 control" },
    { 0x135, "SBUF1",  "serial 1 buffer" },

    { 0x144, "P1",     "port 1 latch" },
    { 0x145, "P1DDR",  "port 1 data direction" },
    { 0x146, "P1FCR",  "port 1 function control" },
    { 0x14C, "P3",     "port 3 latch - buttons (active low)" },
    { 0x14D, "P3DDR",  "port 3 data direction" },
    { 0x14E, "P3INT",  "port 3 interrupt control" },

    { 0x15C, "P7",     "port 7 latch" },
    { 0x15D, "I01CR",  "external interrupt 0/1 control" },
    { 0x15E, "I23CR",  "external interrupt 2/3 control" },
    { 0x15F, "ISL",    "input signal selection" },

    { 0x163, "VSEL",   "VMS control register" },
    { 0x164, "VRMAD1", "work RAM address low" },
    { 0x165, "VRMAD2", "work RAM address high" },
    { 0x166, "VTRBF",  "work RAM send/receive buffer" },
    { 0x167, "VLREG",  "length registration" },

    { 0x17F, "BTCR",   "base timer control" },
};

const char *vmu_sfr_name(uint16_t addr) {
    if (addr >= 0x180 && addr <= 0x1FB) return "XRAM";
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++)
        if (TABLE[i].addr == addr) return TABLE[i].name;
    return NULL;
}

const char *vmu_sfr_note(uint16_t addr) {
    if (addr >= 0x180 && addr <= 0x1FB) return "LCD frame buffer";
    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++)
        if (TABLE[i].addr == addr) return TABLE[i].note;
    return NULL;
}
