#include "CruCombat.h"
#include "CruWorld.h"
#include "CruEngine.h"
#include "UsecodeFlex.h"
#include "Misc/FileHelper.h"

extern bool GCruCombatDebug;

using namespace CruAnim;

// ---------------------------------------------------------------- static data
// FireType table for No Remorse (ScummVM fire_type_table.cpp)
static const FCruFireType GFireTypes[] = {
    { 0x0, 0,    0,    0,  0,  0,     0,   false, 0,   0,    false },
    { 0x1, 0x12, 0x19, 0,  1,  0x82,  0x7, false, 0xc, 1000, false },
    { 0x2, 0xa,  0x11, 0,  8,  0x50,  0x7, false, 0xc, 1000, false },
    { 0x3, 0xc8, 0xc8, 10, 1,  0,     0,   true,  0x3, 1000, true },
    { 0x4, 0x96, 0x96, 10, 1,  0,     0,   false, 0x1, 1000, true },
    { 0x5, 0x19, 0x2d, 0,  1,  0x190, 0x6, false, 0x4, 1000, true },
    { 0x6, 0xf,  0x23, 0,  1,  0x177, 0x6, true,  0x4, 1000, true },
    { 0x7, 0x5,  0x5,  0,  1,  0,     0,   false, 0x1, 1000, true },
    { 0x8, 0x5,  0x5,  0,  1,  0,     0,   false, 0x1, 1000, false },
    { 0x9, 0xa,  0xf,  0,  1,  0x12c, 0x6, true,  0x1, 100,  true },
    { 0xa, 0x2d, 0x50, 10, 3,  0,     0,   false, 0x3, 100,  true },
    { 0xb, 0xe,  0x14, 0,  1,  0x50,  0x7, false, 0xc, 1000, false },
    { 0xc, 0x5,  0x5,  0,  1,  0,     0,   false, 0x1, 1000, false },
    { 0xd, 0xa,  0x11, 0,  5,  0x50,  0x7, false, 0xc, 1000, false },
    { 0xe, 0xfa, 0xfa, 4,  1,  0x9c4, 0x4, true,  0x3, 1000, true },
    { 0xf, 0x23, 0x37, 4,  1,  0x2ee, 0x4, false, 0x3, 1000, true },
};
const FCruFireType* CruGetFireType(int32 Type) { return (Type >= 0 && Type < (int32)UE_ARRAY_COUNT(GFireTypes)) ? &GFireTypes[Type] : nullptr; }

// remorseweapons.ini: Name, Shape, Sound, AmmoType, DamageType, OverlayShape, bSmall, ReloadSound, ShotDelay, ClipSize, AmmoShape, EnergyUse
static const FCruWeaponInfo GWeapons[] = {
    { TEXT("BA-40"),  0x32E, 0x2C, 0, 0x0B, 0x1A1, true,  0x2B, 0x0C, 20,   0,     0 },
    { TEXT("BA-41"),  0x32F, 0x2C, 1, 0x0B, 0x1A1, true,  0x2B, 0x09, 20,   0x33D, 0 },
    { TEXT("PA-21"),  0x330, 0x08, 0, 0x05, 0x36D, true,  0,    20,   0,    0,     0x19 },
    { TEXT("EM-4"),   0x38C, 0x43, 0, 0x0F, 0x36F, false, 0,    20,   0,    0,     0x87 },
    { TEXT("SG-A1"),  0x332, 0x55, 2, 0x02, 0x33B, false, 0x19, 0x19, 12,   0x33E, 0 },
    { TEXT("RP-22"),  0x333, 0x2C, 3, 0x01, 0x36F, false, 0x2B, 10,   0x3C, 0x33F, 0 },
    { TEXT("RP-32"),  0x334, 0x2C, 3, 0x01, 0x36F, false, 0x2B, 6,    0x3C, 0x33F, 0 },
    { TEXT("AR-7"),   0x38E, 0x2C, 4, 0x0A, 0x36E, false, 0x2B, 10,   8,    0x340, 0 },
    { TEXT("GL-303"), 0x388, 0x64, 5, 0x03, 0x36E, false, 0x2B, 0x3C, 10,   0x341, 0 },
    { TEXT("PA-31"),  0x38A, 0x02, 0, 0x05, 0x36F, false, 0,    12,   0,    0,     0x23 },
    { TEXT("PL-1"),   0x38D, 0x47, 0, 0x06, 0x36E, false, 0,    6,    0,    0,     0x2D },
    { TEXT("AC-88"),  0x38B, 0x55, 2, 0x0D, 0x36E, false, 0x2B, 10,   12,   0x33E, 0 },
    { TEXT("UV-9"),   0x386, 0x89, 0, 0x0E, 0x36E, false, 0,    0x23, 0,    0,     0xD2 },
};
const FCruWeaponInfo* CruGetWeaponInfo(uint32 Shape) { for (const FCruWeaponInfo& W : GWeapons) if (W.Shape == Shape) return &W; return nullptr; }

static const FCruGumpIcon GGumpIcons[] = {
    { 0x32E, 3, 2, TEXT("BA-40") }, { 0x32F, 3, 1, TEXT("BA-41") }, { 0x330, 3, 7, TEXT("PA-21") }, { 0x38C, 3, 3, TEXT("EM-4") },
    { 0x332, 3, 0, TEXT("SG-A1") }, { 0x333, 3, 6, TEXT("RP-22") }, { 0x334, 3, 5, TEXT("RP-32") }, { 0x38E, 3, 12, TEXT("AR-7") },
    { 0x388, 3, 8, TEXT("GL-303") }, { 0x38A, 3, 9, TEXT("PA-31") }, { 0x38D, 3, 10, TEXT("PL-1") }, { 0x38B, 3, 4, TEXT("AC-88") },
    { 0x386, 3, 11, TEXT("UV-9") },
    { 0x33D, 5, 8, TEXT("BA-41 CLIP") }, { 0x33E, 5, 9, TEXT("SG-A1 CLIP") }, { 0x33F, 5, 10, TEXT("RP-22 CLIP") }, { 0x340, 5, 11, TEXT("AR-7 CLIP") }, { 0x341, 5, 12, TEXT("GL-303 CLIP") },
    { 0x343, 4, 0, TEXT("GRENADE") }, { 0x34E, 4, 1, TEXT("CONCUSSION GRENADE") }, { 0x34F, 4, 2, TEXT("NERVE GAS GRENADE") }, { 0x350, 4, 3, TEXT("EMP GRENADE") },
    { 0x560, 4, 4, TEXT("SPIDER BOMB") }, { 0x39A, 4, 5, TEXT("LAND MINE") }, { 0x39B, 4, 7, TEXT("BLAST PAC") }, { 0x39C, 4, 6, TEXT("FUSION PAC") },
    { 0x351, 5, 0, TEXT("MEDICAL KIT") }, { 0x4D4, 5, 1, TEXT("DATA LINK") }, { 0x52D, 5, 2, TEXT("INHIBITOR") }, { 0x52E, 5, 3, TEXT("IONIC GENERATOR") },
    { 0x582, 5, 0x19, TEXT("ENERGY CUBE") }, { 0x52F, 5, 5, TEXT("PLASMA GENERATOR") }, { 0x55F, 5, 0x18, TEXT("DET PAC") }, { 0x530, 5, 7, TEXT("GRAVITON GENERATOR") },
    { 0x3A2, 5, 0x16, TEXT("CHEMICAL BATTERY") }, { 0x3A3, 5, 0x15, TEXT("FUSION BATTERY") }, { 0x3A4, 5, 0x17, TEXT("FISSION BATTERY") },
    { 0x4ED, 3, 0xD, TEXT("CREDITS") }, { 0x111, 5, 0xD, TEXT("KEYCARD") },
};
const FCruGumpIcon* CruGetGumpIcon(uint32 Shape) { for (const FCruGumpIcon& G : GGumpIcons) if (G.Shape == Shape) return &G; return nullptr; }

static TArray<FCruCombatDat> GCombatDats;
static TArray<FCruDamageInfo> GDamageInfos;
static uint32 GLastNpcAlertTick = 0;

static int32 MaxDistXYZ(const FIntVector& A, const FIntVector& B) { return FMath::Max3(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y), FMath::Abs(A.Z - B.Z)); }
static void PlaySfx(ObjId Item, int32 Sfx) { if (Sfx > 0) GCruWorld->SfxQueue.Add({ Item, Sfx, false, false }); }
template<int32 N> static int32 RandomElem(const int16 (&Arr)[N]) { return Arr[FMath::RandRange(0, N - 1)]; }
static int32 RandRng(int32 Lo, int32 Hi) { return FMath::RandRange(Lo, Hi); }
static FCruItem* GetControlledActor() { return GCruWorld->GetItem(GCruWorld->ControlledNpc); }
static uint32 FrameNum() { return GCruKernel->GetTickNum() / 2; }

bool CruCombat::LoadCombatDat(const FString& Path)
{
    FUsecodeFlex Flex;
    if (!Flex.Load(Path)) return false;
    GCombatDats.Reset();
    for (uint32 i = 0; i < Flex.NumEntries(); ++i)
    {
        uint32 Size = 0; const uint8* D = Flex.GetEntry(i, Size);
        FCruCombatDat& C = GCombatDats.AddDefaulted_GetRef();
        if (!D || Size < 24) continue;
        char Name[17] = { 0 }; FMemory::Memcpy(Name, D, 16); C.Name = FString(ANSI_TO_TCHAR(Name));
        for (int32 b = 0; b < 4; ++b) C.Offsets[b] = D[16 + b * 2] | (D[17 + b * 2] << 8);
        C.Data.Append(D, Size);
    }
    UE_LOG(LogCru, Display, TEXT("combat: %d tactics loaded (%s, %s, %s...)"), GCombatDats.Num(), GCombatDats.Num() > 0 ? *GCombatDats[0].Name : TEXT(""), GCombatDats.Num() > 1 ? *GCombatDats[1].Name : TEXT(""), GCombatDats.Num() > 2 ? *GCombatDats[2].Name : TEXT(""));
    return true;
}
const FCruCombatDat* CruCombat::GetCombatDat(int32 Index) { return GCombatDats.IsValidIndex(Index) ? &GCombatDats[Index] : nullptr; }

bool CruCombat::LoadDamageFlex(const FString& Path)
{
    FUsecodeFlex Flex;
    if (!Flex.Load(Path)) return false;
    uint32 Size = 0; const uint8* D = Flex.GetEntry(0, Size);
    if (!D) return false;
    GDamageInfos.SetNum(Size / 6);
    int32 Valid = 0;
    for (uint32 i = 0; i < Size / 6; ++i)
    {
        const uint8* R = D + i * 6;
        if (R[0] == 0) continue;
        FCruDamageInfo& Di = GDamageInfos[i];
        Di.Flags = R[0]; Di.Sound = R[1]; Di.Data[0] = R[2]; Di.Data[1] = R[3]; Di.Data[2] = R[4]; Di.DamagePoints = R[5];
        ++Valid;
    }
    UE_LOG(LogCru, Display, TEXT("combat: damage info for %d of %d shapes"), Valid, GDamageInfos.Num());
    return true;
}
const FCruDamageInfo* CruCombat::GetDamageInfo(uint32 Shape) { return (GDamageInfos.IsValidIndex(Shape) && GDamageInfos[Shape].IsValid()) ? &GDamageInfos[Shape] : nullptr; }

