/* cpu.c - the machine state and power-on reset. */
#include "vmurecomp/cpu.h"
#include "vmurecomp/timer.h"
#include "vmurecomp/audio.h"
#include "vmurecomp/recomp_rt.h"
#include <string.h>

vmu_cpu_t vmu;

void vmu_reset(const uint8_t *rom, size_t rom_size) {
    memset(&vmu, 0, sizeof(vmu));
    vmu.rom = rom;
    vmu.rom_size = rom_size;

    /* SP starts at 0x7F so the first push lands at 0x80, the base of the
     * stack area. */
    vmu.sfr[SFR_SP & 0xFF] = 0x7F;

    /* Unprogrammed ports read all-ones, and buttons are active low, so an
     * idle P3 is 0xFF. */
    vmu.sfr[SFR_P3 & 0xFF] = 0xFF;
    vmu.sfr[SFR_P7 & 0xFF] = 0xFF;
    vmu.buttons = 0;

    vmu_timer_reset();
    vmu_audio_reset();
    vmu_rt_tick(0);
}

void vmu_set_buttons(uint8_t buttons) {
    uint8_t old = vmu.buttons;
    vmu.buttons = buttons;

    /* Port 3 pins are active low, so pressing a button is a falling edge - the
     * only thing that wakes the firmware out of its HALT loop. Holding a button
     * raises nothing, which is why setting the state once before a run leaves
     * an idle VMU idle.
     *
     * ponytail: gated on P3INT bit 0 and reported in bit 1. Those positions are
     * the conventional LC86 ones and are not in the published VMS docs; if a
     * unit says otherwise, this is the only place to change. */
    uint8_t newly_pressed = (uint8_t)(buttons & ~old);
    if (newly_pressed && (vmu.sfr[SFR_P3INT & 0xFF] & 0x01)) {
        vmu.sfr[SFR_P3INT & 0xFF] |= 0x02;
        vmu_irq_raise(IRQ_P3);
    }
}
