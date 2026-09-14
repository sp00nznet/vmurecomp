/* audio.c - square-wave rendering for the piezo buzzer. See audio.h. */
#include "vmurecomp/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T1CNT_T1LRUN 0x40

static struct {
    double    clock_hz;
    double    audible_ceiling_hz;
    double    freq;         /* 0 when silent            */
    double    duty;         /* 0..1                     */
    double    phase;        /* 0..1                     */
    double    frac;         /* leftover sample fraction */
    int16_t  *pcm;
    size_t    n, cap;
} au;

void vmu_audio_set_clock(double hz) {
    au.clock_hz = (hz > 0.0) ? hz : 600000.0;
}

void vmu_audio_reset(void) {
    free(au.pcm);
    memset(&au, 0, sizeof(au));
    au.clock_hz = 600000.0;
    au.audible_ceiling_hz = 20000.0;
}

void vmu_audio_retune(void) {
    uint8_t t1cnt = vmu.sfr[SFR_T1CNT & 0xFF];
    uint8_t reload = vmu.sfr[SFR_T1LR & 0xFF];
    uint8_t compare = vmu.sfr[SFR_T1LC & 0xFF];

    /* The counter runs from the reload value up to 0xFF, so the period is
     * (256 - reload) source cycles. A reload of 0xFF would be a period of one
     * cycle - treat that, and a stopped timer, as silence. */
    unsigned period = 256u - reload;
    if (!(t1cnt & T1CNT_T1LRUN) || period <= 1) {
        au.freq = 0.0;
        return;
    }

    unsigned high = (compare >= reload) ? (unsigned)(compare - reload) : 0u;
    au.duty = (double)high / (double)period;

    /* Full-on and full-off both leave the piezo at rest. */
    if (au.duty <= 0.0 || au.duty >= 1.0) {
        au.freq = 0.0;
        return;
    }

    double f = au.clock_hz / (double)period;
    /* A real piezo element cannot follow the top of this range - firmware
     * parks T1 at periods that work out to hundreds of kHz, which is silence
     * you can hear nothing of rather than a tone. Treat anything past the
     * audible ceiling as off, so captured audio matches the physical part. */
    au.freq = (f > au.audible_ceiling_hz) ? 0.0 : f;
}

void vmu_audio_set_audible_ceiling(double hz) {
    au.audible_ceiling_hz = (hz > 0.0) ? hz : 20000.0;
}

double vmu_audio_freq(void) { return au.freq; }

static void push(int16_t s) {
    if (au.n == au.cap) {
        size_t cap = au.cap ? au.cap * 2 : 8192;
        int16_t *p = (int16_t *)realloc(au.pcm, cap * sizeof(int16_t));
        if (!p) return;             /* drop samples rather than abort a run */
        au.pcm = p;
        au.cap = cap;
    }
    au.pcm[au.n++] = s;
}

void vmu_audio_tick(uint32_t cycles) {
    if (au.clock_hz <= 0.0) au.clock_hz = 600000.0;
    if (au.audible_ceiling_hz <= 0.0) au.audible_ceiling_hz = 20000.0;

    double samples = (double)cycles * (double)AUDIO_RATE / au.clock_hz + au.frac;
    long whole = (long)samples;
    au.frac = samples - (double)whole;

    for (long i = 0; i < whole; i++) {
        if (au.freq <= 0.0) {
            push(0);
            continue;
        }
        au.phase += au.freq / (double)AUDIO_RATE;
        if (au.phase >= 1.0) au.phase -= (double)(long)au.phase;
        push(au.phase < au.duty ? 9000 : -9000);
    }
}

static void w16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void w32(FILE *f, uint32_t v) { w16(f, (uint16_t)(v & 0xFFFF)); w16(f, (uint16_t)(v >> 16)); }

int vmu_audio_write_wav(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    uint32_t data_bytes = (uint32_t)(au.n * 2);
    fwrite("RIFF", 1, 4, f);
    w32(f, 36 + data_bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16);                 /* PCM fmt chunk size */
    w16(f, 1);                  /* PCM                */
    w16(f, 1);                  /* mono               */
    w32(f, AUDIO_RATE);
    w32(f, AUDIO_RATE * 2);     /* byte rate          */
    w16(f, 2);                  /* block align        */
    w16(f, 16);                 /* bits per sample    */
    fwrite("data", 1, 4, f);
    w32(f, data_bytes);
    if (au.n) fwrite(au.pcm, 2, au.n, f);

    int err = ferror(f);
    fclose(f);
    return err ? -1 : 0;
}
