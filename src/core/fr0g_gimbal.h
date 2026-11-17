#pragma once
// fr0gOS: stick gimbals drawn by the goggles.
// The FC (fr0gOS R57+) writes five hidden cells on the bottom OSD row, font page 1:
//   col FR0G_STK_COL: marker 0xFE, then yaw, throttle, roll, pitch as 0x30 + (0..199).
// The goggles blank those cells and draw two smooth "clean" gimbals from the values.
#include <stdbool.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

#define FR0G_STK_ROW 17
#define FR0G_STK_COL 17
#define FR0G_STK_LEN 5
#define FR0G_STK_MARK (0x100 + 0xFE)

void fr0g_gimbal_create(uint8_t fhd, lv_obj_t *parent, int offset_x, int offset_y);
// called from the OSD thread for every cell; returns the char to draw (0x20 for the hidden data cells)
uint16_t fr0g_gimbal_filter(int row, int col, uint16_t ch, const uint16_t *row_buf);
