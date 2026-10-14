// Crusader: No Remorse intrinsics. Ported/adapted from ScummVM; unimplemented ones are nullptr in the table and
// logged once when called.
#include "CruIntrinsics.h"
#include "CruWorld.h"
#include "UCMachine.h"
#include "UCProcess.h"
#include "Kernel.h"
#include "CruEngine.h"
#include "CruActors.h"
#include "CruCombat.h"
#include "CruInventory.h"

#define ARG_UINT8(x)   uint8 x = (*Args++); Args++;
#define ARG_UINT16(x)  uint16 x = (*Args++); x += ((*Args++) << 8);
#define ARG_UINT32(x)  uint32 x = (*Args++); x += ((*Args++) << 8); x += ((*Args++) << 16); x += ((*Args++) << 24);
#define ARG_SINT16(x)  int16 x = (*Args++); x += ((*Args++) << 8);
#define ARG_UC_PTR(x)  uint32 x = (*Args++); x += ((*Args++) << 8); x += ((*Args++) << 16); x += ((*Args++) << 24);
#define ARG_OBJID(x)   ObjId x = (*Args++); x += ((*Args++) << 8);
#define ARG_ITEM_FROM_PTR(x) ARG_UC_PTR(ucptr_##x); ObjId id_##x = FUCMachine::PtrToObject(ucptr_##x); FCruItem* x = GCruWorld->GetItem(id_##x);
#define ARG_ITEM_FROM_ID(x)  ARG_OBJID(id_##x); FCruItem* x = GCruWorld->GetItem(id_##x);
#define ARG_NULL16() Args += 2;
#define ARG_NULL32() Args += 4;
#define ARG_WORLDPOINT(x) ARG_UC_PTR(ucptr_##x); uint8 x[5] = {0,0,0,0,0}; GCruMachine->DereferencePointer(ucptr_##x, x, 5);
#define WP_X(p) (int32)(int16)((p)[0] | ((p)[1] << 8))
#define WP_Y(p) (int32)(int16)((p)[2] | ((p)[3] << 8))
#define WP_Z(p) (int32)(p)[4]
#define FromUC(v) ((v) * 2)
#define ToUC(v) ((v) / 2)
#define INTRINSIC(name) static uint32 name(const uint8* Args, uint32 ArgSize)

static void WritePtr16(uint32 Ptr, uint16 V) { uint8 B[2] = { (uint8)V, (uint8)(V >> 8) }; GCruMachine->AssignPointer(Ptr, B, 2); }

