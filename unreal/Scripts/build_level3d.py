"""Build a Crusader level as a true 3D scene (dimetric orthographic camera) from the extracted map JSON.

World mapping (see notes/README.md, "3D world"):
  UE X = x, UE Y = y (engine world units), UE Z = z * ZK where ZK = 2*sqrt(2)/cos(30deg) = 3.2660
  Camera: orthographic, Rotator(pitch=-30, yaw=225). 1 sprite pixel = 2*sqrt(2) = 2.8284 units horizontally.
  Non-flat items: PaperSpriteActor billboards facing the camera, scale (2.8284, 1, 3.2660), pivot at (xoff,yoff).
  Flat items (footpad zd == 0, xd,yd > 0): the same sprite laid on the ground, rotated 45 deg and stretched
  2:1 so the diamond image covers the square footprint exactly.
Run: UnrealEditor-Cmd CrusaderRemake.uproject -ExecutePythonScript="build_level3d.py --map 1 --region 2000x12900x1800x1400" -NullRHI
"""
import unreal, json, os, sys, math, argparse

EXTRACT = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted')
ROOT = '/Game/Crusader/Shapes'
FLIPPED = 0x0020
GLOBEGG_FAMILY = 3
PX = 2 * math.sqrt(2)                 # units per sprite pixel, horizontal
ZK = PX / math.cos(math.radians(30))  # units per engine z unit (vertical sprite pixel)
CAM_ROT = unreal.Rotator(pitch=-30, yaw=225, roll=0)

def log(m): unreal.log(f'[crusader] {m}')

def billboard_rotation():
    right = unreal.Vector(1, -1, 0); right.normalize()
    fwd = CAM_ROT.get_forward_vector()
    up = unreal.Vector(0, 0, 1) - fwd * unreal.Vector.dot(unreal.Vector(0, 0, 1), fwd); up.normalize()
    return unreal.MathLibrary.make_rot_from_xz(right, up)

