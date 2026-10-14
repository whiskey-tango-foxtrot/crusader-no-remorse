// Actors: animation data, animation tracker/process and the simple AI processes.
// Ported from ScummVM (anim_dat, animation_tracker, actor_anim_process, loiter/pace/guard/cru_pathfinder processes).
#pragma once
#include "CruCore.h"
#include "Kernel.h"

struct FCruAnimFrame
{
    uint32 Frame = 0; int32 DeltaZ = 0; uint32 Sfx = 0; int32 DeltaDir = 0; uint32 Flags = 0;
    bool OnGround() const { return (Flags & 0x2) != 0; }
    bool CruFlipped() const { return (Flags & 0x8000) != 0; }
    bool CallUsecode() const { return (Flags & 0x4000) != 0; }
    bool Hurty() const { return (Flags & 0x1000) != 0; }
    int32 AttackRange() const { return (Flags >> 2) & 7; }
    int32 CruAttackX() const { const uint32 Raw = (Flags & 0x780) << 5; return ((int16)Raw >> 12) * 16; }
    int32 CruAttackY() const { return (int8)((Flags & 0x00F00000) >> 16); }
    int32 CruAttackZ() const { return (Flags & 0x0F000000) >> 21; }
    bool IsCruAttack() const { return CruAttackX() || CruAttackY() || CruAttackZ(); }
};

enum : uint32 { AAF_TWOSTEP = 1, AAF_LOOPING = 4, AAF_UNSTOPPABLE = 8, AAF_16DIRS = 0x4000, AAF_DESTROYACTOR = 0x8000, AAF_ROTATED = 0x10000 };

struct FCruAnimAction
{
    uint32 Shape = 0, Action = 0, Size = 0, Flags = 0, FrameRepeat = 0, DirCount = 8;
    TArray<TArray<FCruAnimFrame>> Dirs;
    bool HasFlags(uint32 M) const { return (Flags & M) != 0; }
    const FCruAnimFrame& GetFrame(int32 Dir16, uint32 FrameNo) const
    {
        uint32 D = (uint32)Dir16; if (DirCount == 8) D /= 2;
        D = FMath::Min(D, (uint32)Dirs.Num() - 1);
        return Dirs[D][FMath::Min(FrameNo, (uint32)Dirs[D].Num() - 1)];
    }
    void GetAnimRange(int32 LastAnim, int32 LastDir, bool bFirstStep, int32 Dir, uint32& Start, uint32& End) const;
};

class FCruAnimDat
{
public:
    bool Load(const FString& AnimJson);
    const FCruAnimAction* Get(uint32 Shape, uint32 Action) const { const TMap<uint32, FCruAnimAction>* S = Shapes.Find(Shape); return S ? S->Find(Action) : nullptr; }
private:
    TMap<uint32, TMap<uint32, FCruAnimAction>> Shapes;
};

namespace CruAnim
{
    enum : uint32 { Stand = 0, Walk = 1, RetreatSmall = 2, Run = 3, CombatStandSmall = 4, ReadySmall = 7, FireSmall = 8, ReloadSmall = 10, UnreadySmall = 11, ReadyLarge = 12, FireLarge = 13, UnreadyLarge = 16, FallBackwards = 18, FallForwards = 20, LookLeft = 30, LookRight = 31, TeleportIn = 32, TeleportOut = 33, AdvanceSmall = 36, CombatStandLarge = 37, AdvanceLarge = 44, RetreatLarge = 45, Idle1 = 47, Idle2 = 48 };
    constexpr int32 DIR_CURRENT = 16, DIR_INVALID = 100;
    enum : uint32 { ACT_INVINCIBLE = 1, ACT_ANIMLOCK = 8, ACT_KNEELING = 0x100, ACT_FIRSTSTEP = 0x400, ACT_INCOMBAT = 0x800, ACT_DEAD = 0x1000, ACT_SURRENDERED = 0x2000, ACT_WEAPONREADY = 0x4000, ACT_COMBATRUN = 0x8000, ACT_IMMORTAL = 0x40000, ACT_PATHFINDING = 0x800000 };
    constexpr uint16 ACTOR_ANIM_PROC_TYPE = 0xF0, PATHFINDER_PROC_TYPE = 0x204, PACE_PROC_TYPE = 0x255, LOITER_PROC_TYPE = 0x257, ATTACK_PROC_TYPE = 0x259, GUARD_PROC_TYPE = 0x25E, SURRENDER_PROC_TYPE = 0x25F;
}

// 16-direction helpers (ScummVM direction_util.h)
int32 CruDirXFactor(int32 Dir);
int32 CruDirYFactor(int32 Dir);
int32 CruDirOneLeft(int32 Dir, bool b16);
int32 CruDirOneRight(int32 Dir, bool b16);
int32 CruDirTurnByDelta(int32 Dir, int32 Delta, bool b16);
int32 CruDirShorterTurnDelta(int32 From, int32 To);
int32 CruDirGetWorldDir(int32 Dy, int32 Dx, bool b16);
int32 CruDirInvert(int32 D);

struct FPathfindingState { FIntVector Point = FIntVector::ZeroValue; int32 LastAnim = -1; int32 Direction = 0; bool bFirstStep = false; bool bFlipped = false; bool bCombat = false; };