// ---------------------------------------------------------------- sprite process
FSpriteProcess::FSpriteProcess(int32 InShape, int32 InFrame, int32 InLast, int32 InRepeats, int32 InDelay, int32 InX, int32 InY, int32 InZ)
    : Shape(InShape), Frame(InFrame), FirstFrame(InFrame), LastFrame(InLast), Repeats(InRepeats), Delay(InDelay * 2), X(InX), Y(InY), Z(InZ)
{
    if (FCruItem* It = GCruWorld->CreateItem(Shape, Frame, 0, Cru::FLG_DISPOSABLE, 0, 0, Cru::EXT_SPRITE, true))
    {
        GCruWorld->Move(*It, X, Y, Z);
        ItemNum = It->Id;
    }
    if (Delay <= 0) Delay = 1;
}
FSpriteProcess::~FSpriteProcess() { if (FCruItem* It = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr) GCruWorld->DestroyItem(*It); }
void FSpriteProcess::Move(int32 NewX, int32 NewY, int32 NewZ) { X = NewX; Y = NewY; Z = NewZ; if (FCruItem* It = GCruWorld->GetItem(ItemNum)) GCruWorld->Move(*It, X, Y, Z); }

void FSpriteProcess::Run()
{
    FCruItem* It = GCruWorld->GetItem(ItemNum);
    if (!It || (Frame > LastFrame && Repeats == 1 && !DelayCounter)) { Terminate(); return; }
    if (DelayCounter) { DelayCounter = (DelayCounter + 1) % Delay; return; }
    if (Frame > LastFrame) { Frame = FirstFrame; --Repeats; }
    GCruWorld->SetFrame(*It, Frame);
    ++Frame;
    DelayCounter = (DelayCounter + 1) % Delay;
}

// ---------------------------------------------------------------- bullet splash / splash damage
void CruCombat::MakeBulletSplashShapeAndPlaySound(const FCruFireType& Ft, int32 X, int32 Y, int32 Z)
{
    static const int16 FIRESOUND_1[] = { 0x26, 0x27, 0x41, 0x42, 0x45, 0x46 };
    static const int16 FIRESOUND_3[] = { 0x1c, 0x6c, 0x3e };
    static const int16 FIRESOUND_7[] = { 0x48, 0x5 };
    static const int16 FIRESHAPE_3[] = { 0x326, 0x320, 0x321 };
    static const int16 FIRESHAPE_10[] = { 0x31c, 0x31f, 0x322 };
    int32 Sfx = 0, Shape = 0;
    switch (Ft.TypeNo)
    {
    case 1: case 0xb: Shape = 0x1d8; Sfx = RandomElem(FIRESOUND_1); break;
    case 2: Shape = 0x1d8; break;
    case 3: case 4: Shape = RandomElem(FIRESHAPE_3); Sfx = RandomElem(FIRESOUND_3); break;
    case 5: Shape = 0x537; break;
    case 6: Shape = 0x578; break;
    case 7: Shape = 0x537; Sfx = RandomElem(FIRESOUND_7); break;
    case 10: Shape = RandomElem(FIRESHAPE_10); Sfx = RandomElem(FIRESOUND_3); break;
    case 0xd: Shape = 0x1d8; Sfx = RandomElem(FIRESOUND_1); break;
    case 0xe: Shape = 0x56b; break;
    case 0xf: Shape = 0x59b; Sfx = RandomElem(FIRESOUND_7); break;
    case 9: default: Shape = 0x537; break;
    }
    int32 First = 0, Last = 0x27;
    switch (Shape)
    {
    case 0x56b: First = RandRng(0, 2) * 6; Last = First + 5; break;
    case 0x537: Last = 10; break;
    case 0x578: First = RandRng(0, 2) * 5; Last = First + 4; break;
    case 0x59b: First = RandRng(0, 1) * 4; Last = First + 3; break;
    case 0x1d8:
        switch (RandRng(0, 3)) { case 0: Last = 4; break; case 1: First = 5; Last = 8; break; case 2: First = 9; Last = 0xc; break; default: First = 0xd; Last = 0x10; break; }
        break;
    }
    GCruKernel->AddProcess(new FSpriteProcess(Shape, First, Last, 1, 3, X, Y, Z));
    if (Sfx) PlaySfx(0, Sfx);
}

void CruCombat::ApplySplashDamageAround(const FCruFireType& Ft, const FIntVector& Pt, int32 Damage, int32 RangeDiv, ObjId Exclude, ObjId Src)
{
    if (!Ft.Range || RangeDiv <= 0) return;
    FUCList List(2);
    const uint8 Script[] = { 1, '$' };
    GCruWorld->AreaSearch(List, Script, 2, (uint16)(Ft.Range * 32 / RangeDiv), Pt.X, Pt.Y);
    const ObjId Controlled = GCruWorld->ControlledNpc;
    for (uint32 i = 0; i < List.GetSize(); ++i)
    {
        FCruItem* It = GCruWorld->GetItem(List.GetUInt16(i));
        if (!It) continue;
        if (It->Id == Exclude || (It->Id == Src && Src != Controlled) || It->Shape == BULLET_SPLASH_SHAPE) continue;
        int32 Dmg = Damage;
        if (Ft.TypeNo == 3 || Ft.TypeNo == 4 || Ft.TypeNo == 10)
        {
            int32 R = (MaxDistXYZ(Pt, FIntVector(It->X, It->Y, It->Z)) / 32) / 3;
            if (R) Dmg /= R;
        }
        if (!Dmg) continue;
        int32 Dir = 0;
        if (const FCruItem* S = GCruWorld->GetItem(Src)) { const FIntVector C = GCruWorld->GetCentre(*S); Dir = CruDirGetWorldDir(Pt.Y - C.Y, Pt.X - C.X, false); }
        ReceiveHit(*It, 0, Dir, Dmg, Ft.TypeNo);
    }
}

// ---------------------------------------------------------------- projectile
FSuperSpriteProcess::FSuperSpriteProcess(int32 InShape, int32 InFrame, const FIntVector& Start, const FIntVector& Dest, uint16 InFireType, uint16 InDamage, ObjId InSource, ObjId InTarget, bool bInexact)
    : Shape(InShape), Frame(InFrame), StartPt(Start), NowPt(Start), NextPt(Start), DestPt(Dest), FireType(InFireType), Damage(InDamage), Source(InSource), Target(InTarget), bStartedAsFiretype9(InFireType == 9)
{
    const FCruFireType* Ft = CruGetFireType(FireType);
    if (Ft && Ft->bAccurate) bInexact = false;
    if (bInexact)
    {
        int32 Rng = MaxDistXYZ(StartPt, DestPt);
        FCruItem* SrcItem = GCruWorld->GetItem(Source);
        FCruItem* Controlled = GetControlledActor();
        if (SrcItem && SrcItem == Controlled)
        {
            if (FireType == 2 || FireType == 13) Rng /= 8; else if (FireType == 5) Rng /= 12; else if (FireType == 10) Rng /= 5; else Rng /= 10;
        }
        else
        {
            const FCruItem* SrcNpc = (SrcItem && SrcItem->bActor) ? SrcItem : nullptr;
            const uint32 Frameno = FrameNum();
            const uint32 TimeoutFinish = Controlled ? Controlled->GetAttackMoveTimeoutFinishFrame() : 0;
            if (!SrcNpc || !SrcNpc->bAttackAimFlag)
            {
                if (!SrcNpc || Frameno < TimeoutFinish)
                {
                    if (!SrcNpc && Controlled && (Controlled->ActorFlags & ACT_KNEELING)) Rng /= 5;
                    else
                    {
                        const int32 Dodge = Controlled ? FMath::Max<int32>(1, Controlled->AttackMoveDodgeFactor) : 2;
                        Rng = !SrcNpc ? Rng / (Dodge * 3) : Rng / Dodge;
                    }
                }
                else Rng /= 8;
            }
            else Rng /= 2;
        }
        if (Rng > 0x50) Rng = 0x50;
        const int32 Xo = RandRng(-Rng, Rng), Yo = RandRng(-Rng, Rng);
        Rng /= 3; if (Rng > 0x18) Rng = 0x18;
        const int32 Zo = RandRng(-Rng, Rng);
        DestPt += FIntVector(Xo, Yo, Zo);
        DestPt.Z = FMath::Clamp(DestPt.Z, 0, 0xfa);
    }
    const float Travel = (float)MaxDistXYZ(DestPt, NextPt);
    const float Speed = (Ft ? Ft->CellsPerRound : 1) * 32.f;
    float Rounds = Travel / Speed; if (Rounds < 1) Rounds = 1;
    XStep = (DestPt.X - Start.X) / Rounds; YStep = (DestPt.Y - Start.Y) / Rounds; ZStep = (DestPt.Z - Start.Z) / Rounds;
    if (FireType == 2 || FireType == 0xd) { DestPt.X += (int32)(Travel / 5); DestPt.Y += (int32)(Travel / 5); DestPt.Z += (int32)(Travel / 5); }
}
FSuperSpriteProcess::~FSuperSpriteProcess() { if (FCruItem* It = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr) GCruWorld->DestroyItem(*It); }

static int32 Sign(int32 X) { return X < 0 ? -1 : (X == 0 ? 0 : 1); }
static int32 SignF(float X) { return X < 0 ? -1 : (X == 0 ? 0 : 1); }

void FSuperSpriteProcess::Run()
{
    const int32 Cs = Cru::MAP_CHUNK_SIZE;
    const FCruFireType* Ft = CruGetFireType(FireType);
    if (!Ft || !GCruWorld->IsChunkFast(NextPt.X / Cs, NextPt.Y / Cs)) { DestroyItemOrTerminate(); return; }
    NowPt = NextPt;
    FIntVector NewPt = StartPt;
    if (!bStartedAsFiretype9)
    {
        NewPt.X += (int32)(Counter * XStep); NewPt.Y += (int32)(Counter * YStep); NewPt.Z += (int32)(Counter * ZStep);
    }
    else
    {
        int32 TargetZ = 0;
        if (Counter < Ft->RoundDuration)
        {
            if (!bExpired)
            {
                int32 Dir8 = DIR_CURRENT;
                if (Target == 0) TargetZ = NowPt.Z;
                else if (const FCruItem* T = GCruWorld->GetItem(Target))
                {
                    const FIntVector Ptc = GCruWorld->GetCentre(*T);
                    TargetZ = Ptc.Z + 8;
                    Dir8 = CruDirGetWorldDir(T->Y - NowPt.Y, T->X - NowPt.X, false);
                }
                if (Dir8 != DIR_CURRENT)
                {
                    if (Sign(CruDirXFactor(Dir8)) != SignF(XStep)) XStep *= 2;
                    if (Sign(CruDirYFactor(Dir8)) != SignF(YStep)) YStep *= 2;
                }
            }
        }
        else bExpired = true;
        if (!bExpired) { if (NowPt.Z < TargetZ) ZStep++; else if (TargetZ < NowPt.Z) ZStep--; }
        else ZStep--;
        XStep = FMath::Clamp(XStep, -32.f, 32.f); YStep = FMath::Clamp(YStep, -32.f, 32.f); ZStep = FMath::Clamp(ZStep, -16.f, 16.f);
        NewPt.X += (int32)(Counter * XStep); NewPt.Y += (int32)(Counter * YStep); NewPt.Z += (int32)(Counter * ZStep);
        if (!bExpired && (NowPt.X != NewPt.X || NowPt.Y != NewPt.Y))
        {
            const int32 Dir = CruDirGetWorldDir(NowPt.Y - NewPt.Y, NowPt.X - NewPt.X, false);
            if (FCruItem* It = GCruWorld->GetItem(ItemNum ? ItemNum : SpriteNo)) GCruWorld->SetFrame(*It, Dir + 0x11);
        }
    }
    Pt3 = NewPt; Pt3.Z = FMath::Clamp(Pt3.Z, 0, 0xfa);
    ++Counter;
    if (!SpriteNo && Counter > 1)
    {
        if (FCruItem* Sprite = GCruWorld->CreateItem(Shape, Frame, 0, Cru::FLG_DISPOSABLE, 0, 0, Cru::EXT_SPRITE, true))
        {
            SpriteNo = Sprite->Id;
            GCruWorld->Move(*Sprite, NowPt.X, NowPt.Y, NowPt.Z);
        }
    }
    if (Pt3.Z > 0 && Pt3.Z < 0xfa)
    {
        const int32 Duration = Ft->RoundDuration + 25;
        if (Counter < Duration)
        {
            if (!GCruWorld->IsChunkFast(NowPt.X / Cs, NowPt.Y / Cs)) { DestroyItemOrTerminate(); return; }
            if (AreaSearch())
            {
                AdvanceFrame();
                WaitFor(GCruKernel->AddProcess(new FDelayProcess(FireType == 9 ? 3 : 2)));
                return;
            }
            if (Item0x77 && FireType == 5)
                if (const FCruItem* Hit = GCruWorld->GetItem(Item0x77))
                    if (GCruWorld->Info(*Hit).IsRoof()) { MakeBulletSplash(Pt3); Terminate(); return; }
        }
    }
    if (Source && Item0x77 == Source && Counter < 5)
    {
        // original-game hack to avoid hitting the source item
        if (FCruItem* Src = GCruWorld->GetItem(Source))
        {
            GCruWorld->MoveToEtherealVoid(*Src);
            Run();
            GCruWorld->ReturnFromEtherealVoid(*Src);
            return;
        }
    }
    HitAndFinish();
}

