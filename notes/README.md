# Crusader: No Remorse — reverse-engineering workspace

Goal: recreate Crusader: No Remorse in Unreal Engine with upscaled original assets and
modern features. This folder is the extraction / research side of that project.

## Source of truth

The GOG release on this Mac is a Boxer (DOSBox) wrapper:

    /Applications/Crusader - No Remorse.app/Contents/Resources/Crusader- No Remorse.boxer/
        C.harddisk/   -> copied to data/hd   (exe, STATIC, USECODE, SOUND)
        D.iso         -> extracted to data/cd (FLICS videos, AMF music, extra STATIC, IDATA)

Game version string in the exe: "CRUSADER: No Remorse V1.21".

## Key discovery: most of the reverse engineering already exists

The game runs on a modified Ultima 8 engine. The engine has been fully
reimplemented, and the exe has already been disassembled and annotated:

| What | Where (cloned under reference/) | Use |
|---|---|---|
| ScummVM `engines/ultima/ultima8` | reference/scummvm | Clean-room reimplementation of the whole engine: every file format, usecode VM, intrinsics, AI, pathfinding, combat. The authoritative format reference. |
| ScummVM `image/codecs/jyv1.cpp` | reference/scummvm | Decoder for Origin's RRV1/RRV2/JYV1 video codecs used by the cutscene AVIs. ffmpeg cannot decode these. |
| mduggan/crusader-disasm | reference/crusader-disasm | ScummVM dev's **Ghidra project of CRUSADER.EXE** (1684 of 3076 functions named), full usecode disassembly (crusader_disasm.txt, 135k lines), opcode table, data-file notes. |
| mduggan/jyv1analyzer | reference/jyv1analyzer | Video codec analysis notes. |
| MaddoScientisto/Crusader-Map-Viewer | reference/Crusader-Map-Viewer | Web map viewer, usecode->shape catalog CSVs, decompiler WIP. Live: https://maddoscientisto.github.io/Crusader-Map-Viewer/ |
| dstreet26/CrusaderNoRemorseMapExtractor | reference/CrusaderNoRemorseMapExtractor | TypeScript map renderer producing full-level PNGs. |
| Kitrinx/Ultima8_Decomp | reference/Ultima8_Decomp | Byte-exact decompiled C source of Ultima 8 (Borland C++). Same engine family; module names match Crusader's function prefixes (Anim, AreaSearch, Cache, etc.). |

Official strategy guide (1995): consulted for design intent (not included here; the data files are the ground truth).
Community hub: https://echosector.org (forum, "Crusader Explorer" tool, RE threads).
Nobody has published an Unreal (or other) full remake; the Unreal side is greenfield.

## Executable

`CRUSADER.EXE` = MZ stub + Phar Lap 386|DOS-Extender RUN386 loader, then a Phar Lap
`P2` segmented EXP (level 2) with a `DLLD` import directory (doscalls, phapi, int33).
Compiler: MetaWare High C 386. Memory manager "CRUSADER MMU V1.1" by Tony Zurovec.
Ghidra loads it as `x86:LE:16:Protected Mode` with selector:offset addresses (e.g. 1130:0896).

Local Ghidra project (copy of the community one, owner changed to `mike`):
    ghidra/crusader_ghidra.gpr      open with: ghidraRun  (needs JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home)
    ghidra/out/CRUSADER_EXE_decomp.c      full decompiled C, ~4 MB
    ghidra/out/CRUSADER_EXE_functions.txt addr / name / size
    tools/ghidra_scripts/DumpProgram.java regenerates both

## File formats (all confirmed against the real files by tools/)

**Flex container** (`*.FLX`, `FIXED.DAT`, `WPNOVLAY.DAT`, `COMBAT.DAT`, `XFORMPAL.DAT`):
0x52 bytes comment padded with 0x1A, u32 count at 0x54, u32 version at 0x58,
table of (u32 offset, u32 size) at 0x80.

