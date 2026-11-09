#include <string.h>
#include "multi.h"
#include "params.h"
#include "ootx.h"
#include "sys.h"

_Static_assert(sizeof(multi_cfg_t) <= sizeof(((params_t *)0)->reserved1), "multi cfg too big");

multi_cfg_t *const MC = (multi_cfg_t *)P.reserved1;

void multi_init(void)
{
    if (MC->magic[0] == 'M' && MC->magic[1] == 'S' && MC->version == 1 &&
        MC->slot < MULTI_SLOTS && MC->stagger >= 8000 && MC->stagger <= 20000 && MC->policy <= MP_ALL)
        return;
    /* stock EEPROM (reserved bytes zero) or a freshly flashed station: multi on, the
     * button cycling A b c 1 2 3 4, every flash carrying data (policy ALL) */
    memset(MC, 0, sizeof *MC);
    MC->magic[0] = 'M';
    MC->magic[1] = 'S';
    MC->version = 1;
    MC->enable = 1;
    MC->n = MULTI_SLOTS;
    MC->slot = 0;
    MC->policy = MP_ALL;
    MC->stagger = 12000;
}

bool multi_active(void) { return MC->active; }
char multi_label(void) { return (char)('1' + MC->slot); }

/* ---- the round ------------------------------------------------------------------
 * mask_live: the slots taking turns now. The k-th of them (by slot) sweeps in the
 * revolutions rev % count == k; the master (slot 0) is always k = 0, so its sweeping
 * flash marks every superframe start. A station not in the mask, or a slave that has
 * not yet decoded one, flashes but never sweeps. After any change of the mask a slave
 * stays dark for four superframes: one that missed the change sees the master sweep
 * where its old round doesn't expect it (count changed) within two, and stops. */
static volatile uint8_t mask_live = 1;
static volatile bool mask_known;        /* slave: a mask was decoded from the master */
static volatile uint8_t guard;          /* superframes to stay dark */
static volatile uint8_t pending, pending_set;   /* slave: decoded, taken at its next superframe */

/* master */
static volatile uint32_t seen_ms[MULTI_SLOTS];
#define MASTER_RECENT_MS 100
static volatile uint32_t blind_sweeps;  /* turns swept without this revolution's master flash */
static uint32_t blind_rev = 0xFFFFFFFFu;
static uint8_t candidate, announced = 1;
static uint32_t candidate_since;
static volatile uint16_t announced_at;  /* ootx_frames_sent() when announced */
static volatile bool announcing;

static int popcount4(uint8_t m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1); }

int multi_count(void)
{
    int n = popcount4(mask_live);
    return n ? n : 1;
}

/* this revolution's flashes so far (they come in slot order, the master's first) */
static volatile bool ev_master_skip, ev_earlier_sweep;

void multi_rev_start(void) { ev_master_skip = ev_earlier_sweep = false; }

bool multi_sweeps_in(uint32_t rev)
{
    const uint8_t s = MC->slot, m = mask_live;
    if (!(m & (1u << s))) return false;
    if (s != 0 && (!mask_known || guard)) return false;
    /* a slave also needs this revolution's evidence: no earlier slot's flash saying it
     * sweeps, and the master's flash saying it doesn't. A station that sees the master
     * badly (89AE90F2 as slot 4 missed its turn 1 in 6 times) may do without the latter
     * while it saw the master within the last 100 ms (6 revolutions): it is still phase
     * locked (the timebase stops emission otherwise), and a round change it missed in
     * that window is what the earlier-slot check and the next frame's guard catch. */
    if (s != 0 && ev_earlier_sweep) return false;
    const bool blind = s != 0 && !ev_master_skip;
    if (blind && (!seen_ms[0] || sys_ms() - seen_ms[0] >= MASTER_RECENT_MS)) return false;
    int rank = popcount4(m & ((1u << s) - 1));
    const bool mine = (int)(rev % (uint32_t)multi_count()) == rank;
    if (mine && blind && rev != blind_rev) { blind_rev = rev; blind_sweeps++; }
    return mine;
}

static volatile bool round_changed;     /* slave: rebuild the OOTX frame (main loop) */

