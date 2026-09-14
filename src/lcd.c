/* lcd.c - XRAM to framebuffer. See lcd.h for the memory layout. */
#include "vmurecomp/lcd.h"
#include <stdlib.h>

static int lcd_dirty_flag;

void vmu_lcd_dirty(void) { lcd_dirty_flag = 1; }

int vmu_lcd_take_dirty(void) {
    int d = lcd_dirty_flag;
    lcd_dirty_flag = 0;
    return d;
}

/* Byte offset within a bank for the start of dot-matrix line `line` (0-15). */
static int line_base(int line) {
    return (line >> 1) * 16 + (line & 1) * 6;
}

int vmu_lcd_pixel(int x, int y) {
    if (x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return 0;
    int bank = (y < 16) ? 0 : 1;
    int line = y & 15;
    int byte = vmu.xram[bank][line_base(line) + (x >> 3)];
    return (byte >> (7 - (x & 7))) & 1;
}

uint8_t vmu_lcd_icons(void) {
    return vmu.xram[2][0];
}

void vmu_lcd_frame(uint8_t *out) {
    if (!out) return;
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            out[y * LCD_W + x] = (uint8_t)vmu_lcd_pixel(x, y);
}

int vmu_lcd_write_pgm(const char *path, int scale) {
    if (scale < 1) scale = 1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    int w = LCD_W * scale, h = LCD_H * scale;
    fprintf(f, "P5\n%d %d\n255\n", w, h);

    /* An unlit VMU pixel is the pale green of the LCD, a lit one is nearly
     * black; 0xC8 / 0x18 reads about right in grayscale. */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int on = vmu_lcd_pixel(x / scale, y / scale);
            fputc(on ? 0x18 : 0xC8, f);
        }
    }
    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}
