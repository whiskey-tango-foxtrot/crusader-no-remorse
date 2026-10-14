#!/usr/bin/env python3
"""Parse Crusader STATIC/ANIM.DAT into JSON.

Layout (ScummVM gfx/anim_dat.cpp, Crusader branch):
  u32 shape_offset[2048]            (0 = no animations for this shape)
  at shape_offset: u32 action_offset[256]  (0 = unused action)
  at action_offset:
      u8 size (frames per direction), u8 flags_lo, u8 repeat_and_rotate, u8 flags_hi
      then for each direction (8, or 16 if flag 0x4000): size * 8-byte frames:
          u8 frame_lo, u8 frame_hi_nibble|flags_nibble, s8 dz, u8 sfx, s8 ddir, u8 flags, u16 flags2
"""
import struct, sys, os, json, argparse

AAF_16DIRS = 0x4000

def parse(data):
    anims = {}
    for shape in range(2048):
        off, = struct.unpack_from('<I', data, 4 * shape)
        if not off: continue
        actions = {}
        for action in range(256):
            aoff, = struct.unpack_from('<I', data, off + 4 * action)
            if not aoff: continue
            size, flags_lo, rep_rot, flags_hi = struct.unpack_from('<BBBB', data, aoff)
            flags = flags_lo | (flags_hi << 8) | ((rep_rot & 0xF0) << 12)
            repeat = rep_rot & 0xF
            if repeat: repeat -= 1   # ScummVM workaround: Crusader wait semantics
            ndirs = 16 if flags & AAF_16DIRS else 8
            p = aoff + 4
            dirs = []
            for d in range(ndirs):
                frames = []
                for j in range(size):
                    f0, x, dz, sfx, ddir, fl, fl2 = struct.unpack_from('<BBbBbBH', data, p); p += 8
                    frames.append({'frame': f0 | ((x & 0xF) << 8), 'dz': dz, 'sfx': sfx, 'ddir': ddir,
                                   'flags': (fl & 0xFE) | ((x & 0xF0) << 8) | (fl2 << 16)})
                dirs.append(frames)
            actions[action] = {'size': size, 'flags': flags, 'repeat': repeat, 'dirs': dirs}
        anims[shape] = actions
    return anims

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('animdat'); ap.add_argument('out')
    a = ap.parse_args()
    anims = parse(open(a.animdat, 'rb').read())
    json.dump(anims, open(a.out, 'w'))
    print(f'{len(anims)} animated shapes')
    for s in list(anims)[:12]:
        acts = anims[s]
        print(f'  shape {s}: {len(acts)} actions, e.g. ' + ', '.join(f'{k}:{v["size"]}x{len(v["dirs"])}' for k, v in list(acts.items())[:6]))

if __name__ == '__main__':
    main()
