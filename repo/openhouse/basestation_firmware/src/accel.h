#pragma once
#include <stdbool.h>
bool accel_init(void);
void accel_task(void);
bool accel_flipped(void);
extern float accel_f[3];
