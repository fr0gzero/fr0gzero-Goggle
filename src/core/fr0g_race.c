#include "fr0g_race.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/*
 * Packet (20 cells, page 1): each value cell = 0x30 + v, v = 0..199; two-cell values are hi*200 + lo.
 *  [0] 0xFD marker   [1,2] avg cell voltage, centivolts   [3,4] current, deciamps   [5,6] mAh drawn
 *  [7] LQ 0..100     [8] throttle % 0..100   [9] average motor RPM / 200   [10] G x10
 *  [11,12] timer, tenths of a second   [13] flags: 1 armed, 2 battery warning, 4 battery critical
 *  [14..19] craft name, ASCII (a space arrives as a plain 0x20)
 */

LV_FONT_DECLARE(fr0g_rpm_96);
LV_FONT_DECLARE(fr0g_rpm_144);
LV_FONT_DECLARE(fr0g_ib_20);
LV_FONT_DECLARE(fr0g_ib_26);
LV_FONT_DECLARE(fr0g_ib_30);
LV_FONT_DECLARE(fr0g_ib_34);
LV_FONT_DECLARE(fr0g_ib_39);
LV_FONT_DECLARE(fr0g_ib_51);
LV_FONT_DECLARE(fr0g_lb_13);
LV_FONT_DECLARE(fr0g_lb_20);

#define RED lv_color_hex(0xFF2832)
#define WHT lv_color_hex(0xFFFFFF)
#define NLIGHTS 15

typedef struct {
    lv_obj_t *root;
    lv_obj_t *light[NLIGHTS];
    lv_obj_t *arc, *rpm, *rpm_unit;
    lv_obj_t *val[5]; // batt, curr, link, g, time
    lv_obj_t *craft;
    lv_point_t tick_pts[8][2];
    lv_obj_t *tick[8];
} race_obj_t;

static race_obj_t R[2];
static bool created[2];
static lv_timer_t *timer;

typedef struct {
    int cv, da, mah, lq, thr, rpm200, g10, tenths, flags;
    char name[7];
} race_data_t;

static race_data_t D = {0};
static volatile uint32_t last_seen_ms;
static float thr_s = 0, rpm_s = 0;

static void tick(lv_timer_t *tm);

static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

bool fr0g_race_live(void) { return (now_ms() - last_seen_ms) < 600; }

static lv_obj_t *mk(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *txt) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, txt);
    return l;
}

// a slanted dark panel with a red edge, drawn on a small canvas once
static void panel(lv_obj_t *p, int x, int y, int w, int h, bool right, float s) {
    lv_obj_t *cv = lv_canvas_create(p);
    lv_color_t *buf = lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(w, h));
    lv_canvas_set_buffer(cv, buf, w, h, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_TRANSP);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_black();
    rd.bg_opa = 150;
    const int k = (int)(14 * s), e = (int)(6 * s);
    lv_point_t pts[4];
    if (!right) {
        pts[0] = (lv_point_t){0, 0}; pts[1] = (lv_point_t){w - 1, 0}; pts[2] = (lv_point_t){w - 1 - k, h - 1}; pts[3] = (lv_point_t){0, h - 1};
    } else {
        pts[0] = (lv_point_t){0, 0}; pts[1] = (lv_point_t){w - 1, 0}; pts[2] = (lv_point_t){w - 1, h - 1}; pts[3] = (lv_point_t){k, h - 1};
    }
    lv_canvas_draw_polygon(cv, pts, 4, &rd);
    rd.bg_color = RED;
    rd.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_rect(cv, right ? w - e : 0, 0, e, h, &rd);
    lv_obj_set_pos(cv, x, y);
}

