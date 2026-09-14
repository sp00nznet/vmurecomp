/* vms.c - VMU image container handling. See vms.h. */
#include "vms.h"
#include <stdlib.h>
#include <string.h>

#define HDR_GAME_OFF    0x200
#define HDR_SIZE        0x80
#define DCI_HDR         32

uint16_t vms_crc(const uint8_t *buf, size_t size) {
    /* CRC-16/CCITT with polynomial 0x1021, seeded at zero, as specified for
     * the VMS file header. */
    uint32_t n = 0;
    for (size_t i = 0; i < size; i++) {
        n ^= (uint32_t)buf[i] << 8;
        for (int c = 0; c < 8; c++)
            n = (n & 0x8000) ? ((n << 1) ^ 4129) : (n << 1);
    }
    return (uint16_t)(n & 0xFFFF);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Copy a fixed-width padded field and trim the padding. */
static void field(char *dst, size_t dstlen, const uint8_t *src, size_t n) {
    if (n >= dstlen) n = dstlen - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\0')) dst[--n] = '\0';
}

/* A plausible VMS header: printable-ish descriptions and a sane icon count. */
static int looks_like_header(const uint8_t *h) {
    if (rd16(h + 0x40) > 3) return 0;            /* at most 3 icon frames */
    int printable = 0;
    for (int i = 0; i < 16; i++)
        if (h[i] >= 0x20 || h[i] == 0) printable++;
    return printable == 16;
}

static void read_header(vms_info_t *out, const uint8_t *h) {
    field(out->desc_short, sizeof(out->desc_short), h + 0x00, 16);
    field(out->desc_long,  sizeof(out->desc_long),  h + 0x10, 32);
    field(out->creator,    sizeof(out->creator),    h + 0x30, 16);
    out->icon_count   = rd16(h + 0x40);
    out->icon_speed   = rd16(h + 0x42);
    out->eyecatch     = rd16(h + 0x44);
    out->crc_stored   = rd16(h + 0x46);
    out->payload_size = rd32(h + 0x48);
    out->has_header   = 1;
}

/* .dci stores the file with every 4-byte group byte-reversed, after a 32-byte
 * directory entry. Undo both. */
static uint8_t *unwrap_dci(const uint8_t *data, size_t size, size_t *out_size) {
    size_t n = size - DCI_HDR;
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return NULL;
    const uint8_t *src = data + DCI_HDR;
    size_t whole = n & ~(size_t)3;
    for (size_t i = 0; i < whole; i += 4) {
        buf[i + 0] = src[i + 3];
        buf[i + 1] = src[i + 2];
        buf[i + 2] = src[i + 1];
        buf[i + 3] = src[i + 0];
    }
    /* A .dci is always a whole number of 512-byte blocks, so a ragged tail
     * means the file is malformed; copy it straight through rather than
     * dropping bytes. */
    for (size_t i = whole; i < n; i++) buf[i] = src[i];
    *out_size = n;
    return buf;
}

/* A .dci directory entry starts with the file type: 0xCC game, 0x33 data. */
static int is_dci(const uint8_t *data, size_t size) {
    return size > DCI_HDR && (data[0] == 0xCC || data[0] == 0x33) &&
           ((size - DCI_HDR) % 512) == 0;
}

int vms_parse(const uint8_t *data, size_t size, vms_info_t *out) {
    if (!data || !out || size == 0) return -1;
    memset(out, 0, sizeof(*out));

    if (is_dci(data, size)) {
        size_t n = 0;
        uint8_t *buf = unwrap_dci(data, size, &n);
        if (!buf) return -1;
        out->owned = buf;
        data = buf;
        size = n;
    }

    out->rom = data;
    out->rom_size = size;

    if (size >= HDR_GAME_OFF + HDR_SIZE && looks_like_header(data + HDR_GAME_OFF)) {
        out->kind = VMS_GAME;
        read_header(out, data + HDR_GAME_OFF);
        return 0;
    }
    if (size >= HDR_SIZE && looks_like_header(data)) {
        out->kind = VMS_DATA;
        read_header(out, data);
        /* Data files hold no code. Say so plainly rather than emitting
         * garbage C from a save blob. */
        return 1;
    }

    out->kind = VMS_RAW;
    return 0;
}

void vms_free(vms_info_t *info) {
    if (info && info->owned) {
        free(info->owned);
        info->owned = NULL;
        info->rom = NULL;
    }
}
