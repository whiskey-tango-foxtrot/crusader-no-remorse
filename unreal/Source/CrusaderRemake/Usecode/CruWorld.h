#pragma once
#include "CruCore.h"
#include "UCList.h"
#include "CruActors.h"

// The item world: items, map chunks, searches, fast area, eggs and usecode events.
// Port of the relevant parts of ScummVM World / CurrentMap / Item / Egg / EggHatcherProcess.
enum class ECruItemChange : uint8 { Moved, ShapeOrFrame, Flags, Created, Destroyed };

class FCruWorld
{
public:
    FCruWorld();
    ~FCruWorld();

    bool LoadShapeInfos(const FString& TypeflagsJson);
    bool LoadNpcTable(const FString& DtableJson);
    const FCruNpcDat* GetNpcData(int32 Index) const { return NpcTable.IsValidIndex(Index) ? &NpcTable[Index] : nullptr; }
    const FCruNpcDat* GetNpcDataForShape(uint32 Shape) const { for (const FCruNpcDat& N : NpcTable) if (N.Shape == Shape) return &N; return nullptr; }
    FCruItem* CreateActor(uint32 Shape, uint32 Frame, uint16 Flags, uint32 ExtFlags);
    bool LoadMap(const FString& MapJson);      // creates fixed items with SourceIndex = JSON index
    void Reset();

    // objects
    FCruItem* GetItem(ObjId Id) const { FCruItem* const* P = Items.Find(Id); return P ? *P : nullptr; }
    const FCruShapeInfo* GetShapeInfo(uint32 Shape) const { return ShapeInfos.IsValidIndex(Shape) ? &ShapeInfos[Shape] : nullptr; }
    const FCruShapeInfo& Info(const FCruItem& It) const { static FCruShapeInfo Empty; const FCruShapeInfo* P = GetShapeInfo(It.Shape); return P ? *P : Empty; }
    FCruItem* CreateItem(uint32 Shape, uint32 Frame, uint16 Quality, uint16 Flags, uint16 NpcNum, uint16 MapNum, uint32 ExtFlags, bool bAssignId);
    FCruItem* CreateAvatar(uint32 Shape, int32 X, int32 Y, int32 Z);
    void DestroyItem(FCruItem& It);
    void FlushDestroyed();

    // placement
    void AddItemToMap(FCruItem& It, bool bToEnd);
    void RemoveItemFromMap(FCruItem& It);
    void Move(FCruItem& It, int32 X, int32 Y, int32 Z);
    void SetShape(FCruItem& It, uint32 Shape);
    void SetFrame(FCruItem& It, uint32 Frame);
    void SetFlags(FCruItem& It, uint16 Flags);
    void MoveToEtherealVoid(FCruItem& It);
    void ReturnFromEtherealVoid(FCruItem& It);
    bool EtherealEmpty() const { return Ethereal.Num() == 0; }
    ObjId EtherealPeek() const { return Ethereal.Num() ? Ethereal.Last() : 0; }
    void EtherealRemove(ObjId Id) { Ethereal.Remove(Id); }

    // geometry
    void GetFootpadWorld(const FCruItem& It, int32& Xd, int32& Yd, int32& Zd) const { Info(It).GetFootpadWorld(Xd, Yd, Zd, It.IsFlipped()); }
    FCruBox GetWorldBox(const FCruItem& It) const { int32 Xd, Yd, Zd; GetFootpadWorld(It, Xd, Yd, Zd); return FCruBox(It.X, It.Y, It.Z, Xd, Yd, Zd); }
    FIntVector GetCentre(const FCruItem& It) const;
    bool Overlaps(const FCruItem& A, const FCruItem& B) const;
    bool OverlapsXY(const FCruItem& A, const FCruItem& B) const;
    bool IsOn(const FCruItem& A, const FCruItem& B) const;
    bool IsCompletelyOn(const FCruItem& A, const FCruItem& B) const;
    bool IsCentreOn(const FCruItem& A, const FCruItem& B) const;
    struct FPositionInfo { bool bValid = false; bool bSupported = false; const FCruItem* Blocker = nullptr; const FCruItem* Land = nullptr; const FCruItem* Roof = nullptr; };
    FPositionInfo GetPositionInfo(const FCruBox& Target, const FCruBox& Start, uint32 ShapeFlags, ObjId Ignore) const;
    FPositionInfo GetPositionInfo(int32 X, int32 Y, int32 Z, uint32 Shape, ObjId Ignore) const;
    // swept box collision (CurrentMap::sweepTest); times are 0..0x4000 along the sweep
    struct FSweepItem
    {
        ObjId Item = 0; int32 HitTime = 0, EndTime = 0; bool bTouching = false, bTouchingFloor = false, bBlocking = false; uint8 Dirs = 0;
        FIntVector GetInterpolatedCoords(const FIntVector& Start, const FIntVector& End) const
        {
            const int32 T = HitTime >= 0 ? HitTime : 0;
            return FIntVector(Start.X + ((End.X - Start.X) * T + (End.X > Start.X ? 0x2000 : -0x2000)) / 0x4000,
                              Start.Y + ((End.Y - Start.Y) * T + (End.Y > Start.Y ? 0x2000 : -0x2000)) / 0x4000,
                              Start.Z + ((End.Z - Start.Z) * T + (End.Z > Start.Z ? 0x2000 : -0x2000)) / 0x4000);
        }
    };
    bool SweepTest(const FIntVector& Start, const FIntVector& End, const int32 Dims[3], uint32 ShapeFlags, ObjId Item, bool bBlockingOnly, TArray<FSweepItem>* Hit) const;
    // CurrentMap::scanForValidPosition: look around (x,y,z) orthogonally to MoveDir for a spot the item fits (and is supported)
    bool ScanForValidPosition(int32 X, int32 Y, int32 Z, const FCruItem& Item, int32 MoveDir, bool bWantSupport, int32& Tx, int32& Ty, int32& Tz) const;