void FSuperSpriteProcess::MakeBulletSplash(const FIntVector& Pt)
{
    const FCruFireType* Ft = CruGetFireType(FireType);
    if (!Ft) return;
    if (Ft->Range) CruCombat::ApplySplashDamageAround(*Ft, Pt, Damage, 1, Item0x77, Source);
    CruCombat::MakeBulletSplashShapeAndPlaySound(*Ft, Pt.X, Pt.Y, Pt.Z);
}

static bool PointOutOfMap(const FIntVector& P, int32 MaxXY) { return P.X < 0 || P.Y < 0 || P.Z < 0 || P.X > MaxXY || P.Y > MaxXY || P.Z > 255; }

void FSuperSpriteProcess::HitAndFinish()
{
    FIntVector Pt = NowPt;
    const int32 Xs = Pt3.X - NowPt.X, Ys = Pt3.Y - NowPt.Y, Zs = Pt3.Z - NowPt.Z;
    FIntVector Start(NowPt.X, NowPt.Y, NowPt.Z + 1), End(Pt3.X, Pt3.Y, Pt3.Z + 1);
    const int32 Dims[3] = { 1, 1, 1 };
    bool bCollision = !(Xs || Ys || Zs);
    TArray<FCruWorld::FSweepItem> Hits;
    const int32 MapMax = Cru::MAP_CHUNK_SIZE * Cru::MAP_NUM_CHUNKS;
    while (!bCollision)
    {
        bCollision = GCruWorld->SweepTest(Start, End, Dims, Cru::SI_SOLID, Source, true, &Hits);
        if (bCollision) break;
        Start += FIntVector(Xs, Ys, Zs); End += FIntVector(Xs, Ys, Zs);
        if (PointOutOfMap(Start, MapMax) || PointOutOfMap(End, MapMax)) break;
    }
    if (bCollision && Hits.Num()) { Item0x77 = Hits[0].Item; Pt = Hits[0].GetInterpolatedCoords(Start, End); }
    if (FCruItem* It = GCruWorld->GetItem(Item0x77))
    {
        const FCruShapeInfo& Si = GCruWorld->Info(*It);
        if (Si.X > 2 && Si.Y > 2 && Si.Z > 2)
        {
            const int32 Ix = (Si.X - 2) * 16, Iy = (Si.Y - 2) * 16;
            if (Pt.X < It->X - Ix) Pt.X = It->X - Ix;
            if (Pt.Y < It->Y - Iy) Pt.Y = It->Y - Iy;
        }
        const int32 Dir = CruDirGetWorldDir(It->Y - NowPt.Y, It->X - NowPt.X, false);
        UE_LOG(LogCru, Display, TEXT("combat: projectile from %d (type %d) hits item %d shape %d at (%d,%d,%d) for %d (bullet at %d,%d,%d)"), Source, FireType, It->Id, It->Shape, It->X, It->Y, It->Z, Damage, Pt.X, Pt.Y, Pt.Z);
        CruCombat::ReceiveHit(*It, ItemNum, Dir, Damage, FireType);
    }
    MakeBulletSplash(Pt);
    DestroyItemOrTerminate();
}

void FSuperSpriteProcess::Terminate()
{
    if (SpriteNo) { if (FCruItem* S = GCruWorld->GetItem(SpriteNo)) GCruWorld->DestroyItem(*S); SpriteNo = 0; }
    FProcess::Terminate();
}
void FSuperSpriteProcess::DestroyItemOrTerminate()
{
    if (ItemNum) { if (FCruItem* It = GCruWorld->GetItem(ItemNum)) GCruWorld->DestroyItem(*It); ItemNum = 0; }
    else Terminate();
}
void FSuperSpriteProcess::AdvanceFrame()
{
    NextPt = Pt3;
    if (FCruItem* It = GCruWorld->GetItem(ItemNum)) GCruWorld->Move(*It, Pt3.X, Pt3.Y, Pt3.Z);
    if (SpriteNo)
        if (FCruItem* S = GCruWorld->GetItem(SpriteNo))
        {
            GCruWorld->Move(*S, NextPt.X, NextPt.Y, NextPt.Z);
            uint32 F = S->Frame + 1;
            if (FireType == 0xe) { if (F > 0x4b) F = 0x47; GCruWorld->SetFrame(*S, F); }
        }
    if (FireType == 3)
    {
        if (Pt5.X != -1) GCruKernel->AddProcess(new FSpriteProcess(0x426, 0, 9, 1, 3, Pt5.X, Pt5.Y, Pt5.Z));
        Pt5 = NextPt;
    }
}
bool FSuperSpriteProcess::AreaSearch()
{
    FIntVector Start(NowPt.X, NowPt.Y, NowPt.Z + 1), End(Pt3.X, Pt3.Y, Pt3.Z + 1);
    const int32 Dims[3] = { 1, 1, 1 };
    if (const FCruItem* It = GCruWorld->GetItem(ItemNum)) Start = FIntVector(It->X, It->Y, It->Z);
    TArray<FCruWorld::FSweepItem> Hits;
    GCruWorld->SweepTest(Start, End, Dims, Cru::SI_SOLID, Source, true, &Hits);
    if (Hits.Num() > 0) Item0x77 = Hits[0].Item;
    return Hits.Num() == 0;
}

// ---------------------------------------------------------------- firing
FCruItem* CruCombat::FindBestTargetItem(int32 X, int32 Y, int32 Z, int32 Dir, bool b16)
{
    bool bBestNpc = false, bBestOccl = false; FCruItem* Best = nullptr; int32 BestDist = 0xffff;
    const ObjId Controlled = GCruWorld->ControlledNpc;
    for (const auto& P : GCruWorld->AllItems())
    {
        FCruItem* It = P.Value;
        if (!It || It->Id == Controlled || !(It->Flags & Cru::FLG_FASTAREA) || (It->Flags & Cru::FLG_BROKEN)) continue;
        const FCruShapeInfo& Si = GCruWorld->Info(*It);
        if (!Si.IsTargetable() && !It->bActor) continue;
        const bool bOccl = (Si.Flags & Cru::SI_OCCL) != 0;
        if (CruDirGetWorldDir(It->Y - Y, It->X - X, b16) != Dir) continue;
        if ((bBestOccl && !bOccl) || (bBestNpc && !It->bActor) || !GCruEngine->IsOnScreen(*It)) continue;
        const int32 Dist = FMath::Max3(FMath::Abs(X - It->X), FMath::Abs(Y - It->Y), FMath::Abs(Z - It->Z));
        if (Dist < BestDist) { Best = It; BestDist = Dist; bBestOccl = bOccl; bBestNpc = It->bActor; }
    }
    return Best;
}

int32 CruCombat::GetTargetZRelativeToAttackerZ(const FCruItem& T, int32 OtherZ)
{
    const FCruShapeInfo& Si = GCruWorld->Info(T);
    const int32 Tsz = Si.Z;
    int32 Tz = T.Z + Tsz * 8;
    if (Tsz < 3) { if (Tsz) Tz -= 8; }
    else
    {
        const int32 TargetZ = Tz;
        Tz -= 16;
        if (OtherZ - TargetZ < -0x2f) Tz += 8;
        else if (OtherZ - TargetZ > 0x2f) { if (Tsz == 6) Tz -= 16; else if (Tsz >= 7) Tz -= 24; }
    }
    return Tz;
}

ProcId CruCombat::FireWeapon(FCruItem& Src, int32 X, int32 Y, int32 Z, int32 Dir, int32 FireType, bool bFindTarget)
{
    const FIntVector Pt(Src.X + X, Src.Y + Y, Src.Z + Z);
    const FCruFireType* Ft = CruGetFireType(FireType);
    if (!Ft) return 0;
    int32 Damage = Ft->GetRandomDamage();
    const FCruWorld::FPositionInfo Info = GCruWorld->GetPositionInfo(Pt.X, Pt.Y, Pt.Z, BULLET_SPLASH_SHAPE, Src.Id);
    if (!Info.bValid && Info.Blocker)
    {
        if (FCruItem* Block = GCruWorld->GetItem(Info.Blocker->Id))
        {
            const FIntVector Bp(Block->X, Block->Y, Block->Z);
            const int32 DamageDir = CruDirGetWorldDir(Bp.Y - Pt.Y, Bp.X - Pt.X, false);
            ReceiveHit(*Block, Src.Id, DamageDir, Damage, FireType);
            if (Ft->Range) ApplySplashDamageAround(*Ft, Bp, Ft->GetRandomDamage(), 1, Block->Id, Src.Id);
            if (Ft->bNearSprite) MakeBulletSplashShapeAndPlaySound(*Ft, Pt.X, Pt.Y, Pt.Z);
        }
        return 0;
    }
    int32 SpriteFrame = 0;
    switch (FireType)
    {
    case 3: case 9: case 10: SpriteFrame = Dir + 0x11; break;
    case 5: SpriteFrame = Dir + 1; break;
    case 6: SpriteFrame = 0x46; break;
    case 0xe: SpriteFrame = 0x47 + RandRng(0, 4); break;
    case 0xf: SpriteFrame = 0x4c; break;
    default: break;
    }
    bool b16 = false;
    if (Src.bActor && Src.LastAnim >= 0) if (const FCruAnimAction* Act = GCruWorld->AnimDat.Get(Src.Shape, Src.LastAnim)) b16 = Act->DirCount == 16;
    FCruItem* Target = nullptr;
    const FCruItem* Controlled = GetControlledActor();
    if (bFindTarget)
    {
        if (&Src != Controlled) Target = GetControlledActor();
        else Target = FindBestTargetItem(Pt.X, Pt.Y, Pt.Z - Z, Dir, b16);
    }
    FIntVector T(-1, 0, 0);
    if (Target) { T = GCruWorld->GetCentre(*Target); T.Z = GetTargetZRelativeToAttackerZ(*Target, Src.Z); }
    ProcId Last = 0;
    for (int32 i = 0; i < Ft->NumShots; ++i)
    {
        FIntVector Ss;
        if (T.X != -1) { Ss = T; bFindTarget = true; }
        else if (&Src == Controlled && GCruEngine && GCruEngine->bAimValid) Ss = FIntVector(GCruEngine->AimPoint.X, GCruEngine->AimPoint.Y, Pt.Z);   // shoot toward the crosshair
        else Ss = FIntVector(Pt.X + CruDirXFactor(Dir) * 0x500, Pt.Y + CruDirYFactor(Dir) * 0x500, Pt.Z);
        Last = GCruKernel->AddProcess(new FSuperSpriteProcess(BULLET_SPLASH_SHAPE, SpriteFrame, Pt, Ss, FireType, Damage, Src.Id, Target ? Target->Id : 0, bFindTarget));
        if (GCruCombatDebug) UE_LOG(LogCru, Display, TEXT("combat: %d fires type %d from (%d,%d,%d) toward (%d,%d,%d) target %d%s"), Src.Id, FireType, Pt.X, Pt.Y, Pt.Z, Ss.X, Ss.Y, Ss.Z, Target ? Target->Id : 0, (&Src == Controlled && GCruEngine && GCruEngine->bAimValid && !Target) ? TEXT(" (mouse aim)") : TEXT(""));
    }
    UE_LOG(LogCru, Verbose, TEXT("combat: %d fires type %d dir %d at (%d,%d,%d) target %d"), Src.Id, FireType, Dir, Pt.X, Pt.Y, Pt.Z, Target ? Target->Id : 0);
    return Last;
}

