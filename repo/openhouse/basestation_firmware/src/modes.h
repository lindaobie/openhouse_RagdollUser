/* Channel (mode) table handling - stock button_event @0x3430, ui label @0xC2C0. */
#pragma once
#include <stdbool.h>

int  mode_count(void);          /* clamped to 1..8 like stock */
void mode_next(void);           /* advance mode.current, apply it, save */
void mode_apply(int m);         /* copy mode.m.* into the live parameters */
bool mode_matches_live(void);   /* live config == current mode entry? */
char mode_label(void);
