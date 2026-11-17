#include "fr0g_hud.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/*
 * Packet (bottom OSD row, page 1; each value cell 0x30 + v, v = 0..199; two-cell values hi * 200 + lo):
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

// Mega Drive palette (9-bit levels 0,52,87,116,144,172,206,255)
#define C_BLUE lv_color_hex(0x3457FF)
#define C_NAVY lv_color_hex(0x000074)
#define C_SKY lv_color_hex(0x74ACFF)
#define C_GOLD lv_color_hex(0xFFCE00)
#define C_RED lv_color_hex(0xCE0000)
#define C_GRN lv_color_hex(0x00AC34)
#define C_WHT lv_color_hex(0xFFFFFF)
#define C_GREY lv_color_hex(0x9090AC)
#define C_HEART lv_color_hex(0xFF5757)

LV_FONT_DECLARE(fr0g_bl_40); LV_FONT_DECLARE(fr0g_bl_60);
LV_FONT_DECLARE(fr0g_bl_36); LV_FONT_DECLARE(fr0g_bl_54);
LV_FONT_DECLARE(fr0g_bl_26); LV_FONT_DECLARE(fr0g_bl_39);
LV_FONT_DECLARE(fr0g_sb_20); LV_FONT_DECLARE(fr0g_sb_30);
LV_FONT_DECLARE(fr0g_sb_15); LV_FONT_DECLARE(fr0g_sb_23);
LV_FONT_DECLARE(fr0g_b_15); LV_FONT_DECLARE(fr0g_b_23);
LV_FONT_DECLARE(fr0g_b_11); LV_FONT_DECLARE(fr0g_b_17);

enum { F_IDLE, F_BLINK, F_BLINKH, F_GL, F_GR, F_GU, F_GD, F_GRIN, F_EXCL, F_HURT, F_KO, F_PUMPED, F_DIZZY, F_N };
LV_IMG_DECLARE(fr0g_frog_idle); LV_IMG_DECLARE(fr0g_frog_blink); LV_IMG_DECLARE(fr0g_frog_blinkH);
LV_IMG_DECLARE(fr0g_frog_glanceL); LV_IMG_DECLARE(fr0g_frog_glanceR); LV_IMG_DECLARE(fr0g_frog_glanceU);
LV_IMG_DECLARE(fr0g_frog_glanceD); LV_IMG_DECLARE(fr0g_frog_grin); LV_IMG_DECLARE(fr0g_frog_exclgrin);
LV_IMG_DECLARE(fr0g_frog_hurt); LV_IMG_DECLARE(fr0g_frog_ko); LV_IMG_DECLARE(fr0g_frog_pumped);
LV_IMG_DECLARE(fr0g_frog_dizzy); LV_IMG_DECLARE(fr0g_heart);
static const lv_img_dsc_t *FROG[F_N] = {&fr0g_frog_idle, &fr0g_frog_blink, &fr0g_frog_blinkH, &fr0g_frog_glanceL,
                                        &fr0g_frog_glanceR, &fr0g_frog_glanceU, &fr0g_frog_glanceD, &fr0g_frog_grin,
                                        &fr0g_frog_exclgrin, &fr0g_frog_hurt, &fr0g_frog_ko, &fr0g_frog_pumped, &fr0g_frog_dizzy};

#define NSEG 10
#define NFLOAT 5
typedef struct {
    lv_obj_t *root, *frog, *volt, *vlab, *gauge, *seg[NSEG], *mah, *amp, *lq;
    lv_obj_t *tim, *mode, *thr_box, *thr_fill, *thr_pct, *rpm;
    lv_obj_t *gim[2], *gdot[2], *gcore[2];
    lv_obj_t *card, *c_title, *c_heart, *c_yt, *c_gh, *c_float[NFLOAT];
    lv_obj_t *wrn, *wrn_txt, *edge[2];
    int thr_h, gim_in, card_y, heart_x, heart_y, wrn_y;
    float s;
} hud_t;

static hud_t U[2];
static bool created[2];
static lv_timer_t *timer;
static void tick(lv_timer_t *tm);

