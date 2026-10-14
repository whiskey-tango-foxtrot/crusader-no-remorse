// Crusader combat: fire types, weapon data, item damage (DAMAGE.FLX), projectiles, the NPC attack process
// (generic attack + COMBAT.DAT tactic interpreter), hits and death. Ported from ScummVM ultima8 (GPL).
#pragma once
#include "CruCore.h"
#include "Kernel.h"
#include "CruActors.h"

struct FCruFireType
{
    uint16 TypeNo, MinDamage, MaxDamage, Range, NumShots, ShieldCost, ShieldMask;
    bool bAccurate; uint16 CellsPerRound, RoundDuration; bool bNearSprite;
    int32 GetRandomDamage() const { return MinDamage == MaxDamage ? MinDamage : FMath::RandRange((int32)MinDamage, (int32)MaxDamage); }
};
const FCruFireType* CruGetFireType(int32 Type);

// remorseweapons.ini (ScummVM devtools) - stats per weapon shape
struct FCruWeaponInfo
{
    const TCHAR* Name; uint32 Shape; uint16 Sound, AmmoType, DamageType, OverlayShape; bool bSmall; uint16 ReloadSound, ShotDelay, ClipSize, AmmoShape, EnergyUse;
    uint8 Field8 = 1;   // always 1 in the data
};
const FCruWeaponInfo* CruGetWeaponInfo(uint32 Shape);

// Status bar / pickup icons: GUMPS.FLX shape + frame per item shape (remorseweapons.ini display_shape / display_frame)
struct FCruGumpIcon { uint32 Shape; uint8 GumpShape, GumpFrame; const TCHAR* Name; };
const FCruGumpIcon* CruGetGumpIcon(uint32 Shape);

// DAMAGE.FLX record (6 bytes per shape). Flags ABCxDEEF: A absolute frame data, B replace item, C explosion destroys,
// D explosion damages surroundings, EE explosion type, F takes damage.
struct FCruDamageInfo
{
    uint8 Flags = 0, Sound = 0, Data[3] = { 0, 0, 0 }, DamagePoints = 0;
    bool IsValid() const { return Flags != 0; }
    bool FrameDataIsAbsolute() const { return (Flags >> 7) & 1; }
    bool ReplaceItem() const { return (Flags >> 6) & 1; }
    bool ExplodeDestroysItem() const { return (Flags >> 5) & 1; }
    bool ExplodeWithDamage() const { return (Flags >> 3) & 1; }
    bool TakesDamage() const { return Flags & 1; }
    bool Explodes() const { return (Flags & 0x06) != 0; }
    int32 ExplosionType() const { return ((Flags & 0x06) >> 1) - 1; }
    uint16 ReplacementShape() const { return (uint16)Data[1] << 8 | Data[0]; }
    uint16 ReplacementFrame() const { return Data[2]; }
};

// COMBAT.DAT entry: 16 byte name, 4 u16 block offsets (into the whole entry), tactic bytecode.
struct FCruCombatDat { FString Name; uint16 Offsets[4] = { 0, 0, 0, 0 }; TArray<uint8> Data; };

// Shows frames First..Last of a shape at a point, Repeats times, Delay*2 ticks per frame (SpriteProcess).
class FSpriteProcess : public FProcess
{
public:
    FSpriteProcess(int32 Shape, int32 Frame, int32 LastFrame, int32 Repeats, int32 Delay, int32 X, int32 Y, int32 Z);
    virtual ~FSpriteProcess();
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("SpriteProcess"); }
    void Move(int32 NewX, int32 NewY, int32 NewZ);
private:
    int32 Shape, Frame, FirstFrame, LastFrame, Repeats, Delay, X, Y, Z, DelayCounter = 0;
};

// A projectile (SuperSpriteProcess).
class FSuperSpriteProcess : public FProcess
{
public:
    FSuperSpriteProcess(int32 Shape, int32 Frame, const FIntVector& Start, const FIntVector& Dest, uint16 FireType, uint16 Damage, ObjId Source, ObjId Target, bool bInexact);
    virtual ~FSuperSpriteProcess();
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("SuperSpriteProcess"); }
private:
    void HitAndFinish();
    void MakeBulletSplash(const FIntVector& Pt);
    void AdvanceFrame();
    bool AreaSearch();
    void DestroyItemOrTerminate();
    int32 Shape, Frame; FIntVector StartPt, NowPt, NextPt, DestPt, Pt3, Pt5 = FIntVector(-1, 0, 0);
    uint16 FireType, Damage; ObjId Source, Target; int32 Counter = 1; ObjId Item0x77 = 0, SpriteNo = 0;
    bool bStartedAsFiretype9, bExpired = false; float XStep = 0, YStep = 0, ZStep = 0;
};

// NPC combat AI (AttackProcess, type 0x259).
class FAttackProcess : public FProcess
{
public:
    explicit FAttackProcess(FCruItem& Actor);
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("AttackProcess"); }
    void SetIsActivityAOrB() { bIsActivityAorB = true; }
    void SetIsActivity9OrB() { bIsActivity9orB = true; }
    void SetField97() { bField97 = true; }
    void SetField7F() { bField7f = true; }
    void SetTimer3();
    ObjId GetTarget() const { return Target; }
    void SetTarget(ObjId T) { Target = T; }
