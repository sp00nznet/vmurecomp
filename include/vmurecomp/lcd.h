/* lcd.h - the 48x32 dot-matrix LCD.
 *
 * XRAM layout, per bank: each 48-pixel line is six bytes, MSB leftmost. Two
 * consecutive lines are stored back to back (12 bytes) and then four bytes are
 * skipped, so a line pair occupies 16 bytes. Bank 0 is lines 0-15, bank 1 is
 * lines 16-31, bank 2 drives the four status icons.
 */
#ifndef VMURECOMP_LCD_H
#define VMURECOMP_LCD_H

#include "cpu.h"
#include <stdio.h>

#define LCD_W 48
#define LCD_H 32

/* 1 if the pixel at (x, y) is on. Out-of-range coordinates read 0. */
int vmu_lcd_pixel(int x, int y);

/* Raw icon byte from XRAM bank 2: file, game, clock and flash indicators. */
uint8_t vmu_lcd_icons(void);

/* Unpack the frame buffer into out[LCD_W * LCD_H], one byte per pixel (0/1). */
void vmu_lcd_frame(uint8_t *out);

/* Set when generated code writes XRAM, so a host can skip redundant presents. */
void vmu_lcd_dirty(void);
int  vmu_lcd_take_dirty(void);

/* Write the current frame as a binary PGM. Returns 0 on success. `scale`
 * enlarges by an integer factor (1 for native 48x32). */
int vmu_lcd_write_pgm(const char *path, int scale);

#endif /* VMURECOMP_LCD_H */
