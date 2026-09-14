/* mem.c - RAM-space load/store, indirect resolution, stack. */
#include "vmurecomp/mem.h"
#include "vmurecomp/lcd.h"
#include "vmurecomp/timer.h"
#include "vmurecomp/audio.h"

static uint8_t parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (uint8_t)(v & 1);
}

static int ram_bank(void) {
    return (vmu.sfr[SFR_PSW & 0xFF] & PSW_RAMBK0) ? 1 : 0;
}

/* XRAM bank 2 holds the four status icons and is only 1 byte wide in practice,
 * but the hardware window is the same size as the dot-matrix banks. */
static int xram_bank(void) {
    int b = vmu.sfr[SFR_XBNK & 0xFF] & 0x03;
    return (b > 2) ? 2 : b;
}

uint8_t vmu_rom(uint16_t addr) {
    if (!vmu.rom || (size_t)addr >= vmu.rom_size) return 0xFF;
    return vmu.rom[addr];
}

uint8_t vmu_read(uint16_t addr) {
    addr &= 0x1FF;

    if (addr < 0x100)
        return vmu.ram[ram_bank()][addr];

    if (addr >= XRAM_BASE)
        return vmu.xram[xram_bank()][addr - XRAM_BASE];

    switch (addr) {
    case SFR_PSW:
        /* P is read-only: it reports the parity of ACC, so recompute it here
         * rather than trying to keep it fresh on every ACC write. */
        vmu.sfr[SFR_PSW & 0xFF] =
            (uint8_t)((vmu.sfr[SFR_PSW & 0xFF] & ~PSW_P) |
                      parity(vmu.sfr[SFR_ACC & 0xFF]));
        break;
    case SFR_P3:
        /* Buttons are active low. */
        return (uint8_t)~vmu.buttons;
    case SFR_T0L: case SFR_T0H:
    case SFR_T1LR: case SFR_T1HR:
        return vmu_timer_read(addr);
    case SFR_VTRBF:
        return vmu_wram_read();
    default:
        break;
    }
    return vmu.sfr[addr & 0xFF];
}

void vmu_write(uint16_t addr, uint8_t value) {
    addr &= 0x1FF;

    if (addr < 0x100) {
        vmu.ram[ram_bank()][addr] = value;
        return;
    }

    if (addr >= XRAM_BASE) {
        vmu.xram[xram_bank()][addr - XRAM_BASE] = value;
        vmu_lcd_dirty();
        return;
    }

    switch (addr) {
    case SFR_PSW:
        /* P is read-only; preserve whatever the last read computed. */
        value = (uint8_t)((value & ~PSW_P) | (vmu.sfr[SFR_PSW & 0xFF] & PSW_P));
        break;
    case SFR_P3:
        /* P3 is an input latch; writes set the direction/pull state, not the
         * button values. Keep the byte so P3DDR-style code reads back sanely. */
        break;
    case SFR_T0CON: case SFR_T0PRR: case SFR_T0LR: case SFR_T0HR:
    case SFR_T1CNT: case SFR_T1LC:  case SFR_T1HC:
    case SFR_T1LR:  case SFR_T1HR:  case SFR_BTCR:
        vmu.sfr[addr & 0xFF] = value;
        vmu_timer_write(addr, value);
        if (addr == SFR_T1CNT || addr == SFR_T1LR || addr == SFR_T1LC)
            vmu_audio_retune();
        return;
    case SFR_VTRBF:
        vmu_wram_write(value);
        return;
    default:
        break;
    }
    vmu.sfr[addr & 0xFF] = value;
}

uint16_t vmu_ind(uint8_t ri) {
    ri &= 0x03;
    uint8_t psw = vmu.sfr[SFR_PSW & 0xFF];
    uint8_t irbk = (uint8_t)(((psw & PSW_IRBK1) ? 2 : 0) | ((psw & PSW_IRBK0) ? 1 : 0));
    uint8_t slot = (uint8_t)(irbk * 4 + ri);
    uint16_t lo = vmu.ram[ram_bank()][slot];
    /* bit 1 of the mode number is the 9th address bit */
    return (uint16_t)(((ri & 2) ? 0x100 : 0x000) | lo);
}

void vmu_push_byte(uint8_t v) {
    uint8_t sp = (uint8_t)(vmu.sfr[SFR_SP & 0xFF] + 1);
    vmu.sfr[SFR_SP & 0xFF] = sp;
    vmu.ram[0][sp] = v;                 /* always bank 0 */
}

uint8_t vmu_pop_byte(void) {
    uint8_t sp = vmu.sfr[SFR_SP & 0xFF];
    uint8_t v = vmu.ram[0][sp];
    vmu.sfr[SFR_SP & 0xFF] = (uint8_t)(sp - 1);
    return v;
}
