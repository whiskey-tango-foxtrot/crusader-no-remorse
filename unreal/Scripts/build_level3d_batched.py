"""Build a Crusader level as a few instanced meshes (one per texture atlas) instead of thousands of actors.

Requires: tools/atlas.py output for the map (data/extracted/atlas/mapNN/atlas.json) and the C++ ACrusaderSpriteBatch.
  UnrealEditor-Cmd CrusaderRemake.uproject -ExecutePythonScript="build_level3d_batched.py --map 1 --start 2100x13100" -NullRHI

Geometry: instances of /Engine/BasicShapes/Plane (100x100 in XY, normal +Z). Instance local X = image right,
local Y = image up. Billboards: X = screen right, Y = camera up. Flats: X = (1,-1,0)/sqrt2, Y = (-1,-1,0)/sqrt2.
Long items (footprint >= 2:1 and >= 64 units): X = wall direction tilted to project as screen right, Y = camera up
(see aligned_basis). Editor-only solid shapes (invisible walls) get collision but no sprite. Frame-cycling shapes
(typeflag anim) go in a Sprites_Anim batch with one instance per frame (ACrusaderSpriteBatch::Anims).
Material M_CrusaderAtlas: unlit masked two-sided; UV = TexCoord * (cd2, cd3) + (cd0, cd1); opacity *= cd4.
"""
import unreal, json, os, sys, math, argparse

EXTRACT = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted')
FLIPPED = 0x0020
GLOBEGG_FAMILY = 3
PX = 2 * math.sqrt(2)
ZK = PX / math.cos(math.radians(30))
CAM_ROT = unreal.Rotator(pitch=-30, yaw=225, roll=0)
MAT_DIR = '/Game/Crusader/Materials'

def log(m): unreal.log(f'[crusader] {m}')

def ensure_material():
    path = f'{MAT_DIR}/M_CrusaderAtlas'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset('M_CrusaderAtlas', MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('used_with_instanced_static_meshes', True)   # levels opened at run time get no auto usage fix-up
    mat.set_editor_property('opacity_mask_clip_value', 0.5)
    mel = unreal.MaterialEditingLibrary
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -500, 0)
    tex.set_editor_property('parameter_name', 'Atlas')
    tex.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    cds = []
    for i in range(5):
        cd = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -1300, -200 + 120 * i)
        cd.set_editor_property('data_index', i); cds.append(cd)
    tc = mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1300, 450)
    scale = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -1100, 100)
    mel.connect_material_expressions(cds[2], '', scale, 'A'); mel.connect_material_expressions(cds[3], '', scale, 'B')
    offs = mel.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -1100, -150)
    mel.connect_material_expressions(cds[0], '', offs, 'A'); mel.connect_material_expressions(cds[1], '', offs, 'B')
    mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, 300)
    mel.connect_material_expressions(tc, '', mul, 'A'); mel.connect_material_expressions(scale, '', mul, 'B')
    add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -750, 100)
    mel.connect_material_expressions(mul, '', add, 'A'); mel.connect_material_expressions(offs, '', add, 'B')
    mel.connect_material_expressions(add, '', tex, 'UVs')
    vis = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -250, 200)
    mel.connect_material_expressions(tex, 'A', vis, 'A'); mel.connect_material_expressions(cds[4], '', vis, 'B')
    mel.connect_material_property(tex, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.connect_material_property(vis, '', unreal.MaterialProperty.MP_OPACITY_MASK)
    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(path)
    return mat

def import_atlas_texture(png, name, scale=1):
    dest = f'{MAT_DIR}/Atlas'
    path = f'{dest}/{name}'
    task = unreal.AssetImportTask()
    task.filename = png; task.destination_path = dest; task.destination_name = name
    task.replace_existing = True; task.automated = True; task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(path)
    # 1x pixel art: uncompressed + nearest. Upscaled art: BC7 (4:1, visually lossless here) + bilinear.
    tex.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST if scale == 1 else unreal.TextureFilter.TF_BILINEAR)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON if scale == 1 else unreal.TextureCompressionSettings.TC_BC7)
    tex.set_editor_property('srgb', True); tex.set_editor_property('never_stream', True)
    unreal.EditorAssetLibrary.save_asset(path)
    return tex