void fr0g_race_create(uint8_t fhd, lv_obj_t *parent) {
    const float s = fhd ? 1.5f : 1.0f;
    const lv_font_t *f_rpm = fhd ? &fr0g_rpm_144 : &fr0g_rpm_96;
    const lv_font_t *f_val = fhd ? &fr0g_ib_39 : &fr0g_ib_26;
    const lv_font_t *f_name = fhd ? &fr0g_ib_51 : &fr0g_ib_34;
    const lv_font_t *f_unit = fhd ? &fr0g_ib_30 : &fr0g_ib_20;
    const lv_font_t *f_lab = fhd ? &fr0g_lb_20 : &fr0g_lb_13;
    race_obj_t *r = &R[fhd];
    const int SW = fhd ? 1920 : 1280, SH = fhd ? 1080 : 720;
#define S(v) ((int)((v) * s))

    r->root = mk(parent);
    lv_obj_set_size(r->root, SW, SH);
    lv_obj_set_pos(r->root, 0, 0);

    // shift lights
    for (int i = 0; i < NLIGHTS; i++) {
        lv_obj_t *o = mk(r->root);
        lv_obj_set_size(o, S(16), S(16));
        lv_obj_set_pos(o, SW / 2 - S(NLIGHTS * 22 / 2) + S(i * 22), S(24));
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        r->light[i] = o;
    }

    // tacho arc, bottom centre
    const int R0 = S(170), cx = SW / 2, cy = SH - S(30);
    r->arc = lv_arc_create(r->root);
    lv_obj_remove_style(r->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(r->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(r->arc, 2 * R0, 2 * R0);
    lv_obj_set_pos(r->arc, cx - R0, cy - R0);
    lv_arc_set_bg_angles(r->arc, 200, 340);
    lv_arc_set_angles(r->arc, 200, 200);
    lv_obj_set_style_arc_width(r->arc, S(14), LV_PART_MAIN);
    lv_obj_set_style_arc_width(r->arc, S(14), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(r->arc, WHT, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(r->arc, 60, LV_PART_MAIN);
    lv_obj_set_style_arc_color(r->arc, WHT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(r->arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(r->arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(r->arc, LV_OPA_TRANSP, 0);
    for (int k = 0; k < 8; k++) {
        const float a = (200.0f + 140.0f * k / 7.0f) * 3.14159265f / 180.0f;
        r->tick_pts[k][0] = (lv_point_t){(lv_coord_t)(cx + (R0 - S(22)) * cosf(a)), (lv_coord_t)(cy + (R0 - S(22)) * sinf(a))};
        r->tick_pts[k][1] = (lv_point_t){(lv_coord_t)(cx + (R0 - S(34)) * cosf(a)), (lv_coord_t)(cy + (R0 - S(34)) * sinf(a))};
        r->tick[k] = lv_line_create(r->root);
        lv_line_set_points(r->tick[k], r->tick_pts[k], 2);
        lv_obj_set_style_line_width(r->tick[k], S(3), 0);
        lv_obj_set_style_line_color(r->tick[k], WHT, 0);
        lv_obj_set_style_line_opa(r->tick[k], 200, 0);
    }
    r->rpm = label(r->root, f_rpm, WHT, "0");
    lv_obj_align(r->rpm, LV_ALIGN_BOTTOM_MID, 0, -S(40));
    r->rpm_unit = label(r->root, f_unit, RED, "k RPM");
    lv_obj_align(r->rpm_unit, LV_ALIGN_BOTTOM_MID, S(92), -S(56));

    // side panels
    static const char *labl[3] = {"BATT", "CURR", "LINK"}, *labr[2] = {"G-FORCE", "TIME"};
    for (int i = 0; i < 3; i++) {
        const int y = SH - S(250) + S(i * 62);
        panel(r->root, S(28), y, S(222), S(50), false, s);
        lv_obj_t *l = label(r->root, f_lab, RED, labl[i]);
        lv_obj_set_pos(l, S(46), y + S(5));
        r->val[i] = label(r->root, f_val, WHT, "--");
        lv_obj_set_pos(r->val[i], S(46), y + S(16));
    }
    for (int i = 0; i < 2; i++) {
        const int y = SH - S(250) + S(i * 62);
        panel(r->root, SW - S(28) - S(222), y, S(222), S(50), true, s);
        lv_obj_t *l = label(r->root, f_lab, RED, labr[i]);
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -S(46), y + S(5));
        r->val[3 + i] = label(r->root, f_val, WHT, "--");
        lv_obj_align(r->val[3 + i], LV_ALIGN_TOP_RIGHT, -S(46), y + S(16));
    }
    r->craft = label(r->root, f_name, WHT, "");
    lv_obj_align(r->craft, LV_ALIGN_TOP_RIGHT, -S(40), S(34));

    lv_obj_add_flag(r->root, LV_OBJ_FLAG_HIDDEN);
    created[fhd] = true;
    if (!timer)
        timer = lv_timer_create(tick, 33, NULL);
#undef S
}

static void tick(lv_timer_t *tm) {
    (void)tm;
    const bool live = fr0g_race_live();
    for (int f = 0; f < 2; f++) {
        if (!created[f])
            continue;
        if (live)
            lv_obj_clear_flag(R[f].root, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(R[f].root, LV_OBJ_FLAG_HIDDEN);
    }
    if (!live)
        return;

    thr_s += (D.thr / 100.0f - thr_s) * 0.35f;
    rpm_s += (D.rpm200 * 200.0f - rpm_s) * 0.35f;
    const uint32_t ms = now_ms();
    const bool flash = thr_s > 0.93f && ((ms / 83) & 1);
    const int lit = (int)(thr_s * NLIGHTS + 0.5f);

    char b[5][24], rb[8];
    snprintf(b[0], sizeof(b[0]), "%d.%02dV", D.cv / 100, D.cv % 100);
    snprintf(b[1], sizeof(b[1]), "%d.%dA", D.da / 10, D.da % 10);
    snprintf(b[2], sizeof(b[2]), "LQ %d", D.lq);
    snprintf(b[3], sizeof(b[3]), "%d.%d", D.g10 / 10, D.g10 % 10);
    snprintf(b[4], sizeof(b[4]), "%02d:%02d.%d", D.tenths / 600, (D.tenths / 10) % 60, D.tenths % 10);
    snprintf(rb, sizeof(rb), "%d", (int)(rpm_s / 1000.0f + 0.5f));
    const bool warn = D.flags & 2, crit = D.flags & 4;

    for (int f = 0; f < 2; f++) {
        if (!created[f])
            continue;
        race_obj_t *r = &R[f];
        for (int i = 0; i < NLIGHTS; i++) {
            lv_color_t c = i < 5 ? lv_color_hex(0x3CFF5A) : i < 10 ? lv_color_hex(0xFFDC28) : i < 14 ? RED : lv_color_hex(0x508CFF);
            bool on = i < lit;
            if (flash) {
                c = lv_color_hex(0x508CFF);
                on = true;
            }
            lv_obj_set_style_bg_color(r->light[i], c, 0);
            lv_obj_set_style_bg_opa(r->light[i], on ? LV_OPA_COVER : 45, 0);
        }
        lv_arc_set_angles(r->arc, 200, 200 + (int)(140 * thr_s));
        lv_obj_set_style_arc_color(r->arc, thr_s > 0.85f ? RED : WHT, LV_PART_INDICATOR);
        lv_label_set_text(r->rpm, rb);
        for (int i = 0; i < 5; i++)
            lv_label_set_text(r->val[i], b[i]);
        lv_obj_set_style_text_color(r->val[0], crit ? RED : warn ? lv_color_hex(0xFFDC28) : WHT, 0);
        lv_label_set_text(r->craft, D.name);
    }
}

static int dec(uint16_t ch) {
    if (ch < 0x100)
        return 0;
    int v = (int)ch - 0x100 - 0x30;
    return v < 0 ? 0 : v > 199 ? 199 : v;
}

uint16_t fr0g_race_filter(int row, int col, uint16_t ch, const uint16_t *rb) {
    if (row != FR0G_RACE_ROW || col < FR0G_RACE_COL || col >= FR0G_RACE_COL + FR0G_RACE_LEN)
        return ch;
    if (rb[FR0G_RACE_COL] != FR0G_RACE_MARK)
        return ch;
    if (col == FR0G_RACE_COL) {
        const uint16_t *p = rb + FR0G_RACE_COL;
        D.cv = dec(p[1]) * 200 + dec(p[2]);
        D.da = dec(p[3]) * 200 + dec(p[4]);
        D.mah = dec(p[5]) * 200 + dec(p[6]);
        D.lq = dec(p[7]);
        D.thr = dec(p[8]);
        D.rpm200 = dec(p[9]);
        D.g10 = dec(p[10]);
        D.tenths = dec(p[11]) * 200 + dec(p[12]);
        D.flags = dec(p[13]);
        int n = 0;
        for (int i = 0; i < 6; i++) {
            const int c = p[14 + i] & 0xFF;
            D.name[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
        }
        while (n > 0 && D.name[n - 1] == ' ')
            n--;
        D.name[n] = '\0';
        last_seen_ms = now_ms();
    }
    return 0x20;
}
