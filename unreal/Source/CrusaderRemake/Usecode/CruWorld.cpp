#include "CruWorld.h"
#include "CruInventory.h"
#include "CruCombat.h"
#include "CruEngine.h"
#include "UCMachine.h"
#include "UCProcess.h"
#include "Kernel.h"
#include "UsecodeFlex.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FCruWorld* GCruWorld = nullptr;

FCruWorld::FCruWorld()
{
    GCruWorld = this;
    Chunks.SetNum(Cru::MAP_NUM_CHUNKS * Cru::MAP_NUM_CHUNKS);
    Fast.SetNumZeroed(Cru::MAP_NUM_CHUNKS * Cru::MAP_NUM_CHUNKS);
}

FCruWorld::~FCruWorld() { Reset(); if (GCruWorld == this) GCruWorld = nullptr; }

void FCruWorld::Reset()
{
    for (auto& P : Items) delete P.Value;
    Items.Reset();
    for (FCruItem* P : PendingDelete) delete P;
    PendingDelete.Reset();
    for (auto& C : Chunks) C.Reset();
    for (int32 i = 0; i < Fast.Num(); ++i) Fast[i] = false;
    Ethereal.Reset(); Eggs.Reset(); SfxQueue.Reset();
    FastXMin = FastYMin = FastXMax = FastYMax = -1;
    NextObjId = 256;
}

static bool LoadJsonFile(const FString& Path, TSharedPtr<FJsonValue>& Out)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogCru, Error, TEXT("cannot read %s"), *Path); return false; }
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid()) { UE_LOG(LogCru, Error, TEXT("bad json %s"), *Path); return false; }
    return true;
}

bool FCruWorld::LoadShapeInfos(const FString& TypeflagsJson)
{
    TSharedPtr<FJsonValue> Root;
    if (!LoadJsonFile(TypeflagsJson, Root)) return false;
    const TArray<TSharedPtr<FJsonValue>>& Arr = Root->AsArray();
    ShapeInfos.SetNum(Arr.Num());
    for (int32 i = 0; i < Arr.Num(); ++i)
    {
        const TSharedPtr<FJsonObject>& O = Arr[i]->AsObject();
        if (!O.IsValid()) continue;
        FCruShapeInfo& Si = ShapeInfos[i];
        const uint32 Raw = (uint32)O->GetNumberField(TEXT("flags"));
        const uint32 B6 = (uint32)O->GetNumberField(TEXT("b6"));
        // low 12 bits of the raw flags map 1:1 onto SI_FIXED..SI_TRANSL; byte 6 holds the Crusader flags
        Si.Flags = (Raw & 0x0FFF) | ((B6 & 0xFF) << 12);
        Si.Family = (uint8)O->GetNumberField(TEXT("family"));
        Si.X = (uint8)O->GetNumberField(TEXT("x")); Si.Y = (uint8)O->GetNumberField(TEXT("y")); Si.Z = (uint8)O->GetNumberField(TEXT("z"));
        Si.AnimType = (uint8)O->GetNumberField(TEXT("anim_type")); Si.AnimData = (uint8)O->GetNumberField(TEXT("anim_data")); Si.AnimSpeed = (uint8)O->GetNumberField(TEXT("anim_speed"));
        Si.Weight = (uint8)O->GetNumberField(TEXT("weight")); Si.Volume = (uint8)O->GetNumberField(TEXT("volume"));
    }
    UE_LOG(LogCru, Display, TEXT("world: %d shape infos"), ShapeInfos.Num());
    return true;
}

bool FCruWorld::LoadNpcTable(const FString& DtableJson)
{
    TSharedPtr<FJsonValue> Root;
    if (!LoadJsonFile(DtableJson, Root)) return false;
    for (const TSharedPtr<FJsonValue>& V : Root->AsArray())
    {
        const TSharedPtr<FJsonObject>& O = V->AsObject(); if (!O.IsValid()) continue;
        FCruNpcDat N; N.Name = O->GetStringField(TEXT("name"));
        N.MinHp = (uint16)O->GetNumberField(TEXT("min_hp")); N.MaxHp = (uint16)O->GetNumberField(TEXT("max_hp")); N.Shape = (uint16)O->GetNumberField(TEXT("shape"));
        N.WpnType = (uint16)O->GetNumberField(TEXT("wpn_type")); N.WpnType2 = (uint16)O->GetNumberField(TEXT("wpn_type2"));
        N.DefaultActivity[0] = (uint16)O->GetNumberField(TEXT("activity6")); N.DefaultActivity[1] = (uint16)O->GetNumberField(TEXT("activity8")); N.DefaultActivity[2] = (uint16)O->GetNumberField(TEXT("activityA"));
        NpcTable.Add(N);
    }
    UE_LOG(LogCru, Display, TEXT("world: %d npc table entries"), NpcTable.Num());
    return true;
}

FCruItem* FCruWorld::CreateActor(uint32 Shape, uint32 Frame, uint16 Flags, uint32 ExtFlags)
{
    if (!GetShapeInfo(Shape)) return nullptr;
    ObjId Id = 0;
    for (ObjId C = 2; C < 256; ++C) if (!Items.Contains(C)) { Id = C; break; }
    if (!Id) { UE_LOG(LogCru, Warning, TEXT("world: out of actor ids")); return nullptr; }
    FCruItem* A = new FCruItem();
    A->Id = Id; A->Shape = Shape; A->Frame = Frame; A->Flags = Flags; A->ExtFlags = ExtFlags | Cru::EXT_LERP_NOPREV; A->bActor = true;
    A->NpcNum = Id; A->MapNum = 0;
    if (const FCruDamageInfo* Di = CruCombat::GetDamageInfo(Shape)) A->DamagePoints = Di->DamagePoints;
    Items.Add(Id, A);
    return A;
}

ObjId FCruWorld::AllocObjId()
{
    for (int32 T = 0; T < 32000; ++T)
    {
        ObjId Id = NextObjId; NextObjId = NextObjId >= 32766 ? 256 : NextObjId + 1;
        if (!Items.Contains(Id)) return Id;
    }
    return 0;
}

