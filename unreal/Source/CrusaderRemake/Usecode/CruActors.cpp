#include "CruActors.h"
#include "CruCombat.h"
extern bool GCruCombatDebug;
#include "CruWorld.h"
#include "CruEngine.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace CruAnim;

// ---------------------------------------------------------------- directions
int32 CruDirXFactor(int32 Dir) { static const int32 F[] = { 0, 1, 1, 2, 1, 2, 1, 1, 0, -1, -1, -2, -1, -2, -1, -1, 0 }; return F[FMath::Clamp(Dir, 0, 16)]; }
int32 CruDirYFactor(int32 Dir) { static const int32 F[] = { -1, -2, -1, -1, 0, 1, 1, 2, 1, 2, 1, 1, 0, -1, -1, -2, 0 }; return F[FMath::Clamp(Dir, 0, 16)]; }
int32 CruDirOneLeft(int32 Dir, bool b16) { return (Dir + (b16 ? 15 : 14)) % 16; }
int32 CruDirOneRight(int32 Dir, bool b16) { return (Dir + (b16 ? 1 : 2)) % 16; }
int32 CruDirTurnByDelta(int32 Dir, int32 Delta, bool b16) { for (int32 i = 0; i < Delta; ++i) Dir = CruDirOneRight(Dir, b16); for (int32 i = 0; i < -Delta; ++i) Dir = CruDirOneLeft(Dir, b16); return Dir; }
int32 CruDirShorterTurnDelta(int32 From, int32 To) { return ((From - To + 16) % 16 < 8) ? -1 : 1; }
int32 CruDirInvert(int32 D) { return (D + 8) % 16; }
int32 CruDirGetWorldDir(int32 Dy, int32 Dx, bool b16)
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

// ---------------------------------------------------------------- anim data
bool FCruAnimDat::Load(const FString& Path)
{
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Path)) { UE_LOG(LogCru, Error, TEXT("anim: cannot read %s"), *Path); return false; }
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    int32 NumActions = 0;
    for (auto& SP : Root->Values)
    {
        const uint32 Shape = FCString::Atoi(*FString(SP.Key));
        const TSharedPtr<FJsonObject>& Acts = SP.Value->AsObject(); if (!Acts.IsValid()) continue;
        TMap<uint32, FCruAnimAction>& Dst = Shapes.Add(Shape);
        for (auto& AP : Acts->Values)
        {
            const TSharedPtr<FJsonObject>& O = AP.Value->AsObject(); if (!O.IsValid()) continue;
            FCruAnimAction A; A.Shape = Shape; A.Action = FCString::Atoi(*FString(AP.Key));
            A.Size = (uint32)O->GetNumberField(TEXT("size")); A.FrameRepeat = (uint32)O->GetNumberField(TEXT("repeat"));
            const uint32 Raw = (uint32)O->GetNumberField(TEXT("flags"));
            A.Flags = (Raw & (AAF_TWOSTEP | AAF_LOOPING | AAF_UNSTOPPABLE | 0x80 | AAF_DESTROYACTOR)) | (Raw & AAF_16DIRS) | (Raw & AAF_ROTATED);
            for (const TSharedPtr<FJsonValue>& DV : O->GetArrayField(TEXT("dirs")))
            {
                TArray<FCruAnimFrame> Frames;
                for (const TSharedPtr<FJsonValue>& FV : DV->AsArray())
                {
                    const TSharedPtr<FJsonObject>& F = FV->AsObject();
                    FCruAnimFrame Fr; Fr.Frame = (uint32)F->GetNumberField(TEXT("frame")); Fr.DeltaZ = (int32)F->GetNumberField(TEXT("dz")); Fr.Sfx = (uint32)F->GetNumberField(TEXT("sfx")); Fr.DeltaDir = (int32)F->GetNumberField(TEXT("ddir")); Fr.Flags = (uint32)F->GetNumberField(TEXT("flags"));
                    Frames.Add(Fr);
                }
                A.Dirs.Add(MoveTemp(Frames));
            }
            A.DirCount = A.Dirs.Num();
            Dst.Add(A.Action, MoveTemp(A)); ++NumActions;
        }
    }
    UE_LOG(LogCru, Display, TEXT("anim: %d shapes, %d actions"), Shapes.Num(), NumActions);
    return true;
}

