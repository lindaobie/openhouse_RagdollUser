/* Debug snapshots readable through the USB ISP drive.
 *   flash sector 30 (0x1E000): "LHBSSNAP", status words, then at +0x100 the live
 *                              parameters as a CPRM partition image
 *   flash sector 31 (0x1F000): "LHEEDUMP" header, EEPROM bytes 0..4031 at +0x40
 *   flash sectors 26-29:       boot ROM copy (once)
 * Uses a 1 KB buffer; sectors are programmed in 1 KB chunks. */
#include <string.h>
#include "debug.h"
#include "params.h"
#include "iap.h"

static uint32_t buf[256];

static uint32_t crc32(uint32_t crc, const uint8_t *p, unsigned n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

typedef struct { uint32_t load_rc; int load_part; uint32_t events; uint32_t sum; uint32_t rc; } snap_ctx_t;

static void fill_params(uint32_t *b32, unsigned chunk, void *vctx)
{
    snap_ctx_t *ctx = vctx;
    uint8_t *b = (uint8_t *)b32;
    memset(b, 0xFF, 1024);
    if (chunk) return;
    memcpy(b, "LHBSSNAP", 8);
    b32[2] = ctx->load_rc;
    b32[3] = (uint32_t)ctx->load_part;
    b32[4] = ctx->events;
    b32[5] = params_payload_size();
    uint8_t *o = b + 0x10C;
    for (unsigned i = 0; i < param_count; i++) {
        const pdesc_t *d = &param_desc[i];
        if (d->flags & PF_RUNTIME) continue;
        memcpy(o, d->ptr, d->size);
        o += d->size;
    }
    b32[0x100 / 4] = 0x4D525043u;   /* "CPRM" */
    b32[0x104 / 4] = 0;
    b32[0x108 / 4] = crc32(0, b + 0x10C, params_payload_size());
}

/* sector 31: chunk 0 = header (0x40) + EEPROM[0..959], chunk c>0 = EEPROM[c*1024-64 ..] */
static void fill_eeprom(uint32_t *b32, unsigned chunk, void *vctx)
{
    snap_ctx_t *ctx = vctx;
    uint8_t *b = (uint8_t *)b32;
    memset(b, 0xFF, 1024);
    unsigned dst = chunk ? 0 : 0x40;
    unsigned ee = chunk ? chunk * 1024 - 0x40 : 0;
    unsigned n = 1024 - dst;
    if (ee + n > EEPROM_SIZE) n = EEPROM_SIZE - ee;
    if (iap_eeprom_read(ee, b + dst, n)) ctx->rc = 1;
    for (unsigned i = 0; i < n; i++) ctx->sum += b[dst + i];
    if (chunk == 0) {
        memcpy(b, "LHEEDUMP", 8);
        b32[2] = EEPROM_SIZE;
    }
}

uint32_t debug_snapshot(uint32_t load_rc, int load_part, uint32_t events)
{
    snap_ctx_t ctx = { load_rc, load_part, events, 0, 0 };
    uint32_t rc;
    if ((rc = iap_flash_write_sector_chunked(30, buf, fill_params, &ctx))) return 0x100 | rc;
    if ((rc = iap_flash_write_sector_chunked(31, buf, fill_eeprom, &ctx))) return 0x200 | rc;
    /* the checksum/status words live in chunk 0, already written: they stay
     * 0xFFFFFFFF here; a reader of the dump recomputes the sum itself */
    return ctx.rc;
}

static void fill_rom(uint32_t *b32, unsigned chunk, void *vctx)
{
    unsigned s = *(unsigned *)vctx;
    memcpy(b32, (const void *)(0x1FFF0000u + s * 4096u + chunk * 1024u), 1024);
}

void debug_dump_rom(void)
{
    const uint32_t *dst = (const uint32_t *)0x1A000;
    for (int i = 0; i < 4096; i++) if (dst[i] != 0xFFFFFFFFu) return;   /* already done */
    for (unsigned s = 0; s < 4; s++)
        iap_flash_write_sector_chunked(26 + s, buf, fill_rom, &s);
}
