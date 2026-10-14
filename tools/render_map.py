#!/usr/bin/env python3
"""Render a Crusader level (FIXED.DAT) to PNG using the engine's isometric projection.

Projection (ScummVM world/sort_item.cpp + item_sorter.cpp):
  world x,y from FIXED.DAT are doubled for Crusader; footpad = typeflag x,y * 32, z * 8
  screen_x = x/4 - y/4           (origin = item's near-right-bottom corner)
  screen_y = x/8 + y/8 - z
  sprite top-left = (screen_x - xoff, screen_y - yoff); flipped: left = screen_x + xoff - width
Draw order here is an approximation (far-to-near by x+y, then z); the real engine uses a
dependency sort (SortItem::below). Good enough for validation renders.
"""
import struct, sys, os, json, argparse
sys.path.insert(0, os.path.dirname(__file__))
from flx import Flex
from shape import decode_shape, load_palette, frame_to_image
from sorter import SortItem, sort_items

FLIPPED = 0x0020   # Item::FLG_FLIPPED (ScummVM world/item.h)

def load_typeflags(path):
    d = open(path, 'rb').read()
    out = []
    for i in range(len(d) // 9):
        b = d[9*i:9*i+9]
        flags = b[0] | (b[1] << 8)
        x = ((b[3] << 3) | (b[2] >> 5)) & 0x1F
        y = (b[3] >> 2) & 0x1F
        z = ((b[4] << 1) | (b[3] >> 7)) & 0x1F
        # Crusader layout (ScummVM gfx/type_flags.cpp): anim type = b4 high nibble, anim data = b5 low, anim speed = b5 high
        # (speed 0 -> 1); Item::animateItem runs every anim_speed game ticks (30 ticks/s). Types 1/3 cycle, 2 random,
        # 4 random start, 5 usecode, 6 loop 1..n.
        out.append({'flags': flags, 'family': (b[1] >> 4) | ((b[2] & 1) << 4), 'x': x, 'y': y, 'z': z,
                    'roof': bool(b[1] & 0x04), 'solid': bool(b[0] & 0x02), 'land': bool(b[0] & 0x08),
                    'editor': bool(b[6] & 0x01), 'anim': (b[4] >> 4) != 0,
                    'anim_type': b[4] >> 4, 'anim_data': b[5] & 0x0F, 'anim_speed': (b[5] >> 4) or 1,
                    'b6': b[6], 'weight': b[7], 'volume': b[8]})
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('static_dir'); ap.add_argument('map', type=int); ap.add_argument('out')
    ap.add_argument('--region', help='screen-space crop cx,cy,w,h (centre + size); default: centre of all items, 1600x1200')
    ap.add_argument('--no-roof', action='store_true', help='skip shapes flagged as roof')
    ap.add_argument('--maxz', type=int, default=None, help='skip items above this z')
    ap.add_argument('--editor', action='store_true', help='draw editor-only shapes (invisible walls etc.)')
    ap.add_argument('--scale', type=int, default=1, help='downscale factor (1 = full size)')
    ap.add_argument('--full', action='store_true', help='render the whole map (use with --scale)')
    ap.add_argument('--simple-sort', action='store_true', help='use naive x+y,z order instead of the engine sort')
    a = ap.parse_args()
    from PIL import Image
    fixed = Flex(os.path.join(a.static_dir, 'FIXED.DAT'))
    shapes = Flex(os.path.join(a.static_dir, 'SHAPES.FLX'))
    pal = load_palette(os.path.join(a.static_dir, 'GAMEPAL.PAL'))
    tf = load_typeflags(os.path.join(a.static_dir, 'TYPEFLAG.DAT'))
    from map import parse_fixed, parse_glob, expand_globs, GLOBEGG_FAMILY
    globs_fx = Flex(os.path.join(a.static_dir, 'GLOB.FLX'))
    gl = {g: parse_glob(globs_fx.get(g)) for g in range(len(globs_fx)) if globs_fx.get(g)}
    raw = expand_globs(parse_fixed(fixed.get(a.map)), gl, tf)
    items = []
    wc = {}
    for i, it in enumerate(raw):
        x, y, z, shape, frame, flags = it['x'], it['y'], it['z'], it['shape'], it['frame'], it['flags']
        x *= 2; y *= 2
        info = tf[shape] if shape < len(tf) else None
        if a.no_roof and info and info['roof']: continue
        if not a.editor and info and info['editor']: continue
        if info and info['family'] == GLOBEGG_FAMILY: continue
        if a.maxz is not None and z > a.maxz: continue
        sx = x // 4 - y // 4
        sy = x // 8 + y // 8 - z
        items.append((x + y, z, i, sx, sy, shape, frame, flags))
        wc[i] = (x, y, z)
    sc = a.scale
    if a.full:
        xs = [it[3] for it in items]; ys = [it[4] for it in items]
        left, top = min(xs) - 300, min(ys) - 300
        w, h = (max(xs) - left + 300) // sc, (max(ys) - top + 300) // sc
        cx, cy = left + w * sc // 2, top + h * sc // 2
    elif a.region:
        cx, cy, w, h = map(int, a.region.split(','))
    else:
        from collections import Counter
        w, h = 1600, 1200
        c = Counter((it[3] // 800, it[4] // 600) for it in items)
        (gx, gy), _ = c.most_common(1)[0]
        cx, cy = gx * 800 + 400, gy * 600 + 300
    left, top = cx - (w * sc) // 2, cy - (h * sc) // 2
    img = Image.new('RGBA', (w, h), (0, 0, 0, 255))
    cache = {}
    draw_list = []
    for _, _, idx, sx, sy, shape, frame, flags in items:
        if shape not in cache:
            cache[shape] = decode_shape(shapes.get(shape)) if shapes.get(shape) else []
        frames = cache[shape]
        if frame >= len(frames) or frames[frame] is None: continue
        f = frames[frame]
        px = sx + f.xoff - f.width if flags & FLIPPED else sx - f.xoff
        py = sy - f.yoff
        if px + f.width < left or px > left + w * sc or py + f.height < top or py > top + h * sc: continue
        info = tf[shape] if shape < len(tf) else None
        x, y, z = wc[idx]
        if info:
            xd, yd, zd = (info['y'] * 32, info['x'] * 32, info['z'] * 8) if flags & FLIPPED else (info['x'] * 32, info['y'] * 32, info['z'] * 8)
        else:
            xd = yd = zd = 0
        si = SortItem(idx, x, y, z, xd, yd, zd, (px, py, px + f.width, py + f.height), info, flags,
                      anim=bool(info and info.get('anim')))
        draw_list.append((si, f, shape, frame, flags, px, py))
    if a.simple_sort:
        ordered = draw_list
    else:
        order = {si.idx: n for n, si in enumerate(sort_items([d[0] for d in draw_list]))}
        ordered = sorted(draw_list, key=lambda d: order[d[0].idx])
    drawn = 0
    for si, f, shape, frame, flags, px, py in ordered:
        key = (shape, frame, bool(flags & FLIPPED))
        if key not in cache:
            im = frame_to_image(f, pal)
            if flags & FLIPPED: im = im.transpose(Image.FLIP_LEFT_RIGHT)
            if sc > 1: im = im.resize((max(1, im.width // sc), max(1, im.height // sc)), Image.NEAREST)
            cache[key] = im
        img.alpha_composite(cache[key], ((px - left) // sc, (py - top) // sc))
        drawn += 1
    img.save(a.out)
    print(f'map {a.map}: {len(items)} items, {drawn} drawn into {w}x{h} at centre ({cx},{cy}) -> {a.out}')

if __name__ == '__main__':
    main()