FCruItem* FCruWorld::CreateItem(uint32 Shape, uint32 Frame, uint16 Quality, uint16 Flags, uint16 NpcNum, uint16 MapNum, uint32 ExtFlags, bool bAssignId)
{
    if (!GetShapeInfo(Shape)) return nullptr;
    FCruItem* It = new FCruItem();
    It->Shape = Shape; It->Frame = Frame; It->Quality = Quality; It->Flags = Flags; It->NpcNum = NpcNum; It->MapNum = MapNum; It->ExtFlags = ExtFlags | Cru::EXT_LERP_NOPREV;
    const FCruShapeInfo& Si = *GetShapeInfo(Shape);
    if (Si.Family == Cru::SF_CRUAMMO || Si.Family == Cru::SF_CRUBOMB) It->Quality = 1;
    if (const FCruDamageInfo* Di = CruCombat::GetDamageInfo(Shape)) It->DamagePoints = Di->DamagePoints;
    if (bAssignId) { It->Id = AllocObjId(); Items.Add(It->Id, It); }
    return It;
}

FCruItem* FCruWorld::CreateAvatar(uint32 Shape, int32 X, int32 Y, int32 Z)
{
    FCruItem* Av = new FCruItem();
    Av->Id = Cru::kMainActorId; Av->Shape = Shape; Av->Flags = Cru::FLG_IN_NPC_LIST; Av->ExtFlags = Cru::EXT_PERMANENT_NPC;
    Av->NpcNum = 1; Av->X = X; Av->Y = Y; Av->Z = Z; Av->Hp = 150;
    Items.Add(Av->Id, Av);
    AddItemToMap(*Av, false);
    return Av;
}

bool FCruWorld::LoadMap(const FString& MapJson)
{
    TSharedPtr<FJsonValue> Root;
    if (!LoadJsonFile(MapJson, Root)) return false;
    const TArray<TSharedPtr<FJsonValue>>& Arr = Root->AsArray();
    int32 Loaded = 0;
    for (int32 i = 0; i < Arr.Num(); ++i)
    {
        const TSharedPtr<FJsonObject>& O = Arr[i]->AsObject();
        if (!O.IsValid()) continue;
        const uint32 Shape = (uint32)O->GetNumberField(TEXT("shape"));
        if (!GetShapeInfo(Shape)) continue;
        if (GetShapeInfo(Shape)->Family == Cru::SF_GLOBEGG) continue;   // glob eggs were expanded by the extractor
        FCruItem* It = CreateItem(Shape, (uint32)O->GetNumberField(TEXT("frame")), (uint16)O->GetNumberField(TEXT("quality")),
            (uint16)O->GetNumberField(TEXT("flags")), (uint16)O->GetNumberField(TEXT("npc")), (uint16)O->GetNumberField(TEXT("map")), Cru::EXT_FIXED, true);
        if (!It) continue;
        It->SourceIndex = i;
        It->X = (int32)O->GetNumberField(TEXT("x")) * 2;
        It->Y = (int32)O->GetNumberField(TEXT("y")) * 2;
        It->Z = (int32)O->GetNumberField(TEXT("z"));
        It->Flags &= ~Cru::FLG_FASTAREA;
        AddItemToMap(*It, true);
        const uint8 Fam = GetShapeInfo(Shape)->Family;
        if (Fam == Cru::SF_UNKEGG || Fam == Cru::SF_MONSTEREGG || Fam == Cru::SF_TELEPORTEGG) Eggs.Add(It->Id);
        ++Loaded;
    }
    UE_LOG(LogCru, Display, TEXT("world: %d items loaded from %s (%d eggs)"), Loaded, *MapJson, Eggs.Num());
    // Crusader calls cachein for every item at map load
    for (auto& P : Items) if (P.Value->Id != Cru::kMainActorId) CallUsecodeEvent(*P.Value, Cru::EV_CACHEIN);
    return true;
}

void FCruWorld::AddItemToMap(FCruItem& It, bool bToEnd)
{
    if (It.X < 0 || It.X >= Cru::MAP_CHUNK_SIZE * Cru::MAP_NUM_CHUNKS || It.Y < 0 || It.Y >= Cru::MAP_CHUNK_SIZE * Cru::MAP_NUM_CHUNKS) return;
    TArray<ObjId>& C = Chunks[(It.Y / Cru::MAP_CHUNK_SIZE) * Cru::MAP_NUM_CHUNKS + It.X / Cru::MAP_CHUNK_SIZE];
    if (bToEnd) C.Add(It.Id); else C.Insert(It.Id, 0);
    It.ExtFlags |= Cru::EXT_INCURMAP;
}

void FCruWorld::RemoveItemFromMap(FCruItem& It)
{
    if (It.X < 0 || It.X >= Cru::MAP_CHUNK_SIZE * Cru::MAP_NUM_CHUNKS || It.Y < 0 || It.Y >= Cru::MAP_CHUNK_SIZE * Cru::MAP_NUM_CHUNKS) { It.ExtFlags &= ~Cru::EXT_INCURMAP; return; }
    Chunks[(It.Y / Cru::MAP_CHUNK_SIZE) * Cru::MAP_NUM_CHUNKS + It.X / Cru::MAP_CHUNK_SIZE].Remove(It.Id);
    It.ExtFlags &= ~Cru::EXT_INCURMAP;
}

