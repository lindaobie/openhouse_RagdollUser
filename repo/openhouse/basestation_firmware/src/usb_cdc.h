#pragma once
#include <stdbool.h>

bool usb_cdc_init(void);
bool usb_cdc_connected(void);
int  usb_cdc_getc(void);                         /* -1 if nothing received */
void usb_cdc_write(const char *s, unsigned n);  /* dropped if no host attached */
void usb_cdc_shutdown(void);                    /* call before iap_reinvoke_isp() */