// ---- directions (ScummVM misc/direction_util.h) ----
static int32 DirGetWorldDir(int32 Dy, int32 Dx, bool b16)
{
    if (Dx == 0) { if (Dy == 0) return 2; return Dy > 0 ? 8 : 0; }
    if (!b16)
    {
        const int32 Dydx = (1024 * Dy) / Dx;
        if (Dydx >= 0) { if (Dx > 0) return Dydx <= 424 ? 4 : Dydx <= 2472 ? 6 : 8; else return Dydx <= 424 ? 12 : Dydx <= 2472 ? 14 : 0; }
        else if (Dx > 0) return Dydx >= -424 ? 4 : Dydx >= -2472 ? 2 : 0;
        else return Dydx >= -424 ? 12 : Dydx >= -2472 ? 10 : 8;
    }
    const double A = FMath::RadiansToDegrees(FMath::Atan2((double)Dy, (double)Dx));
    static const double Edges[] = { -168.75, -146.25, -123.75, -101.25, -78.75, -56.25, -33.75, -11.25, 11.25, 33.75, 56.25, 78.75, 101.25, 123.75, 146.25, 168.75 };
    static const int32 Dirs[] = { 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    for (int32 i = 0; i < 16; ++i) if (A < Edges[i]) return Dirs[i];
    return 12;
}
static int32 DirInvert(int32 D) { return (D + 8) % 16; }

// ---- Item ----
INTRINSIC(Item_I_getFrame) { ARG_ITEM_FROM_PTR(It); return It ? It->Frame : 0; }
INTRINSIC(Item_I_setFrame) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(F); if (It) GCruWorld->SetFrame(*It, F); return 0; }
INTRINSIC(Item_I_getShape) { ARG_ITEM_FROM_PTR(It); return It ? It->Shape : 0; }
INTRINSIC(Item_I_setShape) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Sh); if (It) GCruWorld->SetShape(*It, Sh); return 0; }
INTRINSIC(Item_I_getMapArray) { ARG_ITEM_FROM_PTR(It); return It ? It->MapNum : 0; }
INTRINSIC(Item_I_setMapArray) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(M); if (It) It->MapNum = M; return 0; }
INTRINSIC(Item_I_getNpcNum) { ARG_ITEM_FROM_PTR(It); return It ? It->NpcNum : 0; }
INTRINSIC(Item_I_setNpcNum) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(N); if (It) It->NpcNum = N; return 0; }
INTRINSIC(Item_I_getStatus) { ARG_ITEM_FROM_PTR(It); return It ? It->Flags : 0; }
INTRINSIC(Item_I_orStatus) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(M); if (It) GCruWorld->SetFlags(*It, It->Flags | M); return 0; }
INTRINSIC(Item_I_andStatus) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(M); if (It) GCruWorld->SetFlags(*It, It->Flags & M); return 0; }
INTRINSIC(Item_I_getQ) { ARG_ITEM_FROM_PTR(It); return It ? It->Quality : 0; }
INTRINSIC(Item_I_setQ) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Q); if (It) It->Quality = Q; return 0; }
INTRINSIC(Item_I_getQLo) { ARG_ITEM_FROM_PTR(It); return It ? (It->Quality & 0xFF) : 0; }
INTRINSIC(Item_I_getQHi) { ARG_ITEM_FROM_PTR(It); return It ? ((It->Quality >> 8) & 0xFF) : 0; }
INTRINSIC(Item_I_setQLo) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Q); if (It) It->Quality = (It->Quality & 0xFF00) | (Q & 0xFF); return 0; }
INTRINSIC(Item_I_setQHi) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Q); if (It) It->Quality = (It->Quality & 0x00FF) | ((Q << 8) & 0xFF00); return 0; }
INTRINSIC(Item_I_getQuality) { ARG_ITEM_FROM_PTR(It); return (It && GCruWorld->Info(*It).Family == Cru::SF_QUALITY) ? It->Quality : 0; }
INTRINSIC(Item_I_setQuality) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Q); if (It && GCruWorld->Info(*It).Family != Cru::SF_GENERIC) It->Quality = Q; return 0; }
INTRINSIC(Item_I_setQAndCombine) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Q); if (It) { It->Quality = Q; GCruWorld->CallUsecodeEvent(*It, Cru::EV_COMBINE); } return 0; }
INTRINSIC(Item_I_getUnkEggType) { ARG_ITEM_FROM_PTR(It); return (It && GCruWorld->Info(*It).Family == Cru::SF_UNKEGG) ? (It->Quality & 0xFF) : 0; }
INTRINSIC(Item_I_getFamily) { ARG_ITEM_FROM_PTR(It); return It ? GCruWorld->Info(*It).Family : 0; }
INTRINSIC(Item_I_getFamilyOfType) { ARG_UINT16(Sh); const FCruShapeInfo* Si = GCruWorld->GetShapeInfo(Sh); return Si ? Si->Family : 0; }
INTRINSIC(Item_I_getTypeFlag) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Bit); if (!It) return 0; const uint32 F = GCruWorld->Info(*It).Flags; return (Bit < 20 && (F & (1u << Bit))) ? 1 : 0; }
INTRINSIC(Item_I_getX) { ARG_ITEM_FROM_PTR(It); return It ? (uint16)ToUC(It->X) : 0; }
INTRINSIC(Item_I_getY) { ARG_ITEM_FROM_PTR(It); return It ? (uint16)ToUC(It->Y) : 0; }
INTRINSIC(Item_I_getZ) { ARG_ITEM_FROM_PTR(It); return It ? (uint16)It->Z : 0; }
INTRINSIC(Item_I_getCX) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; const FCruShapeInfo& Si = GCruWorld->Info(*It); return (uint16)ToUC(It->X - (It->IsFlipped() ? Si.Y : Si.X) * 16); }
INTRINSIC(Item_I_getCY) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; const FCruShapeInfo& Si = GCruWorld->Info(*It); return (uint16)ToUC(It->Y - (It->IsFlipped() ? Si.X : Si.Y) * 16); }
INTRINSIC(Item_I_getCZ) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; return (uint16)(It->Z + GCruWorld->Info(*It).Z * 4); }
INTRINSIC(Item_I_getPoint)
{
    ARG_ITEM_FROM_PTR(It); ARG_UC_PTR(Ptr); if (!It) return 0;
    const int32 X = ToUC(It->X), Y = ToUC(It->Y);
    uint8 B[5] = { (uint8)X, (uint8)(X >> 8), (uint8)Y, (uint8)(Y >> 8), (uint8)It->Z };
    GCruMachine->AssignPointer(Ptr, B, 5); return 0;
}
INTRINSIC(Item_I_getFootpadData)
{
    ARG_ITEM_FROM_PTR(It); ARG_UC_PTR(Xp); ARG_UC_PTR(Yp); ARG_UC_PTR(Zp); if (!It) return 0;
    const FCruShapeInfo& Si = GCruWorld->Info(*It);
    WritePtr16(Xp, It->IsFlipped() ? Si.Y : Si.X); WritePtr16(Yp, It->IsFlipped() ? Si.X : Si.Y); WritePtr16(Zp, Si.Z); return 0;
}
INTRINSIC(Item_I_touch) { return 0; }
INTRINSIC(Item_I_destroy) { ARG_ITEM_FROM_PTR(It); if (It && It->Id != Cru::kMainActorId) GCruWorld->DestroyItem(*It); return 0; }
INTRINSIC(Item_I_push) { ARG_ITEM_FROM_PTR(It); if (It) GCruWorld->MoveToEtherealVoid(*It); return 0; }
INTRINSIC(Item_I_getEtherealTop) { return GCruWorld->EtherealEmpty() ? 0 : GCruWorld->EtherealPeek(); }
INTRINSIC(Item_I_pop)
{
    if (GCruWorld->EtherealEmpty()) return 0;
    const ObjId Id = GCruWorld->EtherealPeek(); FCruItem* It = GCruWorld->GetItem(Id);
    if (!It) { GCruWorld->EtherealRemove(Id); return 0; }
    GCruWorld->ReturnFromEtherealVoid(*It); return Id;
}
INTRINSIC(Item_I_popToCoords)
{
    ARG_NULL32(); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z);
    if (GCruWorld->EtherealEmpty()) return 0;
    const ObjId Id = GCruWorld->EtherealPeek(); FCruItem* It = GCruWorld->GetItem(Id);
    if (!It) { GCruWorld->EtherealRemove(Id); return 0; }
    int32 Wz = Z; if (It->Shape == 1408 && Wz > 0) Wz -= 1;
    GCruWorld->Move(*It, FromUC((int32)X), FromUC((int32)Y), Wz); return Id;
}
INTRINSIC(Item_I_popToEnd)
{
    ARG_NULL32(); ARG_ITEM_FROM_ID(C);
    if (GCruWorld->EtherealEmpty()) return 0;
    const ObjId Id = GCruWorld->EtherealPeek(); FCruItem* It = GCruWorld->GetItem(Id);
    if (!It) { GCruWorld->EtherealRemove(Id); return 0; }
    if (C) GCruWorld->Move(*It, C->X, C->Y, C->Z); else if (It->Flags & Cru::FLG_ETHEREAL) GCruWorld->DestroyItem(*It);
    return Id;
}
INTRINSIC(Item_I_move) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z); if (It) GCruWorld->Move(*It, FromUC((int32)X), FromUC((int32)Y), Z); return 0; }
INTRINSIC(Item_I_create)
{
    ARG_UC_PTR(Ptr); ARG_UINT16(Sh); ARG_UINT16(Fr);
    FCruItem* It = GCruWorld->CreateItem(Sh, Fr, 0, 0, 0, 0, 0, true);
    if (!It) { UE_LOG(LogCru, Warning, TEXT("I_create failed (%u,%u)"), Sh, Fr); return 0; }
    GCruWorld->MoveToEtherealVoid(*It); GCruWorld->Notify(*It, ECruItemChange::Created);
    WritePtr16(Ptr, It->Id); return 1;
}
static uint32 LegalCreateAt(uint32 Ptr, uint16 Sh, uint16 Fr, int32 X, int32 Y, int32 Z)
{
    GCruWorld->SetFastAtPoint(X, Y);
    if (!GCruWorld->GetPositionInfo(X, Y, Z, Sh, 0).bValid) return 0;
    FCruItem* It = GCruWorld->CreateItem(Sh, Fr, 0, 0, 0, 0, 0, true);
    if (!It) return 0;
    GCruWorld->Move(*It, X, Y, Z); GCruWorld->Notify(*It, ECruItemChange::Created);
    WritePtr16(Ptr, It->Id); return 1;
}
INTRINSIC(Item_I_legalCreateAtCoords) { ARG_UC_PTR(Ptr); ARG_UINT16(Sh); ARG_UINT16(Fr); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z); return LegalCreateAt(Ptr, Sh, Fr, FromUC((int32)X), FromUC((int32)Y), Z); }
INTRINSIC(Item_I_legalCreateAtPoint) { ARG_UC_PTR(Ptr); ARG_UINT16(Sh); ARG_UINT16(Fr); ARG_WORLDPOINT(P); return LegalCreateAt(Ptr, Sh, Fr, FromUC(WP_X(P)), FromUC(WP_Y(P)), WP_Z(P)); }
INTRINSIC(Item_I_legalMoveToPoint)
{
    ARG_ITEM_FROM_PTR(It); ARG_WORLDPOINT(P); ARG_UINT16(bAbort); ARG_NULL16(); if (!It) return 0;
    const int32 X = FromUC(WP_X(P)), Y = FromUC(WP_Y(P)), Z = WP_Z(P);
    int32 Xd, Yd, Zd; GCruWorld->GetFootpadWorld(*It, Xd, Yd, Zd);
    const bool bValid = GCruWorld->GetPositionInfo(FCruBox(X, Y, Z, Xd, Yd, Zd), GCruWorld->GetWorldBox(*It), GCruWorld->Info(*It).Flags, It->Id).bValid;
    if (!bValid && bAbort) return 0;
    GCruWorld->Move(*It, X, Y, Z); return bValid ? 1 : 0;
}
INTRINSIC(Item_I_overlaps) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); return (A && B && GCruWorld->Overlaps(*A, *B)) ? 1 : 0; }
INTRINSIC(Item_I_isOn) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); return (A && B && GCruWorld->IsOn(*A, *B)) ? 1 : 0; }
INTRINSIC(Item_I_isCompletelyOn) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); return (A && B && GCruWorld->IsCompletelyOn(*A, *B)) ? 1 : 0; }
INTRINSIC(Item_I_isCentreOn) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); return (A && B && GCruWorld->IsCentreOn(*A, *B)) ? 1 : 0; }
INTRINSIC(Item_I_isInNpc) { return 0; }
INTRINSIC(Item_I_inFastArea) { ARG_ITEM_FROM_PTR(It); return (It && (It->Flags & Cru::FLG_FASTAREA)) ? 1 : 0; }
INTRINSIC(Item_I_enterFastArea) { ARG_ITEM_FROM_PTR(It); return It ? GCruWorld->EnterFastArea(*It) : 0; }
INTRINSIC(Item_I_isPartlyOnScreen) { ARG_ITEM_FROM_PTR(It); return (It && GCruEngine && GCruEngine->IsOnScreen(*It)) ? 1 : 0; }
INTRINSIC(Item_I_isCrusTypeNPC) { ARG_UINT16(Sh); if (Sh == 0x7FE) return 1; const FCruShapeInfo* Si = GCruWorld->GetShapeInfo(Sh); return (Si && Si->IsNpc()) ? 1 : 0; }
INTRINSIC(Item_I_setBroken) { ARG_ITEM_FROM_PTR(It); if (It) GCruWorld->SetFlags(*It, It->Flags | Cru::FLG_BROKEN); return 0; }
INTRINSIC(Item_I_use) { ARG_ITEM_FROM_PTR(It); return It ? GCruWorld->CallUsecodeEvent(*It, Cru::EV_USE) : 0; }
INTRINSIC(Item_I_equip) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; ARG_UINT16(V); uint8 B[2] = { (uint8)V, (uint8)(V >> 8) }; return GCruWorld->CallUsecodeEvent(*It, Cru::EV_EQUIP, B, 2); }
INTRINSIC(Item_I_unequip) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; ARG_UINT16(V); uint8 B[2] = { (uint8)V, (uint8)(V >> 8) }; return GCruWorld->CallUsecodeEvent(*It, Cru::EV_UNEQUIP, B, 2); }
INTRINSIC(Item_I_avatarStoleSomething) { ARG_ITEM_FROM_PTR(It); if (!It || It->bDead) return 0; ARG_UINT16(V); uint8 B[2] = { (uint8)V, (uint8)(V >> 8) }; return GCruWorld->CallUsecodeEvent(*It, Cru::EV_AVATARSTOLESOMETHING, B, 2); }
INTRINSIC(Item_I_cast) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; ARG_UINT16(V); uint8 B[2] = { (uint8)V, (uint8)(V >> 8) }; return GCruWorld->CallUsecodeEvent(*It, Cru::EV_CAST, B, 2); }
INTRINSIC(Item_I_getDirToItem) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); if (!A || !B) return 0; return DirGetWorldDir(B->Y - A->Y, B->X - A->X, false); }
INTRINSIC(Item_I_getDirFromItem) { ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); if (!A || !B) return 0; return DirInvert(DirGetWorldDir(B->Y - A->Y, B->X - A->X, false)); }
INTRINSIC(Item_I_getDirToCoords) { ARG_ITEM_FROM_PTR(A); ARG_UINT16(X); ARG_UINT16(Y); if (!A) return 0; return DirGetWorldDir(FromUC((int32)Y) - A->Y, FromUC((int32)X) - A->X, false); }
INTRINSIC(Item_I_getDirFromTo16) { ARG_UINT16(X1); ARG_UINT16(Y1); ARG_UINT16(X2); ARG_UINT16(Y2); if (X1 == X2 && Y1 == Y2) return 16; return DirGetWorldDir((int16)Y2 - (int16)Y1, (int16)X2 - (int16)X1, true); }
INTRINSIC(Item_I_getClosestDirectionInRange)
{
    ARG_UINT16(X1); ARG_UINT16(Y1); ARG_UINT16(X2); ARG_UINT16(Y2); ARG_UINT16(NDirs); ARG_UINT16(MinD); ARG_UINT16(MaxD);
    int32 D = DirGetWorldDir((int16)Y2 - (int16)Y1, (int16)X2 - (int16)X1, NDirs == 16);
    // clamp into [MinD, MaxD] going around the circle (ScummVM Direction_GetWorldDirInRange)
    if (MinD == MaxD) return MinD;
    auto InRange = [&](int32 V) { return MinD <= MaxD ? (V >= (int32)MinD && V <= (int32)MaxD) : (V >= (int32)MinD || V <= (int32)MaxD); };
    if (InRange(D)) return D;
    const int32 Step = NDirs == 16 ? 1 : 2;
    const int32 DistMin = FMath::Min((D - MinD + 16) % 16, (MinD - D + 16) % 16), DistMax = FMath::Min((D - MaxD + 16) % 16, (MaxD - D + 16) % 16);
    (void)Step; return DistMin <= DistMax ? MinD : MaxD;
}
INTRINSIC(Item_I_getRangeIfVisible)
{
    ARG_ITEM_FROM_PTR(A); ARG_ITEM_FROM_ID(B); if (!A || !B) return 0;
    int32 R = CruCombat::GetRangeIfVisible(*A, *B) / 32;
    if (R & 0xF) ++R;
    return R <= 48 ? R : 0;
}
INTRINSIC(Item_I_fireWeapon) { ARG_ITEM_FROM_PTR(It); ARG_SINT16(X); ARG_SINT16(Y); ARG_SINT16(Z); ARG_UINT16(Dir); ARG_UINT16(FireType); ARG_UINT16(Find); if (!It) return 0; return CruCombat::FireWeapon(*It, FromUC((int32)X), FromUC((int32)Y), Z, Dir & 15, FireType, Find != 0); }
INTRINSIC(Item_I_fireDistance) { ARG_ITEM_FROM_PTR(It); ARG_ITEM_FROM_ID(Other); ARG_SINT16(Dir); ARG_SINT16(Xo); ARG_SINT16(Yo); ARG_SINT16(Zo); if (!It || !Other) return 0; return CruCombat::FireDistance(*It, *Other, Dir & 15, FromUC((int32)Xo), FromUC((int32)Yo), Zo); }
INTRINSIC(Item_I_explode) { ARG_ITEM_FROM_PTR(It); if (!It) return 0; int32 ExpType = 0; bool bDestroy = true; if (ArgSize > 4) { ARG_UINT16(T); ARG_UINT16(D); ExpType = T; bDestroy = D != 0; } CruCombat::Explode(*It, ExpType, bDestroy, true); return 0; }
INTRINSIC(Item_I_receiveHit) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(Other); ARG_SINT16(Dir); ARG_SINT16(Dmg); ARG_UINT16(Type); if (It) CruCombat::ReceiveHit(*It, Other, Dir & 15, Dmg, Type); return 0; }
INTRINSIC(Item_I_hurl) { ARG_ITEM_FROM_PTR(It); ARG_SINT16(Xs); ARG_SINT16(Ys); ARG_SINT16(Zs); ARG_SINT16(G); if (It) CruCombat::Hurl(*It, Xs, Ys, Zs, G); return 0; }
INTRINSIC(Item_I_shoot) { return 0; }
INTRINSIC(Item_I_fall) { return 0; }
INTRINSIC(Item_I_popToContainer)
{
    // Item::I_popToContainer (item.cpp:3502): into a container (actor / SF_CONTAINER), else to the item's coordinates
    ARG_NULL32(); ARG_ITEM_FROM_ID(C);
    if (GCruWorld->EtherealEmpty()) return 0;
    const ObjId Id = GCruWorld->EtherealPeek(); FCruItem* It = GCruWorld->GetItem(Id);
    if (!It) { GCruWorld->EtherealRemove(Id); return 0; }
    if (C && CruInventory::IsContainer(*C)) CruInventory::MoveToContainer(*It, *C);
    else if (C) GCruWorld->Move(*It, C->X, C->Y, C->Z);
    else if (It->Flags & Cru::FLG_ETHEREAL) GCruWorld->DestroyItem(*It);   // orphaned ethereal object
    else GCruWorld->EtherealRemove(Id);
    return Id;
}
INTRINSIC(Item_I_getSurfaceWeight) { return 0; }

