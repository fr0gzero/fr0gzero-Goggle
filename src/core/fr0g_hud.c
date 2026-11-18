#include "fr0g_hud.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/*
 * fr0gOS 2.2 goggle HUD - "Science Teal" LCARS.
 * Packet (OSD row 17, font page 1; each value cell 0x30 + v, v = 0..199; two-cell values hi * 200 + lo):
 *   col 0:  0xFC, severity, Betaflight warning text (15)
 *   col 17: 0xFE, yaw, throttle, roll, pitch (0..199)
 *   col 23: 0xFD, cell cV (2), current dA (2), mAh (2), LQ %, throttle %, RPM / 200, G x10, armed tenths (2),
 *           flags (1 armed, 2 batt warn, 4 batt crit, 8 angle, 16 horizon, 32 air), craft name (6)
 */
#define ROW 17
#define STK_COL 17
#define STK_LEN 5
#define DAT_COL 23
#define DAT_LEN 20
#define MARK_STK (0x100 + 0xFE)
#define MARK_DAT (0x100 + 0xFD)
#define WRN_COL 0
#define WRN_LEN 17
#define MARK_WRN (0x100 + 0xFC)

// Science Teal (Mega Drive levels 0,52,87,116,144,172,206,255)
#define C_FRAME lv_color_hex(0x00CEAC)
#define C_FRAME2 lv_color_hex(0x74ACAC)
#define C_ACC lv_color_hex(0x90FFCE)
#define C_LAB lv_color_hex(0x749090)
#define C_VAL lv_color_hex(0x90FFCE)
#define C_VAL2 lv_color_hex(0xFFAC74)
#define C_OK lv_color_hex(0x00CE57)
#define C_WARN lv_color_hex(0xFFCE00)
#define C_RED lv_color_hex(0xCE0000)
#define C_AIR lv_color_hex(0x90FFCE)
#define C_MODE lv_color_hex(0xCECEAC)
#define C_THR lv_color_hex(0x00AC90)
#define C_WHT lv_color_hex(0xFFFFFF)
#define C_BLK lv_color_hex(0x000000)
#define C_HEART lv_color_hex(0xFF5757)
#define DIM(c, k) lv_color_mix((c), C_BLK, (k))   // k/255 of the colour, opaque

LV_FONT_DECLARE(fr0g_cbl_22); LV_FONT_DECLARE(fr0g_cbl_33);
LV_FONT_DECLARE(fr0g_cbl_34); LV_FONT_DECLARE(fr0g_cbl_51);
LV_FONT_DECLARE(fr0g_cbl_36); LV_FONT_DECLARE(fr0g_cbl_54);
LV_FONT_DECLARE(fr0g_cbl_26); LV_FONT_DECLARE(fr0g_cbl_39);
LV_FONT_DECLARE(fr0g_cbl_20); LV_FONT_DECLARE(fr0g_cbl_30);
LV_FONT_DECLARE(fr0g_cbl_24);
LV_FONT_DECLARE(fr0g_cb_11); LV_FONT_DECLARE(fr0g_cb_17);
LV_FONT_DECLARE(fr0g_cb_13); LV_FONT_DECLARE(fr0g_cb_20);
LV_FONT_DECLARE(fr0g_sb_18); LV_FONT_DECLARE(fr0g_sb_27);
LV_FONT_DECLARE(fr0g_sb_15); LV_FONT_DECLARE(fr0g_sb_23);
LV_FONT_DECLARE(fr0g_b_15); LV_FONT_DECLARE(fr0g_b_23);

enum { F_IDLE, F_BLINK, F_BLINKH, F_GL, F_GR, F_GU, F_GD, F_GRIN, F_EXCL, F_HURT, F_KO, F_PUMPED, F_DIZZY, F_N };
LV_IMG_DECLARE(fr0g_frog_idle); LV_IMG_DECLARE(fr0g_frog_blink); LV_IMG_DECLARE(fr0g_frog_blinkH);
LV_IMG_DECLARE(fr0g_frog_glanceL); LV_IMG_DECLARE(fr0g_frog_glanceR); LV_IMG_DECLARE(fr0g_frog_glanceU);
LV_IMG_DECLARE(fr0g_frog_glanceD); LV_IMG_DECLARE(fr0g_frog_grin); LV_IMG_DECLARE(fr0g_frog_exclgrin);
LV_IMG_DECLARE(fr0g_frog_hurt); LV_IMG_DECLARE(fr0g_frog_ko); LV_IMG_DECLARE(fr0g_frog_pumped);
LV_IMG_DECLARE(fr0g_frog_dizzy); LV_IMG_DECLARE(fr0g_heart);
static const lv_img_dsc_t *FROG[F_N] = {&fr0g_frog_idle, &fr0g_frog_blink, &fr0g_frog_blinkH, &fr0g_frog_glanceL,
                                        &fr0g_frog_glanceR, &fr0g_frog_glanceU, &fr0g_frog_glanceD, &fr0g_frog_grin,
                                        &fr0g_frog_exclgrin, &fr0g_frog_hurt, &fr0g_frog_ko, &fr0g_frog_pumped, &fr0g_frog_dizzy};

#define NSEG 12
#define NFLOAT 5
#define NLOG 5
#define NTRAIL 12
#define NHIST 120   // graph: 30 s at 4 Hz
static lv_color_t seg_col(int k, bool lit) {
    const lv_color_t c = k < 2 ? C_RED : k < 4 ? C_WARN : C_OK;
    return lit ? c : DIM(c, 56);
}

typedef struct {
    float s;
    lv_obj_t *root, *frog, *name, *model;
    lv_obj_t *volt, *pct, *seg[NSEG], *mah, *amp;
    lv_obj_t *graph;
    lv_color_t *gbuf;
    int gw, gh;
    lv_obj_t *lg_bar[NLOG], *lg_t[NLOG], *lg_e[NLOG], *lg_v[NLOG];
    int lg_x[4];
    lv_obj_t *gim[2], *gdot[2], *trail[2][NTRAIL], *gval[2][2];
    int gim_in, gim_pad;
    lv_obj_t *tim, *blk[3], *blk_txt[3];
    lv_obj_t *rpm_fill, *thr_fill, *thr_pct, *rpm_val;
    int bar_h;
    lv_obj_t *wrn, *wcap[2], *wmain, *wrn_txt, *hint, *hint_txt, *edge[2];
    const lv_font_t *f_wrn, *f_hint;
    int wrn_y;
    lv_obj_t *card, *c_title, *c_heart, *c_yt, *c_gh, *c_float[NFLOAT];
    int card_y, heart_x, heart_y;
} hud_t;

static hud_t U[2];
static bool created[2];
static lv_timer_t *timer;
static void tick(lv_timer_t *tm);
static lv_color_t gbuf_hd[136 * 82], gbuf_fhd[204 * 123];

static struct {
    int stk[4];
    int cv, da, mah, lq, thr, rpm200, g10, tenths, flags;
    int wsev;
    char wtxt[16];
    char name[8];
} D = {.stk = {100, 0, 100, 100}};
static volatile uint32_t seen_ms;