private:
    void SetTacticNo(int32 Tactic);
    void SetBlockNo(int32 Block);
    uint16 ReadNextWordRaw();
    uint16 ReadNextWordWithData();
    void SetAttackData(uint16 Off, uint16 Val);
    uint16 GetAttackData(uint16 Off) const;
    void Seek(uint16 Off) { Pos = Off; }
    void GenericAttack();
    void Sleep(int32 Ticks);
    bool ReadyForNextSound(uint32 Now);
    bool CheckTimer2PlusDelayElapsed(int32 Now);
    void PathfindToItemInNPCData();
    bool Timer4And5Update(int32 Now);
    void TimeNowToTimerVal2(int32 Now) { Timer2 = Now; bTimer2set = true; }
    bool CheckReady(int32 Now, int32 TargetDir);
    void CheckRandomAttackSound(int32 Now, uint32 ShapeNo);

    ObjId Target = 1; uint16 Tactic = 0, Block = 0, TacticDatStartOffset = 0;
    const FCruCombatDat* TacticDat = nullptr; int32 Pos = 0;
    int16 SoundNo = -1; bool bPlayedStartSound = false; int32 NpcInitialDir = 0;
    int16 Field57 = 0; uint16 Field59 = 0;
    bool bField7f = false, bField96 = false, bField97 = false, bIsActivity9orB = false, bIsActivityAorB = false, bTimer2set = false, bTimer3set = false, bDoubleDelay = false;
    uint16 WpnField8 = 1; uint16 DataArray[10] = { 0 };
    int32 WpnBasedTimeout = 0, DifficultyBasedTimeout = 0, Timer2 = 0, Timer3 = 0, Timer4 = 0, Timer5 = 0;
    uint32 SoundTimestamp = 0, SoundDelayTicks = 480; int32 FireTimestamp = 0;
};

// Thrown / falling item (GravityProcess, type 0x203): moves by its speed each tick, sweeps for blockers, bounces
// off side hits, stops when it lands. Actor fall damage is not ported.
class FGravityProcess : public FProcess
{
public:
    FGravityProcess(FCruItem& Item, int32 InGravity);
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("GravityProcess"); }
    void Move(int32 Xs, int32 Ys, int32 Zs) { XSpeed += Xs; YSpeed += Ys; ZSpeed += Zs; }
    void SetGravity(int32 G) { if (G > Gravity) Gravity = G; }
    static constexpr uint16 GRAVITY_PROC_TYPE = 0x203;
private:
    int32 XSpeed = 0, YSpeed = 0, ZSpeed = 0, Gravity = 0;
};

// Marks the auto-aim target in the controlled actor's facing direction with the reticle sprite (TargetReticleProcess).
class FTargetReticleProcess : public FProcess
{
public:
    FTargetReticleProcess() : FProcess(0, 1) {}
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("TargetReticleProcess"); }
    void ItemMoved(const FCruItem& Item);
    void AvatarMoved() { LastUpdate = 0; }
    ObjId GetTarget() const { return LastTargetItem; }
private:
    bool FindTargetItem();
    void PutReticleOnItem(FCruItem& Item, bool bOnlyLastFrame);
    void ClearSprite();
    uint32 LastUpdate = 0; ProcId ReticleSpritePid = 0; int32 LastTargetDir = -1; ObjId LastTargetItem = 0; int32 ReticleStyle = 0;
};

namespace CruCombat
{
    void Hurl(FCruItem& It, int32 Xs, int32 Ys, int32 Zs, int32 Gravity);   // Item::hurl
    constexpr uint32 BULLET_SPLASH_SHAPE = 0x1D9;
    bool LoadCombatDat(const FString& Path);          // data/hd/STATIC/COMBAT.DAT
    const FCruCombatDat* GetCombatDat(int32 Index);
    bool LoadDamageFlex(const FString& Path);         // data/hd/STATIC/DAMAGE.FLX -> per-shape damage info
    const FCruDamageInfo* GetDamageInfo(uint32 Shape);

    // Item::fireWeapon / fireDistance / getTargetZRelativeToAttackerZ
    ProcId FireWeapon(FCruItem& Src, int32 X, int32 Y, int32 Z, int32 Dir, int32 FireType, bool bFindTarget);
    uint16 FireDistance(const FCruItem& Src, const FCruItem& Other, int32 Dir, int32 Xoff, int32 Yoff, int32 Zoff);
    int32 GetTargetZRelativeToAttackerZ(const FCruItem& Target, int32 OtherZ);
    FCruItem* FindBestTargetItem(int32 X, int32 Y, int32 Z, int32 Dir, bool b16);
    int32 GetRangeIfVisible(const FCruItem& A, const FCruItem& B);   // 0 when line of sight is blocked

    // hits
    void ReceiveHit(FCruItem& It, ObjId Other, int32 Dir, int32 Damage, uint16 Type);
    int32 ReceiveShieldHit(FCruItem& Avatar, int32 Damage, uint16 Type);   // returns the damage left after the shield
    void Explode(FCruItem& It, int32 ExplosionType, bool bDestroyItem, bool bCauseDamage = true);
    void ApplySplashDamageAround(const FCruFireType& Ft, const FIntVector& Pt, int32 Damage, int32 RangeDiv, ObjId Exclude, ObjId Src);
    void MakeBulletSplashShapeAndPlaySound(const FCruFireType& Ft, int32 X, int32 Y, int32 Z);
    bool IsRobot(const FCruItem& It);
    int32 ScaleReceivedDamage(const FCruItem& It, int32 Damage, uint16 Type);

    // actors
    ProcId Die(FCruItem& A, uint16 DamageType, uint16 DamagePts, int32 SrcDir);
    void TookHit(FCruItem& A);
    void SetInCombatCru(FCruItem& A, int32 Activity);
    void ClearInCombat(FCruItem& A);
    FAttackProcess* GetAttackProcess(const FCruItem& A);
    void SetToStartOfAnim(FCruItem& A, uint32 Anim);
    void DoFireWeaponCru(FCruItem& A, const FCruAnimFrame& F);   // called by the anim process on attack frames
    void CheckForAlertingNPCs();                                  // the controlled actor fired
}
