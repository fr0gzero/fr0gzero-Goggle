/*
 * fr0gOS "gouache almanac" HUD theme (Mark 4-2).
 * Cream paper side bars with old-book serif type; the static art (paper, ink outlines, dials, captions) is one image per bar
 * baked by misc/fr0g/gouachegen.py; everything that moves is painted swatches (recoloured white brushstrokes), ink lines and labels.
 * Data, the combat log and the warning text come from fr0g_hud.c.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fr0g_hud_int.h"

#define G_PAPER lv_color_hex(0xECE2CC)
#define G_INK lv_color_hex(0x342822)
#define G_RUST lv_color_hex(0xB24A2C)
#define G_OCHRE lv_color_hex(0xCE9634)
#define G_SAGE lv_color_hex(0x60805C)
#define G_PRUSS lv_color_hex(0x2C4670)
#define G_ROSE lv_color_hex(0xC46E6E)
#define G_FADED lv_color_hex(0xA89C88)
#define G_SKY lv_color_hex(0x78A0BE)
#define G_EARTH lv_color_hex(0x967046)
#define G_HEART lv_color_hex(0xFF5757)

LV_IMG_DECLARE(fr0g_g_left); LV_IMG_DECLARE(fr0g_g_right); LV_IMG_DECLARE(fr0g_g_block); LV_IMG_DECLARE(fr0g_g_col);
LV_IMG_DECLARE(fr0g_g_vial); LV_IMG_DECLARE(fr0g_g_banner); LV_IMG_DECLARE(fr0g_g_hint); LV_IMG_DECLARE(fr0g_g_card);
LV_IMG_DECLARE(fr0g_g_title); LV_IMG_DECLARE(fr0g_g_petal); LV_IMG_DECLARE(fr0g_heart);
LV_FONT_DECLARE(fr0g_gserb_34); LV_FONT_DECLARE(fr0g_gserb_51);
LV_FONT_DECLARE(fr0g_gserb_30); LV_FONT_DECLARE(fr0g_gserb_45);
LV_FONT_DECLARE(fr0g_gserb_22); LV_FONT_DECLARE(fr0g_gserb_33);
LV_FONT_DECLARE(fr0g_gserb_20);
LV_FONT_DECLARE(fr0g_gserb_16); LV_FONT_DECLARE(fr0g_gserb_24);
LV_FONT_DECLARE(fr0g_gcaps_15); LV_FONT_DECLARE(fr0g_gcaps_22); LV_FONT_DECLARE(fr0g_gcaps_33);
LV_FONT_DECLARE(fr0g_gser_13); LV_FONT_DECLARE(fr0g_gser_20);
LV_FONT_DECLARE(fr0g_gseri_12); LV_FONT_DECLARE(fr0g_gseri_18);
LV_FONT_DECLARE(fr0g_gseri_14); LV_FONT_DECLARE(fr0g_gseri_21);

#define ORB_R 44
typedef struct {
    float s;
    int SW, SH, BAR;
    lv_obj_t *root;
    lv_obj_t *name, *model;
    lv_obj_t *vial_clip, *vial;
    int vial_y, vial_h;
    lv_obj_t *volt, *mahamp, *petal[8], *lq, *log[2];
    lv_obj_t *tim, *blk[3], *blk_txt[3];
    lv_obj_t *thr_clip, *thr_col, *thr_pct;
    int thr_y, thr_h;
    lv_obj_t *mot[4], *prop[4];
    lv_point_t prop_pts[4][2];
    int mot_cx[4], mot_cy[4];
    lv_obj_t *orb;
    lv_color_t *orb_buf;
    int orb_cs;
    lv_obj_t *needle[2], *ndot[2];
    lv_point_t needle_pts[2][2];
    int rose_cx[2], rose_cy;
    lv_obj_t *wrn, *wrn_bg, *wrn_txt, *hint, *hint_txt, *edge[2];
    lv_obj_t *card, *c_heart;
    int card_y;
} gh_t;

static gh_t G[2];
static bool g_created[2];
static lv_color_t orb_hd[88 * 88], orb_fhd[132 * 132];

static float clampf(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static lv_obj_t *box(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *fill(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r) {
    lv_obj_t *o = box(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}
static lv_obj_t *img(lv_obj_t *p, const lv_img_dsc_t *src, int x, int y, float s, bool recolor, lv_color_t c) {
    lv_obj_t *i = lv_img_create(p);
    lv_img_set_src(i, src);
    lv_img_set_antialias(i, true);
    lv_img_set_pivot(i, 0, 0);
    if (s != 1.0f)
        lv_img_set_zoom(i, (uint16_t)lrintf(256 * s));
    lv_obj_set_pos(i, x, y);
    if (recolor) {
        lv_obj_set_style_img_recolor(i, c, 0);
        lv_obj_set_style_img_recolor_opa(i, 248, 0);
    }
    return i;
}
static lv_obj_t *text(lv_obj_t *p, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}
// centre a label on (cx, cy) inside a parent of width pw (keeps centred as the text changes)
static void ctr(lv_obj_t *l, int pw, int cx, int cy) {
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, cx - pw / 2, cy - f->line_height / 2);
}
static void set_txt(lv_obj_t *l, const char *s) {
    if (strcmp(lv_label_get_text(l), s) != 0)
        lv_label_set_text(l, s);
}
static void set_tc(lv_obj_t *o, lv_color_t c) {
    if (lv_obj_get_style_text_color(o, 0).full != c.full)
        lv_obj_set_style_text_color(o, c, 0);
}
static void set_rc(lv_obj_t *o, lv_color_t c) {
    if (lv_obj_get_style_img_recolor(o, 0).full != c.full)
        lv_obj_set_style_img_recolor(o, c, 0);
}
static void set_bg(lv_obj_t *o, lv_color_t c) {
    if (lv_obj_get_style_bg_color(o, 0).full != c.full)
        lv_obj_set_style_bg_color(o, c, 0);
}
static void set_hidden(lv_obj_t *o, bool h) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != h) {
        if (h)
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// painted horizon orb: sky over earth, a cream horizon, ink ladder; soft edge into the paper
static void orb_draw(gh_t *u, float roll, float pitch) {
    const int cs = u->orb_cs, c = cs / 2;
    const float s = u->s, r = ORB_R * s, ppd = 1.1f * s;
    const float sn = sinf(roll * 0.01745f), cn = cosf(roll * 0.01745f), h = pitch * ppd;
    lv_color_t *b = u->orb_buf;
    for (int y = 0; y < cs; y++)
        for (int x = 0; x < cs; x++) {
            const float dx = x - c + 0.5f, dy = y - c + 0.5f, d = sqrtf(dx * dx + dy * dy);
            if (d > r + 1) {
                b[y * cs + x] = G_PAPER;
                continue;
            }
            const float v = dx * sn + dy * cn, uu = dx * cn - dy * sn;
            lv_color_t col = v > h ? G_EARTH : G_SKY;
            // a little painterly mottling
            const int n = ((x * 7 + y * 13) ^ (x * y)) & 7;
            col = lv_color_mix(col, G_PAPER, (uint8_t)(232 + n * 3));
            if (fabsf(v - h) < 1.0f * s)
                col = G_PAPER;
            else
                for (int p = -20; p <= 20; p += 10)
                    if (p && fabsf(v - (h - p * ppd)) < 0.6f * s && fabsf(uu) < (p % 20 ? 0.2f : 0.32f) * r)
                        col = lv_color_mix(G_INK, col, 150);
            if (d > r)
                col = lv_color_mix(G_PAPER, col, (uint8_t)(255 * clampf(d - r)));
            b[y * cs + x] = col;
        }
    lv_obj_invalidate(u->orb);
}

void fr0g_gouache_create(uint8_t fhd, lv_obj_t *parent) {
    gh_t *u = &G[fhd];
    const float s = fhd ? 1.5f : 1.0f;
#define S(v) ((int)lrintf((v) * s))
    u->s = s;
    u->SW = fhd ? 1920 : 1280;
    u->SH = fhd ? 1080 : 720;
    u->BAR = S(160);
    const int SW = u->SW, SH = u->SH, xR = SW - u->BAR;
    const lv_font_t *f_tim = fhd ? &fr0g_gserb_51 : &fr0g_gserb_34, *f_volt = fhd ? &fr0g_gserb_45 : &fr0g_gserb_30,
                    *f_name = fhd ? &fr0g_gserb_33 : &fr0g_gserb_22, *f_card = fhd ? &fr0g_gserb_30 : &fr0g_gserb_20,
                    *f_thr = fhd ? &fr0g_gserb_24 : &fr0g_gserb_16, *f_caps = fhd ? &fr0g_gcaps_22 : &fr0g_gcaps_15,
                    *f_wrn = fhd ? &fr0g_gcaps_33 : &fr0g_gcaps_22, *f_ser = fhd ? &fr0g_gser_20 : &fr0g_gser_13,
                    *f_it = fhd ? &fr0g_gseri_18 : &fr0g_gseri_12, *f_hint = fhd ? &fr0g_gseri_21 : &fr0g_gseri_14;

    u->root = box(parent);
    lv_obj_set_size(u->root, SW, SH);
    lv_obj_set_pos(u->root, -lv_obj_get_style_pad_left(parent, LV_PART_MAIN), -lv_obj_get_style_pad_top(parent, LV_PART_MAIN));
    lv_obj_t *R = u->root;

    // ---- the paper bars (static art)
    img(R, &fr0g_g_left, 0, 0, s, false, G_INK);
    img(R, &fr0g_g_right, xR, 0, s, false, G_INK);

    // ---- left: cartouche with the craft name
    img(R, &fr0g_g_title, S(10), S(10), s, true, G_PRUSS);
    u->name = text(R, f_name, G_PAPER);
    ctr(u->name, SW, S(80), S(28));
    u->model = text(R, f_it, G_PAPER);
    ctr(u->model, SW, S(80), S(48));
    // battery vial: a painted swatch behind a clip that drops as the pack drains
    u->vial_y = S(102);
    u->vial_h = S(192);
    u->vial_clip = box(R);
    lv_obj_set_pos(u->vial_clip, S(57), u->vial_y);
    lv_obj_set_size(u->vial_clip, S(46), u->vial_h);
    u->vial = img(u->vial_clip, &fr0g_g_vial, 0, 0, s, true, G_SAGE);
    u->volt = text(R, f_volt, G_INK);
    ctr(u->volt, SW, S(80), S(322));
    u->mahamp = text(R, f_ser, G_RUST);
    ctr(u->mahamp, SW, S(80), S(372));
    // signal: pressed-flower petals, one per 12.5 % link quality
    for (int k = 0; k < 8; k++) {
        const float a = (k * 45 - 90) * 0.01745f;
        u->petal[k] = img(R, &fr0g_g_petal, S(80 + 18 * cosf(a) - 10), S(438 + 18 * sinf(a) - 10), s, true, G_ROSE);
    }
    img(R, &fr0g_g_petal, S(70), S(428), s, true, G_OCHRE);
    u->lq = text(R, f_it, G_INK);
    ctr(u->lq, SW, S(80), S(474));
    for (int i = 0; i < 2; i++) {
        u->log[i] = text(R, f_it, G_INK);
        ctr(u->log[i], SW, S(80), S(495 + i * 15));
    }
    // compass-rose needles (left = yaw/throttle, right = roll/pitch)
    u->rose_cx[0] = S(80);
    u->rose_cx[1] = xR + S(80);
    u->rose_cy = S(596);
    for (int k = 0; k < 2; k++) {
        u->needle[k] = lv_line_create(R);
        lv_obj_set_style_line_color(u->needle[k], G_RUST, 0);
        lv_obj_set_style_line_width(u->needle[k], S(3), 0);
        lv_obj_set_style_line_rounded(u->needle[k], true, 0);
        u->needle_pts[k][0].x = u->needle_pts[k][1].x = (lv_coord_t)u->rose_cx[k];
        u->needle_pts[k][0].y = u->needle_pts[k][1].y = (lv_coord_t)u->rose_cy;
        lv_line_set_points(u->needle[k], u->needle_pts[k], 2);
        u->ndot[k] = fill(R, 0, 0, S(10), S(10), G_PRUSS, LV_RADIUS_CIRCLE);
    }

    // ---- right
    u->tim = text(R, f_tim, G_INK);
    ctr(u->tim, SW, xR + S(80), S(36));
    for (int k = 0; k < 3; k++) {
        u->blk[k] = img(R, &fr0g_g_block, xR + S(16), S(84 + k * 30), s, true, G_FADED);
        u->blk_txt[k] = text(R, f_caps, G_PAPER);
        ctr(u->blk_txt[k], SW, xR + S(80), S(96 + k * 30));
    }
    u->thr_y = S(207);
    u->thr_h = S(170);
    u->thr_clip = box(R);
    lv_obj_set_pos(u->thr_clip, xR + S(23), u->thr_y);
    lv_obj_set_size(u->thr_clip, S(30), u->thr_h);
    u->thr_col = img(u->thr_clip, &fr0g_g_col, 0, 0, s, true, G_OCHRE);
    u->thr_pct = text(R, f_thr, G_INK);
    ctr(u->thr_pct, SW, xR + S(38), S(396));
    for (int k = 0; k < 4; k++) {
        const int cx = xR + S(86 + (k % 2) * 42), cy = S(232 + (k / 2) * 84), r = S(13);
        u->mot_cx[k] = cx;
        u->mot_cy[k] = cy;
        lv_obj_t *a = lv_arc_create(R);
        lv_obj_remove_style(a, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(a, 2 * r, 2 * r);
        lv_obj_set_pos(a, cx - r, cy - r);
        lv_obj_set_style_pad_all(a, 0, 0);
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(a, r, LV_PART_INDICATOR);   // as wide as the radius: a filled pie
        lv_obj_set_style_arc_rounded(a, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(a, G_PRUSS, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(a, 200, LV_PART_INDICATOR);
        lv_arc_set_rotation(a, 270);
        lv_arc_set_bg_angles(a, 0, 360);
        lv_arc_set_range(a, 0, 1000);
        lv_arc_set_value(a, 0);
        u->mot[k] = a;
        lv_obj_t *p = lv_line_create(R);
        lv_obj_set_style_line_color(p, G_INK, 0);
        lv_obj_set_style_line_width(p, S(2), 0);
        lv_obj_set_style_line_opa(p, 170, 0);
        u->prop_pts[k][0].x = u->prop_pts[k][1].x = (lv_coord_t)cx;
        u->prop_pts[k][0].y = u->prop_pts[k][1].y = (lv_coord_t)cy;
        lv_line_set_points(p, u->prop_pts[k], 2);
        u->prop[k] = p;
    }
    u->orb_cs = 2 * S(ORB_R);
    u->orb_buf = fhd ? orb_fhd : orb_hd;
    u->orb = lv_canvas_create(R);
    lv_canvas_set_buffer(u->orb, u->orb_buf, u->orb_cs, u->orb_cs, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(u->orb, xR + S(80) - u->orb_cs / 2, S(454) - u->orb_cs / 2);
    orb_draw(u, 0, 0);
    fill(R, xR + S(54), S(453), S(18), S(3), G_INK, 0);   // the craft, fixed
    fill(R, xR + S(88), S(453), S(18), S(3), G_INK, 0);
    fill(R, xR + S(78), S(452), S(5), S(5), G_INK, LV_RADIUS_CIRCLE);

    // ---- over the picture: warning banner + hint, critical edges, like & subscribe
    for (int k = 0; k < 2; k++) {
        u->edge[k] = fill(R, k == 0 ? u->BAR : xR - S(5), 0, S(5), SH, G_RUST, 0);
        lv_obj_add_flag(u->edge[k], LV_OBJ_FLAG_HIDDEN);
    }
    u->wrn = box(R);
    lv_obj_set_size(u->wrn, S(380), S(44));
    lv_obj_set_pos(u->wrn, (SW - S(380)) / 2, S(16));
    u->wrn_bg = img(u->wrn, &fr0g_g_banner, 0, 0, s, true, G_RUST);
    u->wrn_txt = text(u->wrn, f_wrn, G_PAPER);
    lv_obj_center(u->wrn_txt);
    lv_obj_add_flag(u->wrn, LV_OBJ_FLAG_HIDDEN);
    u->hint = box(R);
    lv_obj_set_size(u->hint, S(320), S(26));
    lv_obj_set_pos(u->hint, (SW - S(320)) / 2, S(66));
    img(u->hint, &fr0g_g_hint, 0, 0, s, true, G_PAPER);
    u->hint_txt = text(u->hint, f_hint, G_INK);
    lv_obj_center(u->hint_txt);
    lv_obj_add_flag(u->hint, LV_OBJ_FLAG_HIDDEN);

    u->card = box(R);
    lv_obj_set_size(u->card, S(560), S(48));
    u->card_y = SH - S(84);
    lv_obj_set_pos(u->card, (SW - S(560)) / 2, u->card_y);
    img(u->card, &fr0g_g_card, 0, 0, s, true, G_PAPER);
    lv_obj_t *t = text(u->card, f_card, G_INK);
    lv_label_set_text(t, "Like & Subscribe");
    lv_obj_align(t, LV_ALIGN_LEFT_MID, S(22), 0);
    u->c_heart = lv_img_create(u->card);
    lv_img_set_src(u->c_heart, &fr0g_heart);
    lv_img_set_antialias(u->c_heart, true);
    lv_obj_set_style_img_recolor(u->c_heart, G_RUST, 0);
    lv_obj_set_style_img_recolor_opa(u->c_heart, LV_OPA_COVER, 0);
    lv_obj_set_pos(u->c_heart, S(232) - 24, S(24) - 24);   // 48 px image zoomed about its centre
    lv_img_set_zoom(u->c_heart, (uint16_t)(256 * S(24) / 48));
    lv_obj_t *yt = text(u->card, f_hint, G_PRUSS);
    lv_label_set_text(yt, "youtube.com/@fr0gzerofpv");
    lv_obj_align(yt, LV_ALIGN_LEFT_MID, S(262), -S(9));
    lv_obj_t *gh = text(u->card, f_ser, G_INK);
    lv_label_set_text(gh, "github.com/fr0gzero");
    lv_obj_align(gh, LV_ALIGN_LEFT_MID, S(262), S(10));
    lv_obj_add_flag(u->card, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(u->root, LV_OBJ_FLAG_HIDDEN);
    g_created[fhd] = true;
#undef S
}

void fr0g_gouache_tick(uint32_t ms, int64_t card_t, bool show) {
    for (int f = 0; f < 2; f++)
        if (g_created[f])
            set_hidden(G[f].root, !show);
    if (!show)
        return;

    const bool armed = D.flags & 1, air = D.flags & 32, bwarn = D.flags & 2, bcrit = D.flags & 4;
    static float thr_s = 0, roll_s = 0, pitch_s = 0, st[4] = {100, 0, 100, 100}, prop_a[4];
    static float mfull = 30000;
    static uint32_t last_ms = 0, orb_ms = 0;
    static float o_roll = 999, o_pitch = 999;
    const float dt = last_ms ? (ms - last_ms) / 1000.0f : 0.016f;
    last_ms = ms;
    thr_s += (D.thr - thr_s) * 0.3f;
    roll_s += (D.roll / 10.0f - roll_s) * 0.6f;
    pitch_s += (D.pitch / 10.0f - pitch_s) * 0.6f;
    for (int i = 0; i < 4; i++) {
        st[i] += (D.stk[i] - st[i]) * 0.5f;
        if (D.mot[i] > mfull)
            mfull = D.mot[i];
    }
    bool orb_dirty = false;
    if (ms - orb_ms >= 33 && (fabsf(roll_s - o_roll) > 0.3f || fabsf(pitch_s - o_pitch) > 0.3f)) {
        orb_ms = ms;
        o_roll = roll_s;
        o_pitch = pitch_s;
        orb_dirty = true;
    }
    const float pct = clampf((D.cv - 340) / 75.0f);
    int mavg = 0;
    for (int i = 0; i < 4; i++)
        mavg += D.mot[i] / 4;
    static const int MORD[4] = {3, 1, 2, 0};   // FL FR RL RR -> Betaflight motors 4 2 3 1

    char name[12], volt[12], ma[32], lq[24], tim[12], tp[8], lg[2][32];
    const char *nm = D.name;
    if (nm[0] == 'M' && nm[1] == 'K')
        nm += nm[2] == '.' ? 3 : 2;
    snprintf(name, sizeof(name), "Mk. %s", nm);
    snprintf(volt, sizeof(volt), "%d.%02d", D.cv / 100, D.cv % 100);
    snprintf(ma, sizeof(ma), "%d mAh  -  %d.%d A", D.mah, D.da / 10, D.da % 10);
    snprintf(lq, sizeof(lq), "%d per cent", D.lq);
    snprintf(tim, sizeof(tim), "%02d:%02d", D.tenths / 600, (D.tenths / 10) % 60);
    snprintf(tp, sizeof(tp), "%d%%", (int)lrintf(thr_s));
    for (int i = 0; i < 2; i++) {
        if (i < n_ev && ms - EV[i].born < 30000)
            snprintf(lg[i], sizeof(lg[i]), "%d:%02d  %s %s", (EV[i].tenths / 600) % 10, (EV[i].tenths / 10) % 60, EV[i].e, EV[i].v);
        else
            lg[i][0] = '\0';
    }
    const char *mode = (D.flags & 8) ? "Angle" : (D.flags & 16) ? "Horizon" : "Acro";

    // warnings (same rules as the teal HUD)
    static char shown[16] = "";
    static uint32_t changed = 0, gone = 0;
    const int ct = core_temp(D.wtxt);
    const bool has = D.wtxt[0] != '\0' && (ct < 0 || ct >= CPU_HOT_C) && !wrn_ignored(D.wtxt);
    if (has && strcmp(shown, D.wtxt) != 0) {
        strcpy(shown, D.wtxt);
        changed = ms;
    }
    if (has)
        gone = 0;
    else if (!gone)
        gone = ms;
    const float wa = has ? clampf((ms - changed) / 150.0f) : 1 - clampf((ms - gone) / 250.0f);
    const bool wcrit = D.wsev >= 3 || core_temp(shown) >= 0, wwarn = D.wsev == 2;
    char wt[24], wh[40];
    wrn_map(shown, wt, sizeof(wt), wh, sizeof(wh));
    const lv_color_t pulse = lv_color_mix(G_RUST, lv_color_hex(0x5A1E10), (uint8_t)(255 * (0.5f + 0.5f * sinf(ms * 0.012f))));

    for (int f = 0; f < 2; f++) {
        if (!g_created[f])
            continue;
        gh_t *u = &G[f];
        const float s = u->s;
        set_txt(u->name, name);
        set_txt(u->model, !strcmp(nm, "4-2") ? "Mobula Six" : "");
        // vial
        const int vh = (int)(u->vial_h * pct);
        lv_obj_set_y(u->vial_clip, u->vial_y + u->vial_h - vh);
        lv_obj_set_height(u->vial_clip, vh > 0 ? vh : 1);
        lv_obj_set_y(u->vial, vh - (int)lrintf(190 * s));
        set_rc(u->vial, pct > 0.4f ? G_SAGE : pct > 0.2f ? G_OCHRE : G_RUST);
        set_txt(u->volt, volt);
        set_tc(u->volt, bcrit ? G_RUST : bwarn ? G_OCHRE : G_INK);
        set_txt(u->mahamp, ma);
        const int petals = (D.lq * 8 + 50) / 100;
        for (int k = 0; k < 8; k++)
            set_hidden(u->petal[k], k >= petals);
        set_txt(u->lq, lq);
        set_tc(u->lq, D.lq < 70 ? G_RUST : G_INK);
        for (int i = 0; i < 2; i++) {
            set_txt(u->log[i], lg[i]);
            set_tc(u->log[i], i == 0 ? G_INK : G_FADED);
        }
        // status
        set_txt(u->tim, tim);
        set_rc(u->blk[0], armed ? G_RUST : G_FADED);
        set_txt(u->blk_txt[0], armed ? "Armed" : "Disarmed");
        set_rc(u->blk[1], air ? G_SAGE : G_FADED);
        set_txt(u->blk_txt[1], air ? "Air Mode" : "Air Off");
        set_rc(u->blk[2], G_PRUSS);
        set_txt(u->blk_txt[2], mode);
        // throttle column
        const int th = (int)(u->thr_h * clampf(thr_s / 100.0f));
        lv_obj_set_y(u->thr_clip, u->thr_y + u->thr_h - th);
        lv_obj_set_height(u->thr_clip, th > 0 ? th : 1);
        lv_obj_set_y(u->thr_col, th - (int)lrintf(172 * s));
        set_txt(u->thr_pct, tp);
        // motors: pie = RPM, the prop turns with it; a lagging motor paints rust
        for (int k = 0; k < 4; k++) {
            const int r = D.mot[MORD[k]];
            lv_arc_set_value(u->mot[k], (int16_t)(1000 * clampf(r / mfull)));
            const bool lag = armed && mavg > 3000 && r < mavg * 85 / 100;
            lv_obj_set_style_arc_color(u->mot[k], lag ? G_RUST : G_PRUSS, LV_PART_INDICATOR);
            if (f == 0)
                prop_a[k] += dt * (r / 1000.0f) * 0.9f * ((k == 0 || k == 3) ? 1 : -1);   // visual speed, not literal
            const float L = 20 * s;
            u->prop_pts[k][0].x = (lv_coord_t)(u->mot_cx[k] - L * cosf(prop_a[k]));
            u->prop_pts[k][0].y = (lv_coord_t)(u->mot_cy[k] - L * sinf(prop_a[k]));
            u->prop_pts[k][1].x = (lv_coord_t)(u->mot_cx[k] + L * cosf(prop_a[k]));
            u->prop_pts[k][1].y = (lv_coord_t)(u->mot_cy[k] + L * sinf(prop_a[k]));
            lv_line_set_points(u->prop[k], u->prop_pts[k], 2);
        }
        if (orb_dirty)
            orb_draw(u, roll_s, pitch_s);
        // rose needles
        for (int k = 0; k < 2; k++) {
            const float R = 40 * s;
            const int px = u->rose_cx[k] + (int)((st[k * 2] / 199.0f - 0.5f) * 2 * R);
            const int py = u->rose_cy - (int)((st[k * 2 + 1] / 199.0f - 0.5f) * 2 * R);
            u->needle_pts[k][1].x = (lv_coord_t)px;
            u->needle_pts[k][1].y = (lv_coord_t)py;
            lv_line_set_points(u->needle[k], u->needle_pts[k], 2);
            const int d = lv_obj_get_width(u->ndot[k]);
            lv_obj_set_pos(u->ndot[k], px - d / 2, py - d / 2);
        }
        // warning banner
        for (int k = 0; k < 2; k++) {
            set_hidden(u->edge[k], !(wcrit && has));
            if (wcrit && has)
                set_bg(u->edge[k], pulse);
        }
        if (wa <= 0.01f || !shown[0]) {
            set_hidden(u->wrn, true);
            set_hidden(u->hint, true);
        } else {
            set_hidden(u->wrn, false);
            set_txt(u->wrn_txt, wt);
            set_rc(u->wrn_bg, wcrit ? pulse : wwarn ? G_OCHRE : G_PRUSS);
            set_tc(u->wrn_txt, wwarn && !wcrit ? G_INK : G_PAPER);
            lv_obj_set_style_opa(u->wrn, (lv_opa_t)(255 * wa), 0);
            set_hidden(u->hint, wh[0] == '\0');
            set_txt(u->hint_txt, wh);
            lv_obj_set_style_opa(u->hint, (lv_opa_t)(255 * wa), 0);
        }
        // like & subscribe, once per pack
        const float t = card_t / 1000.0f, T_OUT = 8.0f;
        if (card_t < 0 || t > T_OUT + 0.6f) {
            set_hidden(u->card, true);
        } else {
            float pin = clampf(t / 0.5f);
            pin = 1 - (1 - pin) * (1 - pin) * (1 - pin);
            float pout = clampf((t - T_OUT) / 0.6f);
            pout *= pout;
            set_hidden(u->card, false);
            lv_obj_set_y(u->card, u->card_y + (int)((1 - pin) * 18 * s + pout * 18 * s));
            lv_obj_set_style_opa(u->card, (lv_opa_t)(255 * pin * (1 - pout)), 0);
            float beat = 1.0f;
            if (t > 0.8f)
                beat = 1 + 0.14f * powf(fmaxf(0, sinf((t - 0.8f) * 2 * 3.14159f * 1.2f)), 6);
            lv_img_set_zoom(u->c_heart, (uint16_t)(256 * 24 * s / 48 * beat));
        }
    }
}
