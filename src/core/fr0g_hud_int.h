#pragma once
// fr0gOS HUD internals shared between the themes (fr0g_hud.c = Science Teal LCARS, fr0g_gouache.c = gouache almanac)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lvgl/lvgl.h>

typedef struct {
    int stk[4];
    int cv, da, mah, lq, thr, rpm200, g10, tenths, flags;
    int wsev;
    char wtxt[16];
    char name[8];
    int roll, pitch;   // decidegrees
    int mot[4];        // RPM, Betaflight motor order
    bool att_ok;
} fr0g_data_t;
extern fr0g_data_t D;

typedef struct {
    char e[16], v[12];
    lv_color_t c;
    uint16_t tenths;
    uint32_t born;
} ev_t;
#define NLOG 5
extern ev_t EV[NLOG];
extern int n_ev;

#define CPU_HOT_C 78   // Betaflight warns at 70 C; only bother Tom from 78 C
int core_temp(const char *raw);
bool wrn_ignored(const char *raw);
void wrn_map(const char *raw, char *title, size_t tn, char *hint, size_t hn);

#define FR0G_VERSION "2.6"
typedef struct {
    bool show;
    int visible;   // how many lines have typed in so far (title = line -1)
    int n;
    char title[24], craft[32], line[4][16], val[4][16];
    uint8_t ok[4];   // 0 = waiting / bad, 2 = good
} fr0g_boot_t;
extern fr0g_boot_t BOOT;

void fr0g_gouache_create(uint8_t fhd, lv_obj_t *parent);
void fr0g_gouache_tick(uint32_t ms, int64_t card_t, bool show);
