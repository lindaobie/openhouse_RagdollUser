/* OOTX: base-station data slowly shifted out one bit per sync flash.
 * Stock ootx_build_frame @0x3268, ootx_frame_encode @0xAA04, bit appender
 * @0xB5DE/0xB598, ootx_get_bit @0xAA7A, ootx_pulse_width @0xAAA0.
 *
 * Payload v6 (33 bytes, little endian):
 *   0  u16 (fw_version << 6) | 6         stock: 436 -> 0x6D06
 *   2  u32 serial = crc32(16-byte MCU UID)
 *   6  f16 fcal.0.phase   8 f16 fcal.1.phase
 *  10  f16 fcal.0.tilt   12 f16 fcal.1.tilt
 *  14  u8  rotor lock events (low byte)
 *  15  u8  hardware model (9; 6 on boards with HW_ID rev < 3)
 *  16  f16 fcal.0.curve  18 f16 fcal.1.curve
 *  20  i8  accel dir x, y, z (gravity unit vector * 127)
 *  23  f16 fcal.0.gibphase (mod 2pi)  25 f16 fcal.1.gibphase
 *  27  f16 fcal.0.gibmag  29 f16 fcal.1.gibmag
 *  31  u8  mode.current & 7
 *  32  u8  faults ? 0x80 | faults : 0
 *  33  u8  multi mode only (payload 34): the slots taking turns (bit s = slot s)
 * Frame: 17 zeros, 1, then u16 length, payload, pad to even, crc32 - each byte
 * MSB first, a '1' stuffed after every 16 bits. 33-byte payload = 358 bits. */
#include <string.h>
#include "ootx.h"
#include "params.h"
#include "board.h"
#include "sys.h"
#include "multi.h"

#define FW_VERSION_REPORTED 436     /* identify as the stock build SteamVR knows */

/* double buffered: the index ISR reads `cur` while a rebuild fills the other
 * buffer, so rebuilding never masks interrupts long enough to delay a sync end
 * (stock rebuilds with IRQs off, which costs late syncs) */
static uint8_t frames[2][64];
static uint8_t *volatile cur = frames[0];
static uint8_t *frame;              /* buffer being built */
static uint16_t nbits;              /* bits in the frame being built */
static volatile uint16_t cur_bits, bitpos;
static volatile uint16_t frames_sent;
uint8_t ootx_accel[3];              /* filled by the accelerometer module */
uint16_t ootx_lock_events;
static uint32_t serial;
static uint8_t built_accel[3];      /* the gravity direction in the frame being sent */