void FCruAnimAction::GetAnimRange(int32 LastAnim, int32 LastDir, bool bFirstStep, int32 InDir, uint32& StartF, uint32& EndF) const
{
    StartF = 0; EndF = Size;
    if (Flags & AAF_TWOSTEP)
    {
        const bool bLooping = HasFlags(AAF_LOOPING);
        if (bFirstStep) { StartF = bLooping ? Size - 1 : 0; EndF = Size / 2; }
        else { StartF = Size / 2; if (bLooping) EndF = Size - 1; }
    }
    else if (LastAnim == (int32)Action && LastDir == InDir && Size > 1) StartF = 1;
}

// ---------------------------------------------------------------- tracker
bool FAnimationTracker::Init(const FCruItem& A, uint32 Action, int32 InDir, const FPathfindingState* State)
{
    ActorId = A.Id;
    AnimAction = GCruWorld->AnimDat.Get(A.Shape, Action);
    if (!AnimAction || AnimAction->Dirs.Num() == 0) return false;
    Dir = InDir;
    if (!State)
    {
        AnimAction->GetAnimRange(A.LastAnim, A.Dir, (A.ActorFlags & ACT_FIRSTSTEP) != 0, Dir, StartFrame, EndFrame);
        Curr = FIntVector(A.X, A.Y, A.Z); bFlipped = A.IsFlipped(); bFirstStep = (A.ActorFlags & ACT_FIRSTSTEP) != 0;
    }
    else
    {
        AnimAction->GetAnimRange(State->LastAnim, State->Direction, State->bFirstStep, Dir, StartFrame, EndFrame);
        bFlipped = State->bFlipped; bFirstStep = State->bFirstStep; Curr = State->Point;
    }
    Start = Curr; bFirstFrame = true; bDone = bBlocked = bUnsupported = false; HitObject = 0;
    return true;
}

uint32 FAnimationTracker::GetNextFrame(uint32 F) const
{
    ++F;
    if (!AnimAction || F == EndFrame) return EndFrame;
    if (F >= AnimAction->Size) F = AnimAction->HasFlags(AAF_LOOPING) ? 1 : 0;
    return F;
}

bool FAnimationTracker::Step()
{
    if (bDone) return false;
    CurrentFrame = bFirstFrame ? StartFrame : GetNextFrame(CurrentFrame);
    if (CurrentFrame == EndFrame)
    {
        bDone = true;
        if (AnimAction->HasFlags(AAF_TWOSTEP)) bFirstStep = !bFirstStep;
        return false;
    }
    Prev = Curr; bUnsupported = false; bBlocked = false; bFirstFrame = false;
    const FCruAnimFrame& F = AnimAction->GetFrame(Dir, CurrentFrame);
    ShapeFrame = F.Frame;
    bFlipped = F.CruFlipped();
    int32 MoveDir = Dir;
    if (AnimAction->HasFlags(AAF_ROTATED)) MoveDir = CruDirTurnByDelta(MoveDir, 4, true);
    const int32 Dx = 4 * CruDirXFactor(MoveDir) * F.DeltaDir, Dy = 4 * CruDirYFactor(MoveDir) * F.DeltaDir, Dz = F.DeltaZ;
    FCruItem* A = GCruWorld->GetItem(ActorId);
    if (!A) { bBlocked = true; return false; }
    int32 Xd, Yd, Zd; GCruWorld->GetFootpadWorld(*A, Xd, Yd, Zd);
    if (A->IsFlipped() != bFlipped) Swap(Xd, Yd);
    int32 Tx = Curr.X + Dx, Ty = Curr.Y + Dy, Tz = Curr.Z + Dz;
    const uint32 ShapeFlags = GCruWorld->Info(*A).Flags;
    // only particularly large steps get a full sweep test
    if (FMath::Abs(Dx) >= Xd - 8 || FMath::Abs(Dy) >= Yd - 8 || FMath::Abs(Dz) >= Zd - 8)
    {
        const FIntVector End(Tx, Ty, Tz);
        const int32 Dims[3] = { Xd, Yd, Zd };
        TArray<FCruWorld::FSweepItem> Hits;
        GCruWorld->SweepTest(Curr, End, Dims, ShapeFlags, ActorId, false, &Hits);
        for (const FCruWorld::FSweepItem& H : Hits)
            if (!H.bTouching && H.bBlocking) { bBlocked = true; Curr = H.GetInterpolatedCoords(End, Curr); return false; }
    }
    FCruBox Target(Tx, Ty, Tz, Xd, Yd, Zd);
    const FCruBox StartBox(Start.X, Start.Y, Start.Z, Xd, Yd, Zd);
    FCruWorld::FPositionInfo Info = GCruWorld->GetPositionInfo(Target, StartBox, ShapeFlags, ActorId);
    if (!Info.bValid || (F.OnGround() && !Info.bSupported))
    {
        // on the ground: look for a nearby valid, supported spot (never for dead actors: no gravity for them)
        if (F.OnGround() && Zd > 8 && !A->bDead)
        {
            if (!GCruWorld->ScanForValidPosition(Tx, Ty, Tz, *A, Dir, true, Tx, Ty, Tz)) { bBlocked = true; return false; }
            Target = FCruBox(Tx, Ty, Tz, Xd, Yd, Zd);
        }
        else if (!Info.bValid) { bBlocked = true; return false; }
    }
    Curr = FIntVector(Tx, Ty, Tz);
    if (F.OnGround())
    {
        Info = GCruWorld->GetPositionInfo(Target, StartBox, ShapeFlags, ActorId);
        if (!Info.bSupported) { bUnsupported = true; return false; }
    }
    if (F.CallUsecode()) GCruWorld->CallUsecodeEvent(*A, Cru::EV_CALLEDFROMANIM);
    return true;
}