    // searches (loop opcode support)
    bool CheckLoopScript(const FCruItem& It, const uint8* Script, uint32 Size) const;
    void AreaSearch(FUCList& Out, const uint8* Script, uint32 Size, uint16 Range, int32 X, int32 Y) const;
    void SurfaceSearch(FUCList& Out, const uint8* Script, uint32 Size, const FCruItem& Check, bool bAbove, bool bBelow, bool bRecurse) const;
    void ContainerSearch(FUCList& Out, const uint8* Script, uint32 Size, ObjId Container, bool bRecurse) const;
    const TArray<ObjId>& ChunkItems(int32 Cx, int32 Cy) const { return Chunks[Cy * Cru::MAP_NUM_CHUNKS + Cx]; }

    // usecode events
    uint32 CallUsecodeEvent(FCruItem& It, uint32 Event, const uint8* Args = nullptr, int32 ArgSize = 0);
    uint32 EnterFastArea(FCruItem& It);
    void LeaveFastArea(FCruItem& It);
    bool IsChunkFast(int32 Cx, int32 Cy) const { return Cx >= 0 && Cy >= 0 && Cx < Cru::MAP_NUM_CHUNKS && Cy < Cru::MAP_NUM_CHUNKS && Fast[Cy * Cru::MAP_NUM_CHUNKS + Cx]; }
    void UpdateFastArea(const FIntVector& From, const FIntVector& To);
    void SetFastAtPoint(int32 X, int32 Y);
    void RunEggHatcher();
    uint32 HatchEgg(FCruItem& Egg);
    uint32 UnhatchEgg(FCruItem& Egg);

    // change notification (renderer / collision hookup)
    TFunction<void(const FCruItem&, ECruItemChange)> OnItemChanged;
    void Notify(const FCruItem& It, ECruItemChange C) { if (OnItemChanged) OnItemChanged(It, C); }

    // sound requests from usecode (consumed by the game layer)
    struct FSfxRequest { ObjId Item; int32 Sfx; bool bStop; bool bAmbient; };
    TArray<FSfxRequest> SfxQueue;
    // sounds the game layer reports as playing (Sfx -1 = any sound for the item)
    struct FPlayingSfx { ObjId Item; int32 Sfx; uint32 EndTick; };
    mutable TArray<FPlayingSfx> PlayingSfx;
    void NoteSfxStarted(ObjId Item, int32 Sfx, uint32 DurationTicks);
    void NoteSfxStopped(ObjId Item, int32 Sfx);
    bool IsSfxPlaying(ObjId Item, int32 Sfx) const;

    ObjId AvatarId = Cru::kMainActorId;
    ObjId ControlledNpc = Cru::kMainActorId;
    bool bAlertActive = false;
    bool bJustTeleported = false;                        // MainActor::_justTeleported: no teleport-egg hatching until out of range
    int32 GameDifficulty = 2;
    int32 ScreenW = 640, ScreenH = 480;   // original view used for the fast-area limits
    uint32 NumItems() const { return Items.Num(); }
    const TMap<ObjId, FCruItem*>& AllItems() const { return Items; }
    TArray<ObjId> Eggs;
    FCruAnimDat AnimDat;

private:
    TArray<FCruShapeInfo> ShapeInfos;
    TArray<FCruNpcDat> NpcTable;
    TMap<ObjId, FCruItem*> Items;
    TArray<FCruItem*> PendingDelete;
    TArray<TArray<ObjId>> Chunks;        // MAP_NUM_CHUNKS^2 lists of ObjIds (front = drawn first in the original)
    TArray<bool> Fast;
    TArray<ObjId> Ethereal;
    int32 FastXMin = -1, FastYMin = -1, FastXMax = -1, FastYMax = -1;
    ObjId NextObjId = 256;
    ObjId AllocObjId();
    void SetChunkFast(int32 Cx, int32 Cy);
    void UnsetChunkFast(int32 Cx, int32 Cy);
    uint32 UsecodeClassFor(const FCruItem& It) const;
};

extern FCruWorld* GCruWorld;
