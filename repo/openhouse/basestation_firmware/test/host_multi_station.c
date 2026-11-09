/* One base station's multi.c (+ params.c) for host_multi.c: built as a shared
 * library and loaded once per station, so every station has its own state. */
#include <string.h>
#include "../src/multi.h"
#include "../src/params.h"
#include "../src/iap.h"

static uint8_t ee[4096];
uint32_t iap_eeprom_read(uint32_t a, void *d, uint32_t n) { memcpy(d, ee + a, n); return 0; }
uint32_t iap_eeprom_write(uint32_t a, const void *s, uint32_t n) { memcpy(ee + a, s, n); return 0; }

static uint32_t now_ms;
uint32_t sys_ms(void) { return now_ms; }

static uint32_t crc32(uint32_t crc, const uint8_t *p, unsigned n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}
uint32_t ootx_crc32(const uint8_t *p, unsigned n) { return crc32(0, p, n); }

/* the frame encoder of ootx.c, reduced to what the slaves read */
static uint8_t frame[64];
static uint16_t nbits, bitpos;
static uint16_t frames_sent;
static void append_bit(int bit)
{
    if (bit) frame[nbits >> 3] |= 0x80 >> (nbits & 7);
    nbits++;
    if (nbits > 18 && nbits % 17 == 0) { frame[nbits >> 3] |= 0x80 >> (nbits & 7); nbits++; }
}
static void append(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        for (int b = 7; b >= 0; b--) append_bit((p[i] >> b) & 1);
}
void ootx_build(void)
{
    uint8_t p[34] = {0};
    p[31] = multi_ootx_mode();
    p[33] = multi_ootx_mask();
    memset(frame, 0, sizeof frame);
    frame[2] = 0x40;
    nbits = 18;
    uint8_t len[2] = { 34, 0 };
    append(len, 2);
    append(p, 34);
    uint32_t crc = crc32(0, p, 34);
    append((const uint8_t *)&crc, 4);
    bitpos = 0;
}
uint16_t ootx_frames_sent(void) { return frames_sent; }

/* harness API */
void st_init(int slot, uint32_t t)
{
    now_ms = t;
    memset(&P, 0, sizeof P);
    multi_init();
    multi_enter(slot);
    ootx_build();
}
void st_time(uint32_t t) { now_ms = t; }
int st_next_bit(void)
{
    int bit = (frame[bitpos >> 3] >> (7 - (bitpos & 7))) & 1;
    if (++bitpos >= nbits) { bitpos = 0; frames_sent++; }
    return bit;
}