uint16 CruCombat::FireDistance(const FCruItem& Src, const FCruItem& Other, int32 Dir, int32 Xoff, int32 Yoff, int32 Zoff)
{
    int32 Xoff2 = 0, Yoff2 = 0, Zoff2 = 0; bool bOtherOffsets = false;
    if (Src.bActor)
    {
        const bool bKneeling = (Src.ActorFlags & ACT_KNEELING) != 0;
        bool bSmall = true;
        const bool bMain = Src.Id == Cru::kMainActorId;
        if (const FCruItem* W = GCruWorld->GetItem(Src.ActiveWeapon)) if (const FCruWeaponInfo* Wi = CruGetWeaponInfo(W->Shape)) bSmall = Wi->bSmall;
        uint32 Anim;
        if (bKneeling) Anim = bSmall ? 42 : 43; else Anim = (bSmall || !bMain) ? FireSmall : FireLarge;
        bool bFirst = false;
        if (const FCruAnimAction* Act = GCruWorld->AnimDat.Get(Src.Shape, Anim))
            for (uint32 i = 0; i < Act->Size; ++i)
            {
                const FCruAnimFrame& F = Act->GetFrame(Dir, i);
                if (!F.IsCruAttack()) continue;
                if (!bFirst) { Xoff = F.CruAttackX(); Yoff = F.CruAttackY(); Zoff = F.CruAttackZ(); bFirst = true; }
                else { Xoff2 = F.CruAttackX(); Yoff2 = F.CruAttackY(); Zoff2 = F.CruAttackZ(); bOtherOffsets = true; break; }
            }
    }
    int32 Dist = 0;
    for (int32 i = 0; i < (bOtherOffsets ? 2 : 1) && Dist == 0; ++i)
    {
        const int32 Cx = Src.X + (i == 0 ? Xoff : Xoff2), Cy = Src.Y + (i == 0 ? Yoff : Yoff2), Cz = Src.Z + (i == 0 ? Zoff : Zoff2);
        const FCruWorld::FPositionInfo Info = GCruWorld->GetPositionInfo(Cx, Cy, Cz, BULLET_SPLASH_SHAPE, Src.Id);
        if (!Info.bValid && Info.Blocker)
        {
            if (Info.Blocker->Id == Other.Id) Dist = FMath::Max(FMath::Abs(Src.X - Other.X), FMath::Abs(Src.Y - Other.Y));
        }
        else
        {
            FIntVector Oc = GCruWorld->GetCentre(Other); Oc.Z = GetTargetZRelativeToAttackerZ(Other, Src.Z);
            const FIntVector Start(Cx, Cy, Cz);
            const int32 Dims[3] = { 2, 2, 2 };
            TArray<FCruWorld::FSweepItem> Hits;
            GCruWorld->SweepTest(Start, Oc, Dims, Cru::SI_SOLID, Src.Id, true, &Hits);
            for (const FCruWorld::FSweepItem& H : Hits)
            {
                if (H.Item == Src.Id || H.bTouching) continue;
                if (H.Item != Other.Id) break;
                const FIntVector Out = H.GetInterpolatedCoords(Start, Oc);
                Dist = FMath::Max(FMath::Abs(Src.X - Out.X), FMath::Abs(Src.Y - Out.Y));
                break;
            }
        }
    }
    if (!Dist) return 0;
    return Dist < 32 ? 1 : Dist / 32;
}

int32 CruCombat::GetRangeIfVisible(const FCruItem& A, const FCruItem& B)
{
    const FIntVector Start = GCruWorld->GetCentre(A), End = GCruWorld->GetCentre(B);
    const int32 Dims[3] = { 1, 1, 1 };
    TArray<FCruWorld::FSweepItem> Hits;
    GCruWorld->SweepTest(Start, End, Dims, GCruWorld->Info(A).Flags, A.Id, true, &Hits);
    for (const FCruWorld::FSweepItem& H : Hits)
        if (H.bBlocking && H.Item != A.Id && H.Item != B.Id) return 0;
    return MaxDistXYZ(Start, End);
}

// ---------------------------------------------------------------- hits
bool CruCombat::IsRobot(const FCruItem& It)
{
    const uint32 S = It.Shape;
    return S == 0x4c8 || S == 0x338 || S == 0x45d || S == 0x2cb || S == 0x4e6 || S == 899 || S == 0x385;
}

int32 CruCombat::ScaleReceivedDamage(const FCruItem& It, int32 Damage, uint16 Type)
{
    const int32 Difficulty = GCruWorld->GameDifficulty;
    if (!It.bActor || (It.Id != Cru::kMainActorId && It.Id != GCruWorld->ControlledNpc))
    {
        if (Difficulty == 1) Damage *= 5; else if (Difficulty == 2) Damage *= 3;
    }
    else
    {
        if (Difficulty == 1) Damage /= 5; else if (Difficulty == 2) Damage /= 3;
    }
    if (IsRobot(It) && (Type == 1 || Type == 2 || Type == 0xb || Type == 0xd)) Damage /= 3;
    return FMath::Clamp(Damage, 1, 0xfa);
}

static bool ApplyDamageInfo(const FCruDamageInfo& Di, FCruItem& It, int32 Points)
{
    if (It.Flags & Cru::FLG_GUMP_OPEN) return false;
    if (Points < It.DamagePoints) { It.DamagePoints -= Points; return false; }
    It.DamagePoints = 0;
    GCruWorld->SetFlags(It, It.Flags | Cru::FLG_GUMP_OPEN | Cru::FLG_BROKEN);
    const uint16 Q = It.Quality; const FIntVector Pt(It.X, It.Y, It.Z); const int32 MapNum = It.MapNum; const ObjId Id = It.Id;
    bool bAlive = true;
    if (Di.Explodes())
    {
        CruCombat::Explode(It, Di.ExplosionType(), Di.ExplodeDestroysItem(), Di.ExplodeWithDamage());
        if (Di.ExplodeDestroysItem()) bAlive = false;
    }
    if (Di.Sound) PlaySfx(bAlive ? Id : 0, Di.Sound);
    if (Di.ReplaceItem())
    {
        if (FCruItem* N = GCruWorld->CreateItem(Di.ReplacementShape(), Di.ReplacementFrame(), Q, 0, 0, MapNum, 0, true)) GCruWorld->Move(*N, Pt.X, Pt.Y, Pt.Z);
        if (bAlive) GCruWorld->DestroyItem(It);
    }
    else if (!Di.ExplodeDestroysItem())
    {
        if (Di.FrameDataIsAbsolute())
        {
            int32 N = 1; if (Di.Data[1]) ++N; if (Di.Data[2]) ++N;
            GCruWorld->SetFrame(It, Di.Data[RandRng(0, N - 1)]);
        }
        else
        {
            int32 N = 0; for (int32 i = 0; i < 3; ++i) if (Di.Data[i]) ++N;
            if (!N) GCruWorld->DestroyItem(It);
            else GCruWorld->SetFrame(It, It.Frame + Di.Data[RandRng(0, N - 1)]);
        }
    }
    return true;
}

static void ReceiveHitItem(FCruItem& It, ObjId Other, int32 Dir, int32 Damage, uint16 Type)
{
    Damage = CruCombat::ScaleReceivedDamage(It, Damage, Type);
    const FCruShapeInfo& Si = GCruWorld->Info(It);
    const FCruDamageInfo* Di = CruCombat::GetDamageInfo(It.Shape);
    const uint16 Force = (Type << 8) | (Damage & 0xff);
    const uint8 Args[4] = { 0x00, 0x40, (uint8)Force, (uint8)(Force >> 8) };   // hitter 0x4000, (type << 8) | damage
    GCruWorld->CallUsecodeEvent(It, Cru::EV_GOTHIT, Args, 4);
    if (Di)
    {
        if (ApplyDamageInfo(*Di, It, Damage))
        {
            UE_LOG(LogCru, Display, TEXT("combat: item %d shape %d destroyed (damage %d type %d)"), It.Id, It.Shape, Damage, Type);
            GCruKernel->KillProcesses(It.Id, FKernel::PROC_TYPE_ALL, true);
        }
    }
    if ((Si.Flags & Cru::SI_FIXED) || Si.Weight == 0 || (Type != 3 && Type != 4)) return;
    if (!GCruWorld->GetItem(It.Id)) return;   // destroyed by the hit
    static const int32 HurlX[] = { 0, +1, +2, +2, +2, +2, +2, +1, 0, -1, -2, -2, -2, -2, -2, -1 };
    static const int32 HurlY[] = { -2, -2, -2, -1, 0, +1, +2, +2, +2, +2, +2, +1, 0, -1, -2, -2 };
    const int32 D = Dir & 15;
    CruCombat::Hurl(It, -RandRng(10, 24) * HurlX[D], -RandRng(10, 24) * HurlY[D], 0, 2);
}

// MainActor::receiveShieldHit: the avatar's energy shield absorbs hits of the types its mask covers, for energy
int32 CruCombat::ReceiveShieldHit(FCruItem& A, int32 Damage, uint16 DamageType)
{
    if (!GCruEngine) return Damage;
    uint16 ShieldType = GCruEngine->ShieldType;
    if (ShieldType == 3) ShieldType = 4;
    const FCruFireType* Ft = CruGetFireType(DamageType);
    const int32 Energy = A.Mana;
    if (!(ShieldType && Ft && Ft->ShieldCost && (Ft->ShieldMask & ShieldType) && (int32)Ft->ShieldCost <= Energy)) return Damage;
    A.Mana = Energy - Ft->ShieldCost;
    PlaySfx(A.Id, 0x48);
    if (!GCruEngine->ShieldSpriteProc || !GCruKernel->GetProcess(GCruEngine->ShieldSpriteProc))
    {
        int32 Shape, First, Last; bool bRemember; FIntVector Pt;
        switch (ShieldType)
        {
        case 1: Shape = 0x5a9; First = 7; Last = 0xd; bRemember = false; Pt = GCruWorld->GetCentre(A); break;
        case 2: Shape = 0x5a9; First = 0; Last = 6; bRemember = false; Pt = GCruWorld->GetCentre(A); break;
        default: Shape = 0x52b; First = 0; Last = 8; bRemember = true; Pt = FIntVector(A.X + 0x10, A.Y + 0x18, A.Z); break;
        }
        FProcess* P = new FSpriteProcess(Shape, First, Last, 1, 4, Pt.X, Pt.Y, Pt.Z);
        const ProcId Pid = GCruKernel->AddProcess(P);
        GCruEngine->ShieldSpriteProc = bRemember ? Pid : 0;
    }
    return 0;
}

