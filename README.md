# crusader-no-remorse
Some reseach I & an AI agent did on the code of Crusader No Remorse in hopes of making a Personal HD remaster. 

Unfortunately I ran out of time and Patience, love this game.

Hopefully someone else will find this useful.

## What is here

Notes, file-format documentation and tools from a 2026 effort to understand ORIGIN's *Crusader: No Remorse* (1995)
well enough to re-create it. The Unreal remake itself was abandoned; what is kept here is the research:

- `notes/README.md` — the working journal: every file format (FLX, shapes, GUMPS, maps, GLOBs, TYPEFLAG, ANIM.DAT,
  DAMAGE.FLX, COMBAT.DAT, usecode), how the engine behaves (eggs and triggers, teleport chains, combat and line of
  sight, NPC tactics, the mission start sequences, cutscenes, music), and the gotchas met along the way.
- `notes/usecode_triggers.md`, `notes/usecode_classes.txt` — how the usecode scripts are wired to the maps.
- `tools/` — Python extractors and converters that read your own copy of the game: `flx.py`, `shape.py`, `map.py`,
  `anim.py`, `dtable.py`, `sound.py`, `video.py` (RRV1/RRV2/JYV1 cutscene decoder), `atlas.py`, `sorter.py`
  (the original draw-order rules), `render_map.py`, `upscale.py`.
- `unreal/Source` and `unreal/Scripts` — the Unreal Engine 5 port of the game logic (usecode VM, kernel, world,
  combat, inventory, presentation) and the level-building scripts. Kept as reference for how the pieces fit together.

## What is deliberately not here

The game's own files, and anything derived from them, are copyrighted by Electronic Arts and stay out of this
repository: the GOG install, every extracted or upscaled sprite, gump, font, sound, music track, video, the map
and usecode data, and the imported Unreal assets. The tools regenerate all of it from a legitimately owned copy
(GOG sells it). Likewise the Ghidra project (it embeds `CRUSADER.EXE`) and the decompiled C are not redistributed;
the published disassembly research this work built on is at https://github.com/mduggan/crusader-disasm and the
authoritative engine reimplementation is ScummVM's `engines/ultima/ultima8` (https://github.com/scummvm/scummvm).

## Licence

GPL-3.0-or-later (see `LICENSE`). The usecode engine port under `unreal/Source/CrusaderRemake/Usecode` is a
derivative of ScummVM (GPL-3.0-or-later) and keeps that licence; the rest of the code is released under the same
terms for simplicity. Real-ESRGAN (MIT) is used by `tools/upscale.py` and must be downloaded separately.