void FCruWorld::Move(FCruItem& It, int32 X, int32 Y, int32 Z)
{
    const int32 Cs = Cru::MAP_CHUNK_SIZE;
    if (It.Flags & Cru::FLG_ETHEREAL) Ethereal.Remove(It.Id);
    if (It.Flags & (Cru::FLG_CONTAINED | Cru::FLG_EQUIPPED))
    {
        // leaving a container (ethereal items were already removed from it)
        if (It.Parent && !(It.Flags & Cru::FLG_ETHEREAL)) CruInventory::RemoveFromContainer(It);
        It.Parent = 0;
        It.ExtFlags |= Cru::EXT_LERP_NOPREV;
    }
    else if ((It.ExtFlags & Cru::EXT_INCURMAP) && (It.X / Cs != X / Cs || It.Y / Cs != Y / Cs)) RemoveItemFromMap(It);
    It.Flags &= ~(Cru::FLG_CONTAINED | Cru::FLG_EQUIPPED | Cru::FLG_ETHEREAL);
    It.X = X; It.Y = Y; It.Z = Z;
    if (!(It.ExtFlags & Cru::EXT_INCURMAP)) AddItemToMap(It, (It.Flags & (Cru::FLG_DISPOSABLE | Cru::FLG_FAST_ONLY)) != 0);
    Notify(It, ECruItemChange::Moved);
    if ((It.ExtFlags & Cru::EXT_TARGET) && GCruEngine) GCruEngine->OnTargetItemMoved(It);
    CallUsecodeEvent(It, Cru::EV_JUSTMOVED);
    const bool bDestFast = IsChunkFast(X / Cs, Y / Cs);
    if (!bDestFast && (It.Flags & Cru::FLG_FASTAREA))
    {
        if (It.Id != AvatarId) LeaveFastArea(It);
        return;
    }
    if (bDestFast && !(It.Flags & Cru::FLG_FASTAREA)) EnterFastArea(It);
}

void FCruWorld::SetShape(FCruItem& It, uint32 Shape)
{
    if (It.Shape == Shape) return;
    It.Shape = Shape;
    Notify(It, ECruItemChange::ShapeOrFrame);
}

void FCruWorld::SetFrame(FCruItem& It, uint32 Frame)
{
    if (It.Frame == Frame) return;
    It.Frame = Frame;
    Notify(It, ECruItemChange::ShapeOrFrame);
}

void FCruWorld::SetFlags(FCruItem& It, uint16 Flags)
{
    if (It.Flags == Flags) return;
    const uint16 Old = It.Flags; It.Flags = Flags;
    if ((Old ^ Flags) & (Cru::FLG_INVISIBLE | Cru::FLG_FLIPPED | Cru::FLG_BROKEN)) Notify(It, ECruItemChange::Flags);
}

void FCruWorld::MoveToEtherealVoid(FCruItem& It)
{
    if (It.Flags & Cru::FLG_ETHEREAL) return;
    Ethereal.Add(It.Id);
    if (It.Parent) CruInventory::RemoveFromContainer(It);   // Parent and FLG_CONTAINED stay set until it is moved / popped
    else if (It.ExtFlags & Cru::EXT_INCURMAP) RemoveItemFromMap(It);
    It.Flags |= Cru::FLG_ETHEREAL;
    Notify(It, ECruItemChange::Flags);
}

void FCruWorld::ReturnFromEtherealVoid(FCruItem& It)
{
    if (!(It.Flags & Cru::FLG_ETHEREAL)) return;
    Move(It, It.X, It.Y, It.Z);
}

void FCruWorld::DestroyItem(FCruItem& It)
{
    if (It.Id == Cru::kMainActorId) return;
    if (It.Contents.Num()) CruInventory::RemoveContents(It);   // Container::destroy: contents go to the parent container or the floor
    if (It.Flags & Cru::FLG_ETHEREAL) Ethereal.Remove(It.Id);
    else if (It.Parent) CruInventory::RemoveFromContainer(It);
    else if (It.ExtFlags & Cru::EXT_INCURMAP) RemoveItemFromMap(It);
    Eggs.Remove(It.Id);
    Notify(It, ECruItemChange::Destroyed);
    Items.Remove(It.Id);
    PendingDelete.Add(&It);
}

void FCruWorld::FlushDestroyed()
{
    for (FCruItem* P : PendingDelete) delete P;
    PendingDelete.Reset();
}

FIntVector FCruWorld::GetCentre(const FCruItem& It) const
{
    const FCruShapeInfo& Si = Info(It);
    FIntVector P(It.X, It.Y, It.Z);
    if (It.IsFlipped()) { P.X -= Si.Y * 16; P.Y -= Si.X * 16; } else { P.X -= Si.X * 16; P.Y -= Si.Y * 16; }
    P.Z += Si.Z * 4;
    return P;
}

bool FCruWorld::Overlaps(const FCruItem& A, const FCruItem& B) const { return GetWorldBox(A).Overlaps(GetWorldBox(B)); }
bool FCruWorld::OverlapsXY(const FCruItem& A, const FCruItem& B) const { return GetWorldBox(A).OverlapsXY(GetWorldBox(B)); }

bool FCruWorld::IsOn(const FCruItem& A, const FCruItem& B) const
{
    const FCruBox Ba = GetWorldBox(A), Bb = GetWorldBox(B);
    if (!Ba.OverlapsXY(Bb)) return false;
    return Bb.Z + Bb.Zd == Ba.Z;
}

bool FCruWorld::IsCompletelyOn(const FCruItem& A, const FCruItem& B) const
{
    if ((A.Flags & Cru::FLG_CONTAINED) || (B.Flags & Cru::FLG_CONTAINED)) return false;
    const FCruBox Ba = GetWorldBox(A), Bb = GetWorldBox(B);
    return (Ba.X <= Bb.X && Bb.X - Bb.Xd <= Ba.X - Ba.Xd) && (Ba.Y <= Bb.Y && Bb.Y - Bb.Yd <= Ba.Y - Ba.Yd) && (Bb.Z + Bb.Zd == Ba.Z);
}

bool FCruWorld::IsCentreOn(const FCruItem& A, const FCruItem& B) const
{
    const FIntVector C = GetCentre(A); const FCruBox Bb = GetWorldBox(B);
    if (C.X <= Bb.X - Bb.Xd || Bb.X <= C.X) return false;
    if (C.Y <= Bb.Y - Bb.Yd || Bb.Y <= C.Y) return false;
    return Bb.Z + Bb.Zd == A.Z;
}

static void ClipChunks(int32& MinX, int32& MaxX, int32& MinY, int32& MaxY)
{
    MinX = FMath::Clamp(MinX, 0, Cru::MAP_NUM_CHUNKS - 1); MaxX = FMath::Clamp(MaxX, 0, Cru::MAP_NUM_CHUNKS - 1);
    MinY = FMath::Clamp(MinY, 0, Cru::MAP_NUM_CHUNKS - 1); MaxY = FMath::Clamp(MaxY, 0, Cru::MAP_NUM_CHUNKS - 1);
}