void FAnimationTracker::UpdateActorFlags()
{
    FCruItem* A = GCruWorld->GetItem(ActorId);
    if (!A) return;
    GCruWorld->SetFlags(*A, bFlipped ? (A->Flags | Cru::FLG_FLIPPED) : (A->Flags & ~Cru::FLG_FLIPPED));
    if (bFirstStep) A->ActorFlags |= ACT_FIRSTSTEP; else A->ActorFlags &= ~ACT_FIRSTSTEP;
    if (CurrentFrame != EndFrame) A->AnimFrame = CurrentFrame;
}

// ---------------------------------------------------------------- anim process
FActorAnimProcess::FActorAnimProcess(FCruItem& A, uint32 InAction, int32 InDir, uint32 InSteps) : FProcess(A.Id, ACTOR_ANIM_PROC_TYPE), Action(InAction), Dir(InDir), Steps(InSteps)
{
    if (Dir == DIR_CURRENT) Dir = A.Dir;
}

bool FActorAnimProcess::Init()
{
    RepeatCounter = 0; bAnimAborted = false; bAttackedSomething = false;
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A) return false;
    if (Dir == DIR_CURRENT) Dir = A->Dir;
    if (!(A->Flags & Cru::FLG_FASTAREA)) return false;
    if (A->ActorFlags & ACT_ANIMLOCK) { UE_LOG(LogCru, Verbose, TEXT("anim: actor %d animlock, skipping action %d"), ItemNum, Action); return false; }
    Tracker = MakeUnique<FAnimationTracker>();
    if (!Tracker->Init(*A, Action, Dir)) { Tracker.Reset(); return false; }
    A->ActorFlags |= ACT_ANIMLOCK;
    A->LastAnim = Action; A->Dir = Dir;
    return true;
}

void FActorAnimProcess::Run()
{
    if (bFirstFrame && !Init()) { TerminateDeferred(); return; }
    if (bAnimAborted) { Terminate(); return; }
    if (!bFirstFrame) ++RepeatCounter;
    if (RepeatCounter > (int32)Tracker->AnimAction->FrameRepeat) RepeatCounter = 0;
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A) { Terminate(); return; }
    bFirstFrame = false;
    if (!(A->Flags & Cru::FLG_FASTAREA)) { Terminate(); return; }
    if (RepeatCounter == 0)
    {
        const bool bResult = Tracker->StepFrom(FIntVector(A->X, A->Y, A->Z));
        Tracker->UpdateActorFlags();
        ++CurrentStep;
        if (!bResult)
        {
            if (Tracker->IsDone() || (Steps && CurrentStep >= Steps)) { Terminate(); return; }
            if (Tracker->IsBlocked() && !Tracker->AnimAction->HasFlags(AAF_UNSTOPPABLE)) { Terminate(); return; }
        }
        if (const FCruAnimFrame* Fr = Tracker->GetAnimFrame())
        {
            if (Fr->Sfx) GCruWorld->SfxQueue.Add({ ItemNum, (int32)Fr->Sfx, false, false });
            if (Fr->Hurty()) CruCombat::TookHit(*A);
            else if (Fr->IsCruAttack()) CruCombat::DoFireWeaponCru(*A, *Fr);
        }
        const FIntVector P = Tracker->GetPosition();
        if (P.X != A->X || P.Y != A->Y || P.Z != A->Z) GCruWorld->Move(*A, P.X, P.Y, P.Z);
        GCruWorld->SetFrame(*A, Tracker->GetFrame());
    }
    if (!(A->Flags & Cru::FLG_FASTAREA)) { Terminate(); return; }
}

