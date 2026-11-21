#!/usr/bin/env python3
"""Poudre Trail pixel-art generator (deterministic, Pillow only).

Run:  python3 tools/make_art.py
Writes every scene/portrait natively at 96x72 to data/art/ and a 2x nearest
upscale (192x144) to data/art2x/.  Scenes are looping GIFs (every motion is
driven by phase = 2*pi*frame/N with integer pixel positions, so they loop
seamlessly); portraits and still lifes are PNGs.

Toolkit (top of file): palette, dithered vertical gradients, noise ridgelines,
discs/polys, star fields, clouds, cottonwoods, log cabins, river polygons with
scrolling glint rows, grass blades with sway, smoke/spark/fluff particles.
"""
import math, os, random, sys
from PIL import Image, ImageDraw

W, H = 96, 72
TAU = 2 * math.pi
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
OUT1 = os.path.join(ROOT, 'data', 'art')
OUT2 = os.path.join(ROOT, 'data', 'art2x')

# ----------------------------------------------------------------- colour
def C(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))

def ramp(stops, t):
    t = max(0.0, min(1.0, t)); n = len(stops) - 1
    if n == 0:
        return stops[0]
    i = min(int(t * n), n - 1)
    return mix(stops[i], stops[i + 1], t * n - i)

# shared palette: dusky blues/violets, ochre + sage land, warm lamp orange
NIGHT = [C('141330'), C('221e47'), C('3a2f5b'), C('5b3f6a'), C('8a5270'), C('c56e6c'), C('ee9f5e'), C('f8d58c')]
DAY = [C('5f7fa8'), C('8aa4bf'), C('c0c8cc'), C('ecd9a8')]
SAGE_D, SAGE, OLIVE = C('2f3d33'), C('55694b'), C('7b8050')
OCHRE, TAN, SAND = C('a97c43'), C('c9a263'), C('e1c78f')
BROWN, BROWN_D, BROWN_L = C('6a4a35'), C('382722'), C('92683f')
ROCK_D, ROCK, ROCK_L = C('45394a'), C('786560'), C('a58d79')
WATER_D, WATER, WATER_L, FOAM = C('2b3b5c'), C('4a6988'), C('8bb0c0'), C('e8eee4')
LAMP, LAMP_L, LAMP_D = C('ffb347'), C('ffe0a0'), C('d9601f')
INK = C('1c1822')
LEAF_D, LEAF, LEAF_L = C('2f4a35'), C('5d7a45'), C('9bb060')

B4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
def bay(x, y):
    return (B4[y & 3][x & 3] + 0.5) / 16.0

# ------------------------------------------------------------ primitives
def px(im, x, y, c):
    if 0 <= x < im.width and 0 <= y < im.height:
        im.putpixel((x, y), c)

def pxa(im, x, y, c, a):
    if 0 <= x < im.width and 0 <= y < im.height:
        im.putpixel((x, y), mix(im.getpixel((x, y)), c, a))

def dpx(im, x, y, c, a):
    """ordered-dither alpha: keeps the palette small"""
    if a > bay(x, y):
        px(im, x, y, c)

def rect(im, x0, y0, x1, y1, c):
    ImageDraw.Draw(im).rectangle([x0, y0, x1, y1], fill=c)

def line(im, x0, y0, x1, y1, c):
    ImageDraw.Draw(im).line([x0, y0, x1, y1], fill=c)

def poly(im, pts, c):
    ImageDraw.Draw(im).polygon(pts, fill=c)

def disc(im, cx, cy, rx, ry, c, a=1.0, clip=None):
    for y in range(int(cy - ry - 1), int(cy + ry + 2)):
        for x in range(int(cx - rx - 1), int(cx + rx + 2)):
            if ((x - cx) / (rx + .5)) ** 2 + ((y - cy) / (ry + .5)) ** 2 <= 1:
                if clip and not clip(x, y):
                    continue
                if a >= 1:
                    px(im, x, y, c)
                else:
                    dpx(im, x, y, c, a)

def vgrad(im, y0, y1, stops, bands=8, x0=0, x1=W):
    cols = [ramp(stops, i / (bands - 1)) for i in range(bands)]
    for y in range(y0, y1):
        t = (y - y0) / max(1, (y1 - y0 - 1)) * (bands - 1)
        lo = int(t); fr = t - lo
        for x in range(x0, x1):
            k = min(lo + (1 if fr > bay(x, y) else 0), bands - 1)
            px(im, x, y, cols[k])

