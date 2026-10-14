#include "CrusaderUsecodeTest.h"
#include "Usecode/CruEngine.h"
#include "Usecode/CruWorld.h"
#include "Usecode/Kernel.h"
#include "Usecode/UCMachine.h"
extern bool GCruCombatDebug;

FString UCrusaderUsecodeTest::RunHeadless(const FString& DataDir, int32 Map, int32 Ticks, FVector Start, FVector End, int32 WalkTicks, const FString& TraceClasses, int32 UseAtTick, int32 FireAtTick, int32 FireDir, int32 WeaponShape, bool bInfiniteAmmo)
{
    FCruEngine Engine;
    GCruCombatDebug = FireAtTick > 0;
    if (!Engine.Init(DataDir)) return TEXT("init failed");
    TArray<FString> Classes; TraceClasses.ParseIntoArray(Classes, TEXT("x"));
    for (const FString& C : Classes) if (!C.IsEmpty()) Engine.Machine->TraceClasses.Add((uint16)FCString::Atoi(*C));
    int32 Changes = 0;
    Engine.World->OnItemChanged = [&](const FCruItem& It, ECruItemChange C)
    {
        ++Changes;
        if (It.Id == Cru::kMainActorId) return;
        static const TCHAR* Names[] = { TEXT("moved"), TEXT("shape/frame"), TEXT("flags"), TEXT("created"), TEXT("destroyed") };
        UE_LOG(LogCru, Display, TEXT("tick %u item %d (%s) src %d: %s -> shape %d frame %d at (%d,%d,%d) flags %04X"),
            Engine.Kernel->GetTickNum(), It.Id, *Engine.Usecode.GetClassName(It.Shape), It.SourceIndex, Names[(int32)C], It.Shape, It.Frame, It.X, It.Y, It.Z, It.Flags);
    };
    if (!Engine.LoadMap(Map)) return TEXT("map load failed");
    Engine.SpawnAvatar((int32)Start.X, (int32)Start.Y, (int32)Start.Z);
    Engine.bInfiniteAmmo = bInfiniteAmmo;
    UE_LOG(LogCru, Display, TEXT("headless: %d items, %d processes after load"), Engine.World->NumItems(), Engine.Kernel->NumProcesses());
    UE_LOG(LogCru, Display, TEXT("headless: avatar after load: %s"), *Engine.AvatarStatus());
    for (int32 T = 0; T < Ticks; ++T)
    {
        const float A = WalkTicks > 0 ? FMath::Clamp((float)T / WalkTicks, 0.f, 1.f) : 1.f;
        const FVector P = FMath::Lerp(Start, End, A);
        Engine.SetAvatarPosition((int32)P.X, (int32)P.Y, (int32)P.Z, FireDir);
        if (WeaponShape && T == FMath::Max(FireAtTick - 1, 1)) { Engine.SetAvatarWeapon((uint32)WeaponShape); UE_LOG(LogCru, Display, TEXT("tick %u: weapon cheat -> %s"), Engine.Kernel->GetTickNum(), *Engine.AvatarStatus()); }
        if (FireAtTick > 0 && T >= FireAtTick && (T - FireAtTick) % 20 == 0) { if (Engine.AvatarFire()) UE_LOG(LogCru, Display, TEXT("tick %u: FIRE (avatar hp %d) %s"), Engine.Kernel->GetTickNum(), Engine.AvatarHp(), *Engine.AvatarStatus()); }
        if (T == 120 || T == Ticks - 1) UE_LOG(LogCru, Display, TEXT("tick %u: avatar %s"), Engine.Kernel->GetTickNum(), *Engine.AvatarStatus());
        if (UseAtTick > 0 && T == UseAtTick) { UE_LOG(LogCru, Display, TEXT("tick %u: USE pressed"), Engine.Kernel->GetTickNum()); Engine.UseNearest(); }
        if (Engine.bMoviePlaying) { UE_LOG(LogCru, Display, TEXT("tick %u: cutscene %s skipped (headless)"), Engine.Kernel->GetTickNum(), *Engine.PendingMovie); Engine.MovieFinished(); }
        Engine.RunOneTick();
        if (Engine.bPendingPawnTeleport) { Engine.bPendingPawnTeleport = false; if (const FCruItem* Av = Engine.World->GetItem(Engine.World->AvatarId)) { Start = End = FVector(Av->X, Av->Y, Av->Z); WalkTicks = 0; } }
        if (Engine.PendingMapSwitch) { UE_LOG(LogCru, Display, TEXT("tick %u: map switch to %d requested (carry-over %d items)"), Engine.Kernel->GetTickNum(), Engine.PendingMapSwitch, GCruCarryOver.Items.Num()); Engine.PendingMapSwitch = 0; }
        for (const FCruWorld::FSfxRequest& S : Engine.World->SfxQueue)
            UE_LOG(LogCru, Display, TEXT("tick %u sfx %s %d for item %d"), Engine.Kernel->GetTickNum(), S.bStop ? TEXT("stop") : TEXT("play"), S.Sfx, S.Item);
        Engine.World->SfxQueue.Reset();
    }
    int32 Live = 0; for (const FProcess* P : Engine.Kernel->GetProcesses()) if (!P->IsTerminated()) ++Live;
    const FString Summary = FString::Printf(TEXT("ran %d ticks, %llu ops, %d item changes, %d live processes, %d unimplemented intrinsics hit"),
        Ticks, Engine.Machine->OpCount, Changes, Live, Engine.Machine->UnimplementedReported.Num());
    UE_LOG(LogCru, Display, TEXT("headless: %s"), *Summary);
    return Summary;
}
