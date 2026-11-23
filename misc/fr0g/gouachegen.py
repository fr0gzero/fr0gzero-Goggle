#!/usr/bin/env python3
"""fr0gOS gouache theme art -> LVGL 8 TRUE_COLOR_ALPHA images (B, G, R, A).
Static sidebar art (paper, ink outlines, dials, captions) is baked at HD size; FHD shows it zoomed 1.5x.
Swatches are white with painted, ragged alpha so the HUD can recolour them at runtime.
usage: gouachegen.py <out.c> [preview_dir]"""
import math, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont
OUT = sys.argv[1]; PREV = sys.argv[2] if len(sys.argv) > 2 else None
LM = "/usr/share/texmf/fonts/opentype/public/lm/"
SER = lambda s: ImageFont.truetype(LM + "lmroman10-regular.otf", s)
SERB = lambda s: ImageFont.truetype(LM + "lmroman10-bold.otf", s)
SERI = lambda s: ImageFont.truetype(LM + "lmroman10-italic.otf", s)
CAPS = lambda s: ImageFont.truetype(LM + "lmromancaps10-regular.otf", s)
W, H, BAR = 1280, 720, 160
INK = (52, 40, 34); RUST = (178, 74, 44)
SS = 3   # supersample

def paper(w, h, seed):
    r = random.Random(seed); im = Image.new("RGB", (w, h), (236, 226, 204)); px = im.load()
    for y in range(h):
        for x in range(w):
            n = r.randint(-9, 9); c = px[x, y]; px[x, y] = (c[0] + n, c[1] + n, c[2] + n - 2)
    # a few soft fibres / blotches
    d = ImageDraw.Draw(im, "RGBA")
    for k in range(int(w * h / 900)):
        x, y = r.uniform(0, w), r.uniform(0, h); L = r.uniform(4, 14); a = r.uniform(0, math.pi)
        d.line((x, y, x + L * math.cos(a), y + L * math.sin(a)), fill=(200, 186, 160, 60), width=1)
    for k in range(int(w * h / 6000)):
        x, y = r.uniform(0, w), r.uniform(0, h); rr = r.uniform(6, 22)
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=(214, 200, 172, 22))
    return im.filter(ImageFilter.GaussianBlur(0.6))