class FAnimationTracker
{
public:
    bool Init(const FCruItem& Actor, uint32 Action, int32 Dir, const FPathfindingState* State = nullptr);
    bool Step();
    bool StepFrom(const FIntVector& P) { Curr = P; return Step(); }
    bool IsDone() const { return bDone; }
    bool IsBlocked() const { return bBlocked; }
    bool IsUnsupported() const { return bUnsupported; }
    const FCruAnimFrame* GetAnimFrame() const { return AnimAction ? &AnimAction->GetFrame(Dir, CurrentFrame) : nullptr; }
    uint32 GetFrame() const { return ShapeFrame; }
    FIntVector GetPosition() const { return Curr; }
    void GetSpeed(int32& Dx, int32& Dy, int32& Dz) const { Dx = Curr.X - Prev.X; Dy = Curr.Y - Prev.Y; Dz = Curr.Z - Prev.Z; }
    void UpdateActorFlags();
    void UpdateState(FPathfindingState& S) const { S.Point = Curr; S.bFlipped = bFlipped; S.bFirstStep = bFirstStep; }
    const FCruAnimAction* AnimAction = nullptr;
    ObjId HitObject = 0;
private:
    uint32 GetNextFrame(uint32 F) const;
    ObjId ActorId = 0; int32 Dir = 0;
    uint32 StartFrame = 0, EndFrame = 0, CurrentFrame = 0, ShapeFrame = 0;
    FIntVector Prev, Curr, Start;
    bool bFirstFrame = true, bFirstStep = false, bFlipped = false, bDone = false, bBlocked = false, bUnsupported = false;
};

class FActorAnimProcess : public FProcess
{
public:
    FActorAnimProcess(FCruItem& Actor, uint32 InAction, int32 InDir, uint32 InSteps = 0);
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("ActorAnimProcess"); }
    uint32 Action; int32 Dir; uint32 Steps;
private:
    bool Init();
    TUniquePtr<FAnimationTracker> Tracker;
    int32 RepeatCounter = 0; uint32 CurrentStep = 0;
    bool bFirstFrame = true, bAnimAborted = false, bAttackedSomething = false;
};

class FDelayProcess : public FProcess
{
public:
    explicit FDelayProcess(int32 InCount) : Count(InCount) {}
    virtual void Run() override { if (--Count <= 0) Terminate(); }
    virtual FString GetClassName() const override { return TEXT("DelayProcess"); }
    int32 Count;
};

class FCruPathfinderProcess : public FProcess
{
public:
    FCruPathfinderProcess(FCruItem& Actor, const FIntVector& Target, int32 MaxSteps, int32 StopDistance, bool bTurnAtEnd);
    FCruPathfinderProcess(FCruItem& Actor, const FCruItem& TargetItem, int32 MaxSteps, int32 StopDistance, bool bTurnAtEnd);
    virtual void Run() override;
    virtual void Terminate() override;
    virtual FString GetClassName() const override { return TEXT("CruPathfinderProcess"); }
private:
    void Setup(FCruItem& Actor);
    int32 NextDirFromPoint(FIntVector& NpcPt);
    FIntVector Target; ObjId TargetItem = 0;
    int32 CurrentStep = 0, CurrentDistance = 0, MaxSteps = 0, StopDistance = 0;
    bool bRandomFlag = false, bNextTurn = false, bTurnAtEnd = false, bSolidObject = true, bDirectPathBlocked = false, bNoShotAvailable = true, bDir16Flag = false;
    int32 LastDir = CruAnim::DIR_CURRENT, NextDir = CruAnim::DIR_CURRENT, NextDir2 = CruAnim::DIR_CURRENT;
};

class FLoiterProcess : public FProcess
{
public:
    FLoiterProcess(FCruItem& Actor, int32 InCount = -1);
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("LoiterProcess"); }
    int32 Count;
};

class FPaceProcess : public FProcess
{
public:
    explicit FPaceProcess(FCruItem& Actor);
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("PaceProcess"); }
    int32 Counter = 0;
};

// Unarmed NPC gives up: surrender anim, then keeps facing the avatar and calls out once (SurrenderProcess, 0x25F)
class FSurrenderProcess : public FProcess
{
public:
    explicit FSurrenderProcess(FCruItem& Actor);
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("SurrenderProcess"); }
private:
    bool bPlayedSound = false;
};

class FGuardProcess : public FProcess
{
public:
    explicit FGuardProcess(FCruItem& Actor);
    virtual void Run() override;
    virtual FString GetClassName() const override { return TEXT("GuardProcess"); }
};

namespace CruActor
{
    ProcId DoAnim(FCruItem& A, uint32 Action, int32 Dir, uint32 Steps = 0);
    ProcId DoAnimAfter(FCruItem& A, uint32 Action, int32 Dir, ProcId WaitFor);
    bool TryAnim(FCruItem& A, uint32 Action, int32 Dir, uint32 Steps = 0, FPathfindingState* State = nullptr);
    bool HasAnim(const FCruItem& A, uint32 Action);
    ProcId TurnTowardDir(FCruItem& A, int32 TargetDir, ProcId PrevPid = 0);
    uint16 SetActivity(FCruItem& A, int32 Activity);
    bool IsBusy(const FCruItem& A);
    bool IsInCombat(const FCruItem& A);
    bool CanSeeControlledActor(const FCruItem& A, bool bForCombat);
    int32 DirToItemCentre(const FCruItem& A, const FCruItem& B);
}