def make_instance(mat, tex, name):
    path = f'{MAT_DIR}/{name}'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property('parent', mat)
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, 'Atlas', tex)
    unreal.EditorAssetLibrary.save_asset(path)
    return mi

def rot_from_xy(x, y):
    x = unreal.Vector(*x); y = unreal.Vector(*y); x.normalize(); y.normalize()
    return unreal.MathLibrary.make_rot_from_xy(x, y)

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', type=int, default=1)
    ap.add_argument('--start', default=None, help='screen sx x sy for the player start')
    ap.add_argument('--region', default=None, help='cx x cy x w x h screen-space filter')
    ap.add_argument('--level', default=None)
    ap.add_argument('--atlas', default=None, help='atlas dir (default data/extracted/atlas/mapNN)')
    a = ap.parse_args(argv)
    adir = a.atlas or os.path.join(EXTRACT, 'atlas', f'map{a.map:02d}')
    atlas = json.load(open(os.path.join(adir, 'atlas.json')))
    S, K = atlas['size'], atlas['scale']
    items = json.load(open(os.path.join(EXTRACT, 'maps', f'map{a.map:02d}.json')))
    tf = json.load(open(os.path.join(EXTRACT, 'typeflags.json')))
    level_path = a.level or f'/Game/Crusader/Maps/Map{a.map:02d}_3D'
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        les.load_level(level_path)
        old = [x for x in eas.get_all_level_actors() if isinstance(x, (unreal.PaperSpriteActor, unreal.CameraActor, unreal.PlayerStart, unreal.StaticMeshActor, unreal.CrusaderSpriteBatch, unreal.CrusaderLevelInfo))]
        if old: eas.destroy_actors(old); log(f'removed {len(old)} actors')
    else:
        les.new_level(level_path)
    mat = ensure_material()
    plane = unreal.load_asset('/Engine/BasicShapes/Plane')
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    # one batch per atlas for props, one per atlas for roofs, one collision batch
    batches = {}
    def get_batch(ai, roof, animated=False):
        key = (ai, roof, animated)
        if key in batches: return batches[key]
        b = eas.spawn_actor_from_class(unreal.CrusaderSpriteBatch, unreal.Vector(0, 0, 0))
        b.set_actor_label(f"Sprites_{'Roof_' if roof else ''}{'Anim_' if animated else ''}Atlas{ai}")
        b.set_editor_property('roof_layer', roof)
        b.set_editor_property('atlas_index', ai)
        m = b.get_editor_property('mesh')
        m.set_static_mesh(plane)
        tag = f'{a.map:02d}' + (f'x{K}' if K > 1 else '')
        tex = import_atlas_texture(os.path.join(adir, atlas['atlases'][ai]), f'T_Map{tag}_Atlas{ai}', K)
        m.set_material(0, make_instance(mat, tex, f'MI_Map{tag}_Atlas{ai}'))
        batches[key] = b
        return b
    col = eas.spawn_actor_from_class(unreal.CrusaderSpriteBatch, unreal.Vector(0, 0, 0))
    col.set_actor_label('Collision')
    col.set_editor_property('collision', True)
    cm = col.get_editor_property('mesh'); cm.set_static_mesh(cube)
    cm.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    cm.set_collision_profile_name('BlockAll'); cm.set_visibility(False); cm.set_hidden_in_game(True)
    # import every atlas texture/material up front: runtime-created items (NPCs) may live in atlases no static item uses
    for ai in range(len(atlas['atlases'])):
        tag = f'{a.map:02d}' + (f'x{K}' if K > 1 else '')
        make_instance(mat, import_atlas_texture(os.path.join(adir, atlas['atlases'][ai]), f'T_Map{tag}_Atlas{ai}', K), f'MI_Map{tag}_Atlas{ai}')
    region = tuple(map(int, a.region.lower().split('x'))) if a.region else None
    brot = rot_from_xy((1, -1, 0), (-0.5 * math.sqrt(2) * math.sin(math.radians(30)), -0.5 * math.sqrt(2) * math.sin(math.radians(30)), math.cos(math.radians(30))))
    frot = rot_from_xy((1, -1, 0), (-1, -1, 0))
    right = unreal.Vector(1, -1, 0); right.normalize()
    bup = CAM_ROT.get_forward_vector(); bup = unreal.Vector(0, 0, 1) - bup * unreal.Vector.dot(unreal.Vector(0, 0, 1), bup); bup.normalize()
    fup = unreal.Vector(-1, -1, 0); fup.normalize()
    placed = flats = roofs = cols = missing = anims = 0
    best = None
    tx, ty = map(int, a.start.lower().split('x')) if a.start else (region[0], region[1]) if region else (None, None)
    roof_bounds, roof_z, inst_z, batch_anims = {}, {}, {}, {}
    # Wall-aligned sprite planes: a billboard's pixels all sit at the anchor's depth, so a long wall drawn as a
    # billboard hides anything standing beside its far end. For items much longer along one axis the quad is
    # instead spanned by the wall direction e and the camera up vector: r = a*e + b*U with a = PX/(R.e),
    # b = -a*(e.U) projects to exactly one screen pixel right per unit and is orthogonal to U (no shear), and the
    # plane contains the item's bottom edge, so depth along the wall is exact.
    cam_up = bup
    def aligned_basis(ex, ey):
        e = unreal.Vector(ex, ey, 0); e.normalize()
        av = PX / unreal.Vector.dot(right, e)
        bv = -av * unreal.Vector.dot(e, cam_up)
        r = e * av + cam_up * bv
        ln = r.length(); r = r / ln
        return r, ln, rot_from_xy((r.x, r.y, r.z), (cam_up.x, cam_up.y, cam_up.z))
    xwall = aligned_basis(-1, 0); ywall = aligned_basis(0, -1)
    col_items = []
    def add_collision(x, y, z, xd, yd, zd, item_index):
        # zero-thickness long walls get a 16-unit slab centred on their face (short ones, e.g. fully open doors, do not block)
        if xd == 0 and yd >= 128: x += 8; xd = 16
        if yd == 0 and xd >= 128: y += 8; yd = 16
        if xd <= 0 or yd <= 0: return False
        cm.add_instance(unreal.Transform(unreal.Vector(x - xd / 2, y - yd / 2, z * ZK + zd * ZK / 2), unreal.Rotator(0, 0, 0), unreal.Vector(xd / 100, yd / 100, zd * ZK / 100)), True)
        col_items.append(item_index)
        return True
    batch_items = {}
    def place(fr, x, y, z, flipped, xd, yd, zd, flat, roof, b, nflat, item_index=-1):
        """Add one instance for atlas frame fr anchored at (x, y, z); returns the instance index."""
        w, h, xo, yo = fr['w'], fr['h'], fr['xoff'], fr['yoff']
        anchor = unreal.Vector(x, y, z * ZK)
        if flat:
            sxu, syu = PX, 2 * PX; r = right; u = fup; rot = frot
            loc = unreal.Vector(x, y, z * ZK + 0.05 * (nflat % 20))
        elif xd >= 2 * yd and xd >= 64:
            r, sxu, rot = xwall; syu = ZK; u = cam_up; loc = anchor
        elif yd >= 2 * xd and yd >= 64:
            r, sxu, rot = ywall; syu = ZK; u = cam_up; loc = anchor
        else:
            sxu, syu = PX, ZK; r = right; u = cam_up; rot = brot; loc = anchor
        # centre of the quad: anchor + right*(w/2 - xoff)*sxu + up*(yoff - h/2)*syu   (flipped: mirror the x offset)
        cx = (w / 2 - xo) * sxu * (-1 if flipped else 1)
        cy = (yo - h / 2) * syu
        centre = loc + r * cx + u * cy
        scale = unreal.Vector((w * sxu / 100.0) * (-1 if flipped else 1), h * syu / 100.0, 1)
        m = b.get_editor_property('mesh')
        idx = m.add_instance(unreal.Transform(centre, rot, scale), True)
        # the engine Plane's V runs opposite to local +Y (image up), so address the rect bottom-up
        u0, v0 = fr['x'] / S, (fr['y'] + fr['h'] * K) / S
        du, dv = fr['w'] * K / S, -(fr['h'] * K / S)
        for di, val in enumerate((u0, v0, du, dv, 1.0)):
            m.set_custom_data_value(idx, di, val, False)
        inst_z.setdefault(b, []).append(float(z * ZK))
        batch_items.setdefault(b, []).append(item_index)
        if roof:
            roof_bounds.setdefault(b, []).append(unreal.Vector4(x - xd, y - yd, x, y)); roof_z.setdefault(b, []).append(z * ZK)
        return idx
    for item_index, it in enumerate(items):
        shape, frame, flags = it['shape'], it['frame'], it['flags']
        info = tf[shape] if shape < len(tf) else None
        if info and info['family'] == GLOBEGG_FAMILY: continue
        x, y, z = it['x'] * 2, it['y'] * 2, it['z']
        sx = x // 4 - y // 4; sy = x // 8 + y // 8 - z
        if region and not (abs(sx - region[0]) <= region[2] // 2 and abs(sy - region[1]) <= region[3] // 2): continue
        flipped = bool(flags & FLIPPED)
        xd, yd, zd = (0, 0, 0) if not info else ((info['y'] * 32, info['x'] * 32, info['z'] * 8) if flipped else (info['x'] * 32, info['y'] * 32, info['z'] * 8))
        if info and info['solid'] and zd > 0 and (xd > 0 or yd > 0):
            if add_collision(x, y, z, xd, yd, zd, item_index): cols += 1
        if info and (info['editor'] or info['family'] in (3, 4, 7, 8)): continue    # invisible walls, eggs (glob/unk/monster/teleport), markers: collision only
        fr = atlas['frames'].get(f'{shape}:{frame}')
        if not fr: missing += 1; continue
        flat = zd == 0 and xd > 0 and yd > 0
        roof = bool(info and info['roof'])
        anim_type = info.get('anim_type', 0) if info else 0
        animated = anim_type in (1, 2, 3, 4, 6)
        b = get_batch(fr['atlas'], roof, animated)
        if animated:
            # one instance per frame of the shape; the batch toggles which one is visible
            insts = []
            nframes = max(int(k.split(':')[1]) for k in atlas['frames'] if k.startswith(f'{shape}:')) + 1
            for f in range(nframes):
                ffr = atlas['frames'].get(f'{shape}:{f}')
                insts.append(place(ffr, x, y, z, flipped, xd, yd, zd, flat, roof, b, flats, item_index) if ffr else -1)
                if ffr: placed += 1
            an = unreal.CrusaderSpriteAnim()
            an.set_editor_property('type', anim_type); an.set_editor_property('data', info.get('anim_data', 0))
            an.set_editor_property('speed', info.get('anim_speed', 1)); an.set_editor_property('frame', frame if frame < nframes else 0)
            an.set_editor_property('instances', insts)
            batch_anims.setdefault(b, []).append(an); anims += 1
        else:
            place(fr, x, y, z, flipped, xd, yd, zd, flat, roof, b, flats, item_index)
            placed += 1
        if roof: roofs += 1
        flats += 1 if flat else 0
        if flat and info and info['land'] and tx is not None:
            d = abs(sx - tx) + abs(sy - ty)
            if best is None or d < best[0]: best = (d, x - xd / 2, y - yd / 2, z)
    for b, lst in batch_anims.items():
        b.set_editor_property('anims', lst)
    for b, lst in batch_items.items():
        b.set_editor_property('instance_item', lst)
    col.set_editor_property('instance_item', col_items)
    info_actor = eas.spawn_actor_from_class(unreal.CrusaderLevelInfo, unreal.Vector(0, 0, 0))
    info_actor.set_actor_label('LevelInfo')
    info_actor.set_editor_property('map_num', a.map)
    info_actor.set_editor_property('data_dir', os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data'))
    info_actor.set_editor_property('atlas_json', os.path.join(adir, 'atlas.json'))
    for b, lst in roof_bounds.items():
        b.set_editor_property('roof_bounds', lst); b.set_editor_property('roof_z', roof_z[b])
    for b, lst in inst_z.items():
        b.set_editor_property('instance_z', lst)
    px_, py_, pz_ = (best[1], best[2], best[3]) if best else (2 * tx + 4 * ty, 4 * ty - 2 * tx, 0)
    start = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(px_, py_, pz_ * ZK + 40 * ZK + 2))
    start.set_actor_label('PlayerStart_Room1')
    les.save_current_level()
    log(f'map {a.map}: {placed} instances ({flats} flat, {roofs} roof, {anims} animated items) in {len(batches)} batches, {cols} collision boxes, {missing} missing; start ({px_:.0f},{py_:.0f},{pz_})')

if __name__ == '__main__':
    main(sys.argv[1:])