def bar_art(right):
    im = paper(BAR, H, 7 if right else 3).convert("RGBA")
    big = Image.new("RGBA", (BAR * SS, H * SS), (0, 0, 0, 0)); d = ImageDraw.Draw(big)
    S = lambda v: v * SS
    def text(x, y, t, f, c, a="mm"): d.text((S(x), S(y)), t, font=f, fill=c + (255,), anchor=a)
    def ring(cx, cy, r, w=2, c=INK, a=255): d.ellipse((S(cx - r), S(cy - r), S(cx + r), S(cy + r)), outline=c + (a,), width=S(w))
    def rose(cx, cy, r, lab):
        ring(cx, cy, r); ring(cx, cy, r - 5, 1, INK, 120)
        for k in range(16):
            a = math.radians(k * 22.5); l = 10 if k % 4 == 0 else 5
            d.line((S(cx + (r - l) * math.cos(a)), S(cy + (r - l) * math.sin(a)), S(cx + r * math.cos(a)), S(cy + r * math.sin(a))), fill=INK + (220,), width=S(1))
        for k, nm in enumerate("NESW"):
            a = math.radians(k * 90 - 90); text(cx + (r + 9) * math.cos(a), cy + (r + 9) * math.sin(a), nm, SERB(S(11)), RUST)
        d.ellipse((S(cx - 3), S(cy - 3), S(cx + 3), S(cy + 3)), fill=INK + (255,))
        text(cx, cy + r + 24, lab, SERI(S(12)), INK)
    if not right:
        # battery vial
        d.rounded_rectangle((S(52), S(96), S(108), S(300)), S(18), outline=INK + (255,), width=S(2))
        for k in range(5):
            yy = 116 + k * 36; d.line((S(108), S(yy), S(116), S(yy)), fill=INK + (200,), width=S(1))
        d.rectangle((S(70), S(86), S(90), S(96)), fill=INK + (255,))
        text(80, 344, "volts the cell", SERI(S(13)), INK)
        text(80, 402, "Signal", CAPS(S(14)), INK)
        for k in range(8):
            a = math.radians(k * 45 - 90); cx, cy = 80 + 18 * math.cos(a), 438 + 18 * math.sin(a); ring(cx, cy, 8, 1, INK, 90)
        rose(80, 596, 52, "yaw & throttle")
    else:
        text(80, 62, "minutes aloft", SERI(S(12)), INK)
        text(80, 190, "Throttle", CAPS(S(14)), INK)
        d.rectangle((S(20), S(204), S(56), S(380)), outline=INK + (255,), width=S(2))
        for k, nm in enumerate(("FL", "FR", "RL", "RR")):
            cx = 86 + (k % 2) * 42; cy = 232 + (k // 2) * 84
            ring(cx, cy, 16); text(cx, cy + 28, nm, SER(S(11)), INK)
        ring(80, 454, 46)
        text(80, 514, "the attitude", SERI(S(12)), INK)
        rose(80, 596, 52, "roll & pitch")
    big = big.resize((BAR, H), Image.LANCZOS)
    im.alpha_composite(big)
    return im

def swatch(w, h, seed, jit=2.5, layers=4):
    """white, ragged-edged brushstroke rectangle with streaky alpha"""
    r = random.Random(seed); big = Image.new("L", (w * SS, h * SS), 0); d = ImageDraw.Draw(big)
    pad = 2 * SS
    for k in range(layers):
        j = lambda: r.uniform(-jit, jit) * SS
        pts = []
        n = max(6, int(w / 6))
        for i in range(n + 1): pts.append((pad + i * (w * SS - 2 * pad) / n + j() * 0.3, pad + j()))
        for i in range(n + 1): pts.append((w * SS - pad - i * (w * SS - 2 * pad) / n + j() * 0.3, h * SS - pad + j()))
        d.polygon(pts, fill=255)
    # opaque inside (the goggle keys semi-transparent pixels against grey), ragged anti-aliased edge
    alpha = big.resize((w, h), Image.LANCZOS)
    # brush streaks along the long axis, carried in the brightness (recolour keeps some of it)
    st = Image.new("L", (w * SS, h * SS), 255); sd = ImageDraw.Draw(st)
    horiz = w >= h
    for k in range(int((h if horiz else w) * SS / 2)):
        v = r.randint(150, 255)
        if horiz: y = r.uniform(0, h * SS); sd.line((0, y, w * SS, y + r.uniform(-3, 3)), fill=v, width=r.randint(1, 3))
        else: x = r.uniform(0, w * SS); sd.line((x, 0, x + r.uniform(-3, 3), h * SS), fill=v, width=r.randint(1, 3))
    st = st.filter(ImageFilter.GaussianBlur(1.2)).resize((w, h), Image.LANCZOS)
    im = Image.merge("RGBA", (st, st, st, alpha))
    return im

def petal(seed):
    r = random.Random(seed); s = 20; big = Image.new("L", (s * SS, s * SS), 0); d = ImageDraw.Draw(big)
    for k in range(3):
        pts = []
        for a in range(0, 360, 12):
            ra = math.radians(a); rr = (8 + r.uniform(-1.5, 1.5)) * SS
            pts.append((s * SS / 2 + rr * math.cos(ra), s * SS / 2 + rr * math.sin(ra)))
        d.polygon(pts, fill=255)
    im = Image.new("RGBA", (s, s), (255, 255, 255, 0)); im.putalpha(big.resize((s, s), Image.LANCZOS)); return im

out = ['// Generated by misc/fr0g/gouachegen.py - do not edit.\n#include "lvgl/lvgl.h"\n']
def emit(name, im):
    w, h = im.size; px = im.load(); data = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]; data += [b, g, r, a]
    out.append(f"static const uint8_t {name}_map[] = {{\n")
    for i in range(0, len(data), 48):
        out.append("    " + ",".join(str(v) for v in data[i:i + 48]) + ",\n")
    out.append("};\n")
    out.append(f"const lv_img_dsc_t {name} = {{.header.always_zero = 0, .header.w = {w}, .header.h = {h}, .data_size = {len(data)}, "
               f".header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .data = {name}_map}};\n\n")
    if PREV: im.save(f"{PREV}/{name}.png")
emit("fr0g_g_left", bar_art(False))
emit("fr0g_g_right", bar_art(True))
emit("fr0g_g_block", swatch(128, 24, 1))
emit("fr0g_g_col", swatch(30, 172, 2, 1.5))
emit("fr0g_g_vial", swatch(46, 190, 3, 1.5))
emit("fr0g_g_banner", swatch(380, 44, 4, 3))
emit("fr0g_g_hint", swatch(320, 26, 5, 2))
emit("fr0g_g_card", swatch(560, 48, 6, 3))
emit("fr0g_g_title", swatch(140, 50, 8, 2.5))
emit("fr0g_g_petal", petal(9))
open(OUT, "w").write("".join(out))
