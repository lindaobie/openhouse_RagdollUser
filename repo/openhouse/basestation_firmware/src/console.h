#pragma once
#include <stdbool.h>
void con_printf(const char *fmt, ...);
void console_poll(void);
void console_set_boot_info(int rc, int part);
void console_mode_changed(void);    /* provided by main: refresh display */
bool console_param_set(const char *name, const char *value);
bool console_param_format(const char *name, char *out, unsigned len);
