#!/usr/bin/env python3
"""Build the atlas and Unreal level for a list of Crusader maps (default: missions 3-15).

Start point: destination teleport egg 30 (the mission start egg, see notes/README.md) if the map has one, else the
lowest-numbered destination egg, else the first floor tile. Logs go to data/extracted/atlas/build_mapNN.log."""
import json, os, subprocess, sys, time
ROOT = os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PY = f'{ROOT}/.venv/bin/python'
UE = os.environ.get('UE_ROOT', '/Users/Shared/Epic Games/UE_5.8') + '/Engine/Binaries/Mac/UnrealEditor-Cmd'
tf = json.load(open(f'{ROOT}/data/extracted/typeflags.json'))

def start_for(m):
    items = json.load(open(f'{ROOT}/data/extracted/maps/map{m:02d}.json'))
    eggs = {(o['quality'] & 0xff): o for o in items if tf[o['shape']]['family'] == 8 and o['frame'] == 1}
    egg = eggs.get(30) or (eggs[min(eggs)] if eggs else None)
    if egg: x, y = egg['x'] * 2, egg['y'] * 2
    else:
        floor = [o for o in items if tf[o['shape']]['land'] and not tf[o['shape']]['editor']]
        o = floor[len(floor) // 2]; x, y = o['x'] * 2, o['y'] * 2
    return (x - y) // 4, (x + y) // 8, ('egg %d' % (egg['quality'] & 0xff)) if egg else 'floor'

maps = [int(a) for a in sys.argv[1:]] or list(range(3, 16))
for m in maps:
    tx, ty, how = start_for(m)
    atlas = f'{ROOT}/data/extracted/atlas/map{m:02d}_x2'
    log = open(f'{ROOT}/data/extracted/atlas/build_map{m:02d}.log', 'w')
    t0 = time.time()
    print(f'map {m}: start {tx}x{ty} ({how})', flush=True)
    r = subprocess.run([PY, f'{ROOT}/tools/atlas.py', f'{ROOT}/data/extracted/maps/map{m:02d}.json', atlas, '--src', 'shapes_x2', '--scale', '2', '--size', '8192', '--pad', '4', '--auto'], stdout=log, stderr=subprocess.STDOUT)
    if r.returncode: print(f'map {m}: atlas FAILED', flush=True); continue
    cmd = f'{ROOT}/unreal/Scripts/build_level3d_batched.py --map {m} --start={tx}x{ty} --atlas {atlas} --level /Game/Crusader/Maps/Map{m:02d}_3D_HD'
    r = subprocess.run([UE, f'{ROOT}/unreal/CrusaderRemake.uproject', f'-ExecutePythonScript={cmd}', '-NullRHI', '-stdout', '-unattended'], stdout=log, stderr=subprocess.STDOUT, cwd=f'{ROOT}/unreal')
    ok = os.path.exists(f'{ROOT}/unreal/Content/Crusader/Maps/Map{m:02d}_3D_HD.umap')
    print(f'map {m}: level {"built" if ok else "FAILED"} in {time.time() - t0:.0f} s', flush=True)
print('all done', flush=True)
