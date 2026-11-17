#include "fr0g_gimbal.h"
#include "fr0g_race.h"

#include <string.h>
#include <sys/time.h>

// "CLEAN" style: rounded cyan box, faint crosshair, white dot with a pink core and a short fading trail.
// Sizes are for the 1280x720 (HD) OSD screen; the FHD screen uses x1.5.
#define BOX_W 140
#define BOX_H 112
#define BOX_GAP 60
#define BOX_BOTTOM 48  // px from the bottom of the 720 screen
#define PAD 10
#define DOT 14
#define CORE 8
#define TRAIL 6
#define TRAIL_DOT 6

#define COL_CYAN lv_color_hex(0x50E6FF)
#define COL_PINK lv_color_hex(0xFF5AC8)
#define COL_WHITE lv_color_hex(0xFFFFFF)
#define COL_PANEL lv_color_hex(0x0A141E)

typedef struct {
    lv_obj_t *box, *hline, *vline, *dot, *core, *trail[TRAIL];
} gimbal_obj_t;

static gimbal_obj_t g_obj[2][2];       // [fhd][stick]
static int g_x_normal[2][2], g_x_race[2][2], g_y[2];
static int g_inner_w[2], g_inner_h[2]; // travel area in px, per screen
static bool g_created[2];
static lv_timer_t *g_timer;

static volatile int g_target[4] = {100, 0, 100, 100}; // yaw, thr, roll, pitch (0..199)
static volatile uint32_t g_last_seen_ms;
static float g_cur[4] = {100, 0, 100, 100};
static float g_hist[TRAIL][4];
static int g_hist_n;

static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

