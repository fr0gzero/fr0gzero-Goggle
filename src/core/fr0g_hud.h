#pragma once
// fr0gOS 2 goggle HUD: drawn in the black bars either side of a 4:3 picture, from two hidden data strings the
// fr0gOS flight controller writes on the bottom OSD row (font page 1). See fr0g_hud.c for the packet layout.
#include <stdint.h>
#include <lvgl/lvgl.h>

void fr0g_hud_create(uint8_t fhd, lv_obj_t *parent);
// called by the OSD thread for every FC cell; returns the char to draw (0x20 for the hidden data cells)
uint16_t fr0g_hud_filter(int row, int col, uint16_t ch, const uint16_t *row_buf);