// ---- Egg ----
INTRINSIC(Egg_I_getEggId) { ARG_ITEM_FROM_PTR(It); return It ? It->MapNum : 0; }
INTRINSIC(Egg_I_setEggId) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(V); if (It) It->MapNum = V; return 0; }
INTRINSIC(Egg_I_getEggXRange) { ARG_ITEM_FROM_PTR(It); return It ? It->EggXRange() : 0; }
INTRINSIC(Egg_I_getEggYRange) { ARG_ITEM_FROM_PTR(It); return It ? It->EggYRange() : 0; }
INTRINSIC(Egg_I_setEggXRange) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(R); if (It) It->NpcNum = (It->NpcNum & 0x0F) | ((R & 0xF) << 4); return 0; }
INTRINSIC(Egg_I_setEggYRange) { ARG_ITEM_FROM_PTR(It); ARG_UINT16(R); if (It) It->NpcNum = (It->NpcNum & 0xF0) | (R & 0xF); return 0; }

// ---- Actor (minimal until actors exist) ----
static FCruItem* ActorFromPtr(const uint8*& Args) { ARG_UC_PTR(P); const ObjId Id = FUCMachine::PtrToObject(P); FCruItem* It = GCruWorld->GetItem(Id); return (It && It->Id < 256) ? It : nullptr; }
INTRINSIC(Actor_I_isNPC) { return ActorFromPtr(Args) ? 1 : 0; }
INTRINSIC(Actor_I_isDead) { FCruItem* A = ActorFromPtr(Args); return (!A || A->bDead) ? 1 : 0; }
INTRINSIC(Actor_I_isBusy) { FCruItem* A = ActorFromPtr(Args); return (A && CruActor::IsBusy(*A)) ? 1 : 0; }
INTRINSIC(Actor_I_isInCombat) { FCruItem* A = ActorFromPtr(Args); return (A && CruActor::IsInCombat(*A)) ? 1 : 0; }
INTRINSIC(Actor_I_isKneeling) { FCruItem* A = ActorFromPtr(Args); return (A && (A->ActorFlags & CruAnim::ACT_KNEELING)) ? 1 : 0; }
INTRINSIC(Actor_I_isFalling) { return 0; }
INTRINSIC(Actor_I_getDir) { FCruItem* A = ActorFromPtr(Args); return A ? A->Dir : 0; }
INTRINSIC(Actor_I_getMap) { FCruItem* A = ActorFromPtr(Args); return A ? A->MapNum : 0; }
INTRINSIC(Actor_I_getHp) { FCruItem* A = ActorFromPtr(Args); return A ? A->Hp : 0; }
INTRINSIC(Actor_I_setDead) { FCruItem* A = ActorFromPtr(Args); if (A) { A->bDead = true; A->ActorFlags |= CruAnim::ACT_DEAD; } return 0; }
INTRINSIC(Actor_I_setActivity) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(Act); if (!A) return 0; UE_LOG(LogCru, Display, TEXT("actor %d setActivity %d"), A->Id, Act); return CruActor::SetActivity(*A, Act); }
INTRINSIC(Actor_I_getLastAnimSet) { FCruItem* A = ActorFromPtr(Args); return (A && A->LastAnim >= 0) ? A->LastAnim : 0; }
INTRINSIC(Actor_I_getUnkByte) { FCruItem* A = ActorFromPtr(Args); return A ? A->UnkByte : 0; }
INTRINSIC(Actor_I_getCurrentActivityNo) { FCruItem* A = ActorFromPtr(Args); return A ? A->CurrentActivity : 0; }
INTRINSIC(Actor_I_getLastActivityNo) { FCruItem* A = ActorFromPtr(Args); return A ? A->LastActivity : 0; }
INTRINSIC(Actor_I_getMana) { FCruItem* A = ActorFromPtr(Args); return A ? (uint16)A->Mana : 0; }   // energy in Crusader
INTRINSIC(Actor_I_setMana) { FCruItem* A = ActorFromPtr(Args); ARG_SINT16(V); if (A) A->Mana = V; return 0; }
INTRINSIC(Actor_I_addHp) { FCruItem* A = ActorFromPtr(Args); ARG_SINT16(V); if (A) A->Hp += V; return 0; }
INTRINSIC(Actor_I_clrImmortal) { FCruItem* A = ActorFromPtr(Args); if (A) A->ActorFlags &= ~CruAnim::ACT_IMMORTAL; return 0; }
INTRINSIC(Actor_I_setImmortal) { FCruItem* A = ActorFromPtr(Args); if (A) A->ActorFlags |= CruAnim::ACT_IMMORTAL; return 0; }
INTRINSIC(Actor_I_clrInCombat) { FCruItem* A = ActorFromPtr(Args); if (A) CruCombat::ClearInCombat(*A); return 0; }