static void ReceiveHitActor(FCruItem& A, ObjId Other, int32 Dir, int32 Damage, uint16 DamageType)
{
    FCruItem* Attacker = GCruWorld->GetItem(Other); if (Attacker && !Attacker->bActor) Attacker = nullptr;
    const uint32 Shape = A.Shape;
    if (A.bDead) return;
    A.LastTickWasHit = GCruKernel->GetTickNum();
    FCruItem* Controlled = GetControlledActor();
    if (Shape != 1 && &A != Controlled)
    {
        if (!CruActor::IsInCombat(A))
        {
            CruActor::SetActivity(A, A.DefaultActivity[2]);
            if (!CruActor::IsInCombat(A))
            {
                CruCombat::SetInCombatCru(A, 5);
                if (FAttackProcess* Ap = CruCombat::GetAttackProcess(A)) if (Controlled) Ap->SetTarget(Controlled->Id);
            }
        }
        else
        {
            if (A.CurrentActivity == 8) CruActor::SetActivity(A, 5);
            CruCombat::SetInCombatCru(A, 5);
            if (FAttackProcess* Ap = CruCombat::GetAttackProcess(A)) if (Controlled) Ap->SetTarget(Controlled->Id);
        }
        if (Attacker && Attacker == Controlled && GCruKernel->FindProcess(A.Id, PATHFINDER_PROC_TYPE))
        {
            const int32 MaxDiff = MaxDistXYZ(FIntVector(A.X, A.Y, A.Z), FIntVector(Attacker->X, Attacker->Y, Attacker->Z));
            if (MaxDiff < 641 && GCruEngine->IsOnScreen(A)) GCruKernel->KillProcesses(A.Id, PATHFINDER_PROC_TYPE, true);
        }
    }
    else Damage = CruCombat::ReceiveShieldHit(A, Damage, DamageType);
    if (A.ActorFlags & (ACT_IMMORTAL | ACT_INVINCIBLE)) Damage = 0;
    if (Damage > A.Hp) Damage = A.Hp;
    A.Hp -= Damage;
    UE_LOG(LogCru, Display, TEXT("combat: actor %d shape %d hit by %d for %d (type %d), hp %d"), A.Id, Shape, Other, Damage, DamageType, A.Hp);
    if (A.Hp == 0)
    {
        CruCombat::Die(A, DamageType, Damage, Dir);
        if (A.Id == Cru::kMainActorId && GCruEngine) GCruEngine->OnAvatarDied();
    }
    else if (Damage)
    {
        if (!CruCombat::IsRobot(A)) PlaySfx(Other, (A.ExtFlags & Cru::EXT_FEMALE) ? 0xd8 : 0x8f);
        if (DamageType == 0xf || DamageType == 7)
        {
            if (Shape == 1)
            {
                GCruKernel->KillProcesses(A.Id, PATHFINDER_PROC_TYPE, true);
                CruActor::DoAnim(A, 0x37, DIR_CURRENT);
            }
            else if (Shape == 0x4e6 || Shape == 0x338 || Shape == 0x385 || Shape == 899)
            {
                if (RandRng(0, 2))
                {
                    FProcess* Attack = GCruKernel->FindProcess(A.Id, ATTACK_PROC_TYPE);
                    const int32 Stun = RandRng(8, 17) * 60;
                    if (Attack && Stun) Attack->WaitFor(GCruKernel->AddProcess(new FDelayProcess(Stun)));
                }
            }
        }
    }
}

void CruCombat::ReceiveHit(FCruItem& It, ObjId Other, int32 Dir, int32 Damage, uint16 Type)
{
    if (It.bActor) ReceiveHitActor(It, Other, Dir, Damage, Type);
    else ReceiveHitItem(It, Other, Dir, Damage, Type);
}

void CruCombat::Explode(FCruItem& It, int32 ExplosionType, bool bDestroyItem, bool bCauseDamage)
{
    int32 Divisor = ExplosionType + 1;
    if (Divisor == 1) Divisor = 3; else if (Divisor == 3) Divisor = 1;
    GCruWorld->SetFlags(It, It.Flags | Cru::FLG_BROKEN);
    const FIntVector C = GCruWorld->GetCentre(It);
    static const int32 ExpShapes[] = { 0x31C, 0x31F, 0x326, 0x320, 0x321, 0x324, 0x323, 0x325 };
    int32 SpriteNo;
    switch (ExplosionType)
    {
    case 0: SpriteNo = ExpShapes[RandRng(0, 1)]; break;
    case 1: SpriteNo = ExpShapes[2 + RandRng(0, 2)]; break;
    default: SpriteNo = ExpShapes[5 + RandRng(0, 2)]; break;
    }
    GCruKernel->AddProcess(new FSpriteProcess(SpriteNo, 0, 39, 1, 1, It.X, It.Y, C.Z));
    GCruWorld->SfxQueue.Add({ It.Id, -1, true, false });
    PlaySfx(0, FMath::RandBool() ? 28 : 108);
    const FIntVector Pt(It.X, It.Y, It.Z);
    UE_LOG(LogCru, Display, TEXT("combat: item %d shape %d explodes (type %d, destroy %d)"), It.Id, It.Shape, ExplosionType, bDestroyItem);
    const ObjId Id = It.Id;
    if (bDestroyItem) GCruWorld->DestroyItem(It);
    if (!bCauseDamage) return;
    if (const FCruFireType* Ft = CruGetFireType(4)) ApplySplashDamageAround(*Ft, Pt, Ft->GetRandomDamage() / Divisor, Divisor, Id, Id);
}

// ---------------------------------------------------------------- actors
void CruCombat::SetToStartOfAnim(FCruItem& A, uint32 Anim)
{
    FAnimationTracker T;
    if (!T.Init(A, Anim, A.Dir)) return;
    if (const FCruAnimFrame* F = T.GetAnimFrame())
    {
        GCruWorld->SetFrame(A, F->Frame);
        const uint16 NewFlags = F->CruFlipped() ? (A.Flags | Cru::FLG_FLIPPED) : (A.Flags & ~Cru::FLG_FLIPPED);
        if (NewFlags != A.Flags) GCruWorld->SetFlags(A, NewFlags);
        A.LastAnim = Anim;
    }
}

FAttackProcess* CruCombat::GetAttackProcess(const FCruItem& A) { return static_cast<FAttackProcess*>(GCruKernel->FindProcess(A.Id, ATTACK_PROC_TYPE)); }

void CruCombat::SetInCombatCru(FCruItem& A, int32 Activity)
{
    if (A.ActorFlags & ACT_INCOMBAT) return;
    A.ActorFlags |= ACT_INCOMBAT;
    if (A.Id == GCruWorld->ControlledNpc) return;
    FAttackProcess* Ap = new FAttackProcess(A);
    GCruKernel->AddProcess(Ap);
    if (A.LastActivity == 8) Ap->SetField97();
    if (Activity == 0xc) Ap->SetTimer3();
    ProcId AnimProc;
    if (Activity == 9 || Activity == 0xb) { Ap->SetIsActivity9OrB(); AnimProc = CruActor::DoAnim(A, ReadySmall, DIR_CURRENT); }
    else AnimProc = CruActor::DoAnim(A, Stand, DIR_CURRENT);
    if (AnimProc) Ap->WaitFor(AnimProc);
    if (Activity == 0xa || Activity == 0xb) Ap->SetIsActivityAOrB();
    UE_LOG(LogCru, Display, TEXT("combat: actor %d shape %d enters combat (activity %d, tactic %d)"), A.Id, A.Shape, Activity, A.CombatTactic);
}

void CruCombat::ClearInCombat(FCruItem& A)
{
    if (!(A.ActorFlags & ACT_INCOMBAT)) return;
    if (FAttackProcess* Ap = GetAttackProcess(A)) Ap->Terminate();
    A.ActorFlags &= ~ACT_INCOMBAT;
}

void CruCombat::TookHit(FCruItem& A)
{
    const int32 LastAnim = A.LastAnim;
    const bool bFemale = (A.ExtFlags & Cru::EXT_FEMALE) != 0;
    if (LastAnim == LookLeft || LastAnim == LookRight)
    {
        if (CruActor::CanSeeControlledActor(A, true)) CruActor::SetActivity(A, RandRng(0, 3) != 0 ? 5 : 10);
    }
    else
    {
        const uint32 Shape = A.Shape;
        if (Shape == 0x385 || Shape == 0x4e6)
        {
            Explode(A, 2, false);
            GCruWorld->SetFlags(A, A.Flags & ~(Cru::FLG_IN_NPC_LIST | Cru::FLG_GUMP_OPEN));
        }
        else if (Shape == 0x576 || Shape == 0x596)
        {
            static const int16 FEMALE_SFX[] = { 0xb, 0xa };
            static const int16 MALE_SFX[] = { 0x65, 0x66, 0x67 };
            PlaySfx(A.Id, bFemale ? RandomElem(FEMALE_SFX) : RandomElem(MALE_SFX));
        }
    }
}

ProcId CruCombat::Die(FCruItem& A, uint16 DamageType, uint16 DamagePts, int32 SrcDir)
{
    A.Hp = 0; A.bDead = true;
    A.ActorFlags |= ACT_DEAD; A.ActorFlags &= ~ACT_INCOMBAT;
    GCruWorld->SetFlags(A, A.Flags | Cru::FLG_BROKEN);
    const bool bRobot = IsRobot(A);
    bool bCreatedKoresh = false;
    if (GCruWorld->ControlledNpc == A.Id && A.Id != Cru::kMainActorId) GCruWorld->ControlledNpc = 0;
    ProcId LastAnim = 0;
    GCruKernel->KillProcesses(A.Id, FKernel::PROC_TYPE_ALL, true);
    UE_LOG(LogCru, Display, TEXT("combat: actor %d shape %d dies (damage type %d)"), A.Id, A.Shape, DamageType);

    if (DamageType == 3 || DamageType == 4 || DamageType == 10 || DamageType == 12)
    {
        if (!bRobot)
        {
            const FCruFireType* Ft = CruGetFireType(DamageType);
            int32 Dmg2 = Ft ? Ft->GetRandomDamage() : 0;
            Dmg2 = (DamageType == 3 ? Dmg2 * 3 : Dmg2 * 2) / 5;
            if (Dmg2 <= DamagePts)
            {
                GCruWorld->MoveToEtherealVoid(A);
                const bool bCanCreate = GCruWorld->GetPositionInfo(A.X, A.Y, A.Z, 0x576, A.Id).bValid;
                GCruWorld->ReturnFromEtherealVoid(A);
                if (bCanCreate)
                {
                    bCreatedKoresh = true;
                    int32 RunDir = A.Dir;
                    GCruWorld->SetShape(A, 0x576);
                    SetToStartOfAnim(A, Walk);
                    const int32 NumSteps = RandRng(0, 8);
                    if (RunDir % 2) RunDir = (RunDir + 1) % 16;
                    for (int32 i = 0; i < NumSteps; ++i)
                    {
                        RunDir = CruDirTurnByDelta(RunDir, RandRng(-1, 1), false);
                        LastAnim = CruActor::DoAnimAfter(A, Walk, RunDir, LastAnim);
                    }
                    LastAnim = CruActor::DoAnimAfter(A, FallBackwards, DIR_CURRENT, LastAnim);
                    const int32 NumFalls = RandRng(1, 3);
                    for (int32 i = 0; i < NumFalls; ++i) LastAnim = CruActor::DoAnimAfter(A, FallForwards, DIR_CURRENT, LastAnim);
                    LastAnim = CruActor::DoAnimAfter(A, 23, DIR_CURRENT, LastAnim);
                    TookHit(A);
                }
            }
        }
    }
    else if (DamageType == 6) { if (!bRobot) { GCruWorld->SetShape(A, 0x58f); SetToStartOfAnim(A, Stand); } }
    else if (DamageType == 14) { if (!bRobot) { GCruWorld->SetShape(A, 0x596); SetToStartOfAnim(A, FallBackwards); } }
    else if (DamageType == 15) { if (!bRobot) { GCruWorld->SetShape(A, 0x59c); SetToStartOfAnim(A, FallBackwards); } }
    else if (DamageType == 0x10 || DamageType == 0x12) { if (!bRobot) { GCruWorld->SetShape(A, 0x5d6); SetToStartOfAnim(A, FallBackwards); } }
    else if (DamageType == 0x11) { if (!bRobot) { GCruWorld->SetShape(A, 0x62d); SetToStartOfAnim(A, FallBackwards); } }
    else if (DamageType == 0x14) { if (!bRobot) { GCruWorld->SetShape(A, 0x278); SetToStartOfAnim(A, FallBackwards); } }
    else if (DamageType == 7 && A.Id == Cru::kMainActorId) LastAnim = CruActor::DoAnimAfter(A, 55, DIR_CURRENT, LastAnim);

    if (!bCreatedKoresh)
    {
        bool bFallBackwards = true, bFallRandom = false;
        const uint32 Shape = A.Shape;
        if (Shape != 899 && Shape != 0x58f && Shape != 0x596)
        {
            int32 DirTest[9]; DirTest[0] = A.Dir;
            for (int32 i = 1; i < 9; ++i) DirTest[i] = CruDirTurnByDelta(A.Dir, ((i + 1) / 2) * ((i % 2) ? 1 : -1), false);
            for (int32 i = 0; i < 9; ++i) if (DirTest[i] == SrcDir) { if (i == 8) bFallRandom = true; else bFallBackwards = false; break; }
        }
        else bFallRandom = true;
        if (!CruActor::HasAnim(A, FallForwards)) LastAnim = CruActor::DoAnimAfter(A, FallBackwards, DIR_CURRENT, LastAnim);
        else
        {
            if (bFallRandom) bFallBackwards = FMath::RandBool();
            LastAnim = CruActor::DoAnimAfter(A, bFallBackwards ? FallBackwards : FallForwards, DIR_CURRENT, LastAnim);
        }
        if (!bRobot)
        {
            static const int16 FADING_SCREAM_SFX[] = { 0xD9, 0xDA };
            static const int16 MALE_DEATH_SFX[] = { 0x88, 0x8C, 0x8F };
            static const int16 FEMALE_DEATH_SFX[] = { 0xD8, 0x10 };
            int32 Sfx;
            if (DamageType == 0xf) Sfx = RandomElem(FADING_SCREAM_SFX);
            else Sfx = (A.ExtFlags & Cru::EXT_FEMALE) ? RandomElem(FEMALE_DEATH_SFX) : RandomElem(MALE_DEATH_SFX);
            PlaySfx(A.Id, Sfx);
        }
    }
    return LastAnim;
}

