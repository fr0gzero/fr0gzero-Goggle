#pragma once
// fr0gOS "RACE TELEMETRY" HUD, drawn by the goggles from a hidden data packet the FC (fr0gOS R58+) writes on the
// bottom OSD row at column 23, font page 1. See fr0g_race.c for the layout of the packet.
#include <stdbool.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

#define FR0G_RACE_ROW 17
#define FR0G_RACE_COL 23
#define FR0G_RACE_LEN 20
#define FR0G_RACE_MARK (0x100 + 0xFD)

void fr0g_race_create(uint8_t fhd, lv_obj_t *parent);
uint16_t fr0g_race_filter(int row, int col, uint16_t ch, const uint16_t *row_buf);
bool fr0g_race_live(void);
