#pragma once
#include <stdint.h>
#include <stdbool.h>
void i2c_init(void);
bool i2c_xfer(uint8_t addr7, const uint8_t *w, unsigned len, uint8_t *r, unsigned rlen);