void FActorAnimProcess::Terminate()
{
    if (FCruItem* A = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr)
        if (Tracker)
        {
            A->ActorFlags &= ~ACT_ANIMLOCK;
            if (Tracker->AnimAction->HasFlags(AAF_DESTROYACTOR)) GCruWorld->DestroyItem(*A);
        }
    FProcess::Terminate();
}

// ---------------------------------------------------------------- actor helpers
namespace CruActor
{
    ProcId DoAnim(FCruItem& A, uint32 Action, int32 Dir, uint32 Steps)
    {
        if (Dir < 0 || Dir > 16) return 0;
        if (Dir == DIR_CURRENT) Dir = A.Dir;
        const FCruAnimAction* Act = GCruWorld->AnimDat.Get(A.Shape, Action);
        if (Act && Act->DirCount == 8) Dir -= Dir % 2;
        if (Action == ReadySmall || Action == ReadyLarge || Action == CombatStandSmall || Action == CombatStandLarge || Action == FireSmall || Action == FireLarge || Action == ReloadSmall
            || Action == 42 || Action == 43 || Action == 46 || Action == 47)
            A.ActorFlags |= ACT_WEAPONREADY;
        else A.ActorFlags &= ~ACT_WEAPONREADY;
        if (Action == 40 || Action == 42 || Action == 43 || Action == 23 || Action == 24 || Action == 51 || Action == 52) A.ActorFlags |= ACT_KNEELING;
        else if (Action != 46 && Action != 47) A.ActorFlags &= ~ACT_KNEELING;
        // dodge bookkeeping used by NPC aim (Actor::doAnim)
        const uint32 Frameno = GCruKernel->GetTickNum() / 2;
        if (Action == Walk || Action == RetreatSmall || Action == RetreatLarge) { A.AttackMoveStartFrame = Frameno; A.AttackMoveTimeout = 120; A.AttackMoveDodgeFactor = 3; }
        else if (Action == Run || Action == 23 || Action == 24) { A.AttackMoveStartFrame = Frameno; A.AttackMoveTimeout = 120; A.AttackMoveDodgeFactor = 2; }
        else if (Action == 40 || Action == 41) { A.AttackMoveStartFrame = Frameno; A.AttackMoveTimeout = 75; A.AttackMoveDodgeFactor = 3; }
        return GCruKernel->AddProcess(new FActorAnimProcess(A, Action, Dir, Steps));
    }

    ProcId DoAnimAfter(FCruItem& A, uint32 Action, int32 Dir, ProcId WaitFor)
    {
        const ProcId P = DoAnim(A, Action, Dir);
        if (P && WaitFor) if (FProcess* Proc = GCruKernel->GetProcess(P)) Proc->WaitFor(WaitFor);
        return P;
    }

    bool TryAnim(FCruItem& A, uint32 Action, int32 Dir, uint32 Steps, FPathfindingState* State)
    {
        if (Dir < 0 || Dir > 16) return false;
        if (Dir == DIR_CURRENT) Dir = A.Dir;
        FAnimationTracker T;
        if (!T.Init(A, Action, Dir, State)) return false;
        uint32 Cur = 0;
        while (T.Step() && (!Steps || Cur >= Steps)) ++Cur;
        if (T.IsBlocked() && !T.AnimAction->HasFlags(AAF_UNSTOPPABLE)) return false;
        if (State) { T.UpdateState(*State); State->LastAnim = Action; State->Direction = Dir; }
        return true;
    }

    bool HasAnim(const FCruItem& A, uint32 Action) { return GCruWorld->AnimDat.Get(A.Shape, Action) != nullptr; }
    bool IsBusy(const FCruItem& A) { return GCruKernel->GetNumProcesses(A.Id, ACTOR_ANIM_PROC_TYPE) != 0; }
    bool IsInCombat(const FCruItem& A) { return (A.ActorFlags & ACT_INCOMBAT) != 0; }
    int32 DirToItemCentre(const FCruItem& A, const FCruItem& B) { const FIntVector Ca = GCruWorld->GetCentre(A), Cb = GCruWorld->GetCentre(B); return CruDirGetWorldDir(Cb.Y - Ca.Y, Cb.X - Ca.X, false); }