static uint32_t crc32(uint32_t crc, const uint8_t *p, unsigned n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

/* float -> IEEE half, round to nearest even (matches the values SteamVR stores) */
static uint16_t f2h(float f)
{
    union { float f; uint32_t u; } v = { f };
    uint32_t s = (v.u >> 16) & 0x8000, e = (v.u >> 23) & 0xFF, m = v.u & 0x7FFFFF;
    if (e == 0xFF) return s | 0x7C00 | (m ? 0x200 : 0);
    int ne = (int)e - 127 + 15;
    if (ne >= 31) return s | 0x7C00;
    if (ne <= 0) {
        if (ne < -10) return s;
        m |= 0x800000;
        uint32_t sh = 14 - ne, hm = m >> sh, rem = m & ((1u << sh) - 1), half = 1u << (sh - 1);
        if (rem > half || (rem == half && (hm & 1))) hm++;
        return s | hm;
    }
    uint32_t hm = m >> 13, rem = m & 0x1FFF;
    uint32_t h = s | (ne << 10) | hm;
    if (rem > 0x1000 || (rem == 0x1000 && (hm & 1))) h++;
    return h;
}

uint32_t ootx_crc32(const uint8_t *p, unsigned n) { return crc32(0, p, n); }

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static void append_bit(int bit)
{
    if (bit) frame[nbits >> 3] |= 0x80 >> (nbits & 7);
    nbits++;
    if (nbits > 18 && nbits % 17 == 0) {        /* stuffing '1' */
        frame[nbits >> 3] |= 0x80 >> (nbits & 7);
        nbits++;
    }
}

static void append(const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        for (int b = 7; b >= 0; b--) append_bit((p[i] >> b) & 1);
}

static float wrap2pi(float x)
{
    const float tp = 6.2831853f;
    /* Out of any sensible range, infinite or NaN: 0. Subtracting 2pi from a value this big
       rounds back to it and never ends, and a saved one hung every boot (#56.1). */
    if (!(x > -1000.0f && x < 1000.0f)) return 0;
    while (x >= tp) x -= tp;
    while (x < 0) x += tp;
    return x;
}

uint32_t ootx_serial(void) { return serial; }

void ootx_build(void)
{
    if (!serial) {                              /* stock @0x8498: crc32(IAP UID) */
        typedef void (*iap_fn)(uint32_t *, uint32_t *);
        uint32_t cmd[5] = { 58 }, res[5];
        uint32_t s = irq_save();
        ((iap_fn)IAP_ENTRY)(cmd, res);
        irq_restore(s);
        serial = crc32(0, (const uint8_t *)&res[1], 16);
    }
    uint8_t p[34];
    const unsigned plen = multi_active() ? 34 : 33;
    put16(p + 0, (FW_VERSION_REPORTED << 6) | 6);
    memcpy(p + 2, &serial, 4);
    put16(p + 6,  f2h(P.fcal_0_phase));
    put16(p + 8,  f2h(P.fcal_1_phase));
    put16(p + 10, f2h(P.fcal_0_tilt));
    put16(p + 12, f2h(P.fcal_1_tilt));
    p[14] = (uint8_t)ootx_lock_events;
    p[15] = 9;                                  /* TODO: 6 if HW_ID rev < 3 */
    put16(p + 16, f2h(P.fcal_0_curve));
    put16(p + 18, f2h(P.fcal_1_curve));
    memcpy(p + 20, ootx_accel, 3);
    memcpy(built_accel, ootx_accel, 3);
    put16(p + 23, f2h(wrap2pi(P.fcal_0_gibphase)));
    put16(p + 25, f2h(wrap2pi(P.fcal_1_gibphase)));
    put16(p + 27, f2h(P.fcal_0_gibmag));
    put16(p + 29, f2h(P.fcal_1_gibmag));
    p[31] = multi_active() ? multi_ootx_mode() : (P.mode_current & 7);
    p[32] = (P.sys_faults & 0x7F) ? (0x80 | (P.sys_faults & 0x7F)) : 0;
    p[33] = multi_ootx_mask();

    frame = cur == frames[0] ? frames[1] : frames[0];
    memset(frame, 0, 64);
    frame[2] = 0x40;                            /* bits 0..16 = 0, bit 17 = 1 */
    nbits = 18;
    uint8_t len[2] = { (uint8_t)plen, 0 };
    append(len, 2);
    append(p, plen);
    if (plen & 1) { uint8_t z = 0; append(&z, 1); }
    uint32_t crc = crc32(0, p, plen);
    append((const uint8_t *)&crc, 4);
    uint32_t irq = irq_save();                  /* swap: a few instructions only */
    cur = frame;
    cur_bits = nbits;
    bitpos = 0;
    irq_restore(irq);
}

/* Next data bit for a sync flash (stock: index wraps, frame repeats). */
int ootx_next_bit(void)
{
    if (!cur_bits) return 0;
    int bit = (cur[bitpos >> 3] >> (7 - (bitpos & 7))) & 1;
    if (++bitpos >= cur_bits) { bitpos = 0; frames_sent++; }
    return bit;
}

/* stock ootx_pulse_width @0xAAA0: table index [skip<<2 | axis<<1 | data],
 * live widths are the ootx.* parameters in that order (j0 j1 k0 k1 j2 j3 k2 k3). */
uint32_t ootx_pulse_width(int axis, int data, int skip)
{
    static uint16_t *const w[8] = { &P.ootx_j0, &P.ootx_j1, &P.ootx_k0, &P.ootx_k1,
                                    &P.ootx_j2, &P.ootx_j3, &P.ootx_k2, &P.ootx_k3 };
    return *w[((skip & 1) << 2) | ((axis & 1) << 1) | (data & 1)];
}

uint16_t ootx_frames_sent(void) { return frames_sent; }

/* lhbs: stock sends the gravity direction of the last rotor lock. Refreshed when it
 * turned by 2+ counts (~1 deg), at a frame boundary only (a rebuild restarts the
 * frame): the driver tells a knocked base from it. */
void ootx_refresh_accel(void)
{
    static uint16_t seen;
    if (frames_sent == seen) return;
    seen = frames_sent;
    for (int i = 0; i < 3; i++) {
        int d = (int8_t)ootx_accel[i] - (int8_t)built_accel[i];
        if (d >= 2 || d <= -2) { ootx_build(); return; }
    }
}
uint16_t ootx_bits(void) { return cur_bits; }
const uint8_t *ootx_frame(void) { return cur; }