static void set_mask(uint8_t m)
{
    if (m == mask_live) return;
    mask_live = m;
    if (MC->slot != 0) { guard = 4; round_changed = true; }
}

/* A slave's frame carries the round it knows (byte 33, mode byte N): built at boot and
 * on rotor lock, it kept announcing the round from before it heard the master (N=1,
 * 0x1) until something else rebuilt it. */
bool multi_take_round_changed(void) { const bool r = round_changed; round_changed = false; return r; }

/* Master and slaves switch at the same superframe: the master once the frame
 * announcing the new round went out whole, a slave once it decoded that frame (its
 * last bit is the same flash). Their revolution counters are aligned, and both
 * restart them there (timebase). */
void multi_superframe(void)
{
    if (guard) guard--;
    if (MC->slot == 0) {
        if (announcing && ootx_frames_sent() != announced_at) {
            announcing = false;
            set_mask(announced);
        }
    } else if (pending_set) {
        pending_set = 0;
        mask_known = true;
        set_mask(pending);
    }
}

/* slave: the master's sweeping flash, `aligned` when it came where this station's round
 * expects it. Not: the round changed under us (a frame was missed): dark until the next
 * frame from the master says what it is. */
void multi_master_sweep(bool aligned)
{
    if (MC->slot == 0 || aligned || !mask_known) return;
    mask_known = false;
    guard = 4;
}

void multi_seen(int slot, bool skip)
{
    if (slot < 0 || slot >= MULTI_SLOTS) return;
    seen_ms[slot] = sys_ms();
    if (slot == 0 && skip) ev_master_skip = true;
    if (slot < MC->slot && !skip) ev_earlier_sweep = true;
}

void multi_reset(void)
{
    mask_live = 1;
    mask_known = false;
    guard = 0;
    pending_set = 0;
    announced = 1;
    candidate = 1;
    candidate_since = sys_ms();
    announcing = false;
    for (int i = 0; i < MULTI_SLOTS; i++) seen_ms[i] = 0;
}

/* master, every 100 ms: the slots whose flash was seen in the last 0.5 s (slaves
 * flash every revolution once phase locked, sweeping or not). A set that held for 1 s
 * is announced: rebuilt into the OOTX frame, live once that frame went out whole. */
void multi_task(void)
{
    if (!multi_active() || MC->slot != 0) return;
    const uint32_t now = sys_ms();
    uint8_t c = 1;
    for (int s = 1; s < MULTI_SLOTS; s++)
        if (seen_ms[s] && now - seen_ms[s] < 500) c |= (uint8_t)(1u << s);
    if (c != candidate) { candidate = c; candidate_since = now; }
    if (c != announced && now - candidate_since >= 1000) {
        announced = c;
        ootx_build();                   /* restarts the frame with the new set */
        announced_at = ootx_frames_sent();
        announcing = true;
    }
}

/* ---- slave: the master's OOTX frame, from its flashes -----------------------------
 * 17 zeros, a 1, then 16-bit groups each followed by a stuffed 1: u16 length (LE),
 * payload, pad to even, crc32 (LE). Bits come in the timebase ISR; the CRC is checked
 * in the main loop. */
static struct {
    uint8_t zeros;
    bool in;
    uint8_t group;
    uint16_t n, total;
    uint8_t buf[56];
} dec;
static uint8_t done[56];
static volatile uint16_t done_len;
static volatile bool done_ready;
static uint32_t frames_ok, frames_bad, frames_wrong;
static volatile bool relock;

static void start_frame(void)
{
    memset(dec.buf, 0, sizeof dec.buf);
    dec.in = true;
    dec.n = dec.total = 0;
    dec.group = 0;
}