FCruWorld::FPositionInfo FCruWorld::GetPositionInfo(int32 X, int32 Y, int32 Z, uint32 Shape, ObjId Ignore) const
{
    const FCruShapeInfo* Si = GetShapeInfo(Shape);
    if (!Si) return FPositionInfo();
    int32 Xd, Yd, Zd; Si->GetFootpadWorld(Xd, Yd, Zd, false);
    return GetPositionInfo(FCruBox(X, Y, Z, Xd, Yd, Zd), FCruBox(), Si->Flags, Ignore);
}

FCruWorld::FPositionInfo FCruWorld::GetPositionInfo(const FCruBox& Target, const FCruBox& Start, uint32 ShapeFlags, ObjId Ignore) const
{
    FPositionInfo R;
    const uint32 FlagMask = Cru::SI_SOLID | Cru::SI_DAMAGING | Cru::SI_LAND | Cru::SI_ROOF;
    const uint32 SupportMask = Cru::SI_SOLID | Cru::SI_LAND | Cru::SI_ROOF;
    const uint32 LandMask = Cru::SI_LAND | Cru::SI_ROOF;
    const uint32 BlockMask = Cru::SI_SOLID | Cru::SI_DAMAGING;
    int32 SupportZ = INT32_MIN, LandZ = INT32_MIN, RoofZ = INT32_MAX;
    const int32 MidX = Target.X - Target.Xd / 2, MidY = Target.Y - Target.Yd / 2;
    int32 MinX = (Target.X - Target.Xd) / Cru::MAP_CHUNK_SIZE - 1, MaxX = Target.X / Cru::MAP_CHUNK_SIZE + 1;
    int32 MinY = (Target.Y - Target.Yd) / Cru::MAP_CHUNK_SIZE - 1, MaxY = Target.Y / Cru::MAP_CHUNK_SIZE + 1;
    ClipChunks(MinX, MaxX, MinY, MaxY);
    for (int32 Cx = MinX; Cx <= MaxX; ++Cx)
        for (int32 Cy = MinY; Cy <= MaxY; ++Cy)
            for (ObjId Id : ChunkItems(Cx, Cy))
            {
                const FCruItem* It = GetItem(Id);
                if (!It || It->Id == Ignore || (It->ExtFlags & Cru::EXT_SPRITE)) continue;
                const FCruShapeInfo& Si = Info(*It);
                if (!(Si.Flags & FlagMask)) continue;
                const FCruBox Ib = GetWorldBox(*It);
                if ((Si.Flags & ShapeFlags & BlockMask) && Target.Overlaps(Ib) && !Start.Overlaps(Ib)) { if (!R.Blocker) R.Blocker = It; }
                if (Target.OverlapsXY(Ib))
                {
                    if ((Si.Flags & SupportMask) && Ib.Z + Ib.Zd > SupportZ && Ib.Z + Ib.Zd <= Target.Z) SupportZ = Ib.Z + Ib.Zd;
                    if (Si.IsRoof() && Ib.Z < RoofZ && Ib.Z >= Target.Z + Target.Zd) { R.Roof = It; RoofZ = Ib.Z; }
                }
                if (Ib.IsBelow(MidX, MidY, Target.Z) && (Si.Flags & LandMask) && Ib.Z + Ib.Zd > LandZ) { R.Land = It; LandZ = Ib.Z + Ib.Zd; }
            }
    R.bValid = R.Blocker == nullptr;
    if (SupportZ == Target.Z) R.bSupported = true;
    if (Target.Z <= 0) R.bSupported = true;
    return R;
}

bool FCruWorld::CheckLoopScript(const FCruItem& It, const uint8* Script, uint32 Size) const
{
    uint16 Stack[32]; int32 Sp = 0;
    auto Push = [&](uint16 V) { if (Sp < 32) Stack[Sp++] = V; };
    auto Pop = [&]() -> uint16 { return Sp > 0 ? Stack[--Sp] : 0; };
    Push(1);
    uint32 i = 0;
    while (i < Size)
    {
        const uint8 T = Script[i];
        uint16 A, B;
        switch (T)
        {
        case 0: Push(0); break;          // LS_TOKEN_FALSE
        case 1: Push(1); break;          // LS_TOKEN_TRUE
        case '$': return Pop() != 0;
        case '%': A = Script[i + 1] | (Script[i + 2] << 8); Push(A); i += 2; break;
        case '&': A = Pop(); B = Pop(); Push((A && B) ? 1 : 0); break;
        case '+': A = Pop(); B = Pop(); Push((A || B) ? 1 : 0); break;
        case '!': A = Pop(); Push(A ? 0 : 1); break;
        case '?': Push(It.Flags); break;
        case '*': Push(It.Quality); break;
        case '#': Push(It.NpcNum); break;
        case '=': A = Pop(); B = Pop(); Push(A == B ? 1 : 0); break;
        case '>': A = Pop(); B = Pop(); Push(B > A ? 1 : 0); break;
        case '<': A = Pop(); B = Pop(); Push(B < A ? 1 : 0); break;
        case ']': A = Pop(); B = Pop(); Push(B >= A ? 1 : 0); break;
        case '[': A = Pop(); B = Pop(); Push(B <= A ? 1 : 0); break;
        case ':': Push(Info(It).Family); break;
        case '@': Push((uint16)It.Shape); break;
        case '`': Push((uint16)It.Frame); break;
        default:
            if (T >= 'A' && T <= 'Z')
            {
                const int32 Count = T - '@'; bool bMatch = false;
                for (int32 j = 0; j < Count; ++j) { if (It.Shape == (uint32)(Script[i + 1] | (Script[i + 2] << 8))) bMatch = true; i += 2; }
                Push(bMatch ? 1 : 0);
            }
            else if (T >= 'a' && T <= 'z')
            {
                const int32 Count = T - '`'; bool bMatch = false;
                for (int32 j = 0; j < Count; ++j) { if (It.Frame == (uint32)(Script[i + 1] | (Script[i + 2] << 8))) bMatch = true; i += 2; }
                Push(bMatch ? 1 : 0);
            }
            else UE_LOG(LogCru, Warning, TEXT("loopscript: unknown token %02X"), T);
            break;
        }
        ++i;
    }
    return Pop() != 0;
}