static struct {
    int stk[4];
    int cv, da, mah, lq, thr, rpm200, g10, tenths, flags;
    int wsev;
    char wtxt[16];
} D = {{100, 0, 100, 100}};
static volatile uint32_t seen_ms;

static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000u + tv.tv_usec / 1000u);
}

static lv_obj_t *box(lv_obj_t *p) {
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}
static lv_obj_t *text(lv_obj_t *p, const lv_font_t *f, lv_color_t c) {
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, "");
    return l;
}
// centre a label on x (left bar / right bar columns), at top y
static void centre(lv_obj_t *l, int x, int y) { lv_obj_align(l, LV_ALIGN_TOP_MID, x - lv_obj_get_width(lv_obj_get_parent(l)) / 2, y); }

void fr0g_hud_create(uint8_t fhd, lv_obj_t *parent) {
    hud_t *u = &U[fhd];
    const float s = fhd ? 1.5f : 1.0f;
    u->s = s;
#define S(v) ((int)lrintf((v) * s))
    const int SW = fhd ? 1920 : 1280, SH = fhd ? 1080 : 720, BAR = S(160);
    const lv_font_t *f_volt = fhd ? &fr0g_bl_60 : &fr0g_bl_40, *f_tim = fhd ? &fr0g_bl_54 : &fr0g_bl_36,
                    *f_pct = fhd ? &fr0g_bl_39 : &fr0g_bl_26, *f_val = fhd ? &fr0g_sb_30 : &fr0g_sb_20,
                    *f_small = fhd ? &fr0g_sb_23 : &fr0g_sb_15, *f_mode = fhd ? &fr0g_b_23 : &fr0g_b_15,
                    *f_lab = fhd ? &fr0g_b_17 : &fr0g_b_11;

    u->root = box(parent);
    lv_obj_set_size(u->root, SW, SH);

    // ---- left bar: frog, volts, battery gauge, mAh / current / LQ
    lv_obj_t *L = box(u->root);
    lv_obj_set_size(L, BAR, SH);
    u->frog = lv_img_create(L);
    lv_img_set_src(u->frog, FROG[F_IDLE]);
    lv_img_set_antialias(u->frog, true);
    lv_img_set_pivot(u->frog, 0, 0);
    lv_img_set_zoom(u->frog, (uint16_t)lrintf(256.0f * S(140) / 210.0f));
    lv_obj_set_pos(u->frog, S(10), S(12));
    const int top = S(12) + S(127) + S(12);
    u->volt = text(L, f_volt, C_WHT);
    centre(u->volt, BAR / 2, top);
    u->vlab = text(L, f_lab, C_SKY);
    lv_label_set_text(u->vlab, "VOLTS / CELL");
    centre(u->vlab, BAR / 2, top + S(46));
    const int gx0 = S(56), gy0 = top + S(72), gx1 = S(104), gy1 = SH - S(120);
    u->gauge = box(L);
    lv_obj_set_pos(u->gauge, gx0, gy0);
    lv_obj_set_size(u->gauge, gx1 - gx0, gy1 - gy0);
    lv_obj_set_style_radius(u->gauge, S(8), 0);
    lv_obj_set_style_border_color(u->gauge, C_SKY, 0);
    lv_obj_set_style_border_width(u->gauge, S(2), 0);
    const float sh = (gy1 - gy0 - S(8)) / (float)NSEG;
    for (int k = 0; k < NSEG; k++) {
        lv_obj_t *g = box(L);
        lv_obj_set_pos(g, gx0 + S(5), (int)(gy1 - S(4) - (k + 1) * sh + S(3)));
        lv_obj_set_size(g, gx1 - gx0 - S(10), (int)(sh - S(3)));
        lv_obj_set_style_bg_color(g, k <= 1 ? C_RED : k <= 3 ? C_GOLD : C_GRN, 0);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
        u->seg[k] = g;
    }
    u->mah = text(L, f_val, C_WHT);
    centre(u->mah, BAR / 2, SH - S(100));
    u->amp = text(L, f_val, C_SKY);
    centre(u->amp, BAR / 2, SH - S(72));
    u->lq = text(L, f_val, C_GOLD);
    centre(u->lq, BAR / 2, SH - S(44));

    // ---- right bar: timer, mode, throttle bar, % / RPM, stick gimbals
    lv_obj_t *R = box(u->root);
    lv_obj_set_size(R, BAR, SH);
    lv_obj_set_pos(R, SW - BAR, 0);
    u->tim = text(R, f_tim, C_WHT);
    centre(u->tim, BAR / 2, S(16));
    u->mode = text(R, f_mode, C_GOLD);
    centre(u->mode, BAR / 2, S(58));
    const int tx0 = BAR / 2 - S(24), ty0 = S(96), ty1 = SH - S(190);
    u->thr_box = box(R);
    lv_obj_set_pos(u->thr_box, tx0, ty0);
    lv_obj_set_size(u->thr_box, S(48), ty1 - ty0);
    lv_obj_set_style_radius(u->thr_box, S(8), 0);
    lv_obj_set_style_border_color(u->thr_box, C_SKY, 0);
    lv_obj_set_style_border_width(u->thr_box, S(2), 0);
    u->thr_h = ty1 - ty0 - S(10);
    u->thr_fill = box(u->thr_box);
    lv_obj_set_width(u->thr_fill, S(48) - S(10));
    lv_obj_align(u->thr_fill, LV_ALIGN_BOTTOM_MID, 0, -S(5));
    lv_obj_set_style_radius(u->thr_fill, S(4), 0);
    lv_obj_set_style_bg_opa(u->thr_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(u->thr_fill, C_SKY, 0);
    lv_obj_set_style_bg_grad_color(u->thr_fill, C_BLUE, 0);
    lv_obj_set_style_bg_grad_dir(u->thr_fill, LV_GRAD_DIR_VER, 0);
    u->thr_pct = text(R, f_pct, C_WHT);
    centre(u->thr_pct, BAR / 2, ty1 + S(8));
    u->rpm = text(R, f_small, C_SKY);
    centre(u->rpm, BAR / 2, ty1 + S(40));
    u->gim_in = S(64) - S(16);
    for (int k = 0; k < 2; k++) {
        lv_obj_t *g = box(R);
        lv_obj_set_size(g, S(64), S(64));
        lv_obj_set_pos(g, S(10) + k * S(72), SH - S(96));
        lv_obj_set_style_radius(g, S(8), 0);
        lv_obj_set_style_bg_color(g, C_NAVY, 0);
        lv_obj_set_style_bg_opa(g, 60, 0);
        lv_obj_set_style_border_color(g, C_SKY, 0);
        lv_obj_set_style_border_opa(g, 220, 0);
        lv_obj_set_style_border_width(g, S(2), 0);
        lv_obj_t *h = box(g), *v = box(g);
        lv_obj_set_size(h, S(52), fhd ? 2 : 1);
        lv_obj_center(h);
        lv_obj_set_size(v, fhd ? 2 : 1, S(52));
        lv_obj_center(v);
        lv_obj_set_style_bg_color(h, C_SKY, 0);
        lv_obj_set_style_bg_opa(h, 70, 0);
        lv_obj_set_style_bg_color(v, C_SKY, 0);
        lv_obj_set_style_bg_opa(v, 70, 0);
        lv_obj_t *dot = box(g);
        lv_obj_set_size(dot, S(12), S(12));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, C_WHT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_t *core = box(dot);
        lv_obj_set_size(core, S(6), S(6));
        lv_obj_center(core);
        lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(core, C_GOLD, 0);
        lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
        u->gim[k] = g;
        u->gdot[k] = dot;
    }

    // ---- warning banner: top centre of the picture; edge glows down the inside of both bars for critical
    for (int k = 0; k < 2; k++) {
        lv_obj_t *e = box(u->root);
        lv_obj_set_size(e, S(5), SH);
        lv_obj_set_pos(e, k == 0 ? BAR : SW - BAR - S(5), 0);
        lv_obj_set_style_bg_color(e, C_RED, 0);
        lv_obj_set_style_bg_opa(e, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(e, C_RED, 0);
        lv_obj_set_style_shadow_width(e, S(30), 0);
        lv_obj_set_style_shadow_spread(e, S(4), 0);
        lv_obj_add_flag(e, LV_OBJ_FLAG_HIDDEN);
        u->edge[k] = e;
    }
    u->wrn = box(u->root);
    lv_obj_set_height(u->wrn, S(46));
    lv_obj_set_style_radius(u->wrn, S(23), 0);
    lv_obj_set_style_bg_opa(u->wrn, 190, 0);
    lv_obj_set_style_border_width(u->wrn, S(3), 0);
    lv_obj_set_style_pad_hor(u->wrn, S(26), 0);
    u->wrn_y = S(22);
    u->wrn_txt = text(u->wrn, f_pct, C_WHT);
    lv_obj_center(u->wrn_txt);
    lv_obj_add_flag(u->wrn, LV_OBJ_FLAG_HIDDEN);

    // ---- like & subscribe pill, centred low over the picture (hidden until 5 s after the first arm)
    const int cw = S(340), chh = S(96);
    u->card = box(u->root);
    lv_obj_set_size(u->card, cw, chh);
    u->card_y = SH - chh - S(40);
    lv_obj_set_pos(u->card, (SW - cw) / 2, u->card_y);
    lv_obj_set_style_radius(u->card, chh / 2, 0);
    lv_obj_set_style_bg_color(u->card, C_NAVY, 0);
    lv_obj_set_style_bg_opa(u->card, 120, 0);
    lv_obj_set_style_border_color(u->card, C_SKY, 0);
    lv_obj_set_style_border_opa(u->card, 150, 0);
    lv_obj_set_style_border_width(u->card, S(2), 0);
    u->c_title = text(u->card, f_pct, C_WHT);
    lv_label_set_text(u->c_title, "like & subscribe");
    lv_obj_set_pos(u->c_title, S(44), S(14));
    lv_obj_update_layout(u->c_title);
    u->heart_x = S(44) + lv_obj_get_width(u->c_title) + S(10);
    u->heart_y = S(18);
    for (int i = 0; i < NFLOAT; i++) {
        lv_obj_t *h = lv_img_create(u->root);   // on the root so they can float out of the pill
        lv_img_set_src(h, &fr0g_heart);
        lv_img_set_antialias(h, true);
        lv_obj_set_style_img_recolor_opa(h, LV_OPA_COVER, 0);
        lv_obj_set_style_img_recolor(h, i % 3 == 0 ? C_HEART : i % 3 == 1 ? C_GOLD : lv_color_hex(0xFF90AC), 0);
        lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
        u->c_float[i] = h;
    }
    u->c_heart = lv_img_create(u->card);
    lv_img_set_src(u->c_heart, &fr0g_heart);
    lv_img_set_antialias(u->c_heart, true);
    lv_obj_set_style_img_recolor_opa(u->c_heart, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(u->c_heart, C_HEART, 0);
    lv_obj_set_pos(u->c_heart, u->heart_x, u->heart_y);
    lv_img_set_zoom(u->c_heart, (uint16_t)(256 * S(24) / 48));
    u->c_yt = text(u->card, f_small, C_SKY);
    lv_label_set_text(u->c_yt, "youtube.com/@fr0gzerofpv");
    lv_obj_set_pos(u->c_yt, S(44), S(50));
    u->c_gh = text(u->card, f_small, C_GREY);
    lv_label_set_text(u->c_gh, "github.com/fr0gzero");
    lv_obj_set_pos(u->c_gh, S(44), S(70));
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

// ---------------- the card ----------------
static float clampf(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static void card_tick(hud_t *u, uint32_t ms, int64_t t_ms) {
    const float t = t_ms / 1000.0f, T_OUT = 5.6f;
    if (t_ms < 0 || t > 6.2f) {
        lv_obj_add_flag(u->card, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < NFLOAT; i++)
            lv_obj_add_flag(u->c_float[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const float s = u->s;
    float pin = clampf(t / 0.5f);
    pin = 1 - (1 - pin) * (1 - pin) * (1 - pin);
    float pout = clampf((t - T_OUT) / 0.6f);
    pout *= pout;
    const float a = pin * (1 - pout);
    lv_obj_clear_flag(u->card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(u->card, u->card_y + (int)((1 - pin) * 18 * s + pout * 18 * s));
    lv_obj_set_style_opa(u->card, (lv_opa_t)(255 * a), 0);
    const float tp = clampf((t - 0.7f) / 0.4f);
    lv_obj_set_style_opa(u->c_title, (lv_opa_t)(255 * tp), 0);
    lv_obj_set_style_opa(u->c_yt, (lv_opa_t)(255 * tp), 0);
    lv_obj_set_style_opa(u->c_gh, (lv_opa_t)(255 * tp), 0);
    lv_obj_set_style_opa(u->c_heart, (lv_opa_t)(255 * tp), 0);
    float beat = 1.0f;
    if (t > 1.1f) {
        const float sb = fmaxf(0, sinf((t - 1.1f) * 2 * 3.14159f * 1.2f));
        beat = 1 + 0.12f * powf(sb, 6);
    }
    lv_img_set_zoom(u->c_heart, (uint16_t)(256 * 24 * s / 48 * beat));
    // floating hearts from the heart
    const int hx = lv_obj_get_x(u->card) + u->heart_x, hy = lv_obj_get_y(u->card) + u->heart_y;
    for (int k = 0; k < NFLOAT; k++) {
        const float ph = t - 1.2f - k * 0.55f;
        if (ph < 0 || ph > 1.8f || t > T_OUT) {
            lv_obj_add_flag(u->c_float[k], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const float p = ph / 1.8f;
        lv_obj_clear_flag(u->c_float[k], LV_OBJ_FLAG_HIDDEN);
        lv_img_set_zoom(u->c_float[k], (uint16_t)(256 * 12 * s / 48 * (1 - p * 0.4f)));
        lv_obj_set_pos(u->c_float[k], hx + (int)((10 * sinf(ph * 5 + k) + k * 3) * s) - (int)(12 * s),
                       hy - (int)(p * 90 * s) - (int)(12 * s));
        lv_obj_set_style_opa(u->c_float[k], (lv_opa_t)(220 * (1 - p) * a), 0);
    }
    (void)ms;
}

// ---------------- warnings ----------------
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
    float a;
    if (has) {
        a = clampf((ms - changed) / 150.0f);
    } else {
        a = 1 - clampf((ms - gone) / 250.0f);
    }
    const bool crit = D.wsev >= 3, warn = D.wsev == 2;
    for (int k = 0; k < 2; k++) {
        if (crit && has) {
            lv_obj_clear_flag(u->edge[k], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(u->edge[k], (lv_opa_t)(120 + 120 * (0.5f + 0.5f * sinf(ms * 0.012f))), 0);
        } else {
            lv_obj_add_flag(u->edge[k], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (a <= 0.01f || !shown[0]) {
        lv_obj_add_flag(u->wrn, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(u->wrn, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(lv_label_get_text(u->wrn_txt), shown) != 0) {
        lv_label_set_text(u->wrn_txt, shown);
        lv_obj_set_width(u->wrn, LV_SIZE_CONTENT);
    }
    lv_obj_align(u->wrn, LV_ALIGN_TOP_MID, 0, u->wrn_y - (int)((1 - a) * 14 * s));
    if (crit) {
        lv_obj_set_style_bg_color(u->wrn, C_RED, 0);
        lv_obj_set_style_bg_opa(u->wrn, (lv_opa_t)(170 + 70 * (0.5f + 0.5f * sinf(ms * 0.012f))), 0);
        lv_obj_set_style_border_color(u->wrn, C_WHT, 0);
        lv_obj_set_style_text_color(u->wrn_txt, C_WHT, 0);
    } else if (warn) {
        lv_obj_set_style_bg_color(u->wrn, C_NAVY, 0);
        lv_obj_set_style_bg_opa(u->wrn, 190, 0);
        lv_obj_set_style_border_color(u->wrn, C_GOLD, 0);
        lv_obj_set_style_text_color(u->wrn_txt, C_GOLD, 0);
    } else {
        lv_obj_set_style_bg_color(u->wrn, C_NAVY, 0);
        lv_obj_set_style_bg_opa(u->wrn, 170, 0);
        lv_obj_set_style_border_color(u->wrn, C_SKY, 0);
        lv_obj_set_style_text_color(u->wrn_txt, C_WHT, 0);
    }
    lv_obj_set_style_opa(u->wrn, (lv_opa_t)(255 * a), 0);
}

static void tick(lv_timer_t *tm) {
    (void)tm;
    const uint32_t ms = now_ms();
    const bool live = (ms - seen_ms) < 600;
    for (int f = 0; f < 2; f++) {
        if (!created[f])
            continue;
        if (live)
            lv_obj_clear_flag(U[f].root, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(U[f].root, LV_OBJ_FLAG_HIDDEN);
    }
    if (!live)
        return;

    const bool armed = D.flags & 1, warn = D.flags & 2, crit = D.flags & 4;
    // first arm (since the goggles saw this quad): the card 5 s later
    static int64_t first_arm_ms = -1;
    if (armed && first_arm_ms < 0)
        first_arm_ms = ms;
    const int64_t card_t = first_arm_ms < 0 ? -1 : (int64_t)ms - first_arm_ms - 5000;

    static float thr_s = 0, st[4] = {100, 0, 100, 100};
    thr_s += (D.thr - thr_s) * 0.3f;
    for (int i = 0; i < 4; i++)
        st[i] += (D.stk[i] - st[i]) * 0.45f;
    float pct = (D.cv - 330) / 90.0f;
    pct = clampf(pct);
    const int lit = (int)lrintf(pct * NSEG);

    char volt[12], mah[16], amp[16], lq[12], tim[12], tp[8], rpm[16];
    snprintf(volt, sizeof(volt), "%d.%02d", D.cv / 100, D.cv % 100);
    snprintf(mah, sizeof(mah), "%d mAh", D.mah);
    snprintf(amp, sizeof(amp), "%d.%d A", D.da / 10, D.da % 10);
    snprintf(lq, sizeof(lq), "LQ %d", D.lq);
    snprintf(tim, sizeof(tim), "%02d:%02d", D.tenths / 600, (D.tenths / 10) % 60);
    snprintf(tp, sizeof(tp), "%d%%", (int)lrintf(thr_s));
    const int r = D.rpm200 * 200;
    snprintf(rpm, sizeof(rpm), "%d.%dk RPM", r / 1000, (r % 1000) / 100);
    const char *mode = (D.flags & 8) ? "ANGLE" : (D.flags & 16) ? "HORIZON" : "ACRO";
    const int fr = frog_frame(ms, armed);

    for (int f = 0; f < 2; f++) {
        if (!created[f])
            continue;
        hud_t *u = &U[f];
        if (lv_img_get_src(u->frog) != FROG[fr])
            lv_img_set_src(u->frog, FROG[fr]);
        lv_label_set_text(u->volt, volt);
        lv_obj_set_style_text_color(u->volt, crit ? C_RED : warn ? C_GOLD : C_WHT, 0);
        for (int k = 0; k < NSEG; k++)
            lv_obj_set_style_bg_opa(u->seg[k], k < lit ? LV_OPA_COVER : 40, 0);
        lv_label_set_text(u->mah, mah);
        lv_label_set_text(u->amp, amp);
        lv_label_set_text(u->lq, lq);
        lv_label_set_text(u->tim, tim);
        lv_label_set_text(u->mode, mode);
        lv_obj_set_height(u->thr_fill, (int)(u->thr_h * clampf(thr_s / 100.0f)));
        lv_label_set_text(u->thr_pct, tp);
        lv_label_set_text(u->rpm, rpm);
        for (int k = 0; k < 2; k++) {
            const float hx = st[k * 2] / 199.0f, hy = st[k * 2 + 1] / 199.0f;
            const int d = lv_obj_get_width(u->gdot[k]), pad = (lv_obj_get_width(u->gim[k]) - u->gim_in) / 2;
            lv_obj_set_pos(u->gdot[k], pad + (int)(hx * u->gim_in) - d / 2, pad + (int)((1 - hy) * u->gim_in) - d / 2);
        }
        card_tick(u, ms, card_t);
        wrn_tick(u, ms);
    }
}

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
            seen_ms = now_ms();
        }
        return 0x20;
    }
    return ch;
}