void multi_master_bit(int bit)
{
    if (!bit) {
        if (dec.zeros < 255) dec.zeros++;
        if (dec.zeros >= 17) { dec.in = false; return; }   /* a preamble: frames never hold 17 zeros */
    }
    if (!dec.in) {
        if (bit) {
            if (dec.zeros >= 17) start_frame();             /* the 1 that ends the preamble */
            dec.zeros = 0;
        }
        return;
    }
    if (bit) dec.zeros = 0;
    if (dec.group == 16) {                                  /* the stuffed 1 */
        dec.group = 0;
        if (!bit) dec.in = false;
        return;
    }
    if (bit) dec.buf[dec.n >> 3] |= (uint8_t)(0x80u >> (dec.n & 7));
    dec.n++;
    dec.group++;
    if (dec.n == 16) {
        const uint16_t len = (uint16_t)(dec.buf[0] | dec.buf[1] << 8);
        if (len < 4 || len > 40) { dec.in = false; return; }
        dec.total = (uint16_t)(16 + (len + (len & 1)) * 8 + 32);
    }
    if (dec.total && dec.n == dec.total) {
        if (!done_ready) {
            memcpy(done, dec.buf, (dec.total + 7) / 8);
            done_len = (uint16_t)(dec.buf[0] | dec.buf[1] << 8);
            done_ready = true;
        }
        dec.in = false;
    }
}

void multi_poll(void)
{
    if (!done_ready) return;
    const uint16_t len = done_len;
    const uint8_t *p = done + 2;
    uint32_t crc = 0;
    memcpy(&crc, p + len + (len & 1), 4);
    const bool ok = ootx_crc32(p, len) == crc;
    if (ok && len >= 34 && (p[31] & 0x80) && (p[31] & 3) == 0 && (p[33] & 1)) {
        frames_ok++;
        const uint8_t m = p[33] & 0x0F;
        if (!mask_known || m != mask_live) { pending = m; pending_set = 1; }
    } else if (ok && len >= 32 && (p[31] & 0x80) && (p[31] & 3) != 0 && MC->slot != 0) {
        /* a slave's frame where the master's should be: the correlator locked onto a
         * slave (the master hidden while searching). Start over. */
        frames_wrong++;
        relock = true;
    } else if (!ok) {
        frames_bad++;
    }
    done_ready = false;
}

uint8_t  multi_mask_live(void) { return mask_live; }
uint8_t  multi_mask_announced(void) { return MC->slot == 0 ? announced : mask_live; }
bool     multi_mask_known(void) { return MC->slot == 0 || mask_known; }
uint8_t  multi_guard(void) { return guard; }
uint32_t multi_blind_sweeps(void) { return blind_sweeps; }
uint32_t multi_seen_age_ms(int slot) { return seen_ms[slot] ? sys_ms() - seen_ms[slot] : 0xFFFFFFFFu; }
uint32_t multi_frames_decoded(void) { return frames_ok; }
uint32_t multi_frames_bad(void) { return frames_bad; }
uint32_t multi_frames_wrong(void) { return frames_wrong; }
bool multi_take_relock(void) { const bool r = relock; relock = false; return r; }

/* Live parameters use stock-valid values only: slot 0 = master-tdm, others =
 * slave-optical; the per-slot flash offset is applied inside the timebase. */
void multi_enter(int slot)
{
    MC->active = 1;
    MC->slot = (uint8_t)(slot >= 0 && slot < MULTI_SLOTS ? slot : 0);
    P.timebase_period = 800000;
    P.timebase_mode = MC->slot == 0 ? TB_MASTER_TDM : TB_SLAVE_OPTICAL;
    P.timebase_offset = 0;
    static const uint16_t medium[8] = { 3000, 4000, 3500, 4500, 5000, 6000, 5500, 6500 };
    uint16_t *w[8] = { &P.ootx_j0, &P.ootx_j1, &P.ootx_k0, &P.ootx_k1,
                       &P.ootx_j2, &P.ootx_j3, &P.ootx_k2, &P.ootx_k3 };
    for (int i = 0; i < 8; i++) *w[i] = medium[i];
    multi_reset();
}

void multi_leave(void) { MC->active = 0; multi_reset(); }

uint8_t multi_ootx_mode(void)
{
    /* bit 7 multi, bit 6 policy ALL (every flash carries an OOTX bit), bits 4-5 the
     * stations in the round - 1 (announced), bits 0-1 the slot */
    const int n = popcount4(multi_mask_announced());
    return (uint8_t)(0x80 | (MC->policy == MP_ALL ? 0x40 : 0) | (((n ? n : 1) - 1) << 4) | MC->slot);
}

uint8_t multi_ootx_mask(void) { return multi_mask_announced(); }
