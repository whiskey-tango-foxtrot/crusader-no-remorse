// Core types for the Crusader usecode engine port.
// Ported from ScummVM engines/ultima/ultima8 (GPL) for this personal remake; see notes/README.md.
#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCru, Log, All);

typedef uint16 ObjId;
typedef uint16 ProcId;

namespace Cru
{
    // Item status flags (Item::statusflags)
    enum : uint16
    {
        FLG_DISPOSABLE = 0x0002, FLG_OWNED = 0x0004, FLG_CONTAINED = 0x0008, FLG_INVISIBLE = 0x0010,
        FLG_FLIPPED = 0x0020, FLG_IN_NPC_LIST = 0x0040, FLG_FAST_ONLY = 0x0080, FLG_GUMP_OPEN = 0x0100,
        FLG_EQUIPPED = 0x0200, FLG_BOUNCING = 0x0400, FLG_ETHEREAL = 0x0800, FLG_HANGING = 0x1000,
        FLG_FASTAREA = 0x2000, FLG_LOW_FRICTION = 0x4000, FLG_BROKEN = 0x8000
    };
    // Extended flags
    enum : uint32
    {
        EXT_FIXED = 0x0001, EXT_INCURMAP = 0x0002, EXT_LERP_NOPREV = 0x0008, EXT_HIGHLIGHT = 0x0010,
        EXT_CAMERA = 0x0020, EXT_SPRITE = 0x0040, EXT_TRANSPARENT = 0x0080, EXT_PERMANENT_NPC = 0x0100,
        EXT_TARGET = 0x0200, EXT_FEMALE = 0x8000
    };
    // ShapeInfo flags (SI_*)
    enum : uint32
    {
        SI_FIXED = 0x0001, SI_SOLID = 0x0002, SI_SEA = 0x0004, SI_LAND = 0x0008, SI_OCCL = 0x0010, SI_BAG = 0x0020,
        SI_DAMAGING = 0x0040, SI_NOISY = 0x0080, SI_DRAW = 0x0100, SI_IGNORE = 0x0200, SI_ROOF = 0x0400,
        SI_TRANSL = 0x0800, SI_EDITOR = 0x1000, SI_CRU_SELECTABLE = 0x2000, SI_CRU_PRELOAD = 0x4000,
        SI_CRU_SOUND = 0x8000, SI_CRU_TARGETABLE = 0x10000, SI_CRU_NPC = 0x20000, SI_CRU_UNK66 = 0x40000, SI_CRU_UNK67 = 0x80000
    };
    enum Family : uint8
    {
        SF_GENERIC = 0, SF_QUALITY = 1, SF_QUANTITY = 2, SF_GLOBEGG = 3, SF_UNKEGG = 4, SF_BREAKABLE = 5,
        SF_CONTAINER = 6, SF_MONSTEREGG = 7, SF_TELEPORTEGG = 8, SF_REAGENT = 9, SF_CRUWEAPON = 10,
        SF_CRUAMMO = 11, SF_CRUBOMB = 12, SF_CRUINVITEM = 13, SF_15 = 15
    };
    // usecode events
    enum : uint32
    {
        EV_LOOK = 0, EV_USE = 1, EV_ANIM = 2, EV_CACHEIN = 4, EV_HIT = 5, EV_GOTHIT = 6, EV_HATCH = 7, EV_SCHEDULE = 8,
        EV_RELEASE = 9, EV_EQUIP = 0xA, EV_UNEQUIP = 0xB, EV_COMBINE = 0xC, EV_CALLEDFROMANIM = 0xE,
        EV_ENTERFASTAREA = 0xF, EV_LEAVEFASTAREA = 0x10, EV_CAST = 0x11, EV_JUSTMOVED = 0x12,
        EV_AVATARSTOLESOMETHING = 0x13, EV_UNHATCH = 0x15
    };
    constexpr ObjId kMainActorId = 1;
    constexpr int32 MAP_CHUNK_SIZE = 1024;   // Crusader (U8 uses 512)
    constexpr int32 MAP_NUM_CHUNKS = 64;
}

// Typeflag record for one shape (TYPEFLAG.DAT, Crusader layout).
struct FCruShapeInfo
{
    uint32 Flags = 0;      // SI_* bits
    uint8 Family = 0;
    uint8 X = 0, Y = 0, Z = 0;   // footpad in tiles (x,y) / 8-unit steps (z)
    uint8 AnimType = 0, AnimData = 0, AnimSpeed = 1;
    uint8 Weight = 0, Volume = 0;
    bool IsSolid() const { return (Flags & Cru::SI_SOLID) != 0; }
    bool IsRoof() const { return (Flags & Cru::SI_ROOF) != 0; }
    bool IsLand() const { return (Flags & Cru::SI_LAND) != 0; }
    bool IsEditor() const { return (Flags & Cru::SI_EDITOR) != 0; }
    bool IsNoisy() const { return (Flags & Cru::SI_NOISY) != 0; }
    bool IsTargetable() const { return (Flags & Cru::SI_CRU_TARGETABLE) != 0; }
    bool IsNpc() const { return (Flags & Cru::SI_CRU_NPC) != 0; }
    void GetFootpadWorld(int32& Xd, int32& Yd, int32& Zd, bool bFlipped) const
    {
        Zd = Z * 8;
        if (bFlipped) { Xd = Y * 32; Yd = X * 32; } else { Xd = X * 32; Yd = Y * 32; }
    }
};