    ProcId TurnTowardDir(FCruItem& A, int32 TargetDir, ProcId PrevPid)
    {
        const int32 CurDir = A.Dir;
        if (TargetDir == CurDir || TargetDir == DIR_CURRENT) return 0;
        const bool bCombat = IsInCombat(A) && !(A.ActorFlags & ACT_COMBATRUN);
        const uint32 StandAnim = bCombat ? CombatStandSmall : Stand;
        const int32 StepDelta = CruDirShorterTurnDelta(CurDir, TargetDir);
        const FCruAnimAction* Act = GCruWorld->AnimDat.Get(A.Shape, StandAnim);
        bool b16 = Act && Act->DirCount == 16;
        if (TargetDir % 2 != CurDir % 2) b16 = true;
        ProcId AnimPid = 0;
        bool bDone = false;
        for (int32 D = CruDirTurnByDelta(CurDir, StepDelta, b16); !bDone; D = CruDirTurnByDelta(D, StepDelta, b16))
        {
            if (D == TargetDir) bDone = true;
            AnimPid = DoAnim(A, StandAnim, D);
            if (PrevPid) if (FProcess* P = GCruKernel->GetProcess(AnimPid)) P->WaitFor(PrevPid);
            PrevPid = AnimPid;
        }
        return AnimPid;
    }

    bool CanSeeControlledActor(const FCruItem& A, bool bForCombat)
    {
        const FCruItem* C = GCruWorld->GetItem(GCruWorld->ControlledNpc);
        if (!C || !GCruEngine || !GCruEngine->IsOnScreen(A)) return false;
        const int32 DirTo = DirToItemCentre(A, *C);
        int32 Cur = A.Dir;
        if (bForCombat && (A.LastAnim == LookLeft || A.LastAnim == LookRight) && A.AnimFrame > 1)
        {
            bool bLeft = A.LastAnim == LookLeft ? (Cur != 8 && Cur != 10 && Cur != 0xC) : !(Cur != 8 && Cur != 10 && Cur != 0xC);
            Cur = CruDirTurnByDelta(Cur, bLeft ? -4 : 4, true);
        }
        if (DirTo == Cur || DirTo == CruDirOneLeft(Cur, true) || DirTo == CruDirOneRight(Cur, true) || DirTo == CruDirTurnByDelta(Cur, 2, true) || DirTo == CruDirTurnByDelta(Cur, -2, true))
        {
            const int32 R = CruCombat::GetRangeIfVisible(A, *C);
            if (GCruCombatDebug) UE_LOG(LogCru, Display, TEXT("cansee: actor %d dir %d -> avatar dir %d range %d"), A.Id, A.Dir, DirTo, R);
            return R > 0;
        }
        return false;
    }

    uint16 SetActivity(FCruItem& A, int32 Activity)
    {
        if (A.bDead || GCruWorld->ControlledNpc == A.Id || (A.ActorFlags & ACT_WEAPONREADY) || Activity == 0) return 0;
        if (A.CurrentActivity == Activity || (IsInCombat(A) && Activity != 0xC)) return 0;
        A.LastActivity = A.CurrentActivity; A.CurrentActivity = Activity;
        if (IsInCombat(A)) CruCombat::ClearInCombat(A);
        if (!(A.Flags & Cru::FLG_FASTAREA)) return 0;
        static const uint16 Kill[] = { ACTOR_ANIM_PROC_TYPE, PATHFINDER_PROC_TYPE, PACE_PROC_TYPE, LOITER_PROC_TYPE, ATTACK_PROC_TYPE, GUARD_PROC_TYPE, SURRENDER_PROC_TYPE };
        for (uint16 T : Kill) GCruKernel->KillProcesses(A.Id, T, true);
        switch (Activity)
        {
        case 1: return DoAnim(A, Stand, DIR_CURRENT);
        case 2: return GCruKernel->AddProcess(new FLoiterProcess(A));
        case 3: return GCruKernel->AddProcess(new FPaceProcess(A));
        case 4: case 6: break;
        case 7: if (A.LastActivity != 7) return GCruKernel->AddProcess(new FSurrenderProcess(A)); break;
        case 8: return GCruKernel->AddProcess(new FGuardProcess(A));
        case 5: case 9: case 0xA: case 0xB: case 0xC: CruCombat::SetInCombatCru(A, Activity); return 0;
        case 0x70: return SetActivity(A, A.DefaultActivity[0]);
        case 0x71: return SetActivity(A, A.DefaultActivity[1]);
        case 0x72: return SetActivity(A, A.DefaultActivity[2]);
        default: UE_LOG(LogCru, Display, TEXT("actor %d: invalid activity %d"), A.Id, Activity); return DoAnim(A, Stand, DIR_CURRENT);
        }
        return 0;
    }
}

// ---------------------------------------------------------------- pathfinder
void FCruPathfinderProcess::Setup(FCruItem& A)
{
    ItemNum = A.Id; Type = PATHFINDER_PROC_TYPE;
    bRandomFlag = FMath::RandBool();
    CurrentDistance = FMath::Max(FMath::Abs(A.X - Target.X), FMath::Abs(A.Y - Target.Y));
    const FCruShapeInfo& Si = GCruWorld->Info(A);
    bSolidObject = Si.IsSolid() && Si.Z > 0;
    A.ActorFlags |= ACT_PATHFINDING;
    if (CruActor::IsInCombat(A) && (A.ActorFlags & ACT_WEAPONREADY)) CruActor::DoAnim(A, UnreadySmall, DIR_CURRENT);
}

