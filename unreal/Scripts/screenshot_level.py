"""Load a level, aim the editor viewport at a screen-space point and save a high-res screenshot.
  UnrealEditor-Cmd CrusaderRemake.uproject -ExecutePythonScript="screenshot_level.py --level /Game/Crusader/Maps/Map01 --at 2000,12900 --out /tmp/map01.png" -stdout -unattended
(needs a real RHI, do not pass -NullRHI)
"""
import unreal, sys, time, argparse, os

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--level', default='/Game/Crusader/Maps/Map01')
    ap.add_argument('--at', default='2000x12900', help='engine screen_x x screen_y to centre on (no commas: UE ExecCmds splits on them)')
    ap.add_argument('--dist', type=float, default=3000)
    ap.add_argument('--out', default='level_shot.png')
    ap.add_argument('--size', default='1920x1200')
    ap.add_argument('--loc', default=None, help='camera world location XxYxZ (overrides --at)')
    ap.add_argument('--rot', default=None, help='camera rotation pitchxyawxroll, e.g. -30x225x0')
    ap.add_argument('--actor', default=None, help='label of an actor to aim at (camera backs off along -forward by --dist)')
    a = ap.parse_args(argv)
    sx, sy = map(float, a.at.lower().split('x'))
    w, h = map(int, a.size.lower().split('x'))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(a.level):
        unreal.log_error('[crusader] level load failed'); unreal.SystemLibrary.quit_editor(); return
    key = les.get_active_viewport_config_key()
    # Paper2D convention: X right, Z up, Y into the screen. Camera in front (negative Y) looking +Y.
    if a.actor:
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        tgt = next((x for x in eas.get_all_level_actors() if x.get_actor_label() == a.actor), None)
        if tgt is None:
            unreal.log_error(f'[crusader] actor {a.actor} not found'); unreal.SystemLibrary.quit_editor(); return
        rot = unreal.Rotator(pitch=-30, yaw=225, roll=0)
        if a.rot:
            p_, y_, r_ = map(float, a.rot.lower().split('x')); rot = unreal.Rotator(pitch=p_, yaw=y_, roll=r_)
        les.set_level_viewport_camera_info(tgt.get_actor_location() - rot.get_forward_vector() * a.dist, rot, key)
    elif a.loc:
        lx, ly, lz = map(float, a.loc.lower().split('x'))
        p_, y_, r_ = map(float, a.rot.lower().split('x')) if a.rot else (-30.0, 225.0, 0.0)
        les.set_level_viewport_camera_info(unreal.Vector(lx, ly, lz), unreal.Rotator(pitch=p_, yaw=y_, roll=r_), key)
    else:
        les.set_level_viewport_camera_info(unreal.Vector(sx, -a.dist, -sy), unreal.Rotator(pitch=0, yaw=90, roll=0), key)
    if os.path.exists(a.out): os.remove(a.out)
    state = {'phase': 'wait', 'since': time.monotonic(), 'handle': None}
    def tick(_):
        el = time.monotonic() - state['since']
        if state['phase'] == 'wait' and el > 4:
            unreal.AutomationLibrary.finish_loading_before_screenshot()
            unreal.AutomationLibrary.take_high_res_screenshot(w, h, a.out)
            state.update(phase='shot', since=time.monotonic())
        elif state['phase'] == 'shot' and (os.path.exists(a.out) or el > 30):
            unreal.log(f"[crusader] screenshot {'written' if os.path.exists(a.out) else 'TIMED OUT'}: {a.out}")
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal.SystemLibrary.quit_editor()
    state['handle'] = unreal.register_slate_post_tick_callback(tick)

if __name__ == '__main__':
    main(sys.argv[1:])