void FCruWorld::AreaSearch(FUCList& Out, const uint8* Script, uint32 Size, uint16 Range, int32 X, int32 Y) const
{
    const FCruBox SearchRange(X + Range, Y + Range, 0, Range * 2 + 1, Range * 2 + 1, INT32_MAX);
    int32 MinX = (X - Range) / Cru::MAP_CHUNK_SIZE - 1, MaxX = (X + Range) / Cru::MAP_CHUNK_SIZE + 1;
    int32 MinY = (Y - Range) / Cru::MAP_CHUNK_SIZE - 1, MaxY = (Y + Range) / Cru::MAP_CHUNK_SIZE + 1;
    ClipChunks(MinX, MaxX, MinY, MaxY);
    for (int32 Cy = MinY; Cy <= MaxY; ++Cy)
        for (int32 Cx = MinX; Cx <= MaxX; ++Cx)
            for (ObjId Id : ChunkItems(Cx, Cy))
            {
                const FCruItem* It = GetItem(Id);
                if (!It || (It->ExtFlags & Cru::EXT_SPRITE)) continue;
                if (SearchRange.ContainsXY(It->X, It->Y) && CheckLoopScript(*It, Script, Size)) Out.AppendUInt16(It->Id);
            }
}

void FCruWorld::SurfaceSearch(FUCList& Out, const uint8* Script, uint32 Size, const FCruItem& Check, bool bAbove, bool bBelow, bool bRecurse) const
{
    const FCruBox SearchRange = GetWorldBox(Check);
    int32 MinX = (SearchRange.X - SearchRange.Xd) / Cru::MAP_CHUNK_SIZE - 1, MaxX = SearchRange.X / Cru::MAP_CHUNK_SIZE + 1;
    int32 MinY = (SearchRange.Y - SearchRange.Yd) / Cru::MAP_CHUNK_SIZE - 1, MaxY = SearchRange.Y / Cru::MAP_CHUNK_SIZE + 1;
    ClipChunks(MinX, MaxX, MinY, MaxY);
    for (int32 Cy = MinY; Cy <= MaxY; ++Cy)
        for (int32 Cx = MinX; Cx <= MaxX; ++Cx)
            for (ObjId Id : ChunkItems(Cx, Cy))
            {
                const FCruItem* It = GetItem(Id);
                if (!It || It->Id == Check.Id || (It->ExtFlags & Cru::EXT_SPRITE)) continue;
                const FCruBox Ib = GetWorldBox(*It);
                if (!SearchRange.OverlapsXY(Ib)) continue;
                bool bOk = false;
                if (bAbove && Ib.Z == SearchRange.Z + SearchRange.Zd) bOk = true;
                if (bBelow && SearchRange.Z == Ib.Z + Ib.Zd) bOk = true;
                if (bOk && CheckLoopScript(*It, Script, Size)) Out.AppendUInt16(It->Id);
            }
}

uint32 FCruWorld::UsecodeClassFor(const FCruItem& It) const
{
    uint32 ClassId = It.Shape;
    if (It.Id < 256 && (It.ExtFlags & Cru::EXT_PERMANENT_NPC)) ClassId = It.Id + 2048;
    if (Info(It).Family == Cru::SF_UNKEGG) ClassId = It.Quality + 0x900;
    return ClassId;
}

uint32 FCruWorld::CallUsecodeEvent(FCruItem& It, uint32 Event, const uint8* Args, int32 ArgSize)
{
    if (!GCruMachine || !GCruKernel) return 0;
    const uint32 ClassId = UsecodeClassFor(It);
    const uint32 Offset = GCruMachine->Usecode->GetClassEvent(ClassId, Event);
    if (!Offset) return 0;
    FUCProcess* P = new FUCProcess((uint16)ClassId, (uint16)Offset, FUCMachine::ObjectToPtr(It.Id), 2, Args, ArgSize);
    return GCruKernel->AddProcessExec(P);
}

uint32 FCruWorld::EnterFastArea(FCruItem& It)
{
    uint32 Ret = 0;
    if (!(It.Flags & Cru::FLG_FASTAREA))
    {
        // NOTE: the original only calls usecode for actors / noisy shapes; ScummVM calls it for all items and so do we.
        Ret = CallUsecodeEvent(It, Cru::EV_ENTERFASTAREA);
    }
    It.Flags |= Cru::FLG_FASTAREA;
    if (It.Shape == 0x34D) return 0;
    return Ret;
}

void FCruWorld::LeaveFastArea(FCruItem& It)
{
    if ((!(It.Flags & Cru::FLG_FAST_ONLY) || Info(It).IsNoisy()) && (It.Flags & Cru::FLG_FASTAREA)) CallUsecodeEvent(It, Cru::EV_LEAVEFASTAREA);
    It.Flags &= ~Cru::FLG_FASTAREA;
    It.bHatched = false;
    if ((It.Flags & Cru::FLG_FAST_ONLY) && !It.Parent) { if (It.Contents.Num()) CruInventory::DestroyContents(It); DestroyItem(It); }
}

void FCruWorld::SetChunkFast(int32 Cx, int32 Cy)
{
    Fast[Cy * Cru::MAP_NUM_CHUNKS + Cx] = true;
    TArray<ObjId> Ids = ChunkItems(Cx, Cy);
    for (ObjId Id : Ids) if (FCruItem* It = GetItem(Id)) EnterFastArea(*It);
}

void FCruWorld::UnsetChunkFast(int32 Cx, int32 Cy)
{
    Fast[Cy * Cru::MAP_NUM_CHUNKS + Cx] = false;
    TArray<ObjId> Ids = ChunkItems(Cx, Cy);
    for (ObjId Id : Ids) if (FCruItem* It = GetItem(Id)) LeaveFastArea(*It);
}

void FCruWorld::SetFastAtPoint(int32 X, int32 Y)
{
    const int32 Cx = X / Cru::MAP_CHUNK_SIZE, Cy = Y / Cru::MAP_CHUNK_SIZE;
    if (Cx < 0 || Cy < 0 || Cx >= Cru::MAP_NUM_CHUNKS || Cy >= Cru::MAP_NUM_CHUNKS) return;
    if (!IsChunkFast(Cx, Cy)) SetChunkFast(Cx, Cy);
}