INTRINSIC(Actor_I_createActorCru)
{
    ARG_ITEM_FROM_PTR(It); ARG_ITEM_FROM_ID(Other);
    if (!It || !Other) return 0;
    const int32 NpcDifficulty = (It->MapNum & 3) + 1;
    if (GCruWorld->GameDifficulty < NpcDifficulty) return 0;
    const FCruNpcDat* Npc = GCruWorld->GetNpcData(Other->NpcNum);
    if (!Npc) return 0;
    const int32 Dir = It->NpcNum & 0xF;
    const int32 Frame = (Dir * 2 + 4) & 0xF;
    const uint32 Ext = (Npc->Shape == 0x597 || Npc->Shape == 0x3AC) ? Cru::EXT_FEMALE : 0;
    FCruItem* A = GCruWorld->CreateActor(Npc->Shape, Frame, Cru::FLG_IN_NPC_LIST | Cru::FLG_DISPOSABLE, Ext);
    if (!A) return 0;
    const FCruNpcDat* Stats = GCruWorld->GetNpcDataForShape(Npc->Shape);
    if (Stats) { A->Str = Stats->MaxHp / 2; A->Hp = Stats->MaxHp; for (int32 i = 0; i < 3; ++i) A->DefaultActivity[i] = Stats->DefaultActivity[i]; }
    A->Dir = Dir * 2;
    GCruWorld->Move(*A, It->X, It->Y, It->Z);
    GCruWorld->Notify(*A, ECruItemChange::Created);
    A->DefaultActivity[0] = Other->Quality >> 8; A->DefaultActivity[1] = It->Quality >> 8; A->DefaultActivity[2] = Other->MapNum;
    A->UnkByte = It->Quality & 0xFF;
    const bool bWpnFlag = (It->MapNum & 4) != 0;
    uint16 WpnType = Npc->WpnType, WpnType2 = Npc->WpnType2;
    if ((!WpnType || !bWpnFlag) && WpnType2) WpnType = WpnType2;
    static const uint16 WPNSHAPES[] = { 0, 0x032E, 0x032F, 0x0330, 0x038C, 0x0332, 0x0333, 0x0334, 0x038E, 0x0388, 0x038A, 0x038D, 0x038B, 0x0386 };
    if (WpnType && WpnType < UE_ARRAY_COUNT(WPNSHAPES))
    {
        FCruItem* W = GCruWorld->CreateItem(WPNSHAPES[WpnType], 0, 0, 0, 0, A->MapNum, 0, true);
        if (W) { CruInventory::MoveToContainer(*W, *A); A->ActiveWeapon = W->Id; }
    }
    A->CombatTactic = 0; A->HomeX = It->X; A->HomeY = It->Y; A->HomeZ = It->Z;
    UE_LOG(LogCru, Display, TEXT("actor %d created: %s shape %d at (%d,%d,%d) dir %d hp %d weapon %d"), A->Id, *Npc->Name, Npc->Shape, A->X, A->Y, A->Z, A->Dir, A->Hp, WpnType);
    return A->Id;
}
INTRINSIC(Actor_I_createActor) { return 0; }
INTRINSIC(Actor_I_setDefaultActivity0) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(V); if (A) A->DefaultActivity[0] = V; return 0; }
INTRINSIC(Actor_I_setDefaultActivity1) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(V); if (A) A->DefaultActivity[1] = V; return 0; }
INTRINSIC(Actor_I_setDefaultActivity2) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(V); if (A) A->DefaultActivity[2] = V; return 0; }
INTRINSIC(Actor_I_getDefaultActivity0) { FCruItem* A = ActorFromPtr(Args); return A ? A->DefaultActivity[0] : 0; }
INTRINSIC(Actor_I_getDefaultActivity1) { FCruItem* A = ActorFromPtr(Args); return A ? A->DefaultActivity[1] : 0; }
INTRINSIC(Actor_I_getDefaultActivity2) { FCruItem* A = ActorFromPtr(Args); return A ? A->DefaultActivity[2] : 0; }
INTRINSIC(Actor_I_setCombatTactic) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(V); if (A) A->CombatTactic = V; return 0; }
INTRINSIC(Actor_I_setUnkByte) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(V); if (A) A->UnkByte = (uint8)V; return 0; }
INTRINSIC(Actor_I_turnToward)
{
    FCruItem* A = ActorFromPtr(Args); ARG_UINT16(D); ARG_UINT16(Dir16); if (!A) return 0;
    const int32 NewDir = D & 15, Cur = A->Dir;
    if (Cur == NewDir || (!Dir16 && (NewDir == CruDirOneLeft(Cur, true) || NewDir == CruDirOneRight(Cur, true)))) return 0;
    return CruActor::TurnTowardDir(*A, NewDir);
}
INTRINSIC(Actor_I_doAnim) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(Anim); ARG_UINT16(D); ARG_NULL16(); ARG_NULL16(); if (!A) return 0; return CruActor::DoAnim(*A, Anim, D); }
INTRINSIC(Actor_I_setTarget) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(T); if (A) { if (FAttackProcess* Ap = CruCombat::GetAttackProcess(*A)) Ap->SetTarget(T); } return 0; }
INTRINSIC(Actor_I_teleport) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z); if (A) GCruWorld->Move(*A, FromUC((int32)X), FromUC((int32)Y), Z); return 0; }
// Actor::I_getEquip / I_setEquip (actor.cpp:2751): usecode slot numbers are ShapeInfo equip types minus one
INTRINSIC(Actor_I_getEquip) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(T); return A ? CruInventory::GetEquip(*A, T + 1) : 0; }
INTRINSIC(Actor_I_setEquip) { FCruItem* A = ActorFromPtr(Args); ARG_UINT16(T); ARG_ITEM_FROM_ID(It); if (!A || !It) return 0; return CruInventory::SetEquip(*A, *It) ? 1 : 0; }
INTRINSIC(Container_I_destroyContents) { ARG_ITEM_FROM_PTR(C); if (C && CruInventory::IsContainer(*C)) CruInventory::DestroyContents(*C); return 0; }
INTRINSIC(SpriteProcess_I_createSprite)
{
    // createSprite(shape, frame, end, delay, x, y, z) or createSprite(shape, frame, end, unk, repeats, delay, x, y, z)
    int32 Repeats = 1; ARG_SINT16(Sh); ARG_SINT16(F0); ARG_SINT16(F1);
    if (ArgSize == 18) { ARG_NULL16(); ARG_SINT16(R); Repeats = R; }
    ARG_SINT16(Delay); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z);
    return GCruKernel->AddProcess(new FSpriteProcess(Sh, F0, F1, Repeats, Delay, FromUC((int32)X), FromUC((int32)Y), Z));
}
INTRINSIC(BatteryChargerProcess_I_create) { return 0; }
INTRINSIC(CruHealerProcess_I_create) { return 0; }

