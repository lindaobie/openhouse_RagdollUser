/* LIS2DH accelerometer at I2C 0x18. Stock: lis2dh init @0xA32C, read @0xA2EC,
 * accel task @0x2B08 (every 10 ms), display flip @0x2AC8. */
#include "accel.h"
#include "i2c.h"
#include "params.h"
#include "ootx.h"
#include "sys.h"

#define LIS2DH 0x18

float accel_f[3];                   /* filtered m/s^2, board axes */
static bool flipped;

static bool reg_write(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = { reg, v };
    return i2c_xfer(LIS2DH, b, 2, 0, 0);
}

static bool reg_modify(uint8_t reg, uint8_t set, uint8_t clr)   /* stock @0xA404 */
{
    uint8_t v;
    if (!i2c_xfer(LIS2DH, &reg, 1, &v, 1)) return false;
    return reg_write(reg, (v & ~clr) | set);
}

static float fabs_(float x) { return __builtin_fabsf(x); }

static float sqrt_(float x)                         /* Newton, plenty for |a| */
{
    if (x <= 0) return 0;
    float r = x > 1 ? x : 1;
    for (int i = 0; i < 12; i++) r = 0.5f * (r + x / r);
    return r;
}

bool accel_init(void)
{
    uint8_t reg = 0x0F, who = 0;
    if (!i2c_xfer(LIS2DH, &reg, 1, &who, 1) || who != 0x33) return false;  /* WHO_AM_I */
    if (!reg_modify(0x24, 0x80, 0)) return false;   /* CTRL_REG5: BOOT */
    sys_delay_ms(10);
    if (!reg_modify(0x24, 0, 0x80)) return false;
    sys_delay_ms(10);
    return reg_write(0x20, 0x97) &&                 /* CTRL_REG1: 1.344 kHz, XYZ */
           reg_write(0x2E, 0x00) &&                 /* FIFO off */
           reg_modify(0x1F, 0xC0, 0) &&             /* temperature sensor on */
           reg_modify(0x23, 0x80, 0);               /* CTRL_REG4: block data update */
}

/* every 10 ms (stock task @0x2B08) */
void accel_task(void)
{
    if (!P.accel_enable) {
        P.accel_is_gravity = false;
        ootx_accel[0] = ootx_accel[1] = ootx_accel[2] = 0;
        return;
    }
    uint8_t reg = 0x28 | 0x80;                      /* OUT_X_L, auto-increment */
    int16_t raw[3];
    if (!i2c_xfer(LIS2DH, &reg, 1, (uint8_t *)raw, 6)) return;
    /* board axes as stock: x = -raw_y, y = raw_x, z = raw_z; 1 g = 16384 */
    int16_t a[3] = { (int16_t)-raw[1], raw[0], raw[2] };
    float b1 = P.accel_b1;
    float mag2 = 0;
    for (int i = 0; i < 3; i++) {
        float ms2 = 9.8f * (float)a[i] / 16384.0f;
        accel_f[i] = b1 * accel_f[i] + (1.0f - b1) * ms2;
        mag2 += accel_f[i] * accel_f[i];
    }
    float mag = sqrt_(mag2);
    P.accel_magnitude = mag;
    P.accel_is_gravity = mag >= 7.84f && mag < 11.76f;      /* 0.8 .. 1.2 g */
    if (P.accel_is_gravity) {
        float m = fabs_(accel_f[0]);
        if (fabs_(accel_f[1]) > m) m = fabs_(accel_f[1]);
        if (fabs_(accel_f[2]) > m) m = fabs_(accel_f[2]);
        int8_t *dir[3] = { &P.accel_dir_x, &P.accel_dir_y, &P.accel_dir_z };
        for (int i = 0; i < 3; i++) {
            *dir[i] = (int8_t)(accel_f[i] / m * 127.0f);
            ootx_accel[i] = (uint8_t)*dir[i];
        }
        /* stock flip with hysteresis on the raw board-y (= raw x) value */
        flipped = a[1] < (flipped ? 5000 : -5000);
    }
}

bool accel_flipped(void) { return flipped; }