static bool ChunkOnScreen(int32 Cx, int32 Cy, int32 SLeft, int32 STop, int32 SRight, int32 SBot, int32 Cs)
{
    const int32 Scx = (Cx * Cs - Cy * Cs) / 4, Scy = (Cx * Cs + Cy * Cs) / 8;
    const int32 CxLeft = Scx - Cs / 4, CxRight = Scx + Cs / 4, CyTop = Scy - Cs / 2, CyBot = Scy + Cs / 4;
    const bool bClear = CxRight <= SLeft || CxLeft >= SRight || CyTop >= SBot || CyBot <= STop;
    return !bClear;
}

void FCruWorld::UpdateFastArea(const FIntVector& From, const FIntVector& To)
{
    const int32 Cs = Cru::MAP_CHUNK_SIZE;
    int32 XMin = FMath::Min(From.X, To.X), XMax = FMath::Max(From.X, To.X);
    int32 YMin = FMath::Min(From.Y, To.Y), YMax = FMath::Max(From.Y, To.Y);
    const int32 ZMin = FMath::Min(From.Z, To.Z), ZMax = FMath::Max(From.Z, To.Z);
    const int32 SLeft = (XMin - YMin) / 4 - (ScreenW / 2 + Cs / 4);
    const int32 STop = ((XMin + YMin) / 8 - ZMax) - (ScreenH / 2 + Cs / 8);
    const int32 SRight = (XMax - YMax) / 4 + (ScreenW / 2 + Cs / 4);
    const int32 SBot = ((XMax + YMax) / 8 - ZMin) + (ScreenH / 2 + Cs / 8);
    if (FastXMin == SLeft && FastYMin == STop && FastXMax == SRight && FastYMax == SBot) return;
    FastXMin = SLeft; FastYMin = STop; FastXMax = SRight; FastYMax = SBot;
    const int32 SxLimit = ScreenW / (Cs / 2) + 3, SyLimit = ScreenH / (Cs / 4) + 7, XyLimit = (SyLimit + SxLimit) / 2;
    XMin = XMin / Cs - XyLimit; XMax = XMax / Cs + XyLimit; YMin = YMin / Cs - XyLimit; YMax = YMax / Cs + XyLimit;
    for (int32 Cy = 0; Cy < Cru::MAP_NUM_CHUNKS; ++Cy)
        for (int32 Cx = 0; Cx < Cru::MAP_NUM_CHUNKS; ++Cx)
        {
            bool bWant = Cx >= XMin && Cx <= XMax && Cy >= YMin && Cy <= YMax;
            if (bWant) bWant = ChunkOnScreen(Cx, Cy, SLeft, STop, SRight, SBot, Cs);
            const bool bIs = IsChunkFast(Cx, Cy);
            if (bWant == bIs) continue;
            if (!bWant) UnsetChunkFast(Cx, Cy); else SetChunkFast(Cx, Cy);
        }
}

uint32 FCruWorld::HatchEgg(FCruItem& Egg)
{
    if (Egg.bHatched) return 0;
    Egg.bHatched = true;
    UE_LOG(LogCru, Display, TEXT("egg %d hatched (shape %d q %d id %d at %d,%d)"), Egg.Id, Egg.Shape, Egg.Quality, Egg.MapNum, Egg.X, Egg.Y);
    if (Info(Egg).Family == Cru::SF_MONSTEREGG)
    {
        UE_LOG(LogCru, Display, TEXT("monster egg %d hatched (actors not implemented yet)"), Egg.Id);
        return 0;
    }
    if (Info(Egg).Family == Cru::SF_TELEPORTEGG)
    {
        // TeleportEgg::hatch: sources (frame != 1) send the avatar to the destination egg with the same id
        if (Egg.Frame != 1 && GCruEngine) GCruEngine->TeleportToEgg(Egg.MapNum, Egg.Quality & 0xff);
        return 0;
    }
    return CallUsecodeEvent(Egg, Cru::EV_HATCH);
}

uint32 FCruWorld::UnhatchEgg(FCruItem& Egg)
{
    if (!Egg.bHatched) return 0;
    Egg.bHatched = false;
    return CallUsecodeEvent(Egg, Cru::EV_UNHATCH);
}

void FCruWorld::RunEggHatcher()
{
    FCruItem* Av = GetItem(AvatarId);
    if (!Av) return;
    int32 Axs, Ays, Azs; GetFootpadWorld(*Av, Axs, Ays, Azs);
    TArray<ObjId> Copy = Eggs;
    bool bNearTeleporter = false;
    for (ObjId Id : Copy)
    {
        FCruItem* Egg = GetItem(Id);
        if (!Egg) continue;
        if (!(Egg->Flags & Cru::FLG_FASTAREA)) continue;   // the hatcher only tracks eggs in the fast area
        const int32 X1 = Egg->X - 64 * Egg->EggXRange(), X2 = Egg->X + 64 * Egg->EggXRange();
        const int32 Y1 = Egg->Y - 64 * Egg->EggYRange(), Y2 = Egg->Y + 64 * Egg->EggYRange();
        const bool bTeleporter = Info(*Egg).Family == Cru::SF_TELEPORTEGG;
        if (X1 <= Av->X && Av->X - Axs < X2 && Y1 <= Av->Y && Av->Y - Ays < Y2 && Egg->Z - 48 < Av->Z && Av->Z <= Egg->Z + 48)
        {
            if (bTeleporter && Egg->Frame != 1) bNearTeleporter = true;
            if (bTeleporter && bJustTeleported) continue;
            HatchEgg(*Egg);
        }
        else
            UnhatchEgg(*Egg);
    }
    if (!bNearTeleporter) bJustTeleported = false;
}