// ---- Kernel / machine ----
INTRINSIC(Kernel_I_getNumProcesses) { ARG_OBJID(Id); ARG_UINT16(T); if (T == FKernel::CRU_PROC_TYPE_ALL) T = FKernel::PROC_TYPE_ALL; return GCruKernel->GetNumProcesses(Id, T); }
INTRINSIC(Kernel_I_resetRef) { ARG_OBJID(Id); ARG_UINT16(T); if (T == FKernel::CRU_PROC_TYPE_ALL) T = FKernel::PROC_TYPE_ALL; GCruKernel->KillProcesses(Id, T, true); return 0; }
INTRINSIC(UCMachine_I_true) { return 1; }
INTRINSIC(UCMachine_I_rndRange) { ARG_SINT16(Lo); ARG_SINT16(Hi); if (Hi <= Lo) return (uint16)Lo; return (uint16)FMath::RandRange((int32)Lo, (int32)Hi); }
INTRINSIC(UCMachine_I_numToStr) { ARG_SINT16(N); return GCruMachine->AssignString(FString::FromInt(N)); }

// ---- World / engine ----
INTRINSIC(World_I_getControlledNPCNum) { return GCruWorld->ControlledNpc; }
INTRINSIC(World_I_setControlledNPCNum) { ARG_UINT16(N); GCruWorld->ControlledNpc = N; return 0; }
INTRINSIC(World_I_getAlertActive) { return GCruWorld->bAlertActive ? 1 : 0; }
INTRINSIC(World_I_setAlertActive) { GCruWorld->bAlertActive = true; return 0; }
INTRINSIC(World_I_clrAlertActive) { GCruWorld->bAlertActive = false; return 0; }
INTRINSIC(World_I_gameDifficulty) { return GCruWorld->GameDifficulty; }
INTRINSIC(World_I_resetVargasShield) { return 0; }
INTRINSIC(Ultima8Engine_I_getCurrentTimerTick) { return GCruKernel->GetTickNum(); }
INTRINSIC(Ultima8Engine_I_setAvatarInStasis) { ARG_SINT16(S); if (GCruEngine) { GCruEngine->bAvatarInStasis = S != 0; UE_LOG(LogCru, Display, TEXT("usecode: avatar stasis %d"), S); } return 0; }
INTRINSIC(Ultima8Engine_I_getAvatarInStasis) { return (GCruEngine && GCruEngine->bAvatarInStasis) ? 1 : 0; }
INTRINSIC(Ultima8Engine_I_setCruStasis) { if (GCruEngine) GCruEngine->bCruStasis = true; return 0; }
INTRINSIC(Ultima8Engine_I_clrCruStasis) { if (GCruEngine) GCruEngine->bCruStasis = false; return 0; }
INTRINSIC(Ultima8Engine_I_getCrusaderTeleporting) { return (GCruEngine && GCruEngine->bTeleporting) ? 1 : 0; }
INTRINSIC(Ultima8Engine_I_setCrusaderTeleporting) { if (GCruEngine) GCruEngine->bTeleporting = true; return 0; }
INTRINSIC(Ultima8Engine_I_clrCrusaderTeleporting) { if (GCruEngine) GCruEngine->bTeleporting = false; return 0; }
INTRINSIC(Ultima8Engine_I_moveKeyDownRecently) { return 0; }

