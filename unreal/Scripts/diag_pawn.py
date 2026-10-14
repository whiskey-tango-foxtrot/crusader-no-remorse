import unreal
for d in range(8):
    p = f'/Game/Crusader/Shapes/0001/F_0001_A000_D{d:02d}'
    fb = unreal.load_asset(p)
    kf = fb.get_editor_property('key_frames') if fb else None
    unreal.log(f'[crusader] {p}: {"ok" if fb else "MISSING"} keyframes={len(kf) if kf else 0} sprite0={kf[0].get_editor_property("sprite") if kf else None}')
fb = unreal.load_asset('/Game/Crusader/Shapes/0001/F_0001_A001_D04')
kf = fb.get_editor_property('key_frames')
unreal.log(f'[crusader] walk D04 keyframes={len(kf)} fps={fb.get_editor_property("frames_per_second")}')
cls = unreal.load_class(None, '/Script/CrusaderRemake.SilencerPawn')
unreal.log(f'[crusader] pawn class: {cls}')
cdo = unreal.get_default_object(cls)
unreal.log(f'[crusader] pawn CDO shape={cdo.get_editor_property("shape")} walk={cdo.get_editor_property("walk_speed")}')