// ---------------------------------------------------------------- sweep test (CurrentMap::sweepTest)
bool FCruWorld::SweepTest(const FIntVector& Start, const FIntVector& End, const int32 Dims[3], uint32 ShapeFlags, ObjId Item, bool bBlockingOnly, TArray<FSweepItem>* Hit) const
{
    const uint32 BlockFlagMask = Cru::SI_SOLID | Cru::SI_DAMAGING | Cru::SI_LAND;
    const int32 Cs = Cru::MAP_CHUNK_SIZE;
    int32 MinX = ((Start.X - Dims[0]) / Cs) - 1, MaxX = (Start.X / Cs) + 1;
    int32 MinY = ((Start.Y - Dims[1]) / Cs) - 1, MaxY = (Start.Y / Cs) + 1;
    MinX = FMath::Min(MinX, ((End.X - Dims[0]) / Cs) - 1); MaxX = FMath::Max(MaxX, (End.X / Cs) + 1);
    MinY = FMath::Min(MinY, ((End.Y - Dims[1]) / Cs) - 1); MaxY = FMath::Max(MaxY, (End.Y / Cs) + 1);
    ClipChunks(MinX, MaxX, MinY, MaxY);

    const int32 Vel[3] = { End.X - Start.X, End.Y - Start.Y, End.Z - Start.Z };
    const int32 Ext[3] = { Dims[0] / 2, Dims[1] / 2, Dims[2] / 2 };
    const int32 Centre[3] = { Start.X - Ext[0], Start.Y - Ext[1], Start.Z + Ext[2] };   // z is the opposite direction to x and y

    bool bAny = false;
    for (int32 Cx = MinX; Cx <= MaxX; ++Cx)
        for (int32 Cy = MinY; Cy <= MaxY; ++Cy)
            for (ObjId Id : ChunkItems(Cx, Cy))
            {
                const FCruItem* O = GetItem(Id);
                if (!O || O->Id == Item || (O->ExtFlags & Cru::EXT_SPRITE)) continue;
                const uint32 OtherFlags = Info(*O).Flags;
                const bool bBlocking = (OtherFlags & ShapeFlags & BlockFlagMask) != 0;
                if (bBlockingOnly && !bBlocking) continue;

                int32 Other[3] = { O->X, O->Y, O->Z }, OExt[3];
                GetFootpadWorld(*O, OExt[0], OExt[1], OExt[2]);
                // overlapping (not merely touching) at the start: ignore
                if (!(Start.X <= Other[0] - (OExt[0] - 1) || Start.X - Dims[0] >= Other[0] - 1 ||
                      Start.Y <= Other[1] - (OExt[1] - 1) || Start.Y - Dims[1] >= Other[1] - 1 ||
                      Start.Z + Dims[2] <= Other[2] + 1 || Start.Z >= Other[2] + (OExt[2] - 1)))
                    continue;
                OExt[0] /= 2; OExt[1] /= 2; OExt[2] /= 2;
                Other[0] -= OExt[0] + Centre[0]; Other[1] -= OExt[1] + Centre[1]; Other[2] += OExt[2] - Centre[2];

                int32 U1[3] = { 0, 0, 0 }, U0[3] = { 0x4000, 0x4000, 0x4000 };
                bool bTouch = false, bTouchFloor = false;
                for (int32 i = 0; i < 3; ++i)
                {
                    const int32 AMax = Ext[i], AMin = -Ext[i], BMax = Other[i] + OExt[i], BMin = Other[i] - OExt[i];
                    if (Vel[i] < 0 && AMax >= BMin)
                    {
                        if (AMax == BMin && !(i == 2 && Ext[i] == 0 && OExt[i] == 0 && OExt[0] == 64 && OExt[1] == 64)) bTouch = true;
                        if (AMin + Vel[i] == BMax) { bTouch = true; if (i == 2) bTouchFloor = true; }
                        U0[i] = ((BMax - AMin) * 0x4000) / Vel[i];
                        U1[i] = ((BMin - AMax) * 0x4000) / Vel[i];
                    }
                    else if (Vel[i] > 0 && AMin <= BMax)
                    {
                        if (AMin == BMax) bTouch = true;
                        if (AMax + Vel[i] == BMin) bTouch = true;
                        U0[i] = ((BMin - AMax) * 0x4000) / Vel[i];
                        U1[i] = ((BMax - AMin) * 0x4000) / Vel[i];
                    }
                    else if (Vel[i] == 0 && AMax >= BMin && AMin <= BMax)
                    {
                        if (AMin == BMax || AMax == BMin) bTouch = true;
                        if (i == 2 && AMin == BMax) bTouchFloor = true;
                        U0[i] = -1; U1[i] = 0x4000;
                    }
                    else { U0[i] = 0x4001; U1[i] = -1; }
                    if (U1[i] >= U0[i] && (U0[i] > 0x4000 || U1[i] < 0)) { U0[i] = 0x4001; U1[i] = -1; }
                }
                int32 First = FMath::Max3(U0[0], U0[1], U0[2]);
                int32 Last = FMath::Min3(U1[0], U1[1], U1[2]);
                uint8 Dirs = 0;
                for (int32 i = 0; i < 3; ++i) if (First == U0[i]) Dirs |= (1 << i);
                if (First <= Last)
                {
                    if (!Hit) return true;
                    bAny = true;
                    First = FMath::Max(First, -1); Last = FMath::Min(Last, 0x4000);
                    int32 Pos = 0;
                    for (; Pos < Hit->Num(); ++Pos)
                        if ((*Hit)[Pos].HitTime > First || ((*Hit)[Pos].HitTime == First && (*Hit)[Pos].EndTime > Last)) break;
                    FSweepItem S; S.Item = O->Id; S.HitTime = First; S.EndTime = Last; S.bTouching = bTouch; S.bTouchingFloor = bTouchFloor; S.bBlocking = bBlocking; S.Dirs = Dirs;
                    Hit->Insert(S, Pos);
                }
            }
    return bAny;
}

// ---------------------------------------------------------------- playing sounds (AudioProcess::isSFXPlayingForObject)
void FCruWorld::NoteSfxStarted(ObjId Item, int32 Sfx, uint32 DurationTicks)
{
    const uint32 Now = GCruKernel ? GCruKernel->GetTickNum() : 0;
    PlayingSfx.Add({ Item, Sfx, Now + FMath::Max<uint32>(DurationTicks, 1) });
}
void FCruWorld::NoteSfxStopped(ObjId Item, int32 Sfx)
{
    PlayingSfx.RemoveAll([&](const FPlayingSfx& P) { return P.Item == Item && (Sfx < 0 || P.Sfx == Sfx); });
}
bool FCruWorld::IsSfxPlaying(ObjId Item, int32 Sfx) const
{
    const uint32 Now = GCruKernel ? GCruKernel->GetTickNum() : 0;
    PlayingSfx.RemoveAll([&](const FPlayingSfx& P) { return P.EndTick <= Now; });
    for (const FPlayingSfx& P : PlayingSfx) if (P.Item == Item && (Sfx < 0 || P.Sfx == Sfx)) return true;
    return false;
}

