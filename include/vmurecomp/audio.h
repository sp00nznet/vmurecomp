/* audio.h - the piezo buzzer.
 *
 * The VMU has one square-wave voice: Timer 1's low half is run in PWM mode,
 * T1LR sets the period and T1LC the duty cycle. A duty of 0 or full scale is
 * silence, which is how code mutes without stopping the timer.
 *
 * The runtime renders that to mono PCM so a host can play it or dump a .wav.
 */
#ifndef VMURECOMP_AUDIO_H
#define VMURECOMP_AUDIO_H

#include "cpu.h"

#define AUDIO_RATE 44100

void vmu_audio_reset(void);

/* Re-read T1CNT/T1LR/T1LC after a write to any of them. */
void vmu_audio_retune(void);

/* Render `cycles` worth of CPU time into the capture buffer. */
void vmu_audio_tick(uint32_t cycles);

/* Current tone in Hz, or 0 when silent. Useful for tests and for a host that
 * would rather synthesise the note itself. */
double vmu_audio_freq(void);

/* Write everything captured so far as a 16-bit mono WAV. Returns 0 on success. */
int vmu_audio_write_wav(const char *path);

/* Cycles of CPU time per second. The VMU runs at 32.768 kHz, 600 kHz or 6 MHz
 * depending on OCR; the default is the 600 kHz game-mode setting. Set this if
 * the host drives a different clock, so pitch stays right. */
void vmu_audio_set_clock(double hz);

/* Highest frequency the piezo is treated as able to sound; anything above it
 * is captured as silence. Defaults to 20 kHz. This is a property of the
 * physical element, not of the timer, so it is a knob rather than a constant. */
void vmu_audio_set_audible_ceiling(double hz);

#endif /* VMURECOMP_AUDIO_H */
