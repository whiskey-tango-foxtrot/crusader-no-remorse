"""Port of ScummVM's Ultima 8 / Crusader sprite draw-order (world/sort_item.cpp, item_sorter.cpp).

Each item gets a world box (x,y,z = near/right/bottom corner, xd,yd,zd = footpad) and a screen rect.
Order: for every pair of items whose screen rects overlap, `below(a, b)` decides which is painted
first; the final order is a DFS over those dependencies (ItemSorter::PaintSortItem).
"""
from collections import defaultdict

class SortItem:
    __slots__ = ('idx', 'x', 'y', 'z', 'xLeft', 'yFar', 'zTop', 'sr', 'flat', 'fbigsq', 'occl', 'solid', 'draw',
                 'roof', 'noisy', 'anim', 'trans', 'fixed', 'land', 'sprite', 'invitem', 'order', 'depends')
    def __init__(self, idx, x, y, z, xd, yd, zd, sr, info, flags, anim=False, trans=False, sprite=False, invitem=False):
        self.idx = idx
        self.x, self.y, self.z = x, y, z
        self.xLeft, self.yFar, self.zTop = x - xd, y - yd, z + zd
        self.sr = sr            # (left, top, right, bottom) screen rect
        self.flat = zd == 0
        self.fbigsq = xd == yd and xd >= 128
        f = info['flags'] if info else 0
        self.solid = bool(f & 0x02); self.land = bool(f & 0x08); self.occl = bool(f & 0x10)
        self.noisy = bool(f & 0x80); self.draw = bool(f & 0x100); self.roof = bool(f & 0x400)
        self.trans = trans or bool(f & 0x800)
        self.anim = anim; self.sprite = sprite; self.invitem = invitem; self.fixed = True
        self.order = -1
        self.depends = []

def overlap(a, b):
    return not (a.sr[2] <= b.sr[0] or b.sr[2] <= a.sr[0] or a.sr[3] <= b.sr[1] or b.sr[3] <= a.sr[1])

def cdiv(a, b):   # C integer division (truncate toward zero)
    q = abs(a) // b
    return q if a >= 0 else -q

def below(si1, si2):
    if si1.sprite != si2.sprite:
        return si1.sprite < si2.sprite
    if si1.flat and si2.flat:
        if si1.z != si2.z: return si1.z < si2.z
    elif si1.invitem == si2.invitem:
        if si1.zTop <= si2.z: return True
        if si1.z >= si2.zTop: return False
    yFlat1 = si1.yFar == si1.y; yFlat2 = si2.yFar == si2.y
    if yFlat1 and yFlat2:
        if cdiv(si1.y, 32) != cdiv(si2.y, 32): return si1.y < si2.y
    else:
        if si1.y <= si2.yFar: return True
        if si1.yFar >= si2.y: return False
    xFlat1 = si1.xLeft == si1.x; xFlat2 = si2.xLeft == si2.x
    if xFlat1 and xFlat2:
        if cdiv(si1.x, 32) != cdiv(si2.x, 32): return si1.x < si2.x
    else:
        if si1.x <= si2.xLeft: return True
        if si1.xLeft >= si2.x: return False
    if si1.zTop - 8 <= si2.z and si1.z < si2.zTop - 8: return True
    if si1.z >= si2.zTop - 8 and si1.zTop - 8 > si2.z: return False
    if yFlat1 != yFlat2:
        if cdiv(si1.y, 32) <= cdiv(si2.yFar, 32): return True
        if cdiv(si1.yFar, 32) >= cdiv(si2.y, 32): return False
        c1 = cdiv(cdiv(si1.yFar, 32) + cdiv(si1.y, 32), 2); c2 = cdiv(cdiv(si2.yFar, 32) + cdiv(si2.y, 32), 2)
        if c1 != c2: return c1 < c2
    if xFlat1 != xFlat2:
        if cdiv(si1.x, 32) <= cdiv(si2.xLeft, 32): return True
        if cdiv(si1.xLeft, 32) >= cdiv(si2.x, 32): return False
        c1 = cdiv(cdiv(si1.xLeft, 32) + cdiv(si1.x, 32), 2); c2 = cdiv(cdiv(si2.xLeft, 32) + cdiv(si2.x, 32), 2)
        if c1 != c2: return c1 < c2
    if si1.flat or si2.flat:
        if si1.z != si2.z: return si1.z < si2.z
        if si1.invitem != si2.invitem: return si1.invitem < si2.invitem
        if si1.flat != si2.flat: return si1.flat > si2.flat
        if si1.trans != si2.trans: return si1.trans < si2.trans
        if si1.anim != si2.anim: return si1.anim < si2.anim
        if si1.draw != si2.draw: return si1.draw > si2.draw
        if si1.solid != si2.solid: return si1.solid > si2.solid
        if si1.occl != si2.occl: return si1.occl > si2.occl
        if si1.fbigsq != si2.fbigsq: return si1.fbigsq > si2.fbigsq
    if si1.x == si2.x and si1.y == si2.y:
        if si1.trans != si2.trans: return si1.trans < si2.trans
    if si1.land and si2.land and si1.roof != si2.roof: return si1.roof < si2.roof
    if si1.roof != si2.roof: return si1.roof > si2.roof
    # final tie-breakers from ScummVM: y, x, z, then item number
    if si1.y != si2.y: return si1.y < si2.y
    if si1.x != si2.x: return si1.x < si2.x
    if si1.z != si2.z: return si1.z < si2.z
    return si1.idx < si2.idx

def sort_items(items, cell=256):
    """items: list of SortItem. Returns them in paint order."""
    grid = defaultdict(list)
    for it in items:
        for gx in range(it.sr[0] // cell, it.sr[2] // cell + 1):
            for gy in range(it.sr[1] // cell, it.sr[3] // cell + 1):
                grid[(gx, gy)].append(it)
    seen = set()
    for bucket in grid.values():
        for i in range(len(bucket)):
            a = bucket[i]
            for j in range(i + 1, len(bucket)):
                b = bucket[j]
                key = (a.idx, b.idx) if a.idx < b.idx else (b.idx, a.idx)
                if key in seen: continue
                seen.add(key)
                if overlap(a, b):
                    if below(a, b): b.depends.append(a)
                    else: a.depends.append(b)
    out = []
    import sys
    sys.setrecursionlimit(100000)
    def paint(si):
        if si.order != -1: return
        si.order = -2   # in progress (cycle guard)
        for d in si.depends:
            if d.order == -1: paint(d)
        si.order = len(out); out.append(si)
    for it in items:
        if it.order == -1: paint(it)
    return out
