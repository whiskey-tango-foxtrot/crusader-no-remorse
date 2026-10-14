#!/usr/bin/env python3
"""Crusader: No Remorse shape (sprite) decoder.

Format (ScummVM convert/crusader/convert_shape_crusader.cpp, CrusaderShapeFormat):
  shape header : 4 bytes unknown, uint16 frame count
  frame table  : per frame 8 bytes = uint24 offset, uint8 unk, uint32 length
  frame header : [8 bytes unknown  -- absent for 2D (gump/mouse/font) shapes]
                 uint32 compressed, int32 width, int32 height, int32 xoff, int32 yoff
                 uint32 line_offset[height]  (relative: subtract (height-i)*4)
  rle          : per line: repeat { skip:u8, len:u8, pixels... } until x >= width
                 if compressed: len&1 -> run of one pixel value, len>>=1
"""
import struct, sys, os, json, argparse
sys.path.insert(0, os.path.dirname(__file__))
from flx import Flex

def load_palette(path):
    raw = open(path, 'rb').read()[:768]
    # VGA 6-bit -> 8-bit
    return [((raw[i*3] * 255) // 63, (raw[i*3+1] * 255) // 63, (raw[i*3+2] * 255) // 63) for i in range(256)]

class Frame:
    __slots__ = ('width', 'height', 'xoff', 'yoff', 'compressed', 'pixels')

def _frame_header_size(data, foff):
    """Decide whether this frame uses the 28-byte (world) or 20-byte (2D) header."""
    for unk in (8, 0):
        try:
            comp, w, h, xo, yo = struct.unpack_from('<IiiII', data, foff + unk)
        except struct.error:
            continue
        if comp in (0, 1) and 0 < w <= 4096 and 0 < h <= 4096:
            return unk
    return None

def decode_frame(data, foff, flen, keycolor=0xFF):
    unk = _frame_header_size(data, foff)
    if unk is None:
        return None
    comp, w, h, xo, yo = struct.unpack_from('<IiiII', data, foff + unk)
    xo = struct.unpack('<i', struct.pack('<I', xo))[0]
    yo = struct.unpack('<i', struct.pack('<I', yo))[0]
    p = foff + unk + 20
    line_offs = [struct.unpack_from('<I', data, p + 4*i)[0] - (h - i) * 4 for i in range(h)]
    rle = p + 4 * h
    pix = bytearray([keycolor]) * (w * h)
    for y in range(h):
        q = rle + line_offs[y]
        x = 0
        while x < w:
            x += data[q]; q += 1
            if x >= w: break
            dlen = data[q]; q += 1
            typ = 0
            if comp:
                typ = dlen & 1
                dlen >>= 1
            row = y * w
            if typ:
                v = data[q]; q += 1
                for i in range(dlen):
                    if x + i < w: pix[row + x + i] = v
            else:
                n = min(dlen, w - x)
                pix[row + x: row + x + n] = data[q:q + n]
                q += dlen
            x += dlen
    f = Frame()
    f.width, f.height, f.xoff, f.yoff, f.compressed, f.pixels = w, h, xo, yo, comp, bytes(pix)
    return f

def decode_shape(data):
    if len(data) < 6:
        return []
    nframes, = struct.unpack_from('<H', data, 4)
    frames = []
    for i in range(nframes):
        e = 6 + 8 * i
        if e + 8 > len(data): break
        off = data[e] | (data[e+1] << 8) | (data[e+2] << 16)
        flen, = struct.unpack_from('<I', data, e + 4)
        frames.append(decode_frame(data, off, flen))
    return frames

def frame_to_image(f, pal, keycolor=0xFF):
    from PIL import Image
    img = Image.new('RGBA', (f.width, f.height))
    px = img.load()
    for y in range(f.height):
        for x in range(f.width):
            v = f.pixels[y * f.width + x]
            px[x, y] = (0, 0, 0, 0) if v == keycolor else (*pal[v], 255)
    return img

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('flx')
    ap.add_argument('palette')
    ap.add_argument('outdir')
    ap.add_argument('--shapes', help='comma list or a-b range', default=None)
    ap.add_argument('--sheet', action='store_true', help='one sprite sheet per shape instead of per-frame PNGs')
    a = ap.parse_args()
    fx = Flex(a.flx)
    pal = load_palette(a.palette)
    os.makedirs(a.outdir, exist_ok=True)
    if a.shapes:
        if '-' in a.shapes:
            lo, hi = a.shapes.split('-'); ids = range(int(lo), int(hi) + 1)
        else:
            ids = [int(s) for s in a.shapes.split(',')]
    else:
        ids = range(len(fx))
    manifest = {}
    from PIL import Image
    for sid in ids:
        data = fx.get(sid)
        if not data: continue
        frames = decode_shape(data)
        if not frames: continue
        meta = []
        sdir = os.path.join(a.outdir, f'{sid:04d}')
        if not a.sheet: os.makedirs(sdir, exist_ok=True)
        imgs = []
        for fi, f in enumerate(frames):
            if f is None:
                meta.append(None); continue
            img = frame_to_image(f, pal)
            meta.append({'w': f.width, 'h': f.height, 'xoff': f.xoff, 'yoff': f.yoff})
            if a.sheet: imgs.append(img)
            else: img.save(os.path.join(sdir, f'{fi:03d}.png'))
        if a.sheet and imgs:
            cols = min(len(imgs), 16); rows = (len(imgs) + cols - 1) // cols
            cw = max(i.width for i in imgs); ch = max(i.height for i in imgs)
            sheet = Image.new('RGBA', (cols * cw, rows * ch))
            for i, img in enumerate(imgs):
                sheet.paste(img, ((i % cols) * cw, (i // cols) * ch))
            sheet.save(os.path.join(a.outdir, f'{sid:04d}.png'))
        manifest[sid] = meta
        print(f'shape {sid}: {len(frames)} frames, first {meta[0]}')
    with open(os.path.join(a.outdir, 'manifest.json'), 'w') as fh:
        json.dump(manifest, fh)

if __name__ == '__main__':
    main()
