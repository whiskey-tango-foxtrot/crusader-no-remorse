"""Headless usecode run: usecode_test.py <map> <ticks> <sx> <sy> <sz> <ex> <ey> <ez> <walkticks> [traceclasses] [useAtTick] [fireAtTick] [fireDir16] [weaponShape] [infiniteAmmo]
  weaponShape: Silencer.Weapon cheat applied one tick before fireAtTick (e.g. 815 = BA-41, 819 = RP-22); infiniteAmmo 1 = never consume ammo / energy
  UnrealEditor-Cmd CrusaderRemake.uproject -ExecutePythonScript="usecode_test.py 1 900 56750 48270 0 57246 48802 0 300" -NullRHI"""
import os
import unreal, sys
a = sys.argv[1:]
m, ticks = int(a[0]), int(a[1]); s = unreal.Vector(float(a[2]), float(a[3]), float(a[4])); e = unreal.Vector(float(a[5]), float(a[6]), float(a[7]))
walk = int(a[8]); trace = a[9] if len(a) > 9 else ''; use_at = int(a[10]) if len(a) > 10 else 0
fire_at = int(a[11]) if len(a) > 11 else 0; fire_dir = int(a[12]) if len(a) > 12 else -1
weapon = int(a[13], 0) if len(a) > 13 else 0; inf_ammo = len(a) > 14 and a[14] not in ('0', '')
r = unreal.CrusaderUsecodeTest.run_headless(os.path.join(os.environ.get('CRUSADER_ROOT', os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))), 'data'), m, ticks, s, e, walk, trace, use_at, fire_at, fire_dir, weapon, inf_ammo)
unreal.log(f'[crusader] usecode test: {r}')
