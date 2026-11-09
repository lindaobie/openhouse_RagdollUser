/* I2C master on P0_4 (SCL) / P0_5 (SDA), 100 kHz. Stock uses an interrupt-
 * driven state machine (I2C_IRQHandler @0x1B5C); a polled master is simpler and
 * sufficient for the two devices (LTC6904 carrier, LIS2DH accelerometer). */
#include "i2c.h"
#include "board.h"
#include "sys.h"

#define I2C_CONSET  REG(0x40000000)
#define I2C_STAT    REG(0x40000004)
#define I2C_DAT     REG(0x40000008)
#define I2C_SCLH    REG(0x40000010)
#define I2C_SCLL    REG(0x40000014)
#define I2C_CONCLR  REG(0x40000018)
#define PRESETCTRL  REG(0x40048004)

#define AA 0x04
#define SI 0x08
#define STO 0x10
#define STA 0x20
#define I2EN 0x40

void i2c_init(void)
{
    SYSAHBCLKCTRL |= (1u << 5);
    PRESETCTRL |= (1u << 1);                /* release I2C reset */
    IOCON_P0(4) = IOCON_FUNC(1);            /* SCL, standard mode */
    IOCON_P0(5) = IOCON_FUNC(1);            /* SDA */
    I2C_CONCLR = AA | SI | STA | I2EN;
    I2C_SCLH = CPU_HZ / 100000 / 2;
    I2C_SCLL = CPU_HZ / 100000 / 2;
    I2C_CONSET = I2EN;
}

static bool wait_si(void)
{
    uint32_t t0 = sys_ms();
    while (!(I2C_CONSET & SI))
        if (sys_ms() - t0 > 5) return false;
    return true;
}

static void stop(void)
{
    I2C_CONSET = STO;
    I2C_CONCLR = SI;
    uint32_t t0 = sys_ms();
    while ((I2C_CONSET & STO) && sys_ms() - t0 < 5) {}
}

/* Write `len` bytes, then optionally read `rlen` bytes (repeated start). */
bool i2c_xfer(uint8_t addr7, const uint8_t *w, unsigned len, uint8_t *r, unsigned rlen)
{
    I2C_CONCLR = SI | STA | AA;
    I2C_CONSET = STA;
    if (!wait_si() || (I2C_STAT != 0x08)) goto fail;
    I2C_DAT = addr7 << 1;
    I2C_CONCLR = STA | SI;
    if (!wait_si() || I2C_STAT != 0x18) goto fail;          /* SLA+W ACK */
    for (unsigned i = 0; i < len; i++) {
        I2C_DAT = w[i];
        I2C_CONCLR = SI;
        if (!wait_si() || I2C_STAT != 0x28) goto fail;      /* data ACK */
    }
    if (rlen) {
        I2C_CONSET = STA;
        I2C_CONCLR = SI;
        if (!wait_si() || I2C_STAT != 0x10) goto fail;      /* repeated start */
        I2C_DAT = (addr7 << 1) | 1;
        I2C_CONCLR = STA | SI;
        if (!wait_si() || I2C_STAT != 0x40) goto fail;      /* SLA+R ACK */
        for (unsigned i = 0; i < rlen; i++) {
            if (i + 1 < rlen) I2C_CONSET = AA; else I2C_CONCLR = AA;
            I2C_CONCLR = SI;
            if (!wait_si()) goto fail;
            r[i] = I2C_DAT;
        }
    }
    stop();
    return true;
fail:
    stop();
    return false;
}
