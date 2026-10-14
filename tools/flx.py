#!/usr/bin/env python3
"""Ultima 8 / Crusader 'Flex' archive reader.

Layout (from ScummVM engines/ultima/ultima8/filesys/flex_file.cpp):
  0x00..0x52  comment, padded with 0x1A
  0x54        uint32 entry count
  0x58        uint32 version (1)
  0x80        table: count * (uint32 offset, uint32 size)
"""
import struct, sys, os, argparse

HDR_SIZE = 0x52
TABLE_OFF = 0x80

class Flex:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        if len(d) < TABLE_OFF or d[HDR_SIZE-1] != 0x1A:
            raise ValueError(f'{path}: not a flex file')
        self.comment = d[:HDR_SIZE].split(b'\x1a', 1)[0].decode('latin1')
        self.count, = struct.unpack_from('<I', d, HDR_SIZE + 2)
        self.entries = [struct.unpack_from('<II', d, TABLE_OFF + 8*i) for i in range(self.count)]

    def get(self, i):
        off, size = self.entries[i]
        return self.data[off:off+size] if size else b''

    def __len__(self): return self.count

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('file')
    ap.add_argument('-x', '--extract', metavar='DIR', help='extract all entries to DIR')
    ap.add_argument('-l', '--list', action='store_true')
    a = ap.parse_args()
    fx = Flex(a.file)
    nonempty = sum(1 for _, s in fx.entries if s)
    print(f'{a.file}: comment={fx.comment!r} entries={fx.count} nonempty={nonempty} size={len(fx.data)}')
    if a.list:
        for i, (o, s) in enumerate(fx.entries):
            if s:
                print(f'  [{i:5d}] off=0x{o:08x} size={s:8d} head={fx.get(i)[:16].hex()}')
    if a.extract:
        os.makedirs(a.extract, exist_ok=True)
        for i, (o, s) in enumerate(fx.entries):
            if s:
                with open(os.path.join(a.extract, f'{i:04d}.bin'), 'wb') as f:
                    f.write(fx.get(i))

if __name__ == '__main__':
    main()
