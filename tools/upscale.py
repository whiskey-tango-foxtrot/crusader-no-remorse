#!/usr/bin/env python3
"""Upscale extracted sprite frames for the remake (chosen pipeline, see notes/upscale_comparison.png).

  upscale.py --out shapes_x2 [--keys atlas.json|map.json] [--shapes 1,2,10-20] [--scale 2]

Per frame: (1) fill transparent pixels with the nearest opaque colour so the model cannot bleed the key colour into the
edges, (2) Real-ESRGAN animevideov3-x4 on the opaque RGB, (3) alpha upscaled 4x with Scale2x twice (binary, no halo),
(4) Lanczos downsample to --scale (default 2) and write data/extracted/<out>/NNNN/FFF.png.
The model binary runs on whole directories, so frames are staged in batches of --batch files.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXTRACT = f'{ROOT}/data/extracted'
BIN = f'{ROOT}/tools/upscaler/realesrgan-ncnn-vulkan'
MODELS = f'{ROOT}/tools/upscaler/models'

def scale2x(a):
    P = np.pad(a, ((1, 1), (1, 1)), mode='edge')
    E = P[1:-1, 1:-1]; A = P[:-2, 1:-1]; D = P[1:-1, :-2]; F = P[1:-1, 2:]; C = P[2:, 1:-1]
    out = np.repeat(np.repeat(E, 2, 0), 2, 1).copy()
    cond = (A != C) & (D != F)
    out[0::2, 0::2] = np.where((A == D) & cond, D, E); out[0::2, 1::2] = np.where((A == F) & cond, F, E)
    out[1::2, 0::2] = np.where((C == D) & cond, D, E); out[1::2, 1::2] = np.where((C == F) & cond, F, E)
    return out

def fill_transparent(rgba):
    rgb, a = rgba[..., :3], rgba[..., 3]
    opaque = a > 0
    if opaque.all() or not opaque.any(): return rgb
    _, (iy, ix) = ndimage.distance_transform_edt(~opaque, return_indices=True)
    return rgb[iy, ix]

def frame_keys(a):
    keys = set()
    if a.keys:
        d = json.load(open(a.keys))
        if isinstance(d, dict) and 'frames' in d:
            keys.update(tuple(map(int, k.split(':'))) for k in d['frames'])
        else:
            tf = json.load(open(f'{EXTRACT}/typeflags.json'))
            keys.update((it['shape'], it['frame']) for it in d if not tf[it['shape']]['editor'])
    if a.shapes:
        man = json.load(open(f'{EXTRACT}/shapes/manifest.json'))
        for tok in a.shapes.split(','):
            lo, hi = (tok.split('-') + [tok])[:2] if '-' in tok else (tok, tok)
            for s in range(int(lo), int(hi) + 1):
                keys.update((s, f) for f, m in enumerate(man.get(str(s), [])) if m)
    if a.all:
        man = json.load(open(f'{EXTRACT}/shapes/manifest.json'))
        for s, frames in man.items():
            keys.update((int(s), f) for f, m in enumerate(frames) if m)
    return sorted(keys)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='shapes_x2'); ap.add_argument('--scale', type=int, default=2)
    ap.add_argument('--keys', default=None, help='atlas.json (frames dict) or map json (items)')
    ap.add_argument('--shapes', default=None); ap.add_argument('--all', action='store_true')
    ap.add_argument('--model', default='realesr-animevideov3-x4'); ap.add_argument('--batch', type=int, default=400)
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    keys = frame_keys(a)
    outdir = f'{EXTRACT}/{a.out}'
    todo = [(s, f) for s, f in keys if a.force or not os.path.exists(f'{outdir}/{s:04d}/{f:03d}.png')]
    print(f'{len(keys)} frames, {len(todo)} to do', flush=True)
    for b0 in range(0, len(todo), a.batch):
        batch = todo[b0:b0 + a.batch]
        with tempfile.TemporaryDirectory(prefix='ups_') as tmp:
            src, dst = f'{tmp}/in', f'{tmp}/out'; os.makedirs(src); os.makedirs(dst)
            alphas = {}
            for s, f in batch:
                p = f'{EXTRACT}/shapes/{s:04d}/{f:03d}.png'
                if not os.path.exists(p): continue
                rgba = np.asarray(Image.open(p).convert('RGBA'))
                Image.fromarray(fill_transparent(rgba)).save(f'{src}/{s:04d}_{f:03d}.png')
                alphas[(s, f)] = rgba[..., 3]
            r = subprocess.run([BIN, '-i', src, '-o', dst, '-n', a.model, '-s', '4', '-m', MODELS, '-f', 'png'], capture_output=True, text=True)
            if r.returncode != 0:
                print('upscaler failed:', r.stderr[-500:]); sys.exit(1)
            for (s, f), alpha in alphas.items():
                up = f'{dst}/{s:04d}_{f:03d}.png'
                if not os.path.exists(up): print('missing output', up); continue
                rgb = np.asarray(Image.open(up).convert('RGB'))
                a4 = scale2x(scale2x((alpha > 0).astype(np.uint8))) * 255
                if a4.shape != rgb.shape[:2]:
                    a4 = np.asarray(Image.fromarray(a4).resize((rgb.shape[1], rgb.shape[0]), Image.NEAREST))
                im = Image.fromarray(np.dstack([rgb, a4.astype(np.uint8)]))
                if a.scale != 4:
                    k = a.scale / 4
                    # premultiply so the colour does not bleed from (filled) transparent texels, then unpremultiply
                    arr = np.asarray(im).astype(np.float32); arr[..., :3] *= arr[..., 3:4] / 255
                    pm = Image.fromarray(arr.astype(np.uint8)).resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
                    arr = np.asarray(pm).astype(np.float32); al = np.clip(arr[..., 3:4], 1, 255)
                    arr[..., :3] = np.clip(arr[..., :3] * 255 / al, 0, 255)
                    im = Image.fromarray(arr.astype(np.uint8))
                os.makedirs(f'{outdir}/{s:04d}', exist_ok=True)
                im.save(f'{outdir}/{s:04d}/{f:03d}.png')
        print(f'  {min(b0 + a.batch, len(todo))}/{len(todo)}', flush=True)
    print('done')

if __name__ == '__main__':
    main()
