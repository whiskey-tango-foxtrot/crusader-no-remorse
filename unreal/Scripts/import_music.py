"""Import data/extracted/music/*.wav (AMF tracker renders) as looping USoundWaves /Game/Crusader/Music/<NAME>."""
import unreal, os
import os
SRC = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted', 'music'); DEST = '/Game/Crusader/Music'
tasks = []
for f in sorted(os.listdir(SRC)):
    if not f.lower().endswith('.wav'): continue
    t = unreal.AssetImportTask(); t.filename = os.path.join(SRC, f); t.destination_path = DEST; t.destination_name = os.path.splitext(f)[0].upper()
    t.replace_existing = True; t.automated = True; t.save = False
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
n = 0
for t in tasks:
    for p in t.get_editor_property('imported_object_paths'):
        sw = unreal.load_asset(p)
        if sw:
            sw.set_editor_property('looping', True)
            unreal.EditorAssetLibrary.save_asset(p); n += 1
unreal.log(f'[crusader] imported {n} music tracks (looping)')