void CruCombat::DoFireWeaponCru(FCruItem& A, const FCruAnimFrame& F)
{
    if (!F.IsCruAttack()) return;
    const FCruItem* W = GCruWorld->GetItem(A.ActiveWeapon);
    const FCruWeaponInfo* Wi = W ? CruGetWeaponInfo(W->Shape) : nullptr;
    if (!Wi) return;
    FireWeapon(A, F.CruAttackX(), F.CruAttackY(), F.CruAttackZ(), A.Dir, Wi->DamageType, true);
    PlaySfx(A.Id, Wi->Sound);
}

void CruCombat::CheckForAlertingNPCs()
{
    const uint32 Now = GCruKernel->GetTickNum();
    if (GLastNpcAlertTick && Now - GLastNpcAlertTick < 240) return;
    GLastNpcAlertTick = Now;
    const ObjId Controlled = GCruWorld->ControlledNpc;
    for (ObjId i = 2; i < 256; ++i)
    {
        if (i == Controlled) continue;
        FCruItem* A = GCruWorld->GetItem(i);
        if (!A || !A->bActor || A->bDead || !GCruEngine->IsOnScreen(*A)) continue;
        if (CruActor::IsInCombat(*A)) continue;
        const int32 Cur = A->CurrentActivity, Act2 = A->DefaultActivity[2];
        if (Cur == Act2) { if (Cur == 8) CruActor::SetActivity(*A, 5); }
        else
        {
            int32 Range = 1;
            const uint32 S = A->Shape;
            if (S == 0x2f5 || S == 0x2f6 || S == 0x2f7 || S == 0x595 || S == 0x597)
            {
                Range = 0;
                if (const FCruItem* C = GCruWorld->GetItem(Controlled)) Range = GetRangeIfVisible(*A, *C);
            }
            if (Range) CruActor::SetActivity(*A, Act2);
        }
    }
}

// ---------------------------------------------------------------- attack process
static const int16 REM_SFX_1[] = { 0x15, 0x78, 0x80, 0x83, 0xDC, 0xDD };
static const int16 REM_SFX_2[] = { 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xE7 };
static const int16 REM_SFX_3[] = { 0xFC, 0xFD, 0xFE, 0xC8 };
static const int16 REM_SFX_4[] = { 0xCC, 0xCD, 0xCE, 0xCF };
static const int16 REM_SFX_5[] = { 0xC7, 0xCA, 0xC9 };
static const int16 REM_SFX_6[] = { 0x82, 0x84, 0x85 };
static const int16 REM_SFX_7[] = { 0x9B, 0x9C, 0x9D, 0x9E, 0x9F };
static const int32 MAGIC_DATA_OFF = 33000;
static uint16 GSomeSleepGlobal = 0;
bool GCruCombatDebug = false;
#define ADBG(fmt, ...) do { if (GCruCombatDebug) UE_LOG(LogCru, Display, TEXT("attack %d: " fmt), ItemNum, ##__VA_ARGS__); } while (0)

FAttackProcess::FAttackProcess(FCruItem& A) : FProcess(A.Id, ATTACK_PROC_TYPE)
{
    NpcInitialDir = A.Dir;
    A.bAttackAimFlag = false;
    if (const FCruItem* W = GCruWorld->GetItem(A.ActiveWeapon))
    {
        const uint32 WpnShape = W->Shape, NpcShape = A.Shape;
        const int32 Difficulty = GCruWorld->GameDifficulty;
        if (WpnShape == 0x386 || WpnShape == 0x388 || WpnShape == 0x38e)
        {
            WpnBasedTimeout = 0x3c;
            switch (Difficulty) { case 1: DifficultyBasedTimeout = 0x78; break; case 2: DifficultyBasedTimeout = 0x5a; break; default: DifficultyBasedTimeout = (NpcShape == 0x3ac) ? 0xf : 0x3c; break; }
        }
        else
        {
            WpnBasedTimeout = 0x1e;
            switch (Difficulty) { case 1: DifficultyBasedTimeout = WpnBasedTimeout; break; case 2: DifficultyBasedTimeout = 0x14; break; case 3: DifficultyBasedTimeout = 0xf; break; default: DifficultyBasedTimeout = 0; break; }
        }
    }
    SetTacticNo(A.CombatTactic);
    CruCombat::SetToStartOfAnim(A, Stand);
}

void FAttackProcess::Terminate()
{
    if (FCruItem* A = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr) A->ActorFlags &= ~ACT_INCOMBAT;
    FProcess::Terminate();
}

void FAttackProcess::SetTacticNo(int32 InTactic)
{
    Tactic = (uint16)InTactic;
    TacticDat = CruCombat::GetCombatDat(InTactic);
    if (!TacticDat) UE_LOG(LogCru, Warning, TEXT("combat: no tactic %d"), InTactic);
    SetBlockNo(0);
}
void FAttackProcess::SetBlockNo(int32 InBlock)
{
    Block = (uint16)InBlock;
    if (!TacticDat) return;
    TacticDatStartOffset = TacticDat->Offsets[FMath::Clamp(InBlock, 0, 3)];
    Pos = TacticDatStartOffset;
}
uint16 FAttackProcess::ReadNextWordRaw()
{
    if (!TacticDat || Pos + 2 > TacticDat->Data.Num()) { Pos += 2; return 0; }
    const uint16 V = TacticDat->Data[Pos] | (TacticDat->Data[Pos + 1] << 8); Pos += 2; return V;
}
uint16 FAttackProcess::ReadNextWordWithData() { uint16 D = ReadNextWordRaw(); if (D >= MAGIC_DATA_OFF) D = GetAttackData(D); return D; }
void FAttackProcess::SetAttackData(uint16 Off, uint16 Val) { if (Off >= MAGIC_DATA_OFF && Off < MAGIC_DATA_OFF + UE_ARRAY_COUNT(DataArray) - 1) DataArray[Off - MAGIC_DATA_OFF] = Val; }
uint16 FAttackProcess::GetAttackData(uint16 Off) const { return (Off >= MAGIC_DATA_OFF && Off < MAGIC_DATA_OFF + UE_ARRAY_COUNT(DataArray) - 1) ? DataArray[Off - MAGIC_DATA_OFF] : 0; }
void FAttackProcess::Sleep(int32 Ticks) { Ticks = FMath::Max(Ticks, 2); WaitFor(GCruKernel->AddProcess(new FDelayProcess(Ticks))); }
void FAttackProcess::SetTimer3() { bTimer3set = true; Timer3 = RandRng(0, 9) * 60 + (int32)GCruKernel->GetTickNum(); }
bool FAttackProcess::ReadyForNextSound(uint32 Now) { if (SoundTimestamp == 0 || Now - SoundTimestamp >= SoundDelayTicks) { SoundTimestamp = Now; return true; } return false; }
bool FAttackProcess::CheckTimer2PlusDelayElapsed(int32 Now) { return Now >= Timer2 + (bDoubleDelay ? 120 : 60); }
bool FAttackProcess::Timer4And5Update(int32 Now)
{
    const int32 Delay = bDoubleDelay ? 240 : 120;
    if (Timer4) { Timer5 = Timer4; if (Timer4 + Delay >= Now) return true; }
    Timer4 = 0; bDoubleDelay = false;
    return false;
}
bool FAttackProcess::CheckReady(int32 Now, int32 TargetDir)
{
    if (Timer4And5Update(Now) || bTimer2set) return true;
    const FCruItem* A = GCruWorld->GetItem(ItemNum); const FCruItem* T = GCruWorld->GetItem(Target);
    if (!A || !T) return false;
    return CruCombat::FireDistance(*A, *T, TargetDir, 0, 0, 0) > 0;
}
void FAttackProcess::PathfindToItemInNPCData()
{
    bDoubleDelay = false; bTimer2set = false; bField96 = true;
    FCruItem* A = GCruWorld->GetItem(ItemNum); const FCruItem* T = GCruWorld->GetItem(Target);
    if (!A || !T) return;
    FProcess* Path = new FCruPathfinderProcess(*A, *T, 12, 0x80, false);
    FProcess* Delay = new FDelayProcess(10);
    GCruKernel->AddProcess(Path); GCruKernel->AddProcess(Delay);
    Delay->WaitFor(Path->Pid);
    WaitFor(Delay->Pid);
}
void FAttackProcess::CheckRandomAttackSound(int32 Now, uint32 ShapeNo)
{
    int32 Sound = -1;
    if (!bPlayedStartSound)
    {
        bPlayedStartSound = true;
        if (RandRng(0, 2) == 0)
            switch (ShapeNo)
            {
            case 0x371: Sound = RandomElem(REM_SFX_3); break;
            case 0x1b4: Sound = RandomElem(REM_SFX_5); break;
            case 0x2fd: case 0x319: Sound = RandomElem(REM_SFX_1); break;
            case 900: Sound = RandomElem(REM_SFX_2); break;
            case 0x4d1: case 0x528: Sound = RandomElem(REM_SFX_4); break;
            default: break;
            }
    }
    else if (ReadyForNextSound(Now))
    {
        if (ShapeNo == 0x2df) Sound = RandomElem(REM_SFX_6);
        else if (ShapeNo == 899) Sound = RandomElem(REM_SFX_7);
    }
    if (Sound != -1) { SoundNo = (int16)Sound; PlaySfx(ItemNum, Sound); }
}