| File | Entries | Content |
|---|---|---|
| STATIC/SHAPES.FLX | 2048 (1449 used) | World sprites. Shape 1 = Silencer (3960 frames). |
| STATIC/GUMPS.FLX | 150 (127) | UI graphics, 2D shape variant (20-byte frame header). |
| STATIC/FONTS.FLX | 256 (14) | Bitmap fonts as shapes. |
| STATIC/FIXED.DAT | 256 (63) | Static map geometry, 16-byte items. Entry N = map N. |
| STATIC/GLOB.FLX | 3072 | Prefab groups referenced from maps (u16 n, then 6-byte items). |
| STATIC/TYPEFLAG.DAT | raw, 9 bytes/shape | Collision, family, footprint x/y/z (5 bits each), anim type. |
| STATIC/ANIM.DAT | raw | Per-shape table of 256 action offsets; animation frame data. |
| STATIC/WPNOVLAY.DAT | 64 (35) | Weapon overlay offsets per avatar action/dir/frame. |
| STATIC/DTABLE.FLX, DAMAGE.FLX, COMBAT.DAT, STUFF.DAT, TRIG.DAT | small | NPC table, damage table, AI tactics, Weasel shop, trig table. |
| STATIC/*.PAL | 768 bytes | VGA 6-bit RGB. GAMEPAL.PAL is the main palette. |
| USECODE/EUSECODE.FLX | 3074 (403) | Game scripts. Entry 1 = class-name table (13 bytes/class). Entry N+2 = class N. |
| SOUND/SOUND.FLX | 512 (270) | Entry 0 = name index; others `ASFX` + 28 bytes, then 8-bit signed PCM 11025 Hz. |
| cd/SOUND/*.AMF | 20 files | Music, "ASYLUM Music Format V1.0" 8-channel tracker modules. libopenmpt reads them: `openmpt123 --batch -o out.wav --no-float file.AMF` (done -> data/extracted/music; Opus copies in data/extracted/music_ogg, 80 MB; Unreal imports only the WAVs). |
| cd/FLICS/*.AVI | 174 files | Cutscenes. Video RRV1/RRV2 (large) or JYV1 (in-game), audio JYA1 8-bit 22050 Hz. |

**Shape frame** (Crusader world variant): shape = 4 unk + u16 nframes; frame table 8 bytes
(u24 offset, u8, u32 len); frame = 8 unk + u32 compressed + i32 w,h,xoff,yoff +
u32 lineoff[h] (relative) + RLE lines (skip,len,pixels; compressed bit0 = run).
Gumps/fonts/mouse omit the 8 unknown bytes.

**Map item** (FIXED.DAT): u16 x, u16 y, u8 z, u16 shape, u8 frame, u16 flags, u16 quality,
u8 npc, u8 map, u16 next. Crusader world coords are 2x the usecode coords.
**Globs (essential):** items whose shape has typeflag family 3 are glob eggs; `quality` is the GLOB.FLX index.
The glob's contents (u8 x, u8 y, u8 z, u16 shape, u8 frame) are placed at world
((egg_x & ~0x3FF) + (gx << 2) + 2, (egg_y & ~0x3FF) + (gy << 2) + 2, egg_z + gz) (ScummVM world/glob_egg.cpp).
Most floors and wall panels live in globs: mission 1 is 7,547 raw items but 17,584 after expansion.
tools/map.py expands them (items carry `glob: true`); the egg items themselves are never drawn.
Isometric projection and z-sorting rules are in ScummVM world/item_sorter.cpp.

**Usecode**: 0x7C opcodes (reference/crusader-disasm/usecode_opcodes.txt); intrinsic table
in reference/scummvm/.../usecode/remorse_intrinsics.h; readable disassembly of every class
in reference/crusader-disasm/crusader_disasm.txt. Class list: notes/usecode_classes.txt.

## Tools (python, run with .venv/bin/python)

| Tool | Does |
|---|---|
| tools/flx.py | List / extract any Flex archive. |
| tools/shape.py | Shapes -> per-frame PNG or sprite sheets + manifest.json (w,h,xoff,yoff). |
| tools/map.py | FIXED.DAT + GLOB.FLX -> JSON per map. |
| tools/sound.py | SOUND.FLX -> WAV + name index. |
| tools/anim.py | ANIM.DAT -> anim.json (shape -> action -> direction -> frames with flags). |
| tools/video.py | FLICS/*.AVI (RRV1/RRV2/JYV1) -> PNG frames. Audio: plain PCM, use ffmpeg. In-game MVA*.AVI need `--palette GAMEPAL.PAL`. |
| tools/render_map.py | Validation renderer: FIXED.DAT -> PNG using the engine's projection (`--full --scale 8` for overviews). |
| tools/convert_video.sh | AVI -> H.264 MP4 with audio (tools/video.py + ffmpeg). All 161 clips converted to data/extracted/video (225 MB). |
| tools/sorter.py | Port of the engine draw-order sort (SortItem::below), used by render_map.py. |
| tools/upscaler/ | Real-ESRGAN ncnn-vulkan (Apple Silicon ok). `realesrgan-ncnn-vulkan -i in.png -o out.png -m tools/upscaler/models -n realesrgan-x4plus-anime -s 4`. |

Extracted so far (data/extracted/): all 1449 world shapes (per-frame PNG + manifest.json),
127 gumps, 14 fonts, 63 maps as JSON, 3072 globs, 269 sound effects as WAV, anim.json,
typeflags.json.

## Projection (validated with tools/render_map.py)

    world x,y = FIXED.DAT x,y * 2 ; footpad = typeflag x,y * 32, z * 8
    screen_x = x/4 - y/4
    screen_y = x/8 + y/8 - z
    sprite top-left = (screen_x - xoff, screen_y - yoff); FLG_FLIPPED (0x20): left = screen_x + xoff - width
    Typeflag byte 6 bit 0 = editor-only shape (invisible walls etc.) -> never drawn.
    Engine draw order: dependency sort in ScummVM world/sort_item.cpp (SortItem::below).

## Unreal project (unreal/)

UE 5.8 is installed at $UE_ROOT (Terra Nova project uses the same
headless Python workflow). Project: unreal/CrusaderRemake.uproject (Paper2D + Python enabled).

    UE=$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd
    $UE unreal/CrusaderRemake.uproject -ExecutePythonScript="unreal/Scripts/import_crusader.py --shapes 1-50" -stdout -unattended -nopause -NullRHI
    $UE unreal/CrusaderRemake.uproject -ExecutePythonScript="unreal/Scripts/build_level.py --map 1" -stdout -unattended -nopause -NullRHI

import_crusader.py: PNG frames -> Texture2D (nearest, uncompressed) -> PaperSprite (pivot = xoff,yoff)
-> PaperFlipbook per action/direction from anim.json.
build_level.py: map JSON -> PaperSpriteActors at (screen_x, depth, -screen_y).

State 2026-10-13: /Game/Crusader/Maps/Map01 built with all 5838 mission 1 items (416 shapes,
3415 sprites, 712 flipbooks). Open with "Open Unreal Editor.command" in the project root.
Importer supports `--src shapes_x4 --scale 4` for the upscaled frame set (data/extracted/shapes_x4).
Verified render: notes/map01_unreal.png (editor screenshot via Scripts/screenshot_level.py, run with the
windowed `UnrealEditor` binary and `-ExecCmds="py script.py args"`; `-ExecutePythonScript` quits the editor
before the viewport ticks, and ExecCmds splits arguments on commas, so use `2000x12900` style args).
Known gotcha: assign sprites to PaperSpriteActors with `set_editor_property('source_sprite', ...)`;
`set_sprite()` does not persist when run headlessly.
Reference renders: notes/map01_python_render.png (tools/render_map.py), notes/map01_overview.png (whole level, 1/8).
Note: always pass `-m $CRUSADER_ROOT/tools/upscaler/models` to the Real-ESRGAN binary; with the default relative `models` path it segfaults when run from another directory.

## Level numbering (from map sizes; confirm against usecode/ScummVM remorsegame.ini)

Maps 1–29 odd/even pairs are missions and their variants; 40/41 rebel base; many
high-numbered entries (116, 141, 142, 149, 150, 160, 170, 180, 223, 244…) are
test/duplicate maps. MaddoScientisto's thread notes "map 33" as a large unused map.

## 3D world mapping (vertical slice, unreal/Scripts/build_level3d.py + Source/CrusaderRemake)

    UE X = x, UE Y = y (engine world units), UE Z = z * ZK,  ZK = 2*sqrt(2)/cos(30 deg) = 3.2660
    Camera: orthographic, Rotator(pitch=-30, yaw=225). Screen right = (1,-1,0)/sqrt2, screen up on ground = (-1,-1,0)/sqrt2.
    1 sprite pixel = 2*sqrt(2) = 2.8284 units horizontally; vertical sprite pixel = ZK units.
    Non-flat items: PaperSpriteActor billboard, rotation = make_rot_from_xz(screen_right, camera_up), scale (2.8284, 1, 3.2660),
      pivot (xoff,yoff) at the item's near-right-bottom corner (x, y, z*ZK). Flipped items: negative X scale.
    Flat items (footpad zd == 0): same sprite laid on the ground, image-right -> (1,-1,0)/sqrt2, image-up -> (-1,-1,0)/sqrt2,
      scale (2.8284, 1, 5.6569): the 2:1 diamond image covers the square footprint exactly.
    Collision: hidden cube StaticMeshActors sized to footpads (xd*32, yd*32, zd*8*ZK) for solid non-flat items.
    Directions: engine dir 0 = north = (0,-1), 4 = east = (+1,0), 16 steps of 22.5 deg (8-dir anims use dir16/2).
    Movement: engine moves 4*deltaDir units per anim frame; shape 1 walk cycle sums 64 (-> 256 units per 17 frames).

C++ (Source/CrusaderRemake): ASilencerPawn is an APaperCharacter (capsule 48 x 130, flipbook billboard anchored at the
capsule base, CharacterMovement in Flying mode because floors have no collision), with a separate ACameraActor follow camera
(orthographic, pitch -30 / yaw 225, OrthoWidth 1920*2.8284 = 1:1 original pixels) set as the view target. Enhanced Input is
built in code: WASD/arrows are screen-relative, shift = run; flipbooks load by path F_0001_A{action}_D{dir}. ACrusaderGameMode.
Gotchas found: (1) the pawn's engine anchor must sit on the floor, otherwise the floor tile (same billboard depth) hides it;
(2) some floor spots are covered by the game's black mask/roof shapes, which occlude anything standing there (use
build_level3d.py --no-roof and --start sxXsy to pick an open tile); (3) -ExecutePythonScript cannot take a PIE screenshot,
use Scripts/pie_screenshot.py with the windowed editor and -ExecCmds.
Build: Engine/Build/BatchFiles/Mac/Build.sh CrusaderRemakeEditor Mac Development -Project=<uproject> -WaitMutex (~40 s).
Level: /Game/Crusader/Maps/Map01_3D (first-room region 2000x12900x2200x1600), PlayerStart_Room1, DimetricCamera.
Current state (globs expanded): 13,770 sprites (5,599 flat), 7,810 collision boxes, 0 missing; capture notes/map01_3d_globs_pie.png.
Earlier capture without globs: notes/map01_3d_pie.png (Scripts/pie_screenshot.py, windowed editor + -ExecCmds).
Build used: build_level3d.py --map 1 --region 2000x12900x2200x1600 --start 2100x13100 --no-roof

## Performance (2026-10-13)
Actor-per-sprite level (13.8k PaperSpriteActors + 7.8k collision actors) was unplayable in the editor. Two fixes:
1. DefaultEngine.ini: Lumen GI/reflections off, virtual shadow maps off, AA off, bloom/AO/motion blur off (unlit sprite game).
   Alone this gave ~50 fps on the actor-based level (pawn logs "Silencer fps" every 2 s).
2. Batched level (unreal/Scripts/build_level3d_batched.py + Source/CrusaderRemake/CrusaderSpriteBatch.*): all frames used by the
   level packed into one 4096^2 atlas (tools/atlas.py, 4,677 frames for mission 1), rendered as HISM instances of the engine Plane
   with per-instance custom data (atlas UV rect + visibility) through M_CrusaderAtlas (unlit masked two-sided, built by Python).
   Collision is one hidden HISM of cubes. Roofs: original rule (lowest roof above the player sets a z limit; everything at or above
   it is hidden), implemented with the custom-data visibility channel.
   Result: mission 1 = 14,994 instances in 2 batches + 9,007 collision instances, level file 3.8 MB (was 33 MB),
   steady 100 fps in PIE on the M5 (capture notes/map01_3d_batched_pie.png). Gotcha: the engine Plane's V runs opposite
   to local +Y, so the per-instance UV rect uses a negative dv. build_level3d.py (actor-per-sprite) is now legacy.

## Playtest oddities round 1 (2026-10-13, from Mike's screen recording)

Diagnosis tooling: pie_screenshot.py now drives the pawn from the console (`Silencer.Move X Y secs [run]`,
`Silencer.Face dir16`, `Silencer.NoAnim 0|1`, `Silencer.HideSprite 0|1`, all in SilencerPawn.cpp) via `--move 0x-1x4_-1x0x3`
(steps joined by `_`, no commas), `--precmd/--postcmd/--then` console commands (`=` for spaces), and `--shot plain` which
captures the live viewport frame (`Shot`) instead of HighResShot. The pawn also logs its current sprite texture state every 2 s.

Findings and fixes:
1. Player vanishing behind walls (the main oddity). A billboard puts every pixel of a sprite at its anchor's depth, so a
   16-tile wall drawn as a billboard hid the player standing beside its far end (confirmed: shapes 9/39 in the corridor south
   of the start). Fix (build_level3d_batched.py `aligned_basis`): items whose footprint is >= 2:1 and >= 64 units long are
   placed on the plane spanned by the wall direction e and the camera up vector U: r = a*e + b*U, a = PX/(R.e), b = -a*(e.U).
   r projects to exactly one screen pixel right and is orthogonal to U, so the sprite still maps 1:1 with no shear and the quad
   contains the wall's bottom edge (depth along the wall is exact). Square-ish props stay billboards. A player standing west of
   the console is still covered by it, which matches the original sort order (lower x = farther).
2. The "white ghost" was not the player: it is STEAM2 (shape 1297, typeflag anim type 3) stuck on one frame. The typeflag
   loader now exports anim_type/anim_data/anim_speed (ScummVM gfx/type_flags.cpp: b4 >> 4, b5 & 0xF, b5 >> 4), atlas.py packs
   every frame of frame-cycling shapes, the builder emits one instance per frame into a Sprites_Anim batch, and
   ACrusaderSpriteBatch::Tick ports Item::animateItem (30 Hz game ticks / anim_speed) by toggling instance visibility.
   Mission 1 has 76 animated items (steam, flames, monitors, fans).
3. Editor-only solid shapes (invisible walls such as shape 102) were skipped entirely; they now get collision boxes but no
   sprite, and zero-thickness walls (e.g. shape 311, footpad 5x0x7) get a 16-unit slab. Mission 1: 9,539 collision boxes.
Capture after the fixes: notes/map01_3d_aligned_pie.png (15,441 instances in 4 batches, still 100 fps).

## Upscaled art (2026-10-13)

Comparison of upscalers on representative sprites: notes/upscale_comparison.png (nearest, Scale4x, Real-ESRGAN x4plus,
x4plus-anime, animevideov3-x4, and 4x->2x Lanczos). Chosen: realesr-animevideov3-x4 (keeps rivets/floor grain, clean lines),
downsampled to 2x. All three ESRGAN models bleed the transparent key colour into the edges (23% semi-transparent edge
pixels, dark fringe), so tools/upscale.py (1) fills transparent pixels with the nearest opaque colour before upscaling,
(2) upscales the alpha separately with Scale2x twice (binary, no halo), (3) premultiplied Lanczos downsample to 2x.
Output: data/extracted/shapes_x2 (all 30.6k frames, ~25 min on the M5 in batches of 400 files per model run).
Build: `atlas.py map01.json data/extracted/atlas/map01_x2 --src shapes_x2 --scale 2 --size 8192 --pad 4 --extra "1:*"`
(one 8192^2 atlas for mission 1), `build_level3d_batched.py --map 1 --start 2100x13100 --atlas .../map01_x2 --level
/Game/Crusader/Maps/Map01_3D_HD` (atlas textures T_Map01x2_* are BC7 + bilinear; 1x stays uncompressed + nearest),
`import_crusader.py --shapes 1 --actions 0,1,3 --src shapes_x2 --scale 2` for the player flipbooks.
Camera: the view is now 960 original pixels wide by default (one original pixel = two screen pixels at 1920 wide; the
original game showed 640), keys = / - step through 640/960/1280/1920 (`Silencer.Zoom N` from the console). At 1920 the
upscale is invisible, which is why the zoom came first. Map01_3D_HD is the default map; Map01_3D is the 1x reference.
Capture: notes/map01_3d_hd_pie.png, ~117 fps.

## Usecode engine (2026-10-13)

Door research (notes/usecode_triggers.md) showed the game logic is a scripting graph, so the game's own usecode now runs
inside Unreal: Source/CrusaderRemake/Usecode/ is a port of ScummVM's VM and world model (GPL, personal project):
- UsecodeFlex: EUSECODE.FLX classes/events. UCMachine: all opcodes used by Crusader (pointers, lists, strings, loops,
  spawn/implies/suspend). Kernel: 60 Hz process scheduler with wait/wake semantics. CruWorld: items (objId 1 = avatar,
  256+ = map items in JSON order), 64x64 chunks of 1024 units, area/surface search with loop scripts, ethereal void,
  fast area (640x480 view, +/-7 chunks), egg hatching (range nibbles x 64), usecode events (cachein at load,
  enterFastArea for every item, hatch/unhatch, use...). CruIntrinsics: the Remorse intrinsic table
  (RemorseIntrinsicTable.inl generated from ScummVM) with ~130 implemented; missing ones log once and return 0.
- Coordinates: usecode x/y are world/2; typeflags.json now carries b6 (Crusader flag byte), weight, volume.
- CrusaderGameSubsystem runs the engine in PIE/game: mirrors the pawn into the avatar item, ticks the engine, and
  applies item changes: static instances of a changed/destroyed item are hidden (ACrusaderSpriteBatch::InstanceItem
  maps instances to map-JSON indices) and a per-atlas Sprites_Dynamic batch re-places the item with
  CruSpritePlacer (C++ twin of the builder's place()). Collision boxes follow (CollisionInstances). Sounds come from
  /Game/Crusader/SFX/S_NNN (import_sfx.py). ACrusaderLevelInfo (placed by the builder) says which map/atlas/data to load.
- Use key (E / Enter / Space, console Silencer.Use): ItemSelectionProcess rules from ScummVM, i.e. the nearest
  SI_CRU_SELECTABLE item within 256 units gets its 'use' event. Mission 1 doors open from the wall panels beside them
  (PANELNS/PANELEW/SPANEL -> TRIGGER -> DOOR2::ordinal28/2A), which also destroys the roof tiles over the next room.
- Verified in PIE: teleport beside the first door's panel, use it: beep, roof pieces removed, door slides through
  shapes 195..203 at 4 ticks per step with sfx 81, collision box removed. The security camera sweeps frames and trips the
  alarm box when it sees the player, monster eggs try to spawn guards (Actor::I_createActorCru not ported yet).
- Headless harness: Scripts/usecode_test.py (UCrusaderUsecodeTest::RunHeadless) walks the avatar along a line and logs
  every item change, sfx and unimplemented intrinsic; ~45 s per run. Console: Silencer.Teleport X Y, Silencer.Use.
- atlas.py now packs every frame of every shape used by the level (scripts change frames at run time); mission 1 at 2x
  = 6,829 frames in one 8192 atlas.
Not yet: actors/NPCs (createActorCru, combat), inventory (addItemCru), movies, keypads, teleporting between maps.

## Actors (2026-10-13)

- DTABLE.FLX -> data/extracted/dtable.json (tools/dtable.py): 64 NPC records (name, hp, weapon types, shape, default
  activities). Actor::I_createActorCru is ported: MONSTER marker pairs (frame 0 + frame 1 with the same qlo) spawn NPCs
  with objIds 2..255; the ITEM/MONSTER scripts then run the teleport-in sequence and setActivity(0x70..0x72 = default
  activities). Mission 1 start spawns a chem-suit worker and a Thermatron.
- Usecode/CruActors.*: anim.json loader (ANIM.DAT), FAnimationTracker + FActorAnimProcess (frame stepping with
  frame repeat, 4*deltaDir movement, cru-flip flag, blocked/unsupported via GetPositionInfo; no sweep test or
  position scan yet), DelayProcess, CruPathfinderProcess (direction search with tryAnim), Loiter/Pace/Guard processes,
  TurnTowardDir, SetActivity (Crusader table incl. 0x70-0x72), CanSeeControlledActor (no line-of-sight sweep yet).
  Combat activities (5/9/a/b) only log; AttackProcess, weapons, damage and death are next.
- Rendering: actors are runtime items, placed by the dynamic batches as any other change; the atlas packs every frame
  of the NPC shapes mission 1 uses (9,306 frames, 2 atlases at 2x). Runtime-created solid items get a collision box.
- NPCs inside unopened rooms are hidden under the 538 ceiling tiles until the linked door opens, as in the original.

## Combat and line of sight (2026-10-13)

- Usecode/CruCombat.*: port of the Crusader combat layer from ScummVM. FireType table (16 No Remorse entries),
  weapon stats from ScummVM's remorseweapons.ini (devtools/create_ultima8; shape, sound, fire type, small flag, shot
  delay, clip size, ammo shape, energy), DAMAGE.FLX (6 bytes per shape: flags, sound, frame/replacement data, damage
  points; items get DamagePoints at creation) and COMBAT.DAT (64 tactics: 16 byte name, 4 block offsets, bytecode).
- CurrentMap::sweepTest is ported as FCruWorld::SweepTest (swept box vs chunk items, 0x4000 time scale, touching /
  blocking / dirs, sorted hits with GetInterpolatedCoords). It drives line of sight (GetRangeIfVisible, used by
  CanSeeControlledActor and the getRangeIfVisible intrinsic), FireDistance and projectile collision.
- Item::fireWeapon / fireDistance / getTargetZRelativeToAttackerZ / findBestTargetItem, SuperSpriteProcess (projectile
  with inexact aim, steps of cellsPerRound*32 per round, sweep hits, bullet splash sprite + sound, splash damage),
  SpriteProcess (also backs the createSprite intrinsic), Item::explode, receiveHit for items (gotHit usecode event,
  DamageInfo::applyToItem: frame change / replacement / explosion / destruction) and actors (Actor::receiveHitCru:
  sets the default-2 activity or setInCombatCru(5), screams, stun; death via dieCru with the special death shapes,
  fall direction test and death sfx), tookHitCru, setInCombatCru / clearInCombat.
- AttackProcess (type 0x259): genericAttack (tactic 0) with all its timers plus the COMBAT.DAT metalanguage
  interpreter (opcodes 0x81-0xff). SetActivity 5/9/a/b/c now start it. The anim process fires the weapon on
  cru-attack frames (doFireWeaponCru) and calls tookHitCru on hurty frames; DoAnim keeps the kneeling / weapon-ready
  flags and the dodge bookkeeping (attackMoveStartFrame/Timeout/DodgeFactor) that NPC aim uses.
- Avatar: SpawnAvatar gives a BA-40 (Silencer.Weapon SHAPE to change), HP from DTABLE; FCruEngine::AvatarFire does
  the CruAvatarMoverProcess::tryAttack sequence (fire anim in the facing direction, shot delay, NPC alerting) without
  ammo / energy bookkeeping; keys LeftCtrl / F / left mouse, console Silencer.Fire. Avatar death just revives for now.
- Headless harness takes [fireAtTick] [fireDir16] (fires every 20 ticks from that tick); set GCruCombatDebug for
  per-decision attack logging. Verified: usecode turrets (Item_I_fireWeapon, type 5) hit the avatar; guards spawned by
  the egg at (34440,35800,96) enter combat, walk, turn, ready and fire BA-40 rounds that hit the avatar; the avatar's
  shots hit guards and blow up an explosive barrel (shape 304 -> explosion sprite + splash damage).
- Atlas now also packs the effect / death shapes (0x1d9 bullet, 0x1d8 splash, 0x537, 0x320/1/6, 0x31c/f/0x322,
  0x578, 0x56b, 0x59b, 0x426, 0x576, 0x58f, 0x596, 0x59c, 0x278) and the NPC shapes: 11,392 frames, 2 atlases.
- Player follow-up (2026-10-14): AvatarFire now does the full tryAttack sequence: clip counter in the weapon's
  quality, reload animation + sfx 0x2a when empty (ammo reserve is unlimited until inventory exists), energy weapons
  draw from the avatar's energy (2500 max, no recharge yet), SG-A1 pumps between shots. Avatar death runs dieCru (fall
  animation, engine-rendered), then 5 s later respawns at the mission start with full health
  (FCruEngine::RespawnAvatar; the game layer teleports the pawn). While the engine animates the avatar (fire, reload,
  death) item 1 is drawn through the dynamic sprite batches and the pawn's flipbook is hidden and movement locked
  (UCrusaderGameSubsystem, ASilencerPawn::bEngineAnim), so no extra flipbook imports are needed. A debug HUD line shows
  HP / energy / weapon / clip. notes/combat_pie.png (guards closing in), notes/avatar_death_pie.png (turret kill).
- pie_screenshot.py: --settle now also holds before the shot when there are no --move steps (the game logic only
  starts ticking once PIE is up, so use ~9 s for NPCs to arrive).
- Also ported (2026-10-14): SurrenderProcess (activity 7: hands-up anim 57, keeps facing the avatar, one call-out
  per shape; unarmed NPCs surrender when the player fires nearby via CheckForAlertingNPCs -> default activity 2),
  Item::hurl as FGravityProcess (type 0x203, sweep-based move, side bounces, lands on dirs==4; no actor fall damage)
  used by explosive hits on loose items and the hurl intrinsic, and sound tracking: the game layer reports each sfx's
  duration to FCruWorld (NoteSfxStarted/Stopped) so isSFXPlayingForObject and the NPC "wait for my sound" checks work.
  Note NPC sight is now a real line-of-sight test, so loitering workers no longer notice the player through walls;
  only pace/guard activities and the alert on firing switch them (as in ScummVM).
- Energy shields (2026-10-14): MainActor::receiveShieldHit ported as CruCombat::ReceiveShieldHit (shield type from the
  inventory's shield generators via FCruEngine::ShieldType; fire-type shield mask / cost, sfx 0x48, shield sprite 0x5a9 or
  0x52b). Ported from the ScummVM text; not yet exercised in a run (needs an NPC hit while the ionic shield is up).
- Not done: Vargas shield, energy recharge, kneeling for the avatar, a real death screen / reload, actor fall damage.

## Mission 2 and egg hiding (2026-10-14)

- tools/atlas.py --auto packs the avatar, every NPC the map's MONSTER marker pairs can spawn (DTABLE shapes) and the combat
  effect / death shapes, so a map needs no hand-typed --extra list. Mission 2: `atlas.py maps/map02.json
  data/extracted/atlas/map02_x2 --src shapes_x2 --scale 2 --size 8192 --pad 4 --auto` (7,043 frames, 2 atlases) and
  `build_level3d_batched.py --map 2 --start=-7168x11440 --atlas .../map02_x2 --level /Game/Crusader/Maps/Map02_3D_HD`.
  The mission 2 start is teleport egg 8 at world (31422,60094,96): the mission-1 end script calls
  MainActor::I_teleportToEgg(2, 8) (usecode disasm), and ScummVM's StartCrusaderProcess begins mission 1 with
  teleport(1, 0x1e). Builder --start takes screen coords tx=(x-y)/4, ty=(x+y)/8. The engine loads map N from the
  level's ACrusaderLevelInfo, so mission 2 runs usecode without code changes (notes/.. PIE shot: elevator start room).
- Egg items (families 3 glob, 4 unk, 7 monster, 8 teleport) are never drawn now, in the builder and in the runtime path.
  Monster eggs (shape 591, the blue "M" diamond) are not editor-flagged in TYPEFLAG.DAT, so they showed up on mission 2.

## Music and sound distance (2026-10-14)

- Scripts/import_music.py imports data/extracted/music/*.wav (the AMF tracker renders) as looping USoundWaves under
  /Game/Crusader/Music (M01-M15, M16A/B/C, CRED, MENU, MUSIC). MusicProcess::I_playMusic / I_stopMusic set
  FCruEngine::MusicTrack (CruMusicProcess numbering: 1-15 = M01-M15, 16 cycles the three mission-16 parts, 19 cred,
  20 menu) and UCrusaderGameSubsystem::UpdateMusic plays the track as a 2D looping sound at 60 % volume, one at a time.
  The mission theme comes from AVATAR::cachein (usecode class 2049, one track per map), so SpawnAvatar now raises the
  avatar's cachein event like CurrentMap::loadMap does for actors in Crusader; REB_EGG plays track 16 at the Rebel base.
- Positional sfx now use a sphere attenuation (full volume within 1200 units, silent beyond 4000) and the audio
  listener is the pawn rather than the camera, which sits ~4000 units away; the avatar's own sounds stay 2D.

## All campaign levels (2026-10-14)

- tools/build_all_levels.py MAP... runs atlas.py --auto and build_level3d_batched.py per map (logs in
  data/extracted/atlas/build_mapNN.log); the player start is destination teleport egg 30 (the mission start egg:
  ScummVM starts mission 1 with teleport(1, 0x1e)), else the lowest destination egg, else a floor tile. About 35 s per
  map on this machine. Map 12 does not exist (FIXED.DAT entry 12 is empty).
- Mission -> map, from which map holds each MISSn egg (class 0x900 + quality; extra numbers are duplicate / dev
  copies): 1 -> 1 (map 2 is the lift area the mission 1 end teleports to), 2 -> 3+4, 3 -> 5+6, 4 -> 7, 5 -> 10,
  6 -> 11, 7 -> 13 (+14 via teleports), 8 -> 15 (+16), 9 -> 17, 10 -> 19, 11 -> 21, 12 -> 23+24, 13 -> 25+26,
  14 -> 27+28, 15 -> 29 (the Rebel base map is also where mission 1's map-29 teleporter leads). Maps 8 and 9 have no
  mission egg (sub-areas). Maps above 100 (116, 141, 142, 149, 150, 160, 170, 180, 223, 224, 239, 243...) are variant /
  dev copies (map 244 even has out-of-range shapes) and are not built.
- Built: Map01..Map11, Map13..Map17, Map19, Map21, Map23..Map29 as /Game/Crusader/Maps/MapNN_3D_HD.

## Status bar, pickup toasts and checkpoint saves (2026-10-14)

- ACrusaderHUD now draws the original Crusader status bar from the extracted gump sheets (data/extracted/gumps/NNNN.png,
  up to 16 frames per row, frame sizes from manifest.json; loaded at run time with FImageUtils): five 108x28 boxes
  (gump 1 frames 0/1/0/2/3) 15 px from the left and 2 px from the bottom of a 640x480 space scaled to the viewport
  height; weapon icon (gump 3), ammo box with bullets / clips in font 15 (only for weapons with ammo), active inventory
  item (gump 4 / 5) with its count in font 12, health bar (palette 65, blue) and energy bar (palette 245, red), 67 px max.
  Icons per item shape come from remorseweapons.ini display_shape / display_frame (CruGetGumpIcon in CruCombat.cpp).
- Pickup toasts (CruPickupAreaGump / CruPickupGump): AddItemCru pushes FCruEngine::Pickups (merged per shape, counts for
  credits / weapons / ammo); the HUD draws rows of five translucent 28x28 tiles (gump 2) at (5,5) with the icon, the
  name in font 13 and the count in font 12 for 180 ticks. Fonts are drawn from data/extracted/fonts sheets indexed by
  character code.
- Checkpoint saves: F5 / F9 (slot 0) and Silencer.Save / Silencer.Load [slot] write Saved/SaveGames/crusader_slotN.json
  with map, avatar position / direction, health, energy, keycards, shield, battery, active weapon and inventory;
  loading reuses the cross-map carry-over (FCruCarryOver with Egg = -1 teleports to the saved position) and reopens the
  level, so world / NPC state restarts (not a full ScummVM-style save of every object and process).
- A checkpoint reload replays the level's start script (its cachein / hatch usecode runs inside FCruEngine::LoadMap),
  so FCruEngine::SuppressMoviesUntilTick (set in LoadMap when the carry-over is a checkpoint) skips the intro cutscene;
  the start items it hands out again merge into the inventory like any pickup. Verified: save in the guard room, fresh
  start, load -> back in the guard room with the inventory and no intro.
- Cutscene resolutions (original, re-encoded as H.264 at 15 fps): in-mission video mails 240x180, Rebel base MVA
  scenes 320x236, intro T01 640x480, H01 320x160; the HUD scales them to a 4:3 box the height of the viewport.
- notes/hud_gumps_pie.png: the status bar at the mission 1 start.

## Presentation, player extras and the Rebel base (2026-10-14)

- Cutscenes: MovieGump::I_playMovieCutscene / Alt / Overlay now play the extracted MP4s (data/extracted/video, from
  FLICS/*.AVI via tools/video.py; names as the usecode gives them: "1a" -> 01A.mp4, "mva1" -> MVA01.mp4 like ScummVM's
  _fixCrusaderMovieName). UCrusaderGameSubsystem::PlayMovie builds a UMediaPlayer / UMediaTexture / UFileMediaSource /
  UMediaSoundComponent at run time (AvfMedia on Mac; module MediaAssets), ACrusaderHUD draws the texture letterboxed
  4:3, fire skips, and FCruEngine::bMoviePlaying pauses the kernel (the original uses a modal gump). The mission 1
  start script plays 01A (video mail) before anything else; the headless harness skips cutscenes.
- Fades: PaletteFaderProcess::I_fadeToBlack / fadeToGamePal / fadeToGivenColor / jumpToAllBlack / jumpToAllWhite /
  jumpToNormalPalette drive FCruEngine::FadeAlpha + FadeColor through FFadeProcess (returns a pid usecode can wait
  on); the HUD draws the overlay. jumpToGreyScale is a no-op.
- Death screen: 2 s after the fall the "Silencer terminated" sfx 9 plays, the screen fades to black over 2 s with
  SILENCER TERMINATED, and 6 s after death the avatar respawns at the mission start and fades back in
  (AvatarDeathProcess in the original goes to the main menu instead).
- HUD: ACrusaderHUD (set by ACrusaderGameMode) draws the status line (HP, energy, weapon + clip, inventory,
  credits, keycards, shield) instead of the debug message.
- Kneeling: C toggles kneelStartCru / kneelEndCru; while kneeling the avatar is engine-rendered and cannot walk, and
  firing uses the kneel fire anims. Inventory: I selects the next item, U uses it (MainActor::useInventoryItem: the
  item's use event, quantity -1 and combine, destroyed at 0; data link and shield generators are not consumed).
  Console: Silencer.Kneel / UseItem / NextItem / Movie NAME.
- Animation tracker: large steps now do the full sweep test and on-ground steps that are blocked or unsupported use
  CurrentMap::scanForValidPosition (FCruWorld::ScanForValidPosition, Crusader scan size 10 with the +/-1..9 z
  offsets) so NPCs can step onto the 1-px-offset floor pieces and walkways.
- Rebel base: map 29 built (atlas --auto, 12,330 frames; Map29_3D_HD; start egg 99 at (58878,11774), which is where
  mission 1's map-29 teleporter sends the avatar). notes/rebel_base_kneel_pie.png, notes/cutscene_pie.png.
- Energy does not regenerate in the original either (only batteries / energy items add energy), so that item is
  closed as "not a feature".
- Not done: the pickup toast gump, save games, No Regret.

## Map transitions (2026-10-14)

- Teleport eggs (family 8, shape 404): sources have frame != 1, the destination egg has frame 1 and the same id (quality
  & 0xff); the source's map field is the target map. FCruWorld::RunEggHatcher now honours MainActor::_justTeleported
  (no teleport-egg hatching until the avatar leaves every teleporter's range) and HatchEgg sends sources to
  FCruEngine::TeleportToEgg. Same map: the avatar item moves to the destination egg (processes killed, fast area and
  camera updated) and the game layer teleports the pawn (bPendingPawnTeleport). Other map: the avatar's HP, energy,
  keycards, shield / battery and inventory (shape, frame, quality) go into the process-global FCruCarryOver, the
  subsystem opens /Game/Crusader/Maps/MapNN_3D_HD, and the new level's TryInit restores the carry-over after
  SpawnAvatar and teleports to the egg. MainActor::I_teleportToEgg is wired; mission 1 ends with LIFT usecode calling
  teleportToEgg(2, 8, 0) after a fade. Console: Silencer.TeleportEgg MAP EGG. Verified: egg pair 7/8 on mission 1
  headless, and PIE mission 1 -> mission 2 with 4 items carried (notes: screenshot HighresScreenshot00033).
- Headless harness follows teleports (re-anchors the scripted position) and logs requested map switches.
- Gotcha found while testing the switch: after OpenLevel the new level rendered black except the pawn. Cause: the
  master material M_CrusaderAtlas had `used_with_instanced_static_meshes` off. A level loaded into the editor world
  gets the usage flag fixed up silently (and the PIE duplicate inherits the compiled shaders), but a level opened at
  run time only gets the fallback, so every static batch drew nothing. Fixed in the asset and in the builder
  (build_level3d_batched.py sets the usage flag). Diagnostics kept: UCrusaderGameSubsystem logs a one-shot "batch
  diag" per level (instances, materials, textures, view), and Silencer.BatchMaterial grid|map1|map2|map1tex2|dyn2
  swaps every batch material 3 s after the next level initialises.
- Not done: mission transition fades / cutscenes, the Rebel base (map 29 teleporters), save games.

## Inventory (2026-10-14)

- Usecode/CruInventory.*: containers on FCruItem (Parent + Contents; actors and SF_CONTAINER shapes are containers, contained
  items sit at (0,0,0) with FLG_CONTAINED and are hidden by the renderer), Item::moveToContainer / Container::removeContents /
  destroyContents, getFirstItemWithShape, Actor::getEquip / setEquip (equip types read from TYPEFLAG.DAT byte 2 bits 1-4),
  MainActor::addItemCru (ScummVM main_actor.cpp:127-325 = original World_GiveItemToAvatar 13e8:0330): credits 0x4ED stack
  (cap 64000), one weapon per shape (duplicates are left on the floor), ammo stacks to 20 clips, keycards 0x111 set
  FCruEngine::Keycards bit (quality & 0xFF), batteries 0x3A2/0x3A4/0x3A3 upgrade FCruEngine::BatteryType or add 2500/5000/10000
  energy (max energy per battery: 2500/5000/10000), shields 0x52E/0x52F/0x530 raise FCruEngine::ShieldType, other items stack
  to 10 (spider mines 0x560 to 20). Neither the original nor ScummVM plays a pickup sound (only a toast gump); we log the pickup.
- Intrinsics: I_addItemCru (item by id + showtoast), getNumberOfCredits, getMaxEnergy, getMana/setMana (energy), getEquip/setEquip,
  Container::destroyContents, popToContainer (into actors / containers), loop container search. The use key gives pickups to
  addItemCru directly (ItemSelectionProcess::useSelectedItem). Mission 1 start: MISS1EGG hatch creates BA-40, BA-41 ammo,
  blastpac, chemical battery, ionic shield and energy cube at (0,0,0) and they land in the inventory.
- Avatar: no default weapon any more (the usecode gives the BA-40); Silencer.Weapon SHAPE is a cheat that adds the weapon.
  Tab / Q next weapon, Z (or Shift+Tab) previous, console Silencer.NextWeapon / PrevWeapon. AvatarFire reloads from ammo
  items (quality = clips, destroyed at 0) like CruAvatarMoverProcess::tryAttack and stops with sfx 0x2a when there is none;
  Silencer.InfiniteAmmo 1 (FCruEngine::bInfiniteAmmo) restores unlimited ammo / energy. The BA-40 has no ammo shape in
  remorseweapons.ini, so it never reloads (as in ScummVM). HUD line shows weapons with clips, ammo, items, credits, keycards.
- Headless harness: usecode_test.py takes [weaponShape] [infiniteAmmo] after fireDir16; logs the inventory after load, each
  pickup, at tick 120 and on every FIRE. Verified: start items in the inventory, BA-41 20 -> 0, reload consumes the ammo item,
  then no ammo.
- Not done: using inventory items (blastpac etc., MainActor::useInventoryItem), dropping weapons, the pickup toast gump,
  weight / volume limits, equipment for the rebel base NPC swap (needs typeflag equip types, only 3 shapes have one).

## Mouse aiming, target reticle and key rebinding (2026-10-14)

- Mouse aiming (a remake feature; the original turns the avatar with the keys while in combat): ASilencerPawn projects the
  cursor onto the floor plane at the pawn's feet (DeprojectScreenPositionToWorld, AimWorld) and, for two seconds after any
  mouse movement, the facing follows the cursor instead of the movement direction. The subsystem passes AimPoint/bAimValid
  to FCruEngine; CruCombat::FireWeapon aims the controlled actor's shots at that point when auto-aim found no target
  (otherwise the usecode target wins, as in ScummVM). The cursor is hidden and ACrusaderHUD draws crosshair shape 0x4CC at
  the mouse (data/extracted/shapes_x2/1228). The first mouse read is not a movement (otherwise Silencer.Face is overridden).
- Target reticle: FTargetReticleProcess (persistent kernel process, port of ScummVM TargetReticleProcess) scans every 60
  frames with FindBestTargetItem (16 directions) while the avatar is in combat, marks the target with EXT_TARGET and plays
  sprite 0x59a frames style*6..+5 (SpriteProcess repeats 1, delay 10) then holds the last frame; FCruWorld::Move calls
  FCruEngine::OnTargetItemMoved for EXT_TARGET items so the sprite follows or drops when the direction changes;
  teleports call AvatarMoved. Walking out of combat stows the weapon (FCruEngine::AvatarWalked, like the original mover).
  Shapes 0x59a and 0x4cc are atlas --auto extras, so every level was rebuilt. Verified in PIE: notes/reticle_guard_pie.png
  (guard room, Silencer.Face 13 + Silencer.Fire, reticle on the guard's chest after the 2 s scan).
- Silencer.CombatDebug 0|1 toggles GCruCombatDebug at runtime (attack, visibility and reticle scan logs).
- Key rebinding (the original has fixed keys): FCruKeyBindings (CrusaderKeyBindings.*) holds one entry per action with
  current and default keys, saved as Saved/SaveGames/crusader_keys.json. ASilencerPawn::RebuildBindings re-maps the
  Enhanced Input context from the table (UnmapAll + RequestRebuildControlMappings). Console: Silencer.Binds,
  Silencer.Bind ACTION KEY [KEY...] (FKey names: SpaceBar, LeftMouseButton, F5...), Silencer.ResetBinds. In game F10
  (action "Bindings") opens the menu drawn by ACrusaderHUD::DrawBindingsMenu; ACrusaderPlayerController::InputKey consumes
  presses while it is open (Up/Down select, Enter rebind, Space add a key, Backspace clear, R defaults, Esc/F10 close);
  releases pass through so held movement keys stop. A key taken for one action is removed from the others.
  Defaults: WASD/arrows, Shift run, LMB/LeftCtrl/F fire, E/Enter/Space use, C kneel, Tab/Q next weapon, Z previous,
  U use item, I next item, =/- zoom, F5/F9 save/load, F10 bindings.

## Standalone Mac build (2026-10-14)

- `tools/mac_build.sh [Development] [outdir]` produces `build/Mac/CrusaderRemake.app` (2.3 GB, self-contained: the pak with
  the 25 `*_3D_HD` levels, atlases, music, sfx and the avatar flipbooks lives under Contents/UE). The game still reads the
  original data from the absolute `$CRUSADER_ROOT/data` (ACrusaderLevelInfo::DataDir), so the app only works on this
  machine unless that path exists. Config: Config/DefaultGame.ini lists MapsToCook (the HD levels) and
  DirectoriesToAlwaysCook for assets loaded by path at runtime (Shapes/0001, SFX, Music); DefaultEngine.ini points the
  Mac entitlements at Build/Mac/Resources/NoSandbox.entitlements because the engine default enables the App Sandbox,
  which blocks reading the data folder ("Failed to read file .../EUSECODE.FLX").
- Gotchas: (1) the game target failed to compile for the standalone target on editor-only calls (SetActorLabel,
  IsDefaultTexture, UPaperSprite::GetSourceTexture), now behind WITH_EDITOR / WITH_EDITORONLY_DATA. (2) UBT's
  "finalize the .app with Xcode" step fails when run from a non-interactive shell: the nested UBT that runs xcodebuild
  has a closed stdin and Xcode reports "Failed to launch task /bin/sh: Bad file descriptor" for the scheme pre-action
  (reproducible with `xcodebuild ... <&-`). The script runs xcodebuild itself with an open stdin, then writes the
  missing .target receipt with `UnrealBuildTool.dll -Mode=WriteMetadata -Input=.../TargetMetadata.json -Version=2`
  and stages with RunUAT -skipbuild. From a normal terminal Build.sh may well work directly. (3) RunUAT -archive only
  copies the bare app; the complete app is Saved/StagedBuilds/Mac/CrusaderRemake.app (the script copies that).
- Smoke test: the packaged app loads the usecode, runs mission 1 at ~85 fps and plays the intro cutscene.

## Menus, mission picker, volumes and the cursor fix (2026-10-14)

- Menus (ACrusaderPlayerController + ACrusaderHUD::DrawMenu, keyboard driven, Esc/Enter/Up/Down/Left/Right): the main
  menu opens on a fresh start (New Game, Load Game, Mission Picker, Options, Quit) and Esc opens the pause menu in play
  (Resume, Save Game, Load Game, Mission Picker, Options, Quit). Options: Key Bindings (the F10 menu), Music Volume,
  Sound Volume (0-10, FCruSettings in Saved/SaveGames/crusader_settings.json, applied by
  UCrusaderGameSubsystem::ApplyAudioSettings and on every sfx/music spawn) and the Key Help Overlay toggle (F1). While a
  menu is open the subsystem skips the engine tick and pauses a running cutscene. Console: Silencer.Menu open NAME |
  close | up | down | enter | left | right | esc, Silencer.Mission N.
- Mission picker: mission N -> first map {1,3,5,7,10,11,13,15,17,19,21,23,25,27,29}; FCruEngine::StartMissionAt
  snapshots the avatar into the carry-over with Egg 30 (the mission start egg; TeleportToEgg now falls back to the
  lowest-numbered destination egg when a map has no egg 30) and bTestLoadout, which gives a BA-40 if the avatar has no
  weapon. New Game clears the carry-over and reopens Map01 (GCruSkipMenuOnce keeps the main menu closed after reloads).
- Cursor fix: the earlier GameAndUI input mode left the OS cursor free and the viewport without keyboard focus (keys did
  nothing, clicks fired). Now the OS cursor is hidden and locked (FInputModeGameOnly, DefaultViewportMouseLockMode=
  LockAlways) and ASilencerPawn::CursorPos is a software cursor driven by the Mouse2D deltas (IA_Look); the HUD draws the
  crosshair there and aiming deprojects that point. The key help overlay (top right) lists the current bindings.
- Shots through walls: Silencer.SweepCheck [radius] sweeps a bullet box through every solid item near the avatar and
  reports misses; 0 of 644 items missed at the mission 1 start, so the sweep test itself is sound. Silencer.CombatDebug 1
  now logs each shot's start/destination (with "(mouse aim)") and each projectile hit with positions, to pin down the
  reported case (a tank destroyed behind a wall; candidates: a low wall the bullet flies over at gun height, splash from
  an explosive hit on the near side, or the muzzle offset starting the bullet past a thin wall).

## Combat stance, cursor lock and fullscreen (2026-10-14)

- Original aiming/firing (ScummVM CruAvatarMoverProcess + metaengine keymap): Space fires, `c` is "Draw weapon / Combat"
  (readyWeapon anim, combat mode), LCtrl+direction = jump / roll left-right / crouch (back), LAlt+direction = sidestep,
  advance, retreat, `w` cycles weapons, `i` items, `u` uses an item, Enter uses the selection, Shift runs. Firing does NOT
  require the stance: tryAttack from normal mode sets the combat flag and plays the attack anim at once (the attack anim
  includes the draw). The stance matters for turning in place (left/right turn the aim in combat mode, 16 directions
  via _avatarAngle), retreating while facing the target (back), kneeling and rolls; moving forward stows the weapon.
- Port: `Combat` action (default C; Kneel moved to X) -> FCruEngine::ToggleCombat (readySmall/Large or unreadySmall/
  Large + ACT_INCOMBAT). In the stance the pawn turns in place with left/right (one of 16 directions every 0.12 s,
  keyboard turns override the mouse facing for 0.5 s), back retreats along the facing (retreatSmall/Large flipbooks 2/45,
  imported with import_crusader.py --actions 0,1,2,3,4,37,45), forward walks and stows (AvatarWalked). While standing
  still in the stance the engine draws the avatar item with the combatStand frame for the facing
  (FCruEngine::SetAvatarStanceDir, bAvatarStanceIdle, ShouldRenderAvatar); flipbooks 4/37 are the pawn fallback.
  Console: Silencer.Combat. Rolls / sidesteps / jumps are not ported yet.
- Cursor: Slate's capture needs a click it did not always get on macOS, so ASilencerPawn::UpdateCursorLock locks and
  hides the OS cursor itself whenever the game window is in the foreground (ICursor::Lock to the window rect, Show(false),
  SetHighPrecisionMouseMode) and releases it when the window loses focus; OnLook ignores deltas > 250 px (warps).
  Config/DefaultGameUserSettings.ini defaults to a 1600x1000 window (the windowed-fullscreen default hung the packaged app at startup when launched from a shell); Options has a Display < Windowed / Fullscreen > entry.
  Modules Slate, SlateCore, ApplicationCore, AudioMixer added to CrusaderRemake.Build.cs.

## Original intro, menu, difficulty, credits and the rest of the moves (2026-10-14)

- Start flow (StartCrusaderProcess): a fresh start plays FLICS T01 (Origin logo) then T02 (fire / Esc skip), then the
  original menu (CruMenuGump: gumps 54-57 background quarters and 58-63 two-frame buttons, MISC.PAL -> re-extracted
  with tools/shape.py --sheet into data/extracted/gumps_misc) with menu music track 20; Esc in play opens the same
  menu. New Game -> difficulty screen (DifficultyGump: gumps 73-77, DIFF.PAL -> gumps_diff; Mama's Boy, Weekend Warrior,
  Loose Cannon, No Remorse; GCruDifficulty is applied to every loaded map, saved in checkpoints and used by the damage
  scaling and NPC timeouts already ported) then mission 1. Load / Save Game use slot 0, Options is our text menu (keys,
  volumes, key help, display, mission picker), Credits scrolls data/cd/STATIC/CREDITS.DAT (title/name screens with the
  original delays; the CRED.DAT background is not drawn), Quit quits. Menus take the keyboard (Up/Down/Enter/Esc/1-6)
  and the mouse (ACrusaderPlayerController::HitRects filled by the HUD). The subsystem's StartupStep drives the intro;
  UpdateMovie(DeltaTime) is shared with cutscenes. Console: Silencer.SkipIntro (straight into the game),
  Silencer.Menu open main|options|missions|difficulty|credits|bindings, Silencer.Anim N.
- Mike's key scheme: left mouse fires, and the first click only draws the weapon (AvatarFire -> ToggleCombat; the
  original fired at once); right mouse draws / holsters; C kneels; LeftCtrl + direction = jump (jumpForward 56), crouch
  (back, kneelStartCru 40) or combat roll (left/right 61/62, kneeling 23/24); LeftAlt + direction = sidestep
  (slideLeft/Right 28/29), advance (36/44, kneeling 52) or retreat (2/45, kneeling 51). ASilencerPawn::TryModifierMove
  issues one FCruEngine::AvatarMove per key press; the engine animation moves the avatar item and the pawn follows it
  every tick while the engine draws the avatar (rolls move about a tile). Verified in PIE on mission 2's start pad.
- Note: mission 1 starts with a scripted sequence (MISS1EGG ordinal20: the Silencer walks in and kneels) during which
  the usecode keeps the avatar in stasis; moves and item use are refused until it ends, as in the original.

## Fixes after Mike's first package test (2026-10-14)

- Avatar stasis: the mission cachein scripts (MISS1EGG::cachein etc.) call setAvatarInStasis(1) while the map loads and
  the original engine clears it in StartCrusaderProcess once play begins (start_crusader_process.cpp:144). We never did,
  so moves / item use were refused forever. FCruEngine::StartPlaying() clears it on New Game, mission starts, loads and
  Silencer.SkipIntro. Intrinsics 0x62/0x93/0xD3 reported as "Unused" are null in ScummVM too (no-ops).
- Cutscenes: the intro movies (T01/T02) are played by the subsystem, not the usecode; they must not call
  FCruEngine::MovieFinished (bIntroMovie), and PlayMovie's StopMovie() of the previous movie must not either, otherwise
  the pending mission cutscene (1a, requested during map load) is dropped. The movie intrinsic now returns the
  FMovieProcess pid so the usecode's `implies` really waits for the cutscene.
- Avatar ownership: ShouldRenderAvatar is true for any running animation process (usecode walk-ins included) and the
  pawn follows the item only while CruActor::IsBusy, not while the engine draws the idle stance (that pinned the pawn
  after the first click and made the Silencer unable to leave the spawn).
- Cursor: no high-precision mouse mode; the hidden OS cursor is locked to the window and its viewport position is the
  crosshair (menus included), with Mouse2D deltas only marking mouse activity for aiming.
- Reference: the longplay "Crusader： No Remorse Longplay (PC) [Mission 01].mkv" in the project root; ffmpeg contact
  sheets (fps=1/5, tile=6x5) are a quick way to check the original sequence (Origin/Vested Interest/Tony Zurovec titles,
  intro cinematic, Crusader title, menu, then the Maxis briefing cutscene and the first room).
- pie_screenshot.py: --then2 / --then2-delay add a second command stage before the second shot.

## The real mission 1 start (2026-10-14)

- Our Map01 level had always spawned the player at the builder's --start (the console room from the first vertical
  slice at world 56798,48318), which is not where the game begins. The original (StartCrusaderProcess) does
  teleport(1, 0x1e): destination egg 30 sits in the map corner (world 12798,12798) next to a source teleporter that
  chains to egg 99 at world (60656,59312,16), the fenced platform with the console, barrels and the rebel contact seen
  in the longplay (ScummVM's own warp note for map 1 is 60716,59400,16). The chain is normally blocked by the
  just-teleported flag; the original clears it for this start, so TeleportToEgg(1, 30) now clears World->bJustTeleported
  and FCruEngine::StartMission1 (called by the subsystem for a fresh map 1 start and by the mission picker) does the
  teleport and sets the respawn point to egg 99. Respawn moves the pawn to the avatar item instead of the builder start.
- With the right start, egg id 1 of MISS1EGG hatches: the walk-in sequence (stasis 1 -> rebel contact shape 0x4E0,
  doors, NPC::ordinal22 steps, kneel) runs on the engine-rendered avatar and clears stasis at its end, after which the
  player is free. Verified in PIE (notes: ScreenShot 162/163 of the session): the fenced platform matches the longplay.

## Packaged-game crash: garbage-collected sound waves (2026-10-14)

- Mike's standalone run crashed with SIGBUS in UGameplayStatics::PlaySound2D from UCrusaderGameSubsystem::PlaySfx: the
  sfx cache was a plain `TMap<int32, USoundWave*>` (no UPROPERTY), so the packaged game's garbage collector freed the
  loaded waves while the cache still pointed at them (the editor keeps assets alive longer, which hid it). It is now
  `UPROPERTY() TMap<int32, TObjectPtr<USoundWave>>`. Rule for this project: every UObject pointer held across frames
  in a UObject (subsystem, HUD, pawn) must be a UPROPERTY. Crash reports: ~/Library/Logs/DiagnosticReports/
  CrusaderRemake-*.ips (JSON after the first line; the faulting thread's frames carry symbols in Development builds).

## Suggested next steps toward Unreal

1. Asset pipeline: shapes -> PNG atlases + JSON (done) -> upscale (ESRGAN-style, per-palette
   consistency matters; keep the 0xFF key colour as alpha) -> UE Paper2D/Flipbooks or
   3D billboards. Use ANIM.DAT (ScummVM gfx/anim_dat.cpp, world/actors/anim_action.cpp)
   to group frames into actions/directions.
2. Levels: map JSON + TYPEFLAG footprints -> UE actors; ScummVM's item_sorter.cpp gives
   the exact isometric transform (the engine is a 3D world with 2D sprites, so a true 3D
   Unreal scene with an orthographic camera reproduces it faithfully).
3. Logic: port usecode classes by reading crusader_disasm.txt plus the ScummVM intrinsic
   implementations rather than running a usecode VM in Unreal. Core engine behaviour
   (movement, combat, pathfinding, AI tactics in COMBAT.DAT) lives in ScummVM's
   world/actors/*.cpp and in ghidra/out/CRUSADER_EXE_decomp.c.
4. Video/music: write a RRV/JYV1 -> PNG sequence converter from image/codecs/jyv1.cpp;
   convert AMF with ScummVM's tracker code or openmpt (AMF is DSMI format, openmpt supports it).
