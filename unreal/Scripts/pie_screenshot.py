"""Start play-in-editor on a level, wait, take a screenshot through the game camera, stop, quit.
  UnrealEditor CrusaderRemake.uproject -ExecCmds="py pie_screenshot.py --level /Game/Crusader/Maps/Map01_3D --wait 4" -unattended
Screenshot lands in <project>/Saved/Screenshots/MacEditor/ (HighResShot).
"""
import unreal, sys, time, argparse, glob, os

def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument('--level', default='/Game/Crusader/Maps/Map01_3D')
    ap.add_argument('--wait', type=float, default=4.0)
    ap.add_argument('--size', default='1920x1200')
    ap.add_argument('--move', default=None, help="scripted input before the shot: XxYxSECS[xRUN] steps joined by _ e.g. 0x1x2_-1x0x2x1 (no commas: ExecCmds splits on them)")
    ap.add_argument('--settle', type=float, default=0.0, help='seconds to stand still after --move before the shot')
    ap.add_argument('--precmd', default=None, help="console commands before the move: joined by _ with = for spaces e.g. Silencer.NoAnim=1_Silencer.Face=2")
    ap.add_argument('--postcmd', default=None, help='console commands after the move (same syntax)')
    ap.add_argument('--then-delay', type=float, default=0.5, help='seconds between the --then commands and the second shot')
    ap.add_argument('--then', default=None, help='console commands (same syntax) to run after the first shot, followed by a second shot')
    ap.add_argument('--then2', default=None, help='a second group of console commands run --then-delay after --then; the second shot follows after --then2-delay')
    ap.add_argument('--then2-delay', type=float, default=1.0)
    ap.add_argument('--shot', default='hires', help='hires (HighResShot WxH) or plain (Shot = the live viewport frame)')
    a = ap.parse_args(argv)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(a.level):
        unreal.log_error('[crusader] level load failed'); unreal.SystemLibrary.quit_editor(); return
    shots_dir = os.path.join(unreal.Paths.project_saved_dir(), 'Screenshots')
    before = set(glob.glob(os.path.join(shots_dir, '**', '*.png'), recursive=True))
    state = {'phase': 'start', 'since': time.monotonic(), 'handle': None}
    def finish(msg):
        unreal.log(f'[crusader] {msg}')
        unreal.unregister_slate_post_tick_callback(state['handle'])
        if les.is_in_play_in_editor(): les.editor_request_end_play()
        unreal.SystemLibrary.quit_editor()
    def camera_diag(world):
        try:
            cm = unreal.GameplayStatics.get_player_camera_manager(world, 0)
            unreal.log(f'[crusader] camera manager camloc={cm.get_camera_location()} camrot={cm.get_camera_rotation()} fov={cm.get_fov_angle()}')
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            unreal.log(f'[crusader] view target={pc.get_view_target()} world={world.get_name()}')
            pawn_cls = unreal.load_class(None, '/Script/CrusaderRemake.SilencerPawn')
            for pw in unreal.GameplayStatics.get_all_actors_of_class(world, pawn_cls):
                cam = pw.get_editor_property('camera')
                unreal.log(f'[crusader] pawn loc={pw.get_actor_location()} camera world loc={cam.get_world_location()} rot={cam.get_world_rotation()} ortho={cam.get_editor_property("projection_mode")} width={cam.get_editor_property("ortho_width")} near={cam.get_editor_property("ortho_near_clip_plane")} far={cam.get_editor_property("ortho_far_clip_plane")} active={cam.is_active()}')
        except Exception as e:
            unreal.log(f'[crusader] camera diag ERR {e!r}')
    def tick(_):
        try:
            el = time.monotonic() - state['since']
            ph = state['phase']
            if ph == 'start' and el > 2:
                les.editor_request_begin_play(); state.update(phase='playwait', since=time.monotonic())
            elif ph == 'playwait':
                if les.is_in_play_in_editor(): state.update(phase='playing', since=time.monotonic())
                elif el > 60: finish('PIE never started')
            elif ph == 'playing' and (a.move or a.precmd or a.postcmd) and el > 1.0 and state.get('moves') is None:
                state['moves'] = a.move.lower().split('_') if a.move else []
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                for c in (a.precmd.split('_') if a.precmd else []):
                    unreal.log(f'[crusader] precmd {c.replace("=", " ")}'); unreal.SystemLibrary.execute_console_command(world, c.replace('=', ' '))
                state.update(phase='nextmove', since=time.monotonic())
            elif ph == 'nextmove':
                if state['moves']:
                    mv = state['moves'].pop(0).split('x')
                    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                    unreal.SystemLibrary.execute_console_command(world, f'Silencer.Move {mv[0]} {mv[1]} {mv[2]} {mv[3] if len(mv) > 3 else 0}')
                    unreal.log(f'[crusader] scripted move {mv}')
                    state.update(phase='moving', since=time.monotonic(), movesecs=float(mv[2]))
                else:
                    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                    for c in (a.postcmd.split('_') if a.postcmd else []):
                        unreal.log(f'[crusader] postcmd {c.replace("=", " ")}'); unreal.SystemLibrary.execute_console_command(world, c.replace('=', ' '))
                    state.update(phase='playing', since=time.monotonic() - a.wait + a.settle)   # --settle also holds before the shot when there are no moves
            elif ph == 'moving' and el > state['movesecs'] + a.settle:
                state.update(phase='nextmove', since=time.monotonic())
            elif ph == 'playing' and el > a.wait:
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                pawn_cls = unreal.load_class(None, '/Script/CrusaderRemake.SilencerPawn')
                pawns = unreal.GameplayStatics.get_all_actors_of_class(world, pawn_cls)
                unreal.log(f'[crusader] pawns in PIE world: {len(pawns)}')
                for pw in pawns:
                    sp = pw.get_editor_property('sprite')
                    info = {'loc': pw.get_actor_location()}
                    for name, fn in [('flipbook', lambda: sp.get_flipbook()), ('visible', lambda: sp.is_visible()),
                                     ('scale', lambda: sp.get_editor_property('relative_scale3d')), ('rot', lambda: sp.get_editor_property('relative_rotation')),
                                     ('relloc', lambda: sp.get_editor_property('relative_location')), ('hidden', lambda: pw.is_hidden()),
                                     ('playing', lambda: sp.is_playing())]:
                        try: info[name] = fn()
                        except Exception as e: info[name] = f'ERR {e}'
                    unreal.log(f'[crusader] pawn {info}')
                    try:
                        pc = unreal.GameplayStatics.get_player_controller(world, 0)
                        spos = pc.project_world_location_to_screen(sp.get_world_location(), False)
                        apos = pc.project_world_location_to_screen(pw.get_actor_location(), False)
                        vx, vy = unreal.WidgetLayoutLibrary.get_viewport_size(world).x, unreal.WidgetLayoutLibrary.get_viewport_size(world).y
                        unreal.log(f'[crusader] projected sprite anchor={spos} actor={apos} viewport={vx}x{vy}')
                    except Exception as e:
                        unreal.log(f'[crusader] project ERR {e!r}')
                    try:
                        unreal.log(f'[crusader] pawn sprite bounds={sp.get_editor_property("bounds") if hasattr(sp, "bounds") else "?"} material0={sp.get_material(0)} flipbook_sprite0={sp.get_flipbook().get_sprite_at_frame(0)} tex={sp.get_flipbook().get_sprite_at_frame(0).get_editor_property("source_texture")}')
                    except Exception as e:
                        unreal.log(f'[crusader] bounds ERR {e}')
                    # control experiment: a plain PaperSpriteActor at the pawn's anchor with the level's billboard transform
                    try:
                        lvl = [x for x in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PaperSpriteActor)]
                        ref = lvl[0]
                        unreal.log(f'[crusader] level sprite example rot={ref.get_actor_rotation()} scale={ref.get_actor_scale3d()} mat={ref.get_editor_property("render_component").get_material(0)}')
                        anchor = sp.get_world_location()
                        test = world.spawn_actor_from_class(unreal.PaperSpriteActor, anchor + unreal.Vector(-200, 200, 0), ref.get_actor_rotation()) if hasattr(world, 'spawn_actor_from_class') else None
                        if test is None:
                            test = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PaperSpriteActor, anchor + unreal.Vector(-200, 200, 0), ref.get_actor_rotation())
                        test.get_editor_property('render_component').set_sprite(unreal.load_asset('/Game/Crusader/Shapes/0001/S_0001_004'))
                        test.set_actor_scale3d(ref.get_actor_scale3d())
                        unreal.log(f'[crusader] spawned control sprite at {test.get_actor_location()} world={test.get_world()} sprite={test.get_editor_property("render_component").get_sprite()}')
                    except Exception as e:
                        unreal.log(f'[crusader] control ERR {e!r}')
                camera_diag(world)
                unreal.SystemLibrary.execute_console_command(world, f'HighResShot {a.size}' if a.shot == 'hires' else 'Shot')
                state.update(phase='shot', since=time.monotonic())
            elif ph == 'shot':
                new = set(glob.glob(os.path.join(shots_dir, '**', '*.png'), recursive=True)) - before
                if new and a.then and not state.get('then_done'):
                    state['then_done'] = True
                    unreal.log(f'[crusader] screenshot {sorted(new)[-1]}')
                    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                    for c in a.then.split('_'):
                        unreal.log(f'[crusader] then {c.replace("=", " ")}'); unreal.SystemLibrary.execute_console_command(world, c.replace('=', ' '))
                    state.update(phase='shot2wait', since=time.monotonic(), before2=set(glob.glob(os.path.join(shots_dir, '**', '*.png'), recursive=True)))
                elif new: finish(f'screenshot {sorted(new)[-1]}')
                elif el > 30: finish('screenshot TIMED OUT')
            elif ph == 'shot2wait' and el > a.then_delay:
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                if a.then2 and not state.get('then2_done'):
                    state['then2_done'] = True
                    for c in a.then2.split('_'):
                        unreal.log(f'[crusader] then2 {c.replace("=", " ")}'); unreal.SystemLibrary.execute_console_command(world, c.replace('=', ' '))
                    a.then_delay = a.then2_delay
                    state.update(since=time.monotonic())
                    return
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(); camera_diag(world)
                unreal.SystemLibrary.execute_console_command(world, f'HighResShot {a.size}' if a.shot == 'hires' else 'Shot')
                state.update(phase='shot2', since=time.monotonic())
            elif ph == 'shot2':
                new = set(glob.glob(os.path.join(shots_dir, '**', '*.png'), recursive=True)) - state['before2']
                if new: finish(f'screenshot2 {sorted(new)[-1]}')
                elif el > 30: finish('screenshot2 TIMED OUT')
        except Exception as e:
            finish(f'error {e!r}')
    state['handle'] = unreal.register_slate_post_tick_callback(tick)

if __name__ == '__main__':
    main(sys.argv[1:])
