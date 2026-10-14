#!/usr/bin/env python3
"""Dump Crusader FIXED.DAT (static level geometry) and GLOB.FLX (prefab groups) to JSON.

FIXED.DAT is a Flex; entry N = map N. Each item is 16 bytes
(ScummVM world/map.cpp Map::loadFixedFormatObjects):
  u16 x, u16 y, u8 z, u16 shape, u8 frame, u16 flags, u16 quality, u8 npcnum, u8 mapnum, u16 next
Container nesting: x field holds container depth for contained items.
GLOB.FLX entry = u16 count, then count * (u8 x, u8 y, u8 z, u16 shape, u8 frame).
"""
import struct, sys, os, json, argparse
sys.path.insert(0, os.path.dirname(__file__))
from flx import Flex

def parse_fixed(data):
    items = []
    for i in range(len(data) // 16):
        x, y, z, shape, frame, flags, quality, npc, mapnum, nxt = struct.unpack_from('<HHBHBHHBBH', data, i * 16)
        items.append({'x': x, 'y': y, 'z': z, 'shape': shape, 'frame': frame,
                      'flags': flags, 'quality': quality, 'npc': npc, 'map': mapnum, 'next': nxt})
    return items

GLOBEGG_FAMILY = 3

def expand_globs(items, globs, typeflags):
    """Append the contents of every glob egg (ScummVM world/glob_egg.cpp, Crusader variant).
    Items are in FIXED.DAT units (half world units): world = 2*x. In world units the egg's position is masked
    to 1024 and glob offsets are (gx << 2) + 2, so in file units: base = (x*2 & ~0x3FF)/2, off = gx*2 + 1."""
    out = []
    for it in items:
        it = dict(it); it['glob'] = False
        out.append(it)
        tf = typeflags[it['shape']] if it['shape'] < len(typeflags) else None
        if tf and tf['family'] == GLOBEGG_FAMILY:
            g = globs.get(it['quality'])
            if not g: continue
            bx = ((it['x'] * 2) & ~0x3FF) // 2
            by = ((it['y'] * 2) & ~0x3FF) // 2
            for gi in g:
                out.append({'x': bx + gi['x'] * 2 + 1, 'y': by + gi['y'] * 2 + 1, 'z': it['z'] + gi['z'],
                            'shape': gi['shape'], 'frame': gi['frame'], 'flags': 0, 'quality': 0, 'npc': 0,
                            'map': 0, 'next': 0, 'glob': True, 'globidx': it['quality']})
    return out

def parse_glob(data):
    n, = struct.unpack_from('<H', data, 0)
    return [dict(zip(('x', 'y', 'z', 'shape', 'frame'), struct.unpack_from('<BBBHB', data, 2 + 6 * i))) for i in range(n)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('static_dir')
    ap.add_argument('outdir')
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    fixed = Flex(os.path.join(a.static_dir, 'FIXED.DAT'))
    globs_fx = Flex(os.path.join(a.static_dir, 'GLOB.FLX'))
    gl = {g: parse_glob(globs_fx.get(g)) for g in range(len(globs_fx)) if globs_fx.get(g)}
    sys.path.insert(0, os.path.dirname(__file__))
    from render_map import load_typeflags
    tf = load_typeflags(os.path.join(a.static_dir, 'TYPEFLAG.DAT'))
    summary = {}
    for m in range(len(fixed)):
        d = fixed.get(m)
        if not d: continue
        items = expand_globs(parse_fixed(d), gl, tf)
        neggs = sum(1 for i in items if i['glob'])
        xs = [i['x'] for i in items]; ys = [i['y'] for i in items]; zs = [i['z'] for i in items]
        shapes = sorted({i['shape'] for i in items})
        summary[m] = {'items': len(items), 'glob_items': neggs, 'x': [min(xs), max(xs)], 'y': [min(ys), max(ys)], 'z': [min(zs), max(zs)],
                      'distinct_shapes': len(shapes)}
        with open(os.path.join(a.outdir, f'map{m:02d}.json'), 'w') as fh:
            json.dump(items, fh)
        print(f'map {m:3d}: {len(items):6d} items ({neggs} from globs)  x {min(xs)}-{max(xs)}  y {min(ys)}-{max(ys)}  z {min(zs)}-{max(zs)}  shapes {len(shapes)}')
    with open(os.path.join(a.outdir, 'globs.json'), 'w') as fh:
        json.dump(gl, fh)
    print(f'{len(gl)} globs')
    with open(os.path.join(a.outdir, 'summary.json'), 'w') as fh:
        json.dump(summary, fh, indent=1)

if __name__ == '__main__':
    main()
