import unreal, sys
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les.load_level('/Game/Crusader/Maps/Map01')
actors = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.PaperSpriteActor)]
unreal.log(f'[crusader] sprite actors: {len(actors)}')
xs=[a.get_actor_location().x for a in actors]; zs=[a.get_actor_location().z for a in actors]; ys=[a.get_actor_location().y for a in actors]
unreal.log(f'[crusader] X {min(xs):.0f}..{max(xs):.0f}  Y {min(ys):.0f}..{max(ys):.0f}  Z {min(zs):.0f}..{max(zs):.0f}')
for a in actors[:3]:
    c = a.get_editor_property('render_component')
    s = c.get_sprite()
    unreal.log(f'[crusader] {a.get_actor_label()} loc={a.get_actor_location()} sprite={s.get_name() if s else None} mat={c.get_material(0)} vis={c.is_visible()} hidden={a.is_hidden_ed()}')
    if s:
        unreal.log(f"[crusader]   sprite default_material={s.get_editor_property('default_material')} tex={s.get_editor_property('source_texture')} dim={s.get_editor_property('source_dimension')} bounds={s.get_editor_property('render_bounds') if hasattr(s,'render_bounds') else '?'}")
