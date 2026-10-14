"""Build a Crusader level in Unreal from the extracted map JSON, using imported PaperSprites.

Run inside the editor:
  UnrealEditor-Cmd CrusaderRemake.uproject -ExecutePythonScript="build_level.py --map 1 [--region cx,cy,w,h]"

First pass uses Paper2D's native 2D convention (X right, Z up, Y = depth): each item is placed at
the engine's screen position (screen_x, -screen_y) with Y derived from the engine's draw order,
so the level looks exactly like the original. A true 3D dimetric camera can replace this later.
"""
import unreal, json, os, sys, argparse

EXTRACT = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted')
ROOT = '/Game/Crusader/Shapes'
FLIPPED = 0x0020

def log(msg): unreal.log(f'[crusader] {msg}')

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', type=int, default=1)
    ap.add_argument('--region', help='screen-space cx,cy,w,h; default whole map')
    ap.add_argument('--level', default=None)
    a = ap.parse_args(argv)
    items = json.load(open(os.path.join(EXTRACT, 'maps', f'map{a.map:02d}.json')))
    tf = json.load(open(os.path.join(EXTRACT, 'typeflags.json')))
    level_path = a.level or f'/Game/Crusader/Maps/Map{a.map:02d}'
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not unreal.EditorAssetLibrary.does_asset_exist(level_path):
        les.new_level(level_path)
    else:
        les.load_level(level_path)
    region = tuple(map(int, a.region.split(','))) if a.region else None
    old = [act for act in eas.get_all_level_actors() if isinstance(act, unreal.PaperSpriteActor)]
    if old:
        eas.destroy_actors(old)
        log(f'removed {len(old)} previous sprite actors')
    placed = missing = 0
    missing_shapes = set()
    for it in items:
        x, y, z = it['x'] * 2, it['y'] * 2, it['z']
        info = tf[it['shape']] if it['shape'] < len(tf) else None
        if info and info['editor']:
            continue
        sx = x // 4 - y // 4
        sy = x // 8 + y // 8 - z
        if region and not (region[0] - region[2] // 2 <= sx <= region[0] + region[2] // 2 and region[1] - region[3] // 2 <= sy <= region[1] + region[3] // 2):
            continue
        sprite_path = f"{ROOT}/{it['shape']:04d}/S_{it['shape']:04d}_{it['frame']:03d}"
        sprite = unreal.load_asset(sprite_path)
        if not sprite:
            missing += 1; missing_shapes.add(it['shape']); continue
        # depth: farther (smaller x+y) items further back (larger Y); z breaks ties
        depth = -((x + y) * 0.01 + z * 0.001)
        actor = eas.spawn_actor_from_class(unreal.PaperSpriteActor, unreal.Vector(sx, depth, -sy))
        comp = actor.get_editor_property('render_component')
        comp.set_editor_property('source_sprite', sprite)
        if comp.get_sprite() is None:
            comp.set_sprite(sprite)
        if it['flags'] & FLIPPED:
            actor.set_actor_scale3d(unreal.Vector(-1, 1, 1))
        actor.set_actor_label(f"S{it['shape']}_F{it['frame']}_{x}_{y}_{z}")
        actor.set_folder_path(f"Map{a.map:02d}/Shape{it['shape']:04d}")
        placed += 1
    first = next((act for act in eas.get_all_level_actors() if isinstance(act, unreal.PaperSpriteActor)), None)
    if first:
        log(f"check: {first.get_actor_label()} sprite={first.get_editor_property('render_component').get_sprite()}")
    les.save_current_level()
    log(f'map {a.map}: placed {placed} actors, {missing} items missing sprites (shapes: {sorted(missing_shapes)[:40]})')

if __name__ == '__main__':
    main(sys.argv[1:])
