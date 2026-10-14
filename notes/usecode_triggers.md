# How Crusader doors and triggers actually work (usecode findings, 2026-10-13)

Source: reference/crusader-disasm/crusader_disasm.txt (mduggan) + ScummVM engines/ultima/ultima8.

## Events (ScummVM item.cpp)
0 look, 1 use, 2 anim, 4 cachein, 5 hit, 6 gotHit, 7 hatch, 8 schedule, 9 release, A equip, B unequip,
C combine, E calledFromAnim, F (15) enterFastArea, 10 leaveFastArea, 12 justMoved, 15h (21) unhatch.
The disassembly labels events in decimal ("Event 15" = enterFastArea).

## Eggs (shape 17, family 4 = UNKEGG)
- Usecode class = 0x900 + quality: 0 TRIGEGG, 1 ONCEEGG (destroys itself after use), 10 DOOREGG, 13 MISS1EGG ... (classes 2304-2333).
- Range: npcnum high nibble = x range, low nibble = y range, in units of 64 world units (EggHatcherProcess, z range 48).
- Egg id = mapnum field (Egg::I_getEggId).
- Mission 1: 128 eggs, all TRIGEGG/ONCEEGG. No DOOREGGs.
- TRIGEGG/ONCEEGG: hatch -> TRIGGER::ordinal20(egg, 0x80 | 0); enterFastArea -> TRIGGER::ordinal20(egg, 0x80 | 1).
  (They temporarily set the egg's qlo = egg id so TRIGGER can read it as the link.)

## Trigger markers (shape 1201, editor-only, 308 in mission 1)
TRIGGER::ordinal20(ref, phase): newphase = phase & 1 (0 = hatch, 1 = enterFastArea). Area search (loop type 2,
range 5*32*2 = 320 world units around ref) for shape 1201 with qlo == ref.qlo. For each marker: map = marker.mapnum;
cmdphase = (map & 8) ? 0 : 1; markers whose cmdphase == newphase fire (map & 0x10 = low priority, run after the rest).
Result newLink can chain into another round (baseLink = newLink).
TRIGGER::ordinal21(marker): link = qlo, qHi = qhi, searchtype (= a shape number) = (map & 0xE0) * 8 + npcnum.
  map == 0 -> nothing. map & 4 clear: NPC targets (isCrusTypeNPC) / ordinal23-25. map & 4 set: link == 0xFF -> ordinal28,
  searchtype == 0x7FF -> ordinal27, else ordinal26(searchtype, link, map, npc, qHi, ref).
TRIGGER::ordinal26: cmd = map & 3. 0 setQHi, 1 setQLo, 2 equip on all items of shape searchtype with qlo == link
  (range global[40]*32 = 100*32*2 = effectively the map). cmd 3: sub = qHi & 7, num = qHi >> 3:
  0 (shape 1142 helpers, FREE::ordinal30), 1 replace/spawn (I_spawnUsecodeEvent0x13), 2 setFrame(num),
  3 -> TRIGGER::ordinal22(item, num) on each matching item, 4 newLink += num, 5 newLink -= num, 6 create items.
TRIGGER::ordinal22(item, num): waits while item status & 0x1000 (busy), sets it, spawns DOOR::ordinal21(item, num), clears it.
Other callers of TRIGGER::ordinal20: switches, keycards, consoles etc. (ref = that item, link = its qlo).

## Doors
Mission 1 door shapes: RDOOR_N (5, footpad 6x1x11) and RDOOR_E (123, 1x6x11), 15 each, 2 frames = 2 visual variants
(both closed). The door classes themselves only define gotHit (-> DOOR::ordinal23, spawns a damage/shake effect).
DOOR::ordinal21(door, mode) dispatches by shape:
  shape 5: mode 2 -> return 0; else DOOR2::ordinal28 (open); if mode == 1 also FREE::ordinal31 on shape 810 (EWALLNS)
           items within 3 tiles with the same qlo.
  shape 194 (0xC2, the fully open N door): mode 1 -> return 0; else DOOR2::ordinal29 (close).
  shape 123: mode 2 -> return 0; else DOOR2::ordinal2A (open). shape 203 (0xCB): DOOR2::ordinal2B (close).
  Other door families (MDOOR 70/169, GDOOR, SDOOR, 208, 263, 725/726/731 ...) have their own DOOR2 ordinals.
DOOR2::ordinal28 (open RDOOR_N): if status & 4 (FLG_OWNED = locked in Crusader) -> do nothing.
  DOOR::ordinal22 first: destroys roof pieces (shapes 935, 936, 538, 302, 1308, 1307) whose qlo matches the door's
  qlo or qhi, within 100 tiles -- this is how rooms get uncovered when their door opens.
  Then: play SFX 0x51 (81), shape list [190, 191, 192, 193, 202, 194] (footpads 5..0 wide), for i = 2..6:
  setShape(list[i]), push, popToCoords(x - 16, y, z), wait 3 ticks (30 Hz). Door ends as shape 194 (0 wide, no block).
DOOR2::ordinal29 (close): reverse walk of the list, each step checked with FREE::ordinal2D (legalCreateAtCoords) and
  retried after 3 ticks while blocked, wait 5 ticks per step, x + 16 per step, SFX 81 stopped at the end.
DOOR2::ordinal2A/2B (RDOOR_E): same with shapes [195, 196, 197, 198, 199, 203] and y -/+ 16 (I_move).
DOOR::ordinal20: wrapper that retries DOOR::ordinal21(door, 0) every 10 ticks until it succeeds (used by EVENT,
  SECRTEGG, MISS1EGG, MISS2...).
Monsters (class 1232) and ELEVATOR also call DOOR::ordinal21 (mode 1/2) to open doors they walk through.

## Mission 1 door wiring (data)
11 markers target door shapes (5/123); 7 of them are enterFastArea-phase (doors that open as soon as their area
streams in, i.e. effectively start open), 4 are hatch-phase (open when the player steps into the egg zone).
The egg for a marker is the one with id == link within 320 units; where none exists the marker is reached by
chaining or by another item (switch/console) with that qlo. Doors are closed again by monsters/elevators/scripts
calling mode 2/close; there is no automatic timer close in DOOR2::ordinal28.

## Implication
Door behaviour is not a property of the door shape; it is the TRIGGER usecode graph driven by eggs, markers and
other items. Hand-porting it class by class means re-deriving the whole scripting system. The faithful route is
to run the game's usecode: ScummVM's UCMachine (usecode/uc_machine.cpp 2414 lines + uc_process/uc_list/uc_stack,
usecode_flex loader, kernel/process ~700 lines) plus the Crusader intrinsic table (convert_usecode_crusader.h,
~300 entries, mostly Item/Actor/Egg/Audio/Kernel calls that map onto engine state we already have in JSON).