FCruPathfinderProcess::FCruPathfinderProcess(FCruItem& A, const FIntVector& InTarget, int32 InMaxSteps, int32 InStop, bool bInTurn)
    : Target(InTarget), MaxSteps(InMaxSteps), StopDistance(InStop), bTurnAtEnd(bInTurn) { Setup(A); }
FCruPathfinderProcess::FCruPathfinderProcess(FCruItem& A, const FCruItem& T, int32 InMaxSteps, int32 InStop, bool bInTurn)
    : Target(T.X, T.Y, T.Z), TargetItem(T.Id), MaxSteps(InMaxSteps), StopDistance(InStop), bTurnAtEnd(bInTurn) { Setup(A); }

void FCruPathfinderProcess::Terminate()
{
    if (FCruItem* A = GCruWorld ? GCruWorld->GetItem(ItemNum) : nullptr)
        if (!A->bDead)
        {
            A->ActorFlags &= ~ACT_PATHFINDING;
            ProcId TurnProc = 0;
            if (bTurnAtEnd)
            {
                int32 DestDir = DIR_CURRENT;
                if (TargetItem == 0) DestDir = CruDirGetWorldDir(Target.Y - A->Y, Target.X - A->X, false);
                else if (const FCruItem* T = GCruWorld->GetItem(TargetItem)) DestDir = CruDirGetWorldDir(T->Y - A->Y, T->X - A->X, false);
                if (DestDir != DIR_CURRENT) TurnProc = CruActor::TurnTowardDir(*A, DestDir);
            }
            if (!TurnProc && bNoShotAvailable) CruActor::DoAnim(*A, CruActor::IsInCombat(*A) ? CombatStandSmall : Stand, DIR_CURRENT);
        }
    FProcess::Terminate();
}

int32 FCruPathfinderProcess::NextDirFromPoint(FIntVector& NpcPt)
{
    const int32 DirToTarget = CruDirGetWorldDir(Target.Y - NpcPt.Y, Target.X - NpcPt.X, false);
    FCruItem* Npc = GCruWorld->GetItem(ItemNum);
    if (!Npc) return DIR_CURRENT;
    const int32 MaxDiff = FMath::Max(FMath::Abs(NpcPt.X - Target.X), FMath::Abs(NpcPt.Y - Target.Y));
    if (MaxDiff < CurrentDistance)
    {
        CurrentDistance = MaxDiff;
        FPathfindingState St; St.Point = NpcPt; St.Direction = DirToTarget; St.bCombat = CruActor::IsInCombat(*Npc);
        if (CruActor::TryAnim(*Npc, CruActor::IsInCombat(*Npc) ? Walk : AdvanceSmall, DirToTarget, 0, &St)) bDirectPathBlocked = false;
    }
    static const int8 Off1[8] = { 0, 2, -2, 4, -4, 6, -6, -8 }, Off2[8] = { 0, -2, 2, -4, 4, -6, 6, -8 };
    int32 StartOff = 0; int32 DirTable[8]; int32 NextTable[8];
    if (!bDirectPathBlocked)
    {
        for (int32 i = 0; i < 8; ++i) DirTable[i] = CruDirTurnByDelta(DirToTarget, bRandomFlag ? Off1[i] : Off2[i], true);
    }
    else
    {
        int32 DirOffset = bRandomFlag ? (bNextTurn ? 2 : -2) : (bNextTurn ? -2 : 2);
        NextTable[0] = CruDirTurnByDelta(NextDir, DirOffset + 8, true);
        for (int32 i = 1; i < 8; ++i) NextTable[i] = CruDirTurnByDelta(NextTable[i - 1], DirOffset, true);
        StartOff = 1;
    }
    FPathfindingState St; bool bOk = true; int32 i;
    for (i = StartOff; i < 8; ++i)
    {
        if (bDirectPathBlocked && i == 2) continue;
        if (bDirectPathBlocked && i == 7) break;
        NextDir2 = bDirectPathBlocked ? NextTable[i] : DirTable[i];
        St.Point = NpcPt; St.Direction = NextDir2; St.bCombat = CruActor::IsInCombat(*Npc);
        bOk = CruActor::TryAnim(*Npc, CruActor::IsInCombat(*Npc) ? Walk : AdvanceSmall, NextDir2, 0, &St);
        if (bSolidObject && bOk) { bTurnAtEnd = true; return DIR_INVALID; }
        if (StopDistance && FMath::Max(FMath::Abs(Target.X - St.Point.X), FMath::Abs(Target.Y - St.Point.Y)) <= StopDistance) { bTurnAtEnd = true; return DIR_INVALID; }
        if (bOk) break;
    }
    if (!bOk) return DIR_CURRENT;
    if (NextDir2 != DirToTarget && !bDirectPathBlocked) { bDirectPathBlocked = true; bNextTurn = (i % 2) != 0; }
    NpcPt = St.Point;
    return NextDir2;
}