def dither_fill(im, x0, y0, x1, y1, c1, c2, a):
    """fill box with c1 and c2 mixed at ratio a (of c2)"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(im, x, y, c2 if a > bay(x, y) else c1)

def noise(rng, n, scale, octs=3, periodic=False):
    out = [0.0] * n; amp = 1.0; tot = 0.0
    for o in range(octs):
        sc = max(1, scale // (2 ** o))
        m = n // sc if periodic else n // sc + 2
        pts = [rng.random() for _ in range(m)]
        for x in range(n):
            u = x / sc; i = int(u); f = u - i; f = f * f * (3 - 2 * f)
            a = pts[i % m]; b = pts[(i + 1) % m]
            out[x] += (a + (b - a) * f) * amp
        tot += amp; amp *= 0.5
    return [v / tot for v in out]

def ridge(im, base, amp, col, rng, scale=14, octs=3, hi=None, bottom=H, jag=0.0, shade=None):
    """ridge-line from smoothed noise; returns top y per column"""
    h = noise(rng, W, scale, octs)
    tops = []
    for x in range(W):
        top = int(base - amp * h[x] + (rng.random() - .5) * jag * 2)
        tops.append(top)
        for y in range(max(0, top), bottom):
            c = col
            if shade and (y - top) > 3 and ((x + y) & 1) and ((x * 3 + y) % 7 < 2):
                c = shade
            px(im, x, y, c)
        if hi:
            px(im, x, top, hi)
    return tops

def hline(im, y, x0, x1, c):
    for x in range(x0, x1 + 1):
        px(im, x, y, c)

# ------------------------------------------------------------- sky things
S_DIM, S_MID, S_HI = C('6f6c9c'), C('b9b6d2'), C('fff4d8')

def make_stars(rng, n, ymax, ymin=0, xmin=0, xmax=W):
    return [(rng.randrange(xmin, xmax), rng.randrange(ymin, ymax), rng.random(), rng.choice([1, 1, 2]),
             rng.random() < 0.35) for _ in range(n)]

def draw_stars(im, stars, f, N, twinkle=True):
    for (x, y, off, k, big) in stars:
        if twinkle:
            a = 0.5 + 0.5 * math.sin(TAU * (k * f / N + off))
        else:
            a = 0.5
        lvl = 0 if a < 0.33 else (1 if a < 0.75 else 2)
        c = (S_DIM, S_MID, S_HI)[lvl]
        px(im, x, y, c)
        if big and lvl == 2:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                px(im, x + dx, y + dy, S_DIM)

def cloud(im, cx, cy, w, h, seed, cols, alpha=1.0):
    rng = random.Random(seed)
    mask = set()
    n = max(3, w // 6)
    for i in range(n):
        t = i / (n - 1)
        bx = cx - w // 2 + int((i + .5) * w / n)
        rad = h * (0.55 + 0.45 * math.sin(math.pi * t)) * (0.8 + 0.4 * rng.random())
        for y in range(int(cy - rad * 1.5), cy + 1):
            for x in range(int(bx - rad * 1.6), int(bx + rad * 1.6) + 1):
                if ((x - bx) / (rad * 1.5)) ** 2 + ((y - (cy - rad * .55)) / rad) ** 2 <= 1:
                    mask.add((x, y))
    light, mid, shade = cols
    for (x, y) in mask:
        if (x, y - 1) not in mask:
            c = light
        elif y > cy - h * .35:
            c = shade
        else:
            c = mid
        if alpha >= 1:
            px(im, x, y, c)
        else:
            dpx(im, x, y, c, alpha)
    return mask

def age(f, N, i, K):
    return ((f / N) + i / K) % 1.0

def smoke(im, x0, y0, f, N, K, rise, drift, col, amax=0.8, seed=1, r0=1.0, r1=3.0):
    rng = random.Random(seed)
    for i in range(K):
        a = age(f, N, i, K); jit = rng.random()
        y = y0 - a * rise
        x = x0 + drift * a + 2.2 * math.sin(TAU * (a * 1.2 + jit))
        r = r0 + a * (r1 - r0)
        al = amax * math.sin(math.pi * a) ** .8
        for yy in range(int(y - r - 1), int(y + r + 2)):
            for xx in range(int(x - r - 1), int(x + r + 2)):
                d = ((xx - x) ** 2 + (yy - y) ** 2) ** .5
                if d <= r + .3:
                    dpx(im, xx, yy, col, al * (1 - d / (r + 1.2) * .5))

def sparks(im, x0, y0, f, N, K, rise, spread, cols, seed=2):
    rng = random.Random(seed)
    for i in range(K):
        a = age(f, N, i, K)
        vx = (rng.random() - .5) * spread; ph = rng.random() * TAU
        x = x0 + vx * a * 2 + int(round(1.2 * math.sin(ph + a * 6)))
        y = y0 - a * rise * (0.6 + 0.5 * rng.random())
        c = cols[0] if a < .35 else (cols[1] if a < .7 else cols[2])
        if a < .92:
            px(im, int(round(x)), int(round(y)), c)

# ------------------------------------------------------------- water
def water_rows(im, y0, y1, xl_fn, xr_fn, col_fn):
    ext = []
    for y in range(y0, y1):
        xl, xr = int(xl_fn(y)), int(xr_fn(y))
        for x in range(max(0, xl), min(W - 1, xr) + 1):
            px(im, x, y, col_fn(x, y))
        ext.append((y, xl, xr))
    return ext

def make_streaks(ext, rng, N, density=.6, speeds=(1, 2), length=(2, 5), dirn=1):
    out = []
    for (y, xl, xr) in ext:
        if rng.random() > density or xr - xl < 3:
            continue
        s = rng.choice(speeds)
        out.append(dict(y=y, xl=xl, xr=xr, s=s, base=rng.randrange(0, s * N),
                        ln=rng.randint(*length), d=dirn, c=rng.random()))
    return out

def draw_streaks(im, streaks, N, f, cols, thr=(.55,)):
    for st in streaks:
        per = st['s'] * N
        c = cols[1] if st['c'] > thr[0] else cols[0]
        sh = (st['base'] + st['d'] * st['s'] * f) % per
        for j in range(-1, W // per + 2):
            x0 = sh + j * per
            for k in range(st['ln']):
                x = x0 + k
                if st['xl'] <= x <= st['xr']:
                    px(im, x, st['y'], c)

def sparkle(im, pts, f, N, cols):
    """pts: list of (x,y,off,k). in-place twinkle."""
    for (x, y, off, k) in pts:
        a = math.sin(TAU * (k * f / N + off))
        if a > .55:
            px(im, x, y, cols[1])
        elif a > .1:
            px(im, x, y, cols[0])

def pick_sparkles(ext, rng, n):
    pts = []
    for _ in range(n):
        y, xl, xr = rng.choice(ext)
        if xr > xl:
            pts.append((rng.randint(xl, xr), y, rng.random(), rng.choice([1, 2])))
    return pts

# ------------------------------------------------------------- grass
def make_blades(rng, n, x0, x1, y0, y1, hmin=2, hmax=4):
    return [(rng.randrange(x0, x1), rng.randrange(y0, y1), rng.randint(hmin, hmax), rng.random()) for _ in range(n)]

def draw_grass(im, blades, f, N, cols, amp=1.2, wave=.22):
    for (x, y, h, r) in blades:
        s = amp * math.sin(TAU * f / N - x * wave + y * .1)
        tipx = int(round(s))
        base = cols[int(r * len(cols)) % len(cols)]
        for k in range(1, h + 1):
            dx = int(round(s * (k / h) ** 1.5))
            px(im, x + dx, y - k, base if k < h else cols[-1])

# ------------------------------------------------------------- structures
def cottonwood(im, x, yb, h, w, rng, trunk=BROWN_D, cols=(LEAF_D, LEAF, LEAF_L), lean=0, n_blobs=None):
    """returns list of canopy pixels (x,y,col) for shimmer"""
    tw = max(2, w // 6)
    top = yb - h
    for y in range(int(top + h * .45), yb + 1):
        t = (y - top) / h
        xx = x + int(lean * (1 - t))
        wid = tw + (1 if t > .85 else 0)
        for dx in range(-wid // 2, wid - wid // 2):
            px(im, xx + dx, y, trunk if dx < wid - 1 else mix(trunk, BROWN, .6))
    # branches
    for sgn in (-1, 1):
        for k in range(int(h * .3)):
            bx = x + sgn * (k + 1) + lean // 2
            by = int(top + h * .55) - int(k * .9)
            px(im, bx, by, trunk)
    n_blobs = n_blobs or int(w / 5) + 3
    blobs = []
    for i in range(n_blobs):
        ang = rng.random() * TAU; rr = rng.random() ** .6
        bx = x + lean // 2 + math.cos(ang) * rr * w * .42
        by = top + h * .32 + math.sin(ang) * rr * h * .28
        br = w * (0.13 + rng.random() * .08)
        blobs.append((bx, by, br))
    pix = {}
    for (bx, by, br) in blobs:
        for yy in range(int(by - br), int(by + br) + 1):
            for xx in range(int(bx - br * 1.2), int(bx + br * 1.2) + 1):
                d = ((xx - bx) / (br * 1.2)) ** 2 + ((yy - by) / br) ** 2
                if d <= 1:
                    light = (-(xx - bx) * .5 - (yy - by) * .9) / (br + .1) + (rng.random() - .5) * .9
                    c = cols[2] if light > .45 else (cols[1] if light > -.35 else cols[0])
                    pix[(xx, yy)] = c
    for (xx, yy), c in pix.items():
        px(im, xx, yy, c)
    return [(xx, yy, c) for (xx, yy), c in pix.items()]

def shimmer(im, leafpix, rng_seed, f, N, hi, lo, frac=.18):
    rng = random.Random(rng_seed)
    for (x, y, c) in leafpix:
        r = rng.random()
        if r < frac:
            off = rng.random(); k = rng.choice([1, 2])
            a = math.sin(TAU * (k * f / N + off))
            if a > .6:
                px(im, x, y, hi)
            elif a < -.6:
                px(im, x, y, lo)

def pine(im, x, yb, h, col=C('27382f'), hi=C('3b5440')):
    for k in range(h):
        wdt = int(k * .42) + 1
        y = yb - h + k
        for dx in range(-wdt, wdt + 1):
            px(im, x + dx, y, hi if (dx < -wdt + 1 and k % 2 == 0) else col)
    px(im, x, yb + 1, BROWN_D)

def cabin(im, x, y, w, h, roof=5, wall=BROWN, wall_l=BROWN_L, wall_d=BROWN_D, roofc=C('4a3430'),
          roof_hi=C('6b4a3a'), door=None, win=None, glow=LAMP, chimney=None, gable=True):
    """front elevation log cabin; (x,y) top-left of wall"""
    for yy in range(y, y + h):
        r = (yy - y)
        c = wall_l if r % 3 == 0 else (wall_d if r % 3 == 2 else wall)
        hline(im, yy, x, x + w - 1, c)
        for ex in (x, x + 1, x + w - 2, x + w - 1):  # notched corners
            if r % 3 == 1:
                px(im, ex, yy, wall_d if ex in (x + 1, x + w - 2) else wall_l)
    if gable:
        for k in range(roof):
            hline(im, y - 1 - k, x - 2 + k, x + w + 1 - k, roofc if k < roof - 1 else roof_hi)
            px(im, x - 2 + k, y - 1 - k, roof_hi)
        # gable logs under roof
        for k in range(roof):
            pass
    else:
        for k in range(3):
            hline(im, y - 1 - k, x - 2, x + w + 1, roofc if k else roof_hi)
    if chimney:
        cx, ch = chimney
        rect(im, cx, y - roof - ch, cx + 2, y - roof + 1, C('6a5a58'))
        px(im, cx, y - roof - ch, C('8a7a74'))
    if door:
        dx, dw, dh = door
        rect(im, dx, y + h - dh, dx + dw - 1, y + h - 1, C('2a1c1a'))
        px(im, dx + dw - 2, y + h - dh // 2, LAMP_D)
    if win:
        wx, wy, ww, wh = win
        rect(im, wx - 1, wy - 1, wx + ww, wy + wh, wall_d)
        rect(im, wx, wy, wx + ww - 1, wy + wh - 1, glow)
        px(im, wx + ww // 2, wy, wall_d) if ww > 2 else None
        for yy in range(wy, wy + wh):
            px(im, wx + ww // 2, yy, wall_d)

def fence(im, x0, x1, y, h, col=BROWN_D, hi=BROWN, step=6, rails=2):
    for x in range(x0, x1 + 1, step):
        rect(im, x, y - h, x, y, col)
    for r in range(rails):
        yy = y - h + 1 + r * max(2, h // rails)
        hline(im, yy, x0, x1, hi if r == 0 else col)

_HORSE = [  # (row, c0, c1, ch) facing right, 22x16, ground row 15
    (0, 17, 18, 'd'), (1, 16, 18, 'b'), (2, 16, 20, 'b'), (3, 16, 21, 'b'), (4, 19, 21, 'd'),
    (2, 13, 15, 'b'), (3, 13, 15, 'b'), (4, 12, 18, 'b'), (5, 11, 16, 'b'), (6, 10, 15, 'b'),
    (7, 3, 14, 'b'), (8, 2, 15, 'b'), (9, 2, 15, 'b'), (10, 3, 14, 'b'), (11, 4, 13, 'd'),
    (7, 5, 12, 'l'), (8, 6, 10, 'l'),
    (12, 13, 14, 'b'), (13, 13, 14, 'b'), (14, 14, 14, 'b'), (15, 14, 14, 'd'),
    (12, 10, 11, 'd'), (13, 10, 11, 'd'), (14, 10, 10, 'd'), (15, 10, 10, 'k'),
    (12, 4, 5, 'b'), (13, 4, 5, 'b'), (14, 4, 4, 'b'), (15, 4, 4, 'd'),
    (12, 7, 8, 'd'), (13, 7, 8, 'd'), (14, 8, 8, 'd'), (15, 8, 8, 'k'),
]
def horse(im, x, y, body, dark, light, tail=0, ear=0, facing=1, mane=None):
    """x,y = anchor (centre, ground).  22x16 sprite facing +1 (right) or -1."""
    mane = mane or dark
    cols = {'b': body, 'd': dark, 'l': light, 'k': INK, 'm': mane}
    def put(r, c, ch):
        px(im, x + (c - 11) * facing, y - 15 + r, cols[ch])
    for (r, c0, c1, ch) in _HORSE:
        for c in range(c0, c1 + 1):
            put(r, c, ch)
    put(2 - 0, 18, 'k')
    put(4, 21, 'k')
    if ear:
        put(0, 17, 'b'); put(0, 18, 'b'); put(0 - 1, 18, 'd')
    else:
        put(0, 18, 'd'); put(0, 17, 'd')
    for r in range(3, 8):             # mane along neck crest
        put(r, 12 - (r - 3) // 1 if r < 6 else 8, 'm')
    for k in range(7):                # tail with swish (tail in 0..2)
        c = 1 if k < 3 else 0
        c -= 0 if k < 3 else (tail - 1) * (1 + (k - 3) // 2)
        put(7 + k, c, 'm')
    put(7, 2, 'm')

SCENES = {}
def scene(name, n, kind='gif'):
    def deco(fn):
        SCENES[name] = (kind, n, fn)
        return fn
    return deco

_BG = {}
def bgcache(name, builder):
    if name not in _BG:
        im = Image.new('RGB', (W, H), (0, 0, 0))
        builder(im)
        _BG[name] = im
    return _BG[name].copy()

# ------------------------------------------------------------- output
def _save(path, im, **kw):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, **kw)

def write_gif(name, frames, delay=150, ncol=48):
    mont = Image.new('RGB', (W, H * len(frames)))
    for i, fr in enumerate(frames):
        mont.paste(fr, (0, i * H))
    pal = mont.quantize(ncol, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    q = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
    rgb = [x.convert('RGB') for x in q]
    for scale, outdir in ((1, OUT1),):
        if scale == 1:
            seq = q
        else:
            seq = [r.resize((W * 2, H * 2), Image.NEAREST).quantize(palette=pal, dither=Image.Dither.NONE) for r in rgb]
        _save(os.path.join(outdir, name + '.gif'), seq[0], save_all=True, append_images=seq[1:],
              duration=delay, loop=0, optimize=True, disposal=1)
    return rgb

def write_png(name, im):
    im = im.convert('RGB')
    q = im.quantize(128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    _save(os.path.join(OUT1, name + '.png'), q, optimize=True)
    return q.convert('RGB')

def shade_ellipse(im, cx, cy, rx, ry, dark, k=.3, a=.8, ymin=0, ymax=H):
    for y in range(max(ymin, int(cy - ry)), min(ymax, int(cy + ry) + 1)):
        for x in range(int(cx - rx), int(cx + rx) + 1):
            d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if d <= 1 and 0 <= x < W and (a * (1 - d * .6)) > bay(x, y):
                px(im, x, y, mix(im.getpixel((x, y)), dark, k))

def static_stars(im, rng, n, ymax, ymin=0, xmin=0, xmax=W):
    for _ in range(n):
        x, y = rng.randrange(xmin, xmax), rng.randrange(ymin, ymax)
        px(im, x, y, S_DIM if rng.random() < .75 else S_MID)

# =============================================================== TITLE
@scene('title', 12)
def title(f, N):
    def build(im):
        rng = random.Random(11)
        vgrad(im, 0, 47, [C('0e0d2a'), C('201c46'), C('3d3162'), C('7a4a6e'), C('d0786a'), C('f4b86a')], 12)
        # milky way
        for y in range(0, 40):
            for x in range(W):
                d = abs((y - 0.52 * (x - 4)) - 2) / 9.0
                if d < 1:
                    dpx(im, x, y, C('7a70a8'), .38 * (1 - d) ** 1.3 * (1 - y / 48))
        static_stars(im, rng, 70, 36)
        ridge(im, 41, 15, C('4e4070'), rng, 13, 3, hi=C('9b82a0'), jag=.6)
        ridge(im, 45, 8, C('393058'), rng, 9, 3, jag=.4)
        ridge(im, 48, 5, C('2a2848'), rng, 7, 2)
        vgrad(im, 49, H, [C('6a5262'), C('4c4558'), C('363c48'), C('2a313c')], 6)
        for _ in range(120):
            x, y = rng.randrange(W), rng.randrange(50, H)
            px(im, x, y, C('3a3a45') if rng.random() < .5 else C('171c24'))
        cx = lambda y: 47 + 11 * math.sin((y - 48) * .14) - (y - 48) * .5
        wd = lambda y: 2 + (y - 48) * 1.5
        def col(x, y):
            t = (y - 48) / 24
            base = mix(C('d4826e'), C('2c3260'), min(1, t * 1.2 + .1))
            base = mix(base, C('2e3a64'), .25) if (y + x // 5) % 3 == 0 and t > .2 else base
            return base
        ext = water_rows(im, 48, H, lambda y: cx(y) - wd(y) / 2, lambda y: cx(y) + wd(y) / 2, col)
        _BG['title_ext'] = ext
        rr = random.Random(5)
        for (xb, yb, h, w, l) in ((70, 66, 22, 18, -2), (82, 60, 16, 12, 1), (10, 68, 20, 14, 2), (60, 55, 9, 8, 0), (22, 56, 8, 8, 0)):
            cottonwood(im, xb, yb, h, w, rr, trunk=C('151a22'),
                       cols=(C('12161f'), C('1c2430'), C('2b3844')), lean=l)
        px(im, 59, 56, LAMP); px(im, 59, 57, LAMP_D)
        _BG['title_streaks'] = make_streaks(ext, random.Random(3), 12, .8, (1, 2), (2, 4))
        _BG['title_sp'] = pick_sparkles(ext, random.Random(4), 14)
        _BG['title_stars'] = make_stars(random.Random(12), 22, 34)
    im = bgcache('title', build)
    draw_stars(im, _BG['title_stars'], f, N)
    cloud(im, 30 + int(round(3 * math.sin(TAU * f / N))), 15, 36, 3, 4,
          (C('8d7ab0'), C('5d4e84'), C('473b6c')), alpha=.75)
    cloud(im, 72 - int(round(2 * math.sin(TAU * f / N))), 24, 22, 2, 9,
          (C('b07a92'), C('7a5478'), C('5d4670')), alpha=.55)
    draw_streaks(im, _BG['title_streaks'], N, f, (C('a9707a'), C('f6c88a')))
    sparkle(im, _BG['title_sp'], f, N, (C('8a7a9a'), C('ffe2a0')))
    return im

# =============================================================== LAPORTE
@scene('laporte', 12)
def laporte(f, N):
    def build(im):
        rng = random.Random(21)
        vgrad(im, 0, 46, [C('231f4c'), C('4a3566'), C('a85a6a'), C('ee9c5c'), C('f8d58c')], 10)
        static_stars(im, rng, 18, 18)
        ridge(im, 41, 13, C('4a3a64'), rng, 12, 3, jag=.5)
        ridge(im, 45, 6, C('332c52'), rng, 8, 3)
        # far bank
        vgrad(im, 46, 56, [C('50455a'), C('3c3a46'), C('2e3434')], 4)
        for _ in range(60):
            px(im, rng.randrange(W), rng.randrange(47, 56), C('5a5a46'))
        # river
        ext = water_rows(im, 56, 67, lambda y: 0, lambda y: W - 1,
                         lambda x, y: mix(C('b46a68'), C('252a52'), (y - 56) / 10 + (.12 if (x // 6 + y) % 2 else 0)))
        _BG['lp_ext'] = ext
        # ford gravel bar
        for y in range(57, 67):
            for x in range(10, 38):
                if abs(x - 24) + (y - 57) * .4 < 15 and (x + y) % 3:
                    px(im, x, y, mix(C('8a6a60'), C('3c3a50'), .45 if (x + y) % 2 else .25))
        # near bank
        vgrad(im, 67, H, [C('2a2830'), C('1a1a22')], 3)
        for x in range(0, W, 3):
            px(im, x, 67, C('3a3438'))
        # corral fence, far bank
        fence(im, 3, 34, 55, 5, col=C('2a2024'), hi=C('4a3436'))
        # lean-to porch
        rect(im, 44, 43, 45, 54, C('2e2022')); rect(im, 54, 43, 55, 54, C('2e2022'))
        for k in range(4):
            hline(im, 40 + k, 42, 56, C('3a2a2c') if k else C('5a3a36'))
        # main cabin
        cabin(im, 56, 36, 24, 18, roof=7, wall=C('6a4636'), wall_l=C('8a5c3e'), wall_d=C('3e2a2a'),
              roofc=C('3a2830'), roof_hi=C('7a5040'), door=(68, 5, 11), chimney=(73, 6))
        rect(im, 67, 44, 73, 53, C('8a4a24'))  # lit door area
        rect(im, 68, 45, 72, 53, LAMP_D)
        # barrels / crates
        for bx in (82, 86):
            rect(im, bx, 50, bx + 3, 54, C('5a3a30')); hline(im, 51, bx, bx + 3, C('2a1c1c')); hline(im, 53, bx, bx + 3, C('2a1c1c'))
        rect(im, 88, 47, 93, 54, C('6a4a38')); line(im, 88, 47, 93, 54, C('3e2a2a'))
        rr = random.Random(5)
        cottonwood(im, 92, 56, 24, 16, rr, trunk=C('151a22'), cols=(C('141822'), C('1e2630'), C('2c3844')), lean=-2)
        _BG['lp_streaks'] = make_streaks(ext, random.Random(3), 12, .9, (1, 2), (2, 5))
        _BG['lp_stars'] = make_stars(random.Random(7), 6, 20)
    im = bgcache('laporte', build)
    fl = [0, 1, 0, 2, 1, 0, 1, 2, 0, 1, 2, 1][f % 12]
    draw_stars(im, _BG['lp_stars'], f, N)
    # lit window
    gc = (LAMP, LAMP_L, LAMP_D)[fl]
    rect(im, 59, 41, 63, 45, C('3e2a2a'))
    rect(im, 60, 42, 62, 44, gc)
    px(im, 61, 42, C('3e2a2a')); px(im, 61, 43, C('3e2a2a')); px(im, 61, 44, C('3e2a2a'))
    # window light spill + reflection
    sp = (.34, .5, .26)[fl]
    for y in range(46, 55):
        for x in range(55, 68):
            d = ((x - 61) ** 2 / 36 + (y - 44) ** 2 / 30)
            if d < 1:
                dpx(im, x, y, C('d98a48'), sp * (1 - d))
    for y in range(57, 66):
        for x in range(59, 64):
            if (x + y * 2 + f) % 4 < 1 + fl:
                dpx(im, x, y, LAMP, .55 - (y - 57) * .04)
    draw_streaks(im, _BG['lp_streaks'], N, f, (C('a0607a'), C('f4b87a')))
    smoke(im, 74, 29, f, N, 6, 22, 7, C('b9a8b8'), .75, seed=3, r0=1, r1=3)
    return im

# =============================================================== CANYON MOUTH
@scene('canyon_mouth', 12)
def canyon(f, N):
    def build(im):
        rng = random.Random(31)
        vgrad(im, 0, 44, [C('7a9bc0'), C('b4c6d0'), C('f0dfb4')], 8)
        # distant back ridge visible through gap
        ridge(im, 31, 7, C('9a9fb0'), rng, 10, 3, hi=C('c9ccd0'))
        ridge(im, 35, 4, C('7f8498'), rng, 8, 2)
        # plain (drawn first so cliff feet can dither into it)
        vgrad(im, 38, H, [C('b8a068'), C('8e8a52'), C('6b7448'), C('4d5c3e')], 7)
        for _ in range(90):
            px(im, rng.randrange(W), rng.randrange(44, H), C('a8924e') if rng.random() < .5 else C('4a583a'))
        # cliff masses: slanted strata, lit rim, crevices, talus foot
        nl = noise(rng, 60, 9, 3); nr = noise(rng, 60, 9, 3)
        cr = [rng.random() for _ in range(W)]
        for side in (0, 1):
            for y in range(0, 58):
                if side == 0:
                    edge = int(17 + y * .42 + 14 * (nl[y] - .5)); xs = range(0, edge)
                else:
                    edge = int(78 - y * .42 + 14 * (nr[y] - .5)); xs = range(edge, W)
                for x in xs:
                    dist = abs(x - edge)
                    v = int(y * .30 + 2.2 * math.sin(x * .17 + side) + x * (.09 if side == 0 else -.09)) % 4
                    c = (ROCK_D, ROCK, ROCK_L, ROCK)[v]
                    if side == 0 and dist < 6:      # sun from the right-front: inner faces lit
                        c = mix(c, C('d6b48a'), .35 if (x + y) % 2 else .0)
                    if side == 1 and dist < 7:
                        c = mix(c, ROCK_D, .5 if (x + y) % 2 else .0)
                    if cr[x] < .08 and (y + x) % 3:
                        c = ROCK_D
                    if dist <= 1:
                        c = C('d9b88a') if side == 0 else ROCK_D
                    if y >= 46 and bay(x, y) > (58 - y) / 12.0:
                        continue
                    px(im, x, y, c)
        for _ in range(30):
            px(im, rng.randrange(0, 34), rng.randrange(48, 58), ROCK_L)
        # river
        cx = lambda y: 48 - (y - 38) * .05 + 3 * math.sin((y - 38) * .2)
        wd = lambda y: 6 + (y - 38) * .95
        def col(x, y):
            t = (y - 38) / 34
            return mix(WATER, WATER_D, t * .8) if (x + y) % 4 else mix(WATER_L, WATER, t)
        ext = water_rows(im, 38, H, lambda y: cx(y) - wd(y) / 2, lambda y: cx(y) + wd(y) / 2, col)
        # rock edge near the bank
        for (y, xl, xr) in ext:
            px(im, xl - 1, y, ROCK_D); px(im, xr + 1, y, ROCK_D)
        _BG['cm_ext'] = ext
        _BG['cm_st'] = make_streaks(ext, random.Random(3), 12, .95, (2, 4), (2, 5))
        _BG['cm_sp'] = pick_sparkles(ext, random.Random(4), 26)
        # boulders in river
        for (bx, by, r) in ((44, 52, 2), (52, 58, 3), (42, 64, 3), (58, 66, 2)):
            disc(im, bx, by, r + 1, r, ROCK); px(im, bx - 1, by - 1, ROCK_L); disc(im, bx + 1, by + 1, 1, 1, ROCK_D)
        _BG['cm_gr'] = make_blades(random.Random(8), 80, 0, 24, 54, H, 2, 4) + make_blades(random.Random(9), 70, 70, W, 52, H, 2, 4)
    im = bgcache('canyon', build)
    draw_streaks(im, _BG['cm_st'], N, f, (C('d6e4e0'), FOAM))
    sparkle(im, _BG['cm_sp'], f, N, (WATER_L, FOAM))
    draw_grass(im, _BG['cm_gr'], f, N, (C('8e8a4a'), C('a69c58'), C('c8b872')), amp=1.4)
    # hawk, wraps once per loop
    hx = (f * 8 + 10) % 96
    hy = 13 + int(round(2 * math.sin(TAU * f / N)))
    tl = int(round(math.sin(TAU * f / N * 2)))
    for dx, dy in ((0, 0), (-1, 0), (1, 0), (-2, 0), (2, 0)):
        px(im, hx + dx, hy + dy, C('3a2c30'))
    px(im, hx - 3, hy - 1 + (tl > 0), C('3a2c30')); px(im, hx + 3, hy - 1 + (tl < 0), C('3a2c30'))
    px(im, hx - 1, hy + 1, C('7a5a50')); px(im, hx + 1, hy + 1, C('7a5a50'))
    return im

# =============================================================== COUNCIL TREE
@scene('council_tree', 12)
def council_tree(f, N):
    def build(im):
        rng = random.Random(41)
        vgrad(im, 0, 44, [C('6d8fb5'), C('b6c8d0'), C('f3e2b8')], 8)
        ridge(im, 36, 6, C('9aa0b0'), rng, 12, 3)
        ridge(im, 40, 5, C('7c8a82'), rng, 9, 3)
        vgrad(im, 41, H, [C('9aa060'), C('7a8c4e'), C('5d7040'), C('435a38')], 8)
        for _ in range(140):
            px(im, rng.randrange(W), rng.randrange(44, H), C('b8aa62') if rng.random() < .5 else C('3e5234'))
        # river bend (lower right)
        xl = lambda y: 74 - (y - 50) * 1.9
        ext = water_rows(im, 50, H, xl, lambda y: W - 1,
                         lambda x, y: mix(C('7ea6b8'), C('3d5c7a'), (y - 50) / 22 + (.12 if (x + y) % 3 == 0 else 0)))
        for (y, a, b) in ext:
            px(im, a - 1, y, C('d8c488')); px(im, a - 2, y, C('b4a460')) if y % 2 else None
        _BG['ct_st'] = make_streaks(ext, random.Random(3), 12, .8, (1, 2), (2, 4))
        _BG['ct_sp'] = pick_sparkles(ext, random.Random(4), 12)
        shade_ellipse(im, 36, 61, 30, 7, C('2a3a38'), .5, .9, 52, 72)
        rr = random.Random(7)
        _BG['ct_leaves'] = cottonwood(im, 34, 62, 56, 66, rr, trunk=C('4a352c'),
                                      cols=(C('37533a'), C('66863e'), C('a9bf62')), lean=1, n_blobs=26)
        # trunk detail: roots + bark highlights
        for y in range(40, 62):
            for x in range(30, 40):
                if im.getpixel((x, y)) in (C('4a352c'),) and (y + x) % 5 == 0:
                    px(im, x, y, C('7a5a42'))
        for dx in (-5, -4, 5, 6):
            px(im, 34 + dx, 62, C('4a352c')); px(im, 34 + dx // 2, 61, C('4a352c'))
        _BG['ct_gr'] = make_blades(random.Random(8), 150, 0, W, 58, H, 2, 5)
    im = bgcache('council', build)
    cloud(im, 78 + int(round(3 * math.sin(TAU * f / N))), 12, 26, 3, 5, (C('fdf6e4'), C('e6e4dc'), C('c4ccd4')))
    shimmer(im, _BG['ct_leaves'], 77, f, N, C('d4e48a'), C('2a4030'), .3)
    draw_streaks(im, _BG['ct_st'], N, f, (C('a8c8d0'), FOAM))
    sparkle(im, _BG['ct_sp'], f, N, (C('b8d4dc'), FOAM))
    sh = int(round(6 * math.sin(TAU * f / N)))
    shade_ellipse(im, 66 + sh, 56, 18, 5, C('243a3c'), .45, .7, 44, H)
    draw_grass(im, _BG['ct_gr'], f, N, (C('7e964a'), C('a4b45c'), C('d2d08a')), amp=1.3)
    rng = random.Random(6)
    for i in range(8):
        a = age(f, N, i, 8)
        x0 = 20 + rng.random() * 40; y0 = 16 + rng.random() * 18
        x = x0 + a * 36 + 2 * math.sin(TAU * (a * 2 + rng.random()))
        y = y0 + a * 18
        if a > .06 and a < .94:
            px(im, int(x), int(y), FOAM)
            if a > .4 and rng.random() < .6:
                px(im, int(x) + 1, int(y), C('d8dcd4'))
    return im

# =============================================================== VIRGINIA DALE
@scene('virginia_dale', 12)
def virginia_dale(f, N):
    def build(im):
        rng = random.Random(51)
        vgrad(im, 0, 44, [C('46467a'), C('a86a88'), C('f0a46a'), C('f8d890')], 10)
        static_stars(im, rng, 8, 10)
        ridge(im, 38, 11, C('6a5478'), rng, 12, 3, jag=.6)
        tops = ridge(im, 42, 5, C('4a4262'), rng, 8, 3)
        for x in range(2, W, 5):
            pine(im, x + rng.randint(-1, 1), tops[x] + 3, rng.randint(6, 9), C('2c3040'), C('3e4658'))
        vgrad(im, 45, H, [C('8a6a58'), C('6f5c4a'), C('564c40')], 5)
        for _ in range(90):
            px(im, rng.randrange(W), rng.randrange(46, 56), C('9a7a58') if rng.random() < .5 else C('4a5040'))
        # stage road across foreground
        for y in range(57, 72):
            for x in range(W):
                c = mix(C('d2b484'), C('b49468'), (y - 57) / 15 + (.0 if bay(x, y) > .5 else .0))
                px(im, x, y, c)
        for x in range(W):
            if (x // 3) % 2 == 0:
                px(im, x, 63, C('a88860'))
            if (x // 4) % 2 == 1:
                px(im, x, 68, C('9a7c58'))
        for x in range(0, W, 2):
            px(im, x, 57, C('9c8058'))
        # station building: long low cabin with porch
        cabin(im, 5, 38, 34, 12, roof=4, wall=C('7a5238'), wall_l=C('9a6a44'), wall_d=C('4a3028'),
              roofc=C('4a3030'), roof_hi=C('8a5a44'), door=(21, 6, 9), win=(9, 42, 4, 4), glow=C('f6c070'),
              chimney=(30, 5), gable=False)
        rect(im, 28, 41, 31, 44, C('4a3028')); rect(im, 29, 42, 30, 43, C('f6c070'))
        # porch roof & posts
        for k in range(3):
            hline(im, 36 + k, 3, 41, C('3a2628') if k else C('7a5040'))
        for px_ in (4, 40):
            rect(im, px_, 38, px_, 50, C('3a2628'))
        # corral
        fence(im, 46, 93, 55, 8, col=C('3a2a2a'), hi=C('6a4a3a'), step=7, rails=3)
        rr = random.Random(3)
        cottonwood(im, 92, 52, 20, 14, rr, trunk=C('2a2024'), cols=(C('2a3a38'), C('3c5040'), C('66784a')), lean=-1)
        for (x, y) in ((8, 54), (42, 53)):
            for dx in range(-2, 3):
                px(im, x + dx, y, C('4a3a2c'))
    im = bgcache('virginia', build)
    # lantern on porch post
    fl = [0, 1, 0, 1, 2, 1, 0, 2, 1, 0, 1, 2][f % 12]
    rect(im, 41, 40, 43, 43, C('2a1c1c'))
    rect(im, 42, 41, 42, 42, (LAMP_D, LAMP, LAMP_L)[fl])
    px(im, 42, 39, C('2a1c1c'))
    for y in range(34, 52):
        for x in range(34, 52):
            d = ((x - 42) ** 2 + (y - 42) ** 2) / 70.0
            if d < 1:
                dpx(im, x, y, C('f0b060'), (.25, .38, .5)[fl] * (1 - d))
    # horses
    sw = [0, 0, 1, 2, 2, 1, 0, 0, 1, 2, 1, 0][f % 12]
    ear = 1 if f in (3, 4) else 0
    ear2 = 1 if f in (9,) else 0
    horse(im, 60, 55, C('7a4a30'), C('4a2c26'), C('9a6a44'), tail=sw, ear=ear, facing=-1, mane=C('2a1c1c'))
    horse(im, 83, 55, C('9a9088'), C('625c5a'), C('c4bcae'), tail=(2 - sw) % 3, ear=ear2, facing=-1, mane=C('5a5450'))
    # dust blowing along road
    rng = random.Random(17)
    for i in range(10):
        a = age(f, N, i, 10)
        y = 58 + int(rng.random() * 12) + int(round(math.sin(TAU * (a * 2 + rng.random()))))
        x = rng.random() * 96 + a * 48
        x = int(x) % 96
        al = math.sin(math.pi * a) * .85
        for k in range(rng.choice([2, 3, 4])):
            dpx(im, (x + k) % 96, y, C('e8d4a8'), al)
            if k == 1:
                dpx(im, (x + k) % 96, y - 1, C('e8d4a8'), al * .6)
    return im

# =============================================================== MASON CLAIM
@scene('mason_claim', 12)
def mason_claim(f, N):
    def build(im):
        rng = random.Random(61)
        vgrad(im, 0, 40, [C('86a4c6'), C('d0d6ca'), C('f6e6b4')], 8)
        ridge(im, 33, 9, C('8d94ac'), rng, 12, 3, hi=C('dfe0e6'))
        ridge(im, 38, 4, C('6e7e78'), rng, 8, 3)
        vgrad(im, 37, H, [C('c0ac6c'), C('a49a54'), C('848a4a'), C('62703f')], 8)
        for _ in range(60):
            px(im, rng.randrange(W), rng.randrange(40, H), C('c8b872') if rng.random() < .5 else C('6a7a42'))
        # far wagon road
        for x in range(W):
            px(im, x, 41, C('d8c28a'))
        # cabin frame, left
        wood, wood_d = C('e2c488'), C('a47e4c')
        x0, y0, x1, y1 = 8, 40, 36, 58
        shade_ellipse(im, 24, 59, 20, 3, C('4a5a38'), .5, .8, 54, 66)
        for x in range(x0, x1 + 1, 4):
            rect(im, x, y0, x, y1, wood if x % 8 else wood_d)
        hline(im, y0, x0, x1, wood); hline(im, y0 + 1, x0, x1, wood_d)
        hline(im, y1, x0, x1, wood_d)
        for yy in range(y1 - 5, y1):
            hline(im, yy, x0, x0 + 14, wood if yy % 2 else wood_d)   # planked part
        # roof rafters
        mx = (x0 + x1) // 2
        line(im, x0 - 1, y0, mx, y0 - 8, wood); line(im, x1 + 1, y0, mx, y0 - 8, wood_d)
        line(im, x0 + 4, y0, mx, y0 - 5, wood_d); line(im, x1 - 4, y0, mx, y0 - 5, wood_d)
        rect(im, mx, y0 - 8, mx, y0, wood)
        # sawhorse + lumber pile
        for k in range(4):
            hline(im, 60 + k % 2 - 0, 38, 50, wood if k % 2 else wood_d)
            hline(im, 60 - k, 40, 49, wood if k % 2 else wood_d) if False else None
        rect(im, 40, 56, 49, 59, wood_d); hline(im, 56, 40, 49, wood); hline(im, 58, 40, 49, wood)
        # surveyor stakes
        stakes = []
        for i, t in enumerate((0, .2, .38, .54, .68, .8, .9)):
            sx = 30 + t * 62; sy = 71 - t * 29
            h = int(14 - t * 9)
            stakes.append((int(sx), int(sy), h))
            for k in range(h):
                px(im, int(sx), int(sy) - k, C('7a5632') if k % 3 else C('4e3626'))
                if t < .45: px(im, int(sx) + 1, int(sy) - k, C('4e3626'))
            if t < .6:
                px(im, int(sx) + 1, int(sy) - 1, wood_d)
        _BG['mc_st'] = stakes
        _BG['mc_gr'] = make_blades(random.Random(8), 55, 0, W, 56, H, 2, 3)
    im = bgcache('mason', build)
    st = _BG['mc_st']
    # string line
    for a, b in zip(st, st[1:]):
        xa, ya = a[0], a[1] - a[2] + 3
        xb, yb = b[0], b[1] - b[2] + 3
        steps = max(abs(xb - xa), 1)
        for s_ in range(steps):
            x = xa + (xb - xa) * s_ // steps
            y = ya + (yb - ya) * s_ // steps + (1 if 0 < s_ < steps - 1 and steps > 8 and abs(s_ - steps / 2) < 2 else 0)
            dpx(im, x, y, C('f4ead0'), .8)
    # fluttering flags
    for i, (sx, sy, h) in enumerate(st):
        top = sy - h
        ln = max(3, 7 - i)
        ph = TAU * f / N + i * .9
        for c in range(ln):
            off = int(round(1.4 * math.sin(ph - c * 1.2) * (c / max(1, ln - 1))))
            col = C('c0452f') if c % 3 != 2 else C('e8cfa8')
            px(im, sx + 1 + c, top + off, col)
            if i < 5:
                px(im, sx + 1 + c, top + 1 + off, C('9a3426') if c % 3 != 2 else C('c8b088'))
            if i < 3:
                px(im, sx + 1 + c, top + 2 + off, C('9a3426') if c % 3 != 2 else C('c8b088'))
    draw_grass(im, _BG['mc_gr'], f, N, (C('7e8a48'), C('a49c58'), C('d0bc78')), amp=1.2)
    # distant wagon train, clipped between sage clumps
    for k in range(-1, 4):
        wx = 46 + k * 12 + f
        if 44 <= wx <= 74:
            rect(im, wx, 38, wx + 4, 40, C('f0e6cc'))
            hline(im, 38, wx + 1, wx + 3, C('d8ccaa'))
            px(im, wx, 41, C('3a2c2c')); px(im, wx + 4, 41, C('3a2c2c'))
    for bx in (43, 76):
        disc(im, bx, 41, 3, 2, C('6a7a48')); px(im, bx - 1, 40, C('8a9a58'))
    # drifting dust
    rng = random.Random(16)
    for i in range(8):
        a = age(f, N, i, 8)
        y = 44 + int(rng.random() * 22)
        x = int(rng.random() * 96 + a * 40) % 96
        al = math.sin(math.pi * a) * .7
        for k in range(3):
            dpx(im, (x + k) % 96, y, C('eee0b4'), al)
    return im

# =============================================================== FORT COLLINS
@scene('fort_collins', 12)
def fort_collins(f, N):
    def build(im):
        rng = random.Random(71)
        vgrad(im, 0, 44, [C('3a4678'), C('8a6488'), C('ee946a'), C('fbd88a')], 10)
        static_stars(im, rng, 14, 12)
        # sun + halo
        for y in range(18, 48):
            for x in range(10, 52):
                d = ((x - 30) ** 2 + (y - 40) ** 2) ** .5
                if 5 < d < 20:
                    dpx(im, x, y, C('fbd890'), .5 * (1 - (d - 5) / 15))
        disc(im, 30, 40, 5, 5, C('fff4c8'))
        ridge(im, 40, 7, C('5a4a74'), rng, 11, 3, jag=.4)
        # river lower left
        xr = lambda y: 62 - (y - 46) * .8
        ext = water_rows(im, 46, H, lambda y: 0, xr,
                         lambda x, y: mix(C('f2b070'), C('3a4272'), (y - 46) / 24 + (.1 if (x + y) % 3 == 0 else 0)))
        _BG['fc_st'] = make_streaks(ext, random.Random(3), 12, .8, (1, 2), (2, 4))
        # glint column
        pts = []
        gr = random.Random(5)
        for (y, a, b) in ext:
            for _ in range(2):
                pts.append((30 + int(gr.gauss(0, 2 + (y - 46) * .25)), y, gr.random(), gr.choice([1, 2])))
        _BG['fc_sp'] = [p for p in pts if p[0] < xr(p[1])]
        # hill with tents (right)
        hl = lambda x: 44 - (x - 44) * .22 if x < 70 else 38 - (x - 70) * .05
        for x in range(40, W):
            top = int(hl(x))
            for y in range(top, H):
                if y < int(xr(y)) and x < xr(y) and y > 46:
                    continue
                c = mix(C('4e5a46'), C('2a3a38'), (y - top) / 24)
                if y == top or y == top + 1 and x % 2:
                    c = C('d09a60')
                px(im, x, y, c)
        for _ in range(70):
            x, y = rng.randrange(44, W), rng.randrange(46, H)
            if x > xr(y) + 2:
                px(im, x, y, C('687a52') if rng.random() < .5 else C('2e3a30'))
        for y in range(47, H):
            px(im, int(xr(y)) + 1, y, C('d8a870'))
        # tents
        def tent(cx, yb, w, h):
            for k in range(h):
                half = int(w / 2 * (1 - k / h))
                for dx in range(-half, half + 1):
                    c = C('f0dcb0') if dx < 0 else C('b48a72')
                    if dx == 0:
                        c = C('7a5a52')
                    px(im, cx + dx, yb - k, c)
            for k in range(h // 2):
                px(im, cx + 1, yb - k, C('2a2022'))
                px(im, cx + 2, yb - k, C('2a2022')) if k < h // 3 else None
            px(im, cx, yb - h, C('3a2c2c'))
        tent(60, 54, 16, 11); tent(76, 51, 14, 10); tent(88, 59, 14, 9); tent(50, 62, 12, 8)
        # flagpole
        rect(im, 68, 24, 68, 44, C('e0d0b0')); px(im, 68, 23, C('f8e8a0'))
        # fence-line of surveyed stakes
        for x in range(46, 96, 8):
            y = int(hl(x)) + 11
            rect(im, x, y - 3, x, y, C('c8a46a'))
        for y in range(64, 71):
            px(im, 94, y, C('c8a46a')) if False else None
    im = bgcache('fort', build)
    draw_streaks(im, _BG['fc_st'], N, f, (C('c8886e'), C('ffe4a0')))
    sparkle(im, _BG['fc_sp'], f, N, (C('f8c880'), C('fff6d0')))
    # flag
    for c in range(9):
        amp = 1.5 * (c / 8)
        off = int(round(amp * math.sin(TAU * f / N - c * .85)))
        for r in range(5):
            if c < 4 and r < 3:
                col = C('34457a') if (c + r) % 2 else C('8a96c0')
            else:
                col = C('b03a30') if r % 2 == 0 else C('ece2cf')
            px(im, 69 + c, 24 + r + off, col)
    # birds crossing
    for (ox, oy) in ((0, 14), (-10, 19), (-17, 12)):
        bx = (f * 8 + 24 + ox) % 96
        by = oy + int(round(math.sin(TAU * f / N * 2 + ox)))
        up = (f + ox) % 2 == 0
        c = C('2e2440')
        px(im, bx, by, c)
        px(im, bx - 1, by - (1 if up else 0), c); px(im, bx + 1, by - (1 if up else 0), c)
        px(im, bx - 2, by - (2 if up else -1), c); px(im, bx + 2, by - (2 if up else -1), c)
    return im

# =============================================================== HIGH WATER
@scene('high_water', 12)
def high_water(f, N):
    def build(im):
        rng = random.Random(81)
        vgrad(im, 0, 36, [C('2c3144'), C('4f4e64'), C('807a86'), C('b8a290')], 9)
        ridge(im, 31, 5, C('4a4a5a'), rng, 10, 3)
        # drowned cottonwoods on far bank
        tops = ridge(im, 34, 3, C('2e3238'), rng, 7, 2)
        for x in (6, 22, 41, 58, 79, 90):
            cottonwood(im, x, 38, 11, 9, random.Random(x), trunk=C('1e2228'),
                       cols=(C('22302c'), C('324036'), C('4a5a42')))
        ext = water_rows(im, 36, H, lambda y: 0, lambda y: W - 1, lambda x, y: C('7e603a'))
        trng = random.Random(9)
        tex = []
        for y in range(36, 68):
            sp = 2 if y < 52 else 4
            P = sp * 12
            sc = 4 if y < 44 else (6 if y < 52 else (8 if y < 60 else 12))
            tex.append((y, sp, P, noise(trng, P, sc, 2, periodic=True)))
        _BG['hw_tex'] = tex
        _BG['hw_ext'] = ext
        _BG['hw_foam'] = make_streaks([e for e in ext if e[0] >= 44], random.Random(4), 12, .25, (4,), (4, 9))
        # foreground muddy bank
        for y in range(68, H):
            for x in range(W):
                px(im, x, y, C('3a2c28') if (x + y) % 3 else C('2a2020'))
    im = bgcache('flood', build)
    for (y, sp, P, tile) in _BG['hw_tex']:
        t = (y - 36) / 32.0
        basec = mix(C('74583a'), C('8a6a40'), t)
        for x in range(W):
            v = tile[(x - sp * f) % P]
            if v > .66:
                c = C('d8bc84') if v > .74 else C('b8935c')
            elif v < .3:
                c = C('4a3828') if v < .22 else C('5e4630')
            else:
                c = basec
            px(im, x, y, c)
    draw_streaks(im, _BG['hw_foam'], N, f, (C('c8aa74'), C('f0e2c4')), thr=(.6,))
    # storm clouds
    cl = (C('7a7482'), C('4a4658'), C('34344a'))
    cloud(im, 30 + int(round(3 * math.sin(TAU * f / N))), 17, 52, 8, 3, cl)
    cloud(im, 78 - int(round(3 * math.sin(TAU * f / N + 1))), 11, 40, 7, 8, cl)
    cloud(im, 52, 26, 30, 4, 12, (C('8a7e8a'), C('5a5668'), C('443e54')), alpha=.8)
    def wrapx(x, w):
        return [x % 96, x % 96 - 96]
    # plank
    for ox in wrapx(f * 8 + 70, 9):
        rect(im, ox - 1, 43, ox + 9, 46, C('241a18')); rect(im, ox, 44, ox + 8, 45, C('d6b27a')); hline(im, 44, ox, ox + 8, C('f0d8a0')); px(im, ox + 3, 45, C('8a6a44'))
    # log
    by = int(round(math.sin(TAU * 2 * f / N)))
    for ox in wrapx(f * 8 + 12, 15):
        y = 52 + by
        for k in range(21):
            yy = y + (1 if k > 13 else 0) + (1 if k > 7 else 0) - (1 if k < 3 else 0) * (1 if f % 2 else 0)
            px(im, ox + k, yy - 1, C('241a18'))
            px(im, ox + k, yy, C('c49a5e'))
            px(im, ox + k, yy + 1, C('7a5636'))
            px(im, ox + k, yy + 2, C('7a5636') if k % 5 == 0 else C('4a3426'))
            px(im, ox + k, yy + 3, C('4a3426'))
            px(im, ox + k, yy + 4, C('241a18'))
        rect(im, ox + 21, y + 2, ox + 21, y + 4, C('e6c88c'))
        px(im, ox + 5, y - 1, C('3e2c24')); px(im, ox + 6, y - 2, C('3e2c24'))
        # spray at the trailing edge
        sr = random.Random(f)
        for _ in range(5):
            px(im, ox + 22 + sr.randint(1, 3), y + sr.randint(0, 3), FOAM)
        px(im, ox - 1, y + 4, C('f0e2c4')); px(im, ox + 6, y + 5, C('f0e2c4')); px(im, ox + 12, y + 5, C('f0e2c4'))
    # barrel
    bb = int(round(math.sin(TAU * 2 * f / N + 2)))
    for ox in wrapx(f * 8 + 56, 6):
        y = 61 + bb
        rect(im, ox - 1, y - 1, ox + 6, y + 5, C('241a18')); rect(im, ox, y, ox + 5, y + 4, C('9a6a3c'))
        hline(im, y, ox, ox + 5, C('a87a48')); hline(im, y + 1, ox, ox + 5, C('2e2a2a'))
        hline(im, y + 3, ox, ox + 5, C('2e2a2a')); hline(im, y + 4, ox, ox + 5, C('5a3a28'))
        px(im, ox, y, C('0')) if False else None
        sr = random.Random(f + 50)
        for _ in range(4):
            px(im, ox - sr.randint(0, 2), y + 4 + sr.randint(0, 1), FOAM)
            px(im, ox + 5 + sr.randint(1, 2), y - sr.randint(0, 1), FOAM)
    # foreground reeds
    return im

# =============================================================== TRAIL ROAD
def heat_rows(im, y0, y1, ph, amp=1.4):
    src = im.copy()
    for y in range(y0, y1):
        s = int(round(amp * math.sin(ph + y * 1.4)))
        if s:
            row = src.crop((0, y, W, y + 1))
            im.paste(row.crop((W - s, 0, W, 1)) if s > 0 else row.crop((0, 0, -s, 1)), (0, y) if s > 0 else (W + s, y))
            im.paste(row.crop((0, 0, W - s, 1)) if s > 0 else row.crop((-s, 0, W, 1)), (s, y) if s > 0 else (0, y))

@scene('trail_road', 12)
def trail_road(f, N):
    def build(im):
        rng = random.Random(91)
        vgrad(im, 0, 38, [C('5f84b4'), C('9cb8cc'), C('e8dcc0')], 8)
        ridge(im, 33, 12, C('7b88a8'), rng, 14, 3, hi=C('e2e6ee'), jag=.5)
        ridge(im, 37, 5, C('62736e'), rng, 9, 3)
        vgrad(im, 36, H, [C('c8b474'), C('ad9d5c'), C('8c8c4c'), C('6f7c46')], 8)
        for _ in range(110):
            px(im, rng.randrange(W), rng.randrange(40, H), C('d0c080') if rng.random() < .5 else C('5e6c3e'))
        # road with converging ruts
        for y in range(37, H):
            t = (y - 37) / 34.0
            c0 = 47 - t * 10; hw = 2 + t * 28
            xl, xr = int(c0 - hw), int(c0 + hw)
            for x in range(xl, xr + 1):
                u = abs(x - c0) / max(hw, 1)
                c = C('dcc690') if u < .8 else C('c0a870')
                if .38 < u < .62:     # grassy centre
                    c = C('94964e') if (x + y) % 3 else C('b4aa62')
                elif .12 < u < .3 or .72 < u < .88:   # wheel ruts
                    c = C('b09468') if (x + y) % 2 else C('9c8258')
                px(im, x, y, c)
        # far lone cottonwood and fence post line
        cottonwood(im, 82, 44, 10, 8, random.Random(3), trunk=C('3a2c28'), cols=(C('3e5a3c'), C('5c7a44'), C('8aa458')))
        for x, y in ((14, 44), (8, 47), (2, 51)):
            rect(im, x, y - 4, x, y, C('4a3a30'))
        _BG['tr_gr'] = make_blades(random.Random(8), 95, 0, 25, 52, H, 2, 5) + make_blades(random.Random(9), 90, 72, W, 52, H, 2, 5)
    im = bgcache('trail', build)
    sh = int(round(7 * math.sin(TAU * f / N)))
    shade_ellipse(im, 28 + sh, 54, 22, 6, C('3a4a4a'), .4, .75, 40, H)
    draw_grass(im, _BG['tr_gr'], f, N, (C('8c9650'), C('b4ae66'), C('e0d49c')), amp=1.5)
    # dust devil
    base_y, bx = 52, 70
    for k in range(21):
        y = base_y - k
        sw = int(round(1.5 * math.sin(TAU * f / N + k * .3)))
        wdt = 1 + k * .22
        for dx in range(-int(wdt) - 1, int(wdt) + 2):
            x = bx + sw + dx
            edge = abs(dx) >= int(wdt)
            if abs(dx) <= wdt + .5 and ((x + y + (f * 2 if k % 2 else -f * 2)) % 3 != 0 or not edge):
                px(im, x, y, C('c4aa78') if edge else C('e6d4a4'))
        if k % 3 == 0:
            dpx(im, bx + sw + int(wdt) + 2, y, C('d8c494'), .5)
            dpx(im, bx + sw - int(wdt) - 2, y, C('d8c494'), .5)
    heat_rows(im, 33, 41, TAU * f / N, 1.3)
    return im

# =============================================================== NIGHT CAMP
@scene('night_camp', 12)
def night_camp(f, N):
    def build(im):
        rng = random.Random(101)
        vgrad(im, 0, 48, [C('0b0a22'), C('171538'), C('2a2452'), C('3c3162')], 9)
        for _ in range(26):
            pass
        static_stars(im, rng, 66, 40)
        ridge(im, 43, 8, C('1a1a38'), rng, 11, 3, jag=.4)
        ridge(im, 47, 5, C('141630'), rng, 8, 3)
        vgrad(im, 49, H, [C('262438'), C('1c1d2c'), C('151722')], 5)
        for _ in range(80):
            px(im, rng.randrange(W), rng.randrange(50, H), C('30304a') if rng.random() < .5 else C('101420'))
        for (x, y, h, w, l) in ((8, 52, 16, 12, 1), (58, 50, 9, 8, 0)):
            cottonwood(im, x, y, h, w, random.Random(x), trunk=C('0e1018'), cols=(C('0e1220'), C('151b2a'), C('1f2838')), lean=l)
        # wagon silhouette (right)
        wc = C('3a3454'); wl = C('8a6a6a')
        for k in range(12):
            half = int(14 * math.sqrt(max(0, 1 - ((k - 12) / 12.0) ** 2)))
        for y in range(40, 54):                    # canvas bonnet
            for x in range(60, 94):
                d = ((x - 77) / 17.0) ** 2 + ((y - 53) / 13.0) ** 2
                if d <= 1 and y <= 53:
                    px(im, x, y, wl if (x < 66 and d > .72) else (C('4c4468') if x < 70 and d > .5 else wc))
        rect(im, 60, 53, 94, 56, C('1c1a2e')); hline(im, 53, 60, 94, C('342e4a'))   # wagon bed
        for (wx, r) in ((66, 6), (86, 7)):                                         # wheels
            for a in range(0, 360, 6):
                px(im, int(wx + r * math.cos(math.radians(a))), int(58 + r * math.sin(math.radians(a))), C('141226'))
            for a in range(0, 180, 45):
                line(im, int(wx - r * math.cos(math.radians(a))), int(58 - r * math.sin(math.radians(a))),
                     int(wx + r * math.cos(math.radians(a))), int(58 + r * math.sin(math.radians(a))), C('1a1830'))
        line(im, 59, 55, 48, 60, C('141226'))   # tongue
        # fire ring, logs
        for a in range(0, 360, 40):
            sx = int(34 + 10 * math.cos(math.radians(a))); sy = int(62 + 4 * math.sin(math.radians(a)))
            disc(im, sx, sy, 1, 1, C('4a4658'))
            px(im, sx - 1, sy - 1, C('6a6478'))
        line(im, 28, 62, 40, 58, C('3a2418')); line(im, 29, 58, 40, 63, C('2e1c14'))
        _BG['nc_stars'] = make_stars(random.Random(12), 24, 38)
    im = bgcache('night', build)
    draw_stars(im, _BG['nc_stars'], f, N)
    # glow
    k = 1.0 + .22 * math.sin(TAU * f / N * 2) + .12 * math.sin(TAU * f / N * 3 + 1)
    for y in range(42, H):
        for x in range(8, 74):
            d = ((x - 35) / 26.0) ** 2 + ((y - 59) / 12.0) ** 2
            if d < 1:
                dpx(im, x, y, C('c8683a'), .52 * k * (1 - d) ** 1.2)
    # flames
    fr = random.Random(5)
    for i, (dx, hh, kk) in enumerate(((-3, 6, 2), (0, 10, 1), (3, 7, 3), (-1, 5, 4))):
        ht = hh + int(round(2 * math.sin(TAU * kk * f / N + i * 1.7)))
        sway = int(round(1.2 * math.sin(TAU * f / N + i)))
        xb = 34 + dx
        poly(im, [(xb - 3, 60), (xb + sway, 60 - ht), (xb + 3, 60)], LAMP_D)
        poly(im, [(xb - 2, 60), (xb + sway, 60 - ht + 2), (xb + 2, 60)], LAMP)
        if hh > 5:
            poly(im, [(xb - 1, 60), (xb + sway, 60 - ht + 4), (xb + 1, 60)], LAMP_L)
    sparks(im, 34, 54, f, N, 10, 26, 14, (LAMP_L, LAMP, LAMP_D), seed=4)
    return im

# =============================================================== RIVER FORD
@scene('river_ford', 12)
def river_ford(f, N):
    def build(im):
        rng = random.Random(111)
        vgrad(im, 0, 34, [C('7aa0c8'), C('b8d0dc'), C('f2ecd0')], 8)
        ridge(im, 30, 6, C('8c9cb0'), rng, 12, 3)
        vgrad(im, 30, 40, [C('86a468'), C('627e50')], 4)
        for (x, yb, h, w, l) in ((10, 40, 22, 18, 1), (30, 38, 12, 10, 0), (74, 40, 20, 16, -1), (90, 38, 14, 10, 0)):
            cottonwood(im, x, yb, h, w, random.Random(x), trunk=C('4a3a30'), cols=(C('3a5a3e'), C('5e8244'), C('98b660')), lean=l)
        xl = lambda y: 0
        def col(x, y):
            t = (y - 39) / 33
            base = mix(C('4f8aa0'), C('9cd0c4'), t)
            if (x * 7 + y * 13) % 11 == 0 and t > .3:
                base = C('d0c08a')
            return base
        ext = water_rows(im, 39, 69, lambda y: 0, lambda y: W - 1, col)
        # gravel bar: wagon crossing
        for y in range(40, 69):
            t = (y - 40) / 28.0
            cx = 70 - t * 46
            for x in range(int(cx - 5 - t * 3), int(cx + 5 + t * 3)):
                if (x + y) % 2 == 0 or (x + 2 * y) % 5 == 0:
                    px(im, x, y, mix(C('d2c08a'), C('6a9aa0'), .35 if (x + y) % 3 else .15))
            px(im, int(cx - 3 - t * 2), y, C('9c8458')); px(im, int(cx + 3 + t * 2), y, C('9c8458'))
        # banks: far shore and near sand
        for x in range(W):
            px(im, x, 39, C('c8b078') if x % 3 else C('9a8a58'))
        vgrad(im, 69, H, [C('c8b078'), C('a08a5c')], 3)
        for _ in range(40):
            px(im, random.Random(_).randrange(W), 69 + _ % 3, C('7a6a48'))
        # ruts on far bank & near bank
        line(im, 64, 39, 86, 34, C('a88a58')); line(im, 68, 39, 90, 35, C('8a7048'))
        line(im, 3, 71, 10, 68, C('8a7048')); line(im, 0, 69, 7, 67, C('a88a58'))
        _BG['rf_ext'] = ext
        _BG['rf_st'] = make_streaks(ext, random.Random(3), 12, .7, (1, 2), (2, 4))
        _BG['rf_sp'] = pick_sparkles(ext, random.Random(4), 20)
        _BG['rf_reed'] = make_blades(random.Random(8), 26, 0, 22, 62, H, 6, 12) + make_blades(random.Random(9), 22, 76, W, 60, H, 6, 12)
    im = bgcache('ford', build)
    draw_streaks(im, _BG['rf_st'], N, f, (C('78b0bc'), C('d8f0ea')))
    sparkle(im, _BG['rf_sp'], f, N, (C('a8d8d8'), FOAM))
    # ripple rings
    for i, (cx, cy) in enumerate(((20, 50), (46, 57), (62, 46), (34, 64), (80, 60))):
        a = age(f, N, i * 2, 10)
        r = 1 + a * 7
        for y in range(cy - 4, cy + 5):
            for x in range(int(cx - r * 2.2) - 1, int(cx + r * 2.2) + 2):
                d = ((x - cx) / (r * 2.0)) ** 2 + ((y - cy) / (r * .62)) ** 2
                if abs(d - 1) < .22 and 40 <= y < 69:
                    dpx(im, x, y, FOAM, .95 * math.sin(math.pi * a))
    # reeds (sway) with cattail heads
    draw_grass(im, _BG['rf_reed'], f, N, (C('5a7a3a'), C('7e9a48'), C('a4b860')), amp=1.8, wave=.3)
    for (x, y, h, r) in _BG['rf_reed']:
        if r > .8:
            s = int(round(1.8 * math.sin(TAU * f / N - x * .3 + y * .1)))
            px(im, x + s, y - h - 1, C('5a3a28')); px(im, x + s, y - h, C('5a3a28'))
    # dragonfly: lissajous loop
    dx = 56 + int(round(16 * math.sin(TAU * f / N)))
    dy = 48 + int(round(5 * math.sin(2 * TAU * f / N + 1)))
    px(im, dx, dy, C('1e8a9a')); px(im, dx - 1, dy, C('2aa0b0')); px(im, dx - 2, dy, C('1e8a9a'))
    wu = f % 2
    px(im, dx, dy - 1 - wu, C('e8f4f0')); px(im, dx - 1, dy - 1 - wu, C('c8e0e8'))
    px(im, dx, dy + 1 + wu - 1, C('e8f4f0')) if wu == 0 else None
    return im

# =============================================================== PORTRAIT TOOLKIT
def vignette(im, cx, cy, r, dark, k=.8, y0=0, y1=H):
    for y in range(y0, y1):
        for x in range(W):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** .5 / r
            if d > .6:
                dpx(im, x, y, dark, min(1, (d - .6) * k))

def torso(im, cx, top, w, col, sh, hi, sq=16.0):
    for y in range(top, H):
        hw = 9 + (w / 2 - 9) * min(1.0, ((y - top) / sq)) ** .55
        for x in range(int(cx - hw), int(cx + hw) + 1):
            u = (x - cx) / hw
            c = col
            if u > .4 or (u > .12 and bay(x, y) < (u - .12) * 2.2):
                c = sh
            elif u < -.62 and (y - top) < 14 and bay(x, y) < (-u - .62) * 2.5:
                c = hi
            px(im, x, y, c)

def headshape(im, cx, cy, rx, ry, skin, sh, hi, jaw=2.6, lit=-1):
    for y in range(cy - ry, cy + ry + 1):
        t = (y - cy) / float(ry)
        e = 2.0 if t < 0 else jaw
        wd = rx * max(0, 1 - abs(t) ** e) ** .5
        for x in range(int(round(cx - wd)), int(round(cx + wd)) + 1):
            u = (x - cx) / max(rx, 1)
            c = skin
            if u * -lit > .5 or (u * -lit > .22 and bay(x, y) < (u * -lit - .22) * 2.2):
                c = sh
            elif u * lit > .35 and t < .25 and bay(x, y) < (u * lit - .35) * 1.8:
                c = hi
            if t > .72 and bay(x, y) < (t - .72) * 3:
                c = sh
            px(im, x, y, c)

def neck(im, cx, y0, y1, skin, sh, w=5):
    for y in range(y0, y1):
        for x in range(cx - w, cx + w + 1):
            c = sh if (x > cx + w * .1 or (y - y0) < 3 and bay(x, y) < .6) else skin
            px(im, x, y, c)

def ears(im, cx, cy, rx, skin, sh, dy=1, h=3):
    for sgn, c in ((-1, skin), (1, sh)):
        x = cx + sgn * (rx + 1)
        for k in range(h):
            px(im, x, cy + dy + k, c)
        px(im, x + sgn, cy + dy + 1, sh if sgn == 1 else skin)

def eye(im, x, y, gaze=0, lid=0, dark=C('3a2420'), white=C('e8e0d4'), lidc=C('7a4a38'), iris=INK):
    px(im, x - 1, y, white); px(im, x + 1, y, white); px(im, x, y, white)
    px(im, x + gaze, y, iris)
    for k in (-1, 0, 1):
        px(im, x + k, y - 1, dark)
    if lid:
        px(im, x - 1, y, lidc) if gaze >= 0 else px(im, x + 1, y, lidc)

def brow(im, x, y, w, col, side, tilt=0):
    """side -1 = left brow, +1 = right brow; tilt>0 raises outer end"""
    for k in range(w):
        yy = y
        outer = (k if side == 1 else w - 1 - k)
        if tilt > 0 and outer >= w - 2:
            yy = y - 1
        if tilt < 0 and outer >= w - 2:
            yy = y + 1
        px(im, x - w // 2 + k, yy, col)

def nose(im, cx, cy, sh, hi, dk):
    px(im, cx - 1, cy + 1, hi); px(im, cx - 1, cy + 2, hi)
    for k in range(-1, 4):
        px(im, cx + 1, cy + k, sh)
    px(im, cx, cy + 4, hi)
    px(im, cx - 1, cy + 5, sh); px(im, cx, cy + 5, dk); px(im, cx + 1, cy + 5, dk); px(im, cx + 2, cy + 4, sh)

def mouth(im, cx, y, w, line, lip, sh, left=0, right=0, lowlip=True):
    for k in range(-(w // 2), w - w // 2):
        px(im, cx + k, y, line)
    px(im, cx - w // 2 - 0, y - left, line); px(im, cx + w - w // 2 - 1, y - right, line)
    if left:
        px(im, cx - w // 2, y, line)
    if lowlip:
        for k in range(-(w // 2) + 1, w - w // 2 - 1):
            px(im, cx + k, y + 1, lip)
        for k in range(-(w // 4), w // 4 + 1):
            px(im, cx + k, y + 2, sh)

def bg_noise(im, rng, n, cols, y0=0, y1=H):
    for _ in range(n):
        px(im, rng.randrange(W), rng.randrange(y0, y1), rng.choice(cols))

def still(name, n=1):
    return scene(name, 1, 'png')

# =============================================================== JANIS
@scene('janis', 1, 'png')
def janis():
    im = Image.new('RGB', (W, H))
    rng = random.Random(201)
    vgrad(im, 0, H, [C('5a3c2c'), C('46302a'), C('2e2024')], 6)
    for yy in range(0, H, 3):
        for x in range(0, W, 12):
            px(im, x + (yy // 3) % 3 * 4, yy, C('3a281f'))
    items = [C('8a4a3a'), C('c8b080'), C('4a6a7a'), C('6a7a4a'), C('a8884a'), C('7a4a62'), C('d4c4a0')]
    for sy in (17, 35, 53):
        rect(im, 0, sy, W - 1, sy + 1, C('8a6244')); hline(im, sy + 2, 0, W - 1, C('241a18'))
        x = 0
        while x < W:
            w = rng.randint(4, 9); h = rng.randint(6, 12) if sy < 53 else rng.randint(5, 9)
            c = rng.choice(items)
            kind = rng.random()
            if kind < .35:  # jar / bottle
                rect(im, x, sy - h, x + w - 2, sy - 1, mix(c, INK, .2)); hline(im, sy - h, x + 1, x + w - 3, mix(c, SAND, .5))
                px(im, x + 1, sy - h + 2, mix(c, FOAM, .5))
            elif kind < .7:  # folded cloth stack
                for k in range(0, h, 3):
                    rect(im, x, sy - 3 - k, x + w - 1, sy - 1 - k, c if k % 6 == 0 else mix(c, INK, .25))
                    hline(im, sy - 3 - k, x, x + w - 1, mix(c, FOAM, .3))
            else:  # sack / tin
                rect(im, x, sy - h + 2, x + w - 1, sy - 1, c); hline(im, sy - h + 2, x, x + w - 1, mix(c, FOAM, .4))
                px(im, x + w - 1, sy - 1, INK)
            x += w + rng.randint(0, 2)
    # warm lamp wash from the left, shadow right
    for y in range(H):
        for x in range(W):
            d = ((x - 4) ** 2 + (y - 22) ** 2) ** .5 / 70.0
            if d < 1:
                dpx(im, x, y, C('e0a050'), .1 * (1 - d))
    vignette(im, 30, 30, 62, C('1a1216'), .9)
    cx, cy, rx, ry = 48, 29, 10, 14
    for y in range(0, 60):
        for x in range(W):
            d = (((x - 52) / 20.0) ** 2 + ((y - 30) / 26.0) ** 2)
            if d < 1:
                dpx(im, x, y, C('1a1216'), .7 * (1 - d))
    torso(im, cx, 48, 80, C('2e2a36'), C('1c1a24'), C('4a4656'))
    # neckerchief + shirt collar
    poly(im, [(cx - 8, 46), (cx + 8, 46), (cx, 58)], C('a8483a'))
    poly(im, [(cx - 8, 46), (cx - 2, 46), (cx - 2, 52), (cx - 5, 50)], C('c8654a'))
    poly(im, [(cx + 1, 47), (cx + 8, 46), (cx + 2, 56)], C('7a2e2a'))
    poly(im, [(cx - 8, 45), (cx - 14, 52), (cx - 6, 54)], C('d8c8a8'))
    for yy in range(52, 72):      # coat lapel seams + buttons
        px(im, cx - 12 - (yy - 52) // 4, yy, C('4a4656'))
        px(im, cx + 13 + (yy - 52) // 4, yy, C('15121a'))
    for yy in (62, 68):
        px(im, cx + 2, yy, C('8a7a5a'))
    neck(im, cx, 40, 48, C('c28a62'), C('94603f'))
    headshape(im, cx, cy, rx, ry, C('cc926a'), C('98643f'), C('e2ae82'))
    ears(im, cx, cy, rx, C('c28a62'), C('94603f'))
    for y in range(cy - 7, cy + 4):
        for sgn in (-1, 1):
            for x in (cx + sgn * (rx - 1), cx + sgn * rx):
                px(im, x, y, C('7a6a60') if sgn == -1 else C('5a4c46'))
    # weathered face: deep-set eyes, appraising
    for k in range(-1, 2):
        px(im, cx - 7 + k, cy - 2, C('8a5438')); px(im, cx + 7 + k, cy - 2, C('7a4a34'))
    eye(im, cx - 5, cy - 1, gaze=-1, lid=1, dark=C('4a2a22'), lidc=C('8a5438'))
    eye(im, cx + 5, cy - 1, gaze=-1, lid=1, dark=C('4a2a22'), lidc=C('8a5438'))
    brow(im, cx - 5, cy - 4, 5, C('5a463c'), -1, 1); brow(im, cx + 5, cy - 4, 5, C('5a463c'), 1, -1)
    px(im, cx - 9, cy, C('8a5438')); px(im, cx + 9, cy, C('7a4a34'))     # crow's feet
    px(im, cx - 8, cy + 2, C('9a6a48')); px(im, cx + 8, cy + 2, C('84583c'))
    nose(im, cx, cy, C('a8704a'), C('e8b88a'), C('6a3e2e'))
    for k in range(5):  # nasolabial lines
        px(im, cx - 6 + k // 3, cy + 5 + k, C('a8704a')); px(im, cx + 6 - k // 3, cy + 5 + k, C('946038'))
    # stubble + mustache
    for y in range(cy + 6, cy + ry):
        for x in range(cx - rx, cx + rx + 1):
            t = (y - cy) / float(ry)
            wd = rx * max(0, 1 - abs(t) ** 2.6) ** .5
            if abs(x - cx) <= wd and bay(x, y) < .38:
                px(im, x, y, C('5a463c'))
    for k in range(-5, 6):
        px(im, cx + k, cy + 7, C('4a382e')); 
        if abs(k) > 1:
            px(im, cx + k, cy + 8, C('4a382e')) if abs(k) < 5 else None
    mouth(im, cx, cy + 10, 6, C('4a2a24'), C('a8604a'), C('9a6a48'), left=0, right=0, lowlip=False)
    px(im, cx + 3, cy + 10, C('4a2a24')); px(im, cx + 4, cy + 9, C('4a2a24'))   # faint one-sided smirk
    # broad-brimmed felt hat
    hatc, hath, hats = C('4a3f3a'), C('6a5c52'), C('2e2622')
    for y in range(3, 17):
        t = (y - 3) / 13.0
        hw = 11 - (1 - t) * 2.5
        for x in range(int(cx - hw), int(cx + hw) + 1):
            c = hatc if x < cx + 3 else hats
            if y == 3 or (y == 4 and bay(x, y) < .4):
                c = hath
            if y in (13, 14, 15):
                c = C('8a3a2e') if y == 14 else C('5a2822')    # ribbon band
                if x > cx + 7:
                    c = hats
            px(im, x, y, c)
    for x in range(cx - 26, cx + 27):
        yy = 17 - int((abs(x - cx) / 26.0) ** 2 * -4) + int((x - cx) * .05) - 4 + 3
        yy = 16 + int(((x - cx) / 26.0) ** 2 * 5) - int((x - cx) * .04)
        for y in range(yy - 1, yy + 3):
            px(im, x, y, hath if y == yy - 1 else (hatc if y <= yy + 1 else hats))
        if abs(x - cx) < rx:
            for y in range(yy + 2, yy + 5):
                if bay(x, y) < .7 - (y - yy - 2) * .25:
                    px(im, x, y, C('7a4a38'))
    return im

# =============================================================== CHIEF FRIDAY
@scene('friday', 1, 'png')
def friday():
    im = Image.new('RGB', (W, H))
    vgrad(im, 0, H, [C('6f90b8'), C('a8c0d0'), C('e6dcc0')], 8)
    # cottonwood behind: right trunk, canopy masses top
    cottonwood(im, 86, 74, 70, 52, random.Random(5), trunk=C('5a4234'),
               cols=(C('3a5a3e'), C('68884a'), C('a8c066')), lean=0, n_blobs=14)
    cottonwood(im, 0, 40, 20, 40, random.Random(6), trunk=C('5a4234'),
               cols=(C('3a5a3e'), C('68884a'), C('a8c066')), lean=0, n_blobs=9)
    for x in range(0, W):   # distant river bluff line
        px(im, x, 56, C('9aa8a0'))
    cx, cy, rx, ry = 48, 28, 10, 14
    skin, sh, hi, ln = C('b97d53'), C('8a563a'), C('d6a074'), C('4a2a22')
    torso(im, cx, 47, 78, C('37416a'), C('252d52'), C('56628a'))
    # woven stripe across the blanket-coat
    for y, c in ((58, C('e6d6ae')), (59, C('b0482e')), (60, C('b0482e')), (61, C('e6d6ae')), (67, C('e6d6ae')), (68, C('c8963e'))):
        hw = 9 + (39 - 9) * min(1.0, ((y - 47) / 16.0)) ** .55
        for x in range(int(cx - hw) + 1, int(cx + hw)):
            px(im, x, y, c if x < cx + hw * .45 or bay(x, y) < .5 else mix(c, C('252d52'), .45))
    # V neck showing chest, beaded choker
    poly(im, [(cx - 7, 45), (cx + 7, 45), (cx, 56)], skin)
    poly(im, [(cx + 1, 46), (cx + 7, 45), (cx + 1, 54)], sh)
    for k in range(-5, 6):
        yy = 47 + (abs(k) // 3)
        px(im, cx + k, yy, C('e4d4ac') if k % 2 else C('b0482e'))
    neck(im, cx, 38, 47, skin, sh, 5)
    # hair behind head
    headshape(im, cx, cy, rx + 1, ry + 1, C('1e1a24'), C('1e1a24'), C('1e1a24'), lit=-1)
    headshape(im, cx, cy + 1, rx, ry, skin, sh, hi)
    ears(im, cx, cy, rx, skin, sh)
    # hair cap with centre part, framing the face
    for y in range(cy - ry - 1, cy - 4):
        t = (y - (cy - ry - 1)) / float(ry - 3)
        for x in range(cx - rx - 1, cx + rx + 2):
            hw = (rx + 1) * max(0, 1 - ((y - cy) / float(ry + 1)) ** 2) ** .5
            if abs(x - cx) <= hw + .5 and (y < cy - 8 - int(abs(x - cx) * .15) or abs(x - cx) > rx - 2):
                px(im, x, y, C('1e1a24') if x < cx + 3 else C('14111a'))
    for y in range(cy - ry - 1, cy - 9):
        px(im, cx - 1, y, C('4a4658'))
    for x in range(cx - 7, cx - 1):     # sheen on hair
        px(im, x, cy - ry + 1 + (abs(x - cx + 4) // 3), C('3e3a4c'))
    for sgn in (-1, 1):                 # side hair framing cheek
        for y in range(cy - 8, cy + 6):
            for x in (cx + sgn * (rx - 1), cx + sgn * rx):
                px(im, x, y, C('1e1a24') if sgn == -1 else C('14111a'))
    # features: composed, intelligent
    eye(im, cx - 5, cy, gaze=0, dark=C('2a1a1a'), white=C('d8d0c4'))
    eye(im, cx + 5, cy, gaze=0, dark=C('2a1a1a'), white=C('d8d0c4'))
    brow(im, cx - 5, cy - 3, 5, C('1e1a24'), -1, 0); brow(im, cx + 5, cy - 3, 5, C('1e1a24'), 1, 1)
    nose(im, cx, cy, sh, hi, C('6a3e2e'))
    px(im, cx - 7, cy + 3, hi); px(im, cx - 6, cy + 3, hi); px(im, cx + 7, cy + 4, sh); px(im, cx + 6, cy + 5, sh)   # cheekbones
    for k in range(4):
        px(im, cx + 6, cy + 7 + k, sh)
    mouth(im, cx, cy + 9, 6, ln, C('a8604a'), sh)
    px(im, cx + 3, cy + 9, ln)
    # braids over the shoulders, trade-cloth wraps
    for sgn in (-1, 1):
        bx = cx + sgn * 14
        for y in range(cy + 2, H):
            bend = int(round(1.5 * math.sin((y - cy) * .12))) * -sgn
            for k in range(-2, 3):
                if abs(k) == 2 and (y % 4 >= 2):
                    continue
                c = C('1e1a24')
                if (y + k) % 4 < 2:
                    c = C('2c2836')
                if k == -1 and sgn == -1 or k == 1 and sgn == 1:
                    c = C('4a4658') if y % 4 < 2 else c
                px(im, bx + k + bend, y, c)
        for y in range(63, 66):
            for k in range(-2, 3):
                px(im, bx + k + int(round(1.5 * math.sin((y - cy) * .12))) * -sgn, y, C('c8963e') if y != 64 else C('a8482e'))
    return im

# =============================================================== VIRGINIA SLADE
@scene('virginia', 1, 'png')
def virginia():
    im = Image.new('RGB', (W, H))
    # stable door planks
    for x in range(W):
        for y in range(H):
            k = (x // 6) % 2
            c = (C('6e4c36'), C('5e4230'))[k]
            if x % 6 == 5:
                c = C('2a1c18')
            elif (y * 5 + x * 3) % 17 == 0:
                c = C('3e2a20')
            px(im, x, y, c)
    for y0 in (12, 56):   # cross braces
        rect(im, 0, y0, W - 1, y0 + 4, C('7a5638')); hline(im, y0, 0, W - 1, C('9a7248')); hline(im, y0 + 4, 0, W - 1, C('3e2a20'))
    for x in range(0, W, 8):
        px(im, x + 2, 14, C('1c1418')); px(im, x + 2, 58, C('1c1418'))
    # open top half: dark stall interior with lamp glow
    rect(im, 0, 16, 29, 54, C('1e1620'))
    for y in range(16, 55):
        for x in range(0, 30):
            d = ((x - 4) ** 2 + (y - 36) ** 2) ** .5 / 28
            if d < 1:
                dpx(im, x, y, C('c88a48'), .3 * (1 - d))
    for x in range(0, 30, 2):
        px(im, x, 52, C('b89a50')); px(im, x + 1, 53, C('8a7038'))
    rect(im, 28, 16, 30, 54, C('3e2a20'))
    # hay bale lower right
    rect(im, 66, 58, 95, 71, C('b8963e'))
    for k in range(40):
        rr = random.Random(k)
        x = rr.randrange(66, 96); y = rr.randrange(58, 72)
        line(im, x, y, x + rr.randint(2, 4), y - rr.randint(0, 1), C('dcbc62') if k % 2 else C('8a6a2c'))
    hline(im, 58, 66, 95, C('dcbc62')); vignette(im, 36, 30, 64, C('140e12'), .8)
    cx, cy, rx, ry = 46, 29, 9, 13
    skin, sh, hi, ln = C('e2b48c'), C('b4805e'), C('f4d2ac'), C('6a3a2e')
    torso(im, cx, 48, 76, C('4c6070'), C('34444f'), C('6a8294'))
    # sleeves rolled hint + collar
    poly(im, [(cx - 8, 46), (cx + 8, 46), (cx + 6, 50), (cx - 6, 50)], C('e8dcc4'))
    for k in range(-5, 6, 2):
        px(im, cx + k, 50, C('b8ac94'))
    # apron bib and straps
    poly(im, [(cx - 11, 72), (cx - 11, 61), (cx - 7, 57), (cx + 7, 57), (cx + 11, 61), (cx + 11, 72)], C('d8ccb0'))
    poly(im, [(cx + 3, 72), (cx + 3, 58), (cx + 7, 57), (cx + 11, 61), (cx + 11, 72)], C('b0a48a'))
    for sgn in (-1, 1):
        for k in range(10):
            px(im, cx + sgn * (7 + k // 3), 56 - k, C('d8ccb0') if sgn < 0 else C('b0a48a'))
            px(im, cx + sgn * (8 + k // 3), 56 - k, C('d8ccb0') if sgn < 0 else C('b0a48a'))
    hline(im, 64, cx - 8, cx - 3, C('a89c84')); line(im, cx - 8, 64, cx - 8, 68, C('a89c84')); line(im, cx - 3, 64, cx - 3, 68, C('a89c84'))
    for yy in (59, 63):
        px(im, cx - 5, yy, C('8a7c68')) if False else None
    neck(im, cx, 40, 48, skin, sh, 4)
    # bun behind
    disc(im, cx + 3, 12, 7, 5, C('6a3a26'))
    for k in range(6):
        px(im, cx - 2 + k * 2, 9 + (k % 3), C('9a5a38'))
    px(im, cx + 5, 12, C('c8b080')); px(im, cx + 6, 12, C('c8b080'))
    headshape(im, cx, cy + 1, rx, ry, skin, sh, hi, jaw=2.2)
    ears(im, cx, cy, rx, skin, sh)
    # hair: smooth centre part, pulled back
    for y in range(cy - ry, cy - 5):
        for x in range(cx - rx - 1, cx + rx + 2):
            hw = (rx + 1) * max(0, 1 - ((y - cy) / float(ry + 1)) ** 2) ** .5
            lim = cy - 9 + int(abs(x - cx) * .5)
            if abs(x - cx) <= hw + .5 and y < lim - (1 if abs(x - cx) < 4 else 0):
                c = C('6a3a26')
                if (x + y) % 5 == 0:
                    c = C('8a4e30')
                if x < cx - 2 and y < cy - 11:
                    c = C('9a5a38') if (x + y) % 3 == 0 else c
                px(im, x, y, c)
    for y in range(cy - ry, cy - 9):
        px(im, cx - 1, y, C('2e1a14'))
    for sgn in (-1, 1):
        for y in range(cy - 7, cy + 2):
            px(im, cx + sgn * (rx - 1), y, C('6a3a26')); 
        px(im, cx + sgn * (rx - 1), cy + 2, C('8a4e30'))
    eye(im, cx - 4, cy, gaze=0, dark=C('4a2a22'), white=C('ece4d8'))
    eye(im, cx + 4, cy, gaze=0, dark=C('4a2a22'), white=C('ece4d8'))
    brow(im, cx - 4, cy - 3, 4, C('4a2a1e'), -1, 0); brow(im, cx + 4, cy - 3, 4, C('4a2a1e'), 1, 0)
    px(im, cx - 4, cy, C('3e4a3e')); px(im, cx + 4, cy, C('3e4a3e'))   # grey-green iris
    nose(im, cx, cy, sh, hi, C('8a5a44'))
    px(im, cx - 6, cy + 4, C('e8a08a')); px(im, cx + 6, cy + 4, C('d4907a'))   # weathered cheek colour
    mouth(im, cx, cy + 9, 6, C('8a4a40'), C('c47868'), sh, left=0, right=0)
    return im

# =============================================================== MASON
@scene('mason', 1, 'png')
def mason():
    im = Image.new('RGB', (W, H))
    vgrad(im, 0, H, [C('6e7a6e'), C('56625a'), C('3e4842')], 6)
    for x in range(0, W, 8):
        for y in range(36, H):
            px(im, x, y, C('4a564e')) if y % 2 else None
    hline(im, 36, 0, W - 1, C('8a7a5a')); hline(im, 37, 0, W - 1, C('3a3c36'))
    # framed survey map on the wall
    rect(im, 60, 5, 91, 33, C('8a6a3a')); rect(im, 61, 6, 90, 32, C('3e2e1e'))
    rect(im, 63, 8, 88, 30, C('e8dcb8'))
    for gx in range(63, 89, 5):
        vlin = [(gx, 8), (gx, 30)]
        line(im, gx, 8, gx, 30, C('c8bc98'))
    for gy in range(8, 31, 5):
        line(im, 63, gy, 88, gy, C('c8bc98'))
    for k in range(26):
        px(im, 63 + k, 19 + int(4 * math.sin(k * .35)), C('4a6a88'))
        px(im, 63 + k, 20 + int(4 * math.sin(k * .35)), C('4a6a88'))
    rect(im, 70, 12, 75, 15, C('a85a42')); rect(im, 78, 23, 84, 27, C('a85a42'))  # parcels
    for (a, b) in ((70, 12), (78, 23)):
        px(im, a + 1, b + 1, C('e8dcb8'))
    for y in range(0, H):
        for x in range(0, 30):
            d = abs(x - 0) / 40.0
            dpx(im, x, y, C('c8b888'), .12 * (1 - d))
    vignette(im, 40, 34, 64, C('1a1e1c'), .7)
    cx, cy, rx, ry = 46, 29, 10, 14
    skin, sh, hi, ln = C('dcaa84'), C('ae7c5a'), C('f2caa2'), C('5a3228')
    torso(im, cx, 48, 76, C('3e383e'), C('26222a'), C('5c545c'))
    # waistcoat
    poly(im, [(cx - 9, 47), (cx + 9, 47), (cx + 6, 72), (cx - 6, 72)], C('7a3e34'))
    poly(im, [(cx + 1, 47), (cx + 9, 47), (cx + 6, 72), (cx + 1, 72)], C('582a26'))
    for y in range(52, 72, 3):
        for x in range(cx - 7, cx + 8, 4):
            px(im, x + (y // 3 % 2) * 2, y, C('9a5a44'))
    for yy in (55, 61, 67):
        px(im, cx, yy, C('e0b84e'))
    for k in range(10):                      # watch chain
        px(im, cx - 5 + k, 58 + int(3 * math.sin(k * .33)), C('e0b84e'))
    # lapels
    poly(im, [(cx - 9, 46), (cx - 16, 52), (cx - 8, 66), (cx - 9, 50)], C('2a262c'))
    poly(im, [(cx + 9, 46), (cx + 16, 52), (cx + 8, 66), (cx + 9, 50)], C('1a181e'))
    # collar + cravat
    poly(im, [(cx - 8, 44), (cx + 8, 44), (cx + 7, 50), (cx - 7, 50)], C('eae2d0'))
    poly(im, [(cx - 3, 48), (cx + 3, 48), (cx + 2, 56), (cx - 2, 56)], C('9a98b8'))
    px(im, cx, 50, C('e0b84e'))
    # rolled papers under the arm
    for k, c in enumerate((C('f8f0da'), C('eadcbe'), C('eadcbe'), C('d2c4a4'), C('a89a7c'))):
        line(im, 56, 61 + k, 92, 68 + k, c)
    line(im, 56, 66, 92, 73, C('2e2a30'))
    for k in range(5):
        px(im, 74, 64 + k, C('a83a30')); px(im, 75, 64 + k, C('a83a30')); px(im, 76, 64 + k + 1, C('7a2a24'))
    disc(im, 91, 68, 2, 3, C('f2e8cc')); px(im, 91, 68, C('8a7a5a')); px(im, 92, 67, C('8a7a5a'))
    neck(im, cx, 40, 47, skin, sh, 5)
    headshape(im, cx, cy, rx, ry, skin, sh, hi)
    ears(im, cx, cy, rx, skin, sh)
    # tidy beard
    bd, bh = C('3a2a24'), C('5a4234')
    for y in range(cy - 2, cy + ry + 1):
        t = (y - cy) / float(ry)
        wd = rx * max(0, 1 - abs(t) ** 2.6) ** .5
        for x in range(int(round(cx - wd)), int(round(cx + wd)) + 1):
            edge = abs(x - cx) >= wd - 1.5
            if (y >= cy + 7) or (edge and y < cy + 7):
                if y >= cy + 8 and abs(x - cx) < 3 and y <= cy + 9:
                    continue
                px(im, x, y, bh if (x + y) % 4 == 0 and x < cx else bd)
    for k in range(-5, 6):    # moustache
        px(im, cx + k, cy + 7, bd)
        if abs(k) > 1 and abs(k) < 5:
            px(im, cx + k, cy + 8, bd)
    mouth(im, cx, cy + 9, 5, C('8a4a42'), C('b46a5a'), bd, left=0, right=1, lowlip=False)
    px(im, cx + 3, cy + 9, C('8a4a42')); px(im, cx + 4, cy + 8, C('8a4a42')) if False else None
    # eyes: narrowed, calculating
    eye(im, cx - 5, cy - 1, gaze=1, lid=1, dark=C('4a2a22'), lidc=sh)
    eye(im, cx + 5, cy - 1, gaze=1, lid=1, dark=C('4a2a22'), lidc=sh)
    brow(im, cx - 5, cy - 4, 5, bd, -1, 0); brow(im, cx + 5, cy - 4, 5, bd, 1, 1)
    nose(im, cx, cy, sh, hi, C('7a4a38'))
    # bowler hat
    hc, hh, hs = C('2e2a2c'), C('625c64'), C('1a181a')
    for y in range(3, 16):
        t = (y - 3) / 12.0
        hw = 11 * math.sqrt(max(0, 1 - (1 - t) ** 2.2))
        for x in range(int(round(cx - hw)), int(round(cx + hw)) + 1):
            c = hc if x > cx - 6 else hh
            if x > cx + 5:
                c = hs
            if y in (12, 13):
                c = C('141214')
            px(im, x, y, c)
    for x in range(cx - 14, cx + 15):
        yy = 15 + int(((x - cx) / 14.0) ** 2 * 2) - int(((x - cx) / 14.0) ** 4 * 2)
        yy = 16 - (1 if abs(x - cx) > 11 else 0)
        px(im, x, yy, hh if x < cx else hc); px(im, x, yy + 1, hs)
    return im

# =============================================================== PETER AMES
@scene('ames', 1, 'png')
def ames():
    im = Image.new('RGB', (W, H))
    vgrad(im, 0, 50, [C('6a88b0'), C('b0c2cc'), C('eadeb8')], 8)
    rng = random.Random(301)
    ridge(im, 42, 8, C('8c9c8e'), rng, 12, 3)
    ridge(im, 46, 4, C('6c8068'), rng, 9, 3)
    vgrad(im, 46, H, [C('8a9458'), C('687a46'), C('4a5e3a')], 5)
    bg_noise(im, rng, 80, (C('a4a864'), C('3e5232')), 46, H)
    # surveyor's tripod, right
    for (x1, y1) in ((64, 52), (80, 54), (95, 50)):
        line(im, 80, 20, x1, y1, C('5a3e2c')); line(im, 81, 20, x1 + 1, y1, C('8a6a44'))
    rect(im, 77, 14, 84, 20, C('c89c3c')); rect(im, 79, 11, 82, 14, C('a8802c')); px(im, 78, 15, C('f4d878'))
    cx, cy, rx, ry = 47, 30, 9, 13
    skin, sh, hi = C('e8bc98'), C('b88260'), C('f8d6b2')
    torso(im, cx, 48, 78, C('34456e'), C('222f52'), C('566a98'))
    poly(im, [(cx - 7, 46), (cx + 7, 46), (cx + 6, 50), (cx - 6, 50)], C('f0ead8'))
    rect(im, cx - 9, 46, cx - 6, 52, C('222f52')); rect(im, cx + 6, 46, cx + 9, 52, C('1a2542'))
    for yy in range(54, 72, 4):
        for bx in (cx - 3, cx + 3):
            px(im, bx, yy, C('e0b84e')); px(im, bx, yy + 1, C('a8802c'))
    line(im, cx - 12, 52, cx - 18, 72, C('2a3860')); line(im, cx + 12, 52, cx + 18, 72, C('1a2542'))
    neck(im, cx, 40, 47, skin, sh, 4)
    headshape(im, cx, cy, rx, ry, skin, sh, hi, jaw=2.3)
    ears(im, cx, cy, rx, skin, sh)
    # hair + sideburns
    for sgn in (-1, 1):
        for y in range(cy - 8, cy + 3):
            for x in (cx + sgn * (rx - 1), cx + sgn * rx):
                px(im, x, y, C('8a6040') if sgn == -1 else C('5e4028'))
    # spectacles with glints
    for sx in (-5, 5):
        ex = cx + sx
        for dx in range(-2, 3):
            for dy in range(-2, 3):
                if max(abs(dx), abs(dy)) == 2 and not (abs(dx) == 2 and abs(dy) == 2):
                    px(im, ex + dx, cy + dy, C('b8903a'))
        px(im, ex - 2, cy - 1, C('f4d878')) if False else None
    for dx in range(-2, 3):
        px(im, cx + dx, cy - 1, C('b8903a'))
    px(im, cx - 8, cy - 1, C('b8903a')); px(im, cx + 8, cy - 1, C('8a6a28'))
    eye(im, cx - 5, cy, gaze=1, dark=C('3a2420'), white=C('f4f0e8'))
    eye(im, cx + 5, cy, gaze=1, dark=C('3a2420'), white=C('f4f0e8'))
    px(im, cx - 6, cy - 2, C('fff8e0')); px(im, cx + 4, cy - 2, C('fff8e0'))   # lens glint
    brow(im, cx - 5, cy - 5, 5, C('6a4a30'), -1, 1); brow(im, cx + 5, cy - 5, 5, C('6a4a30'), 1, 1)
    nose(im, cx, cy + 1, sh, hi, C('8a5a44'))
    # eager open smile
    for k in range(-3, 4):
        px(im, cx + k, cy + 9, C('7a3a32'))
    px(im, cx - 4, cy + 8, C('7a3a32')); px(im, cx + 4, cy + 8, C('7a3a32'))
    for k in range(-2, 3):
        px(im, cx + k, cy + 10, C('f6ece0'))
    px(im, cx - 1, cy + 11, C('c8786a')); px(im, cx, cy + 11, C('c8786a')); px(im, cx + 1, cy + 11, C('c8786a'))
    px(im, cx - 6, cy + 5, C('eaa088')); px(im, cx + 6, cy + 5, C('d28e78'))
    # forage cap
    cap, caph, caps = C('2f3e66'), C('4a5c8a'), C('1c2640')
    for y in range(6, 19):
        t = (y - 6) / 12.0
        hw = 10 + t * 1.5
        for x in range(int(round(cx - hw)), int(round(cx + hw)) + 1):
            c = cap if x > cx - 6 else caph
            if x > cx + 6:
                c = caps
            if y >= 15 and y <= 16:
                c = C('1c2640')
            px(im, x, y, c)
    for k in range(-9, 10):
        px(im, cx + k, 3 + (abs(k) > 7) , caph) if False else None
    hline(im, 6, cx - 8, cx + 8, caph)
    for x in range(cx - 10, cx + 12):    # bill
        yy = 19 + (1 if abs(x - cx) > 8 else 0)
        px(im, x, yy, C('15192c')); px(im, x, yy + 1, C('0e1220')) if abs(x - cx) < 8 else None
    px(im, cx, 12, C('e0b84e')); px(im, cx - 1, 12, C('e0b84e')); px(im, cx, 11, C('f8dc7a')); px(im, cx + 1, 12, C('a8802c'))
    # field notebook, bottom-left
    poly(im, [(6, 62), (30, 57), (36, 72), (8, 72)], C('5a3e28'))
    poly(im, [(8, 63), (29, 58), (34, 70), (10, 71)], C('ece0bc'))
    for k in range(4):
        line(im, 10 + k, 65 + k * 2 - 2 if False else 66 + k * 1, 29 + k, 61 + k * 2, C('a8b0b8'))
    line(im, 12, 62, 28, 59, C('3a3a50')); line(im, 12, 64, 24, 62, C('3a3a50'))
    disc(im, 33, 66, 3, 3, skin); disc(im, 34, 69, 3, 3, sh)
    # brass pocket instrument (compass / level), bottom-right
    disc(im, 72, 65, 10, 10, C('a8802c')); disc(im, 71, 64, 9, 9, C('d4a640'))
    disc(im, 72, 65, 7, 7, C('f0e8cc'))
    for (dx, dy) in ((0, -6), (6, 0), (0, 6), (-6, 0)):
        px(im, 72 + dx, 65 + dy, C('3a2e24'))
    line(im, 72, 65, 75, 60, C('b83a30')); line(im, 72, 65, 69, 70, C('3a3a4a')); px(im, 72, 65, C('d4a640'))
    px(im, 66, 58, C('fff0b0')); px(im, 67, 57, C('fff0b0'))
    disc(im, 82, 70, 4, 3, skin)
    return im

# =============================================================== LITTLE WILLOW
@scene('willow', 1, 'png')
def willow():
    im = Image.new('RGB', (W, H))
    rng = random.Random(401)
    vgrad(im, 0, H, [C('88a060'), C('647c48'), C('3e5236')], 6)
    # willow wands
    for x in range(-4, W + 4, 3):
        xx = x + rng.randint(-1, 1); lean = rng.choice([-1, 0, 0, 1]) * .08
        col = rng.choice([C('b4a850'), C('c8b860'), C('9a5a3a'), C('b86a40'), C('8a9a48')])
        for y in range(0, H):
            px(im, int(xx + lean * y), y, col)
            if rng.random() < .09:
                lx = int(xx + lean * y)
                px(im, lx + 1, y, C('9ab04e')); px(im, lx + 2, y + 1, C('7a9440'))
                px(im, lx - 1, y + 1, C('b4c860'))
    for _ in range(60):
        px(im, rng.randrange(W), rng.randrange(H), C('d8e090'))
    vignette(im, 48, 30, 56, C('26361f'), .8)
    cx, cy, rx, ry = 48, 29, 9, 13
    skin, sh, hi, ln = C('c0845a'), C('8e5c3c'), C('dca87a'), C('4a2a22')
    # hide bag + strap
    torso(im, cx, 48, 74, C('8a4a38'), C('5e3028'), C('b06a4a'))
    poly(im, [(cx - 7, 46), (cx + 7, 46), (cx + 2, 56), (cx - 2, 56)], skin)
    poly(im, [(cx + 1, 46), (cx + 7, 46), (cx + 2, 54)], sh)
    poly(im, [(cx + 8, 48), (cx + 12, 48), (cx - 12, 72), (cx - 16, 72)], C('7a5636'))
    line(im, cx + 9, 48, cx - 14, 72, C('a8845a'))
    for k in range(0, 24, 4):
        px(im, cx + 10 - k, 50 + k, C('d8bc88'))
    disc(im, 17, 67, 14, 9, C('8a6a40')); disc(im, 16, 66, 13, 8, C('a8845a'))
    for x in range(3, 31):
        yy = 58 + int(((x - 17) / 14.0) ** 2 * 4)
        px(im, x, yy, C('5a3e28')); px(im, x, yy + 1, C('c8a470')) if x % 2 else None
    for k in range(5):
        px(im, 6 + k * 5, 61 + (1 if k in (0, 4) else 0), C('e0c890'))
    rect(im, 15, 63, 17, 69, C('6a4a30')); px(im, 16, 69, C('e0c890'))
    for k in range(5):
        px(im, 6 + k * 5, 72 - 1, C('6a4a30')) if False else None
    neck(im, cx, 38, 47, skin, sh, 4)
    headshape(im, cx, cy - 1 + 1, rx + 1, ry + 1, C('1e1a24'), C('1e1a24'), C('1e1a24'))
    headshape(im, cx, cy + 1, rx, ry, skin, sh, hi, jaw=2.1)
    ears(im, cx, cy, rx, skin, sh)
    # hair
    for y in range(cy - ry - 1, cy - 4):
        for x in range(cx - rx - 2, cx + rx + 3):
            hw = (rx + 1) * max(0, 1 - ((y - cy) / float(ry + 1)) ** 2) ** .5
            if abs(x - cx) <= hw + .5 and (y < cy - 9 + int(abs(x - cx) * .2) or abs(x - cx) > rx - 2):
                px(im, x, y, C('1e1a24') if x < cx + 3 else C('14111a'))
    for y in range(cy - ry - 1, cy - 9):
        px(im, cx - 1, y, C('4a4658'))
    for x in range(cx - 7, cx - 1):
        px(im, x, cy - ry + 1 + (abs(x - cx + 4) // 3), C('3e3a4c'))
    for sgn in (-1, 1):
        for y in range(cy - 8, cy + 6):
            for x in (cx + sgn * (rx - 1), cx + sgn * rx):
                px(im, x, y, C('1e1a24') if sgn == -1 else C('14111a'))
    # woven headband
    for x in range(cx - rx, cx + rx + 1):
        yy = cy - 9 + int(abs(x - cx) * .22)
        px(im, x, yy, C('e4d4ac') if x % 2 else C('b0482e'))
        px(im, x, yy + 1, C('b0482e') if x % 2 else C('3e5a7a'))
    eye(im, cx - 4, cy, gaze=0, dark=C('2a1a1a'), white=C('e0d8cc'))
    eye(im, cx + 4, cy, gaze=0, dark=C('2a1a1a'), white=C('e0d8cc'))
    brow(im, cx - 4, cy - 3, 4, C('1e1a24'), -1, 0); brow(im, cx + 4, cy - 3, 4, C('1e1a24'), 1, 0)
    nose(im, cx, cy, sh, hi, C('6a3e2e'))
    px(im, cx - 6, cy + 4, C('d8946a')); px(im, cx + 6, cy + 4, C('b87a56'))
    mouth(im, cx, cy + 9, 6, ln, C('b06a58'), sh, left=1, right=1)
    # braids
    for sgn in (-1, 1):
        bx = cx + sgn * 13
        for y in range(cy + 2, H):
            bend = int(round(1.2 * math.sin((y - cy) * .12))) * -sgn
            for k in range(-2, 3):
                if abs(k) == 2 and (y % 4 >= 2):
                    continue
                c = C('1e1a24') if (y + k) % 4 >= 2 else C('2c2836')
                if (k == -1 and sgn == -1) or (k == 1 and sgn == 1):
                    c = C('4a4658') if y % 4 < 2 else c
                px(im, bx + k + bend, y, c)
        for y in range(60, 63):
            for k in range(-2, 3):
                px(im, bx + k + int(round(1.2 * math.sin((y - cy) * .12))) * -sgn, y, C('c8963e') if y != 61 else C('3e5a7a'))
    return im

# =============================================================== OLD POINT
@scene('old_point', 1, 'png')
def old_point():
    im = Image.new('RGB', (W, H))
    rng = random.Random(501)
    vgrad(im, 0, H, [C('4a5878'), C('2e3650'), C('1c2236')], 7)
    for _ in range(14):   # soft out-of-focus river lights
        disc(im, rng.randrange(W), rng.randrange(0, 16), 2, 1, C('6a789a'), a=.35)
    skin, sh, hi = C('c4906a'), C('8c5c42'), C('dcaa82')
    # cupped palm
    for y in range(26, H):
        for x in range(W):
            t = (y - 26) / 46.0
            left = 0 - 2; right = W
            e = ((x - 48) / 56.0) ** 2 + ((y - 62) / 44.0) ** 2
            if e <= 1:
                c = skin
                if e > .72 and bay(x, y) < (e - .72) * 3:
                    c = sh
                if (x - 48) * .5 + (y - 50) * .3 > 12 and bay(x, y) < .5:
                    c = sh
                if x < 34 and y < 52 and bay(x, y) < .25:
                    c = hi
                px(im, x, y, c)
    # fingers curling over the top edge
    for (fx, fy, fr) in ((18, 28, 8), (36, 24, 8), (55, 23, 8), (73, 27, 8), (86, 36, 6)):
        for y in range(fy - 7, fy + 9):
            for x in range(fx - fr, fx + fr + 1):
                d = ((x - fx) / (fr + .5)) ** 2 + ((y - fy) / 9.0) ** 2
                if d <= 1:
                    c = skin if x < fx + 2 else sh
                    if y < fy - 3 and x < fx - 1 and bay(x, y) < .5:
                        c = hi
                    px(im, x, y, c)
        px(im, fx, fy - 6, hi)
    for (fx, fy) in ((27, 32), (45, 30), (64, 31), (80, 33)):   # gaps between fingers
        line(im, fx, fy - 8, fx, fy, C('5a3a2e'))
    for k in range(18):     # palm creases
        px(im, 20 + k, 46 + int(5 * math.sin(k * .22)) + k // 6, C('9a6648') if k % 3 else C('8c5c42'))
        px(im, 72 - k // 2, 44 + k, C('9a6648')) if k % 2 else None
    # the point
    cx, cy, L, Wd = 48, 47.0, 50.0, 19.0
    th = math.radians(-24); ct, st = math.cos(th), math.sin(th)
    def local(x, y):
        dx, dy = x - cx, y - cy
        u = dx * ct + dy * st; v = -dx * st + dy * ct
        return (u + L / 2) / L, v
    def hwf(t):
        if t < .38:
            return Wd / 2 * (.8 + .2 * math.sin(t / .38 * math.pi / 2))
        return Wd / 2 * max(0.0, 1 - ((t - .38) / .62) ** 1.7) ** .8
    mask = {}
    for y in range(20, 70):
        for x in range(10, 90):
            t, v = local(x, y)
            if t < 0 or t > 1:
                continue
            hw = hwf(t)
            if abs(v) <= hw and t >= .045 * (1 - (v / max(hw, 1)) ** 2) + .0:
                mask[(x, y)] = (t, v, hw)
    # shadow on the palm
    for (x, y) in list(mask):
        for (ox, oy) in ((2, 2), (3, 3), (4, 3)):
            if (x + ox, y + oy) not in mask and bay(x + ox, y + oy) < .8:
                px(im, x + ox, y + oy, C('5a3a2e'))
    base, lit, dim, edge = C('a28c66'), C('d2c298'), C('665640'), C('34281f')
    for (x, y), (t, v, hw) in mask.items():
        s = ((x - cx) * -.5 + (y - cy) * -.85) / 12.0
        c = base
        if s > .32 or (s > .1 and bay(x, y) < (s - .1) * 4):
            c = lit
        elif s < -.32 or (s < -.1 and bay(x, y) < (-s - .1) * 4):
            c = dim
        if (x * 3 + y * 7) % 11 == 0:
            c = C('8a8478')
        if abs(v) > hw - .9:
            c = edge if s < 0.05 else C('f4ead0')
        px(im, x, y, c)
    # pressure-flake scars
    for side in (-1, 1):
        for i in range(12):
            t = .1 + i * .072
            hw = hwf(t)
            if hw < 2.5:
                continue
            u0 = t * L - L / 2
            for dv, du, col in ((hw - 1.2, 0, C('9a8660')), (hw * .5, 3.0, C('b4a07a'))):
                pass
            p0 = (cx + u0 * ct - side * (hw - 1.2) * st, cy + u0 * st + side * (hw - 1.2) * ct)
            p1 = (cx + (u0 + 3.2) * ct - side * (hw * .45) * st, cy + (u0 + 3.2) * st + side * (hw * .45) * ct)
            xa, ya, xb, yb = int(round(p0[0])), int(round(p0[1])), int(round(p1[0])), int(round(p1[1]))
            n = max(abs(xb - xa), abs(yb - ya), 1)
            for k in range(n + 1):
                x = xa + (xb - xa) * k // n; y = ya + (yb - ya) * k // n
                if (x, y) in mask:
                    px(im, x, y, C('9a8660') if i % 2 == 0 else C('d8c8a0'))
    # flute channel
    for (x, y), (t, v, hw) in mask.items():
        if .06 <= t <= .62 and abs(v) <= 1.8:
            c = C('a89468')
            if v < -.7:
                c = C('e8dcb8')
            elif v > .9:
                c = C('6e5e44')
            px(im, x, y, c)
    for (x, y), (t, v, hw) in mask.items():   # keen highlight at tip
        if t > .9 and v < 0:
            px(im, x, y, C('fff6dc'))
    return im

# =============================================================== MAP TABLE
@scene('map_table', 1, 'png')
def map_table():
    im = Image.new('RGB', (W, H))
    rng = random.Random(601)
    vgrad(im, 0, 14, [C('1c1622'), C('2e2230')], 3)
    # table top, planks running across
    for y in range(12, H):
        for x in range(W):
            pl = ((y - 12) // 9) % 2
            c = (C('6e4c36'), C('62432f'))[pl]
            if (y - 12) % 9 == 0:
                c = C('2e1e18')
            elif (x * 7 + y * 3 + pl * 5) % 23 == 0:
                c = C('4e3626')
            px(im, x, y, c)
    hline(im, 12, 0, W - 1, C('8a6244'))
    # map sheet (slightly rotated)
    sheet = [(22, 15), (86, 12), (91, 62), (18, 64)]
    poly(im, [(x + 2, y + 2) for x, y in sheet], C('3a2418'))
    poly(im, sheet, C('eadeb6'))
    # inner border and grid
    def lerp2(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    def sp(u, v):   # sheet coords 0..1 -> pixel
        top = lerp2(sheet[0], sheet[1], u); bot = lerp2(sheet[3], sheet[2], u)
        p = lerp2(top, bot, v)
        return int(round(p[0])), int(round(p[1]))
    def sline(u0, v0, u1, v1, c, dash=0):
        xa, ya = sp(u0, v0); xb, yb = sp(u1, v1)
        n = max(abs(xb - xa), abs(yb - ya), 1)
        for k in range(n + 1):
            if dash and (k // dash) % 2:
                continue
            px(im, xa + (xb - xa) * k // n, ya + (yb - ya) * k // n, c)
    for u in (.06, .94):
        sline(u, .06, u, .94, C('4a3a2c'))
    for v in (.06, .94):
        sline(.06, v, .94, v, C('4a3a2c'))
    for u in (.3, .5, .7):
        sline(u, .06, u, .94, C('d0c498'))
    for v in (.35, .65):
        sline(.06, v, .94, v, C('d0c498'))
    # hachured hills (drawn part, left)
    for i in range(18):
        u = .1 + (i % 6) * .055; v = .12 + (i // 6) * .06
        x0, y0 = sp(u, v)
        for k in range(3):
            px(im, x0 + k, y0 + k // 2, C('8a6a48')); px(im, x0 + k, y0 + k // 2 + 2, C('a88a62'))
    # river, drawn from top-left, stops mid-sheet (pen still on it)
    prev = None
    for i in range(0, 58):
        t = i / 57.0
        u = .12 + t * .6; v = .1 + t * .62 + .06 * math.sin(t * 9)
        x, y = sp(u, v)
        px(im, x, y, C('3a5a88')); px(im, x, y + 1, C('3a5a88'))
        if t > .55 and t < .9:
            px(im, x + 1, y, C('3a5a88'))
    sline(.45, .38, .56, .5, C('3a5a88'))      # tributary
    for (u, v) in ((.72, .78), (.82, .5), (.66, .22)):   # blank patches, pencil outlines
        for k in range(8):
            sline(u - .06, v - .05 + k * .014, u + .06, v - .05 + k * .014, C('f2e8c6')) if False else None
        sline(u - .08, v - .08, u + .08, v - .08, C('b0a47c'), 2); sline(u + .08, v - .08, u + .08, v + .08, C('b0a47c'), 2)
        sline(u + .08, v + .08, u - .08, v + .08, C('b0a47c'), 2); sline(u - .08, v + .08, u - .08, v - .08, C('b0a47c'), 2)
    for k in range(6):
        x, y = sp(.12 + k * .02, .86)
        px(im, x, y, C('3a2e28'))
    # pen resting on the river line end
    px0 = sp(.72, .72)
    line(im, px0[0], px0[1], px0[0] + 14, px0[1] - 16, C('2a1e1a'))
    line(im, px0[0] + 1, px0[1], px0[0] + 15, px0[1] - 16, C('8a6244'))
    px(im, px0[0], px0[1], C('d8d0c0'))
    # brass compass, top-right of sheet
    disc(im, 76, 28, 10, 8, C('6a4a1c')); disc(im, 75, 27, 10, 8, C('c89c3c'))
    disc(im, 75, 27, 8, 6, C('f0e6c4'))
    for (dx, dy) in ((0, -5), (7, 0), (0, 5), (-7, 0)):
        px(im, 75 + dx, 27 + dy, C('3a2e24'))
    line(im, 75, 27, 78, 23, C('b83a30')); line(im, 75, 27, 72, 31, C('3a3a4a'))
    px(im, 75, 27, C('c89c3c'))
    for k in range(6):
        px(im, 68 + k, 21 - (k > 2), C('f8e090'))
    # inkwell, lower left
    rect(im, 6, 52, 15, 61, C('1a1e3a')); rect(im, 7, 53, 14, 60, C('2a3262'))
    rect(im, 7, 50, 14, 52, C('c89c3c')); px(im, 8, 54, C('8a9ad0')); px(im, 8, 55, C('8a9ad0')); px(im, 9, 54, C('b4c0e8'))
    line(im, 11, 50, 4, 38, C('d8d0c0')); line(im, 12, 50, 6, 38, C('8a8478'))   # quill
    # brass lamp glow from upper left
    for y in range(0, H):
        for x in range(W):
            d = ((x - 2) ** 2 + (y - 8) ** 2) ** .5 / 80.0
            if d < 1:
                dpx(im, x, y, C('f4b860'), .22 * (1 - d))
            elif d > 1.0:
                dpx(im, x, y, C('120c10'), min(.85, (d - 1) * 1.4))
    # ruler along bottom
    return im

def main():
    n_gif = n_png = 0
    for name, (kind, n, fn) in SCENES.items():
        if kind == 'gif':
            frames = [fn(f, n) for f in range(n)]
            write_gif(name, frames)
            n_gif += 1
        else:
            write_png(name, fn())
            n_png += 1
    print('wrote %d gifs and %d pngs to data/art' % (n_gif, n_png))

if __name__ == '__main__':
    main()