// ---- Audio ----
INTRINSIC(AudioProcess_I_playSFXCru) { ARG_ITEM_FROM_PTR(It); ARG_SINT16(Sfx); if (It) { GCruWorld->SfxQueue.Add({ It->Id, Sfx, false, false }); } return 0; }
INTRINSIC(AudioProcess_I_playAmbientSFXCru) { ARG_ITEM_FROM_PTR(It); ARG_SINT16(Sfx); if (It) GCruWorld->SfxQueue.Add({ It->Id, Sfx, false, true }); return 0; }
INTRINSIC(AudioProcess_I_stopSFXCru) { ARG_ITEM_FROM_PTR(It); int16 Sfx = -1; if (ArgSize == 6) { ARG_SINT16(S); Sfx = S; } if (It) GCruWorld->SfxQueue.Add({ It->Id, Sfx, true, false }); return 0; }
INTRINSIC(AudioProcess_I_isSFXPlayingForObject) { ARG_ITEM_FROM_PTR(It); ARG_SINT16(Sfx); return (It && GCruWorld->IsSfxPlaying(It->Id, Sfx)) ? 1 : 0; }
INTRINSIC(AudioProcess_I_playSFX) { ARG_SINT16(Sfx); GCruWorld->SfxQueue.Add({ 0, Sfx, false, false }); return 0; }
INTRINSIC(AudioProcess_I_stopAllSFX) { GCruWorld->SfxQueue.Add({ 0, -1, true, false }); return 0; }
INTRINSIC(MusicProcess_I_playMusic) { ARG_UINT8(T); if (GCruEngine) GCruEngine->PlayMusic(T & 0x7F); return 0; }
INTRINSIC(MusicProcess_I_stopMusic) { if (GCruEngine) GCruEngine->PlayMusic(0); return 0; }

// ---- Camera ----
INTRINSIC(CameraProcess_I_moveTo) { ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z); if (GCruEngine) GCruEngine->CameraMoveTo(FromUC((int32)X), FromUC((int32)Y), Z); return 0; }
INTRINSIC(CameraProcess_I_setCenterOn) { ARG_OBJID(Id); if (GCruEngine) GCruEngine->CameraCenterOn(Id); return 0; }
INTRINSIC(CameraProcess_I_getCameraX) { return GCruEngine ? (uint16)ToUC(GCruEngine->CameraPos.X) : 0; }
INTRINSIC(CameraProcess_I_getCameraY) { return GCruEngine ? (uint16)ToUC(GCruEngine->CameraPos.Y) : 0; }
INTRINSIC(CameraProcess_I_getCameraZ) { return GCruEngine ? (uint16)GCruEngine->CameraPos.Z : 0; }

// ---- Main actor ----
INTRINSIC(MainActor_I_hasKeycard) { ARG_UINT16(K); return (GCruEngine && GCruEngine->HasKeycard(K)) ? 1 : 0; }
INTRINSIC(MainActor_I_clrKeycards) { if (GCruEngine) GCruEngine->Keycards = 0; return 0; }
// MainActor::I_addItemCru(itemno, showtoast) (main_actor.cpp:908): 4 bytes, the item comes by id
INTRINSIC(MainActor_I_addItemCru) { ARG_ITEM_FROM_ID(It); ARG_UINT16(ShowToast); if (!It) return 0; return CruInventory::AddItemCru(*It, ShowToast != 0) ? 1 : 0; }
INTRINSIC(MainActor_I_getNumberOfCredits) { return (uint16)CruInventory::GetNumberOfCredits(); }
INTRINSIC(MainActor_I_getMaxEnergy) { FCruItem* A = ActorFromPtr(Args); return (A && A->Id == Cru::kMainActorId) ? (uint16)CruInventory::GetMaxEnergy() : 0; }
INTRINSIC(MainActor_I_teleportToEgg) { ARG_UINT16(Map); ARG_UINT16(Egg); if (GCruEngine) GCruEngine->TeleportToEgg(Map, Egg); return 0; }
INTRINSIC(MainActor_I_switchMap) { ARG_UINT16(Map); UE_LOG(LogCru, Display, TEXT("switchMap %d (not implemented)"), Map); return 0; }

// ---- Position checks ----
INTRINSIC(CurrentMap_I_canExistAt) { ARG_UINT16(Sh); ARG_UINT16(X); ARG_UINT16(Y); ARG_UINT8(Z); return GCruWorld->GetPositionInfo(FromUC((int32)X), FromUC((int32)Y), Z, Sh, 0).bValid ? 1 : 0; }
INTRINSIC(CurrentMap_I_canExistAtPoint) { ARG_ITEM_FROM_PTR(Other); ARG_UINT16(Sh); ARG_WORLDPOINT(P); if (Sh > 0x800) return 0; return GCruWorld->GetPositionInfo(FromUC(WP_X(P)), FromUC(WP_Y(P)), WP_Z(P), Sh, 0).bValid ? 1 : 0; }

// ---- UI / cutscene stubs (no-ops) ----
INTRINSIC(Stub0) { return 0; }
#define IMPL_STUB(name) static const FCruIntrinsic name = Stub0;
IMPL_STUB(CruStatusGump_I_hideStatusGump) IMPL_STUB(CruStatusGump_I_showStatusGump)

// ---- fades (PaletteFaderProcess) and cutscenes (MovieGump) ----
static int32 FadeSteps(const uint8*& Args, uint32 ArgSize, int32 Default) { if (ArgSize > 0) { ARG_UINT16(N); return N; } return Default; }
INTRINSIC(PaletteFaderProcess_I_fadeToBlack) { const int32 N = FadeSteps(Args, ArgSize, 40); return GCruEngine ? GCruEngine->StartFade(1.f, N, FColor::Black) : 0; }
INTRINSIC(PaletteFaderProcess_I_fadeToGamePal) { const int32 N = FadeSteps(Args, ArgSize, 20); return GCruEngine ? GCruEngine->StartFade(0.f, N) : 0; }
INTRINSIC(PaletteFaderProcess_I_jumpToAllBlack) { if (GCruEngine) GCruEngine->StartFade(1.f, 0, FColor::Black); return 0; }
INTRINSIC(PaletteFaderProcess_I_jumpToAllWhite) { if (GCruEngine) GCruEngine->StartFade(1.f, 0, FColor::White); return 0; }
INTRINSIC(PaletteFaderProcess_I_jumpToGreyScale) { return 0; }   // greyscale palette: no equivalent yet
INTRINSIC(PaletteFaderProcess_I_jumpToNormalPalette) { if (GCruEngine) GCruEngine->StartFade(0.f, 0); return 0; }
INTRINSIC(PaletteFaderProcess_I_fadeToGivenColor) { ARG_UINT8(Rc); ARG_UINT8(Gc); ARG_UINT8(Bc); ARG_UINT16(N); ARG_UINT16(Unk); return GCruEngine ? GCruEngine->StartFade(1.f, N, FColor(Rc, Gc, Bc)) : 0; }
static uint32 PlayMovieCommon(const uint8* Args, bool bHasItem)
{
    if (bHasItem) { ARG_NULL32(); }
    ARG_UINT16(StrId);
    const FString Name = GCruMachine->GetString(StrId);
    if (GCruEngine && !Name.IsEmpty()) return GCruEngine->PlayMovie(Name);
    return 0;
}
INTRINSIC(MovieGump_I_playMovieCutscene) { return PlayMovieCommon(Args, true); }
INTRINSIC(MovieGump_I_playMovieCutsceneAlt) { return PlayMovieCommon(Args, true); }
INTRINSIC(MovieGump_I_playMovieOverlay) { return PlayMovieCommon(Args, true); }
IMPL_STUB(Game_I_playCredits) IMPL_STUB(KeypadGump_I_showKeypad) IMPL_STUB(ComputerGump_I_readComputer) IMPL_STUB(WeaselGump_I_showWeaselGump)