def flat_rotation():
    # image right -> (1,-1,0)/sqrt2, image up -> (-1,-1,0)/sqrt2 (toward far corner), normal up
    right = unreal.Vector(1, -1, 0); right.normalize()
    imgup = unreal.Vector(-1, -1, 0); imgup.normalize()
    return unreal.MathLibrary.make_rot_from_xz(right, imgup)

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', type=int, default=1)
    ap.add_argument('--region', help='screen cx x cy x w x h (engine screen space), default whole map')
    ap.add_argument('--level', default=None)
    ap.add_argument('--no-collision', action='store_true')
    ap.add_argument('--no-roof', action='store_true', help='skip shapes flagged as roof')
    ap.add_argument('--start', default=None, help='screen-space sx x sy for the player start (nearest land tile); default region centre')
    a = ap.parse_args(argv)
    items = json.load(open(os.path.join(EXTRACT, 'maps', f'map{a.map:02d}.json')))
    tf = json.load(open(os.path.join(EXTRACT, 'typeflags.json')))
    manifest = json.load(open(os.path.join(EXTRACT, 'shapes', 'manifest.json')))
    level_path = a.level or f'/Game/Crusader/Maps/Map{a.map:02d}_3D'
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        les.load_level(level_path)
        old = [x for x in eas.get_all_level_actors() if isinstance(x, (unreal.PaperSpriteActor, unreal.CameraActor, unreal.PlayerStart, unreal.StaticMeshActor))]
        if old: eas.destroy_actors(old); log(f'removed {len(old)} actors')
    else:
        les.new_level(level_path)
    region = tuple(map(int, a.region.lower().split('x'))) if a.region else None
    brot = billboard_rotation(); frot = flat_rotation()
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    placed = flats = cols = missing = 0
    missing_shapes = set()
    best_floor = None
    for it in items:
        shape, frame, flags = it['shape'], it['frame'], it['flags']
        info = tf[shape] if shape < len(tf) else None
        if info and info['editor']: continue
        if info and info['family'] == GLOBEGG_FAMILY: continue
        if a.no_roof and info and info['roof']: continue
        x, y, z = it['x'] * 2, it['y'] * 2, it['z']
        sx = x // 4 - y // 4; sy = x // 8 + y // 8 - z
        if region and not (abs(sx - region[0]) <= region[2] // 2 and abs(sy - region[1]) <= region[3] // 2): continue
        flipped = bool(flags & FLIPPED)
        xd, yd, zd = (0, 0, 0) if not info else ((info['y'] * 32, info['x'] * 32, info['z'] * 8) if flipped else (info['x'] * 32, info['y'] * 32, info['z'] * 8))
        sprite = unreal.load_asset(f'{ROOT}/{shape:04d}/S_{shape:04d}_{frame:03d}')
        if not sprite:
            missing += 1; missing_shapes.add(shape); continue
        loc = unreal.Vector(x, y, z * ZK)
        flat = zd == 0 and xd > 0 and yd > 0
        actor = eas.spawn_actor_from_class(unreal.PaperSpriteActor, loc, frot if flat else brot)
        comp = actor.get_editor_property('render_component')
        comp.set_editor_property('source_sprite', sprite)
        if flat:
            # image x: PX units/px; image y: 2*PX (diamond -> square); tiny z lift per item to limit z-fighting
            actor.set_actor_scale3d(unreal.Vector(-PX if flipped else PX, 1, 2 * PX))
            actor.set_actor_location(unreal.Vector(x, y, z * ZK + 0.05 * (flats % 20)), False, False)
            flats += 1
        else:
            actor.set_actor_scale3d(unreal.Vector(-PX if flipped else PX, 1, ZK))
        actor.set_actor_label(f"S{shape}_F{frame}_{x}_{y}_{z}")
        actor.set_folder_path(f"Map{a.map:02d}/{'Flat' if flat else 'Props'}")
        placed += 1
        # collision volume from the footpad (solid, non-flat)
        if not a.no_collision and info and info['solid'] and zd > 0 and xd > 0 and yd > 0 and cube:
            c = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x - xd / 2, y - yd / 2, z * ZK + zd * ZK / 2))
            c.static_mesh_component.set_static_mesh(cube)
            c.set_actor_scale3d(unreal.Vector(xd / 100, yd / 100, zd * ZK / 100))
            c.set_actor_hidden_in_game(True)
            c.static_mesh_component.set_visibility(False)
            c.set_actor_label(f"COL_S{shape}_{x}_{y}_{z}")
            c.set_folder_path(f"Map{a.map:02d}/Collision")
            cols += 1
        # candidate floor for the player start: flat land tile nearest the region centre
        if flat and info and info['land'] and (region or a.start):
            tx, ty = map(int, a.start.lower().split('x')) if a.start else (region[0], region[1])
            d = abs(sx - tx) + abs(sy - ty)
            if best_floor is None or d < best_floor[0]:
                best_floor = (d, x - xd / 2, y - yd / 2, z)
    # camera + player start
    if a.start:
        cx, cy = map(int, a.start.lower().split('x'))
    elif region:
        cx, cy = region[0], region[1]
    else:
        cx = sum(i['x'] * 2 // 4 - i['y'] * 2 // 4 for i in items) // len(items); cy = 0
    if best_floor:
        _, px_, py_, pz_ = best_floor
    else:
        # invert screen->world on the ground plane z=0: x = 2*cx + 4*cy, y = 4*cy - 2*cx
        px_, py_, pz_ = 2 * cx + 4 * cy, 4 * cy - 2 * cx, 0
    # ASilencerPawn's root is a box of half-height 40*ZK; the spawn point is the box centre, so lift it so the feet sit on the floor
    start = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(px_, py_, pz_ * ZK + 40 * ZK + 2))
    start.set_actor_label('PlayerStart_Room1')
    # (no level camera: the player pawn carries the orthographic dimetric camera)
    les.save_current_level()
    log(f'map {a.map}: {placed} sprites ({flats} flat), {cols} collision boxes, {missing} missing (shapes {sorted(missing_shapes)[:20]}); player start at ({px_:.0f},{py_:.0f},{pz_}) level {level_path}')

if __name__ == '__main__':
    main(sys.argv[1:])
