#!/usr/bin/env python3
"""Decode Crusader cutscene AVIs (Origin RRV1 / RRV2 / JYV1 video codecs) to PNG frames.

Port of ScummVM image/codecs/jyv1.cpp. Audio is plain 8-bit PCM: use ffmpeg for it.
Frame = N u32 block offsets (15 for RRV2, 5 otherwise), each block: u32 cmdLen, cmd bitstream
(MSB-first, 4-bit index -> run length, alternating skip/draw), pixel data follows the cmd bytes.
"""
import struct, sys, os, argparse

BASE_LEN = [0, 1 << 7, 1 << 3, 0, 1 << 1, 0, 1 << 5, 0, 1, 1 << 8, 1 << 4, 0, 1 << 2, 0, 1 << 6, 0]
FINE_BITS = [0, 7, 3, 0, 1, 16, 5, 0, 1, 8, 4, 0, 2, 24, 6, 0]

class Bits:
    __slots__ = ('d', 'pos', 'n')
    def __init__(self, d): self.d = d; self.pos = 0; self.n = len(d) * 8
    def get(self, k):
        v = 0
        for _ in range(k):
            byte = self.d[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v
    def left(self): return self.n - self.pos

def riff_chunks(d, start, end):
    p = start
    while p + 8 <= end:
        cid = d[p:p+4]; sz, = struct.unpack_from('<I', d, p + 4)
        yield cid, p + 8, sz
        p += 8 + sz + (sz & 1)

def parse_avi(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'RIFF' and d[8:12] == b'AVI '
    info = {'streams': [], 'frames': [], 'palette': None}
    def walk(start, end):
        for cid, off, sz in riff_chunks(d, start, end):
            if cid == b'LIST':
                ltype = d[off:off+4]
                if ltype == b'movi':
                    for c2, o2, s2 in riff_chunks(d, off + 4, off + sz):
                        if c2[2:4] in (b'dc', b'db'): info['frames'].append((o2, s2))
                        elif c2[2:4] == b'pc': info['palchunks'] = info.get('palchunks', 0) + 1
                else:
                    walk(off + 4, off + sz)
            elif cid == b'avih':
                us, _, _, _, nframes, _, nstreams, _, w, h = struct.unpack_from('<10I', d, off)
                info.update(us_per_frame=us, nframes=nframes, width=w, height=h)
            elif cid == b'strh':
                info['streams'].append({'type': d[off:off+4], 'handler': d[off+4:off+8]})
            elif cid == b'strf' and info['streams'] and info['streams'][-1]['type'] == b'vids' and info['palette'] is None:
                bisize, bw, bh, planes, bpp = struct.unpack_from('<IiiHH', d, off)
                clrused, = struct.unpack_from('<I', d, off + 32)
                n = clrused or (256 if bpp == 8 else 0)
                pal = []
                for i in range(n):
                    b, g, r, _ = struct.unpack_from('<BBBB', d, off + bisize + 4 * i)
                    pal.append((r, g, b))
                info['palette'] = pal
    walk(12, len(d))
    return d, info

def decode_frame(d, off, size, width, height, tag, dst):
    nofs = 15 if tag == b'RRV2' else 5
    block_h = height // nofs
    offsets = [struct.unpack_from('<I', d, off + 4 * i)[0] + off for i in range(nofs)]
    upscale = False
    if tag in (b'RRV1', b'RRV2'):
        cmdlen, = struct.unpack_from('<I', d, offsets[0])
        bs = Bits(d[offsets[0] + 4: offsets[0] + 4 + cmdlen])
        total = 0
        while bs.left() >= 4:
            idx = bs.get(4); total += BASE_LEN[idx]
            if idx not in (0, 8): total += bs.get(FINE_BITS[idx])
        if total == width * block_h // 2: upscale = True
    x = y = 0
    for i in range(nofs):
        if y >= height: break
        p = offsets[i]
        cmdlen, = struct.unpack_from('<I', d, p); p += 4
        bs = Bits(d[p:p + cmdlen]); p += cmdlen   # pixel data follows command bytes
        skipping = True
        while bs.left() >= 4 and y < height:
            idx = bs.get(4); n = BASE_LEN[idx]
            if idx not in (0, 8): n += bs.get(FINE_BITS[idx])
            if skipping:
                if upscale: n *= 2
                pos = y * width + x + n
                y, x = divmod(pos, width)
            else:
                while n and y < height:
                    v = d[p]; p += 1
                    dst[y * width + x] = v; x += 1
                    if x == width: x = 0; y += 1
                    if upscale:
                        if y < height: dst[y * width + x] = v
                        x += 1
                        if x == width: x = 0; y += 1
                    n -= 1
            skipping = not skipping

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('avi'); ap.add_argument('outdir')
    ap.add_argument('--max', type=int, default=0)
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('--palette', help='768-byte VGA palette to use instead of the AVI palette (in-game JYV1 clips use GAMEPAL.PAL)')
    a = ap.parse_args()
    from PIL import Image
    d, info = parse_avi(a.avi)
    vid = [s for s in info['streams'] if s['type'] == b'vids'][0]
    tag = vid['handler']
    w, h = info['width'], info['height']
    pal = info['palette'] or [(i, i, i) for i in range(256)]
    if a.palette:
        raw = open(a.palette, 'rb').read()[:768]
        pal = [((raw[i*3]*255)//63, (raw[i*3+1]*255)//63, (raw[i*3+2]*255)//63) for i in range(256)]
    print(f"{a.avi}: {tag} {w}x{h} {len(info['frames'])} frames @ {1e6/info['us_per_frame']:.2f} fps, palette {len(pal)}, palchunks {info.get('palchunks',0)}")
    os.makedirs(a.outdir, exist_ok=True)
    dst = bytearray(w * h)
    flat = bytes(c for rgb in pal for c in rgb) + bytes(3 * (256 - len(pal)))
    for i, (off, sz) in enumerate(info['frames']):
        if a.max and i >= a.max: break
        if sz: decode_frame(d, off, sz, w, h, tag, dst)
        img = Image.frombytes('P', (w, h), bytes(dst)); img.putpalette(flat)
        if a.scale > 1: img = img.resize((w * a.scale, h * a.scale), Image.NEAREST)
        img.save(os.path.join(a.outdir, f'{i:05d}.png'))
    print('done')

if __name__ == '__main__':
    main()
