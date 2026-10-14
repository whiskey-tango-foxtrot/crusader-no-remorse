#!/usr/bin/env python3
"""Dump DTABLE.FLX (Crusader NPC data) to JSON. Layout from ScummVM world/actors/npc_dat.cpp:
object 0 = 142-byte records (min hp, max hp, +24 wpntype2, +26 wpntype, +30 activity6, +62 shape, +64 activity8, +66 activityA),
object 2 = 32-byte names."""
import struct, sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from flx import Flex
fx = Flex(sys.argv[1]); data = fx.get(0); names = fx.get(2)
out = []
for i in range(len(data) // 142):
    r = data[i*142:(i+1)*142]
    name = names[i*32:(i+1)*32].split(b'\0')[0].decode('latin1') if (i+1)*32 <= len(names) else ''
    out.append({'name': name, 'min_hp': struct.unpack_from('<H', r, 0)[0], 'max_hp': struct.unpack_from('<H', r, 2)[0],
                'wpn_type2': struct.unpack_from('<H', r, 24)[0], 'wpn_type': struct.unpack_from('<H', r, 26)[0],
                'activity6': struct.unpack_from('<H', r, 30)[0], 'shape': struct.unpack_from('<H', r, 62)[0],
                'activity8': struct.unpack_from('<H', r, 64)[0], 'activityA': struct.unpack_from('<H', r, 66)[0]})
json.dump(out, open(sys.argv[2], 'w'), indent=0)
print(len(out), 'npc entries')
for i, e in enumerate(out[:12]): print(i, e)