#define IMPL(x) IMPL_##x
// Resolve each table name to its implementation, or nullptr when not ported yet.
#define IMPL_Item_I_getFrame Item_I_getFrame
#define IMPL_Item_I_setFrame Item_I_setFrame
#define IMPL_Item_I_getShape Item_I_getShape
#define IMPL_Item_I_setShape Item_I_setShape
#define IMPL_Item_I_getMapArray Item_I_getMapArray
#define IMPL_Item_I_setMapArray Item_I_setMapArray
#define IMPL_Item_I_getNpcNum Item_I_getNpcNum
#define IMPL_Item_I_setNpcNum Item_I_setNpcNum
#define IMPL_Item_I_getStatus Item_I_getStatus
#define IMPL_Item_I_orStatus Item_I_orStatus
#define IMPL_Item_I_andStatus Item_I_andStatus
#define IMPL_Item_I_getQ Item_I_getQ
#define IMPL_Item_I_setQ Item_I_setQ
#define IMPL_Item_I_getQLo Item_I_getQLo
#define IMPL_Item_I_getQHi Item_I_getQHi
#define IMPL_Item_I_setQLo Item_I_setQLo
#define IMPL_Item_I_setQHi Item_I_setQHi
#define IMPL_Item_I_getQuality Item_I_getQuality
#define IMPL_Item_I_setQuality Item_I_setQuality
#define IMPL_Item_I_setQAndCombine Item_I_setQAndCombine
#define IMPL_Item_I_getUnkEggType Item_I_getUnkEggType
#define IMPL_Item_I_getFamily Item_I_getFamily
#define IMPL_Item_I_getFamilyOfType Item_I_getFamilyOfType
#define IMPL_Item_I_getTypeFlag Item_I_getTypeFlag
#define IMPL_Item_I_getX Item_I_getX
#define IMPL_Item_I_getY Item_I_getY
#define IMPL_Item_I_getZ Item_I_getZ
#define IMPL_Item_I_getCX Item_I_getCX
#define IMPL_Item_I_getCY Item_I_getCY
#define IMPL_Item_I_getCZ Item_I_getCZ
#define IMPL_Item_I_getPoint Item_I_getPoint
#define IMPL_Item_I_getFootpadData Item_I_getFootpadData
#define IMPL_Item_I_touch Item_I_touch
#define IMPL_Item_I_destroy Item_I_destroy
#define IMPL_Item_I_push Item_I_push
#define IMPL_Item_I_getEtherealTop Item_I_getEtherealTop
#define IMPL_Item_I_pop Item_I_pop
#define IMPL_Item_I_popToCoords Item_I_popToCoords
#define IMPL_Item_I_popToEnd Item_I_popToEnd
#define IMPL_Item_I_popToContainer Item_I_popToContainer
#define IMPL_Item_I_move Item_I_move
#define IMPL_Item_I_create Item_I_create
#define IMPL_Item_I_legalCreateAtCoords Item_I_legalCreateAtCoords
#define IMPL_Item_I_legalCreateAtPoint Item_I_legalCreateAtPoint
#define IMPL_Item_I_legalMoveToPoint Item_I_legalMoveToPoint
#define IMPL_Item_I_overlaps Item_I_overlaps
#define IMPL_Item_I_isOn Item_I_isOn
#define IMPL_Item_I_isCompletelyOn Item_I_isCompletelyOn
#define IMPL_Item_I_isCentreOn Item_I_isCentreOn
#define IMPL_Item_I_isInNpc Item_I_isInNpc
#define IMPL_Item_I_inFastArea Item_I_inFastArea
#define IMPL_Item_I_enterFastArea Item_I_enterFastArea
#define IMPL_Item_I_isPartlyOnScreen Item_I_isPartlyOnScreen
#define IMPL_Item_I_isCrusTypeNPC Item_I_isCrusTypeNPC
#define IMPL_Item_I_setBroken Item_I_setBroken
#define IMPL_Item_I_use Item_I_use
#define IMPL_Item_I_equip Item_I_equip
#define IMPL_Item_I_unequip Item_I_unequip
#define IMPL_Item_I_avatarStoleSomething Item_I_avatarStoleSomething
#define IMPL_Item_I_cast Item_I_cast
#define IMPL_Item_I_getDirToItem Item_I_getDirToItem
#define IMPL_Item_I_getDirFromItem Item_I_getDirFromItem
#define IMPL_Item_I_getDirToCoords Item_I_getDirToCoords
#define IMPL_Item_I_getDirFromTo16 Item_I_getDirFromTo16
#define IMPL_Item_I_getClosestDirectionInRange Item_I_getClosestDirectionInRange
#define IMPL_Item_I_getRangeIfVisible Item_I_getRangeIfVisible
#define IMPL_Item_I_fireWeapon Item_I_fireWeapon
#define IMPL_Item_I_fireDistance Item_I_fireDistance
#define IMPL_Item_I_explode Item_I_explode
#define IMPL_Item_I_receiveHit Item_I_receiveHit
#define IMPL_Item_I_hurl Item_I_hurl
#define IMPL_Item_I_shoot Item_I_shoot
#define IMPL_Item_I_fall Item_I_fall
#define IMPL_Item_I_getSurfaceWeight Item_I_getSurfaceWeight
#define IMPL_Egg_I_getEggId Egg_I_getEggId
#define IMPL_Egg_I_setEggId Egg_I_setEggId
#define IMPL_Egg_I_getEggXRange Egg_I_getEggXRange
#define IMPL_Egg_I_getEggYRange Egg_I_getEggYRange
#define IMPL_Egg_I_setEggXRange Egg_I_setEggXRange
#define IMPL_Egg_I_setEggYRange Egg_I_setEggYRange
#define IMPL_Actor_I_isNPC Actor_I_isNPC
#define IMPL_Actor_I_isDead Actor_I_isDead
#define IMPL_Actor_I_isBusy Actor_I_isBusy
#define IMPL_Actor_I_isInCombat Actor_I_isInCombat
#define IMPL_Actor_I_isKneeling Actor_I_isKneeling
#define IMPL_Actor_I_isFalling Actor_I_isFalling
#define IMPL_Actor_I_getDir Actor_I_getDir
#define IMPL_Actor_I_getMap Actor_I_getMap
#define IMPL_Actor_I_getHp Actor_I_getHp
#define IMPL_Actor_I_setDead Actor_I_setDead
#define IMPL_Actor_I_setActivity Actor_I_setActivity
#define IMPL_Actor_I_getLastAnimSet Actor_I_getLastAnimSet
#define IMPL_Actor_I_getUnkByte Actor_I_getUnkByte
#define IMPL_Actor_I_getCurrentActivityNo Actor_I_getCurrentActivityNo
#define IMPL_Actor_I_getLastActivityNo Actor_I_getLastActivityNo
#define IMPL_Actor_I_getMana Actor_I_getMana
#define IMPL_Actor_I_setMana Actor_I_setMana
#define IMPL_Actor_I_addHp Actor_I_addHp
#define IMPL_Actor_I_clrImmortal Actor_I_clrImmortal
#define IMPL_Actor_I_setImmortal Actor_I_setImmortal
#define IMPL_Actor_I_clrInCombat Actor_I_clrInCombat
#define IMPL_Kernel_I_getNumProcesses Kernel_I_getNumProcesses
#define IMPL_Kernel_I_resetRef Kernel_I_resetRef
#define IMPL_UCMachine_I_true UCMachine_I_true
#define IMPL_UCMachine_I_rndRange UCMachine_I_rndRange
#define IMPL_UCMachine_I_numToStr UCMachine_I_numToStr
#define IMPL_World_I_getControlledNPCNum World_I_getControlledNPCNum
#define IMPL_World_I_setControlledNPCNum World_I_setControlledNPCNum
#define IMPL_World_I_getAlertActive World_I_getAlertActive
#define IMPL_World_I_setAlertActive World_I_setAlertActive
#define IMPL_World_I_clrAlertActive World_I_clrAlertActive
#define IMPL_World_I_gameDifficulty World_I_gameDifficulty
#define IMPL_World_I_resetVargasShield World_I_resetVargasShield
#define IMPL_Ultima8Engine_I_getCurrentTimerTick Ultima8Engine_I_getCurrentTimerTick
#define IMPL_Ultima8Engine_I_setAvatarInStasis Ultima8Engine_I_setAvatarInStasis
#define IMPL_Ultima8Engine_I_getAvatarInStasis Ultima8Engine_I_getAvatarInStasis
#define IMPL_Ultima8Engine_I_setCruStasis Ultima8Engine_I_setCruStasis
#define IMPL_Ultima8Engine_I_clrCruStasis Ultima8Engine_I_clrCruStasis
#define IMPL_Ultima8Engine_I_getCrusaderTeleporting Ultima8Engine_I_getCrusaderTeleporting
#define IMPL_Ultima8Engine_I_setCrusaderTeleporting Ultima8Engine_I_setCrusaderTeleporting
#define IMPL_Ultima8Engine_I_clrCrusaderTeleporting Ultima8Engine_I_clrCrusaderTeleporting
#define IMPL_Ultima8Engine_I_moveKeyDownRecently Ultima8Engine_I_moveKeyDownRecently
#define IMPL_AudioProcess_I_playSFXCru AudioProcess_I_playSFXCru
#define IMPL_AudioProcess_I_playAmbientSFXCru AudioProcess_I_playAmbientSFXCru
#define IMPL_AudioProcess_I_stopSFXCru AudioProcess_I_stopSFXCru
#define IMPL_AudioProcess_I_isSFXPlayingForObject AudioProcess_I_isSFXPlayingForObject
#define IMPL_AudioProcess_I_playSFX AudioProcess_I_playSFX
#define IMPL_AudioProcess_I_stopAllSFX AudioProcess_I_stopAllSFX
#define IMPL_MusicProcess_I_playMusic MusicProcess_I_playMusic
#define IMPL_MusicProcess_I_stopMusic MusicProcess_I_stopMusic
#define IMPL_CameraProcess_I_moveTo CameraProcess_I_moveTo
#define IMPL_CameraProcess_I_setCenterOn CameraProcess_I_setCenterOn
#define IMPL_CameraProcess_I_getCameraX CameraProcess_I_getCameraX
#define IMPL_CameraProcess_I_getCameraY CameraProcess_I_getCameraY
#define IMPL_CameraProcess_I_getCameraZ CameraProcess_I_getCameraZ
#define IMPL_MainActor_I_hasKeycard MainActor_I_hasKeycard
#define IMPL_MainActor_I_clrKeycards MainActor_I_clrKeycards
#define IMPL_MainActor_I_addItemCru MainActor_I_addItemCru
#define IMPL_MainActor_I_getNumberOfCredits MainActor_I_getNumberOfCredits
#define IMPL_MainActor_I_getMaxEnergy MainActor_I_getMaxEnergy
#define IMPL_MainActor_I_teleportToEgg MainActor_I_teleportToEgg
#define IMPL_MainActor_I_switchMap MainActor_I_switchMap
#define IMPL_CurrentMap_I_canExistAt CurrentMap_I_canExistAt
#define IMPL_CurrentMap_I_canExistAtPoint CurrentMap_I_canExistAtPoint
#define IMPL_MovieGump_I_playMovieCutsceneAlt MovieGump_I_playMovieCutsceneAlt
#define IMPL_MovieGump_I_playMovieOverlay MovieGump_I_playMovieOverlay
#define IMPL_MovieGump_I_playMovieCutscene MovieGump_I_playMovieCutscene
#define IMPL_PaletteFaderProcess_I_fadeToBlack PaletteFaderProcess_I_fadeToBlack
#define IMPL_PaletteFaderProcess_I_fadeToGamePal PaletteFaderProcess_I_fadeToGamePal
#define IMPL_PaletteFaderProcess_I_jumpToAllBlack PaletteFaderProcess_I_jumpToAllBlack
#define IMPL_PaletteFaderProcess_I_jumpToGreyScale PaletteFaderProcess_I_jumpToGreyScale
#define IMPL_PaletteFaderProcess_I_jumpToNormalPalette PaletteFaderProcess_I_jumpToNormalPalette
#define IMPL_PaletteFaderProcess_I_fadeToGivenColor PaletteFaderProcess_I_fadeToGivenColor
#define IMPL_PaletteFaderProcess_I_jumpToAllWhite PaletteFaderProcess_I_jumpToAllWhite
#define IMPL_CruStatusGump_I_hideStatusGump CruStatusGump_I_hideStatusGump
#define IMPL_CruStatusGump_I_showStatusGump CruStatusGump_I_showStatusGump
#define IMPL_Game_I_playCredits Game_I_playCredits
#define IMPL_KeypadGump_I_showKeypad KeypadGump_I_showKeypad
#define IMPL_ComputerGump_I_readComputer ComputerGump_I_readComputer
#define IMPL_WeaselGump_I_showWeaselGump WeaselGump_I_showWeaselGump
// not ported yet
#define IMPL_Actor_I_turnToward Actor_I_turnToward
#define IMPL_Actor_I_doAnim Actor_I_doAnim
#define IMPL_Actor_I_createActor Actor_I_createActor
#define IMPL_Actor_I_createActorCru Actor_I_createActorCru
#define IMPL_Actor_I_teleport Actor_I_teleport
#define IMPL_Actor_I_setDefaultActivity0 Actor_I_setDefaultActivity0
#define IMPL_Actor_I_setDefaultActivity1 Actor_I_setDefaultActivity1
#define IMPL_Actor_I_setDefaultActivity2 Actor_I_setDefaultActivity2
#define IMPL_Actor_I_getDefaultActivity0 Actor_I_getDefaultActivity0
#define IMPL_Actor_I_getDefaultActivity1 Actor_I_getDefaultActivity1
#define IMPL_Actor_I_getDefaultActivity2 Actor_I_getDefaultActivity2
#define IMPL_Actor_I_setCombatTactic Actor_I_setCombatTactic
#define IMPL_Actor_I_getEquip Actor_I_getEquip
#define IMPL_Actor_I_setEquip Actor_I_setEquip
#define IMPL_Actor_I_setTarget Actor_I_setTarget
#define IMPL_Actor_I_setUnkByte Actor_I_setUnkByte
#define IMPL_Container_I_destroyContents Container_I_destroyContents
#define IMPL_SpriteProcess_I_createSprite SpriteProcess_I_createSprite
#define IMPL_BatteryChargerProcess_I_create BatteryChargerProcess_I_create
#define IMPL_CruHealerProcess_I_create CruHealerProcess_I_create

#include "RemorseIntrinsicTable.inl"

const FCruIntrinsicEntry* GetRemorseIntrinsics(uint32& OutCount)
{
    OutCount = UE_ARRAY_COUNT(GRemorseIntrinsics);
    return GRemorseIntrinsics;
}
