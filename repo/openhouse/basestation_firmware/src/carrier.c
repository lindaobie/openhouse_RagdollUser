/* IR sync carrier: LTC6904 programmable oscillator at I2C 0x17, output gated by
 * P0_20. Stock carrier_init @0x36B8, ltc6904_set_freq @0xA48C. */
#include "carrier.h"
#include "i2c.h"
#include "board.h"

#define LTC6904_ADDR 0x17   /* stock passes ADR=1 -> 8-bit 0x2E */
#define CNF_CLK_ONLY 2      /* CLK on, /CLK off (stock) */

/* f = 2^OCT * 2078 Hz / (2 - DAC/1024); octave ranges from stock table @0x106A8 */
static const uint32_t oct_min[16] = {
    1039, 2078, 4156, 8312, 16620, 33250, 66500, 133000, 266000, 532000,
    1064000, 2128000, 4256000, 8511000, 17020000, 34050000 };
static const uint32_t oct_max[16] = {
    2076, 4152, 8304, 16610, 32220, 66430, 132900, 265700, 531400, 1063000,
    2126000, 4252000, 8503000, 17010000, 34010000, 68030000 };

bool carrier_set(uint32_t hz)
{
    int oct;
    for (oct = 0; oct < 16; oct++)
        if (hz >= oct_min[oct] && hz <= oct_max[oct]) break;
    if (oct == 16) return false;
    /* same float arithmetic as stock: DAC = 2048 - 2078 * 2^(10+OCT) / f, truncated */
    float x = (float)(1u << (oct + 10)) / (float)hz * 2078.0f;
    unsigned dac = (unsigned)(2048.0f - x) & 0x3FF;
    uint8_t b[2] = {
        (uint8_t)((oct << 4) | (dac >> 6)),
        (uint8_t)(((dac << 2) & 0xFC) | CNF_CLK_ONLY),
    };
    return i2c_xfer(LTC6904_ADDR, b, 2, 0, 0);
}

void carrier_enable(bool on) { gpio_write(PIN_CARRIER_EN, on); }