// DTABLE.FLX record (tools/dtable.py -> data/extracted/dtable.json)
struct FCruNpcDat
{
    FString Name;
    uint16 MinHp = 0, MaxHp = 0, Shape = 0, WpnType = 0, WpnType2 = 0;
    uint16 DefaultActivity[3] = { 0, 0, 0 };
};

// Axis-aligned box in the engine's convention: (x,y) is the max corner, extends to x-xd, y-yd; z is the bottom.
struct FCruBox
{
    int32 X = 0, Y = 0, Z = 0, Xd = 0, Yd = 0, Zd = 0;
    FCruBox() {}
    FCruBox(int32 InX, int32 InY, int32 InZ, int32 InXd, int32 InYd, int32 InZd) : X(InX), Y(InY), Z(InZ), Xd(InXd), Yd(InYd), Zd(InZd) {}
    bool ContainsXY(int32 Px, int32 Py) const { return Px > X - Xd && Px <= X && Py > Y - Yd && Py <= Y; }
    bool IsBelow(int32 Px, int32 Py, int32 Pz) const { return Px > X - Xd && Px <= X && Py > Y - Yd && Py <= Y && Pz >= Z + Zd; }
    bool Overlaps(const FCruBox& O) const
    {
        if (X <= O.X - O.Xd || O.X <= X - Xd) return false;
        if (Y <= O.Y - O.Yd || O.Y <= Y - Yd) return false;
        if (Z + Zd <= O.Z || O.Z + O.Zd <= Z) return false;
        return true;
    }
    bool OverlapsXY(const FCruBox& O) const
    {
        if (X <= O.X - O.Xd || O.X <= X - Xd) return false;
        if (Y <= O.Y - O.Yd || O.Y <= Y - Yd) return false;
        return true;
    }
};

// A world item (Item / Egg / Actor are distinguished by flags and family, not by class).
struct FCruItem
{
    ObjId Id = 0;
    uint32 Shape = 0;
    uint32 Frame = 0;
    int32 X = 0, Y = 0, Z = 0;       // world units (file coords * 2)
    uint16 Flags = 0;
    uint32 ExtFlags = 0;
    uint16 Quality = 0;
    uint16 NpcNum = 0;
    uint16 MapNum = 0;
    int32 SourceIndex = -1;          // index in the map JSON (for the renderer), -1 for runtime-created items
    bool bHatched = false;           // eggs
    // actors (objId < 256)
    bool bActor = false;
    bool bDead = false;
    int32 Dir = 0;                   // facing direction (16 dirs)
    int32 Hp = 0, Str = 0;
    uint16 DefaultActivity[3] = { 0, 0, 0 };
    int32 CurrentActivity = 0, LastActivity = 0;
    uint8 UnkByte = 0;
    uint16 CombatTactic = 0;
    ObjId ActiveWeapon = 0;
    int32 HomeX = 0, HomeY = 0, HomeZ = 0;
    uint32 ActorFlags = 0;
    int32 LastAnim = -1;
    uint32 AnimFrame = 0;
    uint8 DamagePoints = 0;          // Crusader item damage (DAMAGE.FLX)
    int32 LastTickWasHit = 0;
    bool bAttackAimFlag = false;
    uint32 AttackMoveStartFrame = 0, AttackMoveTimeout = 0;
    uint16 AttackMoveDodgeFactor = 1;
    int32 Mana = 0;                  // energy (avatar)
    // containers (ScummVM Container / Item::_parent): actors and SF_CONTAINER shapes hold items
    ObjId Parent = 0;                // container this item is in (FLG_CONTAINED / FLG_EQUIPPED)
    TArray<ObjId> Contents;          // items inside this container, in insertion order
    uint32 GetAttackMoveTimeoutFinishFrame() const { return AttackMoveStartFrame + AttackMoveTimeout; }

    // egg ranges live in NpcNum (Egg::getXRange / getYRange)
    int32 EggXRange() const { return (NpcNum >> 4) & 0xF; }
    int32 EggYRange() const { return NpcNum & 0xF; }
    bool IsFlipped() const { return (Flags & Cru::FLG_FLIPPED) != 0; }
};