static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}
static float clampf(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// ---------------- small helpers ----------------
static lv_obj_t *box(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
// a flat, opaque LCARS block
static lv_obj_t *fill(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r) {
    lv_obj_t *o = box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
// a transparent strip whose children are clipped to its pill ends (LCARS end caps; gaps show the picture)
static lv_obj_t *strip(lv_obj_t *p, int h) {
    lv_obj_t *o = box(p);
    lv_obj_set_height(o, h);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(o, true, 0);
    return o;
}
static lv_obj_t *text(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *s) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, s ? s : "");
    return l;
}
// put a label's baseline at y, x as its left (mode 0), right (1) or centre (2) edge, inside a bar of width g_bar
static int g_bar;
static void at(lv_obj_t *l, int x, int base, int mode) {
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    const int top = base - (f->line_height - f->base_line);
    if (mode == 0)
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, top);
    else if (mode == 1)
        lv_obj_align(l, LV_ALIGN_TOP_RIGHT, x - g_bar, top);
    else
        lv_obj_align(l, LV_ALIGN_TOP_MID, x - g_bar / 2, top);
}
// only touch LVGL when something really changed (every touch is a redraw)
static void set_txt(lv_obj_t *l, const char *s) {
    if (strcmp(lv_label_get_text(l), s) != 0)
        lv_label_set_text(l, s);
}
static void set_bg(lv_obj_t *o, lv_color_t c) {
    if (lv_obj_get_style_bg_color(o, 0).full != c.full)
        lv_obj_set_style_bg_color(o, c, 0);
}
static void set_tc(lv_obj_t *o, lv_color_t c) {
    if (lv_obj_get_style_text_color(o, 0).full != c.full)
        lv_obj_set_style_text_color(o, c, 0);
}
static void set_hidden(lv_obj_t *o, bool h) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != h) {
        if (h)
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------- create ----------------
static void make_gimbal(hud_t *u, lv_obj_t *bar, int k, int x, int y, const char *l_top, const char *l_bot, float s) {
#define S(v) ((int)lrintf((v) * s))
    const int sz = S(128), L = S(14), t = S(2);
    lv_obj_t *g = box(bar);
    lv_obj_set_pos(g, x, y);
    lv_obj_set_size(g, sz, sz);
    // corner brackets only
    fill(g, 0, 0, L, t, C_FRAME2, 0);
    fill(g, 0, 0, t, L, C_FRAME2, 0);
    fill(g, sz - L, 0, L, t, C_FRAME2, 0);
    fill(g, sz - t, 0, t, L, C_FRAME2, 0);
    fill(g, 0, sz - t, L, t, C_FRAME2, 0);
    fill(g, 0, sz - L, t, L, C_FRAME2, 0);
    fill(g, sz - L, sz - t, L, t, C_FRAME2, 0);
    fill(g, sz - t, sz - L, t, L, C_FRAME2, 0);
    const int hl = s > 1.2f ? 2 : 1;
    fill(g, S(8), sz / 2, sz - S(16), hl, DIM(C_FRAME2, 90), 0);
    fill(g, sz / 2, S(8), hl, sz - S(16), DIM(C_FRAME2, 90), 0);
    for (int i = 0; i < NTRAIL; i++) {
        const int d = S(2) + (S(6) * i) / NTRAIL;
        lv_obj_t *o = fill(g, 0, 0, d, d, lv_color_mix(C_ACC, C_BLK, (uint8_t)(30 + 170 * i / NTRAIL)), LV_RADIUS_CIRCLE);
        u->trail[k][i] = o;
    }
    lv_obj_t *dot = fill(g, 0, 0, S(14), S(14), C_WHT, LV_RADIUS_CIRCLE);
    lv_obj_t *core = fill(dot, 0, 0, S(6), S(6), C_ACC, LV_RADIUS_CIRCLE);
    lv_obj_center(core);
    u->gim[k] = g;
    u->gdot[k] = dot;
    const lv_font_t *f = s > 1.2f ? &fr0g_cb_17 : &fr0g_cb_11;
    at(text(bar, f, C_LAB, l_top), x, y - S(4), 0);
    at(text(bar, f, C_LAB, l_bot), x, y + sz + S(14), 0);
    u->gval[k][0] = text(bar, f, C_VAL, "");
    at(u->gval[k][0], x + sz, y - S(4), 1);
    u->gval[k][1] = text(bar, f, C_VAL, "");
    at(u->gval[k][1], x + sz, y + sz + S(14), 1);
#undef S
}

void fr0g_hud_create(uint8_t fhd, lv_obj_t *parent) {
    hud_t *u = &U[fhd];
    const float s = fhd ? 1.5f : 1.0f;
    u->s = s;
#define S(v) ((int)lrintf((v) * s))
    const int SW = fhd ? 1920 : 1280, SH = fhd ? 1080 : 720, BAR = S(160);
    g_bar = BAR;
    const lv_font_t *f_name = fhd ? &fr0g_cbl_33 : &fr0g_cbl_22, *f_volt = fhd ? &fr0g_cbl_51 : &fr0g_cbl_34,
                    *f_tim = fhd ? &fr0g_cbl_54 : &fr0g_cbl_36, *f_pct = fhd ? &fr0g_cbl_39 : &fr0g_cbl_26,
                    *f_rpm = fhd ? &fr0g_cbl_30 : &fr0g_cbl_20, *f_lab = fhd ? &fr0g_cb_17 : &fr0g_cb_11,
                    *f_blk = fhd ? &fr0g_cb_20 : &fr0g_cb_13, *f_val = fhd ? &fr0g_sb_27 : &fr0g_sb_18,
                    *f_small = fhd ? &fr0g_sb_23 : &fr0g_sb_15;

    u->root = box(parent);
    lv_obj_set_size(u->root, SW, SH);
    // the OSD screen has theme padding; sit the HUD on the real screen edges
    lv_obj_set_pos(u->root, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN), -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));

    // ================= left bar =================
    lv_obj_t *L = fill(u->root, 0, 0, BAR, SH, C_BLK, 0);
    u->frog = lv_img_create(L);
    lv_img_set_src(u->frog, FROG[F_IDLE]);
    lv_img_set_antialias(u->frog, true);
    lv_img_set_pivot(u->frog, 0, 0);
    lv_img_set_zoom(u->frog, (uint16_t)lrintf(256.0f * S(112) / 210.0f));
    lv_obj_set_pos(u->frog, S(10), S(8));
    const int fy = S(8) + S(102);
    // craft name: LCARS block flush to the left screen edge (clipped flat by the bar), rounded right end
    lv_obj_t *nb = fill(L, -S(20), fy + S(8), BAR - S(8) + S(20), S(28), C_FRAME, LV_RADIUS_CIRCLE);
    u->name = text(nb, f_name, C_BLK, "");
    lv_obj_align(u->name, LV_ALIGN_LEFT_MID, S(20) + S(12), 0);
    u->model = text(nb, f_lab, C_BLK, "");
    lv_obj_align(u->model, LV_ALIGN_RIGHT_MID, -S(12), 0);
    // battery
    const int by = fy + S(50);
    u->volt = text(L, f_volt, C_WHT, "");
    at(u->volt, S(12), by + S(30), 0);
    at(text(L, f_lab, C_LAB, "V"), S(12) + lv_txt_get_width("4.02", 4, f_volt, 0, LV_TEXT_FLAG_NONE) + S(3), by + S(30), 0);
    u->pct = text(L, f_val, C_VAL, "");
    at(u->pct, BAR - S(12), by + S(30), 1);
    const int gy = by + S(40), gap = S(3);
    for (int k = 0; k < NSEG; k++) {
        const int x0 = S(12) + (k * (BAR - S(24) + gap)) / NSEG, x1 = S(12) + ((k + 1) * (BAR - S(24) + gap)) / NSEG - gap;
        u->seg[k] = fill(L, x0, gy, x1 - x0, S(12), seg_col(k, false), S(3));
    }
    u->mah = text(L, f_val, C_WHT, "");
    at(u->mah, S(12), gy + S(34), 0);
    at(text(L, f_lab, C_LAB, "MAH"), S(54), gy + S(34), 0);
    u->amp = text(L, f_val, C_VAL2, "");
    at(u->amp, BAR - S(12), gy + S(34), 1);
    // graph: link quality (line) over amps (filled), last 30 s
    const int gy2 = gy + S(50);
    fill(L, S(12), gy2, BAR - S(24), S(3), C_FRAME2, 0);
    at(text(L, f_lab, C_VAL, "LINK"), S(12), gy2 + S(18), 0);
    at(text(L, f_lab, C_VAL2, "AMPS"), S(46), gy2 + S(18), 0);
    at(text(L, f_lab, C_LAB, "30 S"), BAR - S(12), gy2 + S(18), 1);
    u->gw = BAR - S(24);
    u->gh = S(82);
    u->gbuf = fhd ? gbuf_fhd : gbuf_hd;
    u->graph = lv_canvas_create(L);
    lv_canvas_set_buffer(u->graph, u->gbuf, u->gw, u->gh, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(u->graph, C_BLK, LV_OPA_COVER);
    lv_obj_set_pos(u->graph, S(12), gy2 + S(26));
    const int gy3 = gy2 + S(26) + u->gh;
    fill(L, S(12), gy3 + S(6), BAR - S(24), S(2), DIM(C_FRAME2, 128), 0);
    // combat log
    const int ly = gy3 + S(30);
    at(text(L, f_lab, C_LAB, "LOG"), S(12), ly - S(4), 0);
    fill(L, S(40), ly - S(9), BAR - S(52), fhd ? 2 : 1, DIM(C_FRAME2, 100), 0);
    const lv_font_t *f_lt = f_lab, *f_le = f_blk, *f_lv = f_lab;
    for (int i = 0; i < NLOG; i++) {
        const int y = ly + S(14) + i * S(24);
        u->lg_bar[i] = fill(L, S(12), y - S(11), S(3), S(14), C_FRAME, 0);
        u->lg_t[i] = text(L, f_lt, C_LAB, "");
        at(u->lg_t[i], S(20), y, 0);
        u->lg_e[i] = text(L, f_le, C_WHT, "");
        at(u->lg_e[i], S(20) + lv_txt_get_width("00:00", 5, f_lt, 0, LV_TEXT_FLAG_NONE) + S(6), y, 0);
        u->lg_v[i] = text(L, f_lv, C_VAL, "");
        at(u->lg_v[i], BAR - S(12), y, 1);
        lv_obj_add_flag(u->lg_bar[i], LV_OBJ_FLAG_HIDDEN);
    }
    u->gim_pad = S(10);
    u->gim_in = S(128) - 2 * S(10);
    make_gimbal(u, L, 0, S(16), SH - S(148), "YAW", "THR", s);

    // ================= right bar =================
    lv_obj_t *R = fill(u->root, SW - BAR, 0, BAR, SH, C_BLK, 0);
    u->tim = text(R, f_tim, C_WHT, "");
    at(u->tim, BAR / 2, S(40), 2);
    const int sy = S(54);
    for (int k = 0; k < 3; k++) {
        // pills with a rounded left end that run off the screen edge (clipped flat by the bar)
        u->blk[k] = fill(R, S(8), sy + k * S(28), BAR, S(22), DIM(C_FRAME2, 60), LV_RADIUS_CIRCLE);
        u->blk_txt[k] = text(u->blk[k], f_blk, C_BLK, "");
        lv_obj_align(u->blk_txt[k], LV_ALIGN_RIGHT_MID, -S(22), 0);   // block runs 8 px past the bar; text ends 14 px in
    }
    // LCARS elbow: header arm, then the vertical arm is the RPM gauge, throttle bar beside it
    const int ey = sy + 3 * S(28) + S(8), armw = S(30), armh = S(14), Rr = S(22), ybot = SH - S(236);
    lv_obj_t *elb = fill(R, S(8), ey, BAR + S(60), ybot + S(60) - ey, C_FRAME2, Rr);
    fill(R, S(8) + armw, ey + armh, BAR + S(60), ybot + S(120) - ey, C_BLK, Rr / 2);
    lv_obj_t *el = text(elb, f_lab, C_BLK, "THROTTLE / RPM");
    lv_obj_align(el, LV_ALIGN_TOP_RIGHT, -(S(60) + S(20)), (armh - lv_font_get_line_height(f_lab)) / 2 + 1);
    const int ry0 = ey + armh + Rr;
    fill(R, S(8), ry0, armw, SH - ry0, C_BLK, 0);
    u->bar_h = ybot - (ry0 + S(4));
    lv_obj_t *rt = fill(R, S(8), ry0 + S(4), armw, u->bar_h, DIM(C_FRAME2, 64), S(6));
    u->rpm_fill = fill(rt, 0, 0, armw, 0, C_FRAME2, S(6));
    lv_obj_align(u->rpm_fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    for (int k = 1; k < 5; k++)
        fill(rt, armw - S(6), u->bar_h - u->bar_h * k / 5 - S(1), S(6), S(2), C_BLK, 0);
    const int tx0 = S(8) + armw + S(14), tx1 = BAR - S(14);
    lv_obj_t *tb = fill(R, tx0, ry0 + S(4), tx1 - tx0, u->bar_h, DIM(C_THR, 46), 0);
    u->thr_fill = fill(tb, 0, 0, tx1 - tx0, 0, C_THR, 0);
    lv_obj_align(u->thr_fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_side(u->thr_fill, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(u->thr_fill, S(3), 0);
    lv_obj_set_style_border_color(u->thr_fill, C_WHT, 0);
    u->thr_pct = text(R, f_pct, C_WHT, "");
    at(u->thr_pct, tx1, ybot + S(34), 1);
    at(text(R, f_lab, C_LAB, "THROTTLE"), tx1, ybot + S(50), 1);
    u->rpm_val = text(R, f_rpm, C_FRAME2, "");
    at(u->rpm_val, S(8), ybot + S(34), 0);
    at(text(R, f_lab, C_LAB, "K RPM"), S(8), ybot + S(50), 0);
    make_gimbal(u, R, 1, BAR - S(144), SH - S(148), "ROLL", "PITCH", s);

    // ================= over the picture =================
    for (int k = 0; k < 2; k++) {
        u->edge[k] = fill(u->root, k == 0 ? BAR : SW - BAR - S(5), 0, S(5), SH, C_RED, 0);
        lv_obj_add_flag(u->edge[k], LV_OBJ_FLAG_HIDDEN);
    }
    // warning: LCARS strip [cap][ TITLE ][cap] + a small black chip with the hint underneath
    u->wrn_y = S(18);
    u->wrn = strip(u->root, S(34));
    u->wcap[0] = fill(u->wrn, 0, 0, S(40), S(34), C_WARN, 0);
    u->wmain = fill(u->wrn, 0, 0, S(100), S(34), C_WARN, 0);
    u->wcap[1] = fill(u->wrn, 0, 0, S(40), S(34), C_WARN, 0);
    u->f_wrn = fhd ? &fr0g_cbl_36 : &fr0g_cbl_24;
    u->wrn_txt = text(u->wmain, u->f_wrn, C_BLK, "");
    lv_obj_center(u->wrn_txt);
    lv_obj_add_flag(u->wrn, LV_OBJ_FLAG_HIDDEN);
    u->f_hint = fhd ? &fr0g_b_23 : &fr0g_b_15;
    u->hint = fill(u->root, 0, 0, S(100), S(24), C_BLK, LV_RADIUS_CIRCLE);
    u->hint_txt = text(u->hint, u->f_hint, C_WHT, "");
    lv_obj_center(u->hint_txt);
    lv_obj_add_flag(u->hint, LV_OBJ_FLAG_HIDDEN);

    // like & subscribe: [cap] [ LIKE & SUBSCRIBE ] [heart] [ links ] [cap]
    const lv_font_t *f_card = f_rpm;
    const int ch = S(36), cg = S(6), cap = S(30);
    const char *T = "LIKE & SUBSCRIBE", *YT = "youtube.com/@fr0gzerofpv", *GH = "github.com/fr0gzero";
    const int tw = lv_txt_get_width(T, strlen(T), f_card, 0, LV_TEXT_FLAG_NONE) + S(32);
    const int yw = lv_txt_get_width(YT, strlen(YT), f_small, 0, LV_TEXT_FLAG_NONE);
    const int gw = lv_txt_get_width(GH, strlen(GH), f_small, 0, LV_TEXT_FLAG_NONE);
    const int lw = (yw > gw ? yw : gw) + S(22), hw = S(40);
    const int cw = cap + cg + tw + cg + hw + cg + lw + cg + cap;
    u->card = strip(u->root, ch);
    lv_obj_set_width(u->card, cw);
    u->card_y = SH - ch - S(34);
    lv_obj_set_pos(u->card, (SW - cw) / 2, u->card_y);
    int x = 0;
    fill(u->card, x, 0, cap, ch, C_FRAME, 0);
    x += cap + cg;
    lv_obj_t *tbk = fill(u->card, x, 0, tw, ch, C_AIR, 0);
    u->c_title = text(tbk, f_card, C_BLK, T);
    lv_obj_center(u->c_title);
    x += tw + cg;
    lv_obj_t *hb = fill(u->card, x, 0, hw, ch, C_RED, 0);
    u->heart_x = x + hw / 2;
    u->heart_y = ch / 2;
    x += hw + cg;
    lv_obj_t *lb = fill(u->card, x, 0, lw, ch, DIM(C_FRAME2, 64), 0);
    x += lw + cg;
    fill(u->card, x, 0, cap, ch, C_FRAME2, 0);
    for (int i = 0; i < NFLOAT; i++) {
        lv_obj_t *h = lv_img_create(u->root);   // on the root so they can float up out of the strip
        lv_img_set_src(h, &fr0g_heart);
        lv_img_set_antialias(h, true);
        lv_obj_set_style_img_recolor_opa(h, LV_OPA_COVER, 0);
        lv_obj_set_style_img_recolor(h, i % 3 == 0 ? C_HEART : i % 3 == 1 ? C_ACC : C_VAL2, 0);
        lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
        u->c_float[i] = h;
    }
    u->c_heart = lv_img_create(hb);
    lv_img_set_src(u->c_heart, &fr0g_heart);
    lv_img_set_antialias(u->c_heart, true);
    lv_obj_set_style_img_recolor_opa(u->c_heart, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(u->c_heart, C_WHT, 0);
    lv_obj_center(u->c_heart);
    lv_img_set_zoom(u->c_heart, (uint16_t)(256 * S(20) / 48));
    const int lh = lv_font_get_line_height(f_small);
    u->c_yt = text(lb, f_small, C_FRAME2, YT);
    lv_obj_set_pos(u->c_yt, S(11), ch / 2 - lh + 1);
    u->c_gh = text(lb, f_small, DIM(C_FRAME2, 180), GH);
    lv_obj_set_pos(u->c_gh, S(11), ch / 2 - 1);
    lv_obj_add_flag(u->card, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(u->root, LV_OBJ_FLAG_HIDDEN);
    created[fhd] = true;
    if (!timer)
        timer = lv_timer_create(tick, 16, NULL);
#undef S
}

// ---------------- frog moods ----------------
static int frog_frame(uint32_t ms, bool armed) {
    static uint32_t until = 0, next = 0, react_until = 0;
    static int cur = F_IDLE, react = -1, seq = 0;
    static bool was_armed = false;
    // reactions first
    if (armed && D.g10 >= 35) {
        react = F_HURT;
        react_until = ms + 600;
    } else if (armed && D.thr >= 90 && (react < 0 || ms >= react_until)) {
        react = F_EXCL;
        react_until = ms + 300;
    }
    if (!armed && was_armed && react == F_HURT && ms < react_until + 400) {
        react = F_KO;               // a big hit and then a disarm: knocked out, then dizzy
        react_until = ms + 1500;
    }
    if (react == F_KO && ms >= react_until) {
        react = F_DIZZY;
        react_until = ms + 1200;
    }
    was_armed = armed;
    if (react >= 0 && ms < react_until)
        return react;
    react = -1;
    // random idle gestures
    if (ms >= until && cur != F_IDLE) {
        cur = F_IDLE;
        next = ms + 900 + (uint32_t)(rand() % 4500);
    }
    if (cur == F_IDLE && ms >= next) {
        static const int pick[] = {F_GL, F_GR, F_GU, F_GD, F_BLINK, F_BLINK, F_GRIN};
        cur = pick[(seq++ * 7 + rand()) % 7];
        until = ms + (cur == F_BLINK ? 120 : cur == F_GRIN ? 600 : 350 + rand() % 300);
    }
    if (cur == F_BLINK && until - ms < 50)
        return F_BLINKH;
    return cur;
}

// ---------------- history graph ----------------
static uint8_t h_lq[NHIST];
static uint16_t h_da[NHIST];
static int h_head;   // next write
static void graph_draw(hud_t *u) {
    const int w = u->gw, h = u->gh;
    lv_color_t *b = u->gbuf;
    const lv_color_t grid = DIM(C_FRAME2, 46), afill = DIM(C_VAL2, 60), aline = C_VAL2, lline = C_VAL;
    int amax = 100;   // >= 10 A full scale, else the peak + 15 %
    for (int i = 0; i < NHIST; i++)
        if (h_da[i] * 115 / 100 > amax)
            amax = h_da[i] * 115 / 100;
    for (int i = 0; i < w * h; i++)
        b[i] = C_BLK;
    for (int k = 1; k < 4; k++) {
        const int y = h * k / 4;
        for (int x = 0; x < w; x++)
            b[y * w + x] = grid;
    }
    int pa = -1, pl = -1;
    for (int x = 0; x < w; x++) {
        const int i = (h_head + (x * (NHIST - 1)) / (w - 1)) % NHIST;   // oldest on the left
        int ya = (h - 1) - (h_da[i] * (h - 1)) / amax;
        if (ya < 0) ya = 0;
        for (int y = ya; y < h; y++)
            b[y * w + x] = afill;
        const int a0 = pa < 0 ? ya : (pa < ya ? pa : ya), a1 = pa < 0 ? ya : (pa > ya ? pa : ya);
        for (int y = a0; y <= a1 && y < h; y++)
            b[y * w + x] = aline;
        pa = ya;
        int yl = 1 + ((100 - h_lq[i]) * (h - 3)) / 100;
        const int l0 = pl < 0 ? yl : (pl < yl ? pl : yl), l1 = pl < 0 ? yl : (pl > yl ? pl : yl);
        for (int y = l0; y <= l1 + 1 && y < h; y++)
            b[y * w + x] = lline;
        pl = yl;
    }
    lv_obj_invalidate(u->graph);
}

// ---------------- combat log ----------------
typedef struct {
    char e[16], v[12];
    lv_color_t c;
    uint16_t tenths;
    uint32_t born;
} ev_t;
static ev_t EV[NLOG];
static int n_ev;
static void log_push(uint32_t ms, const char *e, const char *v, lv_color_t c) {
    memmove(&EV[1], &EV[0], sizeof(ev_t) * (NLOG - 1));
    snprintf(EV[0].e, sizeof(EV[0].e), "%s", e);
    snprintf(EV[0].v, sizeof(EV[0].v), "%s", v ? v : "");
    EV[0].c = c;
    EV[0].tenths = (uint16_t)D.tenths;
    EV[0].born = ms;
    if (n_ev < NLOG)
        n_ev++;
}
static void log_events(uint32_t ms) {
    static bool init = false, p_armed, p_air;
    static int p_mode, p_batt;
    static uint32_t imp_t = 0, imp_cool = 0, send_t = 0, send_cool = 0;
    static int imp_peak = 0, dip_min = 101;
    const bool armed = D.flags & 1, air = D.flags & 32;
    const int mode = (D.flags & 8) ? 1 : (D.flags & 16) ? 2 : 0, batt = (D.flags & 4) ? 2 : (D.flags & 2) ? 1 : 0;
    static const char *MODES[] = {"ACRO", "ANGLE", "HORIZON"};
    char v[12];
    if (!init) {
        init = true;
        p_armed = armed;
        p_air = air;
        p_mode = mode;
        p_batt = batt;
        return;
    }
    if (armed && !p_armed)
        log_push(ms, "ARMED", "", C_FRAME);
    if (!armed && p_armed) {
        snprintf(v, sizeof(v), "%02d:%02d", D.tenths / 600, (D.tenths / 10) % 60);
        log_push(ms, "DISARMED", v, C_LAB);
    }
    if (air != p_air)
        log_push(ms, "AIR MODE", air ? "ON" : "OFF", air ? C_OK : C_LAB);
    if (mode != p_mode)
        log_push(ms, "MODE", MODES[mode], C_MODE);
    if (batt > p_batt) {
        snprintf(v, sizeof(v), "%d.%02dV", D.cv / 100, D.cv % 100);
        log_push(ms, batt == 2 ? "BATT CRIT" : "BATT LOW", v, batt == 2 ? C_RED : C_WARN);
    }
    p_armed = armed;
    p_air = air;
    p_mode = mode;
    p_batt = batt;
    if (!armed)
        return;
    // impact: a G spike, logged with its peak once it settles
    if (D.g10 >= 30 && ms >= imp_cool) {
        if (!imp_t)
            imp_t = ms;
        if (D.g10 > imp_peak)
            imp_peak = D.g10;
    }
    if (imp_t && (D.g10 < 20 || ms - imp_t > 600)) {
        snprintf(v, sizeof(v), "%d.%d G", imp_peak / 10, imp_peak % 10);
        log_push(ms, "IMPACT", v, C_RED);
        imp_t = 0;
        imp_peak = 0;
        imp_cool = ms + 1500;
    }
    // send it: pinned throttle for a third of a second
    if (D.thr >= 98) {
        if (!send_t)
            send_t = ms;
        if (ms - send_t > 300 && ms >= send_cool) {
            log_push(ms, "SEND IT", "100%", C_VAL);
            send_cool = ms + 6000;
        }
    } else
        send_t = 0;
    // link dip: logged with its lowest point when it recovers
    if (D.lq < 85 && D.lq < dip_min)
        dip_min = D.lq;
    if (dip_min <= 100 && D.lq >= 95) {
        snprintf(v, sizeof(v), "%d%%", dip_min);
        log_push(ms, dip_min == 0 ? "LINK LOST" : "LINK DIP", v, dip_min < 50 ? C_RED : C_WARN);
        dip_min = 101;
    }
}
static void log_draw(hud_t *u, uint32_t ms) {
    static const uint8_t base[NLOG] = {255, 200, 150, 105, 70};
    const float s = u->s;
    for (int i = 0; i < NLOG; i++) {
        if (i >= n_ev) {
            set_hidden(u->lg_bar[i], true);
            set_txt(u->lg_t[i], "");
            set_txt(u->lg_e[i], "");
            set_txt(u->lg_v[i], "");
            continue;
        }
        const ev_t *e = &EV[i];
        const uint32_t age = ms - e->born;
        float a = base[i] / 255.0f;
        if (age > 30000)
            a *= 1 - clampf((age - 30000) / 2000.0f);   // old entries fade away
        if (a <= 0.02f) {
            set_hidden(u->lg_bar[i], true);
            set_txt(u->lg_t[i], "");
            set_txt(u->lg_e[i], "");
            set_txt(u->lg_v[i], "");
            continue;
        }
        char t[8];
        snprintf(t, sizeof(t), "%02d:%02d", e->tenths / 600, (e->tenths / 10) % 60);
        set_hidden(u->lg_bar[i], false);
        set_txt(u->lg_t[i], t);
        set_txt(u->lg_e[i], e->e);
        set_txt(u->lg_v[i], e->v);
        // newest: flashes white and slides in
        const float fl = i == 0 ? 1 - clampf(age / 450.0f) : 0;
        const uint8_t k = (uint8_t)(255 * a);
        set_bg(u->lg_bar[i], lv_color_mix(C_WHT, DIM(e->c, k), (uint8_t)(255 * fl)));
        set_tc(u->lg_t[i], DIM(C_LAB, k));
        set_tc(u->lg_e[i], lv_color_mix(C_ACC, DIM(C_WHT, k), (uint8_t)(255 * fl)));
        set_tc(u->lg_v[i], lv_color_mix(C_WHT, DIM(e->c, k), (uint8_t)(255 * fl)));
        const int dx = i == 0 ? -(int)((1 - clampf(age / 220.0f)) * 18 * s) : 0;
        lv_obj_set_style_translate_x(u->lg_e[i], dx, 0);
        lv_obj_set_style_translate_x(u->lg_v[i], dx, 0);
        lv_obj_set_style_translate_x(u->lg_t[i], dx, 0);
    }
}

// ---------------- the card (once per pack, 5 s after arming) ----------------
static void card_tick(hud_t *u, int64_t t_ms) {
    const float t = t_ms / 1000.0f, T_OUT = 8.0f;
    if (t_ms < 0 || t > T_OUT + 0.6f) {
        set_hidden(u->card, true);
        for (int i = 0; i < NFLOAT; i++)
            set_hidden(u->c_float[i], true);
        return;
    }
    const float s = u->s;
    float pin = clampf(t / 0.5f);
    pin = 1 - (1 - pin) * (1 - pin) * (1 - pin);
    float pout = clampf((t - T_OUT) / 0.6f);
    pout *= pout;
    const float a = pin * (1 - pout);
    set_hidden(u->card, false);
    lv_obj_set_y(u->card, u->card_y + (int)((1 - pin) * 18 * s + pout * 18 * s));
    lv_obj_set_style_opa(u->card, (lv_opa_t)(255 * a), 0);
    float beat = 1.0f;
    if (t > 0.8f) {
        const float sb = fmaxf(0, sinf((t - 0.8f) * 2 * 3.14159f * 1.2f));
        beat = 1 + 0.14f * powf(sb, 6);
    }
    lv_img_set_zoom(u->c_heart, (uint16_t)(256 * 20 * s / 48 * beat));
    const int hx = lv_obj_get_x(u->card) + u->heart_x, hy = lv_obj_get_y(u->card) + u->heart_y;
    for (int k = 0; k < NFLOAT; k++) {
        const float ph = t - 1.0f - k * 0.7f;
        if (ph < 0 || ph > 1.8f || t > T_OUT) {
            set_hidden(u->c_float[k], true);
            continue;
        }
        const float p = ph / 1.8f;
        set_hidden(u->c_float[k], false);
        lv_img_set_zoom(u->c_float[k], (uint16_t)(256 * 12 * s / 48 * (1 - p * 0.4f)));
        lv_obj_set_pos(u->c_float[k], hx + (int)((10 * sinf(ph * 5 + k) + k * 3) * s) - 24,   // 48 px image, zoomed about its centre
                       hy - (int)((10 + p * 90) * s) - 24);
        lv_obj_set_style_opa(u->c_float[k], (lv_opa_t)(220 * (1 - p) * a), 0);
    }
}

// ---------------- warnings: Betaflight's terse text -> plain English + a hint ----------------
static const struct { const char *bf, *title, *hint; } WMAP[] = {
    {"CRASH FLIP", "TURTLE MODE", "FLIP IT BACK WITH THE STICKS"},
    {"CRASHFLIP SW", "TURTLE SWITCH ON", "TURN IT OFF TO ARM"},
    {"FLIP_SWITCH", "TURTLE SWITCH ON", "TURN IT OFF TO ARM"},
    {"LOW BATTERY", "BATTERY LOW", "BRING IT HOME"},
    {"LAND NOW", "LAND NOW", "BATTERY CRITICAL"},
    {"BATT < FULL", "PACK NOT FULL", "PLUGGED IN A USED PACK?"},
    {"RXLOSS", "SIGNAL LOST", "NO LINK TO THE RADIO"},
    {"FAIL SAFE", "FAILSAFE", "LINK LOST"},
    {"FAILSAFE", "FAILSAFE", "LINK LOST"},
    {"BOXFAILSAFE", "FAILSAFE SWITCH ON", "TURN IT OFF TO ARM"},
    {"LINK QUALITY", "WEAK LINK", "TURN BACK"},
    {"RSSI LOW", "WEAK SIGNAL", "TURN BACK"},
    {"RSNR LOW", "WEAK SIGNAL", "TURN BACK"},
    {"THROTTLE", "THROTTLE UP", "LOWER THE THROTTLE TO ARM"},
    {"ANGLE", "TOO TILTED", "LEVEL THE QUAD TO ARM"},
    {"NOGYRO", "NO GYRO", "POWER CYCLE THE QUAD"},
    {"RUNAWAY", "RUNAWAY TAKEOFF", "DISARMED FOR SAFETY"},
    {"CRASH", "CRASH DETECTED", "DISARM TO RESET"},
    {"BOOTGRACE", "STARTING UP", "ONE MOMENT"},
    {"CALIB", "CALIBRATING", "KEEP IT STILL"},
    {"CLI", "CLI OPEN", "CLOSE THE CONFIGURATOR"},
    {"MSP", "USB CONNECTED", "CLOSE THE CONFIGURATOR"},
    {"CMS", "MENU OPEN", "EXIT THE OSD MENU"},
    {"ARM_SWITCH", "ARM SWITCH ON", "SWITCH IT OFF FIRST"},
    {"NOPREARM", "PREARM OFF", "FLICK PREARM FIRST"},
    {"LOAD", "CPU OVERLOAD", "LOWER THE PID LOOP"},
    {"CPU OVERLOAD", "CPU OVERLOAD", "LOWER THE PID LOOP"},
    {"REBOOT_REQD", "REBOOT NEEDED", "POWER CYCLE THE QUAD"},
    {"DSHOT_TELEM", "NO MOTOR TELEMETRY", "CHECK BIDIR DSHOT"},
    {"MOTOR_PROTO", "NO MOTOR PROTOCOL", "SET ONE IN CONFIGURATOR"},
    {"PARALYZE", "PARALYZED", "POWER CYCLE TO CLEAR"},
    {"NO_ACC_CAL", "ACCEL NOT CALIBRATED", "CALIBRATE IN CONFIGURATOR"},
    {"BEACON ON", "BEACON ON", "BUZZER ACTIVE"},
    {"NOT_DISARMED", "NOT DISARMED", "DISARM FIRST"},
};
static void wrn_map(const char *raw, char *title, size_t tn, const char **hint) {
    char k[16];
    int n = 0;
    for (const char *p = raw; *p && n < 15; p++)
        if (*p != '>' && *p != '<')
            k[n++] = *p;
    k[n] = '\0';
    while (n > 0 && k[n - 1] == ' ')
        k[--n] = '\0';
    const char *kk = k;
    while (*kk == ' ')
        kk++;
    for (size_t i = 0; i < sizeof(WMAP) / sizeof(WMAP[0]); i++)
        if (strcmp(kk, WMAP[i].bf) == 0) {
            snprintf(title, tn, "%s", WMAP[i].title);
            *hint = WMAP[i].hint;
            return;
        }
    snprintf(title, tn, "%s", kk);
    *hint = "";
}
static void wrn_tick(hud_t *u, uint32_t ms) {
    static char shown[16] = "";
    static uint32_t changed = 0, gone = 0;
    const bool has = D.wtxt[0] != '\0';
    if (has && strcmp(shown, D.wtxt) != 0) {
        strcpy(shown, D.wtxt);
        changed = ms;
    }
    if (has)
        gone = 0;
    else if (!gone)
        gone = ms;
    const float s = u->s;
    const float a = has ? clampf((ms - changed) / 150.0f) : 1 - clampf((ms - gone) / 250.0f);
    const bool crit = D.wsev >= 3, warn = D.wsev == 2;
    const lv_color_t pulse = lv_color_mix(C_RED, lv_color_hex(0x340000), (uint8_t)(255 * (0.5f + 0.5f * sinf(ms * 0.012f))));
    for (int k = 0; k < 2; k++) {
        set_hidden(u->edge[k], !(crit && has));
        if (crit && has)
            set_bg(u->edge[k], pulse);
    }
    if (a <= 0.01f || !shown[0]) {
        set_hidden(u->wrn, true);
        set_hidden(u->hint, true);
        return;
    }
    char title[24];
    const char *hint;
    wrn_map(shown, title, sizeof(title), &hint);
    set_hidden(u->wrn, false);
    if (strcmp(lv_label_get_text(u->wrn_txt), title) != 0) {
        lv_label_set_text(u->wrn_txt, title);
        const int cap = (int)(40 * s), gap = (int)(6 * s);
        const int mw = lv_txt_get_width(title, strlen(title), u->f_wrn, 0, LV_TEXT_FLAG_NONE) + (int)(48 * s);
        lv_obj_set_x(u->wmain, cap + gap);
        lv_obj_set_width(u->wmain, mw);
        lv_obj_set_x(u->wcap[1], cap + gap + mw + gap);
        lv_obj_set_width(u->wrn, cap + gap + mw + gap + cap);
        lv_label_set_text(u->hint_txt, hint);
        lv_obj_set_width(u->hint, lv_txt_get_width(hint, strlen(hint), u->f_hint, 0, LV_TEXT_FLAG_NONE) + (int)(28 * s));
    }
    const int dy = -(int)((1 - a) * 14 * s);
    lv_obj_align(u->wrn, LV_ALIGN_TOP_MID, 0, u->wrn_y + dy);
    const lv_color_t c = crit ? pulse : warn ? C_WARN : C_FRAME;
    set_bg(u->wcap[0], c);
    set_bg(u->wmain, c);
    set_bg(u->wcap[1], c);
    set_tc(u->wrn_txt, crit ? C_WHT : C_BLK);
    lv_obj_set_style_opa(u->wrn, (lv_opa_t)(255 * a), 0);
    set_hidden(u->hint, hint[0] == '\0');
    if (hint[0]) {
        lv_obj_align(u->hint, LV_ALIGN_TOP_MID, 0, u->wrn_y + (int)(40 * s) + dy);
        lv_obj_set_style_opa(u->hint, (lv_opa_t)(255 * a), 0);
    }
}

// ---------------- frame tick ----------------
static int64_t first_arm_ms = -1;
static void tick(lv_timer_t *tm) {
    (void)tm;
    const uint32_t ms = now_ms();
    const bool live = (ms - seen_ms) < 600;
    static uint32_t last_live = 0;
    static bool hist_init = false;
    if (live) {
        if (last_live && ms - last_live > 3000) {
            // the quad was unplugged for a while: new pack, new flight
            first_arm_ms = -1;
            n_ev = 0;
            hist_init = false;
        }
        last_live = ms;
    }
    for (int f = 0; f < 2; f++)
        if (created[f])
            set_hidden(U[f].root, !live);
    if (!live)
        return;

    const bool armed = D.flags & 1, warn = D.flags & 2, crit = D.flags & 4, air = D.flags & 32;
    if (armed && first_arm_ms < 0)
        first_arm_ms = ms;
    const int64_t card_t = first_arm_ms < 0 ? -1 : (int64_t)ms - first_arm_ms - 5000;
    log_events(ms);

    // history (4 Hz)
    static uint32_t hist_ms = 0;
    bool graph_dirty = false;
    if (!hist_init) {
        for (int i = 0; i < NHIST; i++) {
            h_lq[i] = (uint8_t)D.lq;
            h_da[i] = (uint16_t)D.da;
        }
        hist_init = true;
        graph_dirty = true;
        hist_ms = ms;
    } else if (ms - hist_ms >= 250) {
        hist_ms += 250;
        if (ms - hist_ms > 1000)
            hist_ms = ms;
        h_lq[h_head] = (uint8_t)D.lq;
        h_da[h_head] = (uint16_t)D.da;
        h_head = (h_head + 1) % NHIST;
        graph_dirty = true;
    }
    // stick trails (30 Hz)
    static float trail[2][NTRAIL][2];
    static uint32_t trail_ms = 0;
    static float st[4] = {100, 0, 100, 100};
    for (int i = 0; i < 4; i++)
        st[i] += (D.stk[i] - st[i]) * 0.5f;
    if (ms - trail_ms >= 33) {
        trail_ms = ms;
        for (int k = 0; k < 2; k++) {
            memmove(&trail[k][0], &trail[k][1], sizeof(trail[k][0]) * (NTRAIL - 1));
            trail[k][NTRAIL - 1][0] = st[k * 2] / 199.0f;
            trail[k][NTRAIL - 1][1] = st[k * 2 + 1] / 199.0f;
        }
    }

    static float thr_s = 0, rpm_s = 0, rpm_full = 30000;
    thr_s += (D.thr - thr_s) * 0.3f;
    const int rpm = D.rpm200 * 200;
    rpm_s += (rpm - rpm_s) * 0.3f;
    if (rpm > rpm_full)
        rpm_full = rpm;
    const float pct = clampf((D.cv - 340) / 75.0f);   // 3.40 V empty .. 4.15 V full (whoop cells sag early)
    const int lit = (int)lrintf(pct * NSEG);

    char volt[12], pc[8], mah[12], amp[12], tim[12], tp[8], rv[12], name[8], gv[4][8];
    snprintf(volt, sizeof(volt), "%d.%02d", D.cv / 100, D.cv % 100);
    snprintf(pc, sizeof(pc), "%d%%", (int)lrintf(pct * 100));
    snprintf(mah, sizeof(mah), "%d", D.mah);
    snprintf(amp, sizeof(amp), "%d.%d A", D.da / 10, D.da % 10);
    snprintf(tim, sizeof(tim), "%02d:%02d", D.tenths / 600, (D.tenths / 10) % 60);
    snprintf(tp, sizeof(tp), "%d%%", (int)lrintf(thr_s));
    snprintf(rv, sizeof(rv), "%d.%d", rpm / 1000, (rpm % 1000) / 100);
    snprintf(gv[0], 8, "%+03d", D.stk[0] - 100);
    snprintf(gv[1], 8, "%d%%", D.thr);
    snprintf(gv[2], 8, "%+03d", D.stk[2] - 100);
    snprintf(gv[3], 8, "%+03d", D.stk[3] - 100);
    // craft name "MK4-2" / "mk.4-2" -> "4-2", and the model for the fleet
    const char *nm = D.name;
    if ((nm[0] == 'M') && (nm[1] == 'K'))
        nm += nm[2] == '.' ? 3 : 2;
    snprintf(name, sizeof(name), "%s", nm);
    const char *model = !strcmp(name, "4-1") ? "AIR65" : !strcmp(name, "4-2") ? "MOBULA 6" : !strcmp(name, "5-1") ? "MOBULA 8"
                      : !strcmp(name, "6-1") ? "ANGEL 35" : !strcmp(name, "6-2") ? "BEE35" : !strcmp(name, "7") ? "NAZGUL" : "";
    const char *mode = (D.flags & 8) ? "ANGLE" : (D.flags & 16) ? "HORIZON" : "ACRO";
    const int fr = frog_frame(ms, armed);
    static bool was_armed = false;
    static uint32_t armed_at = 0;
    if (armed && !was_armed)
        armed_at = ms;
    was_armed = armed;
    const uint32_t since = ms - armed_at;
    const uint8_t arm_flash = (armed && since < 450) ? (uint8_t)(255 - since * 255 / 450) : 0;

    for (int f = 0; f < 2; f++) {
        if (!created[f])
            continue;
        hud_t *u = &U[f];
        if (lv_img_get_src(u->frog) != FROG[fr]) {
            lv_img_set_src(u->frog, FROG[fr]);
            lv_img_set_pivot(u->frog, 0, 0);   // set_src re-centres the pivot
        }
        set_txt(u->name, name);
        set_txt(u->model, model);
        set_txt(u->volt, volt);
        set_tc(u->volt, crit ? C_RED : warn ? C_WARN : C_WHT);
        set_txt(u->pct, pc);
        for (int k = 0; k < NSEG; k++)
            set_bg(u->seg[k], seg_col(k, k < lit));
        set_txt(u->mah, mah);
        set_txt(u->amp, amp);
        if (graph_dirty)
            graph_draw(u);
        log_draw(u, ms);
        set_txt(u->tim, tim);
        set_bg(u->blk[0], armed ? lv_color_mix(C_WHT, C_RED, arm_flash) : DIM(C_RED, 90));
        set_tc(u->blk_txt[0], armed ? C_WHT : lv_color_hex(0xB3B3B3));
        set_txt(u->blk_txt[0], armed ? "ARMED" : "DISARMED");
        set_bg(u->blk[1], air ? C_AIR : DIM(C_FRAME2, 80));
        set_tc(u->blk_txt[1], air ? C_BLK : lv_color_hex(0x90ACAC));
        set_txt(u->blk_txt[1], air ? "AIR MODE" : "AIR OFF");
        set_bg(u->blk[2], C_MODE);
        set_txt(u->blk_txt[2], mode);
        lv_obj_set_height(u->thr_fill, (int)(u->bar_h * clampf(thr_s / 100.0f)));
        lv_obj_set_height(u->rpm_fill, (int)(u->bar_h * clampf(rpm_s / rpm_full)));
        set_txt(u->thr_pct, tp);
        set_txt(u->rpm_val, rv);
        for (int k = 0; k < 2; k++) {
            const int in = u->gim_in, pad = u->gim_pad;
            for (int i = 0; i < NTRAIL; i++) {
                const int d = lv_obj_get_width(u->trail[k][i]);
                lv_obj_set_pos(u->trail[k][i], pad + (int)(trail[k][i][0] * in) - d / 2, pad + (int)((1 - trail[k][i][1]) * in) - d / 2);
            }
            const int d = lv_obj_get_width(u->gdot[k]);
            lv_obj_set_pos(u->gdot[k], pad + (int)(st[k * 2] / 199.0f * in) - d / 2, pad + (int)((1 - st[k * 2 + 1] / 199.0f) * in) - d / 2);
            set_txt(u->gval[k][0], gv[k * 2]);
            set_txt(u->gval[k][1], gv[k * 2 + 1]);
        }
        card_tick(u, card_t);
        wrn_tick(u, ms);
    }
}

// ---------------- hidden-cell decoder ----------------
static int dec(uint16_t ch) {
    if (ch < 0x100)
        return 0;
    const int v = (int)ch - 0x100 - 0x30;
    return v < 0 ? 0 : v > 199 ? 199 : v;
}

uint16_t fr0g_hud_filter(int row, int col, uint16_t ch, const uint16_t *rb) {
    if (row != ROW)
        return ch;
    if (col >= STK_COL && col < STK_COL + STK_LEN && rb[STK_COL] == MARK_STK) {
        if (col == STK_COL)
            for (int i = 0; i < 4; i++)
                D.stk[i] = dec(rb[STK_COL + 1 + i]);
        return 0x20;
    }
    if (col >= WRN_COL && col < WRN_COL + WRN_LEN && rb[WRN_COL] == MARK_WRN) {
        if (col == WRN_COL) {
            D.wsev = dec(rb[WRN_COL + 1]);
            char t[16];
            int n = 0;
            for (int i = 0; i < 15; i++) {
                const int c = rb[WRN_COL + 2 + i] & 0xFF;
                t[n++] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
            }
            while (n > 0 && t[n - 1] == ' ')
                n--;
            int b0 = 0;
            while (b0 < n && t[b0] == ' ')
                b0++;
            t[n] = '\0';
            memcpy(D.wtxt, t + b0, (size_t)(n - b0 + 1));
        }
        return 0x20;
    }
    if (col >= DAT_COL && col < DAT_COL + DAT_LEN && rb[DAT_COL] == MARK_DAT) {
        if (col == DAT_COL) {
            const uint16_t *p = rb + DAT_COL;
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
                if (c > 0x20 && c < 0x7F)
                    D.name[n++] = (char)c;
                else if (c == 0x20 && n)
                    D.name[n++] = ' ';
            }
            while (n > 0 && D.name[n - 1] == ' ')
                n--;
            D.name[n] = '\0';
            seen_ms = now_ms();
        }
        return 0x20;
    }
    return ch;
}