void FAttackProcess::Run()
{
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    FCruItem* T = GCruWorld->GetItem(Target); if (T && !T->bActor) T = nullptr;
    if (!A || A->bDead || !TacticDat) { Terminate(); return; }
    if (!(A->Flags & Cru::FLG_FASTAREA)) return;
    if (Tactic == 0) { GenericAttack(); return; }
    if (!T || T->bDead) { UE_LOG(LogCru, Warning, TEXT("combat: attack process for %d with invalid target %d"), ItemNum, Target); Terminate(); return; }
    const int32 CurDir = A->Dir;
    if (Pos >= TacticDat->Data.Num()) { Terminate(); return; }
    const uint8 Opcode = TacticDat->Data[Pos++];
    switch (Opcode)
    {
    case 0x81: ReadNextWordWithData(); return;
    case 0x82: return;
    case 0x84: Target = ReadNextWordWithData(); return;
    case 0x85: CruActor::DoAnim(*A, Walk, DIR_CURRENT); return;
    case 0x86: CruActor::DoAnim(*A, CruAnim::Run, DIR_CURRENT); return;
    case 0x87: CruActor::DoAnim(*A, RetreatSmall, DIR_CURRENT); return;
    case 0x88: CruActor::TurnTowardDir(*A, CruDirTurnByDelta(CurDir, -2, false)); return;
    case 0x89: CruActor::TurnTowardDir(*A, CruDirTurnByDelta(CurDir, 2, false)); return;
    case 0x8a: case 0x8b: if (CruCombat::FireDistance(*A, *T, CurDir, 0, 0, 0)) CruActor::DoAnim(*A, FireSmall, DIR_CURRENT); return;
    case 0x8c: CruActor::DoAnim(*A, Stand, DIR_CURRENT); return;
    case 0x8d: WaitFor(GCruKernel->AddProcess(new FCruPathfinderProcess(*A, FIntVector(A->HomeX, A->HomeY, A->HomeZ), 100, 0x80, true))); return;
    case 0x8e: WaitFor(GCruKernel->AddProcess(new FCruPathfinderProcess(*A, FIntVector(T->X, T->Y, T->Z), 12, 0x80, true))); return;
    case 0x8f: WaitFor(GCruKernel->AddProcess(new FCruPathfinderProcess(*A, FIntVector((T->X + A->X) / 2, (T->Y + A->Y) / 2, (T->Z + A->Z) / 2), 12, 0x80, true))); return;
    case 0x92: CruActor::DoAnim(*A, 42, DIR_CURRENT); return;
    case 0x93:
    {
        int32 Ticks = ReadNextWordWithData();
        if (Ticks == GSomeSleepGlobal) Ticks = RandRng(0x31, 0x45);
        Ticks /= FMath::Max(1, GCruWorld->GameDifficulty);
        Sleep(Ticks); return;
    }
    case 0x94: { const uint16 D = ReadNextWordWithData(); WaitFor(GCruKernel->AddProcess(new FLoiterProcess(*A, D))); return; }
    case 0x95: CruActor::TurnTowardDir(*A, CruActor::DirToItemCentre(*A, *T)); return;
    case 0x96: CruActor::SetActivity(*A, ReadNextWordWithData()); return;
    case 0x97: SetTacticNo(ReadNextWordWithData()); return;
    case 0x98: A->Dir = NpcInitialDir; GCruWorld->MoveToEtherealVoid(*A); GCruWorld->Move(*A, A->HomeX, A->HomeY, A->HomeZ); return;
    case 0x99: Terminate(); return;
    case 0x9a: { const int32 D = MaxDistXYZ(FIntVector(A->X, A->Y, A->Z), FIntVector(T->X, T->Y, T->Z)); const uint16 Off = ReadNextWordWithData(); if (D < 481) Seek(Off); return; }
    case 0x9b: { const int32 D = MaxDistXYZ(FIntVector(A->X, A->Y, A->Z), FIntVector(T->X, T->Y, T->Z)); const uint16 Off = ReadNextWordWithData(); if (D > 160) Seek(Off); return; }
    case 0x9c: { const bool R = CruCombat::FireDistance(*A, *T, CurDir, 0, 0, 0) != 0; const uint16 Off = ReadNextWordWithData(); if (!R) Seek(Off); return; }
    case 0x9d: { const bool R = CruCombat::FireDistance(*A, *T, CurDir, 0, 0, 0) != 0; const uint16 Off = ReadNextWordWithData(); if (R) Seek(Off); return; }
    case 0x9e: { const uint16 MaxVal = ReadNextWordWithData(); const uint16 Off = ReadNextWordWithData(); if (MaxVal != 0 && RandRng(0, MaxVal - 1) != 0) Seek(Off); return; }
    case 0x9f: Field57 = (int16)ReadNextWordWithData(); Field59 = (uint16)Pos; return;
    case 0xa6:
    {
        // pathfind to the wander marker (shape 0x33a) with the given frame and matching quality
        const uint16 TargetFrame = ReadNextWordWithData();
        const uint16 TargetQ = A->UnkByte;
        FUCList List(2); const uint8 Script[] = { 1, '$' };
        GCruWorld->AreaSearch(List, Script, 2, 0x200 * 16, A->X, A->Y);
        for (uint32 i = 0; i < List.GetSize(); ++i)
        {
            const FCruItem* F = GCruWorld->GetItem(List.GetUInt16(i));
            if (!F || F->Shape != 0x33a) continue;
            if (F->Frame == TargetFrame && (TargetQ == 0 || (F->Quality & 0xff) == TargetQ))
            {
                WaitFor(GCruKernel->AddProcess(new FCruPathfinderProcess(*A, *F, 100, 0x80, true)));
                break;
            }
        }
        return;
    }
    case 0xa7: CruActor::TurnTowardDir(*A, 0); return;
    case 0xa8: CruActor::TurnTowardDir(*A, 8); return;
    case 0xa9: CruActor::TurnTowardDir(*A, 4); return;
    case 0xaa: CruActor::TurnTowardDir(*A, 12); return;
    case 0xab: CruActor::TurnTowardDir(*A, 2); return;
    case 0xac: CruActor::TurnTowardDir(*A, 10); return;
    case 0xad: CruActor::TurnTowardDir(*A, 6); return;
    case 0xae: CruActor::TurnTowardDir(*A, 14); return;
    case 0xaf: { const uint16 Next = ReadNextWordWithData(); const uint16 Off = ReadNextWordRaw(); SetAttackData(Off, Next); return; }
    case 0xb0: { const uint16 Off = ReadNextWordRaw(); const uint16 V = GetAttackData(Off); SetAttackData(Opcode, V + ReadNextWordWithData()); return; }
    case 0xb1: { const uint16 Off = ReadNextWordRaw(); const uint16 V = GetAttackData(Off); SetAttackData(Off, V - ReadNextWordWithData()); return; }
    case 0xb2: { const uint16 Off = ReadNextWordRaw(); const uint16 V = GetAttackData(Off); SetAttackData(Off, V * ReadNextWordWithData()); return; }
    case 0xb3: { const uint16 Off = ReadNextWordRaw(); const uint16 V = GetAttackData(Off); uint16 Div = ReadNextWordWithData(); if (!Div) Div = 1; SetAttackData(Off, V / Div); return; }
    case 0xb4: { const uint16 Off = ReadNextWordRaw(); SetAttackData(Off, (uint16)CurDir); return; }
    case 0xb5: A->Dir = ReadNextWordWithData() & 15; return;
    case 0xb6: { const uint16 Off = ReadNextWordRaw(); SetAttackData(Off, (uint16)CurDir); return; }
    case 0xb7: CruActor::DoAnim(*A, 51, DIR_CURRENT); return;
    case 0xb8: CruActor::DoAnim(*A, 52, DIR_CURRENT); return;
    case 0xb9: CruActor::DoAnim(*A, 53, DIR_CURRENT); return;
    case 0xc0: Seek(ReadNextWordWithData()); return;
    case 0xc1: --Field57; if (Field57 > 0) Seek(Field59); return;
    case 0xff: if (Block == 0) SetBlockNo(1); Seek(TacticDatStartOffset); return;
    default: UE_LOG(LogCru, Warning, TEXT("combat: tactic %d unknown opcode %02X at %d"), Tactic, Opcode, Pos - 1); return;
    }
}