void FCruPathfinderProcess::Run()
{
    FCruItem* Npc = GCruWorld->GetItem(ItemNum);
    if (!Npc || !(Npc->Flags & Cru::FLG_FASTAREA)) return;
    if (Npc->bDead) { Terminate(); return; }
    if (bDir16Flag) { Terminate(); return; }
    if (TargetItem && bSolidObject) if (const FCruItem* T = GCruWorld->GetItem(TargetItem)) Target = FIntVector(T->X, T->Y, T->Z);
    FIntVector NpcPt(Npc->X, Npc->Y, Npc->Z);
    if (Target.X == NpcPt.X && Target.Y == NpcPt.Y) { Terminate(); return; }
    const int32 Last = NextDir;
    NextDir = NextDirFromPoint(NpcPt);
    LastDir = Last;
    if (NextDir == DIR_CURRENT) { Terminate(); return; }
    if (NextDir == DIR_INVALID) bDir16Flag = true;
    else if (CurrentStep >= MaxSteps) { Terminate(); return; }
    const int32 NewDir = bDir16Flag ? NextDir2 : NextDir;
    const ProcId TurnPid = CruActor::TurnTowardDir(*Npc, NewDir);
    const ProcId AnimPid = CruActor::DoAnim(*Npc, CruActor::IsInCombat(*Npc) ? AdvanceSmall : Walk, NewDir);
    if (TurnPid) if (FProcess* P = GCruKernel->GetProcess(AnimPid)) P->WaitFor(TurnPid);
    WaitFor(AnimPid);
    ++CurrentStep;
}

// ---------------------------------------------------------------- loiter / pace / guard
FLoiterProcess::FLoiterProcess(FCruItem& A, int32 InCount) : FProcess(A.Id, LOITER_PROC_TYPE), Count(InCount)
{
    if (FProcess* Prev = GCruKernel->FindProcess(ItemNum, Type)) if (Prev != this) Prev->Terminate();
    if (FProcess* Pf = GCruKernel->FindProcess(ItemNum, PATHFINDER_PROC_TYPE)) Pf->Terminate();
}

void FLoiterProcess::Run()
{
    if (Count == 0) { Terminate(); return; }
    if (Count > 0) --Count;
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A || A->bDead) { Terminate(); return; }
    FIntVector P(A->X + 32 * FMath::RandRange(-10, 10), A->Y + 32 * FMath::RandRange(-10, 10), A->Z);
    FProcess* Pf = new FCruPathfinderProcess(*A, P, 0xC, 0x80, false);
    GCruKernel->AddProcess(Pf);
    const bool bIdle1 = CruActor::HasAnim(*A, Idle1), bIdle2 = CruActor::HasAnim(*A, Idle2);
    if ((bIdle1 || bIdle2) && FMath::RandRange(0, 2) == 0)
    {
        const uint32 Idle = !bIdle1 ? Idle2 : (!bIdle2 ? Idle1 : (FMath::RandBool() ? Idle1 : Idle2));
        const ProcId IdlePid = CruActor::DoAnim(*A, Idle, DIR_CURRENT);
        if (FProcess* Ip = GCruKernel->GetProcess(IdlePid)) { Ip->WaitFor(Pf->Pid); WaitFor(IdlePid); }
    }
    else
    {
        FDelayProcess* Dp = new FDelayProcess(30 * FMath::RandRange(4, 7));
        GCruKernel->AddProcess(Dp);
        Dp->WaitFor(Pf->Pid);
        WaitFor(Dp->Pid);
    }
}

FPaceProcess::FPaceProcess(FCruItem& A) : FProcess(A.Id, PACE_PROC_TYPE)
{
    if (FProcess* Prev = GCruKernel->FindProcess(ItemNum, Type)) if (Prev != this) Prev->Terminate();
}

