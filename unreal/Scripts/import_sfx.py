"""Import data/extracted/sfx/NNN_NAME.wav as /Game/Crusader/SFX/S_NNN (USoundWave)."""
import unreal, os, re
import os
SRC = os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data', 'extracted', 'sfx'); DEST = '/Game/Crusader/SFX'
tasks = []
for f in sorted(os.listdir(SRC)):
    m = re.match(r'(\d+)_', f)
    if not m or not f.lower().endswith('.wav'): continue
    t = unreal.AssetImportTask(); t.filename = os.path.join(SRC, f); t.destination_path = DEST; t.destination_name = f'S_{int(m.group(1)):03d}'
    t.replace_existing = True; t.automated = True; t.save = True
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
unreal.log(f'[crusader] imported {len(tasks)} sfx')
