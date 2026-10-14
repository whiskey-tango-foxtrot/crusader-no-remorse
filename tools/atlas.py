#!/usr/bin/env python3
"""Pack the sprite frames used by a level into texture atlases (shelf packing).

usage: atlas.py <map json> <outdir> [--src shapes|shapes_x4] [--size 4096] [--pad 2]
Writes atlas_<n>.png and atlas.json:
  {"size": S, "scale": k, "atlases": ["atlas_0.png", ...],
   "frames": {"<shape>:<frame>": {"atlas": i, "x":, "y":, "w":, "h":, "xoff":, "yoff":}}}
w/h/xoff/yoff are in ORIGINAL pixels (manifest); x,y,w*scale,h*scale locate the frame in the atlas.
"""
import json, os, sys, argparse
from PIL import Image

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('mapjson'); ap.add_argument('outdir')
    ap.add_argument('--src', default='shapes'); ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--size', type=int, default=4096); ap.add_argument('--pad', type=int, default=2)
    ap.add_argument('--extract', default=os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'data', 'extracted'))
    ap.add_argument('--extra', default='', help='extra shape:frame keys (comma list) or shape ranges "1:*" for all frames of a shape')
    ap.add_argument('--auto', action='store_true', help='also pack the avatar, every NPC the map can spawn (MONSTER markers -> dtable shapes) and the combat effect/death shapes')
    a = ap.parse_args()
    items = json.load(open(a.mapjson)); tf = json.load(open(os.path.join(a.extract, 'typeflags.json')))
    manifest = json.load(open(os.path.join(a.extract, 'shapes', 'manifest.json')))
    keys = {(it['shape'], it['frame']) for it in items if not tf[it['shape']]['editor'] and tf[it['shape']]['family'] != 3}
    # every frame of every shape used by the level: usecode changes frames (cameras, alarms, doors) at run time
    for s in {s for s, f in keys}:
        keys.update((s, i) for i, m in enumerate(manifest.get(str(s), [])) if m)
    if a.auto:
        # avatar + NPCs spawned by the MONSTER marker pairs (shape 1232 frame 1 carries the DTABLE index) + effects
        dtable = json.load(open(os.path.join(a.extract, 'dtable.json')))
        auto = {1} | {dtable[it['npc']]['shape'] for it in items if it['shape'] == 1232 and it['frame'] == 1 and it['npc'] < len(dtable)}
        auto |= {0x1d9, 0x1d8, 0x537, 0x326, 0x320, 0x321, 0x31c, 0x31f, 0x322, 0x324, 0x323, 0x325, 0x578, 0x56b, 0x59b, 0x426,
                 0x576, 0x58f, 0x596, 0x59c, 0x278, 0x59a, 0x4cc}   # bullets, splashes, explosions, special death shapes, target reticle, crosshair
        for s_ in auto:
            keys.update((s_, i) for i, m in enumerate(manifest.get(str(s_), [])) if m)
        print(f'auto extras: {len(auto)} shapes')
    for tok in [t for t in a.extra.split(',') if t]:
        s, f = tok.split(':')
        if f == '*': keys.update((int(s), i) for i, m in enumerate(manifest[s]) if m)
        else: keys.add((int(s), int(f)))
    frames = []
    for s, f in keys:
        m = manifest.get(str(s))
        if not m or f >= len(m) or m[f] is None: continue
        p = os.path.join(a.extract, a.src, f'{s:04d}', f'{f:03d}.png')
        if not os.path.exists(p): continue
        frames.append((s, f, m[f], p))
    # shelf packing, tallest first
    frames.sort(key=lambda t: (-t[2]['h'], -t[2]['w']))
    S, pad, k = a.size, a.pad, a.scale
    atlases, out = [], {}
    def new_atlas():
        atlases.append({'img': Image.new('RGBA', (S, S), (0, 0, 0, 0)), 'x': 0, 'y': 0, 'rowh': 0})
    new_atlas()
    for s, f, m, p in frames:
        w, h = m['w'] * k, m['h'] * k
        placed = False
        for ai, at in enumerate(atlases):
            if at['x'] + w + pad > S:
                at['x'] = 0; at['y'] += at['rowh'] + pad; at['rowh'] = 0
            if at['y'] + h + pad > S: continue
            img = Image.open(p).convert('RGBA')
            if img.size != (w, h): img = img.resize((w, h), Image.NEAREST)
            at['img'].paste(img, (at['x'], at['y']))
            out[f'{s}:{f}'] = {'atlas': ai, 'x': at['x'], 'y': at['y'], 'w': m['w'], 'h': m['h'], 'xoff': m['xoff'], 'yoff': m['yoff']}
            at['x'] += w + pad; at['rowh'] = max(at['rowh'], h); placed = True
            break
        if not placed:
            new_atlas(); at = atlases[-1]
            img = Image.open(p).convert('RGBA')
            if img.size != (w, h): img = img.resize((w, h), Image.NEAREST)
            at['img'].paste(img, (0, 0))
            out[f'{s}:{f}'] = {'atlas': len(atlases) - 1, 'x': 0, 'y': 0, 'w': m['w'], 'h': m['h'], 'xoff': m['xoff'], 'yoff': m['yoff']}
            at['x'] = w + pad; at['rowh'] = h
    os.makedirs(a.outdir, exist_ok=True)
    names = []
    for i, at in enumerate(atlases):
        n = f'atlas_{i}.png'; at['img'].save(os.path.join(a.outdir, n)); names.append(n)
    json.dump({'size': S, 'scale': k, 'atlases': names, 'frames': out}, open(os.path.join(a.outdir, 'atlas.json'), 'w'))
    print(f'{len(out)} frames packed into {len(atlases)} atlas(es) of {S}x{S} -> {a.outdir}')

if __name__ == '__main__':
    main()