static lv_obj_t *mk(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *mk_circle(lv_obj_t *parent, int d, lv_color_t c, lv_opa_t opa) {
    lv_obj_t *o = mk(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    return o;
}

static void place(uint8_t fhd) {
    const float s = fhd ? 1.5f : 1.0f;
    for (int k = 0; k < 2; k++) {
        gimbal_obj_t *g = &g_obj[fhd][k];
        const float hx = g_cur[k * 2] / 199.0f, hy = g_cur[k * 2 + 1] / 199.0f;
        const int pad = (int)(PAD * s), d = (int)(DOT * s), c = (int)(CORE * s), td = (int)(TRAIL_DOT * s);
        const int x = pad + (int)(hx * g_inner_w[fhd]), y = pad + (int)((1.0f - hy) * g_inner_h[fhd]);
        lv_obj_set_pos(g->dot, x - d / 2, y - d / 2);
        lv_obj_set_pos(g->core, (d - c) / 2, (d - c) / 2);
        for (int t = 0; t < TRAIL; t++) {
            if (t >= g_hist_n) {
                lv_obj_add_flag(g->trail[t], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const int tx = pad + (int)(g_hist[t][k * 2] / 199.0f * g_inner_w[fhd]);
            const int ty = pad + (int)((1.0f - g_hist[t][k * 2 + 1] / 199.0f) * g_inner_h[fhd]);
            lv_obj_clear_flag(g->trail[t], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(g->trail[t], tx - td / 2, ty - td / 2);
        }
    }
}

static void tick(lv_timer_t *t) {
    (void)t;
    const bool live = (now_ms() - g_last_seen_ms) < 600;
    for (uint8_t f = 0; f < 2; f++) {
        if (!g_created[f])
            continue;
        for (int k = 0; k < 2; k++) {
            if (live)
                lv_obj_clear_flag(g_obj[f][k].box, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(g_obj[f][k].box, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!live)
        return;
    const bool race = fr0g_race_live();
    for (uint8_t f = 0; f < 2; f++) {
        if (!g_created[f])
            continue;
        for (int k = 0; k < 2; k++)
            lv_obj_set_pos(g_obj[f][k].box, race ? g_x_race[f][k] : g_x_normal[f][k], g_y[f]);
    }

    // glide toward the latest stick values (the FC sends ~20 a second, this runs ~60)
    float moved = 0;
    for (int i = 0; i < 4; i++) {
        const float d = g_target[i] - g_cur[i];
        g_cur[i] += d * 0.45f;
        moved += d > 0 ? d : -d;
    }
    // trail: newest at the end, oldest dropped
    static int every = 0;
    if (++every >= 2) {
        every = 0;
        if (moved > 1.0f || g_hist_n > 0) {
            if (g_hist_n == TRAIL) {
                memmove(g_hist[0], g_hist[1], sizeof(g_hist[0]) * (TRAIL - 1));
                g_hist_n--;
            }
            if (moved > 1.0f) {
                memcpy(g_hist[g_hist_n++], g_cur, sizeof(g_cur));
            } else if (g_hist_n > 0) {
                memmove(g_hist[0], g_hist[1], sizeof(g_hist[0]) * (g_hist_n - 1)); // fade out when still
                g_hist_n--;
            }
        }
    }
    for (uint8_t f = 0; f < 2; f++) {
        if (g_created[f])
            place(f);
    }
}

void fr0g_gimbal_create(uint8_t fhd, lv_obj_t *parent, int offset_x, int offset_y) {
    (void)offset_x;
    (void)offset_y;
    const float s = fhd ? 1.5f : 1.0f;
    const int scr_w = fhd ? 1920 : 1280, scr_h = fhd ? 1080 : 720;
    const int bw = (int)(BOX_W * s), bh = (int)(BOX_H * s), gap = (int)(BOX_GAP * s);
    const int y = scr_h - (int)(BOX_BOTTOM * s) - bh;
    const int x0 = scr_w / 2 - gap / 2 - bw;
    g_inner_w[fhd] = bw - 2 * (int)(PAD * s);
    g_inner_h[fhd] = bh - 2 * (int)(PAD * s);

    for (int k = 0; k < 2; k++) {
        gimbal_obj_t *g = &g_obj[fhd][k];
        g->box = mk(parent);
        lv_obj_set_size(g->box, bw, bh);
        lv_obj_set_pos(g->box, x0 + k * (bw + gap), y);
        g_x_normal[fhd][k] = x0 + k * (bw + gap);
        // race HUD: either side of the tacho, between it and the side panels
        g_x_race[fhd][k] = k == 0 ? scr_w / 2 - (int)(200 * s) - bw : scr_w / 2 + (int)(200 * s);
        g_y[fhd] = y;
        lv_obj_set_style_radius(g->box, (int)(14 * s), 0);
        lv_obj_set_style_bg_color(g->box, COL_PANEL, 0);
        lv_obj_set_style_bg_opa(g->box, 70, 0);
        lv_obj_set_style_border_color(g->box, COL_CYAN, 0);
        lv_obj_set_style_border_width(g->box, fhd ? 3 : 2, 0);
        lv_obj_set_style_border_opa(g->box, 230, 0);

        g->hline = mk(g->box);
        lv_obj_set_size(g->hline, bw - (int)(16 * s), fhd ? 2 : 1);
        lv_obj_set_pos(g->hline, (int)(8 * s), bh / 2);
        lv_obj_set_style_bg_color(g->hline, COL_CYAN, 0);
        lv_obj_set_style_bg_opa(g->hline, 70, 0);
        g->vline = mk(g->box);
        lv_obj_set_size(g->vline, fhd ? 2 : 1, bh - (int)(16 * s));
        lv_obj_set_pos(g->vline, bw / 2, (int)(8 * s));
        lv_obj_set_style_bg_color(g->vline, COL_CYAN, 0);
        lv_obj_set_style_bg_opa(g->vline, 70, 0);

        for (int t = 0; t < TRAIL; t++) {
            g->trail[t] = mk_circle(g->box, (int)(TRAIL_DOT * s), COL_PINK, (lv_opa_t)(40 + 30 * t));
            lv_obj_add_flag(g->trail[t], LV_OBJ_FLAG_HIDDEN);
        }
        g->dot = mk_circle(g->box, (int)(DOT * s), COL_WHITE, LV_OPA_COVER);
        lv_obj_set_style_shadow_color(g->dot, COL_PINK, 0);
        lv_obj_set_style_shadow_width(g->dot, (int)(8 * s), 0);
        lv_obj_set_style_shadow_opa(g->dot, 150, 0);
        g->core = mk_circle(g->dot, (int)(CORE * s), COL_PINK, LV_OPA_COVER);
        lv_obj_add_flag(g->box, LV_OBJ_FLAG_HIDDEN);
    }
    g_created[fhd] = true;
    place(fhd);
    if (!g_timer)
        g_timer = lv_timer_create(tick, 16, NULL);
}

uint16_t fr0g_gimbal_filter(int row, int col, uint16_t ch, const uint16_t *row_buf) {
    if (row != FR0G_STK_ROW || col < FR0G_STK_COL || col >= FR0G_STK_COL + FR0G_STK_LEN)
        return ch;
    if (row_buf[FR0G_STK_COL] != FR0G_STK_MARK)
        return ch;
    if (col == FR0G_STK_COL) {
        for (int i = 0; i < 4; i++) {
            int v = (int)row_buf[FR0G_STK_COL + 1 + i] - 0x100 - 0x30;
            if (v < 0)
                v = 0;
            if (v > 199)
                v = 199;
            g_target[i] = v;
        }
        g_last_seen_ms = now_ms();
    }
    return 0x20;
}