void FPaceProcess::Run()
{
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A || A->bDead) { Terminate(); return; }
    if (!(A->Flags & Cru::FLG_FASTAREA)) return;
    const uint16 Act1 = A->DefaultActivity[1];
    if (Act1 != 0 && A->CurrentActivity != Act1 && CruActor::CanSeeControlledActor(*A, false)) { CruActor::SetActivity(*A, Act1); return; }
    if (CruActor::IsBusy(*A)) return;
    if (CruActor::TryAnim(*A, Walk, A->Dir))
    {
        Counter = 0;
        WaitFor(CruActor::DoAnim(*A, Walk, A->Dir));
    }
    else
    {
        if (++Counter > 1)
        {
            const uint32 S = A->Shape;
            if (S == 0x2F5 || S == 0x2F7 || S != 0x2F6 || S == 0x344 || S == 0x597) CruActor::SetActivity(*A, 7); else CruActor::SetActivity(*A, 5);
            return;
        }
        const ProcId StandPid = CruActor::DoAnim(*A, Stand, A->Dir);
        const ProcId TurnPid = CruActor::TurnTowardDir(*A, CruDirInvert(A->Dir), StandPid);
        FDelayProcess* Wp = new FDelayProcess(60);
        GCruKernel->AddProcess(Wp);
        Wp->WaitFor(TurnPid ? TurnPid : StandPid);
        WaitFor(Wp->Pid);
    }
}

FGuardProcess::FGuardProcess(FCruItem& A) : FProcess(A.Id, GUARD_PROC_TYPE) {}

void FGuardProcess::Run()
{
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    if (!A || A->bDead) { Terminate(); return; }
    if (CruActor::IsBusy(*A)) return;
    if (!CruActor::CanSeeControlledActor(*A, false))
    {
        if (FMath::RandBool())
        {
            FDelayProcess* Dp = new FDelayProcess(30 * FMath::RandRange(1, 3));
            GCruKernel->AddProcess(Dp);
            WaitFor(Dp->Pid);
        }
        else
        {
            const ProcId Ap = CruActor::DoAnim(*A, FMath::RandBool() ? LookLeft : LookRight, DIR_CURRENT);
            CruActor::DoAnimAfter(*A, Stand, DIR_CURRENT, Ap);
        }
        return;
    }
    CruActor::SetActivity(*A, 5);
}

// ---------------------------------------------------------------- surrender
FSurrenderProcess::FSurrenderProcess(FCruItem& A) : FProcess(A.Id, CruAnim::SURRENDER_PROC_TYPE)
{
    if (!(A.ActorFlags & CruAnim::ACT_SURRENDERED)) CruActor::DoAnim(A, 57, A.Dir);   // Animation::surrender
}

void FSurrenderProcess::Run()
{
    FCruItem* A = GCruWorld->GetItem(ItemNum);
    const FCruItem* Main = GCruWorld->GetItem(Cru::kMainActorId);
    if (!A || A->bDead || !Main) { Terminate(); return; }
    if (!(A->Flags & Cru::FLG_FASTAREA) || CruActor::IsBusy(*A)) return;
    A->ActorFlags |= CruAnim::ACT_SURRENDERED;
    const int32 Dir = CruActor::DirToItemCentre(*A, *Main);
    if (A->Dir != Dir) { if (const ProcId P = CruActor::TurnTowardDir(*A, Dir)) WaitFor(P); return; }
    if (bPlayedSound || CruCombat::GetRangeIfVisible(*A, *Main) == 0) return;
    bPlayedSound = true;
    static const int16 SUIT[] = { 0xe9, 0xe0, 0xeb, 0xe1, 0xea };
    static const int16 CHEMSUIT[] = { 0xb4, 0xc5, 0xc6, 0xe8 };
    static const int16 SCIENTIST[] = { 0xe3, 0xe4, 0xec, 0xf6 };
    static const int16 HARDHAT[] = { 0xde, 0xdf, 0x8a, 0x8b };
    static const int16 FEMALE[] = { 0xd6, 0xff, 0xd7 };
    auto Pick = [](const int16* Arr, int32 N) { return (int32)Arr[FMath::RandRange(0, N - 1)]; };
    int32 Sfx = -1;
    switch (A->Shape)
    {
    case 0x2f7: Sfx = Pick(SUIT, 5); break;
    case 0x2f5: Sfx = Pick(HARDHAT, 4); break;
    case 0x2f6: Sfx = Pick(CHEMSUIT, 4); break;
    case 0x344: Sfx = Pick(SCIENTIST, 4); break;
    case 0x597: Sfx = Pick(FEMALE, 3); break;
    default: break;
    }
    if (Sfx != -1) GCruWorld->SfxQueue.Add({ ItemNum, Sfx, false, false });
}
