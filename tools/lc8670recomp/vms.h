/* vms.h - VMU image container handling.
 *
 * Three kinds of input matter to the recompiler:
 *
 *   .vms game file   The file *is* the ROM image: ROM address 0 is file offset
 *                    0, and a 128-byte descriptive header sits in the second
 *                    block (offset $200) where it is harmless because the code
 *                    jumps over it. Entry is the reset vector at $0000.
 *   .vms data file   Header at offset 0, no executable code. Rejected.
 *   raw image        A headerless dump (a BIOS image, or a .vms already
 *                    stripped out of a .dci). Treated as ROM from offset 0.
 *
 * .dci is a 32-byte directory entry followed by the file data with every
 * 4-byte word byte-reversed; .vmi is a metadata sidecar that points at a
 * separate .vms. Both are unwrapped to the underlying image.
 */
#ifndef LC8670_VMS_H
#define LC8670_VMS_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    VMS_RAW = 0,    /* headerless ROM image (e.g. a BIOS dump) */
    VMS_GAME,       /* executable mini-game, header at $200    */
    VMS_DATA        /* save data, header at $0 - not runnable  */
} vms_kind_t;

typedef struct {
    vms_kind_t     kind;
    const uint8_t *rom;         /* ROM image; address 0 is rom[0]       */
    size_t         rom_size;
    uint8_t       *owned;       /* non-NULL if rom must be free()d      */

    /* header fields, empty for VMS_RAW */
    char           desc_short[17];
    char           desc_long[33];
    char           creator[17];
    uint16_t       icon_count;
    uint16_t       icon_speed;
    uint16_t       eyecatch;
    uint16_t       crc_stored;
    uint32_t       payload_size;
    int            has_header;
} vms_info_t;

/* Classify `data` and fill `out`. Returns 0 on success, non-zero if the image
 * cannot be used as a ROM. Takes no ownership of `data`; if unwrapping was
 * needed, out->owned holds the buffer to free. */
int vms_parse(const uint8_t *data, size_t size, vms_info_t *out);

void vms_free(vms_info_t *info);

/* CRC-16 over the whole file with the CRC field zeroed, as the VMS file
 * manager computes it. Ignored for game files but useful for `info`. */
uint16_t vms_crc(const uint8_t *buf, size_t size);

#endif /* LC8670_VMS_H */
