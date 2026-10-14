"""Import extracted Crusader assets into the Unreal project.

Run inside the editor (headless):
  UnrealEditor-Cmd CrusaderRemake.uproject -run=pythonscript -script="import_crusader.py <args>"

Creates, under /Game/Crusader/Shapes/<NNNN>/:
  T_<NNNN>_<FFF>   Texture2D (nearest filter, no mips, uncompressed) per frame
  S_<NNNN>_<FFF>   PaperSprite per frame, pivot set from the shape's xoff/yoff so the sprite
                   origin is the engine's "near-right-bottom" anchor point
  F_<NNNN>_A<aaa>_D<dd>  PaperFlipbook per animation action & direction (from anim.json)
Pixels per unit: 1 (sprite pixel == 1 UE unit); see level builder for the world->UE transform.
"""
import unreal, json, os, sys, argparse

EXTRACT = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted')
ROOT = '/Game/Crusader/Shapes'
SRC = 'shapes'      # or shapes_x4 (set by --src)
SCALE = 1.0         # texture pixels per original pixel (4 for shapes_x4)

def log(msg): unreal.log(f'[crusader] {msg}')

def import_texture(png, dest_path, name):
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = dest_path
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(f'{dest_path}/{name}')
    if tex:
        tex.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST if SCALE == 1.0 else unreal.TextureFilter.TF_BILINEAR)
        tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)  # uncompressed RGBA
        tex.set_editor_property('srgb', True)
        tex.set_editor_property('never_stream', True)
    return tex

def make_sprite(tex, dest_path, name, w, h, xoff, yoff):
    path = f'{dest_path}/{name}'
    sprite = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if sprite is None:
        sprite = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, dest_path, unreal.PaperSprite, unreal.PaperSpriteFactory())
    if sprite is None:
        log(f'could not create sprite {path}'); return None
    sprite.set_editor_property('source_texture', tex)
    sprite.set_editor_property('source_uv', unreal.Vector2D(0, 0))
    sprite.set_editor_property('source_dimension', unreal.Vector2D(w, h))
    sprite.set_editor_property('pivot_mode', unreal.SpritePivotMode.CUSTOM)
    # Engine anchor is at (xoff, yoff) from the frame's top-left, in pixels.
    sprite.set_editor_property('custom_pivot_point', unreal.Vector2D(xoff, yoff))
    sprite.set_editor_property('pixels_per_unreal_unit', SCALE)
    sprite.set_editor_property('sprite_collision_domain', unreal.SpriteCollisionMode.NONE)
    return sprite

def make_flipbook(dest_path, name, sprites, fps):
    path = f'{dest_path}/{name}'
    fb = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if fb is None:
        fb = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, dest_path, unreal.PaperFlipbook, unreal.PaperFlipbookFactory())
    if fb is None:
        log(f'could not create flipbook {path}'); return None
    keys = []
    for s in sprites:
        k = unreal.PaperFlipbookKeyFrame()
        k.set_editor_property('sprite', s)
        k.set_editor_property('frame_run', 1)
        keys.append(k)
    fb.set_editor_property('key_frames', keys)
    fb.set_editor_property('frames_per_second', fps)
    return fb

def import_shape(shape_id, manifest, anims, with_flipbooks=True, actions=None):
    sdir = os.path.join(EXTRACT, SRC, f'{shape_id:04d}')
    if not os.path.isdir(sdir):
        log(f'shape {shape_id}: no extracted frames'); return
    meta = manifest.get(str(shape_id)) or manifest.get(shape_id)
    dest = f'{ROOT}/{shape_id:04d}'
    sprites = {}
    wanted = None
    if actions is not None and str(shape_id) in anims:
        wanted = {f['frame'] for act, a_ in anims[str(shape_id)].items() if int(act) in actions for d in a_['dirs'] for f in d}
    for fi, m in enumerate(meta):
        if m is None: continue
        if wanted is not None and fi not in wanted: continue
        png = os.path.join(sdir, f'{fi:03d}.png')
        if not os.path.exists(png): continue
        tex = import_texture(png, dest, f'T_{shape_id:04d}_{fi:03d}')
        if not tex: continue
        sp = make_sprite(tex, dest, f'S_{shape_id:04d}_{fi:03d}', m['w'] * SCALE, m['h'] * SCALE, m['xoff'] * SCALE, m['yoff'] * SCALE)
        if sp: sprites[fi] = sp
    log(f'shape {shape_id}: {len(sprites)} sprites')
    if with_flipbooks and str(shape_id) in anims:
        n = 0
        for action, act in anims[str(shape_id)].items():
            if actions is not None and int(action) not in actions: continue
            for d, frames in enumerate(act['dirs']):
                seq = [sprites[f['frame']] for f in frames if f['frame'] in sprites]
                if not seq: continue
                # engine runs animation at ~15 fps; repeat>0 means each frame is held longer
                fps = 15.0 / (1 + act['repeat'])
                make_flipbook(dest, f'F_{shape_id:04d}_A{int(action):03d}_D{d:02d}', seq, fps)
                n += 1
        log(f'shape {shape_id}: {n} flipbooks')
    unreal.EditorAssetLibrary.save_directory(dest, only_if_is_dirty=False, recursive=True)

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--shapes', default='1', help='comma list or a-b range')
    ap.add_argument('--no-flipbooks', action='store_true')
    ap.add_argument('--skip-existing', action='store_true', help='skip shapes whose folder already has assets')
    ap.add_argument('--actions', default=None, help='only import frames/flipbooks of these animation actions (comma list), e.g. 0,1,3')
    ap.add_argument('--src', default='shapes', help='extracted frame set: shapes (original) or shapes_x4 (upscaled)')
    ap.add_argument('--scale', type=float, default=1.0, help='pixel scale of --src relative to the original (4 for shapes_x4)')
    a = ap.parse_args(argv)
    global SRC, SCALE
    SRC, SCALE = a.src, a.scale
    manifest = json.load(open(os.path.join(EXTRACT, 'shapes', 'manifest.json')))
    anims = json.load(open(os.path.join(EXTRACT, 'anim.json')))
    if '-' in a.shapes:
        lo, hi = a.shapes.split('-'); ids = range(int(lo), int(hi) + 1)
    else:
        ids = [int(s) for s in a.shapes.split(',')]
    for sid in ids:
        if a.skip_existing and unreal.EditorAssetLibrary.does_directory_exist(f'{ROOT}/{sid:04d}'):
            continue
        import_shape(sid, manifest, anims, not a.no_flipbooks, [int(x) for x in a.actions.split(',')] if a.actions else None)
    log('done')

if __name__ == '__main__':
    main(sys.argv[1:])