void FAttackProcess::GenericAttack()
{
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A) { Terminate(); return; }
    if (CruActor::IsBusy(*A) || (A->ActorFlags & ACT_PATHFINDING)) return;
    ADBG("generic: dir %d lastanim %d flags %X target %d", A->Dir, A->LastAnim, A->ActorFlags, Target);
    const ObjId ControlledNpc = GCruWorld->ControlledNpc;
    if (ItemNum == ControlledNpc) { Terminate(); return; }
    const FCruItem* Wpn = GCruWorld->GetItem(A->ActiveWeapon);
    const int32 CurDir = A->Dir;
    const int32 TickNow = (int32)GCruKernel->GetTickNum();
    const int32 WpnField8Val = 1;
    int32 TargetDir = DIR_INVALID;
    if (Target != ControlledNpc) Target = ControlledNpc <= 1 ? 1 : ControlledNpc;
    FCruItem* T = GCruWorld->GetItem(Target);
    if (!T || !GCruEngine->IsOnScreen(*T) || T->bDead)
    {
        // walk around randomly hoping to find the target
        Target = 0;
        if (!bIsActivity9orB)
        {
            FIntVector Pt(A->X + RandRng(-0x1ff, 0x1ff), A->Y + RandRng(-0x1ff, 0x1ff), A->Z);
            bField96 = true;
            const ProcId Pid = GCruKernel->AddProcess(new FCruPathfinderProcess(*A, Pt, 12, 0x80, true));
            FProcess* Delay = new FDelayProcess(2);
            GCruKernel->AddProcess(Delay);
            Delay->WaitFor(Pid);
            WaitFor(Delay->Pid);
            return;
        }
        return;
    }
    const uint32 StandAnim = CruActor::IsInCombat(*A) ? CombatStandSmall : Stand;
    const FCruAnimAction* StandAct = GCruWorld->AnimDat.Get(A->Shape, StandAnim);
    const bool bStand16 = StandAct && StandAct->DirCount == 16;
    if (bTimer3set)
    {
        if (Timer3 >= TickNow)
        {
            if (CruActor::IsInCombat(*A))
            {
                if (RandRng(0, 2) != 0)
                {
                    if (A->LastAnim != UnreadySmall && A->LastAnim != UnreadyLarge) CruActor::DoAnim(*A, UnreadySmall, DIR_CURRENT);
                    else CruActor::DoAnim(*A, ReadySmall, DIR_CURRENT);
                    return;
                }
                if (RandRng(0, 2) == 0) { CruActor::TurnTowardDir(*A, CruDirTurnByDelta(CurDir, RandRng(0, 7), false)); return; }
                if (!(A->ActorFlags & ACT_WEAPONREADY)) return;
                if (CurDir != CruActor::DirToItemCentre(*A, *T)) return;
                if (SoundNo != -1) { if (GCruWorld->IsSfxPlaying(ItemNum, SoundNo)) return; SoundNo = -1; }
                WpnField8 = WpnField8Val;
                if (WpnField8 < 3) WpnField8 = 1;
                else if ((bDoubleDelay && RandRng(0, 1) == 0) || RandRng(0, 4) == 0) { A->bAttackAimFlag = true; WpnField8 *= 4; }
                FireTimestamp = TickNow;
                if (Timer4 == 0) Timer4 = TickNow;
                const ProcId AnimPid = CruActor::DoAnim(*A, FireSmall, DIR_CURRENT);
                if (AnimPid == 0) return;
                WaitFor(AnimPid);
                --WpnField8;
                return;
            }
        }
        else { bTimer3set = false; CruActor::SetActivity(*A, 5); }
    }
    if (TargetDir == DIR_INVALID) TargetDir = CruActor::DirToItemCentre(*A, *T);
    const int32 Dist = MaxDistXYZ(FIntVector(A->X, A->Y, A->Z), FIntVector(T->X, T->Y, T->Z));
    const int32 ZDiff = FMath::Abs(A->Z - T->Z);
    const bool bOnScreen = GCruEngine->IsOnScreen(*A);
    if ((!bIsActivity9orB && !bOnScreen) || (Dist <= ZDiff)) { ADBG("pathfind (onscreen %d dist %d zdiff %d)", bOnScreen, Dist, ZDiff); PathfindToItemInNPCData(); return; }
    if (TargetDir == CurDir)
    {
        const int32 Rnd = RandRng(0, 9);
        const uint32 Frameno = FrameNum();
        const uint32 TimeoutFinish = T->GetAttackMoveTimeoutFinishFrame();
        if (!bOnScreen || (!bField96 && !Timer4And5Update(TickNow) && Frameno < TimeoutFinish && Rnd > 2 && (!bIsActivityAorB || Rnd > 3))) { ADBG("facing target, sleep 20"); Sleep(0x14); return; }
        bField96 = false;
        bool bReady;
        if (TickNow - A->LastTickWasHit <= 120) bReady = true; else bReady = CheckReady(TickNow, TargetDir);
        if (bTimer2set && (RandRng(0, 4) == 0 || CheckTimer2PlusDelayElapsed(TickNow))) bTimer2set = false;
        ADBG("facing target, ready %d weaponready %d", bReady, (A->ActorFlags & ACT_WEAPONREADY) != 0);
        if (!bReady) { if (!bIsActivity9orB) PathfindToItemInNPCData(); else Sleep(0xf); return; }
        CheckRandomAttackSound(TickNow, A->Shape);
        if (!(A->ActorFlags & ACT_WEAPONREADY)) { Timer4 = TickNow; CruActor::DoAnim(*A, ReadySmall, DIR_CURRENT); return; }
        if (SoundNo != -1) { if (GCruWorld->IsSfxPlaying(ItemNum, SoundNo)) return; SoundNo = -1; }
        const int32 T5Elapsed = TickNow - Timer5;
        if (T5Elapsed > WpnBasedTimeout)
        {
            const int32 FireElapsed = TickNow - FireTimestamp;
            if (FireElapsed <= DifficultyBasedTimeout) { Sleep(DifficultyBasedTimeout - FireElapsed); return; }
            if (!Wpn) WpnField8 = 1;
            else
            {
                WpnField8 = WpnField8Val;
                if (WpnField8 > 2 && ((bDoubleDelay && RandRng(0, 1) == 0) || RandRng(0, 4) == 0)) { A->bAttackAimFlag = true; WpnField8 *= 4; }
            }
            FireTimestamp = TickNow;
            if (Timer4 == 0) Timer4 = TickNow;
            const ProcId FirePid = CruActor::DoAnim(*A, FireSmall, DIR_CURRENT);
            ADBG("FIRE anim pid %d", FirePid);
            if (FirePid != 0) { WaitFor(FirePid); --WpnField8; }
        }
        else if (T5Elapsed != 0) { ADBG("wpn timeout sleep %d", WpnBasedTimeout - T5Elapsed); Sleep(WpnBasedTimeout - T5Elapsed); return; }
    }
    else
    {
        bool bReady;
        if (!Timer4And5Update(TickNow) && !bField7f)
        {
            if (!bStand16) TargetDir = CruActor::DirToItemCentre(*A, *T);
            bReady = CruCombat::FireDistance(*A, *T, TargetDir, 0, 0, 0) != 0;
            if (bReady) TimeNowToTimerVal2(TickNow);
        }
        else { TimeNowToTimerVal2(TickNow); bReady = true; bField7f = false; }
        ADBG("not facing: targetdir %d curdir %d ready %d", TargetDir, CurDir, bReady);
        if (!(A->ActorFlags & ACT_WEAPONREADY) && bReady) { Timer4 = TickNow; CruActor::DoAnim(*A, ReadySmall, DIR_CURRENT); return; }
        if (bReady || bIsActivity9orB) { const ProcId Tp = CruActor::TurnTowardDir(*A, TargetDir); ADBG("turn toward %d -> pid %d", TargetDir, Tp); return; }
        PathfindToItemInNPCData();
    }
}

// ---------------------------------------------------------------- gravity / hurl
FGravityProcess::FGravityProcess(FCruItem& It, int32 InGravity) : FProcess(It.Id, GRAVITY_PROC_TYPE), Gravity(InGravity) {}

void FGravityProcess::Run()
{
    FCruItem* It = GCruWorld->GetItem(ItemNum);
    if (!It) { Terminate(); return; }
    if (It->Z <= 0 && ZSpeed < 0) { TerminateDeferred(); return; }
    FIntVector Start(It->X, It->Y, It->Z), End(It->X + XSpeed, It->Y + YSpeed, FMath::Max(0, It->Z + ZSpeed));
    int32 Dims[3]; GCruWorld->GetFootpadWorld(*It, Dims[0], Dims[1], Dims[2]);
    TArray<FCruWorld::FSweepItem> Hits;
    GCruWorld->SweepTest(Start, End, Dims, GCruWorld->Info(*It).Flags, It->Id, true, &Hits);
    const FCruWorld::FSweepItem* Hit = nullptr;
    for (const FCruWorld::FSweepItem& H : Hits) if (!H.bTouching) { Hit = &H; break; }
    if (!Hit)
    {
        if (End != Start) GCruWorld->Move(*It, End.X, End.Y, End.Z);
        ZSpeed -= Gravity;
        return;
    }
    const FIntVector P = Hit->GetInterpolatedCoords(Start, End);
    if (P != Start) GCruWorld->Move(*It, P.X, P.Y, P.Z);
    if (Hit->Dirs == 4 && ZSpeed < 0)
    {
        // landed
        GCruWorld->SetFlags(*It, It->Flags & ~Cru::FLG_BOUNCING);
        TerminateDeferred();
        return;
    }
    if (Hit->Dirs & 1) XSpeed = -XSpeed / 2;
    if (Hit->Dirs & 2) YSpeed = -YSpeed / 2;
    if (Hit->Dirs & 4) ZSpeed = -ZSpeed / 2;
    GCruWorld->SetFlags(*It, It->Flags | Cru::FLG_BOUNCING);
}

void FGravityProcess::Terminate()
{
    if (FCruItem* It = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr) if (It->Flags & Cru::FLG_BOUNCING) GCruWorld->SetFlags(*It, It->Flags & ~Cru::FLG_BOUNCING);
    FProcess::Terminate();
}

void CruCombat::Hurl(FCruItem& It, int32 Xs, int32 Ys, int32 Zs, int32 Gravity)
{
    if (It.Flags & Cru::FLG_CONTAINED) return;
    FGravityProcess* P = static_cast<FGravityProcess*>(GCruKernel->FindProcess(It.Id, FGravityProcess::GRAVITY_PROC_TYPE));
    const bool bNew = P == nullptr;
    if (!P) { P = new FGravityProcess(It, 0); GCruKernel->AddProcess(P); }
    P->SetGravity(Gravity);
    P->Move(Xs, Ys, Zs);
    if (bNew) P->WaitFor(GCruKernel->AddProcess(new FDelayProcess(0x14)));   // Crusader sleeps a fresh gravity process first
}

// ---------------------------------------------------------------- target reticle (TargetReticleProcess)
void FTargetReticleProcess::Run()
{
    const uint32 Frameno = GCruKernel->GetTickNum() / 2;
    FCruItem* Main = GetControlledActor();
    FProcess* SpriteProc = ReticleSpritePid ? GCruKernel->GetProcess(ReticleSpritePid) : nullptr;
    if (!Main || !CruActor::IsInCombat(*Main))
    {
        if (SpriteProc) SpriteProc->Terminate();
        ReticleSpritePid = 0;
        return;
    }
    if (ReticleSpritePid && (!SpriteProc || SpriteProc->IsTerminated()))
    {
        // the animated sprite finished but the target is still valid: hold the last frame
        if (FCruItem* T = GCruWorld->GetItem(LastTargetItem)) PutReticleOnItem(*T, true);
    }
    if (Frameno - LastUpdate < 60) return;
    const bool bChanged = FindTargetItem();
    if (SpriteProc && bChanged) SpriteProc->Terminate();
    LastUpdate = Frameno;
}

bool FTargetReticleProcess::FindTargetItem()
{
    FCruItem* Main = GetControlledActor();
    if (!Main) return false;
    const int32 Dir = Main->Dir;
    FCruItem* It = CruCombat::FindBestTargetItem(Main->X, Main->Y, Main->Z, Dir, true);
    if (GCruCombatDebug) UE_LOG(LogCru, Display, TEXT("reticle scan from (%d,%d,%d) dir %d: %s"), Main->X, Main->Y, Main->Z, Dir, It ? *FString::Printf(TEXT("item %d shape %d at (%d,%d,%d)"), It->Id, It->Shape, It->X, It->Y, It->Z) : TEXT("nothing"));
    if (It && It->Id != LastTargetItem)
    {
        if (FCruItem* Last = GCruWorld->GetItem(LastTargetItem)) Last->ExtFlags &= ~Cru::EXT_TARGET;
        PutReticleOnItem(*It, false);
        LastTargetDir = Dir;
        return true;
    }
    if (!It)
    {
        if (LastTargetItem) if (FCruItem* Last = GCruWorld->GetItem(LastTargetItem)) Last->ExtFlags &= ~Cru::EXT_TARGET;
        ClearSprite();
        return true;
    }
    return false;
}

void FTargetReticleProcess::PutReticleOnItem(FCruItem& It, bool bOnlyLastFrame)
{
    FIntVector Pt = GCruWorld->GetCentre(It); Pt.Z -= 8;
    const int32 First = ReticleStyle * 6, Last = First + 5;
    FSpriteProcess* P = bOnlyLastFrame ? new FSpriteProcess(0x59a, Last, Last, 1, 1000, Pt.X, Pt.Y, Pt.Z) : new FSpriteProcess(0x59a, First, Last, 1, 10, Pt.X, Pt.Y, Pt.Z);
    ReticleSpritePid = GCruKernel->AddProcess(P);
    LastTargetItem = It.Id;
    It.ExtFlags |= Cru::EXT_TARGET;
    UE_LOG(LogCru, Display, TEXT("reticle on item %d shape %d at (%d,%d,%d)%s"), It.Id, It.Shape, Pt.X, Pt.Y, Pt.Z, bOnlyLastFrame ? TEXT(" (hold)") : TEXT(""));
}

void FTargetReticleProcess::ItemMoved(const FCruItem& It)
{
    if (!ReticleSpritePid || It.Id != LastTargetItem) { ClearSprite(); return; }
    const FIntVector Pt = GCruWorld->GetCentre(It);
    int32 ActorDir = -1, DirToItem = -2;
    if (FCruItem* Main = GetControlledActor()) { ActorDir = Main->Dir; DirToItem = CruActor::DirToItemCentre(*Main, It); }
    FSpriteProcess* Sp = static_cast<FSpriteProcess*>(GCruKernel->GetProcess(ReticleSpritePid));
    if (!Sp) return;
    if (ActorDir != LastTargetDir || DirToItem != LastTargetDir) { Sp->Terminate(); ReticleSpritePid = 0; ClearSprite(); }
    else Sp->Move(Pt.X, Pt.Y, Pt.Z - 8);
}

void FTargetReticleProcess::ClearSprite()
{
    if (FProcess* P = ReticleSpritePid ? GCruKernel->GetProcess(ReticleSpritePid) : nullptr) P->Terminate();
    ReticleSpritePid = 0;
    if (FCruItem* Last = GCruWorld->GetItem(LastTargetItem)) Last->ExtFlags &= ~Cru::EXT_TARGET;
    LastTargetItem = 0;
}