// loop opcode search types 4/5: items inside a container (CurrentMap::containerSearch equivalent)
void FCruWorld::ContainerSearch(FUCList& Out, const uint8* Script, uint32 Size, ObjId Container, bool bRecurse) const
{
    const FCruItem* C = GetItem(Container);
    if (!C) return;
    for (ObjId Id : C->Contents)
    {
        const FCruItem* It = GetItem(Id);
        if (!It) continue;
        if (CheckLoopScript(*It, Script, Size)) Out.AppendUInt16(Id);
        if (bRecurse && CruInventory::IsContainer(*It)) ContainerSearch(Out, Script, Size, Id, true);
    }
}

// ---------------------------------------------------------------- scan for a valid position (CurrentMap::scanForValidPosition)
bool FCruWorld::ScanForValidPosition(int32 X, int32 Y, int32 Z, const FCruItem& Item, int32 MoveDir, bool bWantSupport, int32& Tx, int32& Ty, int32& Tz) const
{
    const uint32 BlockFlagMask = (Cru::SI_SOLID | Cru::SI_DAMAGING) & Info(Item).Flags;
    const int32 SearchDir = (MoveDir + 4) % 8;
    const bool bXDir = CruDirXFactor(SearchDir) != 0, bYDir = CruDirYFactor(SearchDir) != 0;
    const int32 SearchType = SearchDir / 2;
    constexpr int32 ScanSize = 10;
    uint32 ValidMask[24], SupportMask[24];
    for (int32 i = 0; i < ScanSize * 2 + 1; ++i) { ValidMask[i] = 0xFFFFFFFF; SupportMask[i] = 0; }
    int32 Xd, Yd, Zd; GetFootpadWorld(Item, Xd, Yd, Zd);
    const int32 Cs = Cru::MAP_CHUNK_SIZE;
    int32 MinX = ((X - Xd) / Cs) - 1, MaxX = (X / Cs) + 1, MinY = ((Y - Yd) / Cs) - 1, MaxY = (Y / Cs) + 1;
    ClipChunks(MinX, MaxX, MinY, MaxY);
    for (int32 Cx = MinX; Cx <= MaxX; ++Cx)
        for (int32 Cy = MinY; Cy <= MaxY; ++Cy)
            for (ObjId Id : ChunkItems(Cx, Cy))
            {
                const FCruItem* C = GetItem(Id);
                if (!C || C->Id == Item.Id || (C->ExtFlags & Cru::EXT_SPRITE)) continue;
                const FCruShapeInfo& Si = Info(*C);
                if (!(Si.Flags & BlockFlagMask)) continue;
                int32 Ixd, Iyd, Izd; GetFootpadWorld(*C, Ixd, Iyd, Izd);
                int32 MinV = C->Z - Z - Zd + 1, MaxV = C->Z + Izd - Z - 1;
                if (MinV < -ScanSize) MinV = -ScanSize;
                if (MaxV > ScanSize) MaxV = ScanSize;
                const int32 SMinX = C->X - Ixd + 1 - X, SMaxX = C->X + Xd - 1 - X, SMinY = C->Y - Iyd + 1 - Y, SMaxY = C->Y + Yd - 1 - Y;
                int32 MinH = -100, MaxH = 100;
                if (!bXDir && (SMinX > 0 || SMaxX < 0)) continue;
                if (!bYDir && (SMinY > 0 || SMaxY < 0)) continue;
                if (bXDir && MinH < SMinX) MinH = SMinX;
                if (bXDir && MaxH > SMaxX) MaxH = SMaxX;
                if ((bYDir && SearchType != 1) && MinH < SMinY) MinH = SMinY;
                if ((bYDir && SearchType != 1) && MaxH > SMaxY) MaxH = SMaxY;
                if (SearchType == 1 && MinH < -SMaxY) MinH = -SMaxY;
                if (SearchType == 1 && MaxH > -SMinY) MaxH = -SMinY;
                if (MinH < -ScanSize) MinH = -ScanSize;
                if (MaxH > ScanSize) MaxH = ScanSize;
                for (int32 j = MinV; j <= MaxV; ++j)
                    for (int32 i = MinH; i <= MaxH; ++i)
                        ValidMask[j + ScanSize] &= ~(1u << (i + ScanSize));
                if (bWantSupport && Si.IsSolid() && C->Z + Izd >= Z - ScanSize && C->Z + Izd <= Z + ScanSize)
                    for (int32 i = MinH; i <= MaxH; ++i)
                        SupportMask[C->Z + Izd - Z + ScanSize] |= (1u << (i + ScanSize));
            }
    bool bFoundUnsupported = false;
    static const int32 Offsets[] = { 0, -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -7, 7, -8, 8, -9, 9 };
    for (int32 i = 0; i < 11; ++i)
    {
        const int32 Horiz = Offsets[i];
        for (int32 j = 0; j < (int32)UE_ARRAY_COUNT(Offsets); ++j)
        {
            const int32 Vert = Offsets[j];
            const uint32 CheckBit = 1u << (Horiz + ScanSize);
            if (ValidMask[Vert + ScanSize] & CheckBit)
            {
                if (!bWantSupport || !bFoundUnsupported || (SupportMask[Vert + ScanSize] & CheckBit))
                {
                    Tz = Z + Vert; Tx = X; if (SearchType != 0) Tx += Horiz;
                    Ty = Y; if (SearchType == 1) Ty -= Horiz; else if (SearchType != 2) Ty += Horiz;
                }
                if (!bWantSupport || (SupportMask[Vert + ScanSize] & CheckBit)) return true;
                bFoundUnsupported = true;
            }
        }
    }
    return bFoundUnsupported;
}
